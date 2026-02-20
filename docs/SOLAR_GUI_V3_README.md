# SOLAR Controller V3 GUI

A Python GUI application for controlling ItsyBitsy M4 boards with LED arrays and servo motors in a daisy-chained configuration.

## Features

- **Serial Connection**: Easy port selection, baud rate configuration, and auto-connect
- **System Status**: Real-time monitoring of connection status, total devices, and system state
- **Servo Control**: Precise servo positioning (60-120°) with preset buttons and individual/broadcast targeting
- **LED Control**: Group-based LED programming with current control and exposure timing
- **Frame Programming**: Configure frame sequences with adjustable counts and delays
- **Communication Log**: Real-time logging with DEBUG message filtering and export functionality

## Installation

### Requirements

- Python 3.7 or higher
- pip (Python package manager)

### Setup

1. Install required packages:
```bash
pip install -r requirements.txt
```

2. Run the GUI:
```bash
python SOLAR_GUI_V3.py
```

## Usage

### 1. Connect to Device

1. Select the serial port from the dropdown (or click "Refresh" to update the list)
2. Verify baud rate is set to 115200 (default)
3. Optionally enable "Auto-connect" for automatic connection on startup
4. Click "Connect"
5. The GUI will automatically request system status upon connection

### 2. System Status

Monitor your system's health:
- **Connection**: Green indicator when connected
- **Total Devices**: Automatically populated from device chain
- **System State**: Current state (Ready, Initializing, Processing, etc.)

Control buttons:
- **Device Status**: Request full system status
- **Re-initialize**: Restart device chain detection
- **Emergency**: Immediate shutdown of all devices

### 3. Servo Control

Control servo positions (60-120 degrees):
1. Choose target mode:
   - **All Servos**: Command all devices simultaneously
   - **Individual**: Target specific device
2. Select device (if Individual mode)
3. Set angle using slider, spinbox, or preset buttons (60°, 75°, 90°, 105°, 120°)
4. Click "Set Servo"

### 4. LED Control

#### Program Devices

1. **Select Device**: Choose device ID (000 for all, or specific device)
2. **Configure Group**:
   - **Group Total**: Total number of groups in your system (1-50)
   - **Group ID**: This device's group number (1 to Group Total)
3. **Set LED Parameters**:
   - **Current (mA)**: Target current 0-1500 mA
   - **Exposure (ms)**: LED on-time minimum 10 ms
4. Click **Program** to send configuration

#### Configure Frames

1. **Frame Count**: Number of times to repeat the sequence (1-1000)
2. **Interframe Delay**: Pause between groups in milliseconds (minimum 5)
3. Click **Set Frame** to apply settings

#### Execute

Click **EXECUTE** to start the program:
- Frame_0: Automatic DAC calibration phase
- Frame_1+: Your programmed frames with calibrated values

### 5. Communication Log

- **Hide DEBUG messages**: Toggle to filter out DEBUG messages (enabled by default)
- **Clear Log**: Clear all log contents
- **Export Log**: Save log to timestamped text file

## Command Format

The GUI sends commands to the Arduino firmware:

### Device Commands
- **Servo**: `xxx,servo,angle` (e.g., `001,servo,90`)
- **Program**: `xxx,program,{g_id,g_total,current,exp}` (e.g., `001,program,{1,2,1300,20}`)

### System Commands
- **Status**: `status`
- **Re-initialize**: `reinit`
- **Emergency**: `emergency`
- **Frame**: `frame,count,delay` (e.g., `frame,5,50`)
- **Start**: `start`

## Example Workflow

### 4-Device, 2-Group Setup

1. **Connect** to the master device
2. Wait for auto-status to populate device count (should show "4")
3. **Program devices**:
   - Device 1: Group 1, 1300mA, 30ms
   - Device 2: Group 2, 1200mA, 20ms
   - Device 3: Group 2, 1200mA, 20ms
   - Device 4: Group 1, 1300mA, 30ms
4. **Set Frame**: 5 frames, 50ms delay
5. **Execute**: Start the program

The system will:
- Run Frame_0 calibration (automatic)
- Execute 5 complete frames (Group 1 → Group 2, 5 times)
- Use 50ms delay between each group activation

## Troubleshooting

### Connection Issues
- Ensure correct serial port is selected
- Verify device is powered and USB cable is connected
- Try clicking "Refresh" to update port list
- Check that no other application is using the serial port

### Command Not Working
- Verify device count is correct (check Total Devices)
- Ensure all parameters are within valid ranges
- Check Communication Log for error messages
- Try Re-initialize to restart device chain

### No Devices Detected
- Click "Re-initialize" to restart detection
- Verify all devices are powered before connecting
- Check physical daisy-chain connections

## Safety Features

- **Input Validation**: All parameters are validated before sending
- **Emergency Shutdown**: Immediate shutdown button for safety
- **Range Enforcement**: Spinboxes enforce valid ranges:
  - Servo: 60-120°
  - Current: 0-1500 mA
  - Exposure: minimum 10 ms
  - Group Total: 1-50
  - Frame Count: 1-1000
  - Interframe Delay: minimum 5 ms

## Technical Details

- **Baud Rate**: 115200 (default, required for firmware)
- **Auto-Calibration**: Frame_0 runs automatically before user frames
- **Device Format**: 3-digit zero-padded IDs (001, 002, etc.)
- **Communication**: Thread-safe serial communication with queue-based GUI updates

## Version Compatibility

This GUI is designed for SOLAR Controller V3 firmware (v3.3.8 or later).

## License

Copyright (c) 2025 - SOLAR Controller Project

## Support

For issues or questions:
1. Check the Communication Log for error messages
2. Verify firmware version matches GUI requirements
3. Review the troubleshooting section above
4. Export log for detailed debugging

