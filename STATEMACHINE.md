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

```
001,program,{10,20,1500}
002,program,{30,20,1800}
003,program,{10,20,1500}
004,program,{30,20,1800}
```
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
