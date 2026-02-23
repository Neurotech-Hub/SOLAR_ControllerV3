// SOLAR Controller V3 - High Speed LED Control with Trigger Chain and Current Closeloop

#include <Arduino.h>
#include <Servo.h>
#include <INA226.h>
#include <Wire.h>

// Version tracking
const String CODE_VERSION = "3.5.3";

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

// Auto-Calibration Parameters (Phase 8)
const int dacCalibrationStart = 1300;   // Starting DAC value for Frame_0 calibration
const int calibrationDuration = 100;   // Calibration duration per group (ms)

// INA226 current sensor (I2C address 0x4A)
INA226 ina226(0x4A);

// INA226 Configuration
const float shuntResistance = 0.04195;  // 42mΩ shunt
const float maxCurrent_mA = 1550.0;     // 1.5A max current
bool ina226_available = false;          // INA226 initialization flag

// Variable Configuration Parameters
const int conversion_miss_count_limit = 3;   // Maximum consecutive conversion-not-ready events before emergency shutdown
const int overcurrent_consecutive_count_limit = 3;   // Maximum consecutive overcurrent readings before emergency shutdown

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
float target_current_mA = 0; // Target current in mA
int duration = 0;           // Duration for this group (in milliseconds)
int current_group = 1;      // Currently active group (1-based)
bool program_success = false; // Master only: tracks program execution success

// Current control variables
float safe_current_mA = 0;      // 99% of target (safety margin)
int current_dac_value = 0;      // Current DAC value during closeloop
float measured_current_mA = 0;  // Last measured current
bool closeloop_active = false;  // Controls when closeloop runs (set by trigger interrupt)
bool dac_output_active = false; // true iff DAC output > 0 has been applied
int last_current_state = 0;     // Tracks current state for userLedPin edge detection (0 or 1)
int overcurrent_consecutive_count = 0;  // Tracks consecutive overcurrent readings (0-2)
int conversion_miss_count = 0;          // Tracks consecutive INA226 conversion-not-ready events

// Frame control variables
int frameCount = 1;         // Number of frames to execute (default 1)
int interframeDelay = 50;   // Delay between frames in milliseconds (default 50)
int currentFrameLoop = 0;   // Current loop number in frame execution
int totalLoops = 0;         // Total loops needed for all frames
bool frameExecutionActive = false; // Flag to track if frame execution is running
bool inPulsePhase = true;   // true = pulse active, false = interframe delay active
bool emergencyShutdownActive = false; // Sticky flag: prevents trigger re-activation after emergency

// Auto-Calibration variables (Phase 8)
bool inCalibrationPhase = false;   // true during Frame_0, false during Frame_1+

// Healthcheck variables (master only)
bool healthcheck_complete = false;      // true when healthcheck response received from chain
int healthcheck_failed_device = 0;      // Device ID that failed healthcheck (0 = all passed)

// Per-device status caching (master only)
struct DeviceStatus {
    int group_id;
    float current;      // Target current in mA
    int exposure;       // Exposure duration in ms
    bool isSet;         // true if program command received
};
const int MAX_DEVICES = 50; // Maximum supported devices
DeviceStatus deviceCache[MAX_DEVICES]; // Dynamic device cache based on totalDevices

// DAC control variables

int last_adjusted_dac = 0;      // Stores last non-zero DAC value before pulse end (0 = not yet set)

// Timing variables
unsigned long programStartTime = 0;
unsigned long programDuration = 0;

// Blind time watchdog: detects if DAC is on without current feedback
const unsigned long MAX_BLIND_TIME_MS = 5;  // Max ms DAC can run without INA226 feedback
volatile unsigned long dac_blind_start_ms = 0;  // Timestamp when DAC applied in ISR (0 = not blind)

// Volatile variables for interrupt handling
volatile bool triggerStateChanged = false;
volatile bool lastTriggerState = HIGH;

// Function declarations
void updateServo();
void processCommand(String data);
bool parseCommand(String data, Command &cmd);
bool parseProgramCommand(String value, int &group_id, int &group_total, float &current, int &exposure);
void updateStatusLED();
void waitForChain();
void reinitializeDevices();
void printHelp();
void printStatus();
void sendCommandAndWait(String command);
void emergencyShutdown();
void triggerInterrupt();
void handleProgramExecution();
bool getGroupSettings(int group_id, int &group_exposure);
bool validateGroupExposures();
bool initializeINA226();
bool calculateCalibration();
bool probeINA226();
bool verifyINA226();
int calculateDacReduction(float currentReading, float targetCurrent);
int calculateDacIncrement(float currentReading, float targetCurrent);
void handleCurrentControl();

// INA226 Initialization Functions
bool initializeINA226() {
    Serial.println("DEBUG: Initializing INA226...");
    if (!ina226.begin()) {
        Serial.println("ERROR: INA226 not found at address 0x4A!");
        return false;
    }
    
    // Set conversion times: 204µs (0x01)
    ina226.setAverage(0x01);  // 4 sample averaging
    ina226.setBusVoltageConversionTime(0x01);  // 204µs
    ina226.setShuntVoltageConversionTime(0x01); // 204µs
    ina226.setMode(0x07);  // Continuous shunt and bus voltage
    
    if (!calculateCalibration()) {
        Serial.println("ERROR: INA226 calibration failed!");
        return false;
    }
    
    Serial.println("DEBUG: INA226 initialized successfully");
    Serial.println("DEBUG: Current LSB: " + String(ina226.getCurrentLSB_mA(), 6) + " mA");
    return true;
}

bool calculateCalibration() {
    int result = ina226.setMaxCurrentShunt(maxCurrent_mA / 1000.0, shuntResistance);
    return (result == 0); // INA226_ERR_NONE = 0
}

// INA226 Live Probe - verifies sensor is physically responding on I2C
bool probeINA226() {
    if (!ina226.waitConversionReady(5)) {  // 5ms timeout
        return false;  // Chip not responding on I2C
    }
    float testRead = ina226.getCurrent_mA();
    if (isnan(testRead)) {
        return false;  // Chip responding but returning garbage data
    }
    return true;  // Chip is alive and returning valid data
}

// INA226 Verify - probe first, attempt re-init if needed, return final status
bool verifyINA226() {
    // Step 1: If already marked unavailable, try re-initialization
    if (!ina226_available) {
        Serial.println("DEBUG: INA226 marked unavailable, attempting re-initialization...");
        ina226_available = initializeINA226();
        if (!ina226_available) {
            Serial.println("ERROR: INA226 re-initialization failed");
            return false;
        }
        Serial.println("DEBUG: INA226 re-initialized successfully");
    }
    
    // Step 2: Live probe - verify chip is actually responding right now
    if (!probeINA226()) {
        Serial.println("WARNING: INA226 not responding to live probe, attempting re-initialization...");
        ina226_available = initializeINA226();
        if (!ina226_available) {
            Serial.println("ERROR: INA226 re-initialization failed after probe failure");
            return false;
        }
        // Verify probe works after re-init
        if (!probeINA226()) {
            Serial.println("ERROR: INA226 still not responding after re-initialization");
            ina226_available = false;
            return false;
        }
        Serial.println("DEBUG: INA226 recovered after re-initialization");
    }
    
    return true;
}

int calculateDacReduction(float currentReading, float targetCurrent) {
    if (targetCurrent <= 0) return 0;  // Avoid division by zero
    
    float percentageOver = ((currentReading - targetCurrent) / targetCurrent) * 100.0;
    
    // If current > target_current*n% then reduce by n/2 points
    if (percentageOver >= 50.0) {
        return 50;  // 50% or more: reduce by 50 points
    } else if (percentageOver >= 40.0) {
        return 25;   // 40% or more: reduce by 25 points
    } else if (percentageOver >= 30.0) {
        return 20;   // 30% or more: reduce by 20 points
    } else if (percentageOver >= 20.0) {
        return 15;   // 20% or more: reduce by 15 points
    } else if (percentageOver >= 14.0) {
        return 10;   // 14% or more: reduce by 10 points
    } else if (percentageOver >= 12.0) {
        return 7;    // 12% or more: reduce by 7 points
    } else if (percentageOver >= 10.0) {
        return 6;    // 10% or more: reduce by 6 points
    } else if (percentageOver >= 8.0) {
        return 5;    // 8% or more: reduce by 5 points
    } else if (percentageOver >= 6.0) {
        return 4;    // 6% or more: reduce by 4 points
    } else if (percentageOver >= 4.0) {
        return 3;    // 4% or more: reduce by 3 points
    } else if (percentageOver >= 2.5) {
        return 2;    // 2.5% or more: reduce by 2 points
    } else {
        return 1;    // Less than 2.5%: reduce by 1 point
    }
}

int calculateDacIncrement(float currentReading, float targetCurrent) {
    if (targetCurrent <= 0) return 0;  // Avoid division by zero
    
    // Calculate percentage deficit (how far UNDER target we are)
    float percentageUnder = ((targetCurrent - currentReading) / targetCurrent) * 100.0;
    
    // If current is far below target, increase DAC proportionally for fast convergence
    if (percentageUnder >= 80.0) {
        return 35;  // 80% or more under: increase by 35 points
    } else if (percentageUnder >= 60.0) {
        return 20;   // 60% or more under: increase by 20 points
    } else if (percentageUnder >= 50.0) {
        return 15;   // 50% or more under: increase by 15 points
    } else if (percentageUnder >= 40.0) {
        return 10;   // 40% or more under: increase by 10 points
    } else if (percentageUnder >= 35.0) {
        return 8;   // 35% or more under: increase by 8 points
    } else if (percentageUnder >= 30.0) {
        return 7;    // 30% or more under: increase by 7 points
    } else if (percentageUnder >= 25.0) {
        return 6;    // 25% or more under: increase by 6 points
    } else if (percentageUnder >= 20.0) {
        return 5;    // 20% or more under: increase by 5 points
    } else if (percentageUnder >= 15.0) {
        return 4;    // 15% or more under: increase by 4 points
    } else if (percentageUnder >= 10.0) {
        return 3;    // 10% or more under: increase by 3 points
    } else if (percentageUnder >= 5.0) {
        return 2;    // 5% or more under: increase by 2 points
    } else {
        return 1;    // Less than 4% under: increase by 1 point (fine control)
    }
}

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

    last_adjusted_dac = 0;

    // Initialize program variables
    group_id = 0;
    group_total = 0;
    target_current_mA = 0;
    safe_current_mA = 0;
    duration = 0;
    current_group = 1;
    program_success = false;
    current_dac_value = 0;
    measured_current_mA = 0;
    closeloop_active = false;
    dac_output_active = false;
    dac_blind_start_ms = 0;
    last_current_state = 0;
    overcurrent_consecutive_count = 0;
    
    // Initialize frame variables
    frameCount = 1;
    interframeDelay = 10;
    currentFrameLoop = 0;
    totalLoops = 0;
    frameExecutionActive = false;
    inPulsePhase = true;
    
    // Initialize calibration and emergency variables
    inCalibrationPhase = false;
    emergencyShutdownActive = false;
    
    // Initialize device cache (master only)
    for (int i = 0; i < MAX_DEVICES; i++) {
        deviceCache[i].group_id = 0;
        deviceCache[i].current = 0;
        deviceCache[i].exposure = 0;
        deviceCache[i].isSet = false;
    }

    // Initialize Serial interfaces
    Serial.begin(115200); // USB
    Serial1.begin(115200);  // Device chain
    
    // Initialize I2C
    Wire.begin();
    
    // Small delay for I2C to stabilize
    delay(100);
    
    // Initialize INA226 (critical - halt if fails)
    ina226_available = initializeINA226();
    if (!ina226_available) {
        Serial.println("CRITICAL ERROR: INA226 initialization failed!");
        Serial.println("System halted. Check I2C connections at address 0x4A.");
    }

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
    
    // Handle current control closeloop (all devices)
    handleCurrentControl();

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
                Serial.println("DEBUG: Starting program execution with Frame_0 auto-calibration");
                Serial.println("DEBUG: Frame count: " + String(frameCount) + ", Interframe delay: " + String(interframeDelay) + "ms");
                
                // Validate that all devices in each group have the same exposure time
                if (!validateGroupExposures()) {
                    Serial.println("ERR:EXPOSURE_MISMATCH");
                    Serial.println("UI:All devices in a group must have the same exposure time");
                    Serial.println("UI:Check status command to see programmed values");
                    return;
                }
                
                // CHECKPOINT: Verify master's own INA226 is available and responding
                if (!verifyINA226()) {
                    Serial.println("ERR:INA226_UNAVAILABLE");
                    Serial.println("UI:Master INA226 current sensor not responding");
                    Serial.println("UI:Check I2C connections at address 0x4A");
                    return;
                }
                Serial.println("DEBUG: Master INA226 checkpoint passed");
                
                // HEALTHCHECK: Verify all slave devices' INA226 before starting
                if (totalDevices > 1) {
                    healthcheck_complete = false;
                    healthcheck_failed_device = 0;
                    Serial.println("DEBUG: Sending healthcheck to all devices...");
                    Serial1.println("000,healthcheck,0");
                    
                    // Wait for healthcheck response (timeout 2 seconds)
                    unsigned long hcStart = millis();
                    while (!healthcheck_complete && (millis() - hcStart < 2000)) {
                        if (Serial1.available()) {
                            String hcData = Serial1.readStringUntil('\n');
                            hcData.trim();
                            if (hcData.length() > 0) {
                                processCommand(hcData);
                            }
                        }
                    }
                    
                    if (!healthcheck_complete) {
                        Serial.println("ERR:HEALTHCHECK_TIMEOUT");
                        Serial.println("UI:Healthcheck timed out - check device chain");
                        return;
                    }
                    
                    if (healthcheck_failed_device > 0) {
                        Serial.println("ERR:INA226_UNAVAILABLE");
                        Serial.println("UI:Device " + String(healthcheck_failed_device) + " failed INA226 healthcheck");
                        Serial.println("UI:Aborting start - all devices must pass INA226 check");
                        return;
                    }
                    Serial.println("DEBUG: All devices passed healthcheck");
                } else {
                    Serial.println("DEBUG: Single device - skipping chain healthcheck");
                }
                
                // CRITICAL: Reset ALL control values for clean start
                program_success = false;
                emergencyShutdownActive = false;  // Clear emergency lockout for fresh start
                frameExecutionActive = true;
                inCalibrationPhase = true;  // Start with Frame_0
                currentFrameLoop = 0;  // Frame_0 tracking
                inPulsePhase = true;

                // Reset ALL DAC and current control variables
                last_adjusted_dac = 0;  // Reset for fresh calibration
                current_dac_value = dacCalibrationStart;  // Reset to calibration start value (1300)
                measured_current_mA = 0;  // Reset measured current
                closeloop_active = false;  // Ensure closeloop starts inactive
                dac_output_active = false;  // Ensure DAC output tracking is reset
                dac_blind_start_ms = 0;  // Clear blind watchdog
                overcurrent_consecutive_count = 0;  // Reset overcurrent counter
                last_current_state = 0;  // Reset LED state tracking

                // Ensure DAC is physically OFF before starting
                analogWrite(dacPin, 0);
                digitalWrite(userLedPin, LOW);
                
                // Reset current_group to 1 at start
                current_group = 1;
                
                // Get target current for first group
                float groupCurrent = 0;
                for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                    if (deviceCache[i].isSet && deviceCache[i].group_id == current_group) {
                        groupCurrent = deviceCache[i].current;
                        break;
                    }
                }
                
                // Start Frame_0 calibration
                Serial.println("FRAME_0: Calibration Phase Starting...");
                Serial.println("FRAME_0: G_ID=" + String(current_group) + ", I_TARGET=" + String(groupCurrent) + "mA");
                
                // Start timing for Frame_0 (calibration duration = 1 second per group)
                programStartTime = millis();
                programDuration = calibrationDuration;

                // CRITICAL: Broadcast calibration mode to ALL slave devices
                Serial1.println("000,calibration,start");
                delay(10);                
                // Immediately drive TRIGGER_OUT LOW to start calibration
                digitalWrite(triggerOutPin, LOW);
                
                // Master activates closeloop with calibration starting DAC
                if (current_group == group_id && group_id > 0) {
                    current_dac_value = dacCalibrationStart;
                    closeloop_active = true;  // Enable closeloop for calibration
                }
                
                Serial.println("DEBUG: Frame_0 started, TRIGGER_OUT driven LOW");
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
                
                if (count < 1 || count > 5000)
                {
                    Serial.println("ERR:FRAME_COUNT_RANGE:" + String(count));
                    Serial.println("UI:Frame count must be 1-5000");
                    return;
                }
                
                if (delay < 5)
                {
                    Serial.println("ERR:INTERFRAME_DELAY_RANGE:" + String(delay));
                    Serial.println("UI:Interframe delay must be at least 5 milliseconds");
                    return;
                }
                
                // Update frame settings
                frameCount = count;
                interframeDelay = delay;
                
                Serial.println("FRAME_SET:" + String(frameCount) + "," + String(interframeDelay));
                return;
            }


            // Parse and validate regular command
            Command cmd;
            if (!parseCommand(command, cmd))
            {
                Serial.println("ERR:INVALID_FORMAT");
                Serial.println("UI:Use format: deviceId,command,value");
                Serial.println("UI:Example: 001,servo,90 or 001,program,{1,2,1806,1300,20}");
                return;
            }

            // Validate command type (frame and start are handled above as simple commands)
            if (cmd.command != "servo" && 
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

            // Cache program commands before sending to chain (master only)
            if (cmd.command == "program" && isMasterDevice) {
                int cache_group_id, cache_group_total, cache_exposure;
                float cache_current;
                
                if (parseProgramCommand(cmd.value, cache_group_id, cache_group_total, cache_current, cache_exposure)) {
                    if (cmd.deviceId >= 1 && cmd.deviceId <= totalDevices && cmd.deviceId <= MAX_DEVICES) {
                        // Specific device command - cache for that device
                        deviceCache[cmd.deviceId - 1].group_id = cache_group_id;
                        deviceCache[cmd.deviceId - 1].current = cache_current;
                        deviceCache[cmd.deviceId - 1].exposure = cache_exposure;
                        deviceCache[cmd.deviceId - 1].isSet = true;
                        Serial.println("DEBUG: Cached program for device " + String(cmd.deviceId));
                    } else if (cmd.deviceId == 0) {
                        // Broadcast command - cache for all devices in chain
                        for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                            deviceCache[i].group_id = cache_group_id;
                            deviceCache[i].current = cache_current;
                            deviceCache[i].exposure = cache_exposure;
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
                // Serial.println("DEBUG: Received from chain: " + data);
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
    
    // Only slave devices propagate trigger signals
    // Master device NEVER propagates in interrupt handler (per SHIFT.md)
    if (!isMasterDevice || triggerState != lastTriggerState) {
        digitalWrite(triggerOutPin, triggerState);
    }
    
    if (triggerState == LOW && lastTriggerState == HIGH) {
        // HIGH->LOW transition: Enable closeloop control (blocked during emergency)
        if (!emergencyShutdownActive && current_group == group_id && group_id > 0) {
            // Initialize DAC based on whether we have calibrated value
            if (last_adjusted_dac > 0 && last_adjusted_dac != dacMax) {
                // Have calibrated value - use it (Frame_1+)
                current_dac_value = last_adjusted_dac;
                // IMMEDIATE DAC OUTPUT: Apply validated DAC value in ISR for near-zero latency
                // Safe because last_adjusted_dac was verified by closeloop in a prior frame
                analogWrite(dacPin, current_dac_value);
                dac_output_active = true;
                dac_blind_start_ms = millis();  // Start blind time watchdog
            } else {
                // No calibrated value yet - start from calibration DAC (Frame_0)
                // Do NOT apply DAC in ISR - wait for handleCurrentControl() with INA226 feedback
                current_dac_value = dacCalibrationStart;
                dac_blind_start_ms = 0;  // No blind time (DAC not applied yet)
            }
            closeloop_active = true;  // Activate closeloop
            // Serial.println("DEBUG: Trigger State: LOW");
        }
    } else if (triggerState == HIGH && lastTriggerState == LOW) {
        // LOW->HIGH transition: Record DAC value (calibration or adjustment)
        if (current_dac_value > 0 && closeloop_active) {
            // Store DAC for next frame (works for both Frame_0 and Frame_1+)
            last_adjusted_dac = current_dac_value;
        }
        // IMMEDIATE DAC OFF: near-zero latency LED shutoff in ISR
        analogWrite(dacPin, 0);
        dac_output_active = false;
        closeloop_active = false;
        dac_blind_start_ms = 0;
        
        // Only slave devices rotate to next group based on trigger signal
        // Master device handles current_group updates in handleProgramExecution()
        if (!isMasterDevice) {
            current_group++;
            if (current_group > group_total) {
                current_group = 1;
            }
        }
    }
    
    lastTriggerState = triggerState;
}

void handleCurrentControl() {
    // CRITICAL: Only run closeloop when closeloop_active is true
    if (!closeloop_active) {
        // Closeloop is disabled - ensure DAC output is off once
        if (dac_output_active) {
            // Serial.println("DEBUG: Last adjusted DAC=" + String(current_dac_value));
            last_adjusted_dac = current_dac_value;
            analogWrite(dacPin, 0);
            dac_output_active = false;
            digitalWrite(userLedPin, LOW);
            last_current_state = 0;
        }
        overcurrent_consecutive_count = 0;  // Reset counter when closeloop inactive
        conversion_miss_count = 0;          // Reset conversion miss counter
        return;
    }
    
    // BLIND TIME WATCHDOG: Emergency shutdown if DAC was applied in ISR
    // but INA226 feedback hasn't arrived within MAX_BLIND_TIME_MS
    if (dac_blind_start_ms > 0) {
        unsigned long blind_elapsed = millis() - dac_blind_start_ms;
        if (blind_elapsed > MAX_BLIND_TIME_MS) {
            Serial.println("EMERGENCY: DAC blind for " + String(blind_elapsed) + 
                          "ms without INA226 feedback on device " + String(myDeviceId));
            emergencyShutdown();
            
            if (!isMasterDevice) {
                Serial1.println("000,blind_timeout," + String(myDeviceId));
            }
            return;
        }
    }
    
    if (!ina226_available) {
        // INA226 not available - cannot control DAC safely
        Serial.println("ERROR: INA226 not available - closeloop disabled");
        closeloop_active = false;
        analogWrite(dacPin, 0);
        current_dac_value = 0;
        dac_blind_start_ms = 0;
        digitalWrite(userLedPin, LOW);
        last_current_state = 0;
        return;
    }
    
    if (target_current_mA <= 0) {
        Serial.println("DEBUG: target_current_mA is 0! Shutting down DAC");
        // No target current - cannot regulate
        closeloop_active = false;
        analogWrite(dacPin, 0);
        current_dac_value = 0;
        dac_blind_start_ms = 0;
        digitalWrite(userLedPin, LOW);
        last_current_state = 0;
        return;
    }
    
    // Serial.println("DEBUG: Current DAC value: " + String(current_dac_value));
    // Wait for conversion ready with 2ms timeout to get fresh current reading
    if (ina226.waitConversionReady(2)) {
        conversion_miss_count = 0;  // Reset on successful conversion
        dac_blind_start_ms = 0;     // INA226 feedback received - clear blind watchdog
        // Read current feedback from INA226
        measured_current_mA = ina226.getCurrent_mA();
        
        // SAFETY: Detect garbage readings (NaN or negative) - indicates INA226 communication failure
        if (isnan(measured_current_mA) || measured_current_mA < -10.0) {
            Serial.println("EMERGENCY: INA226 garbage reading detected: " + String(measured_current_mA) + "mA");
            ina226_available = false;  // Mark sensor as failed
            
            emergencyShutdown();
            
            // Report INA226 failure through chain so master can abort all frames
            if (!isMasterDevice) {
                Serial1.println("000,ina_fail," + String(myDeviceId));
                if (Serial) {
                    Serial.println("DEBUG: Sent ina_fail report for device " + String(myDeviceId));
                }
            }
            return;
        }
        
        // Serial.print("DEBUG: I: "); Serial.print(measured_current_mA); Serial.print("(mA) | DAC: "); Serial.println(current_dac_value);
        
        // CRITICAL: Emergency shutdown check - current > maxCurrent for TWO CONSECUTIVE readings
        if (measured_current_mA > (maxCurrent_mA)) {
            overcurrent_consecutive_count++;
            
            if (overcurrent_consecutive_count == 1) {
                // First over-threshold reading - log warning
                Serial.println("WARNING: Overcurrent detected (reading #1): " + 
                              String(measured_current_mA) + "mA on device " + String(myDeviceId));
                Serial.println("WARNING: Threshold: " + String(maxCurrent_mA) + 
                              "mA - Monitoring for consecutive reading");
            } else if (overcurrent_consecutive_count >= overcurrent_consecutive_count_limit) {
                // Consecutive over-threshold confirmed - FULL EMERGENCY SHUTDOWN
                Serial.println("EMERGENCY: Current exceeded " + String(maxCurrent_mA) + 
                              " mA on device " + String(myDeviceId));
                
                emergencyShutdown();
                
                // Slave: Report overcurrent to master through chain
                if (!isMasterDevice) {
                    Serial1.println("000,overcurrent," + String(myDeviceId));
                    if (Serial) {
                        Serial.println("DEBUG: Sent overcurrent report for device " + String(myDeviceId));
                    }
                }
                return;
            }
            
            // During first over-threshold: Prevent DAC increases (safety measure)
            // Closeloop will continue but can only decrease/hold DAC
        } else {
            // Current is below threshold - reset counter
            if (overcurrent_consecutive_count > 0) {
                Serial.println("INFO: Current back to normal: " + String(measured_current_mA) + 
                              "mA (was over " + String(overcurrent_consecutive_count) + " time(s))");
                overcurrent_consecutive_count = 0;
            }
        }
        
        // Closeloop control: Adjust DAC dynamically based on current feedback
        if (measured_current_mA < safe_current_mA) {
            // Under target - increment DAC (but not if we have overcurrent warning)
            if (overcurrent_consecutive_count == 0) {
                // Use fast increment during calibration (Frame_0), slow increment during normal operation (Frame_1+)
                if (inCalibrationPhase) {
                    // Frame_0 calibration: Use smart increment for fast convergence
                    int increment = calculateDacIncrement(measured_current_mA, target_current_mA);
                    current_dac_value += increment;
                } else {
                    // Frame_1+ normal operation: Increment by 1 for stable fine control
                    current_dac_value++;
                }
            }
            // else: hold DAC during overcurrent warning for safety
        } else if (measured_current_mA >= target_current_mA) {
            // Over target - reduce DAC proportionally
            int reduction = calculateDacReduction(measured_current_mA, target_current_mA);
            current_dac_value -= reduction;
        }
        // If between safe_current_mA and target_current_mA, hold steady
        
        // Constrain and apply new DAC value
        current_dac_value = constrain(current_dac_value, dacCalibrationStart, dacMax);
        
        // CRITICAL: This is the ONLY place where DAC is set during closeloop operation
        analogWrite(dacPin, current_dac_value);
        dac_output_active = (current_dac_value > 0);
        
        // Update userLedPin based on active current (measured_current_mA)
        int new_current_state = (measured_current_mA > 1.0) ? 1 : 0;
        if (new_current_state != last_current_state) {
            digitalWrite(userLedPin, new_current_state ? HIGH : LOW);
            last_current_state = new_current_state;
        }
    } else {
        // Conversion NOT ready - DAC is active with NO current feedback!
        conversion_miss_count++;
        
        if (conversion_miss_count >= conversion_miss_count_limit) {
            // 3+ consecutive failures (~6ms blind at 2ms timeout) = INA226 unresponsive
            Serial.println("EMERGENCY: INA226 conversion not ready for " + 
                          String(conversion_miss_count) + " consecutive attempts on device " + String(myDeviceId));
            Serial.println("EMERGENCY: DAC running blind at value " + String(current_dac_value) + " - forcing shutdown");
            ina226_available = false;
            
            emergencyShutdown();
            
            // Report failure through chain so master aborts all frames
            if (!isMasterDevice) {
                Serial1.println("000,ina_fail," + String(myDeviceId));
                if (Serial) {
                    Serial.println("DEBUG: Sent ina_fail report for device " + String(myDeviceId));
                }
            }
        }
    }
}

void handleProgramExecution()
{
    if (!frameExecutionActive || emergencyShutdownActive) return;
    
    // Check if current phase duration has elapsed
    if (millis() - programStartTime >= programDuration) {
        
        if (inPulsePhase) {
            // Pulse phase complete - end the pulse
            
            if (inCalibrationPhase) {
                // Frame_0: Calibration pulse complete
                // Master: Store calibrated DAC value and log results
                if (group_id == current_group && group_id > 0 && current_dac_value > 0) {
                    last_adjusted_dac = current_dac_value;  // Store calibrated DAC
                    
                    // Log calibration result with measured current
                    Serial.println("FRAME_0: G_ID=" + String(current_group) + 
                                  ", I=" + String(measured_current_mA) + "mA, DAC=" + String(current_dac_value) + ", CALIBRATED");
                } else {
                    // Slave devices or non-active groups - just log target
                    float groupCurrent = 0;
                    for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                        if (deviceCache[i].isSet && deviceCache[i].group_id == current_group) {
                            groupCurrent = deviceCache[i].current;
                            break;
                        }
                    }
                    Serial.println("FRAME_0: G_ID=" + String(current_group) + ", I_TARGET=" + String(groupCurrent) + "mA, CALIBRATED");
                }
            } else {
                // Frame_1+: Normal pulse complete
                // Master: Record DAC value before ending pulse if active in current group
                if (group_id == current_group && group_id > 0 && current_dac_value > 0 && closeloop_active) {
                    last_adjusted_dac = current_dac_value;
                }
            }
            
            digitalWrite(triggerOutPin, HIGH);
            
            // Switch to interframe delay phase
            inPulsePhase = false;
            programStartTime = millis();
            programDuration = interframeDelay;
            
        } else {
            // Interframe delay complete - check if we need to continue
            currentFrameLoop++;
            
            if (inCalibrationPhase) {
                // Frame_0: Check if calibration complete for all groups
                if (currentFrameLoop >= group_total) {
                    // Frame_0 complete - transition to Frame_1
                    Serial.println("FRAME_0: Calibration Complete");
                    
                    // CRITICAL: Broadcast calibration end to ALL slave devices
                    Serial1.println("000,calibration,end");
                    // Serial.println("DEBUG: Broadcasted calibration end to all slave devices");
                    
                    inCalibrationPhase = false;
                    currentFrameLoop = 1;  // Start Frame_1
                    
                    // Calculate total loops for user frames
                    totalLoops = frameCount * group_total;
                    // Serial.println("DEBUG: Starting user frames - Total loops: " + String(totalLoops));
                    
                    // Continue to next group for Frame_1
                }
            } else {
                // Frame_1+: Check if all user frames complete
                if (currentFrameLoop > totalLoops) {
                    // All frames complete
                    digitalWrite(triggerOutPin, HIGH);
                    Serial.println("DEBUG: All frames complete, final TRIGGER_OUT HIGH sent");
                    
                    program_success = true;
                    frameExecutionActive = false;
                    programDuration = 0;
                    Serial.println("PROGRAM_ACK:" + String(program_success ? "true" : "false"));
                    return;
                }
            }
            
            // Master device: Update current_group to stay synchronized with slaves
            current_group++;
            if (current_group > group_total) {
                current_group = 1;
            }
            
            // Get group-specific settings for current group
            int groupExposure;
            if (getGroupSettings(current_group, groupExposure)) {
                // Get target current for this group from cache
                float groupCurrent = 0;
                for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                    if (deviceCache[i].isSet && deviceCache[i].group_id == current_group) {
                        groupCurrent = deviceCache[i].current;
                        break;
                    }
                }
                
                if (inCalibrationPhase) {
                    // Frame_0: Log calibration start for next group
                    Serial.println("FRAME_0: G_ID=" + String(current_group) + ", I_TARGET=" + String(groupCurrent) + "mA");
                    programDuration = calibrationDuration;  // Use calibration duration
                } else {
                    // Frame_1+: Log normal frame start
                    int currentFrame = ((currentFrameLoop - 1) / group_total) + 1;
                    Serial.println("FRAME_" + String(currentFrame) + ": G_ID=" + String(current_group) + 
                                  ", I=" + String(groupCurrent) + "mA, EXP=" + String(groupExposure) + "ms");
                    programDuration = groupExposure;  // Use programmed exposure
                }
                
                // Start the next pulse
                inPulsePhase = true;
                programStartTime = millis();
                
                // Drive TRIGGER_OUT LOW to start the next pulse
                digitalWrite(triggerOutPin, LOW);
                
                // Master activates closeloop immediately after driving trigger LOW
                if (current_group == group_id && group_id > 0) {
                    if (inCalibrationPhase) {
                        // Frame_0: Start from calibration DAC
                        current_dac_value = dacCalibrationStart;
                    } else if (last_adjusted_dac > 0 && last_adjusted_dac != dacMax) {
                        // Frame_1+: Use last frame's DAC (includes Frame_0 calibration)
                        current_dac_value = last_adjusted_dac;
                    }
                    else {
                        current_dac_value = dacCalibrationStart;
                    }
                    closeloop_active = true;  // Enable closeloop
                }
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
            Serial.println("Failed to parse chain command: " + data);
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
                Serial.println("TOTAL:" + String(totalDevices));
                Serial.println("UI:Ready for commands");
            }
            else
            {
                Serial.println("Ignoring init command in state: " + String(currentState));
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

    // Handle healthcheck command (pre-start INA226 verification for all devices)
    if (cmd.command == "healthcheck")
    {
        if (isMasterDevice) {
            // Master: Receive healthcheck result from chain
            healthcheck_failed_device = cmd.value.toInt();
            healthcheck_complete = true;
            if (healthcheck_failed_device > 0) {
                Serial.println("HEALTHCHECK:FAIL:DEV" + String(healthcheck_failed_device));
            } else {
                Serial.println("HEALTHCHECK:PASS");
            }
        } else {
            // Slave: Check own INA226 and forward result
            int upstreamResult = cmd.value.toInt();
            if (upstreamResult > 0) {
                // Upstream device already failed - just forward the failure
                Serial1.println("000,healthcheck," + String(upstreamResult));
                if (Serial) {
                    Serial.println("DEBUG: Forwarding upstream healthcheck failure (device " + String(upstreamResult) + ")");
                }
            } else {
                // No upstream failure - check our own INA226
                if (verifyINA226()) {
                    Serial1.println("000,healthcheck,0");  // Pass
                    if (Serial) {
                        Serial.println("DEBUG: Device " + String(myDeviceId) + " healthcheck passed");
                    }
                } else {
                    Serial1.println("000,healthcheck," + String(myDeviceId));  // Fail
                    if (Serial) {
                        Serial.println("ERROR: Device " + String(myDeviceId) + " healthcheck FAILED");
                    }
                }
            }
        }
        return;  // Custom forwarding - don't use generic forwarding
    }

    // Handle ina_fail command (mid-operation INA226 failure report from slave)
    if (cmd.command == "ina_fail")
    {
        int failedDeviceId = cmd.value.toInt();
        if (isMasterDevice) {
            // Master: A slave reported INA226 failure mid-operation - abort everything
            Serial.println("EMERGENCY: Device " + String(failedDeviceId) + " reported INA226 failure mid-operation");
            Serial.println("EMERGENCY: Aborting all frame execution");
            emergencyShutdown();  // Broadcasts 000,dac,0 to all devices
        } else {
            // Slave: Another device failed - emergency shutdown and forward
            if (Serial) {
                Serial.println("EMERGENCY: Device " + String(failedDeviceId) + " reported INA226 failure - shutting down");
            }
            emergencyShutdown();
            // Forward ina_fail to next device in chain (towards master)
            Serial1.println("000,ina_fail," + String(failedDeviceId));
        }
        return;  // Custom forwarding - don't use generic forwarding
    }

    // Handle overcurrent command (mid-operation overcurrent report from slave)
    if (cmd.command == "overcurrent")
    {
        int failedDeviceId = cmd.value.toInt();
        if (isMasterDevice) {
            // Master: A slave reported overcurrent - abort everything
            Serial.println("EMERGENCY: Device " + String(failedDeviceId) + " reported overcurrent");
            emergencyShutdown();  // Broadcasts 000,dac,0 to all devices
        } else {
            // Slave: Another device had overcurrent - emergency shutdown and forward
            if (Serial) {
                Serial.println("EMERGENCY: Device " + String(failedDeviceId) + " reported overcurrent - shutting down");
            }
            emergencyShutdown();
            // Forward overcurrent to next device in chain (towards master)
            Serial1.println("000,overcurrent," + String(failedDeviceId));
        }
        return;  // Custom forwarding - don't use generic forwarding
    }

    // Handle blind_timeout command (DAC ran without INA226 feedback too long)
    if (cmd.command == "blind_timeout")
    {
        int failedDeviceId = cmd.value.toInt();
        if (isMasterDevice) {
            // Master: A slave's DAC ran blind too long - abort everything
            Serial.println("EMERGENCY: Device " + String(failedDeviceId) + " DAC blind timeout");
            emergencyShutdown();  // Broadcasts 000,dac,0 to all devices
        } else {
            // Slave: Another device had blind timeout - emergency shutdown and forward
            if (Serial) {
                Serial.println("EMERGENCY: Device " + String(failedDeviceId) + " DAC blind timeout - shutting down");
            }
            emergencyShutdown();
            // Forward to next device in chain (towards master)
            Serial1.println("000,blind_timeout," + String(failedDeviceId));
        }
        return;  // Custom forwarding - don't use generic forwarding
    }

    // Handle dac,0 command (emergency shutdown broadcast from master)
    if (cmd.command == "dac" && cmd.value.toInt() == 0)
    {
        if (!emergencyShutdownActive) {
            if (Serial) {
                Serial.println("EMERGENCY: Received dac,0 broadcast - shutting down");
            }
            emergencyShutdown();
        }
        // Slaves forward to next device; master consumes (it originated the message)
        if (!isMasterDevice) {
            Serial1.println(data);
        }
        return;  // Custom forwarding - don't use generic forwarding
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
            int new_group_id, new_group_total, new_exposure;
            float new_current;
            
            if (parseProgramCommand(cmd.value, new_group_id, new_group_total, new_current, new_exposure))
            {
                group_id = new_group_id;
                group_total = new_group_total;
                target_current_mA = new_current;
                safe_current_mA = target_current_mA * 0.99;  // 99% safety margin
                duration = new_exposure;
                current_group = 1; // Reset to group 1 when new program is set
                current_dac_value = 0;  // Will be set during Frame_0
                
                // Cache device settings (master only caches for all devices)
                if (isMasterDevice) {
                    if (cmd.deviceId >= 1 && cmd.deviceId <= totalDevices && cmd.deviceId <= MAX_DEVICES) {
                        // Specific device command - cache for that device
                        deviceCache[cmd.deviceId - 1].group_id = new_group_id;
                        deviceCache[cmd.deviceId - 1].current = new_current;
                        deviceCache[cmd.deviceId - 1].exposure = new_exposure;
                        deviceCache[cmd.deviceId - 1].isSet = true;
                    } else if (cmd.deviceId == 0) {
                        // Broadcast command - cache for all devices in chain
                        for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
                            deviceCache[i].group_id = new_group_id;
                            deviceCache[i].current = new_current;
                            deviceCache[i].exposure = new_exposure;
                            deviceCache[i].isSet = true;
                        }
                    }
                }
                
                if (Serial)
                {
                    Serial.println("DEBUG: Program set - Group:" + String(group_id) + 
                                   " Total:" + String(group_total) + 
                                   " Current:" + String(target_current_mA) + "mA" +
                                   " Exposure:" + String(duration) + "ms" +
                                   " (DAC will be auto-calibrated in Frame_0)");
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
        // Handle calibration mode command (for slave synchronization)
        else if (cmd.command == "calibration")
        {
            if (cmd.value == "start") {
                // CHECKPOINT: Verify INA226 before entering calibration (defense-in-depth)
                if (!verifyINA226()) {
                    if (Serial) {
                        Serial.println("ERROR: INA226 failed on device " + String(myDeviceId) + " during calibration start");
                        Serial.println("ERR:INA226_UNAVAILABLE");
                    }
                    // Report failure through chain so master aborts
                    Serial1.println("000,ina_fail," + String(myDeviceId));
                    return;
                }
                inCalibrationPhase = true;
                emergencyShutdownActive = false;  // Clear emergency lockout - master is starting fresh
                last_adjusted_dac = 0;  // Reset for fresh calibration
                current_dac_value = dacCalibrationStart;  // Reset DAC to start value (1300)
                dac_output_active = false;  // Reset DAC output tracking
                dac_blind_start_ms = 0;  // Clear blind watchdog
                overcurrent_consecutive_count = 0;  // Reset overcurrent counter
                if (Serial && !isMasterDevice) {
                    Serial.println("DEBUG: INA226 checkpoint passed, slave entering calibration mode");
                }
            } else if (cmd.value == "end") {
                inCalibrationPhase = false;
                if (Serial && !isMasterDevice) {
                    Serial.println("DEBUG: Slave exiting calibration mode - using normal DAC increment");
                }
            }
            // DO NOT FORWARD calibration commands - they're one-time broadcasts
            return;  // Exit before forwarding logic to prevent infinite loop
        }
        // Handle servo command
        else if (cmd.command == "servo")
        {
            int angle = cmd.value.toInt();
            angle = constrain(angle, 60, 120);
            targetServoPos = angle;
            if (Serial)
            {
                Serial.println("Servo set to:" + String(angle));
            }
        }
    }

    // Always forward unless we're master and it's our command returning
    // OR it's a calibration command (already processed, don't loop)
    if ((!isMasterDevice || (isMasterDevice && data != pendingCommand)) && cmd.command != "calibration")
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

bool parseProgramCommand(String value, int &group_id, int &group_total, float &current, int &exposure)
{
    // Remove curly brackets
    if (value.length() < 3 || value.charAt(0) != '{' || value.charAt(value.length() - 1) != '}') {
        return false;
    }
    
    value = value.substring(1, value.length() - 1);
    
    // Split by comma - expecting 4 parameters (no DAC - auto-calibrated in Frame_0)
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
        current < 0 || current > maxCurrent_mA || 
        exposure < 10) {
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
    Serial.println("UI:  Program Control:");
    Serial.println("UI:    xxx,program,{group_id,group_total,current,exposure}");
    Serial.println("UI:      - group_id: Group number (1-n)");
    Serial.println("UI:      - group_total: Total groups");
    Serial.println("UI:      - current: Target current in mA (0-1500)");
    Serial.println("UI:      - exposure: Duration in ms (>=10)");
    Serial.println("UI:      - DAC auto-calibrated during Frame_0");
    Serial.println("UI:    frame,count,interframe_delay - Set frame parameters");
    Serial.println("UI:    start - Start Frame_0 calibration + user frames");
    Serial.println("UI:  Where xxx is:");
    Serial.println("UI:    000 = all devices");
    Serial.println("UI:    001-" + String(totalDevices) + " = specific device");
    Serial.println("UI:  System Commands:");
    Serial.println("UI:    help     - Show this help");
    Serial.println("UI:    status   - Show system status");
    Serial.println("UI:    reinit   - Restart device initialization");
    Serial.println("UI:    emergency - Emergency shutdown (master only)");
    Serial.println("UI:  Examples:");
    Serial.println("UI:    001,program,{1,2,1300,20}");
    Serial.println("UI:      Device 1, group 1 of 2, I=1300mA, 20ms exposure");
    Serial.println("UI:    frame,5,50              - Set 5 frames with 50ms interframe delay");
    Serial.println("UI:    start                   - Start program (Frame_0 + 5 frames)");
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
                Serial.println("DEV:" + String(i + 1) + 
                              ", G_ID:" + String(deviceCache[i].group_id) + 
                              ", I:" + String(deviceCache[i].current) + "mA" +
                              ", EXP:" + String(deviceCache[i].exposure) + "ms");
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
    // Capture whether we were already in emergency (for broadcast guard)
    bool alreadyActive = emergencyShutdownActive;
    
    // Set sticky flag FIRST - prevents interrupt from re-activating closeloop
    emergencyShutdownActive = true;

    Serial.println("EMERGENCY SHUTDOWN ACTIVATED!");
    
    // Turn off DAC immediately
    analogWrite(dacPin, 0);
    digitalWrite(userLedPin, LOW);
    
    // Reset runtime control variables (preserve program configuration for restart via 'start')
    last_adjusted_dac = 0;
    current_dac_value = 0;
    closeloop_active = false;
    dac_output_active = false;
    dac_blind_start_ms = 0;
    last_current_state = 0;
    overcurrent_consecutive_count = 0;
    conversion_miss_count = 0;
    
    // Stop frame execution
    programDuration = 0;
    frameExecutionActive = false;
    
    // Ensure trigger line is HIGH (inactive)
    digitalWrite(triggerOutPin, HIGH);
    
    // Broadcast shutdown to all devices (only once - guard prevents infinite loops)
    if (isMasterDevice && !alreadyActive)
    {
        if (Serial) {
            Serial.println("***ALERT: Broadcasting emergency shutdown to all devices***");
        }
        Serial1.println("000,dac,0");
    }
    
    if (Serial) {
        Serial.println("System shutdown complete. Use 'start' to re-calibrate and resume.");
    }
}

// Function to get exposure for a specific group from device cache (DAC is auto-calibrated)
bool getGroupSettings(int group_id, int &group_exposure)
{
    // Look for any device with the specified group_id in the cache
    for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
        if (deviceCache[i].isSet && deviceCache[i].group_id == group_id) {
            group_exposure = deviceCache[i].exposure;
            return true;
        }
    }
    
    // If no device found with this group_id, return default value
    group_exposure = 10;
    return false;
}

// Function to validate that all devices in each group have the same exposure time
bool validateGroupExposures()
{
    // Array to store first exposure value found for each group
    int groupExposures[MAX_DEVICES];  // Index by group_id-1
    bool groupHasDevices[MAX_DEVICES]; // Track if group has any devices
    
    // Initialize arrays
    for (int i = 0; i < MAX_DEVICES; i++) {
        groupExposures[i] = -1;  // -1 means not set yet
        groupHasDevices[i] = false;
    }
    
    // Check all programmed devices
    bool hasAnyProgrammedDevices = false;
    for (int i = 0; i < totalDevices && i < MAX_DEVICES; i++) {
        if (deviceCache[i].isSet && deviceCache[i].group_id > 0) {
            hasAnyProgrammedDevices = true;
            int gid = deviceCache[i].group_id;
            
            // Validate group_id is within bounds
            if (gid < 1 || gid > MAX_DEVICES) {
                Serial.println("ERROR: Device " + String(i + 1) + " has invalid group_id: " + String(gid));
                return false;
            }
            
            int exposure = deviceCache[i].exposure;
            
            // Check if this is the first device we've seen for this group
            if (groupExposures[gid - 1] == -1) {
                // First device in this group - store its exposure
                groupExposures[gid - 1] = exposure;
                groupHasDevices[gid - 1] = true;
            } else {
                // Not the first device - compare exposure with stored value
                if (groupExposures[gid - 1] != exposure) {
                    // Mismatch found!
                    Serial.println("ERROR: Exposure mismatch in Group " + String(gid));
                    
                    // List all devices in this group with their exposures
                    Serial.println("ERROR: Devices in Group " + String(gid) + ":");
                    for (int j = 0; j < totalDevices && j < MAX_DEVICES; j++) {
                        if (deviceCache[j].isSet && deviceCache[j].group_id == gid) {
                            Serial.println("  Device " + String(j + 1) + ": " + String(deviceCache[j].exposure) + "ms");
                        }
                    }
                    return false;
                }
            }
        }
    }
    
    // Check if any devices are programmed
    if (!hasAnyProgrammedDevices) {
        Serial.println("ERROR: No devices programmed. Use program command first.");
        return false;
    }
    
    // All validation passed
    Serial.println("DEBUG: Group exposure validation passed");
    return true;
}
