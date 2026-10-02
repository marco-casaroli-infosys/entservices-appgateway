# WebSocket Traffic Control During App Hibernation — Detailed Design

## 1. Overview

This document describes the design for pausing and resuming traffic when an application enters the `HIBERNATED` lifecycle state. It applies **exclusively to the Lifecycle 2 (AppManagers) architecture** — legacy/non-AppManagers paths are unaffected.

The approach is **drop-based**: all WebSocket messages (inbound and outbound) for a hibernated application are silently discarded. No queuing or replay occurs. Normal traffic resumes as soon as the application transitions out of the `HIBERNATED` state.

---

## 2. Background and Terminology

| Term | Meaning in this codebase |
|---|---|
| **App container** | The sandboxed process running the app, managed by `IRuntimeManager` |
| **WebSocket connection** | A persistent TCP connection between the app container and `AppGatewayResponderImplementation` on `127.0.0.1:3473` |
| **`connectionId`** | Unique integer identifying one WebSocket connection inside `AppGatewayResponderImplementation` |
| **Hibernation** | OS-level freeze of the container process (`CGROUP_FREEZE`). The TCP state survives in the kernel; the user-space reader/writer inside the container is frozen. |
| **`AppGatewayResponder`** | The `IAppGatewayResponder` interface, implemented by `AppGatewayResponderImplementation`, used by other plugins to push responses and events to an app |
| **`IAppGatewayAppSessionGuard`** | New COM interface added to `IAppGateway.h`; exposes `SuspendTraffic` / `ResumeTraffic` per appId |

---

## 3. Problem Statement

When a container is hibernated:

- The **WebSocket TCP connection remains open** because the kernel holds the socket independently of user space.
- **Inbound messages** from the frozen app arrive and would be dispatched as if the app were active, wasting work and potentially returning responses into a frozen TCP window.
- **Outbound messages** (responses / events pushed via `AppGatewayResponder`) would be written to a send-buffer the frozen container cannot drain — messages would be silently lost.

The solution is to **drop both directions** while hibernated. The app restarts its interactions after resuming, so message ordering across the hibernation boundary is not meaningful.

---

## 4. Architectural Context

```
┌─────────────────────────────────────────────────────────────┐
│                     AppGatewayCommon                        │
│                                                             │
│  ┌──────────────────────────────────────────────────────┐   │
│  │  AppGatewayResponderImplementation                   │   │
│  │  (implements IAppGatewayResponder +                  │   │
│  │             IAppGatewayAppSessionGuard)              │   │
│  │                                                      │   │
│  │  DispatchWsMsg()   ← inbound drop gate               │   │
│  │  Respond()         ← outbound drop gate              │   │
│  │  Emit()            ← outbound drop gate              │   │
│  │  Request()         ← outbound drop gate              │   │
│  │                                                      │   │
│  │  PausedAppsRegistry  (thread-safe unordered_set)     │   │
│  │    SuspendTraffic(appId)                             │   │
│  │    ResumeTraffic(appId)                              │   │
│  └──────────────────────────────────────────────────────┘   │
│                         ▲                                   │
│                         │                                   │
│             IAppGatewayAppSessionGuard(COM-RPC)             │
│  ┌──────────────────────┴───────────────────────────────┐   │
│  │  LifecycleDelegate                                   │   │
│  │  OnAppLifecycleStateChanged()                        │   │
│  │    HIBERNATED  → SessionGuard.SuspendTraffic         │   │
│  │    ← HIBERNATED → SessionGuard.ResumeTraffic         │   │
│  └──────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
         ▲                              ▲
         │ WebSocket TCP                │ COM-RPC
         │                              │
┌────────┴──────────┐     ┌─────────────┴────────────┐
│  App Container    │     │  LifecycleManager /      │
│  (frozen during   │     │  AppGatewayCommon /      │
│   hibernation)    │     │  Other plugins           │
└───────────────────┘     └──────────────────────────┘
```

---

## 5. Design Components

### 5.1 `IAppGatewayAppSessionGuard` (new interface)

Added to `entservices-apis/apis/AppGateway/IAppGateway.h` with ID `ID_APP_GATEWAY_APP_SESSION_GUARD`.

```cpp
// entservices-apis/apis/AppGateway/IAppGateway.h
struct EXTERNAL IAppGatewayAppSessionGuard : virtual public Core::IUnknown
{
    enum { ID = ID_APP_GATEWAY_APP_SESSION_GUARD };

    // Pauses all WebSocket I/O for the named application.
    // Incoming messages are silently dropped; outgoing messages are discarded.
    // No errors are surfaced to the application.
    virtual Core::hresult SuspendTraffic(const string& appId) = 0;

    // Resumes normal WebSocket I/O for the named application.
    virtual Core::hresult ResumeTraffic(const string& appId) = 0;
};
```

`ID_APP_GATEWAY_APP_SESSION_GUARD = ID_APP_GATEWAY + 6` is registered in `entservices-apis/apis/Ids.h`.

---

### 5.2 `PausedAppsRegistry` inner class (in `AppGatewayResponderImplementation`)

A private inner class inside `AppGatewayResponderImplementation` that maintains a thread-safe set of paused appIds. It exposes `Pause`, `Resume`, and `IsPaused` operations. All methods are internally synchronised; callers do not need to hold any external lock.

---

### 5.3 `AppGatewayResponderImplementation` — Drop Gates

`AppGatewayResponderImplementation` now also inherits `IAppGatewayAppSessionGuard`. Drop checks against `PausedAppsRegistry` are inserted at the four traffic entry points:

- **`DispatchWsMsg`** (inbound): if the sender's appId is paused, the message is silently discarded before reaching the resolver.
- **`Respond`, `Emit`, `Request`** (outbound): if the target appId is paused, the operation returns `Core::ERROR_NONE` immediately without submitting a worker-pool job. Jobs accepted immediately before suspension re-check the paused state when they execute and are dropped if the application has since hibernated.

`SuspendTraffic` and `ResumeTraffic` simply delegate to `PausedAppsRegistry::Pause` and `Resume` respectively.

---

### 5.4 `LifecycleDelegate` — Hibernation Trigger

`LifecycleDelegate` receives `OnAppLifecycleStateChanged` from `ILifecycleManagerState`. It holds an `IAppGatewayAppSessionGuard*` member (`mSessionGuard`) and drives pause/resume around the `Lifecycle2.onStateChanged` dispatch with the following ordering rules:

- `ResumeTraffic` is called **before** `Dispatch("Lifecycle2.onStateChanged", ...)` when leaving hibernation or starting a new session. This ensures the state-change event itself is not dropped by a still-paused responder.
- When entering hibernation, the state-change event is delivered synchronously before `SuspendTraffic` is called. This guarantees the event reaches the responder while the channel is still open.

`SetSessionGuard(IAppGatewayAppSessionGuard*)` is the thread-safe setter called by `AppGatewayCommon` during `Initialize` (to set) and `Deinitialize` (to clear with `nullptr`).

#### 5.4.1 Stale Suspension Cleanup on New Session

A corner case must be handled when a container crashes (or is killed) while in the `HIBERNATED` state, or when it fails to issue the lifecycle state transition out of `HIBERNATED`:

- `SuspendTraffic(appId)` was called when the app entered `HIBERNATED`.
- No matching `ResumeTraffic(appId)` is ever received because the state-change notification that would trigger it never arrives.
- The `appId` would remain permanently in `PausedAppsRegistry`.
- When the same app is relaunched as a new session, its `INITIALIZING` notification arrives — but traffic would be silently dropped for the entire lifetime of the new session.

To handle this, `ResumeTraffic(appId)` is called unconditionally on every `INITIALIZING` transition. `INITIALIZING` always marks the start of a new session for that `appId`, so this both clears any stale suspension and is a benign no-op when none exists.

---

### 5.5 `AppGatewayCommon` — Wiring

During `Initialize()`, `AppGatewayCommon` queries `IAppGatewayAppSessionGuard` from `AppGatewayResponderImplementation` (via the `APP_GATEWAY_CALLSIGN`) and injects it into `LifecycleDelegate` via `SetSessionGuard`. This runs on the Lifecycle 2 path only (`ConfigUtils::useAppManagers()`).

During `Deinitialize()`, `SetSessionGuard(nullptr)` is called before delegate cleanup to prevent in-flight callbacks from using a stale pointer.

---

### 5.6 Design Decisions
#### Why is there no `IsSuspended()` query method in IAppGatewayAppSessionGuard?

`IAppGatewayAppSessionGuard` is deliberately a **command-only interface**. No query method (e.g. `IsSuspended(appId, bool& out)`) is provided, for two reasons:

1. **The caller already knows the state.** `AppGatewayCommon` is the sole driver of suspend/resume — it calls `SuspendTraffic` in response to a `HIBERNATED` notification it received, so it already knows whether a given app is suspended. A query would duplicate information the caller already holds.

2. **TOCTOU race condition.** Any code that checks `IsSuspended()` and then acts on the result is inherently racy in a multi-threaded environment (Time-Of-Check to Time-Of-Use). The state could change between the check and the action. The current design avoids this entirely: `SuspendTraffic`/`ResumeTraffic` are fire-and-forget commands, and the drop logic inside `AppGatewayResponderImplementation` evaluates `PausedAppsRegistry::IsPaused()` atomically under a mutex at each message boundary.

If suspended-state visibility is ever needed for diagnostics, the appropriate mechanism is a JSON-RPC diagnostic method exposed on the `AppGateway` plugin itself, not a query on this internal COM interface.

---

## 6. Data Flow Diagrams

### 6.1 App enters HIBERNATED — traffic paused

```
ILifecycleManagerState::INotification::OnAppLifecycleStateChanged
   (appId="TestApp", newState=HIBERNATED)
        │
        ▼
LifecycleDelegate::HandleLifecycleUpdate()
        │
        ▼
Dispatch("Lifecycle2.onStateChanged")        ← event delivered while still unpaused
        │
        ▼
IAppGatewayAppSessionGuard::SuspendTraffic("TestApp")   ← suspend AFTER dispatch
   → PausedAppsRegistry.Pause("TestApp")


─── Inbound path (app → AppGatewayResponder) ─────────────────
App message → AppGatewayResponderImplementation::DispatchWsMsg()
        │
        ▼
PausedAppsRegistry::IsPaused("TestApp") → true
        │
        ▼
Message DROPPED


─── Outbound path (AppGatewayResponder → app) ────────────────
AppGatewayCommon calls Respond(ctx{appId="TestApp"}, payload)
        │
        ▼
AppGatewayResponderImplementation::Respond()
        │
        ▼
PausedAppsRegistry::IsPaused("TestApp") → true
        │
        ▼
Message DROPPED
```

### 6.2 App resumes from HIBERNATED — traffic restored

```
ILifecycleManagerState::INotification::OnAppLifecycleStateChanged
   (appId="TestApp", oldState=HIBERNATED, newState=ACTIVE)
        │
        ▼
LifecycleDelegate::HandleLifecycleUpdate()
        │
        ▼
IAppGatewayAppSessionGuard::ResumeTraffic("TestApp")   ← resume BEFORE dispatch
   → PausedAppsRegistry.Resume("TestApp")
        │
        ▼
Dispatch("Lifecycle2.onStateChanged")        ← event delivered on unpaused channel
        │
        ▼
All subsequent WebSocket I/O for "TestApp" proceeds normally
```

### 6.3 Container crashes while HIBERNATED — stale suspension cleared on relaunch

```
[Previous session]
ILifecycleManagerState::INotification::OnAppLifecycleStateChanged
   (appId="TestApp", newState=HIBERNATED)
        │
        ▼
LifecycleDelegate::HandleLifecycleUpdate()
        │
        ▼
IAppGatewayAppSessionGuard::SuspendTraffic("TestApp")
   → PausedAppsRegistry.Pause("TestApp")

*** Container process crashes / no HIBERNATED→X transition is ever received ***

[New session launched]
ILifecycleManagerState::INotification::OnAppLifecycleStateChanged
   (appId="TestApp", newState=INITIALIZING)
        │
        ▼
LifecycleDelegate::HandleLifecycleUpdate()
        │
        ▼
IAppGatewayAppSessionGuard::ResumeTraffic("TestApp")   ← stale entry cleared
   → PausedAppsRegistry.Resume("TestApp")
        │
        ▼
New session's WebSocket traffic proceeds normally
```

---

## 7. Sequence Diagrams

### 7.1 Normal hibernation and resume

```mermaid
sequenceDiagram
    participant LM as ILifecycleManagerState
    participant LD as LifecycleDelegate
    participant SG as IAppGatewayAppSessionGuard
    participant AGR as AppGatewayResponderImpl
    participant App as App Container

    Note over App,AGR: App is ACTIVE, WebSocket connected
    LM->>LD: OnAppLifecycleStateChanged(HIBERNATED)
    LD->>AGR: Dispatch(Lifecycle2.onStateChanged) [app still unpaused]
    Note over AGR: Event delivered normally
    LD->>SG: SuspendTraffic("TestApp") [suspend AFTER dispatch]
    SG->>AGR: PausedAppsRegistry.Pause("TestApp")

    Note over App,AGR: Inbound message arrives while frozen
    App->>AGR: DispatchWsMsg(method, params)
    AGR->>AGR: IsPaused("TestApp") → true
    Note over AGR: Message DROPPED

    Note over AGR: Outbound event generated while hibernated
    AGR->>AGR: Respond(ctx{appId=TestApp}, payload)
    AGR->>AGR: IsPaused("TestApp") → true
    Note over AGR: Message DROPPED

    Note over App,LM: Container resumed
    LM->>LD: OnAppLifecycleStateChanged(ACTIVE, oldState=HIBERNATED)
    LD->>SG: ResumeTraffic("TestApp") [resume BEFORE dispatch]
    SG->>AGR: PausedAppsRegistry.Resume("TestApp")

    Note over App,AGR: Normal WebSocket traffic resumes
    App->>AGR: DispatchWsMsg(method, params)
    AGR->>AGR: IsPaused("TestApp") → false
    AGR->>App: Response delivered normally
```

### 7.2 Container crashes while HIBERNATED — stale suspension cleared on relaunch

```mermaid
sequenceDiagram
    participant LM as ILifecycleManagerState
    participant LD as LifecycleDelegate
    participant SG as IAppGatewayAppSessionGuard
    participant AGR as AppGatewayResponderImpl
    participant App as App Container

    Note over App,LM: Previous session — app enters HIBERNATED
    LM->>LD: OnAppLifecycleStateChanged(HIBERNATED)
    LD->>SG: SuspendTraffic("TestApp")
    SG->>AGR: PausedAppsRegistry.Pause("TestApp")

    Note over App: Container process CRASHES
    Note over LM: No HIBERNATED→X notification is ever issued

    Note over App,LM: New session — app relaunched
    LM->>LD: OnAppLifecycleStateChanged(INITIALIZING)
    LD->>SG: ResumeTraffic("TestApp") [stale entry cleared on new session]
    SG->>AGR: PausedAppsRegistry.Resume("TestApp")

    Note over App,AGR: New session traffic is not blocked
    App->>AGR: DispatchWsMsg(method, params)
    AGR->>AGR: IsPaused("TestApp") → false
    AGR->>App: Response delivered normally
```

---

## 8. File Changes Summary

| File | Change |
|---|---|
| `entservices-apis/apis/Ids.h` | Add `ID_APP_GATEWAY_APP_SESSION_GUARD = ID_APP_GATEWAY + 6` |
| `entservices-apis/apis/AppGateway/IAppGateway.h` | Add `IAppGatewayAppSessionGuard` interface with `SuspendTraffic` / `ResumeTraffic` |
| `entservices-appgateway/AppGateway/AppGatewayResponderImplementation.h` | Inherit `IAppGatewayAppSessionGuard`; add `PausedAppsRegistry` inner class + `mPausedAppsRegistry` member; declare `SuspendTraffic` / `ResumeTraffic` |
| `entservices-appgateway/AppGateway/AppGatewayResponderImplementation.cpp` | Implement `SuspendTraffic` / `ResumeTraffic`; add drop guards in `DispatchWsMsg`, `Respond`, `Emit`, `Request` |
| `entservices-appgateway/AppGatewayCommon/delegate/LifecycleDelegate.h` | Add `mSessionGuard` member + `mSessionGuardMutex`; add `SetSessionGuard()`; add pause/resume calls in `HandleLifecycleUpdate()` |
| `entservices-appgateway/AppGatewayCommon/AppGatewayCommon.cpp` | Query `IAppGatewayAppSessionGuard` and inject into `LifecycleDelegate` in `Initialize()`; clear in `Deinitialize()` |

---

## 9. Scope and Constraints

| Constraint | Detail |
|---|---|
| **Lifecycle 2 only** | All guard logic is gated behind `ConfigUtils::useAppManagers()`. Legacy/non-AppManagers paths have no changes. |
| **AppGatewayCommon only** | Any responder implementation outside the AppManagers path is separate and is not affected. |
| **Drop, not queue** | Messages are discarded while the app is hibernated. The app is expected to re-request any state it needs after resuming. |
| **Stale suspension cleanup** | When a new session starts (`INITIALIZING`), `ResumeTraffic(appId)` is called unconditionally to clear any suspension carried over from a previous session that may have crashed while hibernated or never transitioned out of `HIBERNATED`. |
| **No errors surfaced** | `Respond`, `Emit`, and `Request` return `Core::ERROR_NONE` when dropping; `DispatchWsMsg` returns void. The application does not observe hibernation-related errors. |
| **Thread safety** | All shared state is internally synchronised. `PausedAppsRegistry` protects its set with its own mutex. `mSessionGuard` is protected by a separate mutex that is never held across COM-RPC calls. |


