# SOLAR Controller V3 GUI - Testing Guide

This guide helps you verify that all features of the GUI are working correctly.

## Pre-Testing Setup

### Requirements
- Python 3.7+ installed
- `pyserial` package installed (`pip install -r requirements.txt`)
- At least one ItsyBitsy M4 with SOLAR_ControllerV3 firmware

### Launch GUI
```bash
python SOLAR_GUI_V3.py
```

## Test Checklist

### 1. Serial Connection Section ✓

**Test 1.1: Port Detection**
- [ ] Click "Refresh" button
- [ ] Verify available COM ports appear in dropdown
- [ ] Connect/disconnect USB cable and refresh - list should update

**Test 1.2: Connection**
- [ ] Select correct COM port
- [ ] Verify baud rate is 115200
- [ ] Click "Connect"
- [ ] Verify "Connected" status shows in green
- [ ] Verify connection indicator (●) turns green in System Status

**Test 1.3: Auto-Connect**
- [ ] Check "Auto-connect" checkbox
- [ ] Restart GUI
- [ ] Verify automatic connection attempt

**Test 1.4: Disconnection**
- [ ] Click "Disconnect" when connected
- [ ] Verify "Disconnected" status shows in red
- [ ] Verify connection indicator (●) turns red

### 2. System Status Section ✓

**Test 2.1: Auto-Population on Connect**
- [ ] Connect to device
- [ ] Wait 1 second
- [ ] Verify "Total Devices" field updates automatically
- [ ] Verify "State" field shows current state (e.g., "Ready")
- [ ] Check communication log for version message (VER:)

**Test 2.2: Device Status Button**
- [ ] Click "Device Status" button
- [ ] Verify `status` command sent (check log: `>> status`)
- [ ] Verify response received with system information
- [ ] Verify fields update: Total Devices, State, Group Total, Frame Count, Interframe Delay

**Test 2.3: Re-initialize Button**
- [ ] Click "Re-initialize" button
- [ ] Verify `reinit` command sent
- [ ] Verify device chain re-initializes (check log for INIT messages)
- [ ] Verify Total Devices updates after re-initialization

**Test 2.4: Emergency Button**
- [ ] Click "Emergency" button
- [ ] Verify confirmation dialog appears
- [ ] Click "Yes"
- [ ] Verify `emergency` command sent
- [ ] Verify emergency message in log (red text)

### 3. Servo Control Section ✓

**Test 3.1: Target Mode - All Servos**
- [ ] Select "All Servos" radio button
- [ ] Verify device dropdown is disabled
- [ ] Verify dropdown shows "000 - All"

**Test 3.2: Target Mode - Individual**
- [ ] Select "Individual" radio button
- [ ] Verify device dropdown becomes enabled
- [ ] Verify dropdown shows "000 - All" plus detected devices

**Test 3.3: Device Dropdown Update**
- [ ] Note current Total Devices value
- [ ] Send `reinit` with different device count
- [ ] Verify servo device dropdown updates with new device count

**Test 3.4: Angle Control - Slider**
- [ ] Move slider to different positions
- [ ] Verify spinbox value updates accordingly
- [ ] Verify range is limited to 60-120

**Test 3.5: Angle Control - Spinbox**
- [ ] Type value in spinbox (e.g., 75)
- [ ] Verify slider position updates
- [ ] Try invalid value (<60 or >120)
- [ ] Verify validation on send command

**Test 3.6: Preset Buttons**
- [ ] Click each preset button (60°, 75°, 90°, 105°, 120°)
- [ ] Verify both slider and spinbox update to preset value

**Test 3.7: Set Servo Command - All**
- [ ] Select "All Servos"
- [ ] Set angle to 90
- [ ] Click "Set Servo"
- [ ] Verify command sent: `>> 000,servo,90`
- [ ] Verify all servos move to 90°

**Test 3.8: Set Servo Command - Individual**
- [ ] Select "Individual"
- [ ] Choose device "001 - Device 1"
- [ ] Set angle to 120
- [ ] Click "Set Servo"
- [ ] Verify command sent: `>> 001,servo,120`
- [ ] Verify only selected servo moves

**Test 3.9: Servo Validation**
- [ ] Try to send angle outside range (modify code or test edge cases)
- [ ] Verify error message appears
- [ ] Verify command not sent

### 4. LED Control Section ✓

**Test 4.1: Device Selection**
- [ ] Verify device dropdown shows "000 - All" by default
- [ ] Verify dropdown includes all detected devices
- [ ] Select different devices
- [ ] Verify selection persists

**Test 4.2: Group Total Spinbox**
- [ ] Set group total to 1
- [ ] Verify group ID dropdown shows only "1"
- [ ] Set group total to 5
- [ ] Verify group ID dropdown shows "1", "2", "3", "4", "5"
- [ ] Verify range is limited to 1-50

**Test 4.3: Group ID Dropdown Update**
- [ ] Set group total to 3
- [ ] Set group ID to 3
- [ ] Change group total to 2
- [ ] Verify group ID resets to 1 (since 3 > 2)

**Test 4.4: LED Parameters**
- [ ] Set Current to 0 mA
- [ ] Verify accepts value
- [ ] Set Current to 1500 mA
- [ ] Verify accepts value
- [ ] Try to set Current > 1500 (if possible)
- [ ] Set Exposure to 10 ms
- [ ] Verify accepts value
- [ ] Set Exposure to 500 ms
- [ ] Verify accepts value

**Test 4.5: Program Command - All Devices**
- [ ] Select "000 - All"
- [ ] Set: Group Total=2, Group ID=1, Current=1300, Exposure=20
- [ ] Click "Program"
- [ ] Verify command sent: `>> 000,program,{1,2,1300,20}`
- [ ] Check log for EOT or acknowledgment

**Test 4.6: Program Command - Individual Device**
- [ ] Select "001 - Device 1"
- [ ] Set: Group Total=2, Group ID=1, Current=1200, Exposure=30
- [ ] Click "Program"
- [ ] Verify command sent: `>> 001,program,{1,2,1200,30}`

**Test 4.7: Program Validation**
- [ ] Try invalid current (e.g., 2000 mA)
- [ ] Verify error message
- [ ] Try invalid exposure (e.g., 9 ms)
- [ ] Verify error message
- [ ] Try invalid group ID (> group_total)
- [ ] Verify error message

**Test 4.8: Frame Configuration**
- [ ] Set Frame Count to 1
- [ ] Verify accepts value
- [ ] Set Frame Count to 1000
- [ ] Verify accepts value
- [ ] Set Interframe Delay to 0
- [ ] Verify accepts value
- [ ] Set Interframe Delay to 500
- [ ] Verify accepts value

**Test 4.9: Set Frame Command**
- [ ] Set Frame Count=5, Interframe Delay=50
- [ ] Click "Set Frame"
- [ ] Verify command sent: `>> frame,5,50`
- [ ] Check log for FRAME_SET acknowledgment

**Test 4.10: Frame Validation**
- [ ] Try frame count outside range
- [ ] Verify error message
- [ ] Try interframe delay outside range
- [ ] Verify error message

**Test 4.11: Execute Command**
- [ ] Program at least one device
- [ ] Set frame parameters
- [ ] Click "EXECUTE"
- [ ] Verify confirmation dialog appears
- [ ] Click "Yes"
- [ ] Verify `start` command sent
- [ ] Watch log for Frame_0 calibration messages
- [ ] Watch log for Frame_1+ execution messages
- [ ] Verify PROGRAM_ACK at completion

### 5. Communication Log Section ✓

**Test 5.1: Message Display**
- [ ] Send various commands
- [ ] Verify all sent commands appear with `>>`
- [ ] Verify all received messages appear with `<<`
- [ ] Verify timestamps are present `[HH:MM:SS]`

**Test 5.2: DEBUG Filter - Enabled**
- [ ] Ensure "Hide DEBUG messages" is checked
- [ ] Send status command
- [ ] Verify DEBUG messages are hidden
- [ ] Verify user-relevant messages are shown (VER, TOTAL, STATE, etc.)

**Test 5.3: DEBUG Filter - Disabled**
- [ ] Uncheck "Hide DEBUG messages"
- [ ] Verify log refreshes
- [ ] Verify DEBUG messages now appear
- [ ] Verify all messages are visible

**Test 5.4: Filter Toggle**
- [ ] Toggle DEBUG filter on/off multiple times
- [ ] Verify log updates correctly each time
- [ ] Verify no messages are lost

**Test 5.5: Clear Log**
- [ ] Generate several log messages
- [ ] Click "Clear Log"
- [ ] Verify log is completely empty
- [ ] Send new command
- [ ] Verify new messages appear

**Test 5.6: Export Log**
- [ ] Generate various log messages
- [ ] Click "Export Log"
- [ ] Verify file save dialog appears
- [ ] Verify default filename includes timestamp
- [ ] Save file
- [ ] Open exported file
- [ ] Verify all messages (including DEBUG) are present
- [ ] Verify timestamps are preserved

**Test 5.7: Color Coding**
- [ ] Send successful command
- [ ] Verify success messages in green
- [ ] Trigger error (invalid command)
- [ ] Verify error messages in red
- [ ] Send info messages
- [ ] Verify info messages in blue

### 6. Integration Tests ✓

**Test 6.1: Complete Workflow**
- [ ] Connect to device
- [ ] Wait for auto-status
- [ ] Program 2 devices with different groups
- [ ] Set frame parameters
- [ ] Execute program
- [ ] Verify complete execution with calibration
- [ ] Check log for all expected messages

**Test 6.2: Multi-Device Chain**
- [ ] Connect system with 4+ devices
- [ ] Verify correct device count detected
- [ ] Program each device individually
- [ ] Verify device dropdowns populate correctly
- [ ] Execute and verify synchronized operation

**Test 6.3: Error Recovery**
- [ ] Send invalid command format
- [ ] Verify error message in log
- [ ] Verify GUI remains responsive
- [ ] Send valid command
- [ ] Verify normal operation resumes

**Test 6.4: Disconnect During Operation**
- [ ] Start program execution
- [ ] Disconnect USB cable
- [ ] Verify GUI detects disconnection
- [ ] Reconnect
- [ ] Verify can reconnect and continue

**Test 6.5: Rapid Command Sending**
- [ ] Send multiple servo commands quickly
- [ ] Verify all commands are sent
- [ ] Verify system handles commands correctly
- [ ] Check log for all EOT messages

### 7. Edge Cases ✓

**Test 7.1: Zero Values**
- [ ] Set Current to 0 mA
- [ ] Send program command
- [ ] Verify accepted
- [ ] Set Interframe Delay to 0
- [ ] Send frame command
- [ ] Verify accepted

**Test 7.2: Maximum Values**
- [ ] Set all parameters to maximum allowed values
- [ ] Verify all accepted
- [ ] Send commands
- [ ] Verify system handles correctly

**Test 7.3: Minimum Values**
- [ ] Set all parameters to minimum allowed values
- [ ] Verify all accepted
- [ ] Send commands
- [ ] Verify system handles correctly

**Test 7.4: Boundary Values**
- [ ] Test values at boundaries (e.g., 60°, 120°, 1500mA, 100ms)
- [ ] Verify all accepted
- [ ] Test values just outside boundaries
- [ ] Verify rejections with error messages

**Test 7.5: No Devices Connected**
- [ ] Try to connect with no device attached
- [ ] Verify appropriate error message
- [ ] Verify GUI remains stable

**Test 7.6: Single Device System**
- [ ] Connect system with only 1 device
- [ ] Verify Total Devices shows 1
- [ ] Verify device dropdowns show only 000 and 001
- [ ] Test all functionality
- [ ] Verify single-device operation

## Test Results Template

```
Date: ____________
Firmware Version: 3.3.9
GUI Version: 3.0.0

Test Section | Pass | Fail | Notes
-------------|------|------|------
Serial Connection | [ ] | [ ] | 
System Status | [ ] | [ ] |
Servo Control | [ ] | [ ] |
LED Control | [ ] | [ ] |
Communication Log | [ ] | [ ] |
Integration | [ ] | [ ] |
Edge Cases | [ ] | [ ] |

Overall Result: PASS / FAIL

Issues Found:
1. 
2. 
3. 

```

## Automated Testing Notes

For automated testing in the future, consider:
- Mock serial port for unit testing
- Command format validation tests
- GUI state machine tests
- Thread safety tests
- Performance tests with high message rates

## Known Limitations

1. GUI does not validate device IDs against actual connected devices before sending (relies on firmware validation)
2. Serial buffer may overflow with very high debug message rates (>1000 msg/s)

## Reporting Issues

When reporting issues, please include:
1. Complete test steps to reproduce
2. Expected vs actual behavior
3. Exported communication log
4. Firmware version
5. GUI version

