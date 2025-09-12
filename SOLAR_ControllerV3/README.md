# SOLAR Controller Firmware

## Overview

This firmware is designed for the Adafruit ItsyBitsy M4, creating a high-speed, synchronized LED and servo controller system. It is ideal for applications requiring precise, coordinated control across multiple independent modules, such as in neuroscience or machine vision lighting.

The architecture moves away from traditional daisy-chained serial commands for timing control and instead implements a hardware-based **trigger chain**. A single master device sends programming commands down a serial line, but a dedicated trigger line is used to initiate actions. This allows for highly synchronized, low-latency execution of complex, multi-group lighting sequences.

## Key Features

*   **High-Speed Pulse Control:** The master device can generate timed pulses with millisecond precision (1-100ms), enabling high-frequency operation.
*   **Hardware-Synchronized Execution:** All devices in the chain act on the rising and falling edges of a hardware trigger signal, ensuring near-perfect synchronization.
*   **Group-Based State Machine:** Devices can be programmed into different groups, allowing for the creation of complex sequential patterns (e.g., Group 1 fires, then Group 2, then Group 1 again).
*   **Serial Command Interface:** A simple, human-readable serial command set allows for easy control from a computer, GUI, or other microcontroller.
*   **Automatic Device Discovery:** A `reinit` command automatically discovers and assigns unique IDs to all devices in the serial chain.
*   **System Integrity Check:** The master device can verify the integrity of the trigger chain by checking if the signal successfully propagates through all devices and returns.


### System Commands

These commands are simple, single-word commands.

| Command     | Description                                                                                             |
| :---------- | :------------------------------------------------------------------------------------------------------ |
| `reinit`    | Starts the device discovery process. Assigns IDs (Master=1, Slave1=2, etc.) and reports the total count. |
| `status`    | Asks the master to print its current status, including device count and program variables.                 |
| `help`      | Prints a list of all available commands and examples.                                                   |
| `emergency` | Immediately shuts down all DAC outputs on all devices.                                                  |
| `start`     | Starts the pre-configured program execution based on the `program` and `frame` settings.               |

### Device Commands

These commands follow the format `deviceId,command,value`.

| Command Syntax | Description |
| :--- | :--- |
| `xxx,program,{g_id,g_total,intensity,duration}` | Programs a device. `xxx` is the device ID (or `000` for all). `g_id` is the group this device belongs to (1-based). `g_total` is the **total number of unique groups** in the system. `intensity` is the DAC value (0-4095) for now. `duration` is the pulse length in milliseconds (1-100ms) for this group. |
| `frame,count,interframe_delay` | **Master only.** Configures the program execution frames. `count` is the number of times the entire group sequence will be repeated. `interframe_delay` is the pause in milliseconds between each group activation. |
| `xxx,servo,angle` | Sets the servo position. `angle` is 60-120 degrees. |
| `xxx,dac,value` | Directly sets the DAC output. `value` is 0-4095. This bypasses any programmed intensity. |

---

## Example Usage: 4-Device, 2-Group Setup

This example walks you through setting up and running an automated, multi-frame sequence on four devices.

**Goal:**
*   Devices #1 and #4 are in **Group 1** with an intensity of 1700 and a 30ms pulse duration.
*   Devices #2 and #3 are in **Group 2** with an intensity of 1500 and a 20ms pulse duration.
*   The system will automatically cycle through the sequence: Group 1 -> Group 2.
*   This entire sequence will repeat 5 times, with a 50ms delay between each group activation.

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

Now, we will assign each device to its respective group, including its intensity and pulse duration. Note that `group_total` is set to `2` for all devices.

1.  Send the following commands one by one:
    ```
    001,program,{1,2,1500,30}
    002,program,{2,2,1200,20}
    003,program,{2,2,1200,20}
    004,program,{1,2,1500,30}
    ```
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

Now, simply start the entire pre-configured sequence.

1.  Send the `start` command:
    ```
    start
    ```
2.  **Result:** The master device will now take control. You will see real-time logging in the serial monitor as it executes the program.
    *   Group 1 (Devs 1 & 4) will activate for 30ms.
    *   The system will pause for 50ms.
    *   Group 2 (Devs 2 & 3) will activate for 20ms.
    *   The system will pause for 50ms.
    *   This cycle will repeat for a total of 5 frames (10 total pulses) with 50ms interframe delay.
3.  Once complete, the master will report the final status.
    **Expected Output:** `PROGRAM_ACK:true` (if the trigger signal completed the chain successfully).
