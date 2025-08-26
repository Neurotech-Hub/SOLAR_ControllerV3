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

000,program,{group_id,group_total,intensity}

**Command Parsing for Curly Bracket Parameters:**
```cpp
// Example parsing function for program command
bool parseProgramCommand(String value, int &group_id, int &group_total, float &intensity) {
    // Remove curly brackets
    value = value.substring(1, value.length() - 1);
    
    // Split by comma
    int firstComma = value.indexOf(',');
    int secondComma = value.indexOf(',', firstComma + 1);
    
    if (firstComma == -1 || secondComma == -1) {
        return false;
    }
    
    group_id = value.substring(0, firstComma).toInt();
    group_total = value.substring(firstComma + 1, secondComma).toInt();
    intensity = value.substring(secondComma + 1).toFloat();
    
    return true;
}
```

If `group_id` is set to 0, it shall not execute any program (see below). Therefore, group_ids begin at 1 for active modules.

## Program Execution
Assuming a 4 module system, first set the program for all devices:

000,program,{1,4,1400}
001,program,{2,4,1300}
002,program,{1,4,1400}
003,program,{2,4,1300}

Devices 0 (master) and 2, and devices 1 and 3 are in the same group and will operate together. When a new program is set, a variable `current_group` is set to 1 (master and slaves). Here's how that works:

1. `program_success` begins as false (only tracker for master).
2. The GUI will send a `start_program` command to the master device only with a specified duration in milliseconds (max 100ms): 000,start_program,20
3. The master will immediately drive its TRIGGER_OUT pin LOW for the specified duration. This should propogate across the system via our interrupt system. The master device performs no action on receipt of a HIGH->LOW interupt.
4. All devices will only implement their specified intensity based on circular state machine; the master will do this immediately upon receiving the serial command and slaves upon the transition from HIGH->LOW (on their interrupt). As described, the master will control the timing of the 'on' period of intensity via the TRIGGER line.
5. When the duration is complete on the master device, it will drive its TRIGGER_OUT line HIGH (progogating via onChange interrupts). Slave devices will immediately shut off their output and 'rotate' the current state. The master device will also 'rotate' current state when its TRIGGER_IN line goes LOW->HIGH and at that time, set `program_success` to true (this verifies the interrupt signal successfully went thru the entire system). 

**Group Rotation Logic**: Devices only activate their intensity when `current_group` matches their `group_id`. When rotation occurs, `current_group` increments and wraps around to 1 when it exceeds `group_total`. In our 4 module setup (with group_total=2), `current_group` would 'rotate' as follows:

1->2->1->2...

If we had group_total=4, it would look like this:

1->2->3->4->1->2...

6. The master will always acknowledge the `start_program` once complete with the program_success value (true or false). This will allow the GUI to pend on the acknowledgement and proceed if there were no errors.

Naturally, the `current_group` can be reset by re-sending the `program` command to each device.

## Implementation Checklist

**Phase 1: Basic Infrastructure**
- [ ] Add pin definitions for triggerInPin (7) and triggerOutPin (9)
- [ ] Add new global variables (group_id, group_total, intensity, current_group, program_success)
- [ ] Update setup() to configure trigger pins and attach interrupt
- [ ] Implement basic triggerInterrupt() function with state propagation

**Phase 2: Command Parsing**
- [ ] Implement parseProgramCommand() function for curly bracket parsing
- [ ] Add "program" command handling to processCommand()
- [ ] Add "start_program" command handling to processCommand()
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
- [ ] Duration timeout handling for start_program