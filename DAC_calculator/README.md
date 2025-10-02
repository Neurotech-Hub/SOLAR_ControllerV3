# DAC Calculator

This tool helps determine the optimal DAC (Digital-to-Analog Converter) value for a closed-loop current control system. It is designed to find the best DAC setting to achieve a desired current, which is crucial for applications like controlling exposure time in a sensitive system.

## Use Case

The primary goal is to find the most suitable DAC value to maintain a stable target current in a closed-loop system, balancing performance and safety.

## Commands

There are two main commands to operate the calculator:

### 1. `current,[value]`

This command helps you find the nearest DAC value for a given target current. It's a direct way to get a starting point for your DAC settings.

**Example:**
`current,1200` will run a process to find the DAC value that produces approximately 1200mA.

### 2. `dac,[initial_dac],[current]`

This command is used to fine-tune and find the optimal DAC value within a closed-loop system. You provide an initial DAC value and a target current limit. The system will then adjust the DAC to maintain the target current.

This is useful for determining the best exposure time by observing how the system settles. It is important to monitor the logs to ensure your `initial_dac` is not set too high, which could cause an overcurrent situation.

**Example:**
`dac,1800,1200` starts the DAC at 1800 and tries to maintain a current of 1200mA.

By using these commands, you can effectively determine the best DAC value for your closed-loop control system.
