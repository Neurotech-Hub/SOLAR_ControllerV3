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

All devices will need to track 5 pieces of information (new variables, default to 0):

1. int group_id = 0;
2. int group_total = 0;
3. float intensity = 0; // assume we are setting DAC value for now, we will close the loop later
4. int current_group = 1; // tracks which group is currently active (1-based)
5. bool program_success = false; // master only: tracks if program execution was successful

This should be established using the following command structure:

000,program,{group_id,group_total,intensity,duration}

**Command Parsing for Curly Bracket Parameters:**
```cpp
// Example parsing function for program command
bool parseProgramCommand(String value, int &group_id, int &group_total, float &intensity, int &duration) {
    // Remove curly brackets
    value = value.substring(1, value.length() - 1);
    
    // Split by comma
    int firstComma = value.indexOf(',');
    int secondComma = value.indexOf(',', firstComma + 1);    
    int thirdComma = value.indexOf(',', secondComma + 1);
    
    if (firstComma == -1 || secondComma == -1 || thirdComma == -1) {
        return false;
    }
    
    group_id = value.substring(0, firstComma).toInt();
    group_total = value.substring(firstComma + 1, secondComma).toInt();
    intensity = value.substring(secondComma + 1, thirdComma).toFloat();
    duration = value.substring(thirdComma + 1).toInt();
    
    return true;
}
```

If `group_id` is set to 0, it shall not execute any program (see below). Therefore, group_ids begin at 1 for active modules.

### Programming Consistency Check
To ensure predictable behavior, all devices within the same `group_id` must have identical `intensity` and `duration` values. The master device will be responsible for caching the settings for each `group_id`.

-   If a `program` command is sent for a new device in an existing group, its `intensity` and `duration` must match the cached values for that group.
-   If the values do not match, the command will be rejected with an error, and the user will be prompted to send a valid command.

## Program Execution
Assuming a 4 module system, first set the program for all devices:
```
001,program,{1,2,1500,30} 
002,program,{2,2,1200,20} 
003,program,{2,2,1200,20} 
004,program,{1,2,1500,30} 
```

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
    a. Drive `TRIGGER_OUT` LOW for the `duration` specified in the `program` command for the `current_group`.
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

`FRAME_{current_frame_count}: G_ID={current_group}, I={intensity}, D={duration}`
-   `{current_frame_count}`: Active frame number from the loop.
-   `{current_group}`: The group ID currently being activated.
-   `{intensity}`: The intensity value for the active group.
-   `{duration}`: The LOW pulse duration for the active group.

### Status Command
The `status` command provides a comprehensive overview of the system's current configuration. The master device will collect and display the following information:

-   **Total number of groups:** `GROUP_TOTAL: {group_total}`
-   **Frame programming settings:**
    -   `FRAME_COUNT: {frameCount}`
    -   `INTERFRAME_DELAY: {interframeDelay}`
-   **Per-device status:** For each device in the chain, a line will be printed in the format:
    `DEV:{device_id}, G_ID:{group_id}, I:{intensity}, D:{duration}`

To implement this, the master will need to cache the `group_id`, `intensity`, and `duration` for each `device_id`.

## Implementation Checklist

**Phase 1: Basic Infrastructure**
- [ ] Add pin definitions for triggerInPin (7) and triggerOutPin (9)
- [ ] Add new global variables (group_id, group_total, intensity, current_group, program_success)
- [ ] Update setup() to configure trigger pins and attach interrupt
- [ ] Implement basic triggerInterrupt() function with state propagation

**Phase 2: Command Parsing**
- [ ] Implement parseProgramCommand() function for curly bracket parsing
- [ ] Add "program" command handling to processCommand()
- [ ] Add "frame" command handling to processCommand()
- [ ] Add "start" command handling to processCommand()
- [ ] Test command parsing with malformed input handling

**Phase 3: Program Execution**
- [ ] Implement master timing logic for TRIGGER_OUT control
- [ ] Add intensity output logic in interrupt handler
- [ ] Implement group rotation logic
- [ ] Add program_success tracking for master

**Phase 4: Integration & Testing**
- [ ] Remove old rxReady/txReady logic from initialization
- [ ] Test single device program execution
- [ ] Test multi-device chain propagation
- [ ] Test group rotation with multiple groups
- [ ] Verify program_success acknowledgment

**Error Handling:**
- [ ] Malformed program command structure (invalid curly brackets, missing parameters)
- [ ] Invalid group_id or group_total values
- [ ] Duration timeout handling for start
- [ ] Mismatched intensity/duration for devices in the same group_id