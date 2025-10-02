/*
 * INA226 Closeloop Controller to calculate DAC value for a given current limit
 * 
 * Commands:
 * - current [value]: Set target current for closed-loop control (0-1500 mA)
 * - dac,[dac_value],[current_value]: Start DAC with initial value and maintain target current
 * 
 * INA226 Configuration:
 * - Conversion time: 1.1ms (1100us) for both bus and shunt voltage
 * - Averaging: 4 samples
 * - Mode: Continuous shunt and bus voltage measurement
 * - Optimized for current-only readings
 */

#include <INA226.h>
#include <Wire.h>

INA226 ina226(0x4A);

// DAC pin for ItsyBitsy M4
const int dacPin = A0; // DAC output pin

// Configuration
float shuntResistance = 0.04195; // 42mΩ shunt
float maxCurrent_mA = 1500;      // 1.5A max current to stay within 81.9mV shunt voltage limit
float current = 0.0;
float targetCurrent_mA = 0; // Target current in mA
int currentDacValue = 1250;    // Current DAC value (0-4095)
const int dacMin = 0;       // Minimum DAC value
const int dacMax = 1900;    // Maximum DAC value
int parsedDACValue = 0;

float current_limit_mA = 0; // User defined current limit
float safe_margin_mA = 0;   // 1% below current_limit_mA

const uint16_t ALERT_CONVERSION_READY = 0x0400;

// Timing variables
unsigned long startTime = 0;
unsigned long endTime = 0;
unsigned long readTime = 0;

// Timing variables for safe margin tracking
unsigned long commandStartTime = 0;      // When dac/current command was received
unsigned long safeMarginReachedTime = 0;    // When current first reached safe_margin_mA
unsigned long targetReachedTime = 0;        // When current first reached target (for current command)
bool safeMarginReached = false;             // Flag to track if we've reached safe margin
bool targetReached = false;                 // Flag to track if we've reached target (for current command)
bool shouldTerminate = false;               // Flag to indicate loop should terminate
bool isCurrentCommand = false;              // Flag to distinguish between current and dac commands

// Helper function to calculate DAC reduction based on overcurrent amount
int calculateDacReduction(float currentReading, float targetCurrent) {
  float overcurrent = currentReading - targetCurrent;
  
  
  if (overcurrent >= 200.0) {
    return 50;  // 200mA or more: reduce by 50
  } else if (overcurrent >= 100.0) {
    return 20;  // 100mA or more: reduce by 15
  } else if (overcurrent >= 50.0) {
    return 10;  // 50mA or more: reduce by 10
  } else if (overcurrent >= 40.0) {
    return 5;  // 40mA or more: reduce by 5
  } else if (overcurrent >= 30.0) {
    return 4;  // 30mA or more: reduce by 4
  } else if (overcurrent >= 20.0) {
    return 3;  // 20-29mA: reduce by 3
  } else if (overcurrent >= 10.0) {
    return 2;  // 10-19mA: reduce by 2
  } else {
    return 0;  // Less than 10mA: no reduction needed
  }
}

void setup()
{
  Serial.begin(115200);

  // Initialize I2C
  Wire.begin();

  // Initialize INA226
  if (initializeINA226())
  {
    Serial.println("INA226 initialized successfully!");
  }
  else
  {
    Serial.println("ERROR: Failed to initialize INA226!");
    Serial.println("Check connections and I2C address");
  }

  Serial.println();
  Serial.println("Commands:");
  Serial.println("  current,[value]       - Find optimal DAC for target current");
  Serial.println("  dac,[dac],[limit]     - Start with DAC value and current limit");
  Serial.println("Examples: current,1200  dac,1800,1200");
  Serial.println();
}

void loop()
{
  if (Serial.available())
  {
    String input = Serial.readStringUntil('\n');
    input.trim();

    // Parse commands
    if (input.startsWith("current,"))
    {
      float newTarget = input.substring(8).toFloat();
      if (newTarget >= 0 && newTarget <= maxCurrent_mA)
      {
        targetCurrent_mA = newTarget;
        if (targetCurrent_mA == 0)
        {
          currentDacValue = 0;
          analogWrite(dacPin, currentDacValue);
        }
        else
        {
          current_limit_mA = targetCurrent_mA;
          safe_margin_mA = current_limit_mA * 0.99;
          commandStartTime = millis();
          safeMarginReached = false;
          targetReached = false;
          shouldTerminate = false;
          isCurrentCommand = true;
          Serial.print("Current target: ");
          Serial.print(targetCurrent_mA);
          Serial.println(" mA - Finding optimal DAC...");
        }
      }
      else
      {
        Serial.println("ERROR: Current out of range (0-" + String(maxCurrent_mA) + " mA)");
      }
    }
    else if (input.startsWith("dac,"))
    {
      String args = input.substring(4);
      int commaIndex = args.indexOf(',');
      
      if (commaIndex != -1)
      {
        int initialDac = args.substring(0, commaIndex).toInt();
        parsedDACValue = initialDac;
        float currentLimit = args.substring(commaIndex + 1).toFloat();
        
        if (initialDac >= dacMin && initialDac <= dacMax && currentLimit >= 0 && currentLimit <= maxCurrent_mA)
        {
          currentDacValue = initialDac;
          targetCurrent_mA = currentLimit;
          current_limit_mA = currentLimit;
          safe_margin_mA = current_limit_mA * 0.99;
          analogWrite(dacPin, currentDacValue);
          
          commandStartTime = millis();
          safeMarginReached = false;
          targetReached = false;
          shouldTerminate = false;
          isCurrentCommand = false;
          
          Serial.print("DAC start: ");
          Serial.print(currentDacValue);
          Serial.print(", Limit: ");
          Serial.print(targetCurrent_mA);
          Serial.println(" mA");
        }
        else
        {
          Serial.println("ERROR: Values out of range");
        }
      }
      else
      {
        Serial.println("ERROR: Use dac,[dac_value],[current_limit]");
      }
    }
    else
    {
      Serial.println("ERROR: Unknown command. Use current,[value] or dac,[dac],[limit]");
    }
  }

  // Control loop
  if (targetCurrent_mA > 0 && !shouldTerminate)
  {
    if (!ina226.waitConversionReady())
    {
      Serial.println("Timeout!");
      return;
    }

    startTime = micros();
    current = ina226.getCurrent_mA();
    endTime = micros();
    readTime = endTime - startTime;

    if (isCurrentCommand) {
      if (!targetReached && current >= targetCurrent_mA) {
        targetReachedTime = millis();
        targetReached = true;
        Serial.print("Target reached: ");
        Serial.print(targetReachedTime - commandStartTime);
        Serial.println(" ms");
      }
      if (targetReached && (millis() - targetReachedTime >= 50)) {
        shouldTerminate = true;
        Serial.println("=== RESULTS ===");
        Serial.print("Current (mA): "); Serial.println(current);
        Serial.print("DAC: "); Serial.println(currentDacValue);
        Serial.print("Time (ms): "); Serial.println(targetReachedTime - commandStartTime);        
        Serial.println("===============");
        analogWrite(dacPin, 0);
        targetCurrent_mA = 0;
        return;
      }
    } else {
      if (!safeMarginReached && current >= safe_margin_mA) {
        safeMarginReachedTime = millis();
        safeMarginReached = true;
        Serial.print("Safe margin: ");
        Serial.print(safeMarginReachedTime - commandStartTime);
        Serial.println(" ms");
      }
      if (safeMarginReached && (millis() - safeMarginReachedTime >= 50)) {
        shouldTerminate = true;
        Serial.println("=== RESULTS ===");
        Serial.print("Current (mA): "); Serial.println(current);
        Serial.print("DAC: "); Serial.println(parsedDACValue);
        Serial.print("Time (ms): "); Serial.println(safeMarginReachedTime - commandStartTime);
        Serial.println("===============");
        analogWrite(dacPin, 0);
        targetCurrent_mA = 0;
        return;
      }
    }

    // DAC control
    if (isCurrentCommand) {
      if (current < targetCurrent_mA) {
        currentDacValue++;
      } else if (current > targetCurrent_mA) {
        int reduction = calculateDacReduction(current, targetCurrent_mA);
        if (reduction > 0) {
          currentDacValue -= reduction;
        } else {
          currentDacValue--;
        }
      }
    } else {
      if (current >= current_limit_mA) {
        int reduction = calculateDacReduction(current, current_limit_mA);
        if (reduction > 0) {
          currentDacValue -= reduction;
          Serial.print("Limit! DAC-");
          Serial.print(reduction);
          Serial.print(" (");
          Serial.print(current - current_limit_mA, 1);
          Serial.println("mA)");
        } else {
          currentDacValue--;
        }
      } else if (current < safe_margin_mA) {
        currentDacValue++;
      }
    }

    currentDacValue = constrain(currentDacValue, dacMin, dacMax);
    analogWrite(dacPin, currentDacValue);

    Serial.print("DAC:");
    Serial.print(currentDacValue);
    Serial.print(" I:");
    Serial.print(current);
    Serial.print("mA T:");
    Serial.print(readTime);
    Serial.println("us");
  }
  else if (targetCurrent_mA == 0)
  {
    analogWrite(dacPin, 0);
  }
}

bool initializeINA226()
{
  Serial.println("Initializing INA226...");
  if (!ina226.begin()) return false;

  ina226.setAverage(0x01);
  ina226.setBusVoltageConversionTime(0x04);
  ina226.setShuntVoltageConversionTime(0x04);
  ina226.setMode(0x07);

  if (!calculateCalibration()) return false;

  Serial.println("INA226 ready");
  Serial.println("LSB: " + String(ina226.getCurrentLSB_mA(), 6) + " mA");
  return true;
}

bool calculateCalibration()
{
  Serial.println("Calibrating...");
  int result = ina226.setMaxCurrentShunt(maxCurrent_mA / 1000.0, shuntResistance);
  
  if (result == INA226_ERR_NONE)
  {
    Serial.println("Calibration OK");
    return true;
  }
  else
  {
    Serial.println("Calibration failed");
    return false;
  }
}
