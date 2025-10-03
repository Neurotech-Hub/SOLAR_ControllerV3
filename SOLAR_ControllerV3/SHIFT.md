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
3. float initial_dac = 0; // Initial DAC value to start exposure
4. float target_current_mA = 0; // Target current for closeloop regulation
5. int duration = 0; // exposure duration in milliseconds
6. int current_group = 1; // tracks which group is currently active (1-based)
7. bool program_success = false; // master only: tracks if program execution was successful

This should be established using the following command structure:

000,program,{group_id,group_total,dac,current,exposure}

**Parameter Details:**
- `group_id`: Group number for this device (1-n, 0=inactive)
- `group_total`: Total number of groups in the system
- `dac`: Initial DAC value (0-4095) to start the LED exposure
- `current`: Target current in mA (0-1500) for closeloop regulation
- `exposure`: LED exposure duration in milliseconds (1-100)

**Command Parsing for Curly Bracket Parameters:**
```cpp
// Example parsing function for program command (5 parameters)
bool parseProgramCommand(String value, int &group_id, int &group_total, 
                         float &dac, float &current, int &exposure) {
    // Remove curly brackets
    value = value.substring(1, value.length() - 1);
    
    // Split by comma - expecting 5 parameters
    int firstComma = value.indexOf(',');
    int secondComma = value.indexOf(',', firstComma + 1);    
    int thirdComma = value.indexOf(',', secondComma + 1);
    int fourthComma = value.indexOf(',', thirdComma + 1);
    
    if (firstComma == -1 || secondComma == -1 || 
        thirdComma == -1 || fourthComma == -1) {
        return false;
    }
    
    group_id = value.substring(0, firstComma).toInt();
    group_total = value.substring(firstComma + 1, secondComma).toInt();
    dac = value.substring(secondComma + 1, thirdComma).toFloat();
    current = value.substring(thirdComma + 1, fourthComma).toFloat();
    exposure = value.substring(fourthComma + 1).toInt();
    
    return true;
}
```

If `group_id` is set to 0, it shall not execute any program (see below). Therefore, group_ids begin at 1 for active modules.

### Programming Consistency Check
To ensure predictable behavior, all devices within the same `group_id` must have identical `dac`, `current`, and `exposure` values. The master device will be responsible for caching the settings for each `group_id`.

-   If a `program` command is sent for a new device in an existing group, its `dac`, `current`, and `exposure` must match the cached values for that group.
-   If the values do not match, the command will be rejected with an error, and the user will be prompted to send a valid command.

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
  - Overcurrent ≥500mA: reduce DAC by 100
  - Overcurrent ≥200mA: reduce DAC by 50
  - Overcurrent ≥100mA: reduce DAC by 20
  - Under target: increment DAC by 1

### Overcurrent Protection
- **Emergency Shutdown:** Immediate shutdown if current > maxCurrent × 1.01 (1515mA)
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
9. Updates `userLedPin` to reflect DAC state (HIGH when DAC>0, LOW when DAC=0)

**Critical Rules for DAC Control:**
- **DAC is NEVER set outside `handleCurrentControl()`** during closeloop operation
- **Closeloop Activation:**
  - New variable: `bool closeloop_active` controls when closeloop runs
  - Interrupt handler sets `closeloop_active = true` when TRIGGER_OUT goes LOW (exposure starts)
  - Interrupt handler sets `closeloop_active = false` when TRIGGER_OUT goes HIGH (exposure ends)
  - Closeloop immediately stops when `closeloop_active = false`
- **userLedPin as Visual Indicator:**
  - `userLedPin` is updated in `handleCurrentControl()` only on DAC state changes
  - HIGH when DAC transitions from 0 to non-zero
  - LOW when DAC transitions from non-zero to 0
  - Provides visual feedback of LED activity
- All DAC updates are dynamically controlled by real-time current feedback
- No DAC value is transmitted without active INA226 feedback
- Initial DAC value is stored in `current_dac_value` but not applied until closeloop runs
- When `TRIGGER_OUT` goes LOW, closeloop is enabled but DAC remains at 0
- First call to `handleCurrentControl()` applies initial DAC and begins regulation

**End-of-Program Safety (Fix for Last Group Issue):**
- **Problem:** Last active group's DAC remains ON after program completion because no trigger HIGH is sent
- **Solution:** Master ensures final TRIGGER_OUT HIGH pulse after last exposure completes
- **Implementation:**
  - After final frame's exposure duration, master sets TRIGGER_OUT HIGH
  - This propagates through interrupt chain to all devices
  - All devices with `closeloop_active = true` will set it to false
  - `handleCurrentControl()` stops running and sets DAC to 0
  - Ensures clean shutdown of all devices at program completion
- **No emergency broadcast needed** - proper trigger signaling handles cleanup

## Program Execution
Assuming a 4 module system, first set the program for all devices:
```
001,program,{1,2,1806,1300,30} 
002,program,{2,2,1750,1200,20} 
003,program,{2,2,1750,1200,20} 
004,program,{1,2,1806,1300,30} 
```

Format: `device_id,program,{group_id,group_total,initial_dac,target_current_mA,exposure_ms}`

Devices 0 (master) and 2, and devices 1 and 3 are in the same group and will operate together. When a new program is set, a variable `current_group` is set to 1 (master and slaves). Here's how that works:

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

1.  Set the program for all devices (e.g., `000,program,{g_id,2,intensity,20}`).
2.  Set 5 frames with a 50ms delay between triggers: `000,frame,5,50`.
    *   This sets `frameCount = 5` and `interframeDelay = 50`.
    *   `totalLoops` will be `5 * 2 = 10`.
3.  Send the start command: `start`.
4.  The master device will now generate 10 trigger pulses. Each pulse will be LOW for 20ms, followed by a HIGH state for 50ms. The group activation sequence will be: `1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2 -> 1 -> 2`.
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
-   **Per-device status:** For each device in the chain, a line will be printed in the format:
    `DEV:{device_id}, G_ID:{group_id}, DAC:{dac}, I:{current}mA, EXP:{exposure}ms`

To implement this, the master will need to cache the `group_id`, `dac`, `current`, and `exposure` for each `device_id`.

## Implementation Checklist

**Phase 1: Basic Infrastructure**
- [x] Add pin definitions for triggerInPin (7) and triggerOutPin (9)
- [x] Add new global variables (group_id, group_total, initial_dac, current_group, program_success)
- [x] Update setup() to configure trigger pins and attach interrupt
- [x] Implement basic triggerInterrupt() function with state propagation

**Phase 2: Command Parsing**
- [x] Implement parseProgramCommand() function for curly bracket parsing (4 parameters)
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

**Phase 5: Current Control Closeloop Implementation**
- [x] Add INA226 library includes and hardware configuration
- [x] Add current control variables (target_current_mA, safe_current_mA, current_dac_value)
- [x] Update parseProgramCommand() to handle 5 parameters (dac, current, exposure)
- [x] Update DeviceStatus cache structure with dac/current/exposure fields
- [x] Implement INA226 initialization functions:
  - [x] initializeINA226() with 140µs conversion time
  - [x] calculateCalibration() for shunt calibration
  - [x] calculateDacReduction() for proportional DAC adjustment
- [x] Add INA226 initialization to setup() with critical error handling
- [x] Implement handleCurrentControl() function:
  - [x] Check userLedPin state (only run when HIGH)
  - [x] Verify INA226 availability before proceeding
  - [x] Confirm target current is defined
  - [x] Use waitConversionReady(1) for current readings
  - [x] Implement emergency shutdown (current > maxCurrent × 1.01)
  - [x] Apply proportional DAC adjustment algorithm
  - [x] **CRITICAL:** Make this the ONLY place DAC is set during operation
- [x] Update triggerInterrupt() to enable closeloop (NOT set DAC directly)
- [x] Update handleProgramExecution() to enable closeloop (NOT set DAC directly)
- [x] Update getGroupSettings() to return dac and exposure
- [x] Update printStatus() to display dac/current/exposure in cache
- [x] Update printHelp() with new 5-parameter format examples
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
- [ ] Test INA226 initialization and calibration
- [ ] Test closeloop regulation with single device
- [ ] Verify 99% target regulation accuracy
- [ ] Test emergency shutdown and recovery with reinit
- [ ] Test multi-device chain with different current targets
- [ ] Verify independent current control per device
- [ ] Validate timing: closeloop updates during full exposure window
- [ ] **Critical:** Test last group DAC turns off properly at program end
- [ ] Verify userLedPin accurately reflects DAC state (visual indicator)
- [ ] Test closeloop_active flag behavior with trigger signals

**Error Handling:**
- [x] Malformed program command structure (invalid curly brackets, missing parameters)
- [x] Invalid group_id or group_total values
- [x] Duration timeout handling for start
- [x] Mismatched dac/current/exposure for devices in the same group_id
- [ ] INA226 initialization failure (halt system with error)
- [ ] Current sensor read failures during operation
- [ ] Emergency shutdown coordination across device chain