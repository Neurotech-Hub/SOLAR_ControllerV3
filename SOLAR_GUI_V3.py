#!/usr/bin/env python3
"""
SOLAR Controller V3 GUI
A Python GUI application for controlling ItsyBitsy M4 boards with LED arrays and servo motors.
"""

__version__ = "3.0.5"

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox, filedialog
import serial
import serial.tools.list_ports
import threading
import time
from datetime import datetime
import queue


class SolarController:
    def __init__(self, root):
        self.root = root
        self.root.title("SOLAR Controller V3 GUI")
        self.root.geometry("1200x835")
        
        # Serial connection
        self.serial_port = None
        self.is_connected = False
        self.read_thread = None
        self.running = False
        
        # Queue for thread-safe GUI updates
        self.message_queue = queue.Queue()
        
        # GUI variables
        self.port_var = tk.StringVar()
        self.baud_var = tk.StringVar(value="115200")
        self.auto_connect_var = tk.BooleanVar(value=True)
        
        # System status variables
        self.total_devices = tk.IntVar(value=0)
        self.system_state = tk.StringVar(value="Unknown")
        self.connection_status = tk.StringVar(value="Disconnected")
        
        # Servo control variables
        self.servo_target_mode = tk.StringVar(value="all")
        self.servo_device_var = tk.StringVar(value="000 - All")
        self.servo_angle_var = tk.IntVar(value=90)
        
        # LED control variables
        self.led_device_var = tk.StringVar(value="001 - Device 1")
        self.group_total_var = tk.IntVar(value=1)
        self.group_id_var = tk.StringVar(value="1")
        self.current_var = tk.IntVar(value=0)
        self.exposure_var = tk.IntVar(value=10)
        self.frame_count_var = tk.IntVar(value=10)
        self.interframe_delay_var = tk.IntVar(value=10)
        
        # Communication log variables
        self.debug_filter_var = tk.BooleanVar(value=True)  # True = hide DEBUG
        self.log_buffer = []  # Store all messages
        
        # Build GUI
        self.create_widgets()
        
        # Start queue processing
        self.process_queue()
        
        # Auto-connect if enabled
        if self.auto_connect_var.get():
            self.root.after(500, self.try_auto_connect)
    
    def create_widgets(self):
        """Create all GUI widgets"""
        # Main container with padding
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(0, weight=1)
        main_frame.rowconfigure(0, weight=1)
        
        # Horizontal paned window for resizable split between controls and log
        paned = ttk.PanedWindow(main_frame, orient=tk.HORIZONTAL)
        paned.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Left side container for controls
        left_frame = ttk.Frame(paned)
        left_frame.columnconfigure(0, weight=1)
        paned.add(left_frame, weight=3)
        
        # Create control sections on left side
        row = 0
        self.create_serial_section(left_frame, row)
        row += 1
        
        self.create_system_status_section(left_frame, row)
        row += 1
        
        self.create_servo_section(left_frame, row)
        row += 1
        
        self.create_led_control_section(left_frame, row)
        row += 1
        
        # Right side - Communication Log
        right_frame = ttk.Frame(paned)
        right_frame.columnconfigure(0, weight=1)
        right_frame.rowconfigure(0, weight=1)
        paned.add(right_frame, weight=1)
        self.create_communication_log_section(right_frame)
    
    def create_serial_section(self, parent, row):
        """Create Serial Connection section"""
        frame = ttk.LabelFrame(parent, text="Serial Connection", padding="10")
        frame.grid(row=row, column=0, sticky=(tk.W, tk.E), pady=5)
        frame.columnconfigure(1, weight=1)
        
        # Row 1: Port selection and Refresh button
        ttk.Label(frame, text="Port:").grid(row=0, column=0, sticky=tk.W, padx=5, pady=2)
        self.port_combo = ttk.Combobox(frame, textvariable=self.port_var, width=20)
        self.port_combo.grid(row=0, column=1, sticky=(tk.W, tk.E), padx=5, pady=2)
        
        ttk.Button(frame, text="Refresh", command=self.refresh_ports).grid(
            row=0, column=2, padx=5, pady=2)
        
        # Row 2: Baud rate and Connect button
        ttk.Label(frame, text="Baud:").grid(row=1, column=0, sticky=tk.W, padx=5, pady=2)
        baud_combo = ttk.Combobox(frame, textvariable=self.baud_var, 
                                   values=["9600", "115200"], width=20, state="readonly")
        baud_combo.grid(row=1, column=1, sticky=(tk.W, tk.E), padx=5, pady=2)
        
        self.connect_button = ttk.Button(frame, text="Connect", 
                                        command=self.toggle_connection)
        self.connect_button.grid(row=1, column=2, padx=5, pady=2)
        
        # Row 3: Auto-connect checkbox and Connection status
        ttk.Checkbutton(frame, text="Auto-connect", 
                       variable=self.auto_connect_var).grid(row=2, column=0, columnspan=2, 
                                                            sticky=tk.W, padx=5, pady=2)
        
        self.connection_label = ttk.Label(frame, textvariable=self.connection_status,
                                         foreground="red", font=("Arial", 10, "bold"))
        self.connection_label.grid(row=2, column=2, padx=5, pady=2)
        
        # Initial port refresh
        self.refresh_ports()
    
    def create_system_status_section(self, parent, row):
        """Create System Status section"""
        frame = ttk.LabelFrame(parent, text="System Status", padding="10")
        frame.grid(row=row, column=0, sticky=(tk.W, tk.E), pady=5)
        
        # Status display
        status_frame = ttk.Frame(frame)
        status_frame.grid(row=0, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(status_frame, text="Connection:").grid(row=0, column=0, sticky=tk.W, padx=5)
        self.conn_status_label = ttk.Label(status_frame, text="●", 
                                           foreground="red", font=("Arial", 14))
        self.conn_status_label.grid(row=0, column=1, padx=5)
        
        ttk.Label(status_frame, text="Total Devices:").grid(row=0, column=2, sticky=tk.W, padx=5)
        ttk.Label(status_frame, textvariable=self.total_devices).grid(row=0, column=3, padx=5)
        
        ttk.Label(status_frame, text="State:").grid(row=0, column=4, sticky=tk.W, padx=5)
        ttk.Label(status_frame, textvariable=self.system_state).grid(row=0, column=5, padx=5)
        
        # Control buttons
        button_frame = ttk.Frame(frame)
        button_frame.grid(row=1, column=0, columnspan=3, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Button(button_frame, text="Device Status", 
                  command=self.send_status_command).grid(row=0, column=0, padx=5)
        ttk.Button(button_frame, text="Re-initialize", 
                  command=self.send_reinit_command).grid(row=0, column=1, padx=5)
        ttk.Button(button_frame, text="Emergency", 
                  command=self.send_emergency_command).grid(row=0, column=2, padx=5)
    
    def create_servo_section(self, parent, row):
        """Create Servo Control section"""
        frame = ttk.LabelFrame(parent, text="Servo Control (60-120°)", padding="10")
        frame.grid(row=row, column=0, sticky=(tk.W, tk.E), pady=5)
        frame.columnconfigure(1, weight=1)
        
        # Target mode selection
        mode_frame = ttk.Frame(frame)
        mode_frame.grid(row=0, column=0, columnspan=4, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(mode_frame, text="Target:").grid(row=0, column=0, padx=5)
        ttk.Radiobutton(mode_frame, text="All Servos", variable=self.servo_target_mode,
                       value="all", command=self.update_servo_device_state).grid(
                           row=0, column=1, padx=5)
        ttk.Radiobutton(mode_frame, text="Individual", variable=self.servo_target_mode,
                       value="individual", command=self.update_servo_device_state).grid(
                           row=0, column=2, padx=5)
        
        # Device selection
        ttk.Label(mode_frame, text="Device:").grid(row=0, column=3, padx=5)
        self.servo_device_combo = ttk.Combobox(mode_frame, textvariable=self.servo_device_var,
                                              width=15, state="disabled")
        self.servo_device_combo.grid(row=0, column=4, padx=5)
        self.update_servo_device_list()
        
        # Angle control
        control_frame = ttk.Frame(frame)
        control_frame.grid(row=1, column=0, columnspan=4, sticky=(tk.W, tk.E), pady=5)
        control_frame.columnconfigure(1, weight=1)
        
        ttk.Label(control_frame, text="Angle:").grid(row=0, column=0, padx=5)
        
        # Slider
        self.servo_slider = ttk.Scale(control_frame, from_=60, to=120, 
                                     variable=self.servo_angle_var, orient=tk.HORIZONTAL, length=200, command=lambda value: self.servo_angle_var.set(round(float(value))))
        self.servo_slider.grid(row=0, column=1, sticky=(tk.W, tk.E), padx=5, pady=5)
        
        # Spinbox
        servo_spin = ttk.Spinbox(control_frame, from_=60, to=120, 
                                textvariable=self.servo_angle_var, width=10)
        servo_spin.grid(row=0, column=2, padx=5)
        
        # Preset buttons
        preset_frame = ttk.Frame(frame)
        preset_frame.grid(row=2, column=0, columnspan=4, pady=5)
        
        presets = [60, 75, 90, 105, 120]
        for i, angle in enumerate(presets):
            ttk.Button(preset_frame, text=f"{angle}°", width=8,
                      command=lambda a=angle: self.servo_angle_var.set(a)).grid(
                          row=0, column=i, padx=2)
        
        # Set Servo button
        ttk.Button(frame, text="Set Servo", command=self.send_servo_command).grid(
            row=3, column=0, columnspan=4, pady=5)
    
    def create_led_control_section(self, parent, row):
        """Create LED Control section"""
        frame = ttk.LabelFrame(parent, text="LED Control", padding="10")
        frame.grid(row=row, column=0, sticky=(tk.W, tk.E), pady=5)
        frame.columnconfigure(1, weight=1)
        
        # Group configuration (Row 1)
        group_frame = ttk.LabelFrame(frame, text="Group Configuration", padding="5")
        group_frame.grid(row=0, column=0, columnspan=4, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(group_frame, text="Group Total:").grid(row=0, column=0, padx=5, sticky=tk.W)
        self.group_total_spin = ttk.Spinbox(group_frame, from_=1, to=50,
                                           textvariable=self.group_total_var,
                                           width=10, command=self.update_group_id_list)
        self.group_total_spin.grid(row=0, column=1, padx=5, sticky=tk.W)
        self.group_total_spin.bind('<KeyRelease>', lambda e: self.update_group_id_list())
        
        # Programming parameters section
        prog_param_frame = ttk.LabelFrame(frame, text="Programming Parameters", padding="5")
        prog_param_frame.grid(row=1, column=0, columnspan=4, sticky=(tk.W, tk.E), pady=5)
        
        # Device and Group ID (Row 1 of programming parameters)
        ttk.Label(prog_param_frame, text="Device:").grid(row=0, column=0, padx=5, sticky=tk.W)
        self.led_device_combo = ttk.Combobox(prog_param_frame, textvariable=self.led_device_var, width=15)
        self.led_device_combo.grid(row=0, column=1, padx=5, sticky=tk.W)
        
        ttk.Label(prog_param_frame, text="Group ID:").grid(row=0, column=2, padx=5, sticky=tk.W)
        self.group_id_combo = ttk.Combobox(prog_param_frame, textvariable=self.group_id_var, width=10, state="readonly")
        self.group_id_combo.grid(row=0, column=3, padx=5, sticky=tk.W)
        
        # Current and Exposure (Row 2 of programming parameters)
        ttk.Label(prog_param_frame, text="Current (mA):").grid(row=1, column=0, padx=5, sticky=tk.W)
        ttk.Spinbox(prog_param_frame, from_=0, to=1500, 
                   textvariable=self.current_var, width=10).grid(row=1, column=1, padx=5, sticky=tk.W)
        
        ttk.Label(prog_param_frame, text="Exposure (ms):").grid(row=1, column=2, padx=5, sticky=tk.W)
        ttk.Spinbox(prog_param_frame, from_=10, to=1000000, 
                   textvariable=self.exposure_var, width=10).grid(row=1, column=3, padx=5, sticky=tk.W)
        
        # Initialize dropdowns
        self.update_led_device_list()
        self.update_group_id_list()
        
        # Program button
        ttk.Button(frame, text="Program", command=self.send_program_command).grid(
            row=2, column=0, columnspan=4, pady=5)
        
        # Frame configuration
        frame_config_frame = ttk.LabelFrame(frame, text="Frame Configuration", padding="5")
        frame_config_frame.grid(row=3, column=0, columnspan=4, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(frame_config_frame, text="Frame Count:").grid(row=0, column=0, padx=5, sticky=tk.W)
        ttk.Spinbox(frame_config_frame, from_=1, to=1000000, 
                   textvariable=self.frame_count_var, width=10).grid(row=0, column=1, padx=5, sticky=tk.W)
        
        ttk.Label(frame_config_frame, text="Interframe Delay (ms):").grid(row=0, column=2, padx=5, sticky=tk.W)
        ttk.Spinbox(frame_config_frame, from_=5, to=1000000, 
                   textvariable=self.interframe_delay_var, width=10).grid(row=0, column=3, padx=5, sticky=tk.W)
        
        # Frame button
        ttk.Button(frame, text="Set Frame", command=self.send_frame_command).grid(
            row=4, column=0, columnspan=4, pady=5)
        
        # Execute button (prominent)
        execute_button = ttk.Button(frame, text="Execute", command=self.send_start_command)
        execute_button.grid(row=5, column=0, columnspan=4, pady=10)
        # Make execute button more prominent
        style = ttk.Style()
        style.configure('Execute.TButton', font=('Arial', 12, 'bold'))
        execute_button.configure(style='Execute.TButton')
    
    def create_communication_log_section(self, parent):
        """Create Communication Log section"""
        frame = ttk.LabelFrame(parent, text="Communication Log", padding="10")
        frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5, padx=(5, 0))
        frame.columnconfigure(0, weight=1)
        frame.rowconfigure(1, weight=1)
        
        # Control buttons
        control_frame = ttk.Frame(frame)
        control_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Checkbutton(control_frame, text="Filter DEBUG messages", 
                       variable=self.debug_filter_var,
                       command=self.refresh_log_display).grid(row=0, column=0, padx=5)
        
        ttk.Button(control_frame, text="Clear Log", 
                  command=self.clear_log).grid(row=0, column=1, padx=5)
        ttk.Button(control_frame, text="Export Log", 
                  command=self.export_log).grid(row=0, column=2, padx=5)
        
        # Log text area
        self.log_text = scrolledtext.ScrolledText(frame, height=15, width=60, wrap=tk.WORD)
        self.log_text.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        self.log_text.config(state=tk.DISABLED)
        
        # Configure text tags for colored output
        self.log_text.tag_config("error", foreground="red")
        self.log_text.tag_config("success", foreground="green")
        self.log_text.tag_config("info", foreground="blue")
    
    # Serial Communication Methods
    
    def refresh_ports(self):
        """Refresh available serial ports"""
        ports = serial.tools.list_ports.comports()
        port_list = [port.device for port in ports]
        self.port_combo['values'] = port_list
        if port_list and not self.port_var.get():
            self.port_var.set(port_list[0])
    
    def try_auto_connect(self):
        """Try to auto-connect to first available port"""
        if not self.is_connected and self.port_var.get():
            self.toggle_connection()
    
    def toggle_connection(self):
        """Connect or disconnect serial port"""
        if not self.is_connected:
            self.connect()
        else:
            self.disconnect()
    
    def connect(self):
        """Connect to serial port"""
        port = self.port_var.get()
        baud = int(self.baud_var.get())
        
        if not port:
            messagebox.showerror("Error", "Please select a serial port")
            return
        
        try:
            self.serial_port = serial.Serial(port, baud, timeout=0.1)
            self.is_connected = True
            self.running = True
            
            # Start read thread
            self.read_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.read_thread.start()
            
            # Update UI
            self.connection_status.set("Connected")
            self.connection_label.config(foreground="green")
            self.conn_status_label.config(foreground="green")
            self.connect_button.config(text="Disconnect")
            
            self.log_message("Connected to " + port, "success")
            
            # Wait for version message and auto-request status
            self.root.after(1000, self.auto_request_status)
            
        except serial.SerialException as e:
            messagebox.showerror("Connection Error", f"Failed to connect: {str(e)}")
            self.log_message(f"Connection failed: {str(e)}", "error")
    
    def disconnect(self):
        """Disconnect from serial port"""
        self.running = False
        
        if self.read_thread:
            self.read_thread.join(timeout=1.0)
        
        if self.serial_port and self.serial_port.is_open:
            self.serial_port.close()
        
        self.is_connected = False
        self.serial_port = None
        
        # Update UI
        self.connection_status.set("Disconnected")
        self.connection_label.config(foreground="red")
        self.conn_status_label.config(foreground="red")
        self.connect_button.config(text="Connect")
        
        self.log_message("Disconnected", "info")
    
    def read_serial(self):
        """Read from serial port in background thread"""
        while self.running and self.serial_port and self.serial_port.is_open:
            try:
                if self.serial_port.in_waiting:
                    line = self.serial_port.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        self.message_queue.put(('serial', line))
            except Exception as e:
                self.message_queue.put(('error', f"Read error: {str(e)}"))
                break
            time.sleep(0.01)
    
    def send_command(self, command):
        """Send command to serial port"""
        if not self.is_connected or not self.serial_port:
            messagebox.showwarning("Not Connected", "Please connect to a device first")
            return False
        
        try:
            self.serial_port.write((command + '\n').encode('utf-8'))
            self.log_message(f">> {command}", "info")
            return True
        except Exception as e:
            self.log_message(f"Send error: {str(e)}", "error")
            return False
    
    def process_queue(self):
        """Process messages from queue (runs in main thread)"""
        try:
            while True:
                msg_type, msg = self.message_queue.get_nowait()
                
                if msg_type == 'serial':
                    self.handle_serial_message(msg)
                elif msg_type == 'error':
                    self.log_message(msg, "error")
                elif msg_type == 'log':
                    self.log_message(msg[0], msg[1] if len(msg) > 1 else None)
                    
        except queue.Empty:
            pass
        
        # Schedule next check
        self.root.after(50, self.process_queue)
    
    def handle_serial_message(self, message):
        """Handle incoming serial message"""
        self.log_message(f"<< {message}")
        
        # Parse status messages
        if message.startswith("VER:"):
            version = message.split(":", 1)[1]
            self.log_message(f"Firmware Version: {version}", "success")
        
        elif message.startswith("TOTAL:"):
            try:
                total = int(message.split(":", 1)[1])
                self.total_devices.set(total)
                self.update_servo_device_list()
                self.update_led_device_list()
            except ValueError:
                pass
        
        elif message.startswith("STATE:"):
            state = message.split(":", 1)[1]
            self.system_state.set(state)
        
        elif message.startswith("GROUP_TOTAL:"):
            try:
                group_total = int(message.split(":", 1)[1])
                self.group_total_var.set(group_total)
                self.update_group_id_list()
            except ValueError:
                pass
        
        elif message.startswith("FRAME_COUNT:"):
            try:
                frame_count = int(message.split(":", 1)[1])
                self.frame_count_var.set(frame_count)
            except ValueError:
                pass
        
        elif message.startswith("INTERFRAME_DELAY:"):
            try:
                delay = int(message.split(":", 1)[1])
                self.interframe_delay_var.set(delay)
            except ValueError:
                pass
        
        elif message.startswith("EOT"):
            self.log_message("Command completed successfully", "success")
        
        elif message.startswith("PROGRAM_ACK:"):
            self.log_message("Program execution completed", "success")
        
        elif message.startswith("FRAME_SET:"):
            self.log_message("Frame settings updated", "success")
        
        elif message.startswith("ERR:") or message.startswith("ERROR:"):
            self.log_message(message, "error")
    
    def auto_request_status(self):
        """Automatically request status after connection"""
        if self.is_connected:
            self.send_status_command()
    
    # Command Methods
    
    def send_status_command(self):
        """Send status command"""
        self.send_command("status")
    
    def send_reinit_command(self):
        """Send reinit command"""
        self.send_command("reinit")
    
    def send_emergency_command(self):
        """Send emergency shutdown command"""
        if messagebox.askyesno("Emergency", "Execute emergency shutdown?"):
            self.send_command("emergency")
            self.log_message("EMERGENCY SHUTDOWN SENT", "error")
    
    def send_servo_command(self):
        """Send servo command"""
        angle = self.servo_angle_var.get()
        
        # Validate range
        if angle < 60 or angle > 120:
            messagebox.showerror("Invalid Angle", "Servo angle must be between 60 and 120 degrees")
            return
        
        # Get device ID
        if self.servo_target_mode.get() == "all":
            device_id = "000"
        else:
            device_str = self.servo_device_var.get()
            device_id = device_str.split(" ")[0]  # Extract device ID
        
        command = f"{device_id},servo,{angle}"
        self.send_command(command)
    
    def send_program_command(self):
        """Send program command"""
        # Get device ID
        device_str = self.led_device_var.get()
        device_id = device_str.split(" ")[0]
        
        # Get parameters
        group_id = int(self.group_id_var.get())
        group_total = self.group_total_var.get()
        current = self.current_var.get()
        exposure = self.exposure_var.get()
        
        # Validate
        if current < 0 or current > 1500:
            messagebox.showerror("Invalid Current", "Current must be between 0 and 1500 mA")
            return
        
        if exposure < 10:
            messagebox.showerror("Invalid Exposure", "Exposure must be at least 10 ms")
            return
        
        if group_id < 1 or group_id > group_total:
            messagebox.showerror("Invalid Group", f"Group ID must be between 1 and {group_total}")
            return
        
        # Format command with curly brackets
        command = f"{device_id},program,{{{group_id},{group_total},{current},{exposure}}}"
        self.send_command(command)
    
    def send_frame_command(self):
        """Send frame command"""
        frame_count = self.frame_count_var.get()
        interframe_delay = self.interframe_delay_var.get()
        
        # Validate
        if frame_count < 1 or frame_count > 1000:
            messagebox.showerror("Invalid Frame Count", "Frame count must be between 1 and 1000")
            return
        
        if interframe_delay < 5:
            messagebox.showerror("Invalid Delay", "Interframe delay must be at least 5 ms")
            return
        
        command = f"frame,{frame_count},{interframe_delay}"
        self.send_command(command)
    
    def send_start_command(self):
        """Send start command"""
        if messagebox.askyesno("Execute", "Start program?"):
            self.send_command("start")
    
    # UI Update Methods
    def update_servo_device_state(self):
        """Update servo device combo state based on target mode"""
        if self.servo_target_mode.get() == "all":
            self.servo_device_combo.config(state="disabled")
            self.servo_device_var.set("000 - All")
        else:
            self.servo_device_combo.config(state="readonly")
    
    def update_servo_device_list(self):
        """Update servo device dropdown list"""
        devices = ["000 - All"]
        total = self.total_devices.get()
        
        for i in range(1, total + 1):
            devices.append(f"{i:03d} - Device {i}")
        
        self.servo_device_combo['values'] = devices
        if self.servo_target_mode.get() == "all":
            self.servo_device_var.set("000 - All")
    
    def update_led_device_list(self):
        """Update LED device dropdown list"""
        devices = []
        total = self.total_devices.get()
        
        for i in range(1, total + 1):
            devices.append(f"{i:03d} - Device {i}")
        
        self.led_device_combo['values'] = devices
        if total >= 1:
            self.led_device_var.set("001 - Device 1")
        else:
            self.led_device_var.set("")
    
    def update_group_id_list(self):
        """Update group ID dropdown based on group total"""
        try:
            group_total = self.group_total_var.get()
        except (tk.TclError, ValueError):
            return
        
        if group_total < 1:
            group_total = 1
        
        group_ids = [str(i) for i in range(1, group_total + 1)]
        self.group_id_combo['values'] = group_ids
        
        try:
            current_group_id = int(self.group_id_var.get())
            if current_group_id > group_total:
                self.group_id_var.set("1")
        except ValueError:
            self.group_id_var.set("1")

    def update_led_device_state(self):
        """Enable/disable LED device combo and group fields"""
        self.led_device_combo.config(state="readonly")
        self.group_total_spin.config(state="normal")
        self.group_id_combo.config(state="readonly")
        if self.total_devices.get() >= 1:
            self.led_device_var.set("001 - Device 1")
        else:
            self.led_device_var.set("")
    
    # Log Methods
    
    def log_message(self, message, tag=None):
        """Add message to log with timestamp"""
        timestamp = datetime.now().strftime("[%H:%M:%S]")
        full_message = f"{timestamp} {message}"
        
        # Store in buffer
        self.log_buffer.append((full_message, tag))
        
        # Apply filter and display
        if not self.should_filter_message(message):
            self.append_to_log(full_message, tag)
    
    def should_filter_message(self, message):
        """Check if message should be filtered"""
        if self.debug_filter_var.get():
            # Hide DEBUG messages
            return message.startswith("DEBUG:") or "DEBUG:" in message
        return False
    
    def append_to_log(self, message, tag=None):
        """Append message to log text widget"""
        self.log_text.config(state=tk.NORMAL)
        if tag:
            self.log_text.insert(tk.END, message + "\n", tag)
        else:
            self.log_text.insert(tk.END, message + "\n")
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def refresh_log_display(self):
        """Refresh log display based on filter settings"""
        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete(1.0, tk.END)
        
        for message, tag in self.log_buffer:
            # Extract original message without timestamp
            parts = message.split(" ", 1)
            original_msg = parts[1] if len(parts) > 1 else message
            
            if not self.should_filter_message(original_msg):
                if tag:
                    self.log_text.insert(tk.END, message + "\n", tag)
                else:
                    self.log_text.insert(tk.END, message + "\n")
        
        self.log_text.see(tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def clear_log(self):
        """Clear communication log"""
        self.log_buffer.clear()
        self.log_text.config(state=tk.NORMAL)
        self.log_text.delete(1.0, tk.END)
        self.log_text.config(state=tk.DISABLED)
    
    def export_log(self):
        """Export log to file"""
        filename = filedialog.asksaveasfilename(
            defaultextension=".txt",
            initialfile=f"solar_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt",
            filetypes=[("Text files", "*.txt"), ("All files", "*.*")]
        )
        
        if filename:
            try:
                with open(filename, 'w') as f:
                    for message, _ in self.log_buffer:
                        f.write(message + "\n")
                messagebox.showinfo("Success", f"Log exported to {filename}")
            except Exception as e:
                messagebox.showerror("Error", f"Failed to export log: {str(e)}")


def main():
    try:
        root = tk.Tk()
        print("Tkinter root window created successfully")
        print(f"Tkinter version: {tk.TkVersion}")
        
        app = SolarController(root)
        print("SolarController initialized successfully")
        
        root.mainloop()
    except Exception as e:
        print(f"ERROR: Failed to start GUI: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()

