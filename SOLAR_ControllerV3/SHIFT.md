We need make several changes to our architecture to support higher speed LED control from the DAC, reduced reliance on the closed-loop INA226, and a repurposing of the IO pins currently used for RX_ and TX_Ready.

## IO Pin Repurpose
These need to simply propogate an IO state with zero delay. The master device will be receiving a command to set the logic level. All slave devices need to recognize that state change via interrupt (both HIGH and LOW!). The pins will need to be mapped as:

- TX
- TRIGGER_OUT
- GND

- RX
- TRIGGER_IN
- GND

**Implementation Details:**
- TRIGGER_IN should respond to an onChange interrupt and immediately write the TRIGGER_IN value to TRIGGER_OUT (for slaves); master device has special interrupt handling described below
- The initialization protocol needs to be fully maintained, no longer relying on RX and TX ready signals, but rather, serial polling only

**Pin Definitions:**
```cpp
const int triggerInPin = 7;   // TRIGGER_IN (was rxReadyPin)
const int triggerOutPin = 9;  // TRIGGER_OUT (was txReadyPin)
```

**Interrupt Setup:**
```cpp
// In setup() function
void setup() {
    // ... existing setup code ...
    
    // Setup TRIGGER_IN interrupt (all devices)
    pinMode(triggerInPin, INPUT_PULLDOWN);  // TRIGGER_IN
    pinMode(triggerOutPin, OUTPUT);          // TRIGGER_OUT
    digitalWrite(triggerOutPin, HIGH); // active LOW, default HIGH
    
    attachInterrupt(digitalPinToInterrupt(triggerInPin), triggerInterrupt, CHANGE);
    
    // ... rest of setup code ...
}
```

**Interrupt Handler Implementation:**
```cpp
void triggerInterrupt() {
    // Read current trigger state
    bool triggerState = digitalRead(triggerInPin);
    
    // Immediately propagate to next device
    digitalWrite(triggerOutPin, triggerState);
    
    if (triggerState == LOW) {
        // HIGH->LOW transition: Start intensity output
        if (current_group == group_id && group_id > 0) {
            analogWrite(dacPin, (int)intensity);
            digitalWrite(userLedPin, HIGH);
        }
    } else {
        // LOW->HIGH transition: Stop output and rotate group
        analogWrite(dacPin, 0);
        digitalWrite(userLedPin, LOW);
        
        // Rotate to next group
        current_group++;
        if (current_group > group_total) {
            current_group = 1;
        }
        
        // Master sets program_success when signal completes full cycle
        if (isMasterDevice) {
            program_success = true;
        }
    }
}
```

All devices will need to track the following pieces of information (new variables, default to 0):

1. int group_id = 0;
2. int group_total = 0;
3. float target_current_mA = 0; // Target current for closeloop regulation
4. int exposure = 0; // exposure duration in milliseconds
5. int current_group = 1; // tracks which group is currently active (1-based)
6. int last_adjusted_dac = 0; // Last DAC value (calibrated in Frame_0, adjusted in Frame_1+)
7. bool program_success = false; // master only: tracks if program execution was successful

**Note:** `last_adjusted_dac` serves dual purpose:
- After Frame_0: Stores the auto-calibrated DAC value
- After Frame_1+: Stores the runtime-adjusted DAC value from previous frame
- Always reset to 0 when new `program` command received or `start` command issued

This should be established using the following command structure:

000,program,{group_id,group_total,current,exposure}

**Parameter Details:**
- `group_id`: Group number for this device (1-n, 0=inactive)
- `group_total`: Total number of groups in the system
- `current`: Target current in mA (0-1500) for closeloop regulation
- `exposure`: LED exposure duration in milliseconds (1-100)

**Note:** The `initial_dac` value is NO LONGER user-specified. It is automatically calibrated during Frame_0 (see Auto-Calibration System below).

**Command Parsing for Curly Bracket Parameters:**
```cpp
// Example parsing function for program command (4 parameters)
bool parseProgramCommand(String value, int &group_id, int &group_total, 
                         float &current, int &exposure) {
    // Remove curly brackets
    if (value.length() < 3 || value.charAt(0) != '{' || value.charAt(value.length() - 1) != '}') {
        return false;
    }
    
    value = value.substring(1, value.length() - 1);
    
    // Split by comma - expecting 4 parameters
    int firstComma = value.indexOf(',');
    int secondComma = value.indexOf(',', firstComma + 1);    
    int thirdComma = value.indexOf(',', secondComma + 1);
    
    if (firstComma == -1 || secondComma == -1 || thirdComma == -1) {
        return false;
    }
    
    group_id = value.substring(0, firstComma).toInt();
    group_total = value.substring(firstComma + 1, secondComma).toInt();
    current = value.substring(secondComma + 1, thirdComma).toFloat();
    exposure = value.substring(thirdComma + 1).toInt();
    
    // Validate parsed values
    if (group_total < 1 || group_id < 0 || group_id > group_total || 
        current < 0 || current > 1500 || 
        exposure < 1 || exposure > 100) {
        return false;
    }
    
    return true;
}
```

If `group_id` is set to 0, it shall not execute any program (see below). Therefore, group_ids begin at 1 for active modules.

### Programming Consistency Check
To ensure predictable behavior, all devices within the same `group_id` must have identical `current` and `exposure` values. The master device will be responsible for caching the settings for each `group_id`.

-   If a `program` command is sent for a new device in an existing group, its `current` and `exposure` must match the cached values for that group.
-   If the values do not match, the command will be rejected with an error, and the user will be prompted to send a valid command.
-   The `initial_dac` value is auto-calibrated per device during Frame_0, so devices in the same group may have different calibrated DAC values based on their individual LED characteristics.

## INA226 Current Sensing & Closeloop Control

Each device must have an INA226 current sensor connected via I2C (address 0x4A) to enable active current regulation during LED exposure.

### INA226 Configuration
- **Shunt Resistance:** 0.04195Ω (42mΩ)
- **Max Current:** 1500 mA
- **Conversion Time:** 140µs (0x01) for both bus and shunt voltage
- **Averaging:** 1 sample (0x01)
- **Mode:** Continuous shunt and bus voltage measurement (0x07)

### Current Control Strategy
- **Target Regulation:** 99% of specified current value (safety margin)
  - Example: Target 1300mA → Regulate to 1287mA
- **Update Rate:** As fast as conversion ready (checked with `waitConversionReady(1)` - 1ms timeout)
- **Active Period:** Only during LED exposure window (when userLedPin is HIGH)
- **DAC Adjustment:** Uses proportional reduction algorithm from DAC_Calculator

### Overcurrent Protection
- **Emergency Shutdown:** Shutdown if current > maxCurrent × 1.01 (1515mA) for **two consecutive INA226 readings**
  - **Second consecutive over-threshold:** Immediate emergency shutdown
  - **Non-consecutive spikes:** Do NOT trigger shutdown
  - **Counter reset:** Automatically resets when current drops below threshold or closeloop deactivates
  - **Applies to:** Both Frame_0 (calibration) and Frame_1+ (normal operation)
  - Slave devices self-shutdown independently
  - Master broadcasts `000,dac,0` to all devices
  - Recovery: Master must send `reinit` command to restore system

### Closeloop Implementation
The closeloop is implemented in the `handleCurrentControl()` function, called from the main loop. It:
1. Only runs when `closeloop_active` is true (separate activation flag)
2. Verifies INA226 is available and operational before proceeding
3. Confirms target current is defined (>0)
4. Waits for INA226 conversion ready with 1ms timeout
5. Reads fresh current value from INA226
6. Checks for emergency conditions (>1515mA)
7. Calculates DAC adjustment based on current feedback
8. Updates DAC output (ONLY place DAC is set during operation)
9. Updates `userLedPin` to reflect Current (HIGH when activeCurrent>1, LOW when activeCurrent<1 || DAC=0)

**Critical Rules for DAC Control:**
- **DAC is NEVER set outside `handleCurrentControl()`** during closeloop operation
- **Closeloop Activation:**
  - New variable: `bool closeloop_active` controls when closeloop runs
  - Interrupt handler sets `closeloop_active = true` when TRIGGER_OUT goes LOW (exposure starts)
  - Interrupt handler sets `closeloop_active = false` when TRIGGER_OUT goes HIGH (exposure ends)
  - Closeloop immediately stops when `closeloop_active = false`
- **userLedPin as Visual Indicator:**
  - `userLedPin` is updated in `handleCurrentControl()` only on current readings
  - HIGH when activeCurrent > 1mA
  - LOW when activeCurrent < 1 or DAC=0
  - Provides visual feedback of LED activity
- All DAC updates are dynamically controlled by real-time current feedback
- No DAC value is transmitted without active INA226 feedback
- Initial DAC value is stored in `current_dac_value` but not applied until closeloop runs
- When `TRIGGER_OUT` goes LOW, closeloop is enabled but DAC remains at 0
- First call to `handleCurrentControl()` applies initial DAC and begins regulation

**End-of-Program Safety (Fix for Last Group Issue):**
  - After final frame's exposure duration, master sets TRIGGER_OUT HIGH
  - This propagates through interrupt chain to all devices
  - All devices with `closeloop_active = true` will set it to false
  - `handleCurrentControl()` stops running and sets DAC to 0
  - Ensures clean shutdown of all devices at program completion
- **No emergency broadcast needed** - proper trigger signaling handles cleanup

## Auto-Calibration System (Frame_0)

### Overview
The auto-calibration system eliminates the need for users to manually determine the `initial_dac` value for each device. Instead, the system automatically finds the optimal DAC value during a special calibration phase called **Frame_0**.

### Complete Workflow Summary

**User Programming Sequence:**
1. User programs each device with `program` command: `xxx,program,{group_id,group_total,target_current,exposure}`
   - Example: `001,program,{1,2,1300,20}`
2. User sets frame parameters (optional): `frame,count,interframe_delay`
   - Example: `frame,5,50` (5 frames with 50ms between groups)
3. User sends `start` command

**System Execution Sequence:**
1. **Frame_0 (Auto-Calibration):** 
   - System cycles through all groups (1 second each)
   - Each device finds its optimal DAC to reach target current
   - Master logs calibration results
   - Frame_0 duration: `group_total × 1 second`
2. **Frame_1 through Frame_N (User Frames):**
   - Uses calibrated DAC values from Frame_0
   - Each group activates for programmed exposure time
   - Closeloop maintains target current throughout
   - Frame_2+ use last_adjusted_dac from previous frame

### How It Works

**Frame_0 Execution:**
1. When the `start` command is received, the system enters Frame_0 (calibration phase) before executing user frames
2. Master broadcasts `000,calibration,start` to enable fast DAC increment on all slave devices
3. Frame_0 cycles through ALL groups sequentially (same as normal frame execution)
4. Each group is activated for a fixed calibration duration (100ms)
5. During this 100ms window, closeloop control actively adjusts the DAC to reach the target current using fast increment
6. At the end of each group's calibration window, the final DAC value is stored as `last_adjusted_dac` for that device
7. After all groups complete calibration, master broadcasts `000,calibration,end` to switch slaves to normal increment
8. Frame_0 ends and normal frame execution (Frame_1, Frame_2, etc.) begins

**Calibration Parameters:**
```cpp
const int dacCalibrationStart = 1300;    // Starting DAC value for calibration
const int calibrationDuration = 100;     // 100ms per group (fast calibration)
```

**Example Timeline (2 groups, 5 user frames):**
```
Frame_0: Group 1 calibration (100ms) → stores result in last_adjusted_dac
Frame_0: Group 2 calibration (100ms) → stores result in last_adjusted_dac
[Frame_0 complete - total time: 400ms]
Frame_1: Group 1 (exposure ms) → uses last_adjusted_dac (from Frame_0)
Frame_1: Group 2 (exposure ms) → uses last_adjusted_dac (from Frame_0)
Frame_2: Group 1 (exposure ms) → uses last_adjusted_dac (updated from Frame_1)
Frame_2: Group 2 (exposure ms) → uses last_adjusted_dac (updated from Frame_1)
... continues for all 5 frames (always using last_adjusted_dac)
```

### Calibration Process Details

**For Each Group During Frame_0:**
1. Master drives TRIGGER_OUT LOW (activates group)
2. All devices in that group initialize `current_dac_value = dacCalibrationStart` (1300)
3. Closeloop control activates (`closeloop_active = true`)
4. `handleCurrentControl()` runs continuously for 100ms:
   - Reads current from INA226
   - Uses **fast DAC increment** (1-35 points based on current deficit) during calibration
   - Adjusts DAC up/down to reach target current (99% of target_current_mA)
   - Uses proportional reduction algorithm when current exceeds target
5. After 100ms, master drives TRIGGER_OUT HIGH
6. Devices in that group:
   - Store final `current_dac_value` as `last_adjusted_dac` (this is the calibrated value!)
   - Set `calibrationComplete = true` flag
   - Deactivate closeloop (`closeloop_active = false`)
7. Move to next group

**Note:** From Frame_1 onward, devices simply use `last_adjusted_dac` as their starting DAC value. No special handling needed since Frame_0's calibrated value is already stored there.

**Best-Effort Calibration:**
- If DAC reaches `dacMax` (2000) before reaching target current, that value is used
- No error is generated - the system uses the best DAC value found

### Logging Format

**Master Device Logging (during Frame_0):**
```
FRAME_0: Calibration Phase Starting...
FRAME_0: G_ID=1, I_TARGET=1300mA
FRAME_0: G_ID=1, I=1287mA, CALIBRATED
FRAME_0: G_ID=2, I_TARGET=1200mA
FRAME_0: G_ID=2, I=1188mA, CALIBRATED
FRAME_0: Calibration Complete
```

**Notes on Logging:**
- Master device shows actual measured current and DAC (since it has direct access)
- Slave devices are assumed calibrated if they're in the active group (master cannot easily read back DAC from slaves)
- "CALIBRATED" status indicates the device completed its 1-second calibration window

### Re-Calibration on Each Start

**Critical Behavior:** Every time `start` is sent, Frame_0 runs fresh calibration
- Handles thermal drift (LED characteristics change with temperature)
- Ensures consistent behavior across runs
- Previous `initial_dac` values are overwritten
- `last_adjusted_dac` is reset to 0 at start of Frame_0

### State Machine Integration

**New State Variables:**
```cpp
bool inCalibrationPhase = false;        // true during Frame_0, false during Frame_1+
bool calibrationComplete = false;       // per-device: true after device calibrates
```

**Frame Numbering:**
- Frame_0: Calibration phase (NOT counted in user's frame count)
- Frame_1 through Frame_N: User frames (N = frameCount set by user)

**Modified Frame Execution Logic:**
```cpp
// When start command received:
inCalibrationPhase = true;
currentFrameLoop = 0;  // Frame_0
calibrationComplete = false;
last_adjusted_dac = 0;  // Reset for fresh calibration

// Frame_0: totalLoops = group_total (one cycle through all groups)
// Frame_1+: totalLoops = frameCount * group_total (user frames)

// DAC Selection Logic:
if (inCalibrationPhase) {
    // Frame_0: Start from calibration starting point
    current_dac_value = dacCalibrationStart;
} else if (last_adjusted_dac > 0) {
    // Frame_1+: Use last frame's DAC (includes Frame_0 calibrated value)
    current_dac_value = last_adjusted_dac;
}
```

**Key Simplification:** The `last_adjusted_dac` serves both purposes:
- After Frame_0: Contains the calibrated DAC value
- After Frame_1+: Contains the runtime-adjusted DAC value

### DeviceStatus Cache Updates

**Modified Structure (Master Only):**
```cpp
struct DeviceStatus {
    int group_id;
    float calibrated_dac;      // Stores initial_dac after Frame_0 (master device only)
    float current;             // Target current in mA
    int exposure;              // Exposure duration in ms
    bool isSet;                // true if program command received
    bool isCalibrated;         // true after Frame_0 completes for this device
};
```

### Error Handling

**INA226 Failure During Calibration:**
- If INA226 unavailable when Frame_0 starts → Abort, report error, don't proceed
- If INA226 fails mid-calibration → Device logs error, don't proceed

**No Programmed Devices:**
- Same as current behavior: `start` validates at least one device programmed before Frame_0

**Partial Calibration:**
- If target current not reached after 1 second → Use best DAC value found, log result
- System continues to Frame_1+ (no abort on partial calibration)

## Program Execution
Assuming a 4 module system, first set the program for all devices:
```
001,program,{1,2,1300,30} 
002,program,{2,2,1200,20} 
003,program,{2,2,1200,20} 
004,program,{1,2,1300,30} 
```

Format: `device_id,program,{group_id,group_total,target_current_mA,exposure_ms}`

Devices 1 and 4 are in group 1, devices 2 and 3 are in group 2. When a new program is set, a variable `current_group` is set to 1 (master and slaves). Here's how that works:

## Frame Programing
Frame programming allows for the execution of a defined program sequence for a specified number of cycles, or "frames". A single frame constitutes one full rotation through all the defined groups.

To control the number of frames and the delay between them, the `frame` command is introduced:

`000,frame,count,interframe_delay`

-   `count`: The number of frames to execute.
-   `interframe_delay`: The duration in milliseconds to hold the `TRIGGER_OUT` pin HIGH after each trigger pulse. This creates a pause between group activations.

This command should be sent to all devices (broadcast address `000`).

**How it works:**

1.  Two new global variables are introduced: `frameCount` (default 1) and `interframeDelay` (default 10).
2.  When the `frame,count,interframe_delay` command is received, these variables are updated.
3.  The total number of execution loops is calculated as `totalLoops = frameCount * group_total`.
4.  When the `start` command is sent to the master device, it will execute the trigger sequence `totalLoops` times.

**Execution Flow:**

When `start` is received, the master device will:
1.  Calculate `totalLoops = frameCount * group_total`.
2.  Reset `current_group` to 1.
3.  For each loop from 1 to `totalLoops`:
    a. Drive `TRIGGER_OUT` LOW for the `exposure` specified in the `program` command for the `current_group`.
    b. Drive `TRIGGER_OUT` HIGH for `interframeDelay` milliseconds to signal the end of the pulse.
    c. This sequence triggers the group rotation on all devices.

**Example:**

Consider a system with `group_total = 2`.

1.  Set the program for all devices (e.g., `001,program,{1,2,1300,20}`, `002,program,{2,2,1200,20}`).
2.  Set 5 frames with a 50ms delay between triggers: `frame,5,50`.
    *   This sets `frameCount = 5` and `interframeDelay = 50`.
    *   User frames: `totalLoops` will be `5 * 2 = 10`.
3.  Send the start command: `start`.
4.  **Frame_0 (Calibration):** The system first runs calibration phase:
    *   Group 1 calibrates for 100ms → finds optimal DAC using fast increment
    *   Group 2 calibrates for 100ms → finds optimal DAC using fast increment
    *   Total Frame_0 time: 400ms
5.  **Frame_1 through Frame_5:** The master device will generate 10 trigger pulses (5 frames × 2 groups). Each pulse will be LOW for the programmed exposure (20ms), followed by a HIGH state for 50ms (interframe delay). The group activation sequence will be: `1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2`.

This allows for precise control over the number of times the entire program sequence is repeated and the timing between each step.

1. `program_success` begins as false (only tracker for master).
2. The GUI will send a `start` command to the master device only.
3. The master will immediately drive its TRIGGER_OUT pin LOW. The duration of this LOW state is determined by the `duration` value set for the currently active group (`current_group`) via the `program` command. This should propogate across the system via our interrupt system. The master device performs no action on receipt of a HIGH->LOW interupt.
4. All devices will only implement their specified intensity and duration based on circular state machine; the master will do this immediately upon receiving the serial command and slaves upon the transition from HIGH->LOW (on their interrupt). As described, the master will control the timing of the 'on' period of intensity via the TRIGGER line.
5. When the duration is complete on the master device, it will drive its TRIGGER_OUT line HIGH (progogating via onChange interrupts). Slave devices will immediately shut off their output and 'rotate' the current state. The master device will also 'rotate' current state when its TRIGGER_IN line goes LOW->HIGH and at that time, set `program_success` to true (this verifies the interrupt signal successfully went thru the entire system). 

**Group Rotation Logic**: Devices only activate their `intensity` and `duration` when `current_group` matches their `group_id`. When rotation occurs, `current_group` increments and wraps around to 1 when it exceeds `group_total`. In our 4 module setup (with group_total=2), `current_group` would 'rotate' as follows:

1->2->1->2...

If we had group_total=4, it would look like this:

1->2->3->4->1->2...

6. The master will always acknowledge the `start` once complete with the program_success value (true or false). This will allow the GUI to pend on the acknowledgement and proceed if there were no errors.

Naturally, the `current_group` can be reset by re-sending the `program` command to each device.

### Real-time Logging
During program execution (after a `start` command is issued), the master device will provide real-time logging to the serial monitor for each trigger pulse. The format for this logging will be:

`FRAME_{current_frame_count}: G_ID={current_group}, I={targetCurrent}mA, EXP={exposure}ms`
-   `{current_frame_count}`: Active frame number from the loop.
-   `{current_group}`: The group ID currently being activated.
-   `{targetCurrent}`: The target current value for the active group.
-   `{exposure}`: The exposure duration in milliseconds for the active group.

Additionally, during closeloop operation, each device may log:
- `OVERCURRENT on device [X]: [current] mA` - When current exceeds safe margin by >50mA
- `EMERGENCY: Current exceeded [limit] mA on device [X]` - When emergency shutdown triggered

### Status Command
The `status` command provides a comprehensive overview of the system's current configuration. The master device will collect and display the following information:

-   **Total number of groups:** `GROUP_TOTAL: {group_total}`
-   **Frame programming settings:**
    -   `FRAME_COUNT: {frameCount}`
    -   `INTERFRAME_DELAY: {interframeDelay}`

To implement this, the master will need to cache the `group_id`, `calibratation_status`, `current`, and `exposure` for each `device_id`.

## Implementation Checklist

**Phase 1: Basic Infrastructure**
- [x] Add pin definitions for triggerInPin (7) and triggerOutPin (9)
- [x] Add new global variables (group_id, group_total, initial_dac, current_group, program_success)
- [x] Update setup() to configure trigger pins and attach interrupt
- [x] Implement basic triggerInterrupt() function with state propagation

**Phase 2: Command Parsing**
- [x] Implement parseProgramCommand() function for curly bracket parsing (4 parameters - no DAC)
- [x] Add "program" command handling to processCommand()
- [x] Add "frame" command handling to processCommand()
- [x] Add "start" command handling to processCommand()
- [x] Test command parsing with malformed input handling

**Phase 3: Program Execution**
- [x] Implement master timing logic for TRIGGER_OUT control
- [x] Add DAC output logic in interrupt handler
- [x] Implement group rotation logic
- [x] Add program_success tracking for master

**Phase 4: Integration & Testing**
- [x] Remove old rxReady/txReady logic from initialization
- [x] Test single device program execution
- [x] Test multi-device chain propagation
- [x] Test group rotation with multiple groups
- [x] Verify program_success acknowledgment

**Phase 5: Current Control Closeloop Implementation** ✅ COMPLETED
- [x] Add INA226 library includes and hardware configuration
- [x] Add current control variables (target_current_mA, safe_current_mA, current_dac_value)
- [x] Update parseProgramCommand() to handle 4 parameters (current, exposure - no DAC)
- [x] Update DeviceStatus cache structure with current/exposure fields
- [x] Implement INA226 initialization functions:
  - [x] initializeINA226() with 140µs conversion time
  - [x] calculateCalibration() for shunt calibration
  - [x] calculateDacReduction() for proportional DAC adjustment
- [x] Add INA226 initialization to setup() with critical error handling
- [x] Implement handleCurrentControl() function:
  - [x] Check closeloop_active state (only run when true)
  - [x] Verify INA226 availability before proceeding
  - [x] Confirm target current is defined
  - [x] Use waitConversionReady(1) for current readings
  - [x] Implement emergency shutdown (current > maxCurrent × 1.01)
  - [x] Apply proportional DAC adjustment algorithm
  - [x] **CRITICAL:** Make this the ONLY place DAC is set during operation
- [x] Update triggerInterrupt() to enable closeloop (NOT set DAC directly)
- [x] Update handleProgramExecution() to enable closeloop (NOT set DAC directly)
- [x] Update getGroupSettings() to return exposure (DAC is auto-calibrated)
- [x] Update printStatus() to display current/exposure in cache
- [x] Update printHelp() with new 4-parameter format examples
- [x] Initialize all new variables in setup()
- [x] Update all debug messages and logging to show target current

**Phase 6: Closeloop Activation Refactor** ✅ COMPLETED
- [x] Add new global variable: `bool closeloop_active = false`
- [x] Add variable to track previous DAC state for edge detection: `int last_dac_state = 0`
- [x] Update `triggerInterrupt()` to control `closeloop_active`:
  - [x] Set `closeloop_active = true` when trigger goes LOW
  - [x] Set `closeloop_active = false` when trigger goes HIGH
  - [x] Remove direct userLedPin control from interrupt
- [x] Update `handleCurrentControl()`:
  - [x] Check `closeloop_active` instead of `userLedPin` state
  - [x] Add DAC state change detection (0↔non-zero)
  - [x] Update `userLedPin` only on DAC state transitions
  - [x] Ensure DAC is set to 0 when `closeloop_active = false`
- [x] Update `handleProgramExecution()`:
  - [x] Ensure final TRIGGER_OUT HIGH pulse is sent after last frame
  - [x] Master sends final trigger to turn off all DACs cleanly
- [x] Update variable initialization in `setup()`

**Phase 7: Testing & Validation**
- [x] Test INA226 initialization and calibration
- [x] Test closeloop regulation with single device
- [x] Verify 99% target regulation accuracy
- [x] Test emergency shutdown and recovery with reinit
- [x] Test multi-device chain with different current targets
- [x] Verify independent current control per device
- [x] Validate timing: closeloop updates during full exposure window
- [x] **Critical:** Test last group DAC turns off properly at program end
- [x] Verify userLedPin accurately reflects DAC state (visual indicator)
- [x] Test closeloop_active flag behavior with trigger signals

**Phase 8: Auto-Calibration System (Frame_0 Implementation)** ✅ COMPLETED
- [x] Update DeviceStatus structure (master only):
- [x] Modify `handleProgramExecution()`:
  - [x] Detect Frame_0 vs Frame_1+ based on `inCalibrationPhase`
  - [x] **Frame_0 Logic:**
    - [x] Use `calibrationDuration` (100ms) for each group
    - [x] Log start of each group: "FRAME_0: G_ID={n}, I_TARGET={current}mA"
    - [x] After group completes, log result for master: "FRAME_0: G_ID={n}, I={measured}mA, DAC={dac}, CALIBRATED"
    - [x] For slaves in active group, log: "FRAME_0: G_ID={n}, I_TARGET={current}mA, CALIBRATED"
    - [x] After all groups calibrated, log: "FRAME_0: Calibration Complete"
    - [x] Set `inCalibrationPhase = false` after Frame_0 ends
    - [x] Proceed to Frame_1 (reset `currentFrameLoop = 1`)
  - [x] **Frame_1+ Logic:**
    - Use programmed `exposure` duration for each group
    - Log as before: "FRAME_{n}: G_ID={group}, I={current}mA, EXP={exposure}ms"
- [x] Update `triggerInterrupt()`:
  - [x] On LOW->HIGH transition during Frame_0:
    - [x] Store `current_dac_value` to `last_adjusted_dac` (this is calibrated value!)
    - [x] Set `calibrationComplete = true` for this device
- [x] Update DAC initialization logic (multiple locations):
  - [x] **In `start` command:** When starting Frame_0, set `current_dac_value = dacCalibrationStart`
  - [x] **In `triggerInterrupt()` (slaves):** 
    - [x] If `inCalibrationPhase`: `current_dac_value = dacCalibrationStart`
    - [x] If `!inCalibrationPhase && last_adjusted_dac > 0`: `current_dac_value = last_adjusted_dac`
    - [x] If `!inCalibrationPhase && last_adjusted_dac == 0`: `current_dac_value = dacCalibrationStart` (fallback)
  - [x] **In `handleProgramExecution()` (master):**
    - [x] Same simplified logic - always use `last_adjusted_dac` after Frame_0
- [x] Update `getGroupSettings()`:
  - [x] **REMOVE:** `group_dac` parameter (no longer needed)
  - [x] Keep only: `group_exposure` return value
  - [x] Update function signature and all call sites
- [x] Update `printStatus()`:
  - [x] Change output format for device cache
  - [x] Keep current/exposure display
- [x] Update `printHelp()`:
  - [x] Update program command format from 5 to 4 parameters
  - [x] Update examples: `001,program,{1,2,1300,20}` (no DAC)
  - [x] Update parameter descriptions (remove initial_dac)
- [x] Update all program command caching:
  - [x] Remove `cache_dac` from cache operations in `loop()` and `processCommand()`
  - [x] Store only: group_id, group_total, current, exposure
  - [x] After Frame_0, master caches its own `initial_dac` to `calibrated_dac`
- [x] Add Frame_0 completion validation:
  - [x] Verify all active groups completed calibration
  - [x] Log warning if any device failed to calibrate (best-effort)
  - [x] Continue to Frame_1+ regardless (don't abort)
- [x] Update variable initialization in `setup()`:
  - [x] Initialize `inCalibrationPhase = false`
  - [x] Initialize `calibrationComplete = false`
  - [x] **REMOVED:** No more `initial_dac` variable - only `last_adjusted_dac = 0`
- [x] Testing Frame_0 System:
  - [x] Test single device, single group calibration
  - [x] Test multi-device, multi-group calibration
  - [x] Verify 100ms calibration duration per group
  - [x] Verify Frame_1 uses `last_adjusted_dac` (calibrated value from Frame_0)
  - [x] Verify Frame_2+ use `last_adjusted_dac` (updated from previous frame)
  - [x] Test re-calibration on multiple `start` commands
  - [x] Verify master logs calibrated DAC correctly
  - [x] Test partial calibration (DAC hits max before target)
  - [x] Verify Frame_0 not counted in user's frame count
  - [x] **SIMPLIFIED:** Verify only one DAC tracking variable (`last_adjusted_dac`) is needed

**Error Handling:**
- [x] Malformed program command structure (invalid curly brackets, missing parameters)
- [x] Invalid group_id or group_total values
- [x] Duration timeout handling for start
- [x] Mismatched current/exposure for devices in the same group_id (DAC is auto-calibrated)
- [x] INA226 initialization failure (system halts with error message)
- [x] INA226 failure during Frame_0 calibration (device logs error, deactivates closeloop)
- [x] Current sensor read failures during operation (closeloop deactivates safely)
- [x] Emergency shutdown coordination across device chain (2 consecutive overcurrent readings)
- [x] Partial calibration handling (best-effort, uses best DAC found, continues to Frame_1+)

**Phase 9: INA226 Healthcheck System (Pre-Start Verification)** ✅ COMPLETED

The healthcheck system ensures all devices in the chain have a functioning INA226 current sensor before any program execution begins. This prevents blind DAC operation and protects LEDs from unregulated current.

### Healthcheck Flow

**Variables (master only):**
```cpp
bool healthcheck_complete = false;      // true when healthcheck response received from chain
int healthcheck_failed_device = 0;      // Device ID that failed healthcheck (0 = all passed)
```

**Pre-Start Verification Sequence (in `start` command handler):**
1. **Master self-check:** Calls `verifyINA226()` to confirm its own sensor is responding
   - If failed → Abort with `ERR:INA226_UNAVAILABLE`, do not proceed
2. **Chain healthcheck (multi-device only):** Master broadcasts `000,healthcheck,0` to chain
3. **Slave verification:** Each slave calls `verifyINA226()` on its own INA226:
   - If upstream device already failed → Forward failure ID without checking own sensor
   - If own INA226 passes → Forward `000,healthcheck,0` (pass)
   - If own INA226 fails → Forward `000,healthcheck,{myDeviceId}` (fail with device ID)
4. **Master receives result:** Final healthcheck response returns through chain
   - `healthcheck_failed_device == 0` → All passed, proceed to Frame_0
   - `healthcheck_failed_device > 0` → Abort with device ID of failed sensor
5. **Timeout protection:** Master waits up to 2 seconds for healthcheck response
   - If no response → Abort with `ERR:HEALTHCHECK_TIMEOUT`

**`verifyINA226()` Function (defense-in-depth):**
```
Step 1: If ina226_available == false → attempt re-initialization
Step 2: Live probe with waitConversionReady(5ms) → read current → verify non-NaN
Step 3: If probe fails → re-init + re-probe
Step 4: Return final status (true = ready, false = failed)
```

**Calibration Start INA226 Checkpoint (slave devices):**
When a slave receives `000,calibration,start`, it performs an additional INA226 verification before entering calibration mode. If this checkpoint fails:
- Slave reports `000,ina_fail,{deviceId}` through chain so master can abort
- Slave does NOT enter calibration mode

### Healthcheck Error Messages
```
ERR:INA226_UNAVAILABLE          - Master's own INA226 not responding
ERR:HEALTHCHECK_TIMEOUT         - No response from chain within 2 seconds
ERR:INA226_UNAVAILABLE          - Specific slave device failed (with device ID in UI message)
HEALTHCHECK:PASS                - All devices passed
HEALTHCHECK:FAIL:DEV{n}         - Device n failed healthcheck
```

### Implementation Checklist
- [x] Add healthcheck variables: `healthcheck_complete`, `healthcheck_failed_device`
- [x] Add master self-check with `verifyINA226()` before chain healthcheck
- [x] Implement `verifyINA226()` with re-initialization fallback
- [x] Implement `probeINA226()` for live I2C verification
- [x] Add healthcheck command broadcast (`000,healthcheck,0`)
- [x] Implement slave healthcheck handler: verify own INA226, forward result
- [x] Implement master healthcheck receiver: capture result, set `healthcheck_complete`
- [x] Add 2-second timeout for healthcheck response
- [x] Add upstream failure pass-through (slaves forward first failure, skip own check)
- [x] Add calibration-start INA226 checkpoint on slave devices
- [x] Abort start if any device fails healthcheck

**Phase 10: Emergency Shutdown System** ✅ COMPLETED

The emergency shutdown system provides multi-layered safety protection against overcurrent, INA226 failures, and uncontrolled DAC operation. It uses a sticky flag (`emergencyShutdownActive`) to prevent trigger re-activation after shutdown until a fresh `start` command is issued.

### Emergency Shutdown Triggers

**1. Overcurrent Protection (in `handleCurrentControl()`):**
- Threshold: `maxCurrent_mA` (1550mA)
- Configurable consecutive count limit: `overcurrent_consecutive_count_limit` (default: 3)
- First over-threshold reading → WARNING logged, DAC increases blocked (hold/decrease only)
- Consecutive readings reaching limit → Full emergency shutdown
- Counter resets when current drops below threshold or closeloop deactivates
- Slave devices report overcurrent to master: `000,overcurrent,{deviceId}`

**2. INA226 Garbage Reading Detection (in `handleCurrentControl()`):**
- Detects NaN or extremely negative readings (< -10mA)
- Indicates I2C communication failure with sensor
- Immediately marks `ina226_available = false`
- Triggers emergency shutdown
- Slave reports to master: `000,ina_fail,{deviceId}`

**3. INA226 Conversion Timeout (in `handleCurrentControl()`):**
- Configurable miss limit: `conversion_miss_count_limit` (default: 3)
- `waitConversionReady(1)` timeout = 1ms
- Consecutive conversion failures → DAC running blind without feedback
- After limit reached → Emergency shutdown, marks `ina226_available = false`
- Slave reports to master: `000,ina_fail,{deviceId}`

**4. Manual Emergency Command:**
- User sends `emergency` or `e` command via serial
- Triggers immediate shutdown on master

### `emergencyShutdown()` Function

```cpp
void emergencyShutdown() {
    bool alreadyActive = emergencyShutdownActive;
    emergencyShutdownActive = true;       // Sticky flag - prevents interrupt re-activation

    analogWrite(dacPin, 0);               // DAC off immediately
    digitalWrite(userLedPin, LOW);

    // Reset runtime control variables (preserve program config for restart via 'start')
    last_adjusted_dac = 0;
    current_dac_value = 0;
    closeloop_active = false;
    dac_output_active = false;
    overcurrent_consecutive_count = 0;
    conversion_miss_count = 0;

    programDuration = 0;                  // Stop frame execution
    frameExecutionActive = false;
    digitalWrite(triggerOutPin, HIGH);    // Ensure trigger line inactive

    // Master broadcasts shutdown to all devices (once - guard prevents infinite loops)
    if (isMasterDevice && !alreadyActive) {
        Serial1.println("000,dac,0");
    }
}
```

### Chain-Wide Emergency Propagation

**Overcurrent on any device:**
```
Device detects overcurrent → emergencyShutdown() locally
  → Slave sends 000,overcurrent,{deviceId} through chain
  → Each slave in chain: emergencyShutdown() + forward
  → Master receives: emergencyShutdown() + broadcasts 000,dac,0
  → All remaining devices: emergencyShutdown()
```

**INA226 failure on any device:**
```
Device detects INA226 failure → emergencyShutdown() locally
  → Slave sends 000,ina_fail,{deviceId} through chain
  → Each slave in chain: emergencyShutdown() + forward
  → Master receives: emergencyShutdown() + broadcasts 000,dac,0
  → All remaining devices: emergencyShutdown()
```

**Master-initiated shutdown (manual or self-detected):**
```
Master calls emergencyShutdown()
  → Broadcasts 000,dac,0 to chain
  → Each slave receives dac,0: emergencyShutdown() + forward to next slave
```

### Emergency State Variables
```cpp
bool emergencyShutdownActive = false;    // Sticky flag: prevents trigger re-activation
const int overcurrent_consecutive_count_limit = 3;   // Configurable overcurrent threshold
const int conversion_miss_count_limit = 3;           // Configurable conversion miss threshold
```

### Recovery
- `emergencyShutdownActive` is a **sticky flag** — it persists until cleared
- Cleared by:
  - `start` command: Clears flag and re-calibrates from Frame_0
  - `calibration,start` (slaves): Clears flag for fresh start
- While active:
  - `handleProgramExecution()` returns immediately (line: `if (!frameExecutionActive || emergencyShutdownActive) return;`)
  - `triggerInterrupt()` will NOT re-activate closeloop (blocked by `emergencyShutdownActive` check)
  - DAC stays at 0, all outputs inactive
- User message: `"System shutdown complete. Use 'start' to re-calibrate and resume."`

### Implementation Checklist
- [x] Add `emergencyShutdownActive` sticky flag variable
- [x] Add configurable `overcurrent_consecutive_count_limit` (default: 3)
- [x] Add configurable `conversion_miss_count_limit` (default: 3)
- [x] Implement `emergencyShutdown()` function with:
  - [x] Sticky flag set (prevents interrupt re-activation)
  - [x] DAC off + userLedPin off
  - [x] Runtime variable reset (preserve program config)
  - [x] Frame execution stop
  - [x] Trigger line HIGH (inactive)
  - [x] Master-only broadcast guard (`!alreadyActive` prevents infinite loops)
- [x] Add overcurrent detection in `handleCurrentControl()`:
  - [x] Consecutive count tracking with configurable limit
  - [x] WARNING on first over-threshold reading
  - [x] DAC increase blocked during overcurrent warning
  - [x] Full shutdown on consecutive limit reached
  - [x] Counter reset on normal reading or closeloop deactivation
- [x] Add INA226 garbage reading detection (NaN / < -10mA):
  - [x] Mark `ina226_available = false`
  - [x] Trigger emergency shutdown
  - [x] Slave reports `000,ina_fail,{deviceId}` to master
- [x] Add INA226 conversion timeout detection:
  - [x] Consecutive miss tracking with configurable limit
  - [x] Emergency shutdown when limit reached
  - [x] Mark `ina226_available = false`
  - [x] Slave reports `000,ina_fail,{deviceId}` to master
- [x] Add chain propagation handlers in `processCommand()`:
  - [x] `ina_fail` command: emergency shutdown + forward
  - [x] `overcurrent` command: emergency shutdown + forward
  - [x] `dac,0` command: emergency shutdown + slave-forward (master consumes)
- [x] Add manual `emergency` / `e` command in master serial handler
- [x] Add `emergencyShutdownActive` guard in `triggerInterrupt()` (prevent closeloop re-activation)
- [x] Add `emergencyShutdownActive` guard in `handleProgramExecution()` (prevent frame execution)
- [x] Clear `emergencyShutdownActive` in `start` command for fresh start
- [x] Clear `emergencyShutdownActive` in slave `calibration,start` handler