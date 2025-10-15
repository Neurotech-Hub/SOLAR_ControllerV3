# SOLAR Controller Firmware

## Overview

This firmware is designed for the Adafruit ItsyBitsy M4, creating a high-speed, synchronized LED and servo controller system. It is ideal for applications requiring precise, coordinated control across multiple independent modules, such as in neuroscience or machine vision lighting.

The architecture moves away from traditional daisy-chained serial commands for timing control and instead implements a hardware-based **trigger chain**. A single master device sends programming commands down a serial line, but a dedicated trigger line is used to initiate actions. This allows for highly synchronized, low-latency execution of complex, multi-group lighting sequences.

**NEW in v3.3:** The system now features **automatic DAC calibration** (Frame_0), eliminating the need to manually determine optimal DAC values for each device. Simply specify your target current, and the system automatically finds the right DAC value during a 1-second calibration phase before executing programmed frames.

## Key Features

*   **Automatic DAC Calibration (Frame_0):** System automatically determines optimal DAC values for each device during a 1-second calibration phase. No manual DAC tuning required!
*   **High-Speed Pulse Control:** The master device can generate timed pulses with millisecond precision (1-100ms), enabling high-frequency operation.
*   **Active Current Control (Closeloop):** Each device uses an onboard INA226 current sensor to dynamically regulate LED output, ensuring stable and accurate current delivery during the entire exposure window.
*   **Hardware-Synchronized Execution:** All devices in the chain act on the rising and falling edges of a hardware trigger signal, ensuring near-perfect synchronization.
*   **Group-Based State Machine:** Devices can be programmed into different groups, allowing for the creation of complex sequential patterns (e.g., Group 1 fires, then Group 2, then Group 1 again).
*   **Serial Command Interface:** A simple, human-readable serial command set allows for easy control from a computer, GUI, or other microcontroller.
*   **Automatic Device Discovery:** A `reinit` command automatically discovers and assigns unique IDs to all devices in the serial chain.
*   **Enhanced Safety Features:** Consecutive overcurrent detection (requires 2 consecutive readings above threshold) prevents false positives while maintaining robust protection. Includes real-time warnings and automatic emergency shutdown.
*   **System Integrity Check:** The master device can verify the integrity of the trigger chain by checking if the signal successfully propagates through all devices and returns.


### System Commands

These commands are simple, single-word commands.

| Command     | Description                                                                                             |
| :---------- | :------------------------------------------------------------------------------------------------------ |
| `reinit`    | Starts the device discovery process. Assigns IDs (Master=1, Slave1=2, etc.) and reports the total count. |
| `status`    | Asks the master to print its current status, including device count and program variables.                 |
| `help`      | Prints a list of all available commands and examples.                                                   |
| `e` or `emergency` | Immediately shuts down all DAC outputs on all devices.                                                  |
| `start`     | Starts the pre-configured program execution based on the `program` and `frame` settings.               |

### Closeloop Configuration

The current control system uses an INA226 sensor with the following default configuration:

| Parameter | Value |
| :--- | :--- |
| **I2C Address** | `0x4A` |
| **Shunt Resistance** | `0.04195 Ω` |
| **Max Current** | `1500 mA` |
| **Conversion Time** | `140 µs` |
| **Regulation Target** | 99% of target current (safety buffer) |
| **Overcurrent Threshold** | 1515 mA (101% of max) |
| **Emergency Trigger** | 2 consecutive readings above threshold |

**Safety Note:** The system requires **two consecutive** INA226 readings above 1515mA to trigger emergency shutdown. This prevents false positives from transient spikes while maintaining robust protection. A warning is logged on the first over-threshold reading, and DAC increases are blocked until current normalizes.


### Device Commands

These commands follow the format `deviceId,command,value`.

| Command Syntax | Description |
| :--- | :--- |
| `xxx,program,{g_id,g_total,current,exp}` | Programs a device. `xxx` is device ID (or `000` for all). `g_id` is the group ID (1-based). `g_total` is the total number of unique groups. `current` is the target current in mA (0-1500). `exp` is the exposure time in ms (1-100). **Note:** DAC value is auto-calibrated during Frame_0. |
| `frame,count,delay` | **Master only.** Configures program execution frames. `count` is repetitions of the group sequence. `delay` is the pause in ms between group activations. |
| `xxx,servo,angle` | Sets the servo position. `angle` is 60-120 degrees. |
| `xxx,dac,value` | Directly sets the DAC output, bypassing the closeloop. `value` is 0-4095. |

---

## Example Usage: 4-Device, 2-Group Setup

This example walks you through setting up and running an automated, multi-frame sequence on four devices with automatic calibration.

**Goal:**
*   Devices #1 and #4 are in **Group 1**, targeting **1300mA** with a 30ms exposure.
*   Devices #2 and #3 are in **Group 2**, targeting **1200mA** with a 20ms exposure.
*   The system will automatically cycle through the sequence: Group 1 -> Group 2.
*   This entire sequence will repeat 5 times, with a 50ms delay between each group activation.
*   **Frame_0** (auto-calibration) will run first, finding optimal DAC values for all devices.

### Step 1: Initialization

Connect all 4 devices as described in the wiring instructions. Power them on and connect the master to your computer.

1.  Open the Arduino Serial Monitor.
2.  Send the command:
    ```
    reinit
    ```
3.  The system will respond with the total number of devices found.
    **Expected Output:** `INIT:TOTAL:4`

### Step 2: Program the Groups

Now, we will assign each device to its respective group, including its target current and exposure duration. Note that `group_total` is set to `2` for all devices. **No DAC values needed** - the system will auto-calibrate!

1.  Send the following commands one by one:
    ```
    001,program,{1,2,1300,30}
    002,program,{2,2,1200,20}
    003,program,{2,2,1200,20}
    004,program,{1,2,1300,30}
    ```
    *Format: `{group_id, group_total, target_current_mA, exposure_ms}`*

2.  You can confirm the setup by sending `status`.

### Step 3: Configure the Frame Execution

Set the program to run for 5 frames (meaning the full 1->2 group sequence will run 5 times) with a 50ms delay between each pulse.

1.  Send the `frame` command:
    ```
    frame,5,50
    ```
2.  The master will confirm the settings.
    **Expected Output:** `FRAME_SET:5,50`

### Step 4: Run the Program

Now, simply start the entire pre-configured sequence. The system will automatically run Frame_0 calibration first.

1.  Send the `start` command:
    ```
    start
    ```
2.  **Result:** The master device will now take control. You will see real-time logging in the serial monitor:

    **Frame_0 - Auto-Calibration Phase (~2 seconds):**
    ```
    FRAME_0: Calibration Phase Starting...
    FRAME_0: G_ID=1, I_TARGET=1300mA
    FRAME_0: G_ID=1, I=1287mA, DAC=1806, CALIBRATED
    FRAME_0: G_ID=2, I_TARGET=1200mA
    FRAME_0: G_ID=2, I=1188mA, CALIBRATED
    FRAME_0: Calibration Complete
    ```
    
    **Frame_1 through Frame_5 - User Frames:**
    *   Group 1 (Devs 1 & 4) will activate for 30ms, regulating current to 1300mA using calibrated DAC.
    *   The system will pause for 50ms.
    *   Group 2 (Devs 2 & 3) will activate for 20ms, regulating current to 1200mA using calibrated DAC.
    *   The system will pause for 50ms.
    *   This cycle will repeat for a total of 5 frames (10 total pulses).

3.  Once complete, the master will report the final status.
    **Expected Output:** `PROGRAM_ACK:true` (if the trigger signal completed the chain successfully).
