# SOLAR Controller V3 - Project Structure

## Directory Tree

```
Solar_ControllerV3/
│
├── README.md                      # Project overview and quick start
├── PROJECT_STRUCTURE.md           # This file
├── requirements.txt               # Python dependencies
├── SOLAR_GUI_V3.py               # Main GUI application
│
├── SOLAR_ControllerV3/           # Arduino firmware
│   ├── SOLAR_ControllerV3.ino   # Main firmware (v3.3.8)
│   ├── README.md                 # Firmware documentation
│   └── SHIFT.md                  # Architecture details
│
├── DAC_calculator/               # Utility tool
│   ├── DAC_calculator.ino       # DAC testing utility
│   └── README.md                 # Calculator documentation
│
└── docs/                         # Additional documentation
    ├── EXPOSURE_VALIDATION.md    # Exposure validation docs
    ├── TESTING_GUIDE.md              # Testing procedures
    └── SOLAR_GUI_V3_README.md        # GUI user guide
```

## File Descriptions

### Root Level

#### `README.md`
**Purpose**: Project overview and entry point  
**Content**:
- Project components overview
- Quick start guide
- System architecture
- Key features summary
- Links to detailed documentation

#### `PROJECT_STRUCTURE.md`
**Purpose**: Project organization reference (this file)  
**Content**:
- Directory tree
- File descriptions
- Component relationships
- Size and line count information

#### `requirements.txt`
**Purpose**: Python package dependencies  
**Content**:
```
pyserial>=3.5
```

### GUI Application Files

#### `SOLAR_GUI_V3.py`
**Purpose**: Main GUI application  
**Size**: ~735 lines  
**Language**: Python 3.7+  
**Dependencies**: pyserial, tkinter  
**Features**:
- Serial connection management
- System status monitoring
- Servo control (60-120°)
- LED group programming
- Frame configuration
- Real-time logging with filtering
- Thread-safe communication

### Firmware Directory

#### `SOLAR_ControllerV3/`
**Contents**: Arduino firmware for ItsyBitsy M4

##### `SOLAR_ControllerV3.ino`
**Size**: ~1455 lines  
**Version**: 3.3.8  
**Features**:
- Hardware trigger chain
- Automatic DAC calibration (Frame_0)
- INA226 current control
- Group-based state machine
- Round-robin communication
- Safety features

##### `README.md`
**Purpose**: Firmware documentation  
**Content**:
- Feature overview
- Command reference
- Configuration details
- Usage examples
- Wiring instructions

##### `SHIFT.md`
**Purpose**: Architecture documentation  
**Content**:
- System architecture changes
- Trigger chain implementation
- Auto-calibration system
- Implementation checklist
- Technical specifications

### Utility Directory

#### `DAC_calculator/`
**Contents**: DAC value testing utility

##### `DAC_calculator.ino`
**Purpose**: Standalone DAC testing tool  
**Features**:
- Current target finding
- DAC value optimization
- INA226 testing
- Real-time current monitoring

##### `README.md`
**Purpose**: Calculator documentation  
**Content**:
- Usage instructions
- Command reference
- Example sessions

### Documentation Directory

#### `docs/`
**Contents**: Additional documentation

##### `EXPOSURE_VALIDATION.md`
**Purpose**: Exposure validation documentation  
**Content**:
- Validation procedures
- Test results
- Calibration notes

#### `TESTING_GUIDE.md`
**Purpose**: Comprehensive testing procedures  
**Sections**:
- Pre-testing setup
- Feature-by-feature test checklists
- Integration tests
- Edge case testing
- Test results template
- Issue reporting guidelines

#### `SOLAR_GUI_V3_README.md`
**Purpose**: GUI user manual  
**Sections**:
- Installation instructions
- Usage guide for all features
- Example workflows
- Troubleshooting
- Command reference
- Safety features

## Component Relationships

```
┌─────────────────────────────────────────────────┐
│  User Interface Layer                           │
│  ┌──────────────────────────────────────────┐  │
│  │  SOLAR_GUI_V3.py                         │  │
│  │  - Serial connection                      │  │
│  │  - Command formatting                     │  │
│  │  - Status monitoring                      │  │
│  └──────────────────────────────────────────┘  │
└──────────────────┬──────────────────────────────┘
                   │ USB Serial (115200 baud)
                   ▼
┌─────────────────────────────────────────────────┐
│  Hardware Layer                                 │
│  ┌──────────────────────────────────────────┐  │
│  │  Master Device (ItsyBitsy M4)            │  │
│  │  SOLAR_ControllerV3.ino                  │  │
│  │  - Command processing                     │  │
│  │  - Trigger chain control                  │  │
│  │  - Auto-calibration (Frame_0)            │  │
│  └──────────────────────────────────────────┘  │
│         │ Serial1                                │
│         ▼                                        │
│  ┌──────────────────────────────────────────┐  │
│  │  Slave Devices (ItsyBitsy M4)            │  │
│  │  SOLAR_ControllerV3.ino                  │  │
│  │  - Command forwarding                     │  │
│  │  - Trigger propagation                    │  │
│  │  - Independent current control            │  │
│  └──────────────────────────────────────────┘  │
└─────────────────────────────────────────────────┘
```

## File Statistics

| File/Directory | Type | Lines/Size | Status |
|----------------|------|------------|--------|
| SOLAR_GUI_V3.py | Python | ~735 lines | ✅ Complete |
| SOLAR_ControllerV3.ino | Arduino | ~1455 lines | ✅ v3.3.8 |
| SOLAR_GUI_V3_README.md | Markdown | - | ✅ Complete |
| TESTING_GUIDE.md | Markdown | - | ✅ Complete |
| CHANGELOG.md | Markdown | - | ✅ Complete |
| README.md (root) | Markdown | - | ✅ Complete |
| IMPLEMENTATION_SUMMARY.md | Markdown | - | ✅ Complete |

## Version Matrix

| Component | Version | Status |
|-----------|---------|--------|
| GUI Application | 1.0.0 | ✅ Released |
| Firmware | 3.3.8 | ✅ Stable |
| Communication Protocol | V3 | ✅ Current |
| Python Requirement | 3.7+ | Required |
| Arduino IDE | 1.8.x / 2.x | Compatible |

## Development Workflow

### For GUI Development
1. Edit `SOLAR_GUI_V3.py`
2. Test with `python SOLAR_GUI_V3.py`
3. Update `SOLAR_GUI_V3_README.md` if features change
4. Add tests to `TESTING_GUIDE.md`
5. Update `CHANGELOG.md`

### For Firmware Development
1. Edit `SOLAR_ControllerV3/SOLAR_ControllerV3.ino`
2. Update `CODE_VERSION` constant
3. Flash to ItsyBitsy M4
4. Test with GUI
5. Update `SOLAR_ControllerV3/README.md`
6. Update `CHANGELOG.md`

## Installation Paths

### GUI Installation
```bash
# From project root
pip install -r requirements.txt
python SOLAR_GUI_V3.py
```

### Firmware Installation
```
1. Open SOLAR_ControllerV3/SOLAR_ControllerV3.ino in Arduino IDE
2. Select board: Adafruit ItsyBitsy M4
3. Upload to each device
```

## Documentation Hierarchy

```
README.md (Start Here)
├── Quick Start
├── Project Components
│   ├── SOLAR_ControllerV3/
│   │   └── README.md (Firmware Guide)
│   │       └── SHIFT.md (Architecture)
│   ├── SOLAR_GUI_V3.py
│   │   └── SOLAR_GUI_V3_README.md (User Guide)
│   │       └── TESTING_GUIDE.md (Testing)
│   └── DAC_calculator/
│       └── README.md (Utility Guide)
└── Additional Resources
    ├── CHANGELOG.md (Version History)
    ├── IMPLEMENTATION_SUMMARY.md (Completion Report)
    └── docs/EXPOSURE_VALIDATION.md (Validation)
```

## Quick Reference

### Running the GUI
```bash
python SOLAR_GUI_V3.py
```

### Installing Dependencies
```bash
pip install -r requirements.txt
```

### Testing the System
```bash
# Follow TESTING_GUIDE.md
# Run all tests in checklist
```

### Viewing Documentation
- **User Guide**: Open `SOLAR_GUI_V3_README.md`
- **Firmware Guide**: Open `SOLAR_ControllerV3/README.md`
- **Architecture**: Open `SOLAR_ControllerV3/SHIFT.md`
- **Testing**: Open `TESTING_GUIDE.md`

## Project Statistics

- **Total Files**: 14
- **New Files (V3 GUI)**: 7
- **Documentation Files**: 9
- **Code Files**: 3
- **Total Lines of Code**: ~2200 (GUI + Firmware)
- **Languages**: Python, C++ (Arduino), Markdown

## Support Resources

| Resource | Location |
|----------|----------|
| User Guide | SOLAR_GUI_V3_README.md |
| Testing Guide | TESTING_GUIDE.md |
| Firmware Docs | SOLAR_ControllerV3/README.md |
| Architecture | SOLAR_ControllerV3/SHIFT.md |
| Changelog | CHANGELOG.md |
| Project Overview | README.md |

---

**Last Updated**: October 29, 2025  
**Project Version**: 1.0.0  
**Status**: ✅ Complete - Ready for Testing

