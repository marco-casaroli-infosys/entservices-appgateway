# entservices-appgateway

## Generated subsystem documentation

This repository is an RDK/Thunder plugin suite implementing a Firebolt-compatible application gateway. The following self-contained guides document every discovered source subsystem:

- [AppGateway](AppGateway/AppGateway.md) — WebSocket ingress/egress, resolver, responder, context routing, and telemetry.
- [AppGatewayCommon](AppGatewayCommon/AppGatewayCommon.md) — device/service business logic, authentication, handler routing, and delegates.
- [AppNotifications](AppNotifications/AppNotifications.md) — Thunder event subscriptions, subscriber contexts, and fan-out.
- [AppActions](AppActions/AppActions.md) — app-to-app action dispatch and asynchronous notifications.
- [Shared helpers](helpers/helpers.md) — context, WebSocket, telemetry, JSON, logging, and utility headers.
- [Testing and build](docs/testing-and-build.md) — CMake selection, mocks, L0/L1/L2 tests, coverage, and build limits.

Each guide contains purpose, architecture, file/class walkthroughs, configuration, workflows, Mermaid diagrams, quality analysis, and a beginner-to-expert learning path. Source or platform information unavailable in this workspace is called out explicitly in the relevant guide.

> **License:** Apache 2.0 — Copyright 2023–2025 Comcast Cable Communications Management, LLC / RDK Management

`entservices-appgateway` is an RDK / WPEFramework (Thunder) plugin suite that implements the **Firebolt-compatible API Gateway** for applications running on RDK devices. It replaces the legacy Ripple Gateway and provides a single, authenticated WebSocket entry-point for all app-facing JSON-RPC calls.

---

## Repository Layout

```
entservices-appgateway/
├── AppGateway/              # Thunder plugin — WebSocket gateway & request routing
│   ├── AppGateway.md        # Plugin documentation
│   ├── docs/                # Telemetry deep-dive docs & sequence diagrams
│   └── tests/               # cURL smoke-test commands
├── AppGatewayCommon/        # Thunder plugin — Business logic, auth, lifecycle, settings
│   ├── AppGatewayCommon.md  # Plugin documentation
│   └── tests/               # cURL smoke-test commands
├── AppNotifications/        # Thunder plugin — Event subscription & routing
│   ├── AppNotifications.md  # Plugin documentation
│   └── tests/               # cURL smoke-test commands
├── AppActions/              # Thunder plugin — App-to-app action/intent dispatch
│   ├── AppActions.md        # Plugin documentation
│   └── tests/               # Unit & integration test stubs
├── helpers/                 # Shared headers and helpers documentation
│   └── helpers.md           # Shared helper subsystem guide
├── cmake/                   # CMake helper scripts
├── Tests/                   # L0 / L1 / L2 test trees + mocks + Copilot helpers
│   ├── Testing.md           # Test infrastructure documentation
│   ├── docs/                # Per-plugin test documentation (L0 & L1)
│   ├── L0Tests/             # L0 branch-coverage tests
│   └── CopilotFiles/        # AI-assisted test generation instructions
├── docs/                    # API, platform, subsystem, and build documentation
├── .github/                 # GitHub Copilot & code-review instructions
├── DESIGN.md                # Authoritative system-level design narrative
├── CONTRIBUTING.md          # Contribution guidelines
├── CHANGELOG.md             # Version history
└── CMakeLists.txt           # Root build file
```

---

## High-Level System Diagram

```
┌──────────────┐   WebSocket / JSON-RPC
│ Application  │ ──────────────────────►┐
└──────────────┘                        │
                              ┌─────────▼──────────────────────┐
                              │       AppGateway               │
                              │  (org.rdk.AppGateway)          │
                              │  AppGatewayResponderImpl        │
                              │  AppGatewayImplementation       │
                              │  Resolver  │  Telemetry         │
                              └──────┬─────────────┬───────────┘
                         COM-RPC     │             │  JSON-RPC
                    ┌───────────────▼──┐      ┌───▼─────────────────┐
                    │ AppGatewayCommon │      │  Thunder Plugins     │
                    │ (auth, settings, │      │  (System, Display,   │
                    │  lifecycle, ...)  │      │   UserSettings, ...) │
                    └──────────────────┘      └──────────────────────┘
                              │
                    ┌─────────▼──────────┐
                    │  AppNotifications  │
                    │  (event routing)   │
                    └────────────────────┘
```

---

## Quick Build Reference

```cmake
cmake .. \
  -DPLUGIN_APPGATEWAY=ON \
  -DPLUGIN_APPGATEWAYCOMMON=ON \
  -DPLUGIN_APPNOTIFICATIONS=ON \
  -DPLUGIN_APPACTIONS=ON \
  -DBUILD_ENABLE_TELEMETRY_LOGGING=OFF   # set ON to enable T2
```

See [docs/BuildSystem.md](docs/BuildSystem.md) for the full flag reference.

---

## Documentation Index

### Root Documents

| Document | Description |
|---|---|
| [DESIGN.md](DESIGN.md) | Authoritative system-level design narrative |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Contribution guidelines |
| [CHANGELOG.md](CHANGELOG.md) | Version history |

---

### `docs/` — API & Platform Documentation

| Document | Description |
|---|---|
| [docs/API_REFERENCE.md](docs/API_REFERENCE.md) | App Gateway API reference — interfaces and exposed APIs |
| [docs/RDK8.md](docs/RDK8.md) | RDK8 Firebolt API support design documentation |
| [docs/BuildSystem.md](docs/BuildSystem.md) | CMake build flags, configuration files, install targets |

#### Generated subsystem guides

| Document | Description |
|---|---|
| [AppGateway/AppGateway.md](AppGateway/AppGateway.md) | AppGateway WebSocket ingress/egress, routing, context handling, and telemetry |
| [AppGatewayCommon/AppGatewayCommon.md](AppGatewayCommon/AppGatewayCommon.md) | AppGatewayCommon business logic, authentication, routing, and delegates |
| [AppNotifications/AppNotifications.md](AppNotifications/AppNotifications.md) | AppNotifications subscriptions, event fan-out, and cleanup |
| [AppActions/AppActions.md](AppActions/AppActions.md) | AppActions dispatch, callbacks, and asynchronous delivery |
| [docs/testing-and-build.md](docs/testing-and-build.md) | CMake selection, test layers, mocks, coverage, and build limits |

### `helpers/`

| Document | Description |
|---|---|
| [helpers/helpers.md](helpers/helpers.md) | Shared context, WebSocket, telemetry, JSON, logging, and utility helpers |

---

### `AppGateway/`

| Document | Description |
|---|---|
| [AppGateway/AppGateway.md](AppGateway/AppGateway.md) | AppGateway plugin — WebSocket I/O, request routing, `Resolver`, telemetry |

#### `AppGateway/docs/` — Telemetry Deep Dive

| Document | Description |
|---|---|
| [AppGateway/docs/AppGatewayTelemetry_Architecture.md](AppGateway/docs/AppGatewayTelemetry_Architecture.md) | App Gateway T2 telemetry architecture overview |
| [AppGateway/docs/AppGatewayTelemetryIntegrationGuide.md](AppGateway/docs/AppGatewayTelemetryIntegrationGuide.md) | Step-by-step telemetry integration guide |
| [AppGateway/docs/AppGatewayTelemetryMarkers.md](AppGateway/docs/AppGatewayTelemetryMarkers.md) | Quick reference for all T2 telemetry markers |
| [AppGateway/docs/EntservicesPluginTelemetryMacroUsageSummary.md](AppGateway/docs/EntservicesPluginTelemetryMacroUsageSummary.md) | Telemetry macro usage summary across entservices plugins |
| [AppGateway/docs/NETWORK_HTTP_GRPC_CALL_INVENTORY.md](AppGateway/docs/NETWORK_HTTP_GRPC_CALL_INVENTORY.md) | Network call inventory — HTTP REST and gRPC calls |

#### `AppGateway/docs/sequence-diagrams/` — Telemetry Sequence Diagrams

| Document | Description |
|---|---|
| [AppGateway/docs/sequence-diagrams/README.md](AppGateway/docs/sequence-diagrams/README.md) | Index of all App Gateway telemetry sequence diagrams |
| [AppGateway/docs/sequence-diagrams/01_Bootstrap_Time_Tracking.md](AppGateway/docs/sequence-diagrams/01_Bootstrap_Time_Tracking.md) | Scenario 1: Bootstrap time tracking |
| [AppGateway/docs/sequence-diagrams/02_Health_Stats_Reporting.md](AppGateway/docs/sequence-diagrams/02_Health_Stats_Reporting.md) | Scenario 2: Health stats periodic reporting |
| [AppGateway/docs/sequence-diagrams/03_API_Error_Reporting_Badger.md](AppGateway/docs/sequence-diagrams/03_API_Error_Reporting_Badger.md) | Scenario 3: API error reporting |
| [AppGateway/docs/sequence-diagrams/04_External_Service_Error_OttServices.md](AppGateway/docs/sequence-diagrams/04_External_Service_Error_OttServices.md) | Scenario 4: External service error reporting |
| [AppGateway/docs/sequence-diagrams/05_Metric_Latency_Tracking.md](AppGateway/docs/sequence-diagrams/05_Metric_Latency_Tracking.md) | Scenario 5: API and service latency tracking |
| [AppGateway/docs/sequence-diagrams/06_COM_RPC_Event_Telemetry.md](AppGateway/docs/sequence-diagrams/06_COM_RPC_Event_Telemetry.md) | Scenario 6: External plugin COM-RPC event telemetry |
| [AppGateway/docs/sequence-diagrams/07_COM_RPC_Metric_Recording.md](AppGateway/docs/sequence-diagrams/07_COM_RPC_Metric_Recording.md) | Scenario 7: External plugin COM-RPC metric recording |

---

### `AppGateway/tests/`

| Document | Description |
|---|---|
| [AppGateway/tests/CurlCmds.md](AppGateway/tests/CurlCmds.md) | cURL smoke-test commands for AppGateway |

---

### `AppGatewayCommon/`

| Document | Description |
|---|---|
| [AppGatewayCommon/AppGatewayCommon.md](AppGatewayCommon/AppGatewayCommon.md) | AppGatewayCommon plugin — business logic, authentication, lifecycle, settings delegates |

---

### `AppGatewayCommon/tests/`

| Document | Description |
|---|---|
| [AppGatewayCommon/tests/CurlCmds.md](AppGatewayCommon/tests/CurlCmds.md) | FbMetrics JSON-RPC cURL commands for AppGatewayCommon |

---

### `AppNotifications/`

| Document | Description |
|---|---|
| [AppNotifications/AppNotifications.md](AppNotifications/AppNotifications.md) | AppNotifications plugin — event subscription, `SubscriberMap`, `ThunderSubscriptionManager` |

---

### `AppNotifications/tests/`

| Document | Description |
|---|---|
| [AppNotifications/tests/CurlCmds.md](AppNotifications/tests/CurlCmds.md) | FbMetrics JSON-RPC cURL commands for AppNotifications |

---

### `AppActions/`

| Document | Description |
|---|---|
| [AppActions/AppActions.md](AppActions/AppActions.md) | AppActions plugin — app-to-app intent/action dispatch |

### `AppActions/tests/`

| Document | Description |
|---|---|
| [AppActions/tests/README.md](AppActions/tests/README.md) | AppActions plugin unit and integration test stubs |

---

### `Tests/` — Test Infrastructure

| Document | Description |
|---|---|
| [Tests/Testing.md](Tests/Testing.md) | Test infrastructure, mock inventory, L1 test coverage, suggestions |
| [Tests/README.md](Tests/README.md) | L1/L2 test infrastructure changes and migration notes |
| [Tests/docs/L0/AppNotifications.md](Tests/docs/L0/AppNotifications.md) | AppNotifications L0 tests — quick reference |
| [Tests/docs/L1/AppNotificationsL1Tests.md](Tests/docs/L1/AppNotificationsL1Tests.md) | AppNotifications L1 tests — full test case list |
| [Tests/L0Tests/common/README.md](Tests/L0Tests/common/README.md) | Shared L0 test utilities (`Tests/L0Tests/common`) |
| [Tests/CopilotFiles/copilot-instructions-mock.md](Tests/CopilotFiles/copilot-instructions-mock.md) | WPEFramework plugin mock generation guide |
| [Tests/CopilotFiles/l1_tests.instructions.md](Tests/CopilotFiles/l1_tests.instructions.md) | Copilot instructions for L1 test generation |
| [Tests/L0Tests/AppGatewayCommon/prompts/create-l0-tests-for-staged-changes.md](Tests/L0Tests/AppGatewayCommon/prompts/create-l0-tests-for-staged-changes.md) | Prompt: create L0 tests for staged changes |

---

### `.github/` — Copilot & Code Review Instructions

| Document | Description |
|---|---|
| [.github/copilot-instructions.md](.github/copilot-instructions.md) | Review comment linking guidelines |
| [.github/instructions/Plugin.instructions.md](.github/instructions/Plugin.instructions.md) | Plugin coding standards & interface rules |
| [.github/instructions/Pluginlifecycle.instructions.md](.github/instructions/Pluginlifecycle.instructions.md) | Plugin lifecycle compliance rules |
| [.github/instructions/Pluginimplementation.instructions.md](.github/instructions/Pluginimplementation.instructions.md) | Plugin implementation guidelines |
| [.github/instructions/Pluginmodule.instructions.md](.github/instructions/Pluginmodule.instructions.md) | Plugin module name convention rules |
| [.github/instructions/Pluginconfig.instructions.md](.github/instructions/Pluginconfig.instructions.md) | Plugin configuration standards |
| [.github/instructions/Plugincmake.instructions.md](.github/instructions/Plugincmake.instructions.md) | Plugin CMake conventions |
| [.github/instructions/PluginOnboardingCompliance.instructions.md](.github/instructions/PluginOnboardingCompliance.instructions.md) | Coverity scan inclusion & test workflow for new plugins |
| [.github/instructions/General.instructions.md](.github/instructions/General.instructions.md) | General coding standards (logging, error handling, etc.) |

---

> **New to this repo?** Start with [DESIGN.md](DESIGN.md) for the full system narrative, then read [AppGateway/AppGateway.md](AppGateway/AppGateway.md) to understand the gateway entry point, and [AppGatewayCommon/AppGatewayCommon.md](AppGatewayCommon/AppGatewayCommon.md) for business logic.
