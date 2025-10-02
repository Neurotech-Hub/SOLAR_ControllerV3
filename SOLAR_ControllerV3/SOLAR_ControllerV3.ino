// SOLAR Controller V3 - High Speed LED Control with Trigger Chain

#include <Arduino.h>
#include <Servo.h>

// Version tracking
const String CODE_VERSION = "3.1";

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
float intensity = 0;        // Target intensity (DAC value for now)
int duration = 0;           // Duration for this group (in milliseconds)
int current_group = 1;      // Currently active group (1-based)
bool program_success = false; // Master only: tracks program execution success

// Frame control variables
int frameCount = 1;         // Number of frames to execute (default 1)
int interframeDelay = 10;   // Delay between frames in milliseconds (default 10)
int currentFrameLoop = 0;   // Current loop number in frame execution
int totalLoops = 0;         // Total loops needed for all frames
bool frameExecutionActive = false; // Flag to track if frame execution is running
bool inPulsePhase = true;   // true = pulse active, false = interframe delay active

// Per-device status caching (master only)
struct DeviceStatus {
    int group_id;
    float intensity;
    int duration;
    bool isSet;
};
const int MAX_DEVICES = 50; // Maximum supported devices
DeviceStatus deviceCache[MAX_DEVICES]; // Dynamic device cache based on totalDevices

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
bool parseProgramCommand(String value, int &group_id, int &group_total, float &intensity, int &duration);
void updateStatusLED();
void waitForChain();
void reinitializeDevices();
void printHelp();
void printStatus();
void sendCommandAndWait(String command);
void emergencyShutdown();
void triggerInterrupt();
void handleProgramExecution();
bool getGroupSettings(int group_id, float &group_intensity, int &group_duration);

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
    duration = 0;
    current_group = 1;
    program_success = false;
    
    // Initialize frame variables
    frameCount = 1;
    interframeDelay = 10;
    currentFrameLoop = 0;
    totalLoops = 0;
    frameExecutionActive = false;
    inPulsePhase = true;
    
    // Initialize device cache (master only)
    for (int i = 0; i < MAX_DEVICES; i++) {
        deviceCache[i].group_id = 0;
        deviceCache[i].intensity = 0;
        deviceCache[i].duration = 0;
        deviceCache[i].isSet = false;
    }

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
            Serial.println("\nSOLAR Controller Starting...");
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
            
            // Handle simple start command (no device ID needed)
            else if (command == "start")
            {
                Serial.println("DEBUG: Starting frame-based program execution");
                Serial.println("DEBUG: Frame count: " + String(frameCount) + ", Interframe delay: " + String(interframeDelay) + "ms");
                
                // Reset program success flag and frame execution state
                program_success = false;
                frameExecutionActive = true;
                currentFrameLoop = 1;
                inPulsePhase = true;
                
                // Calculate total loops needed
                totalLoops = frameCount * group_total;
                Serial.println("DEBUG: Total loops: " + String(totalLoops));
                
                // Reset current_group to 1 at start
                current_group = 1;
                
                // Get group-specific settings for current group
                float groupIntensity;
                int groupDuration;
                if (getGroupSettings(current_group, groupIntensity, groupDuration)) {
                    // Start the program timing with the duration from the current group
                    programStartTime = millis();
                    programDuration = groupDuration;
                    
                    // Log the first frame start with correct group settings
                    int currentFrame = ((currentFrameLoop - 1) / group_total) + 1;
                    Serial.println("FRAME_" + String(currentFrame) + ": G_ID=" + String(current_group) + 
                                  ", I=" + String(groupIntensity) + ", D=" + String(groupDuration));
                    
                    // Immediately drive TRIGGER_OUT LOW to start the pulse
                    digitalWrite(triggerOutPin, LOW);
                    
                    // If master has a group assignment for this group, activate it immediately
                    if (current_group == group_id && group_id > 0) {
                        analogWrite(dacPin, (int)groupIntensity);
                        digitalWrite(userLedPin, HIGH);
                    }
                    
                    Serial.println("DEBUG: Program started, TRIGGER_OUT driven LOW");
                } else {
                    Serial.println("ERROR: No devices programmed for group " + String(current_group));
                    frameExecutionActive = false;
                }
                return;
            }
            
            // Handle simple frame command (format: frame,count,interframe_delay)
            else if (command.startsWith("frame,"))
            {
                String frameParams = command.substring(6); // Remove "frame," prefix
                int firstComma = frameParams.indexOf(',');
                
                if (firstComma == -1)
                {
                    Serial.println("ERR:INVALID_FRAME_FORMAT");
                    Serial.println("UI:Frame format: frame,count,interframe_delay");
                    return;
                }
                
                int count = frameParams.substring(0, firstComma).toInt();
                int delay = frameParams.substring(firstComma + 1).toInt();
                
                if (count < 1 || count > 1000)
                {
                    Serial.println("ERR:FRAME_COUNT_RANGE:" + String(count));
                    Serial.println("UI:Frame count must be 1-1000");
                    return;
                }
                
                if (delay < 0 || delay > 100)
                {
                    Serial.println("ERR:INTERFRAME_DELAY_RANGE:" + String(delay));
                    Serial.println("UI:Interframe delay must be 0-100 milliseconds");
                    return;
                }
                
                // Update frame settings
                frameCount = count;
                interframeDelay = delay;
                
                Serial.println("DEBUG: Frame settings updated - Count: " + String(frameCount) + 
                              ", Interframe delay: " + String(interframeDelay) + "ms");
                Serial.println("FRAME_SET:" + String(frameCount) + "," + String(interframeDelay));
                return;
            }


            // Parse and validate regular command
            Command cmd;
            if (!parseCommand(command, cmd))
            {
                Serial.println("ERR:INVALID_FORMAT");
                Serial.println("UI:Use format: deviceId,command,value");
                Serial.println("UI:Example: 001,servo,90 or 001,program,{1,4,1400,20} (duration: 1-100ms)");
                return;
            }

            // Validate command type (frame and start are handled above as simple commands)
            if (cmd.command != "servo" && cmd.command != "dac" && 
                cmd.command != "init" && cmd.command != "program")
            {
                Serial.println("ERR:INVALID_COMMAND:" + cmd.command);
                Serial.println("UI:Valid commands: servo, dac, program");
                Serial.println("UI:Simple commands: start, frame,count,delay");
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

            // Cache program commands before sending to chain (master only)
            if (cmd.command == "program" && isMasterDevice) {
                int cache_group_id, cache_group_total, cache_duration;
                float cache_intensity;
                
                if (parseProgramCommand(cmd.value, cache_group_id, cache_group_total, cache_intensity, cache_duration)) {
                    if (cmd.deviceId >= 1 && cmd.deviceId <= totalDevices && cmd.deviceId <= MAX_DEVICES) {
                        // Specific device command - cache for that device
                        deviceCache[cmd.deviceId - 1].group_id = cache_group_id;
                        deviceCache[cmd.deviceId - 1].intensity = cache_intensity;
                        deviceCache[cmd.deviceId - 1].duration = cache_duration;
                        deviceCache[cmd.deviceId - 1].isSet = true;
                        Serial.println("DEBUG: Cached program for device " + String(cmd.deviceId));
                    } else if (cmd.deviceId == 0) {
                        // Broadcast command - cache for all devices in chain
                        for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                            deviceCache[i].group_id = cache_group_id;
                            deviceCache[i].intensity = cache_intensity;
                            deviceCache[i].duration = cache_duration;
                            deviceCache[i].isSet = true;
                        }
                        Serial.println("DEBUG: Cached program for all " + String(totalDevices) + " devices");
                    }
                }
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
    if (!frameExecutionActive) return;
    
    // Check if current phase duration has elapsed
    if (millis() - programStartTime >= programDuration) {
        
        if (inPulsePhase) {
            // Pulse phase complete - end the pulse and start interframe delay
            digitalWrite(triggerOutPin, HIGH);
            Serial.println("DEBUG: Pulse complete, starting interframe delay of " + String(interframeDelay) + "ms");
            
            // Switch to interframe delay phase
            inPulsePhase = false;
            programStartTime = millis();
            programDuration = interframeDelay;
            
        } else {
            // Interframe delay complete - check if we need to continue
            currentFrameLoop++;
            
            // Check if all loops are complete
            if (currentFrameLoop > totalLoops) {
                // All frames complete
                frameExecutionActive = false;
                programDuration = 0;
                Serial.println("DEBUG: All frames complete");
                Serial.println("PROGRAM_ACK:" + String(program_success ? "true" : "false"));
                return;
            }
            
            // Get group-specific settings for current group
            float groupIntensity;
            int groupDuration;
            if (getGroupSettings(current_group, groupIntensity, groupDuration)) {
                // Calculate current frame number for logging
                int currentFrame = ((currentFrameLoop - 1) / group_total) + 1;
                
                // Log the frame start with correct group settings
                Serial.println("FRAME_" + String(currentFrame) + ": G_ID=" + String(current_group) + 
                              ", I=" + String(groupIntensity) + ", D=" + String(groupDuration));
                
                // Start the next pulse
                inPulsePhase = true;
                programStartTime = millis();
                programDuration = groupDuration;
                
                // Drive TRIGGER_OUT LOW to start the next pulse
                digitalWrite(triggerOutPin, LOW);
                
                // If master has a group assignment for current group, activate it
                if (current_group == group_id && group_id > 0) {
                    analogWrite(dacPin, (int)groupIntensity);
                    digitalWrite(userLedPin, HIGH);
                }
                
                Serial.println("DEBUG: Next pulse started, TRIGGER_OUT driven LOW");
            } else {
                Serial.println("ERROR: No devices programmed for group " + String(current_group));
                frameExecutionActive = false;
                programDuration = 0;
            }
        }
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
            int new_group_id, new_group_total, new_duration;
            float new_intensity;
            
            if (parseProgramCommand(cmd.value, new_group_id, new_group_total, new_intensity, new_duration))
            {
                group_id = new_group_id;
                group_total = new_group_total;
                intensity = new_intensity;
                duration = new_duration;
                current_group = 1; // Reset to group 1 when new program is set
                
                // Cache device settings (master only caches for all devices)
                if (isMasterDevice) {
                    if (cmd.deviceId >= 1 && cmd.deviceId <= totalDevices && cmd.deviceId <= MAX_DEVICES) {
                        // Specific device command - cache for that device
                        deviceCache[cmd.deviceId - 1].group_id = new_group_id;
                        deviceCache[cmd.deviceId - 1].intensity = new_intensity;
                        deviceCache[cmd.deviceId - 1].duration = new_duration;
                        deviceCache[cmd.deviceId - 1].isSet = true;
                    } else if (cmd.deviceId == 0) {
                        // Broadcast command - cache for all devices in chain
                        for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                            deviceCache[i].group_id = new_group_id;
                            deviceCache[i].intensity = new_intensity;
                            deviceCache[i].duration = new_duration;
                            deviceCache[i].isSet = true;
                        }
                    }
                }
                
                if (Serial)
                {
                    Serial.println("DEBUG: Program set - Group:" + String(group_id) + 
                                   " Total:" + String(group_total) + 
                                   " Intensity:" + String(intensity) +
                                   " Duration:" + String(duration) + "ms");
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

bool parseProgramCommand(String value, int &group_id, int &group_total, float &intensity, int &duration)
{
    // Remove curly brackets
    if (value.length() < 3 || value.charAt(0) != '{' || value.charAt(value.length() - 1) != '}') {
        return false;
    }
    
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
    intensity = value.substring(secondComma + 1, thirdComma).toInt();
    duration = value.substring(thirdComma + 1).toInt();
    
    // Validate parsed values
    if (group_total < 1 || group_id < 0 || group_id > group_total || 
        intensity < 0 || intensity > 4095 || duration < 1 || duration > 100) {
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
    Serial.println("UI:    xxx,program,{group_id,group_total,intensity,duration}");
    Serial.println("UI:    frame,count,interframe_delay - Set frame parameters");
    Serial.println("UI:    start - Start frame-based program execution");
    Serial.println("UI:  Where xxx is:");
    Serial.println("UI:    000 = all devices");
    Serial.println("UI:    001-" + String(totalDevices) + " = specific device");
    Serial.println("UI:  System Commands:");
    Serial.println("UI:    help     - Show this help");
    Serial.println("UI:    status   - Show system status");
    Serial.println("UI:    reinit   - Restart device initialization");
    Serial.println("UI:    emergency - Emergency shutdown (master only)");
    Serial.println("UI:  Examples:");
    Serial.println("UI:    001,program,{1,2,1400,20} - Set device 1 group 1 of 2, intensity 1400, duration 20ms");
    Serial.println("UI:    frame,5,50              - Set 5 frames with 50ms interframe delay");
    Serial.println("UI:    start                   - Start program execution");
    Serial.println("UI:    001,servo,90            - Set device 1 servo to 90°");
}

void printStatus()
{
    Serial.println("VER:" + CODE_VERSION);
    Serial.println("TOTAL:" + String(totalDevices));
    
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
    
    // Total number of groups and frame settings
    Serial.println("GROUP_TOTAL:" + String(group_total));
    Serial.println("FRAME_COUNT:" + String(frameCount));
    Serial.println("INTERFRAME_DELAY:" + String(interframeDelay));
    
    // Per-device cached program settings (master only)
    if (isMasterDevice) {
        Serial.println("PROGRAMS_CACHE:");
        for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
            if (deviceCache[i].isSet) {
                Serial.println("DEV:" + String(i + 1) + ", G_ID:" + String(deviceCache[i].group_id) + 
                              ", I:" + String(deviceCache[i].intensity) + 
                              ", D:" + String(deviceCache[i].duration));
            } else {
                Serial.println("DEV:" + String(i + 1) + ", NOT_PROGRAMMED");
            }
        }
    }
    
    // Current execution state
    Serial.println("CURRENT_GROUP:" + String(current_group));
    
    if (pendingCommand.length() > 0)
    {
        Serial.println("PENDING:" + pendingCommand);
    }

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

// Function to get intensity and duration for a specific group from device cache
bool getGroupSettings(int group_id, float &group_intensity, int &group_duration)
{
    // Look for any device with the specified group_id in the cache
    for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
        if (deviceCache[i].isSet && deviceCache[i].group_id == group_id) {
            group_intensity = deviceCache[i].intensity;
            group_duration = deviceCache[i].duration;
            return true;
        }
    }
    
    // If no device found with this group_id, return default values
    group_intensity = 0;
    group_duration = 10;
    return false;
}
