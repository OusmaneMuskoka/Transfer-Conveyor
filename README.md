TransferConveyor — Dual‑Core Arduino Opta (M7 + M4) Conveyor Controller

Overview
- TransferConveyor is a PlatformIO project targeting Arduino Opta’s dual cores (M7 and M4).
- The M7 core (src/main.cpp) provides orchestration, networking (Ethernet first, Wi‑Fi fallback), and a simple HTTP interface placeholder.
- The M4 core (src/m4_main.cpp) owns real‑time motion control: stepper driving (AccelStepper), limit switches (ezButton), and an RPC API consumed by M7.
- Inter‑core communication uses ArduinoRPC with a shared contract defined in include/protocol.h.

Tech stack and tooling
- Language: C++ (Arduino)
- Framework: Arduino (STM32/mbed core for Opta)
- Target board(s): Arduino Opta (M7) and Opta M4 co‑processor
- Build system and package manager: PlatformIO
- Key libraries (pinned in platformio.ini):
  - Common: ezButton, AccelStepper, ArduinoJson
  - M7-only: Arduino_Opta_Blueprint, Arduino_SerialUpdater, Arduino_DebugUtils
  - M4-only: ArduinoRPC

Requirements
- PlatformIO Core (CLI) or an IDE with PlatformIO integration
- Arduino Opta hardware (for on-device run and tests)
- USB connection to the board
- Optional: Network (Ethernet or Wi‑Fi) if exercising M7 networking

Project configuration
- See platformio.ini. Two environments are defined:
  - env:opta → builds M7 firmware (includes src/main.cpp, excludes src/m4_main.cpp)
  - env:opta_m4 → builds M4 firmware (includes src/m4_main.cpp, excludes src/main.cpp)
- lib_ldf_mode = deep is enabled. Include headers explicitly; do not rely on transitive includes.

Entry points
- M7 application: src/main.cpp
- M4 application: src/m4_main.cpp
- Shared headers: include/main.h (pins, motion constants), include/protocol.h (RPC contract)

Environment variables and configuration
- Serial baud: 115200 (see M7 setup())
- Wi‑Fi credentials are placeholders in src/main.cpp:
  - WIFI_SSID, WIFI_PASSWORD
  - Replace with your values before building, or refactor to use build flags/private header.
- Networking behavior (M7): attempts Ethernet first, falls back to Wi‑Fi, periodically checks and reconnects.

Setup and build
- Install dependencies: PlatformIO will fetch library deps automatically.
- Build commands:
  - Build M7: pio run -e opta
  - Build M4: pio run -e opta_m4

Upload/flash
- Flash each core separately:
  - Upload M7: pio run -e opta -t upload
  - Upload M4: pio run -e opta_m4 -t upload
- If you change RPC signatures or protocol.h, reflash both cores before testing.

Run and monitor
- Open serial monitor at 115200 baud:
  - pio device monitor -b 115200
  - If multiple ports are present: pio device monitor -p COMx -b 115200 (replace COMx)

Available scripts/targets
- Standard PlatformIO targets are used; there are no custom extra_scripts in platformio.ini.
- Useful commands:
  - Build: pio run -e opta | pio run -e opta_m4
  - Upload: pio run -e opta -t upload | pio run -e opta_m4 -t upload
  - Clean: pio run -t clean
  - Monitor: pio device monitor -b 115200
  - Test: see Testing section below

Testing
- Strategy: Use PlatformIO’s Unity-based unit testing. Tests compile and run per environment on the device.
- Current state: This repo includes test/README but no active test suites committed.
- To add a minimal compile-time test for both cores, create test/basic/test_main.cpp with the following template (from project guidelines):

  /*
  #include <Arduino.h>
  #include <unity.h>

  void setUp() {}
  void tearDown() {}

  void test_math_sanity() {
      TEST_ASSERT_EQUAL(4, 2 + 2);
      TEST_ASSERT_TRUE(115200 >= 9600);
  }

  void test_protocol_contract_compiles() {
      #include "protocol.h"
      TEST_PASS();
  }

  void setup() {
      delay(100);
      UNITY_BEGIN();
      RUN_TEST(test_math_sanity);
      RUN_TEST(test_protocol_contract_compiles);
      UNITY_END();
  }

  void loop() {}
  */

- Run tests:
  - M7: pio test -e opta
  - M4: pio test -e opta_m4
- Note: PlatformIO flashes a test runner; remove ad-hoc tests before committing unless they are part of the permanent suite.

Project structure
- platformio.ini — environments, dependencies, and build filters
- include/
  - main.h — pins and motion configuration
  - protocol.h — inter-core RPC contract (commands, statuses, request/response types)
- src/
  - main.cpp — M7 application: networking, request queueing, RPC client calls to M4
  - m4_main.cpp — M4 application: stepper control, limit switches, RPC server implementation
- lib/README — placeholder for project-local libraries
- test/README — notes on PlatformIO testing
- IMPLEMENTATION.md — additional implementation notes
- CLEANUP_SUMMARY.md — maintenance history/notes

Development notes
- Core separation is enforced via build_src_filter:
  - env:opta excludes src/m4_main.cpp
  - env:opta_m4 excludes src/main.cpp
- When adding core-specific code, either place it in the appropriate source file(s) or extend build filters/guards to prevent dual-definition conflicts.
- Motion safety: Limit switches are debounced (20 ms). Long-travel moves use large absolute counts to seek until switches are hit; validate wiring and polarity on hardware.
- Logging: Prefer prefixing with [M7] or [M4] if adding multi-core logs for clarity.

Known endpoints and RPC contract
- RPC functions exposed by M4 (bound in m4_main.cpp):
  - processRequest(requestId, command, distance)
  - isBusy()
  - getStatus()
  - getStatusCode()
  - getCurrentPosition()
  - getRoboticSwitchState()
  - getStorageSwitchState()
- Shared enums/types are in include/protocol.h.

Licensing
- No explicit license file was found in this repository.
- TODO: Add a LICENSE file (e.g., MIT/Apache-2.0) and reflect it here.

Contributing
- Build both environments before committing changes that affect shared contracts:
  - pio run -e opta && pio run -e opta_m4
- If RPC or protocol changed, reflash both cores and smoke test motion and limit switches.

Acknowledgements
- Arduino Opta platform and Arduino libraries
- PlatformIO and Unity test framework

Changelog and additional docs
- See CLEANUP_SUMMARY.md and IMPLEMENTATION.md for background and implementation details.
