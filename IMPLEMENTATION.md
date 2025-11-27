# Transfer Conveyor Controller - Implementation Summary

## System Architecture

The system uses a dual-core Arduino Opta controller with the following architecture:

### M7 Core (main.cpp)
- Handles all network communication (Ethernet with WiFi fallback)
- Manages HTTP request queue for multiple simultaneous requests
- Monitors network connection and automatically reconnects if lost
- Communicates with M4 core via RPC (Remote Procedure Call)

### M4 Core (m4_main.cpp)
- Controls stepper motor via driver interface
- Monitors limit switches for robotic cell and storage cell positions
- Executes movement commands with position tracking
- Reports status back to M7 core

## API Endpoints

### Conveyor Movement Commands

**POST /move/robotic**
- Moves conveyor to robotic cell position
- Stops when robotic cell limit switch is triggered
- Returns: `{"requestId": 1, "status": "QUEUED", "message": "..."}`

**POST /move/storage**
- Moves conveyor to storage cell position
- Stops when storage cell limit switch is triggered
- Returns: `{"requestId": 2, "status": "QUEUED", "message": "..."}`

**POST /move/distance**
- Moves conveyor to specific distance/position
- Request body: `{"distance": 100.0}`
- Returns: `{"requestId": 3, "status": "QUEUED", "distance": 100.0, "message": "..."}`

**GET /conveyor/status**
- Returns current status of conveyor system
- Response includes:
  - `requestId`: Currently executing request ID
  - `status`: Current movement status (0=IDLE, 1=QUEUED, 2=MOVING, 3=AT_ROBOTIC_CELL, 4=AT_STORAGE_CELL, 5=AT_POSITION, 6=ERROR)
  - `currentPosition`: Current position in steps
  - `roboticCellSwitch`: Robotic cell limit switch state
  - `storageCellSwitch`: Storage cell limit switch state
  - `queueLength`: Number of pending requests

### Legacy Expansion Module Endpoints
- POST /clamp - Control individual clamp
- POST /clamps - Control multiple clamps
- GET /clamp - Get clamp status
- GET /status - Get expansion module status

## Network Configuration

### Ethernet (Primary)
- Automatically attempts DHCP connection
- Checks for hardware and cable connection
- Falls back to WiFi if unavailable

### WiFi (Fallback)
- Credentials configured in main.cpp:
  ```cpp
  const char* WIFI_SSID = "YOUR_WIFI_SSID";
  const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
  ```
- Update these values before uploading

### Auto-Reconnection
- Connection checked every 5 seconds
- Automatically reconnects if connection is lost
- Attempts Ethernet first, then WiFi

## Hardware Configuration

### Pin Definitions (main.h)
```cpp
#define STEP_PIN 4                    // Stepper driver STEP pin
#define DIRECTION_PIN 3               // Stepper driver DIR pin
#define LIMIT_SWITCH_START_PIN 7      // Robotic cell position switch
#define LIMIT_SWITCH_END_PIN 8        // Storage cell position switch
#define SPEED 1500                    // Motor speed (steps/sec)
#define ACCELERATION 600              // Motor acceleration
```

### Stepper Motor Driver Connections
- STEP → Pin 4
- DIR → Pin 3
- ENABLE → (optional, not currently controlled)
- Power and ground to driver

### Limit Switches
- Robotic Cell Switch → Pin 7 (normally open, active low)
- Storage Cell Switch → Pin 8 (normally open, active low)

## Request Flow

1. **User sends HTTP request** to M7 core
2. **M7 validates and queues request**, responds with request ID and "QUEUED" status
3. **M7 checks if M4 is busy** via RPC call
4. **When M4 is available**, M7 sends command via RPC
5. **M4 executes movement**, monitoring limit switches
6. **M4 stops at target** (limit switch or position reached)
7. **M4 updates status** to IDLE
8. **M7 processes next queued request**
9. **User can query status** at any time via GET /conveyor/status

## Movement Logic

### Move to Robotic Cell
- M4 drives motor in negative direction
- Monitors robotic cell limit switch
- Stops immediately when switch is pressed
- Resets position counter to 0

### Move to Storage Cell
- M4 drives motor in positive direction
- Monitors storage cell limit switch
- Stops immediately when switch is pressed
- Updates position counter

### Move to Distance
- M4 calculates steps needed
- Drives motor with acceleration/deceleration
- Stops when target position reached
- Does NOT use limit switches

## Building and Uploading

### M7 Core
```bash
pio run -e opta -t upload
```

### M4 Core
```bash
pio run -e opta_m4 -t upload
```

### Both Cores
```bash
pio run -t upload
```

## Testing

### Test Network Connection
1. Upload code to both cores
2. Open serial monitor at 115200 baud
3. Watch for "Ethernet connected" or "WiFi connected" message
4. Note the IP address
5. Navigate to `http://<IP_ADDRESS>/` in browser

### Test Movement Commands
```bash
# Move to robotic cell
curl -X POST http://<IP_ADDRESS>/move/robotic

# Move to storage cell
curl -X POST http://<IP_ADDRESS>/move/storage

# Move to specific distance
curl -X POST http://<IP_ADDRESS>/move/distance \
  -H "Content-Type: application/json" \
  -d '{"distance": 1000}'

# Get status
curl http://<IP_ADDRESS>/conveyor/status
```

## Status Codes

### MoveStatus Enum
- 0: IDLE - Not moving, ready for commands
- 1: QUEUED - Request queued, waiting to execute
- 2: MOVING - Currently executing movement
- 3: AT_ROBOTIC_CELL - Stopped at robotic cell position
- 4: AT_STORAGE_CELL - Stopped at storage cell position
- 5: AT_POSITION - Stopped at target position
- 6: ERROR - Error occurred during movement

## Next Steps

1. **Update WiFi credentials** in main.cpp before uploading
2. **Verify pin connections** match your hardware
3. **Calibrate motor speed/acceleration** if needed
4. **Test limit switch functionality** before full operation
5. **Adjust debounce times** if switches are unreliable
6. **Consider adding homing routine** on startup to establish known position

## Safety Considerations

- Limit switches indicate position but do not provide safety stops
- Consider adding emergency stop functionality if needed
- Test movements at reduced speed first
- Ensure mechanical limits prevent over-travel beyond switches
- Monitor for missed steps or position errors
