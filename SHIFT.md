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
    pinMode(triggerInPin, INPUT_PULLDOWN);  // TRIGGER_IN
    pinMode(triggerOutPin, OUTPUT);          // TRIGGER_OUT
    
    // Attach interrupt for TRIGGER_IN (both master and slaves)
    attachInterrupt(digitalPinToInterrupt(triggerInPin), triggerInterrupt, CHANGE);
    
    // ... rest of setup code ...
}
```

Purpose: these IO lines will now be used as an active LOW one-shot trigger, playing out 'programs' as described below. The master will receive the START signal over serial and begin its program once it sets its own line LOW.

Changes: we will need to poll for incoming serial commands now instead of relying on the IO pins.