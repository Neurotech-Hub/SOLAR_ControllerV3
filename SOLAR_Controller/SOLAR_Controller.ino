// Round-Robin Communication Controller with ItsyBitsy M4 and INA226 Current Control

#include <Arduino.h>
#include <Servo.h>
#include <Wire.h>
#include <INA226.h>

// Version tracking
const String CODE_VERSION = "1.000";

// Pin assignments for ItsyBitsy M4
const int dacPin = A0;          // DAC output (12-bit, 0-4095)
const int servoPin = 5;        // Servo control
const int ledPin = 13; // Status LED
const int rxReadyPin = 7;      // Chain ready input
const int txReadyPin = 9;      // Chain ready output
const int userLedPin = 2;      // User LED
// Serial1 uses D1 (TX) and D0 (RX)

// INA226 Configuration
INA226 ina226(0x4A);  // Default I2C address
const float SHUNT_RESISTANCE = 0.04195;  // 58mΩ shunt for 1.4A max
const float MAX_CURRENT_MA = 1500.0;   // Maximum current in mA
const float MIN_CURRENT_MA = 0.0;      // Minimum current in mA
const float SAFETY_SHUTDOWN_MA = 1350.0; // Safety shutdown threshold

// Current Control Parameters (optimized from INA226_Test)
const float KP = 0.1;           // Proportional gain (conservative)
const float KI = 0.01;          // Integral gain (small to prevent oscillations)
const float KD = 0.001;         // Derivative gain (minimal)
const int CONTROL_RATE_MS = 100; // Control loop rate (10Hz)
const int MIN_DAC_ADJUSTMENT = 1; // Minimum DAC adjustment
const int MAX_DAC_STEP = 50;    // Maximum DAC change per cycle (prevents oscillations)

// Ramping Parameters
const float RAMP_RATE_MA_PER_SEC = 100.0; // Current ramp rate
const int RAMP_UPDATE_MS = 50;  // Ramp update rate (20Hz)

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
    WAITING_FOR_CHAIN, // Waiting for rxReady to go HIGH
    CHAIN_READY,       // Chain is ready, normal operation
    INIT_IN_PROGRESS,  // Only used by master during initialization
    PROCESSING,        // Master is processing a command
    READY              // Master is ready to receive new commands
};

// Current control states
enum CurrentControlState
{
    CURRENT_OFF,       // Current control disabled
    CURRENT_RAMPING,   // Ramping to target
    CURRENT_STABLE,    // At target, maintaining
    CURRENT_ERROR      // Error condition
};

// Global variables
DeviceState currentState = WAITING_FOR_CHAIN;
CurrentControlState currentControlState = CURRENT_OFF;
bool isMasterDevice = false;
int myDeviceId = 0;
int totalDevices = 0;
Servo myServo;
int currentServoPos = 90;
int targetServoPos = 90;
String pendingCommand = ""; // Track master's current command

// INA226 and Current Control variables
bool ina226Initialized = false;
float targetCurrentMA = 0.0;
float currentMA = 0.0;
float voltageV = 0.0;
float powerMW = 0.0;
float shuntVoltageMV = 0.0;
int currentDacValue = 0;
int targetDacValue = 0;
bool currentControlEnabled = false;

// PID Control variables
float integralError = 0.0;
float lastError = 0.0;
unsigned long lastControlTime = 0;
unsigned long lastRampTime = 0;

// Ramping variables
float rampTargetMA = 0.0;
float rampStartMA = 0.0;
unsigned long rampStartTime = 0;
bool ramping = false;

// Safety and monitoring
unsigned long lastMeasurementTime = 0;
int consecutiveErrors = 0;
const int MAX_CONSECUTIVE_ERRORS = 5;

// Function declarations
void updateServo();
void processCommand(String data);
bool parseCommand(String data, Command &cmd);
void updateStatusLED();
void waitForChain();
void reinitializeDevices();
void printHelp();
void printStatus();
void sendCommandAndWait(String command);
bool initializeINA226();
void updateCurrentControl();
void setTargetCurrent(float currentMA);
void measureCurrent();
void displayCurrentStatus();
void updateRamping();
void safetyCheck();
void emergencyShutdown();

void setup()
{
    // Initialize pins
    pinMode(dacPin, OUTPUT);
    pinMode(ledPin, OUTPUT);
    pinMode(rxReadyPin, INPUT_PULLDOWN);
    pinMode(txReadyPin, OUTPUT);
    pinMode(userLedPin, OUTPUT);

    // Initialize outputs
    digitalWrite(txReadyPin, LOW); // Start with txReady LOW
    digitalWrite(userLedPin, LOW);

    // Initialize servo
    myServo.attach(servoPin);
    myServo.write(90);

    // Initialize DAC
    analogWriteResolution(12);  // Set 12-bit resolution for ItsyBitsy M4
    analogWrite(dacPin, 0);     // Start with DAC off

    // Initialize I2C and INA226
    Wire.begin();
    if (initializeINA226()) {
        ina226Initialized = true;
    }

    // Initialize Serial interfaces
    Serial.begin(115200); // USB
    Serial1.begin(9600);  // Device chain

    // Wait for either Serial (master) or rxReadyPin (slave)
    while (!isMasterDevice)
    {
        if (Serial)
        {
            isMasterDevice = true;
            myDeviceId = 1; // Master is always device 1
            Serial.println("\nSOLAR Controller Starting...");
            Serial.println("VER:" + CODE_VERSION);
            Serial.println("DEBUG: Setting txReady HIGH to start chain");
            digitalWrite(txReadyPin, HIGH);
            break;
        }
        if (digitalRead(rxReadyPin) == HIGH)
        {
            isMasterDevice = false;
            break;
        }
        digitalWrite(ledPin, (millis() / 500) % 2); // Blink while waiting
        delay(10);                                  // Small delay to prevent tight loop
    }

    // Master must wait for rxReady before proceeding
    if (isMasterDevice)
    {
        Serial.println("DEBUG: Master waiting for rxReady...");
        while (digitalRead(rxReadyPin) == LOW)
        {
            digitalWrite(ledPin, (millis() / 500) % 2);
            delay(10);
        }
        Serial.println("DEBUG: Chain connected to master!");
    }

    // Initial chain connection
    waitForChain();
}

void loop()
{
    // Always mirror rxReady to txReady for slaves
    if (!isMasterDevice)
    {
        digitalWrite(txReadyPin, digitalRead(rxReadyPin));
    }

    // Update LED status
    updateStatusLED();

    // Safety check
    safetyCheck();

    // Update current control if enabled
    if (currentControlEnabled && ina226Initialized)
    {
        updateCurrentControl();
        updateRamping();
    }

    // If rxReady is LOW, chain is broken
    if (digitalRead(rxReadyPin) == LOW)
    {
        if (currentState != WAITING_FOR_CHAIN)
        {
            currentState = WAITING_FOR_CHAIN;
            Serial1.flush(); // Clear any pending data
            if (isMasterDevice)
            {
                Serial.println("DEBUG: rxReady went LOW - chain broken!");
                Serial.println("DEBUG: Attempting to re-establish chain...");
                pendingCommand = ""; // Clear any pending command
            }
            // Reset device state
            myDeviceId = isMasterDevice ? 1 : 0;
            totalDevices = 0;
            // Wait for chain to reconnect
            waitForChain();
        }
        return;
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
            else if (command == "current")
            {
                if (ina226Initialized) {
                    measureCurrent();
                    displayCurrentStatus();
                } else {
                    Serial.println("ERR:INA226_NOT_INITIALIZED");
                }
                return;
            }
            else if (command == "emergency")
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
                Serial.println("UI:Example: 001,servo,90 or 000,current,500");
                return;
            }

            // Validate command type
            if (cmd.command != "servo" && cmd.command != "current" && cmd.command != "dac" && cmd.command != "init")
            {
                Serial.println("ERR:INVALID_COMMAND:" + cmd.command);
                Serial.println("UI:Valid commands: servo, current, dac");
                Serial.println("UI:Example: 001,servo,90 or 002,current,500");
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
            else if (cmd.command == "current")
            {
                float current = cmd.value.toFloat();
                if (current < MIN_CURRENT_MA || current > MAX_CURRENT_MA)
                {
                    Serial.println("ERR:CURRENT_RANGE:" + String(current));
                    Serial.println("UI:Current must be " + String(MIN_CURRENT_MA) + "-" + String(MAX_CURRENT_MA) + " mA");
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

    if (Serial)
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
        if (Serial)
        {
            Serial.println("DEBUG: Device " + String(myDeviceId) + " executing command");
        }

        // Handle servo command
        if (cmd.command == "servo")
        {
            int angle = cmd.value.toInt();
            angle = constrain(angle, 60, 120);
            targetServoPos = angle;
            if (Serial)
            {
                Serial.println("SRV:" + String(myDeviceId) + ":" + String(angle));
            }
        }
        // Handle current command (closed-loop control with ramping)
        else if (cmd.command == "current")
        {
            float current = cmd.value.toFloat();
            current = constrain(current, MIN_CURRENT_MA, MAX_CURRENT_MA);
            setTargetCurrent(current);
            if (Serial)
            {
                Serial.println("CUR:" + String(myDeviceId) + ":" + String(current));
            }
        }
        // Handle DAC command (direct control)
        else if (cmd.command == "dac")
        {
            int value = cmd.value.toInt();
            value = constrain(value, 0, 4095);
            targetDacValue = value;
            currentDacValue = value;
            analogWrite(dacPin, value);
            currentControlEnabled = false; // Disable current control when using direct DAC
            currentControlState = CURRENT_OFF;
            digitalWrite(userLedPin, value > 1200 ? HIGH : LOW);
            if (Serial)
            {
                Serial.println("DAC:" + String(myDeviceId) + ":" + String(value));
            }
        }
    }

    // Always forward unless we're master and it's our command returning
    if (!isMasterDevice || (isMasterDevice && data != pendingCommand))
    {
        if (Serial)
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
        Serial.println("DEBUG: Waiting for rxReady to go HIGH...");
    }

    // Wait for chain to be ready (rxReady HIGH)
    while (digitalRead(rxReadyPin) == LOW)
    {
        // Blink LED while waiting
        digitalWrite(ledPin, (millis() / 500) % 2);
        if (isMasterDevice && (millis() % 1000 == 0))
        { // Print every second
            Serial.println("DEBUG: rxReady still LOW");
            delay(200); // Prevent message flood
        }
    }

    // Chain is ready
    currentState = CHAIN_READY;
    if (isMasterDevice)
    {
        Serial.println("DEBUG: rxReady is HIGH - chain connected!");
        // Start initialization
        currentState = INIT_IN_PROGRESS;
        Serial.println("DEBUG: Starting device initialization");
        Serial.println("DEBUG: Sending initial command: 000,init,001");
        // Send initial device count
        Serial1.println("000,init,001");
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
    
    // Check chain connection
    if (digitalRead(rxReadyPin) == LOW)
    {
        Serial.println("ERR:CHAIN_NOT_CONNECTED");
        Serial.println("UI:Chain not connected - check physical connections");
        currentState = WAITING_FOR_CHAIN;
        return;
    }
    
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
    Serial.println("UI:    xxx,current,ma  - Set target current (0-" + String(MAX_CURRENT_MA) + "mA)");
    Serial.println("UI:    xxx,dac,value   - Set DAC value directly (0-4095)");
    Serial.println("UI:  Where xxx is:");
    Serial.println("UI:    000 = all devices");
    Serial.println("UI:    001-" + String(totalDevices) + " = specific device");
    Serial.println("UI:  System Commands:");
    Serial.println("UI:    help     - Show this help");
    Serial.println("UI:    status   - Show system status");
    Serial.println("UI:    reinit   - Restart device initialization");
    Serial.println("UI:    current  - Show current measurement (master only)");
    Serial.println("UI:    emergency - Emergency shutdown (master only)");
    Serial.println("UI:  Examples:");
    Serial.println("UI:    000,current,500 - Set all devices to 500mA");
    Serial.println("UI:    001,servo,90   - Set device 1 servo to 90°");
    Serial.println("UI:    002,dac,2048   - Set device 2 DAC to 50%");
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
    
    if (pendingCommand.length() > 0)
    {
        Serial.println("PENDING:" + pendingCommand);
    }

    // Show INA226 status
    Serial.println("INA226:" + String(ina226Initialized ? "OK" : "FAIL"));
    if (ina226Initialized)
    {
        String controlStateName;
        switch (currentControlState)
        {
            case CURRENT_OFF:
                controlStateName = "OFF";
                break;
            case CURRENT_RAMPING:
                controlStateName = "RAMPING";
                break;
            case CURRENT_STABLE:
                controlStateName = "STABLE";
                break;
            case CURRENT_ERROR:
                controlStateName = "ERROR";
                break;
            default:
                controlStateName = "UNKNOWN";
                break;
        }
        Serial.println("CURRENT_CONTROL:" + controlStateName);
        Serial.println("TARGET_CURRENT:" + String(targetCurrentMA) + "mA");
        Serial.println("CURRENT_DAC:" + String(currentDacValue));
        Serial.println("RAMPING:" + String(ramping ? "YES" : "NO"));
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
        // Call loop to handle incoming data
        loop();

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

// INA226 Initialization
bool initializeINA226()
{
    if (!ina226.begin()) {
        Serial.println("ERROR: Could not connect to INA226");
        Serial.println("Check I2C connections and address");
        return false;
    }

    // Configure INA226
    ina226.setAverage(4);  // 16 samples
    ina226.setBusVoltageConversionTime(4);  // 1.1ms
    ina226.setShuntVoltageConversionTime(4);  // 1.1ms
    ina226.setMode(7);  // Continuous shunt and bus measurement

    // Calculate and set calibration
    int result = ina226.setMaxCurrentShunt(MAX_CURRENT_MA/1000.0, SHUNT_RESISTANCE, true);
    
    if (result != INA226_ERR_NONE) {
        Serial.println("ERROR: Calibration failed with error: 0x" + String(result, HEX));
        return false;
    }

    Serial.println("INA226 initialized successfully!");
    Serial.println("Current LSB: " + String(ina226.getCurrentLSB_mA(), 6) + " mA");
    Serial.println("Shunt resistance: " + String(SHUNT_RESISTANCE, 6) + " Ω");
    Serial.println("Max current: " + String(MAX_CURRENT_MA) + " mA");
    return true;
}

// Current Control Functions
void setTargetCurrent(float currentMA)
{
    float newTarget = constrain(currentMA, MIN_CURRENT_MA, MAX_CURRENT_MA);
    
    if (newTarget != targetCurrentMA) {
        // Start ramping if target changed
        rampStartMA = targetCurrentMA;
        rampTargetMA = newTarget;
        rampStartTime = millis();
        ramping = true;
        
        Serial.println("Starting ramp: " + String(rampStartMA) + " -> " + String(rampTargetMA) + " mA");
    }
    
    targetCurrentMA = newTarget;
    currentControlEnabled = (targetCurrentMA > 0);
    
    if (currentControlEnabled) {
        // Reset PID variables
        integralError = 0.0;
        lastError = 0.0;
        lastControlTime = millis();
        consecutiveErrors = 0;
        
        currentControlState = CURRENT_RAMPING;
        Serial.println("Current control enabled: " + String(targetCurrentMA) + " mA");
    } else {
        // Turn off DAC
        targetDacValue = 0;
        currentDacValue = 0;
        analogWrite(dacPin, 0);
        digitalWrite(userLedPin, LOW);
        currentControlState = CURRENT_OFF;
        ramping = false;
        Serial.println("Current control disabled");
    }
}

void updateCurrentControl()
{
    unsigned long currentTime = millis();
    
    // Run control loop at specified rate
    if (currentTime - lastControlTime >= CONTROL_RATE_MS) {
        measureCurrent();
        
        // Calculate error
        float error = targetCurrentMA - currentMA;
        
        // PID control with anti-windup and limits
        integralError += error * (CONTROL_RATE_MS / 1000.0);
        float derivativeError = (error - lastError) / (CONTROL_RATE_MS / 1000.0);
        
        // Anti-windup: limit integral term
        if (abs(integralError) > 1000) {
            integralError = (integralError > 0) ? 1000 : -1000;
        }
        
        // Calculate control output
        float controlOutput = KP * error + KI * integralError + KD * derivativeError;
        
        // Convert to DAC adjustment with limits
        int dacAdjustment = (int)controlOutput;
        dacAdjustment = constrain(dacAdjustment, -MAX_DAC_STEP, MAX_DAC_STEP);
        
        // Apply minimum adjustment threshold
        if (abs(dacAdjustment) < MIN_DAC_ADJUSTMENT) {
            dacAdjustment = (dacAdjustment >= 0) ? MIN_DAC_ADJUSTMENT : -MIN_DAC_ADJUSTMENT;
        }
        
        // Update DAC value
        targetDacValue += dacAdjustment;
        targetDacValue = constrain(targetDacValue, 0, 4095);
        
        // Apply DAC value
        currentDacValue = targetDacValue;
        analogWrite(dacPin, currentDacValue);
        digitalWrite(userLedPin, currentDacValue > 1200 ? HIGH : LOW);
        
        // Update variables for next iteration
        lastError = error;
        lastControlTime = currentTime;
        
        // Check for stability
        if (abs(error) < 10.0) { // Within 10mA of target
            consecutiveErrors = 0;
            if (!ramping) {
                currentControlState = CURRENT_STABLE;
            }
        } else {
            consecutiveErrors++;
            if (consecutiveErrors > MAX_CONSECUTIVE_ERRORS) {
                currentControlState = CURRENT_ERROR;
                Serial.println("WARNING: Current control error - " + String(consecutiveErrors) + " consecutive errors");
            }
        }
    }
}

void updateRamping()
{
    if (!ramping) return;
    
    unsigned long currentTime = millis();
    if (currentTime - lastRampTime >= RAMP_UPDATE_MS) {
        float elapsed = (currentTime - rampStartTime) / 1000.0; // seconds
        float rampDistance = rampTargetMA - rampStartMA;
        float rampTime = abs(rampDistance) / RAMP_RATE_MA_PER_SEC;
        
        if (elapsed >= rampTime) {
            // Ramp complete
            targetCurrentMA = rampTargetMA;
            ramping = false;
            currentControlState = CURRENT_STABLE;
            Serial.println("Ramp complete: " + String(targetCurrentMA) + " mA");
        } else {
            // Calculate current target based on ramp progress
            float progress = elapsed / rampTime;
            targetCurrentMA = rampStartMA + (rampDistance * progress);
        }
        
        lastRampTime = currentTime;
    }
}

void measureCurrent()
{
    if (!ina226Initialized) return;
    
    shuntVoltageMV = ina226.getShuntVoltage_mV();
    voltageV = ina226.getBusVoltage();
    currentMA = ina226.getCurrent_mA();
    powerMW = ina226.getPower() * 1000; // Convert to mW
    lastMeasurementTime = millis();
}

void displayCurrentStatus()
{
    Serial.println("=== Current Status ===");
    Serial.println("Target Current: " + String(targetCurrentMA) + " mA");
    Serial.println("Measured Current: " + String(currentMA, 2) + " mA");
    Serial.println("Bus Voltage: " + String(voltageV, 3) + " V");
    Serial.println("Shunt Voltage: " + String(shuntVoltageMV, 3) + " mV");
    Serial.println("Power: " + String(powerMW, 1) + " mW");
    Serial.println("DAC Value: " + String(currentDacValue) + " (0-4095)");
    Serial.println("DAC Voltage: " + String((currentDacValue * 3.3) / 4095.0, 3) + " V");
    Serial.println("Control State: " + String(currentControlState));
    Serial.println("Ramping: " + String(ramping ? "Yes" : "No"));
    Serial.println("Consecutive Errors: " + String(consecutiveErrors));
    Serial.println("=====================");
}

void safetyCheck()
{
    if (!ina226Initialized || !currentControlEnabled) return;
    
    // Check for overcurrent
    if (currentMA > SAFETY_SHUTDOWN_MA) {
        Serial.println("EMERGENCY: Overcurrent detected! " + String(currentMA) + " mA");
        emergencyShutdown();
    }
    
    // Check for measurement timeout
    if (millis() - lastMeasurementTime > 5000) { // 5 second timeout
        Serial.println("EMERGENCY: INA226 measurement timeout!");
        emergencyShutdown();
    }
}

void emergencyShutdown()
{
    Serial.println("EMERGENCY SHUTDOWN ACTIVATED!");
    
    // Turn off DAC immediately
    analogWrite(dacPin, 0);
    digitalWrite(userLedPin, LOW);
    
    // Reset control variables
    currentControlEnabled = false;
    currentControlState = CURRENT_OFF;
    targetCurrentMA = 0.0;
    currentDacValue = 0;
    targetDacValue = 0;
    ramping = false;
    
    // Reset PID variables
    integralError = 0.0;
    lastError = 0.0;
    consecutiveErrors = 0;
    
    Serial.println("System shutdown complete. Check hardware and restart.");
}
