# Solar Controller Firmware

A sophisticated Arduino firmware for controlling solar panel systems using ItsyBitsy M4 microcontrollers with INA226 current monitoring and round-robin communication.

## Features

- **Round-Robin Communication**: Multi-device chain communication system
- **Current Control**: Closed-loop current control with PID algorithm
- **INA226 Integration**: High-precision current and voltage monitoring
- **Servo Control**: Position control for solar panel tracking
- **Safety Features**: Overcurrent protection and emergency shutdown
- **Ramping Control**: Smooth current transitions to prevent system stress

## Hardware Requirements

- **Microcontroller**: Adafruit ItsyBitsy M4 Express
- **Current Sensor**: INA226 I2C current/voltage monitor
- **Shunt Resistor**: 58mΩ for 1.4A maximum current
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
- `xxx,current,ma` - Set target current (0-1500mA)
- `xxx,dac,value` - Set DAC value directly (0-4095)

Where `xxx` is:
- `000` = all devices (broadcast)
- `001-N` = specific device ID

#### System Commands
- `help` - Show command help
- `status` - Show system status
- `reinit` - Restart device initialization
- `current` - Show current measurement
- `emergency` - Emergency shutdown

### Examples

```bash
# Set all devices to 500mA
000,current,500

# Set device 1 servo to 90 degrees
001,servo,90

# Set device 2 DAC to 50%
002,dac,2048
```

## Configuration

### Current Control Parameters
- **KP**: 0.1 (Proportional gain)
- **KI**: 0.01 (Integral gain)
- **KD**: 0.001 (Derivative gain)
- **Control Rate**: 100ms (10Hz)
- **Ramp Rate**: 100mA/second

### Safety Limits
- **Maximum Current**: 1500mA
- **Safety Shutdown**: 1350mA
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
- `CURRENT_OFF` - Current control disabled
- `CURRENT_RAMPING` - Ramping to target current
- `CURRENT_STABLE` - At target, maintaining
- `CURRENT_ERROR` - Error condition

## Safety Features

- **Overcurrent Protection**: Automatic shutdown at 1350mA
- **Measurement Timeout**: Shutdown if INA226 fails
- **Consecutive Error Detection**: Shutdown after 5 consecutive errors
- **Emergency Shutdown**: Immediate DAC shutdown and system reset

## Version History

- **v1.000**: Initial release with round-robin communication and current control

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
