# Exposure Time Validation Feature

## Overview
The system now enforces that all devices within the same group must have identical exposure times when the `start` command is issued. This prevents inconsistent pulse durations within a group.

## Implementation Details

### Function: `validateGroupExposures()`
- **Location**: Lines 1264-1327 in `SOLAR_ControllerV3.ino`
- **Purpose**: Validates exposure time consistency across all groups before starting execution
- **Returns**: `true` if validation passes, `false` if mismatches found

### Validation Logic

1. **Scans Device Cache**: Iterates through all programmed devices
2. **Groups Tracking**: Maintains a map of exposure times per group
3. **Comparison**: For each group, compares all device exposure times
4. **Error Reporting**: If mismatch found, reports:
   - Which group has the mismatch
   - What exposure time was expected
   - Which device has a different value
   - Lists all devices in that group with their exposure times

### Integration with Start Command
- **Location**: Lines 343-349 in `SOLAR_ControllerV3.ino`
- **Behavior**: Before starting frame execution, validates exposure times
- **On Failure**: 
  - Returns error message `ERR:EXPOSURE_MISMATCH`
  - Provides user guidance
  - Prevents execution from starting

## Usage Example

### Valid Configuration (Will Execute)
```
001,program,{1,3,1800,1300,20}  # Device 1, Group 1, 20ms exposure
002,program,{2,3,1800,1300,100} # Device 2, Group 2, 100ms exposure
003,program,{3,3,1800,1300,50}  # Device 3, Group 3, 50ms exposure
004,program,{2,3,1800,1300,100} # Device 4, Group 2, 100ms exposure ✓ Matches Dev2
005,program,{3,3,1800,1300,50}  # Device 5, Group 3, 50ms exposure ✓ Matches Dev3
start  # ✓ Will succeed
```

### Invalid Configuration (Will Be Rejected)
```
001,program,{1,3,1800,1300,20}  # Device 1, Group 1, 20ms exposure
002,program,{2,3,1800,1300,100} # Device 2, Group 2, 100ms exposure
003,program,{3,3,1800,1300,50}  # Device 3, Group 3, 50ms exposure
004,program,{2,3,1800,1300,80}  # Device 4, Group 2, 80ms exposure ✗ MISMATCH!
005,program,{3,3,1800,1300,50}  # Device 5, Group 3, 50ms exposure
start  # ✗ Will fail with error message
```

### Error Output Example
```
ERROR: Exposure mismatch in Group 2
ERROR: Expected 100ms, but device 4 has 80ms
ERROR: Devices in Group 2:
  Device 2: 100ms
  Device 4: 80ms
ERR:EXPOSURE_MISMATCH
UI:All devices in a group must have the same exposure time
UI:Check status command to see programmed values
```

## Benefits

1. **Consistency**: Ensures all devices in a group fire for the same duration
2. **Early Detection**: Catches configuration errors before execution starts
3. **Clear Feedback**: Provides detailed error messages to help fix issues
4. **Safety**: Prevents unexpected timing behavior during operation

## How to Fix Mismatches

If you receive an exposure mismatch error:

1. Check the error message to see which group has the mismatch
2. Use the `status` command to view all programmed devices
3. Reprogram the conflicting devices with matching exposure times:
   ```
   004,program,{2,3,1800,1300,100}  # Update Dev4 to match Dev2
   ```
4. Issue `start` command again

## Technical Notes

- Validation occurs only on the **master device**
- Uses the existing `deviceCache[]` array
- Checks performed at `start` command, not during `program` command
- Zero or unset group_id (group 0) devices are ignored in validation
- Requires at least one programmed device to pass validation

