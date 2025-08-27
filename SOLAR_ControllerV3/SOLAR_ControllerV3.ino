// SOLAR Controller V3 - High Speed LED Control with Trigger Chain
// Based on SHIFT.md specifications for trigger-based group control

#include <Arduino.h>
#include <Servo.h>

// Version tracking
const String CODE_VERSION = "3.001";

// Pin assignments for ItsyBitsy M4
const int dacPin = A0;          // DAC output (12-bit, 0-4095)
const int servoPin = 5;         // Servo control
const int ledPin = 13;          // Status LED
const int triggerInPin = 7;     // TRIGGER_IN
const int triggerOutPin = 9;    // TRIGGER_OUT
const int userLedPin = 2;       // User LED
// Serial1 uses D1 (TX) and D0 (RX)

// DAC Parameters
const int dacMin = 0;            // Minimum DAC value
const int dacMax = 2000;         // Maximum DAC value

// Command structure
struct Command
{
    int deviceId;
    String command;
    String value;
};

// Device states
enum DeviceState
{
    WAITING_FOR_CHAIN, // Waiting for serial chain to establish
    CHAIN_READY,       // Chain is ready, normal operation
    INIT_IN_PROGRESS,  // Only used by master during initialization
    PROCESSING,        // Master is processing a command
    READY              // Master is ready to receive new commands
};

// Global variables
DeviceState currentState = WAITING_FOR_CHAIN;
bool isMasterDevice = false;
int myDeviceId = 0;
int totalDevices = 0;
Servo myServo;
int currentServoPos = 90;
int targetServoPos = 90;
String pendingCommand = ""; // Track master's current command

// Program control variables
int group_id = 0;           // This device's group ID (0 = inactive)
int group_total = 0;        // Total number of groups in system
float intensity = 0;      // Target intensity (DAC value for now)
int current_group = 1;      // Currently active group (1-based)
bool program_success = false; // Master only: tracks program execution success

// DAC control variables
int currentDacValue = 0;
int targetDacValue = 0;

// Timing variables
unsigned long programStartTime = 0;
unsigned long programDuration = 0;

// Volatile variables for interrupt handling
volatile bool triggerStateChanged = false;
volatile bool lastTriggerState = HIGH;

// Function declarations
void updateServo();
void processCommand(String data);
bool parseCommand(String data, Command &cmd);
bool parseProgramCommand(String value, int &group_id, int &group_total, float &intensity);
void updateStatusLED();
void waitForChain();
void reinitializeDevices();
void printHelp();
void printStatus();
void sendCommandAndWait(String command);
void emergencyShutdown();
void triggerInterrupt();
void handleProgramExecution();

void setup()
{
    // Initialize pins
    pinMode(dacPin, OUTPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(triggerInPin, INPUT_PULLDOWN);  // TRIGGER_IN
    pinMode(triggerOutPin, OUTPUT);         // TRIGGER_OUT
    pinMode(userLedPin, OUTPUT);

    // Initialize outputs
    digitalWrite(triggerOutPin, HIGH); // active LOW, default HIGH
    digitalWrite(userLedPin, LOW);

    // Setup TRIGGER_IN interrupt (all devices)
    attachInterrupt(digitalPinToInterrupt(triggerInPin), triggerInterrupt, CHANGE);

    // Initialize servo
    myServo.attach(servoPin);
    myServo.write(90);

    // Initialize DAC - always start with 0 for safety
    analogWriteResolution(12);  // Set 12-bit resolution for ItsyBitsy M4
    analogWrite(dacPin, 0);     // Start with DAC off
    
    // Reset all control variables to safe state
    currentDacValue = 0;
    targetDacValue = 0;

    // Initialize program variables
    group_id = 0;
    group_total = 0;
    intensity = 0;
    current_group = 1;
    program_success = false;

    // Initialize Serial interfaces
    Serial.begin(115200); // USB
    Serial1.begin(115200);  // Device chain

    // Wait for either Serial (master) or chain communication (slave)
    while (!isMasterDevice)
    {
        if (Serial)
        {
            isMasterDevice = true;
            myDeviceId = 1; // Master is always device 1
            Serial.println("\nSOLAR Controller V3 Starting...");
            Serial.println("VER:" + CODE_VERSION);
            Serial.println("DEBUG: Master device initialized");
            break;
        }
        // For slaves, wait for serial communication to establish identity
        if (Serial1.available())
        {
            isMasterDevice = false;
            break;
        }
        digitalWrite(ledPin, (millis() / 500) % 2); // Blink while waiting
        delay(10);                                  // Small delay to prevent tight loop
    }

    // Initial chain connection - using serial polling only (no rxReady/txReady)
    waitForChain();
}

void loop()
{
    // Update LED status
    updateStatusLED();

    // Handle program execution timing (master only)
    if (isMasterDevice && programDuration > 0)
    {
        handleProgramExecution();
    }

    // Master: Handle USB Serial commands
    if (isMasterDevice && Serial.available())
    {
        String command = Serial.readStringUntil('\n');
        command.trim();
        if (command.length() > 0)
        {
            // Handle system commands directly on master
            if (command == "help")
            {
                printHelp();
                return;
            }
            else if (command == "status")
            {
                printStatus();
                return;
            }
            else if (command == "reinit")
            {
                reinitializeDevices();
                return;
            }

            else if (command == "emergency" || command == "e")
            {
                emergencyShutdown();
                return;
            }


            // Parse and validate regular command
            Command cmd;
            if (!parseCommand(command, cmd))
            {
                Serial.println("ERR:INVALID_FORMAT");
                Serial.println("UI:Use format: deviceId,command,value");
                Serial.println("UI:Example: 001,servo,90 or 000,program,{1,4,1400}");
                return;
            }

            // Validate command type
            if (cmd.command != "servo" && cmd.command != "dac" && 
                cmd.command != "init" && cmd.command != "program" && 
                cmd.command != "start_program")
            {
                Serial.println("ERR:INVALID_COMMAND:" + cmd.command);
                Serial.println("UI:Valid commands: servo, dac, program, start_program");
                return;
            }

            // Validate device ID range (except for broadcast)
            if (cmd.deviceId != 0)
            {
                if (cmd.deviceId > totalDevices)
                {
                    Serial.println("ERR:INVALID_DEVICE:" + String(cmd.deviceId) + " (max:" + String(totalDevices) + ")");
                    return;
                }
            }

            // Validate command values
            if (cmd.command == "servo")
            {
                int angle = cmd.value.toInt();
                if (angle < 60 || angle > 120)
                {
                    Serial.println("ERR:SERVO_RANGE:" + String(angle));
                    Serial.println("UI:Servo angle must be 60-120 degrees");
                    return;
                }
            }

            else if (cmd.command == "dac")
            {
                int value = cmd.value.toInt();
                if (value < 0 || value > 4095)
                {
                    Serial.println("ERR:DAC_RANGE:" + String(value));
                    Serial.println("UI:DAC value must be 0-4095");
                    return;
                }
            }
            else if (cmd.command == "start_program")
            {
                int duration = cmd.value.toInt();
                if (duration < 1 || duration > 100)
                {
                    Serial.println("ERR:DURATION_RANGE:" + String(duration));
                    Serial.println("UI:Duration must be 1-100 milliseconds");
                    return;
                }
            }

            // Handle start_program command locally for master
            if (cmd.command == "start_program" && cmd.deviceId == 0)
            {
                int duration = cmd.value.toInt();
                Serial.println("DEBUG: Starting program with duration: " + String(duration) + "ms");
                
                // Reset program success flag
                program_success = false;
                
                // Start the program timing
                programStartTime = millis();
                programDuration = duration;
                
                // Immediately drive TRIGGER_OUT LOW to start the pulse
                digitalWrite(triggerOutPin, LOW);
                
                // If master has a group assignment, activate it immediately
                if (current_group == group_id && group_id > 0) {
                    analogWrite(dacPin, (int)intensity);
                    digitalWrite(userLedPin, HIGH);
                }
                
                Serial.println("DEBUG: Program started, TRIGGER_OUT driven LOW");
                return;
            }

            // Send valid command to chain
            pendingCommand = command;  // Track the command we're sending
            currentState = PROCESSING; // Set state to PROCESSING while waiting for command return
            Serial1.println(command);
            Serial.println("DEBUG: Sent to chain: " + command);
        }
    }

    // All devices: Process chain communication (Serial1)
    if (Serial1.available())
    {
        String data = Serial1.readStringUntil('\n');
        data.trim();

        if (data.length() > 0)
        {
            if (isMasterDevice)
            {
                Serial.println("DEBUG: Received from chain: " + data);
            }
            processCommand(data);
        }
    }

    // Handle servo movement
    updateServo();
}

// Interrupt handler for TRIGGER_IN changes
void triggerInterrupt()
{
    // Read current trigger state
    bool triggerState = digitalRead(triggerInPin);
    
    // Immediately propagate to next device (except for master's own signal)
    if (!isMasterDevice || triggerState != lastTriggerState) {
        digitalWrite(triggerOutPin, triggerState);
    }
    
    if (triggerState == LOW && lastTriggerState == HIGH) {
        // HIGH->LOW transition: Start intensity output
        if (current_group == group_id && group_id > 0) {
            analogWrite(dacPin, (int)intensity);
            digitalWrite(userLedPin, HIGH);
        }
    } else if (triggerState == HIGH && lastTriggerState == LOW) {
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
            Serial.println("DEBUG: Program cycle complete, program_success = true");
        }
    }
    
    lastTriggerState = triggerState;
}

void handleProgramExecution()
{
    // Check if program duration has elapsed
    if (millis() - programStartTime >= programDuration) {
        // Drive TRIGGER_OUT HIGH to end the pulse
        digitalWrite(triggerOutPin, HIGH);
        
        // Reset program timing
        programDuration = 0;
        
        Serial.println("DEBUG: Program duration complete, TRIGGER_OUT driven HIGH");
        
        // Send acknowledgment with program success status
        Serial.println("PROGRAM_ACK:" + String(program_success ? "true" : "false"));
    }
}

void processCommand(String data)
{
    Command cmd;
    if (!parseCommand(data, cmd))
    {
        if (isMasterDevice)
        {
            Serial.println("DEBUG: Failed to parse chain command: " + data);
        }
        return;
    }

    if (Serial && !isMasterDevice)
    {
        Serial.println("DEBUG: Device " + String(myDeviceId) + " processing: " + data);
    }

    // Handle initialization command
    if (cmd.command == "init")
    {
        if (isMasterDevice)
        {
            if (currentState == INIT_IN_PROGRESS)
            {
                // Received final count
                totalDevices = cmd.value.toInt();
                currentState = CHAIN_READY;
                Serial.println("DEBUG: Init complete - found " + String(totalDevices) + " devices");
                Serial.println("INIT:TOTAL:" + String(totalDevices));
                Serial.println("UI:Ready for commands");
            }
            else
            {
                Serial.println("DEBUG: Ignoring init command in state: " + String(currentState));
            }
        }
        else
        {
            // Slave: take ID and increment
            myDeviceId = cmd.value.toInt() + 1;
            // Forward incremented count
            String nextInit = "000,init," + String(myDeviceId);
            if (Serial)
            {
                Serial.println("DEBUG: Slave taking ID " + String(myDeviceId));
                Serial.println("DEBUG: Forwarding: " + nextInit);
            }
            Serial1.println(nextInit);
            if (Serial)
            {
                Serial.println("INIT:DEV:" + String(myDeviceId));
            }
        }
        return;
    }

    // Process command if:
    // 1. It's a broadcast (000)
    // 2. OR it matches our device ID exactly
    bool isForUs = (cmd.deviceId == 0) || (cmd.deviceId == myDeviceId);

    if (isForUs)
    {
        if (Serial && !isMasterDevice)
        {
            Serial.println("DEBUG: Device " + String(myDeviceId) + " executing command");
        }

        // Handle program command
        if (cmd.command == "program")
        {
            int new_group_id, new_group_total;
            float new_intensity;
            
            if (parseProgramCommand(cmd.value, new_group_id, new_group_total, new_intensity))
            {
                group_id = new_group_id;
                group_total = new_group_total;
                intensity = new_intensity;
                current_group = 1; // Reset to group 1 when new program is set
                
                if (Serial)
                {
                    Serial.println("DEBUG: Program set - Group:" + String(group_id) + 
                                   " Total:" + String(group_total) + 
                                   " Intensity:" + String(intensity));
                }
            }
            else
            {
                if (Serial)
                {
                    Serial.println("ERROR: Failed to parse program command: " + cmd.value);
                }
            }
        }
        // Handle servo command
        else if (cmd.command == "servo")
        {
            int angle = cmd.value.toInt();
            angle = constrain(angle, 60, 120);
            targetServoPos = angle;
            if (Serial)
            {
                Serial.println("DEBUG: Servo set to:" + String(angle));
            }
        }

        // Handle DAC command (direct control)
        else if (cmd.command == "dac")
        {
            int value = cmd.value.toInt();
            value = constrain(value, dacMin, dacMax);
            targetDacValue = value;
            currentDacValue = value;
            analogWrite(dacPin, value);
            digitalWrite(userLedPin, value > 1200 ? HIGH : LOW);
            if (Serial)
            {
                Serial.println("DEBUG: Target DAC:" + String(value));
            }
        }
    }

    // Always forward unless we're master and it's our command returning
    if (!isMasterDevice || (isMasterDevice && data != pendingCommand))
    {
        if (Serial && !isMasterDevice)
        {
            Serial.println("DEBUG: Device " + String(myDeviceId) + " forwarding: " + data);
        }
        Serial1.println(data);
    }
    else if (isMasterDevice && data == pendingCommand)
    {
        currentState = READY;  // Reset state when command returns
        pendingCommand = "";   // Clear pending command
        Serial.println("EOT"); // Command made it around the chain
    }
}

bool parseCommand(String data, Command &cmd)
{
    int firstComma = data.indexOf(',');
    int secondComma = data.indexOf(',', firstComma + 1);

    if (firstComma == -1 || secondComma == -1)
    {
        return false;
    }

    cmd.deviceId = data.substring(0, firstComma).toInt();
    cmd.command = data.substring(firstComma + 1, secondComma);
    cmd.value = data.substring(secondComma + 1);

    return true;
}

bool parseProgramCommand(String value, int &group_id, int &group_total, float &intensity)
{
    // Remove curly brackets
    if (value.length() < 3 || value.charAt(0) != '{' || value.charAt(value.length() - 1) != '}') {
        return false;
    }
    
    value = value.substring(1, value.length() - 1);
    
    // Split by comma
    int firstComma = value.indexOf(',');
    int secondComma = value.indexOf(',', firstComma + 1);
    
    if (firstComma == -1 || secondComma == -1) {
        return false;
    }
    
    group_id = value.substring(0, firstComma).toInt();
    group_total = value.substring(firstComma + 1, secondComma).toInt();
    intensity = value.substring(secondComma + 1).toInt();
    
    // Validate parsed values
    if (group_total < 1 || group_id < 0 || group_id > group_total || intensity < 0 || intensity > 4095) {
        return false;
    }
    
    return true;
}

void updateServo()
{
    if (currentServoPos < targetServoPos)
    {
        currentServoPos++;
        myServo.write(currentServoPos);
    }
    else if (currentServoPos > targetServoPos)
    {
        currentServoPos--;
        myServo.write(currentServoPos);
    }
}

void updateStatusLED()
{
    switch (currentState)
    {
    case WAITING_FOR_CHAIN:
        // Fast blink while waiting
        digitalWrite(ledPin, (millis() / 500) % 2);
        break;

    case INIT_IN_PROGRESS:
        // Slow blink during init
        digitalWrite(ledPin, (millis() / 1000) % 2);
        break;

    case CHAIN_READY:
        // Solid on when ready
        digitalWrite(ledPin, HIGH);
        break;
    }
}

void waitForChain()
{
    if (isMasterDevice)
    {
        Serial.println("DEBUG: Starting device initialization via serial polling");
        // Start initialization using serial polling only
        currentState = INIT_IN_PROGRESS;
        Serial.println("DEBUG: Sending initial command: 000,init,001");
        Serial1.println("000,init,001");
    }
    else
    {
        currentState = CHAIN_READY;
    }
}

void reinitializeDevices()
{
    if (!isMasterDevice)
    {
        Serial.println("ERR:ONLY_MASTER_CAN_REINIT");
        return;
    }
    
    Serial.println("UI:Restarting device initialization...");
    
    // Reset device count and state
    totalDevices = 0;
    pendingCommand = "";
    currentState = INIT_IN_PROGRESS;
    
    // Clear any pending Serial1 data
    Serial1.flush();
    while (Serial1.available())
    {
        Serial1.read();
    }
    
    // Small delay to let slaves settle
    delay(100);
    
    // Start fresh initialization
    Serial.println("DEBUG: Starting fresh device initialization");
    Serial.println("STATE:Initializing");
    Serial.println("DEBUG: Sending initial command: 000,init,001");
    Serial1.println("000,init,001");
}

void printHelp()
{
    Serial.println("VER:" + CODE_VERSION);
    Serial.println("UI:Available Commands:");
    Serial.println("UI:  Device Control:");
    Serial.println("UI:    xxx,servo,angle - Set servo angle (60-120)");
    Serial.println("UI:    xxx,dac,value   - Set DAC value directly (0-2000)");
    Serial.println("UI:  Program Control:");
    Serial.println("UI:    xxx,program,{group_id,group_total,intensity}");
    Serial.println("UI:    000,start_program,duration_ms - Start program (1-100ms)");
    Serial.println("UI:  Where xxx is:");
    Serial.println("UI:    000 = all devices");
    Serial.println("UI:    001-" + String(totalDevices) + " = specific device");
    Serial.println("UI:  System Commands:");
    Serial.println("UI:    help     - Show this help");
    Serial.println("UI:    status   - Show system status");
    Serial.println("UI:    reinit   - Restart device initialization");
    Serial.println("UI:    emergency - Emergency shutdown (master only)");
    Serial.println("UI:  Examples:");
    Serial.println("UI:    000,program,{1,4,1400} - Set group 1 of 4, intensity 1400");
    Serial.println("UI:    000,start_program,20   - Start 20ms program pulse");
    Serial.println("UI:    001,servo,90          - Set device 1 servo to 90°");
}

void printStatus()
{
    Serial.println("VER:" + CODE_VERSION);
    Serial.println("TOTAL:" + String(totalDevices));
    Serial.println("DEVICE_ID:" + String(myDeviceId));
    
    // Print meaningful state names
    String stateName;
    switch (currentState)
    {
        case WAITING_FOR_CHAIN:
            stateName = "Waiting for Chain";
            break;
        case CHAIN_READY:
            stateName = "Ready";
            break;
        case INIT_IN_PROGRESS:
            stateName = "Initializing";
            break;
        case PROCESSING:
            stateName = "Processing";
            break;
        case READY:
            stateName = "Ready";
            break;
        default:
            stateName = "Unknown";
            break;
    }
    Serial.println("STATE:" + stateName);
    
    // Program status
    Serial.println("PROGRAM_GROUP:" + String(group_id));
    Serial.println("PROGRAM_TOTAL:" + String(group_total));
    Serial.println("PROGRAM_INTENSITY:" + String(intensity));
    Serial.println("CURRENT_GROUP:" + String(current_group));
    
    if (pendingCommand.length() > 0)
    {
        Serial.println("PENDING:" + pendingCommand);
    }

    // Show DAC status
    Serial.println("CURRENT_DAC:" + String(currentDacValue));
}

void sendCommandAndWait(String command)
{
    if (!isMasterDevice)
        return; // Only master can send commands

    // Send the command
    pendingCommand = command;
    currentState = PROCESSING;
    Serial1.println(command);
    Serial.println("CMD:" + command);

    // Wait for command to complete (return to master)
    unsigned long startTime = millis();
    while (currentState == PROCESSING)
    {
        // Process incoming data while waiting
        if (Serial1.available())
        {
            String data = Serial1.readStringUntil('\n');
            data.trim();
            if (data.length() > 0)
            {
                processCommand(data);
            }
        }

        // Timeout after 2 seconds
        if (millis() - startTime > 2000)
        {
            Serial.println("ERR:TIMEOUT");
            currentState = READY;
            pendingCommand = "";
            break;
        }
    }
}

void emergencyShutdown()
{
    if (Serial) {
        Serial.println("EMERGENCY SHUTDOWN ACTIVATED!");
    }
    
    // Turn off DAC immediately
    analogWrite(dacPin, 0);
    digitalWrite(userLedPin, LOW);
    
    // Reset control variables
    currentDacValue = 0;
    targetDacValue = 0;
    
    // Reset program variables
    intensity = 0;
    programDuration = 0;
    
    // Ensure trigger line is HIGH (inactive)
    digitalWrite(triggerOutPin, HIGH);
    
    // Send emergency shutdown command to all devices
    if (isMasterDevice)
    {
        if (Serial) {
            Serial.println("DEBUG: Broadcasting emergency shutdown to all devices");
        }
        Serial1.println("000,dac,0");
    }
    
    if (Serial) {
        Serial.println("System shutdown complete. Check hardware and restart.");
    }
}
