# Solar Controller Firmware

A sophisticated Arduino firmware for controlling solar panel systems using ItsyBitsy M4 microcontrollers with INA226 current monitoring and round-robin communication.

## Features

- **Round-Robin Communication**: Multi-device chain communication system
- **Current Control**: Closed-loop current control with direct DAC adjustment
- **INA226 Integration**: High-precision current and voltage monitoring
- **Servo Control**: Position control for solar panel tracking
- **Safety Features**: Overcurrent protection, voltage drop detection, and emergency shutdown
- **Chain Break Protection**: Automatic shutdown when communication is lost

## Hardware Requirements

- **Microcontroller**: Adafruit ItsyBitsy M4 Express
- **Current Sensor**: INA226 I2C current/voltage monitor
- **Shunt Resistor**: 41.95mΩ for 1.5A maximum current
- **Servo Motor**: Standard servo for panel positioning
- **Status LEDs**: System status indication

## Pin Assignments

| Pin | Function | Description |
|-----|----------|-------------|
| A0  | DAC Output | 12-bit analog output (0-4095) |
| 5   | Servo Control | Servo motor position control |
| 13  | Status LED | System status indication |
| 7   | RX Ready | Chain ready input |
| 9   | TX Ready | Chain ready output |
| 2   | User LED | User indicator |
| D1  | Serial1 TX | Chain communication |
| D0  | Serial1 RX | Chain communication |

## Installation

1. Clone this repository:
   ```bash
   git clone https://github.com/yourusername/Solar_Controller.git
   cd Solar_Controller
   ```

2. Install required Arduino libraries:
   - `Servo` (built-in)
   - `Wire` (built-in)
   - `INA226` (install via Arduino Library Manager)

3. Open `SOLAR_Controller/SOLAR_Controller.ino` in Arduino IDE

4. Select your ItsyBitsy M4 board and upload

## Usage

### Master Device Commands

The master device (connected via USB) accepts the following commands:

#### Device Control
- `xxx,servo,angle` - Set servo angle (60-120 degrees)
- `xxx,current,ma` - Set target current (0-1500mA) - closed-loop control
- `xxx,dac,value` - Set DAC value directly (0-4095) - bypasses current control

Where `xxx` is:
- `000` = all devices (broadcast)
- `001-N` = specific device ID

#### System Commands
- `help` - Show command help
- `status` - Show system status
- `reinit` - Restart device initialization
- `current` or `c` - Show current measurement
- `emergency` or `e` - Emergency shutdown

### Examples

```bash
# Set all devices to 500mA using closed-loop control
000,current,500

# Set device 1 servo to 90 degrees
001,servo,90

# Set device 2 DAC to 50% (direct control)
002,dac,2048

# Show current status
current
```

## Configuration

### Current Control
- **Control Method**: Simple increment/decrement based on target vs measured current
- **DAC Range**: 0-4095 (12-bit resolution)
- **Update Rate**: Continuous during main loop execution

### Safety Limits
- **Maximum Current**: 1500mA
- **Voltage Drop Limit**: 4.85V (triggers emergency shutdown)
- **Shunt Resistance**: 0.04195Ω

## Architecture

### Communication Protocol
The system uses a round-robin communication protocol where:
1. Master device initiates commands
2. Commands travel through the device chain
3. Each device processes commands intended for it
4. Commands return to master for completion confirmation

### Device States
- `WAITING_FOR_CHAIN` - Waiting for chain connection
- `CHAIN_READY` - Chain connected and ready
- `INIT_IN_PROGRESS` - Device initialization in progress
- `PROCESSING` - Processing a command
- `READY` - Ready for new commands

### Current Control States
- `CURRENT_OFF` - Current control disabled (target = 0)
- `CURRENT_STABLE` - At target, maintaining
- `CURRENT_ERROR` - Error condition

## Safety Features

### Automatic Protection
- **Overcurrent Protection**: Automatic shutdown at 1500mA
- **Voltage Drop Protection**: Shutdown if bus voltage drops below 4.85V
- **Chain Break Protection**: Automatic shutdown when communication is lost
- **Power-On Safety**: All devices start with DAC = 0 for safety

### Emergency Shutdown
- **Local Shutdown**: Immediately turns off local DAC output
- **Broadcast Shutdown**: Master device broadcasts DAC=0 to all devices
- **System Reset**: All control variables reset to safe state

### Safety Flow
1. **Power On** → DAC = 0, all variables reset to safe state
3. **Overcurrent** → Emergency shutdown, broadcast DAC=0 to all devices  
4. **Voltage Drop** → Emergency shutdown, broadcast DAC=0 to all devices
5. **Emergency Command** → Manual emergency shutdown

## Current Control Operation

### Closed-Loop Control (current command)
- Sets target current (0-1500mA)
- Continuously adjusts DAC value to maintain target current
- Simple algorithm: increment DAC if current < target, decrement if current > target
- Constrains DAC value between 0-4095

### Direct Control (dac command)
- Bypasses current control
- Sets DAC value directly (0-4095)
- Useful for testing or manual control

## Troubleshooting

### Common Issues
1. **Chain not connecting**: Check physical connections and rxReady/txReady pins
2. **Current not reaching target**: Check INA226 connections and shunt resistor
3. **Emergency shutdowns**: Check for overcurrent, voltage drops, or chain breaks
4. **INA226 not initializing**: Check I2C connections and address (default: 0x4A)

### Debug Commands
- Use `status` to check system state
- Use `current` to see real-time measurements
- Monitor Serial output for debug messages

## Version History

- **v1.000**: Initial release with round-robin communication and current control
- **v1.001**: Simplified current control, enhanced safety features, voltage drop protection

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Test thoroughly
5. Submit a pull request

## License

This project is licensed under the MIT License - see the LICENSE file for details.

## Support

For issues and questions, please open an issue on GitHub or contact the development team.
