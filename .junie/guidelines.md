Project guidelines for TransferConveyor

Scope
- This document captures project-specific build, configuration, testing, and development practices for the TransferConveyor PlatformIO project targeting Arduino Opta’s dual cores (M7 and M4).
- Audience: advanced developers familiar with PlatformIO/Arduino and embedded development on STM32.


1. Build and configuration

Environments
- The project defines two PlatformIO environments in platformio.ini:
  - env:opta → M7 core application (src/main.cpp), excludes m4_main.cpp via build_src_filter.
  - env:opta_m4 → M4 core application (src/m4_main.cpp), excludes main.cpp via build_src_filter.

Boards/framework
- Both envs use platform=ststm32, framework=arduino with Arduino Opta board definitions (board = opta and board = opta_m4 respectively).

Library graph and dependencies
- Common libraries (via [env].lib_deps):
  - ezButton, AccelStepper, ArduinoJson
- M7-only libraries (env:opta):
  - Arduino_Opta_Blueprint, Arduino_SerialUpdater, Arduino_DebugUtils
- M4-only libraries (env:opta_m4):
  - ArduinoRPC
- lib_ldf_mode = deep is enabled; ensure headers are discoverable and includes are correct. Do not rely on transitive includes; include headers explicitly (e.g., <RPC.h>, <AccelStepper.h>, "protocol.h").

Core separation (critical)
- Source separation is enforced via build_src_filter in platformio.ini:
  - env:opta builds everything except src/m4_main.cpp.
  - env:opta_m4 builds everything except src/main.cpp.
- When adding new files, place core-specific code in files that are excluded appropriately or guard with preprocessor checks (e.g., dedicated folders or build_src_filter rules) to avoid dual-definition linkage conflicts.

Serial/monitoring
- Default Serial baud is 115200 (see src/main.cpp setup()). Use this for monitor_speed when running pio device monitor.

Network
- The M7 application attempts Ethernet first, then Wi‑Fi with fallback and basic reconnect logic. Provide credentials via code (currently hardcoded or placeholders in src/main.cpp). If you add secrets, prefer build flags or src/private_config.h excluded from VCS and referenced conditionally.

Build
- Build M7: pio run -e opta
- Build M4: pio run -e opta_m4

Upload
- Upload M7: pio run -e opta -t upload
- Upload M4: pio run -e opta_m4 -t upload
- You must flash both cores after changes that affect inter-core contracts (RPC signatures, protocol.h). Flash order typically does not matter, but update both before system-level testing.

Monitor
- pio device monitor -b 115200
- If the board exposes multiple serial interfaces, specify the port explicitly: pio device monitor -p COMx -b 115200


2. Testing

Test framework and strategy
- Use PlatformIO’s unit testing (Unity) for embedded targets. Tests are compiled and run per environment (M7 or M4). For hardware-in-the-loop tests, PlatformIO uploads a test runner firmware to the board.
- Target selection matters: tests that include Arduino and use timing/APIs should run under env:opta or env:opta_m4 on real hardware. Avoid host-native tests unless you provide pure C++ logic without Arduino dependencies.

Running tests
- Run tests for M7: pio test -e opta
- Run tests for M4: pio test -e opta_m4
- Verbose (useful for debugging): pio test -e opta -vv
- To monitor serial output during tests, PlatformIO captures it, but you can also monitor separately if you run custom test sketches.

Where tests live
- Place tests under test/<suite_name>/. Each suite can contain test_main.cpp (or multiple .cpp files). PlatformIO will build and deploy per env.

Minimal working test example
- The following example compiles and runs on both envs. It avoids hardware access and asserts basic invariants. Create file: test/basic/test_main.cpp with the content below.

  /*
  #include <Arduino.h>
  #include <unity.h>

  // Optional: fast setup to appease frameworks that expect Serial
  void setUp() {}
  void tearDown() {}

  void test_math_sanity() {
      TEST_ASSERT_EQUAL(4, 2 + 2);
      TEST_ASSERT_TRUE(115200 >= 9600);
  }

  void test_protocol_contract_compiles() {
      // Ensure protocol.h can be included in both cores
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

Execution
- After adding the file above, run: pio test -e opta and/or pio test -e opta_m4. The test runner should build and flash the firmware, then report 2/2 tests passed.

Adding richer tests
- Hardware-aware tests: If you need to touch GPIOs, prefer using pins abstracted via main.h and guard to run only on the appropriate core. Example: test M4 stepper config under env:opta_m4 only.
- Inter-core RPC tests: Unit testing cross-core RPC end-to-end is non-trivial in PlatformIO’s built-in runner because it deploys per env separately. Strategy:
  - Validate compile-time contracts by including protocol.h from tests for both cores (as in the example).
  - Add functional tests that exercise RPC on-device in the M7 firmware itself under a feature flag (e.g., a test mode triggered via serial) and assert expected responses; capture via device monitor. Keep such code excluded from production via build flags.

Cleaning up
- Per the task requirements, do not leave test files committed unless they are part of your intended permanent test suite. For ad-hoc validation, keep tests local or remove the test/ suite after use. This document includes the sample test inline rather than checked into the repo.


3. Additional development information

Protocol and RPC contracts
- The inter-core API is expressed via protocol.h and RPC.bind calls:
  - M4 exports: processRequest, isBusy, getStatus, getStatusCode, getCurrentPosition, getRoboticSwitchState, getStorageSwitchState.
- When changing signatures or adding functions, update both callers (M7) and implementers (M4). Maintain consistent enums and integral mapping across cores.

Motion control safety
- M4 (src/m4_main.cpp) owns all real-time motion control. Key points:
  - Limit switches: ezButton with debounce 20 ms on LIMIT_SWITCH_START_PIN and LIMIT_SWITCH_END_PIN.
  - AccelStepper configured with SPEED and ACCELERATION from include/main.h. Any change impacts motion profile; test on hardware.
  - Long travel moves use large absolute counts (±1,000,000) to seek until limit switches. Ensure wiring and switch polarity are correct; consider using hard stop or software endstops to prevent runaway if a switch fails.
  - LED_BUILTIN is used as a simple motion/busy indicator (LOW when moving, HIGH when idle/target reached).

Networking and robustness (M7)
- M7 (src/main.cpp) selects Ethernet first, then Wi‑Fi, and periodically checks connectivity with reconnection. If you alter intervals or states, keep CONNECTION_CHECK_INTERVAL conservative to avoid starving other tasks. Ensure Serial logging remains at 115200.

Source hygiene and style
- Language: Arduino C++ (C++17 availability depends on the STM32 Arduino core; keep templates simple).
- Prefer explicit includes; avoid relying on transitive headers provided by libraries.
- Keep ISR/real-time sections minimal; m4_main.cpp loop() runs executeMovement() with delayMicroseconds(100). Be cautious adding heavy work there.

Debugging tips
- Use Serial on both cores where possible; on tight loops, throttle logs.
- For M4 timing-sensitive behavior, prefer toggling a GPIO and measuring with a scope rather than heavy logging.
- To debug RPC issues, first confirm RPC.begin() is called on both cores and that function names match exactly on bind and call sites.

Extending the project
- When adding new commands or states:
  - Update enums in protocol.h.
  - Extend processRequest switch and executeMovement logic on M4.
  - Update M7-side orchestrator and any UI/network endpoints that drive commands.
  - Add compile-time tests that include protocol.h for both cores.

Versioning and reproducibility
- Keep platformio.ini pinned to specific library versions (already done). If you add dependencies, pin them and document why.

Monitoring and logging conventions
- Prefix M7 logs with [M7] and M4 with [M4] if you add multicore serial output to simplify log correlation.

Checklist before committing changes
- Build both envs: pio run -e opta && pio run -e opta_m4
- If RPC or protocol changed, flash both cores and smoke test motion start/stop and limit switches.
- If adding tests, ensure they compile for both envs or are conditionally included.

Housekeeping per this task
- This guideline file is the only artifact added by this task. No extra test files are committed. If you created temporary files locally to validate commands, remove them before committing changes.
