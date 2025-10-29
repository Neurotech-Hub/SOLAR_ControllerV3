# SOLAR Controller V3

A high-speed, synchronized LED and servo controller system for Adafruit ItsyBitsy M4 microcontrollers with automatic DAC calibration and current-based LED control.

## Project Components

### 1. SOLAR_ControllerV3 (Firmware)
Arduino firmware for ItsyBitsy M4 boards implementing:
- Hardware-based trigger chain for synchronized LED control
- Automatic DAC calibration (Frame_0)
- Active current control with INA226 sensors
- Group-based sequential pattern execution
- Daisy-chain communication protocol

**Location**: `SOLAR_ControllerV3/`  
**Documentation**: See `SOLAR_ControllerV3/README.md`

### 2. SOLAR GUI V3 (Control Interface)
Python-based graphical user interface for controlling the SOLAR system:
- Serial connection management
- Servo control (60-120°)
- LED group programming with current and exposure settings
- Frame sequencing configuration
- Real-time communication logging

**Location**: `SOLAR_GUI_V3.py`  
**Documentation**: See `SOLAR_GUI_V3_README.md`

### 3. DAC Calculator (Utility)
Standalone Arduino utility for finding optimal DAC values and testing current control.

**Location**: `DAC_calculator/`  
**Documentation**: See `DAC_calculator/README.md`

## Quick Start

### Hardware Setup
1. Flash SOLAR_ControllerV3 firmware to ItsyBitsy M4 boards
2. Connect boards in daisy-chain configuration
3. Connect INA226 current sensors to each board
4. Connect LED arrays and servo motors

### Software Setup
1. Install Python 3.7+ and dependencies:
   ```bash
   pip install -r requirements.txt
   ```

2. Run the GUI:
   ```bash
   python SOLAR_GUI_V3.py
   ```

3. Connect to the master device and start controlling!

## Key Features

### Automatic DAC Calibration
- **Frame_0**: System automatically finds optimal DAC values for each device
- No manual tuning required
- Handles thermal drift with re-calibration on each start

### Group-Based Control
- Organize devices into multiple groups
- Sequential activation with precise timing
- Independent current and exposure settings per group

### Current-Based LED Control
- Target current: 0-1500 mA with INA226 monitoring
- 99% regulation accuracy (safety margin)
- Emergency shutdown protection

### Hardware Synchronization
- Trigger chain ensures microsecond-level synchronization
- All devices respond to hardware signals simultaneously
- No serial communication delays during execution

## System Architecture

```
Master Device (USB) ←→ GUI Control
    ↓ Serial
Slave Device 1
    ↓ Serial
Slave Device 2
    ↓ Serial
Slave Device N

All devices connected via:
- Serial chain (TX/RX)
- Trigger chain (TRIGGER_IN/TRIGGER_OUT)
```

## Example Use Case

**4-Device, 2-Group LED Array**

1. Program devices into 2 groups
2. Set frame count and interframe delay
3. Execute:
   - Frame_0: Auto-calibration (100ms per group)
   - Frame_1-N: Synchronized execution with calibrated values

Result: Groups activate sequentially with precise timing and accurate current control.

## Documentation

- **Firmware**: `SOLAR_ControllerV3/README.md` - Complete firmware documentation
- **GUI**: `SOLAR_GUI_V3_README.md` - GUI user guide
- **Trigger System**: `SOLAR_ControllerV3/SHIFT.md` - Architecture details
- **Validation**: `docs/EXPOSURE_VALIDATION.md` - Exposure validation documentation

## Version Information

- **Firmware**: v3.3.8
- **GUI**: v1.0.0
- **Protocol**: Round-robin with hardware trigger chain

## Hardware Requirements

- **Microcontroller**: Adafruit ItsyBitsy M4 (SAMD51)
- **Current Sensor**: INA226 (I2C, 0x4A)
- **Shunt Resistor**: 41.95 mΩ
- **Power Supply**: 5V for LED arrays
- **Communication**: USB (master), Serial1 (chain)

## Safety Features

- Overcurrent protection (>1515mA for 2 consecutive readings)
- Voltage drop detection
- Emergency shutdown broadcasting
- Power-on safe state (DAC=0)
- Input validation in GUI

## Command Reference

### Device Commands (via GUI)
- `xxx,servo,angle` - Set servo position (60-120°)
- `xxx,program,{g_id,g_total,current,exp}` - Program device
- `frame,count,delay` - Set frame parameters
- `start` - Execute program with auto-calibration

### System Commands
- `status` - Request system status
- `reinit` - Restart device chain detection
- `emergency` - Emergency shutdown

## Troubleshooting

### Common Issues

1. **No devices detected**
   - Verify power to all devices before USB connection
   - Click "Re-initialize" in GUI
   - Check serial chain connections

2. **Calibration issues**
   - Ensure INA226 sensors are properly connected
   - Check I2C address (0x4A)
   - Verify shunt resistor value (41.95 mΩ)

3. **Synchronization problems**
   - Check trigger chain connections (TRIGGER_IN/TRIGGER_OUT)
   - Verify all devices are receiving commands (check log)
   - Re-initialize device chain

## Development

### Project Structure
```
Solar_ControllerV3/
├── SOLAR_ControllerV3/      # Arduino firmware
│   ├── SOLAR_ControllerV3.ino
│   ├── README.md
│   └── SHIFT.md
├── DAC_calculator/          # DAC utility
│   └── DAC_calculator.ino
├── docs/                    # Documentation
│   └── EXPOSURE_VALIDATION.md
├── SOLAR_GUI_V3.py          # Python GUI
├── requirements.txt         # Python dependencies
└── README.md               # This file
```

### Contributing

When making changes:
1. Update codeVersion in firmware when modifying Arduino code
2. Test with actual hardware before committing
3. Update relevant documentation
4. Verify backwards compatibility

## References

- [SOLAR Controller V2](https://github.com/Neurotech-Hub/SOLAR_ControllerV2) - Previous version
- INA226 Datasheet - Current sensor specifications
- ItsyBitsy M4 - Microcontroller documentation

## Support

For technical support:
1. Check firmware serial output for debug messages
2. Export GUI log for detailed communication history
3. Verify hardware connections and power supply
4. Review troubleshooting sections in documentation

