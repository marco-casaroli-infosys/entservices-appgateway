# WPEFramework Plugin Mock Generation Guide

### Mock Generation Objectives

For L1 unit testing preparation, ensure the generated mocks meet the following criteria:
- **External Dependency Identification**: Accurately identify all external dependencies that need mocking
- **Existing Mock Validation**: Verify that required mocks don't already exist before generating new ones
- **Comprehensive Mock Coverage**: Generate complete, functional mocks that align with repository patterns

## Mock Generation Process
When generating mocks for L1 testing, please follow this step-by-step approach:

### Pre-Generation Checklist
Before generating mocks, complete this verification checklist:

1. **List ALL build dependencies** from CMakeLists.txt (`find_package`, `target_link_libraries`)
2. **For EACH build dependency, verify**:
   - [ ] Does a wrapper file exist? (e.g., `Iarm.h`, `dependency wrapper`)
   - [ ] Does the wrapper define `*Impl` interfaces?
   - [ ] Does a corresponding `*Mock.h` file exist with `MOCK_METHOD` declarations?
   - [ ] **If wrapper exists but mock doesn't → ADD TO GENERATION LIST**
3. **Analyze ALL #include statements** in plugin source files to identify external headers
4. **Trace ALL method execution paths** - for EVERY method in implementation files:
   - [ ] Identify ALL external API calls (direct dependencies)
   - [ ] **CRITICAL: Identify ALL returned objects from external APIs**
   - [ ] **CRITICAL: Trace what methods are called ON those returned objects**
   - [ ] Example: `Api::getObject(...)` returns an object, then `object.getStatus()` is called → **Both need mocks**
   - [ ] **BOTH the getter interface AND the returned object type need mocks**
5. **For EACH identified dependency** (from steps 1-4), verify corresponding mock exists:
   - [ ] Check for direct API interface mocks (e.g., `ManagerMock.h`)
   - [ ] **Check for returned object type mocks** (e.g., `ConnectionImplMock` in `HdmiCecMock.h`, `ReturnedObjectMock.h`)
   - [ ] If either is missing → ADD TO GENERATION LIST

**CRITICAL**: A wrapper file (like `Iarm.h`) is NOT a mock. You must find or generate a separate `*Mock.h` file with MOCK_METHOD declarations.

1. **External Dependency Analysis**
    - **A. Build Dependency Analysis (CMakeLists.txt)**:
        - Examine ALL `find_package()` calls to identify build-time dependencies
        - Examine ALL libraries in `target_link_libraries()` statements
        - Include conditional dependencies (inside `if` blocks)
        - **These must be mocked even if not explicitly called in code** (linker requires all declared dependencies)
    - **B. Source Code Analysis**:
        - Examine the plugin source code and implementation files thoroughly
        - **Note the actual header filenames in #include statements** to identify external dependencies
        - **Verify completeness**: Compare your generated mock against similar existing mocks (same library/interface family) - significant differences in method count may indicate incomplete sources
        - Trace through all #include statements to identify external headers
    - **C. Execution Path Tracing (CRITICAL)**:
        - **MANDATORY STEP**: For EVERY method in the implementation files, trace the complete execution path line-by-line
        - Identify all external function calls, API invocations, and service interactions
        - **CRITICAL - Two-Level Dependency Analysis**:
            1. **Level 1: Direct API calls** - Identify the interface/manager being called
            2. **Level 2: Returned object usage** - When APIs return objects, trace what methods are called ON those objects
        - **Common Return-Object Patterns with Concrete Examples**:
            - **Getter Pattern**: `Api::getHandle(...)` writes a handle that is passed to `Api::getState(handle, ...)`; mock both API calls and their output values
            - **List/Collection Pattern**: `Api::getItems(...)` fills a collection whose elements are then used; mock the API call and provide representative items
            - **Factory Pattern**: `Factory.create()` → returns `Product` → `product.operation()` is called
            - **Constructor Pattern**: `Connection(params)` → creates `Connection` object → `connection.open()` / `connection.sendTo()` is called
        - **BOTH the getter interface AND the returned object types must be mocked**
    - **Exclude from mocking**:
        - Thunder framework headers (interfaces/*, com/*, core/*, plugins/*)
        - Standard C/C++ library headers (iostream, string, vector, etc.)
        - Internal plugin files (PluginName.h, PluginNameImplementation.h, PluginNotification.h)
        - Module.h and other internal Thunder plugin boilerplate
        - Internal helper classes (e.g., TpTimer, internal utilities)
    - **Include for mocking**:
        - Third-party library headers (HAL APIs, system libraries, vendor-specific APIs)
        - Platform-specific APIs that are not part of Thunder
        - External service interfaces that the plugin depends on
        - **ALL dependencies listed in CMakeLists.txt** (build stability requirement)

2. **Existing Mock Verification**
    - **FIRST: Check if complete mock already exists** - do not regenerate unnecessarily
    - Search `entservices-testframework/Tests/mocks/` directory comprehensively
    - **CRITICAL - Verify a file is actually a mock by checking ALL three criteria**:
        1. Filename contains "Mock" (e.g., `LibraryMock.h`, `ManagerMock.h`)
        2. File contains `#include <gmock/gmock.h>`
        3. File contains `MOCK_METHOD(...)` declarations
    - **Files are NOT mocks if they**:
        - Only define wrapper classes with `*Impl` interfaces (e.g., `Iarm.h`, `dependency wrapper`)
        - Lack gmock includes or MOCK_METHOD declarations
        - Are pure interface definitions without mock implementations
    - **If existing mock found**: Verify it's complete by comparing method count with similar mocks before deciding to regenerate
    - **CRITICAL DECISION RULE**: If you find a wrapper file (like `Iarm.h` or `dependency wrapper`) that defines `*Impl` interfaces:
        1. Search for a corresponding mock file (e.g., `IarmBusMock.h`, `IarmMock.h`)
        2. **If NO corresponding mock exists**, you MUST generate the mock
        3. **A wrapper file is NOT a mock file** - it does not meet the 3 criteria above (no "Mock" in filename, no gmock include, no MOCK_METHOD declarations)
        4. A separate mock file with MOCK_METHOD declarations is required
        5. Example: `Iarm.h` defines `IarmBusImpl` interface → Must have `IarmBusMock.h` with `IarmBusImplMock` class
    - Verify that Thunder-specific mocks exist in `entservices-testframework/Tests/mocks/thunder/`
    - Do not generate mocks for dependencies that already have mock implementations
    - **CRITICAL**: Examine at least 3-5 existing mock files in the repository to understand the exact structure, patterns, and conventions used
    - Ensure generated mocks match the style and structure of existing mocks (do NOT assume or invent patterns)

3. **Mock Design and Implementation**
    - Before generating any mock, examine multiple existing mocks in `entservices-testframework/Tests/mocks/` to identify the repository's patterns
    - **Compare method counts**: If similar existing mocks have significantly more methods than your source, investigate for completeness
    - Follow the established patterns found in the existing mock files (not assumed patterns)
    - Use Google Mock (gmock) framework with MOCK_METHOD macros
    - Include proper copyright headers matching the repository standard
    
    - **Complete Mock Generation - Critical Steps**:
        1. **Read the source file FIRST**: Open the wrapper file (e.g., `dependency wrapper`, `Iarm.h`) OR actual header if available, and locate the `*Impl` interface class definition
        2. **Copy ALL methods**: For every virtual method in the interface, create a corresponding MOCK_METHOD declaration with the exact signature
        3. **Match signatures exactly**: Copy return types, parameter types (including references/pointers), and const qualifiers directly - do not guess or assume
        4. **Do not skip methods**: Mock every method in the interface, even if not currently called in the code being tested
        5. **Verify completeness**: Compare against similar existing mocks - if your mock has significantly fewer methods, investigate for missing definitions
    
    - **For main interface/API mocks**: Include COM interface methods (AddRef, Release, QueryInterface) when the interface inherits from COM interfaces
    - **For notification/callback mocks**: DO NOT include COM methods (AddRef, Release, QueryInterface) - only mock the actual callback/notification methods
    - **Group related classes in one file** following repository patterns:
        - **Main class + its event/notification interfaces** go in the same file (e.g., `ManagerMock.h` contains `ManagerImplMock` + all `I*EventsImplMock` classes that Manager can register)
        - **Multiple related helper classes from same wrapper** can be in one file (e.g., `HdmiCecMock.h` contains multiple CEC-related class mocks)
        - Check the original wrapper file structure to determine grouping
    - Create notification/callback mocks when the external dependency supports them
    - Use `MockUtils` base class pattern when the original API is C-style functions
    - Ensure mock class names follow the pattern: `[OriginalName]Mock` or `[OriginalName]ImplMock`

4. **Mock Structure Requirements**
    - Include necessary headers and namespaces
    - Provide default constructor and virtual destructor
    - Use exact method signatures from the wrapper interface (return types, parameters, const qualifiers, override specifiers)
    - Include any nested classes or enums that tests might need
    - **Add ALL related event/notification interface mocks in the same file** when a main class has event registration methods
    - Example: If `ManagerImpl` has `Register(IManagerEvents*)` method, include `IManagerEventsImplMock` in `ManagerMock.h`

5. **Output Format**
    - Generate complete, compilable mock header files
    - Use `.h` extension for all mock files
    - Place appropriate include guards or #pragma once
    - Include the original header file (e.g., `#include "manager.hpp"` in `ManagerMock.h`)
    - Do not include implementation (.cpp) files unless the original has static/global functions
    - Output only the mock code without explanations or summaries
    - Ensure all generated mocks can be included in test files without compilation errors

## Mock Generation Guidelines

### Mock File Grouping Patterns (CRITICAL)
Understanding when to group multiple mock classes in one file:

**Pattern 1: Main Class + Event Interfaces**
- When a main class has `Register()` methods for event listeners, include ALL related event interface mocks in the same file
- Example: `ManagerMock.h` contains:
  - `ManagerImplMock` (main interface)
  - `IManagerEventsImplMock` (because Host has `Register(IManagerEvents*)`)
  - `IManagerEventsImplMock` (because Host has `Register(IManagerEvents*)`)
  - `IManagerEventsImplMock`, `IManagerEventsImplMock`, etc. (all event interfaces Manager can register)

**Pattern 2: Related Helper Classes**
- Multiple classes from the same wrapper file that work together
- Example: `HdmiCecMock.h` contains `LibCCECImplMock`, `ConnectionImplMock`, `LogicalAddressImplMock`, etc.
- These are all defined in the same wrapper (`HdmiCec.h`) and used together in CEC operations

**Pattern 3: Standalone Classes**
- Individual classes with no event registration or tightly coupled helpers
- Example: `ManagerMock.h` contains only `ManagerImplMock`
- Example: `IarmBusMock.h` contains only `IarmBusImplMock`

**How to Determine Grouping:**
1. Check if the main class has `Register(I*Events*)` or `Register(I*Notification*)` methods
2. If yes, group the main mock + ALL event/notification interface mocks in one file
3. Check the original wrapper file structure - if multiple helper classes are defined together, mock them together
4. When in doubt, examine similar existing mocks in the repository

### Repository Pattern Analysis (REQUIRED FIRST STEP)
Before generating any mock, you MUST:
1. List and examine at least 3-5 similar mock files from `entservices-testframework/Tests/mocks/`
2. Document the patterns you observe:
   - How are namespaces handled? (using declarations, fully qualified, etc.)
   - What's the naming convention? (Mock prefix, suffix, etc.)
   - How are COM methods handled? (MOCK_METHOD vs other approaches)
   - What includes are standard?
   - How are notification classes structured?
3. Use the observed patterns for your new mock - do NOT invent or assume patterns

### Standard Mock Structure
Follow this pattern based on existing mocks (VERIFY against actual repository mocks first):

```cpp
#pragma once

#include <gmock/gmock.h>
#include "original_header.h"  // Include the original interface/wrapper

// Main interface mock - mock ALL methods from the interface
class OriginalNameImplMock : public OriginalNameImpl {
public:
    virtual ~OriginalNameImplMock() = default;

    // Mock ALL public methods (complete interface)
    MOCK_METHOD(ReturnType, MethodName, (ParamTypes...), (override));
    MOCK_METHOD(ReturnType, AnotherMethod, (ParamTypes...), (const, override));
    
    // COM interface methods if applicable (for main interfaces only)
    MOCK_METHOD(void, AddRef, (), (const, override));
    MOCK_METHOD(uint32_t, Release, (), (const, override));
    MOCK_METHOD(void*, QueryInterface, (const uint32_t interfaceNumber), (override));
};

// Event/Notification interface mocks (in same file if main class has Register methods)
// IMPORTANT: Event mocks should NOT include COM methods
// They only mock the actual event/notification callback methods
class IEventListenerImplMock : public IEventListenerImpl {
public:
    virtual ~IEventListenerImplMock() = default;
    
    // Only mock the event callback methods - NO COM methods
    MOCK_METHOD(void, OnEvent, (ParamTypes...), (override));
    MOCK_METHOD(void, OnAnotherEvent, (ParamTypes...), (override));
};

// Add ALL related event interface mocks if main class has Register() methods for them
class IAnotherEventImplMock : public IAnotherEventImpl {
public:
    virtual ~IAnotherEventImplMock() = default;
    
    MOCK_METHOD(void, OnSomeEvent, (ParamTypes...), (override));
};
```

**IMPORTANT**: The above is a TEMPLATE. Always verify against actual repository mocks before using.

### Critical Pattern Differences
When generating mocks, understand these key distinctions observed in the repository:

1. **Main Interface Mocks** (e.g., `IStore2Mock`, `IStorageManagerMock`):
   - Mock all public interface methods
   - Include COM methods: `AddRef()`, `Release()`, `QueryInterface()`
   - These are the primary service interfaces

2. **Notification/Callback Mocks** (e.g., `IStore2NotificationMock`, `DevicesChangedNotificationMock`):
   - Only mock the notification/callback methods (e.g., `ValueChanged`, `OnDevicesChanged`)
   - **DO NOT** include COM methods (AddRef, Release, QueryInterface)
   - These are purely callback interfaces

This distinction is critical and consistent across all mocks in the repository.

### Build Dependency Analysis (Critical for Complex Plugins)
Modern plugins often have dependencies that must be mocked even if not directly called in code:

**Why Build Dependencies Matter:**
- Linker requires all libraries listed in `target_link_libraries()` to be available
- Static initializers and build stability require complete dependency chain
- Some dependencies may be loaded at runtime or used indirectly

**Analysis Steps:**
1. **Open plugin's CMakeLists.txt**
2. **Find ALL `find_package()` calls** - these declare dependencies
3. **Find ALL libraries in `target_link_libraries()`** - these are linked dependencies
4. **Include conditional dependencies** - check inside `if()` blocks

**Example from FrameRate plugin:**
```cmake
# These ALL need mocks:
find_package(IARMBus)          # ← Mock needed: IarmBusMock.h

target_link_libraries(${PLUGIN_IMPLEMENTATION}
    PRIVATE ${IARMBUS_LIBRARIES}  # ← Must mock
)
```

**Verification:**
- Check if wrapper files exist: `Iarm.h`, `dependency wrapper`
- Verify if mocks exist: `IarmBusMock.h`, `ManagerMock.h`, etc.
- Even if code doesn't call library functions, the mock is still required for build stability

### Quality Assurance
- **Cross-reference with existing mocks**: Before finalizing, compare your generated mock with at least 2-3 similar mocks in the repository
- **Verify completeness**: Count methods in wrapper interface vs. MOCK_METHOD declarations - they must match
- **Verify two-level dependency coverage** (see "Execution Path Tracing" section for detailed examples):
  - For each external API call, verify the API interface mock exists
  - **For each returned object type, verify the object type mock exists**
  - Example: If code calls `object.getResult()`, verify mocks exist for BOTH: `ReturnedObjectMock` AND `RelatedObjectMock`
- **Verify grouping**: If main class has `Register()` methods for events, ensure ALL event interface mocks are in the same file
- Verify all mock methods have correct signatures matching the original interface (return types, parameters, const qualifiers)
- Ensure proper const-correctness and override specifications
- Verify namespace usage matches repository patterns (examine existing mocks)
- Verify naming conventions match repository patterns (examine existing mocks)
- Verify structure matches repository patterns (no interface maps unless standard)
- Include all necessary forward declarations and typedefs
- Test that mocks compile successfully with the test framework
- Validate that mocks can be instantiated and used in test scenarios
- **Confirm file is actually a mock** by verifying:
  1. Filename contains "Mock"
  2. Contains `#include <gmock/gmock.h>`
  3. Contains `MOCK_METHOD(...)` declarations
- **WRAPPER WITHOUT MOCK CHECK**: For each dependency from CMakeLists.txt, verify:
  1. If a wrapper file exists (e.g., `Iarm.h`, `dependency wrapper`)
  2. AND the wrapper defines `*Impl` interfaces
  3. THEN a corresponding `*Mock.h` file with MOCK_METHOD declarations MUST exist
  4. If missing, generate the mock file

Remember: Generate only the mocks that are truly needed and don't already exist. Focus on external dependencies including build dependencies from CMakeLists.txt. **ALWAYS cross-reference with existing repository mocks to ensure consistency and completeness.**
