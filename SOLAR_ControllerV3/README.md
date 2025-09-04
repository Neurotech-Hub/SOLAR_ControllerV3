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

### Device Commands

These commands follow the format `deviceId,command,value`.

| Command Syntax                                  | Description                                                                                                                                                                                                                                     |
| :---------------------------------------------- | :---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `xxx,program,{g_id,g_total,intensity}`          | Programs a device. `xxx` is the device ID. `g_id` is the group this device belongs to. `g_total` is the **total number of groups** in the system. `intensity` is the DAC value (0-2000). |
| `000,start_program,duration`                    | **Master only.** Starts a single timed pulse. `duration` is the pulse length in milliseconds (1-100). This command executes one pulse for the `current_group` and then rotates the system to the next group. |
| `xxx,servo,angle`                               | Sets the servo position. `angle` is 60-120 degrees.                                                                                                                                                                                             |
| `xxx,dac,value`                                 | Directly sets the DAC output. `value` is 0-2000. This bypasses any programmed intensity.                                                                                                                                                        |

---

## Example Usage: 4-Device, 2-Group Setup

This example walks you through setting up and running a sequential program on four devices.

**Goal:**
*   Devices #1 and #4 are in **Group 1**.
*   Devices #2 and #3 are in **Group 2**.
*   The system will flash Group 1, then Group 2, then Group 1, etc., with each pulse command.

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

Now, we will assign each device to its respective group. The crucial part is to set `group_total` to `2` for all devices, since we are only using two groups.

1.  Send the following commands one by one:
    ```
    001,program,{1,2,1500}
    002,program,{2,2,1200}
    003,program,{2,2,1200}
    004,program,{1,2,1500}
    ```
2.  You can confirm the setup by sending `status`. The master should report `CURRENT_GROUP:1` and `PROGRAM_TOTAL:2`.

### Step 3: Run the Program Sequentially

Each time you send `start_program`, the system will fire one pulse and automatically advance to the next group.

1.  **Send Pulse 1:**
    ```
    000,start_program,50
    ```
    *   **Result:** The LEDs on devices #1 and #4 (Group 1) will flash for 50ms. The master will reply `PROGRAM_ACK:true`.

2.  **Send Pulse 2:**
    ```
    000,start_program,50
    ```
    *   **Result:** The LEDs on devices #2 and #3 (Group 2) will flash for 50ms. The system has successfully rotated to the next group.

3.  **Send Pulse 3:**
    ```
    000,start_program,50
    ```
    *   **Result:** The LEDs on devices #1 and #4 (Group 1) will flash again. The system has wrapped around back to the first group.

You can continue sending the `start_program` command to cycle through the groups indefinitely.
