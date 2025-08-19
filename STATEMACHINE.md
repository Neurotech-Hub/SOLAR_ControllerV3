We need make several changes to our architecture to support higher speed LED control from the DAC, reduced reliance on the closed-loop INA226, and a repurposing of the IO pins currently used for RX_ and TX_Ready.

## IO Pin Repurpose
These need to simply propogate an IO state with zero delay. The master device will be receiving a command to set the logic level. All slave devices need to recognize that state change via interrupt and propogate it accordingly. We effectively want all slaves to have the current RX_ and TX_Ready pins tied for super fast propogation across the system. The pins will need to be mapped as:

- TX
- TRIGGER_OUT
- GND

- RX
- TRIGGER_IN
- GND

**Implementation Details:**
- TRIGGER_IN should respond to an onChange interrupt and immediately write the TRIGGER_IN value to TRIGGER_OUT
- The initialization protocol needs to be fully maintained, no longer relying on RX and TX ready signals, but rather, serial polling only
- Master device ignores its TRIGGER_IN line

**Revised Trigger Architecture:**
- Master device DOES have TRIGGER_IN interrupt
- When master receives LOW on TRIGGER_IN, it immediately brings TRIGGER_OUT HIGH
- This creates a brief one-shot signal that propagates to all slaves
- Master command format: `000,trigger,start`
- Master intercepts and does not relay its own trigger command

**Interrupt Setup:**
```cpp
// In setup() function
void setup() {
    // ... existing setup code ...
    
    // Setup TRIGGER_IN interrupt (both master and slaves)
    pinMode(rxReadyPin, INPUT_PULLDOWN);  // TRIGGER_IN
    pinMode(txReadyPin, OUTPUT);          // TRIGGER_OUT
    
    // Attach interrupt for TRIGGER_IN (both master and slaves)
    attachInterrupt(digitalPinToInterrupt(rxReadyPin), triggerInterrupt, CHANGE);
    
    // ... rest of setup code ...
}
```

Purpose: these IO lines will now be used as an active LOW one-shot trigger, playing out 'programs' as described below. The master will receive the START signal over serial and begin its program once it sets its own line LOW.

Changes: we will need to poll for incoming serial commands now instead of relying on the IO pins.

## PHASE I - Creating Programs for Devices
In Phase I, we will manually assign programs to all devices. A program will define the intensity and timing associated with the one-shot program to control the DAC line. We will define the following:

1. t_on (int16 - milliseconds)
2. duration_on (int16 - milliseconds) 
3. DAC value (int16 - 0-4095)

```
DAC=0   DAC=n              DAC=0
________|------------------|
        t_on --> duration_on
```        

**Implementation Details:**
- Variables can be hardcoded and defaulted to 0
- Applying programs to devices will become part of the higher level GUI operations
- All values limited to int16 range
- If values are invalid, do not set the variables (revert to stored/prior value)

We will send the program itself in a simplified json structure, encoding these variables in milliseconds:

```
001,program,{10,20,1500}
```

This program will need to be stored. Upon receiving a trigger command (via serial for master, and TRIGGER_IN for slaves) the program will play: DAC=0 for 10ms, DAC=1500 for 20ms, DAC=0 (exit). In this manner, we can effectively stagger LED timing.

**Program Execution Rules:**
- Only respond/play the 'program' from trigger (master)/TRIGGER_IN (slave) if we are in CHAIN_READY state
- Use millis() for timing (strict timing required)
- No current control loop - only direct DAC control
- No safety features or error handling - simple process only
- Programs are blocking until completed
- Programs reset to defaults on power cycle
- Only one program per device
- If new program command received while running, ignore until current program completes
- Previous program values are maintained if new program fails to parse

**ISR Context Considerations:**
- Slaves can propagate trigger state in ISR context (simple digitalWrite)
- Program execution happens in main loop, not ISR
- ISR only sets flags and propagates trigger signal

**Sample Program Execution Code:**
```cpp
// Program variables (stored per device)
int16_t program_t_on = 0;        // Delay before DAC on (ms)
int16_t program_duration_on = 0; // Duration DAC stays on (ms)  
int16_t program_dac_value = 0;   // DAC value to set

// Program execution state
bool program_running = false;
unsigned long program_start_time = 0;
unsigned long program_phase_start = 0;
enum ProgramPhase { WAITING, DAC_ON, DAC_OFF, COMPLETE };

// TRIGGER_IN interrupt handler
void triggerInterrupt() {
    if (currentState == CHAIN_READY && !program_running) {
        // Start program execution
        program_running = true;
        program_start_time = millis();
        program_phase_start = millis();
        
        // Immediately propagate trigger to next device
        digitalWrite(txReadyPin, digitalRead(rxReadyPin));
        
        // Start with DAC off
        analogWrite(dacPin, 0);
    }
}

// Master-specific trigger interrupt handler
void masterTriggerInterrupt() {
    if (currentState == CHAIN_READY && !program_running) {
        // Master: when TRIGGER_IN goes LOW, immediately set TRIGGER_OUT HIGH
        digitalWrite(txReadyPin, HIGH);
        
        // Start program execution
        program_running = true;
        program_start_time = millis();
        program_phase_start = millis();
        
        // Start with DAC off
        analogWrite(dacPin, 0);
    }
}

// Program execution in main loop
void executeProgram() {
    if (!program_running) return;
    
    unsigned long current_time = millis();
    unsigned long elapsed = current_time - program_start_time;
    
    if (elapsed < program_t_on) {
        // Phase 1: Waiting before DAC on
        // DAC remains at 0
    }
    else if (elapsed < (program_t_on + program_duration_on)) {
        // Phase 2: DAC on
        analogWrite(dacPin, program_dac_value);
    }
    else {
        // Phase 3: Program complete
        analogWrite(dacPin, 0);
        program_running = false;
    }
}

// Parse program command (similar to existing parseCommand function)
bool parseProgramCommand(String data, int16_t &t_on, int16_t &duration_on, int16_t &dac_value) {
    // Expected format: "001,program,{10,20,1500}"
    
    // Find the opening brace
    int braceStart = data.indexOf('{');
    int braceEnd = data.indexOf('}');
    
    if (braceStart == -1 || braceEnd == -1) {
        return false;
    }
    
    // Extract the values between braces
    String values = data.substring(braceStart + 1, braceEnd);
    
    // Parse the three comma-separated values
    int firstComma = values.indexOf(',');
    int secondComma = values.indexOf(',', firstComma + 1);
    
    if (firstComma == -1 || secondComma == -1) {
        return false;
    }
    
    // Extract and validate each value
    int16_t temp_t_on = values.substring(0, firstComma).toInt();
    int16_t temp_duration_on = values.substring(firstComma + 1, secondComma).toInt();
    int16_t temp_dac_value = values.substring(secondComma + 1).toInt();
    
    // Validate ranges (int16 limits)
    if (temp_t_on < 0 || temp_t_on > 32767) return false;
    if (temp_duration_on < 0 || temp_duration_on > 32767) return false;
    if (temp_dac_value < 0 || temp_dac_value > 4095) return false;
    
    // All valid, assign to output parameters
    t_on = temp_t_on;
    duration_on = temp_duration_on;
    dac_value = temp_dac_value;
    
    return true;
}

// Program command handling (add to existing processCommand function)
void handleProgramCommand(Command &cmd) {
    int16_t t_on, duration_on, dac_value;
    
    if (parseProgramCommand(cmd.value, t_on, duration_on, dac_value)) {
        // Store the program parameters
        program_t_on = t_on;
        program_duration_on = duration_on;
        program_dac_value = dac_value;
        
        if (Serial) {
            Serial.println("DEBUG: Program stored - t_on:" + String(t_on) + 
                          "ms, duration:" + String(duration_on) + 
                          "ms, DAC:" + String(dac_value));
        }
    } else {
        if (Serial) {
            Serial.println("DEBUG: Invalid program format: " + cmd.value);
        }
    }
}

// Add to existing processCommand function:
// else if (cmd.command == "program")
// {
//     handleProgramCommand(cmd);
// }

// Trigger command handling (master only)
void handleTriggerCommand(Command &cmd) {
    if (!isMasterDevice) return; // Only master handles trigger commands
    
    if (cmd.value == "start" && currentState == CHAIN_READY && !program_running) {
        // Master initiates trigger cascade
        // Set TRIGGER_OUT LOW to start the cascade
        digitalWrite(txReadyPin, LOW);
        
        if (Serial) {
            Serial.println("DEBUG: Master initiated trigger cascade");
        }
    }
}

// Add to existing processCommand function:
// else if (cmd.command == "trigger")
// {
//     handleTriggerCommand(cmd);
// }

### Example Program on Oscilloscope

```
001 and 003
________|------------------|
        10                 30
time (ms)

002 and 004
___________________________|------------------|
        10                 30                  50
time (ms)
```

## Command Protocol Changes
**Implementation Details:**
- The new `program` command should co-exist with current commands (servo, current, dac)
- Fully compatible with current command set
- Master device needs a new command for the initial trigger cascade
- This implementation should leave most of our code unscathed
- Can prune other commands later as needed

## State Machine Integration
**Implementation Details:**
- Only execute programs when in CHAIN_READY state
- No new states required - integrate with existing state machine
- Maintain all existing functionality
- No changes to current control during program execution (DAC direct control only)

