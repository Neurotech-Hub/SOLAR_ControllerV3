#!/usr/bin/env python3
"""
SOLAR GUI - Python GUI for SOLAR Controller Firmware
Compatible with ItsyBitsy M4 and INA226 Current Control

This GUI controls a daisy-chained system of ItsyBitsy M4 controllers
with INA226 current monitoring and servo control capabilities.
"""

import tkinter as tk
from tkinter import ttk, scrolledtext, messagebox
import serial
import serial.tools.list_ports
import threading
import time
import re
from datetime import datetime

class SOLARGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("SOLAR Controller v1.001")
        self.root.geometry("1000x880")
        
        # Serial communication
        self.serial_port = None
        self.is_connected = False
        self.current_status_timer = None
        
        # Device information
        self.total_devices = 0
        self.system_state = "Unknown"
        self.ina226_status = "Unknown"
        
        # Demo control
        self.demo_running = False
        self.demo_thread = None
        
        # Create GUI
        self.create_widgets()
        
        # Start periodic current status (every 60 seconds)
        # self.start_current_status_timer()
        
        # Initial auto-connect attempt (after GUI is fully loaded)
        self.root.after(1000, self.initial_auto_connect)
    
    def create_widgets(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(0, weight=3)  # Left panel gets more space
        main_frame.columnconfigure(1, weight=1)  # Right panel gets less space
        main_frame.rowconfigure(1, weight=1)
        
        # Title
        title_label = ttk.Label(main_frame, text="SOLAR Controller",
                               font=("Arial", 16, "bold"))
        title_label.grid(row=0, column=0, columnspan=2, pady=(0, 20))
        
        # Left panel (controls)
        left_panel = ttk.Frame(main_frame)
        left_panel.grid(row=1, column=0, sticky=(tk.W, tk.E, tk.N, tk.S), padx=(0, 5))
        left_panel.columnconfigure(0, weight=1)  # Make left panel expand horizontally
        
        # Right panel (log)
        right_panel = ttk.Frame(main_frame)
        right_panel.grid(row=1, column=1, sticky=(tk.W, tk.E, tk.N, tk.S))
        right_panel.columnconfigure(0, weight=1)
        right_panel.rowconfigure(0, weight=1)
        
        # Create left panel sections
        self.create_connection_section(left_panel)
        self.create_status_section(left_panel)
        self.create_servo_section(left_panel)
        self.create_current_section(left_panel)
        self.create_demo_section(left_panel)
        
        # Create right panel (log)
        self.create_log_section(right_panel)
    
    def create_connection_section(self, parent):
        conn_frame = ttk.LabelFrame(parent, text="Serial Connection", padding="10")
        conn_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        conn_frame.columnconfigure(1, weight=1)  # Make port combo expand
        conn_frame.columnconfigure(3, weight=1)  # Make baud combo expand
        
        # Port selection
        ttk.Label(conn_frame, text="Port:").grid(row=0, column=0, sticky=tk.W)
        self.port_var = tk.StringVar()
        self.port_combo = ttk.Combobox(conn_frame, textvariable=self.port_var, width=15)
        self.port_combo.grid(row=0, column=1, padx=(5, 10), sticky=(tk.W, tk.E))
        
        # Refresh button
        ttk.Button(conn_frame, text="Refresh", command=self.refresh_ports).grid(row=0, column=2, padx=(0, 10))
        
        # Baud rate
        ttk.Label(conn_frame, text="Baud:").grid(row=1, column=0, sticky=tk.W, pady=(5, 0))
        self.baud_var = tk.StringVar(value="115200")
        baud_combo = ttk.Combobox(conn_frame, textvariable=self.baud_var, 
                                 values=["9600", "19200", "38400", "57600", "115200"], width=10)
        baud_combo.grid(row=1, column=1, padx=(5, 10), pady=(5, 0), sticky=(tk.W, tk.E))
        
        # Connect button
        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=1, column=2, padx=(0, 10), pady=(5, 0))
        
        # Auto-connect checkbox
        self.auto_connect_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(conn_frame, text="Auto-connect", variable=self.auto_connect_var).grid(row=1, column=3, pady=(5, 0))
        
        # Status indicator
        self.connection_status = ttk.Label(conn_frame, text="Disconnected", foreground="red")
        self.connection_status.grid(row=2, column=0, columnspan=4, pady=(5, 0))
        
        # Initial port refresh
        self.refresh_ports()
    
    def create_status_section(self, parent):
        status_frame = ttk.LabelFrame(parent, text="System Status", padding="10")
        status_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        status_frame.columnconfigure(1, weight=1)  # Make device count expand
        status_frame.columnconfigure(3, weight=1)  # Make system state expand
        status_frame.columnconfigure(5, weight=1)  # Make INA226 status expand
        
        # Status indicators
        ttk.Label(status_frame, text="Total Devices:").grid(row=0, column=0, sticky=tk.W)
        self.device_count_label = ttk.Label(status_frame, text="0", font=("Arial", 10))
        self.device_count_label.grid(row=0, column=1, padx=(5, 20), sticky=(tk.W, tk.E))
        
        ttk.Label(status_frame, text="System State:").grid(row=0, column=2, sticky=tk.W)
        self.state_label = ttk.Label(status_frame, text="Unknown", font=("Arial", 10))
        self.state_label.grid(row=0, column=3, padx=(5, 0), sticky=(tk.W, tk.E))
        
        ttk.Label(status_frame, text="INA226:").grid(row=1, column=0, sticky=tk.W, pady=(5, 0))
        self.ina226_label = ttk.Label(status_frame, text="Unknown", font=("Arial", 10))
        self.ina226_label.grid(row=1, column=1, padx=(5, 20), pady=(5, 0), sticky=(tk.W, tk.E))
        
        # Control buttons
        ttk.Button(status_frame, text="Device Status", command=self.get_status).grid(row=2, column=0, pady=(10, 0))
        ttk.Button(status_frame, text="Re-initialize", command=self.reinitialize).grid(row=2, column=1, pady=(10, 0))
        ttk.Button(status_frame, text="Current Status", command=self.get_current_status).grid(row=2, column=2, pady=(10, 0))
        ttk.Button(status_frame, text="Emergency", command=self.emergency_shutdown).grid(row=2, column=3, pady=(10, 0))
    
    def create_servo_section(self, parent):
        servo_frame = ttk.LabelFrame(parent, text="Servo Control (60-120°)", padding="10")
        servo_frame.grid(row=2, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        servo_frame.columnconfigure(1, weight=1)  # Make angle spinbox expand
        servo_frame.columnconfigure(2, weight=2)  # Make slider expand more
        servo_frame.columnconfigure(3, weight=1)  # Make device combo expand
        
        # Target mode
        self.servo_target_var = tk.StringVar(value="all")
        ttk.Radiobutton(servo_frame, text="All Servos", variable=self.servo_target_var, 
                       value="all", command=self.update_servo_device_list).grid(row=0, column=0, sticky=tk.W)
        ttk.Radiobutton(servo_frame, text="Individual", variable=self.servo_target_var, 
                       value="individual", command=self.update_servo_device_list).grid(row=0, column=1, sticky=tk.W, padx=(20, 0))
        
        # Device selection (for individual mode)
        ttk.Label(servo_frame, text="Device:").grid(row=0, column=2, padx=(20, 5))
        self.servo_device_var = tk.StringVar(value="000")
        self.servo_device_combo = ttk.Combobox(servo_frame, textvariable=self.servo_device_var, 
                                              values=["000"], width=5, state="disabled")
        self.servo_device_combo.grid(row=0, column=3, sticky=(tk.W, tk.E))
        
        # Angle control
        ttk.Label(servo_frame, text="Angle:").grid(row=1, column=0, sticky=tk.W, pady=(10, 0))
        self.servo_angle_var = tk.IntVar(value=90)
        servo_angle_spin = ttk.Spinbox(servo_frame, from_=60, to=120, textvariable=self.servo_angle_var, width=10, increment=1)
        servo_angle_spin.grid(row=1, column=1, padx=(5, 0), pady=(10, 0), sticky=(tk.W, tk.E))
        
        # Configure servo spinbox to show integer values
        servo_angle_spin.configure(format="%.0f")
        
        # Angle slider
        self.servo_slider = tk.Scale(servo_frame, from_=60, to=120, orient=tk.HORIZONTAL, 
                                     variable=self.servo_angle_var, length=200, resolution=1)
        self.servo_slider.grid(row=1, column=2, columnspan=2, padx=(20, 0), pady=(10, 0), sticky=(tk.W, tk.E))
        
        # Preset buttons
        preset_frame = ttk.Frame(servo_frame)
        preset_frame.grid(row=2, column=0, columnspan=4, pady=(10, 0), sticky=(tk.W, tk.E))
        preset_frame.columnconfigure(0, weight=1)
        preset_frame.columnconfigure(1, weight=1)
        preset_frame.columnconfigure(2, weight=1)
        preset_frame.columnconfigure(3, weight=1)
        preset_frame.columnconfigure(4, weight=1)
        
        ttk.Button(preset_frame, text="60°", command=lambda: self.set_servo_angle(60)).grid(row=0, column=0, padx=(0, 5))
        ttk.Button(preset_frame, text="75°", command=lambda: self.set_servo_angle(75)).grid(row=0, column=1, padx=(0, 5))
        ttk.Button(preset_frame, text="90°", command=lambda: self.set_servo_angle(90)).grid(row=0, column=2, padx=(0, 5))
        ttk.Button(preset_frame, text="105°", command=lambda: self.set_servo_angle(105)).grid(row=0, column=3, padx=(0, 5))
        ttk.Button(preset_frame, text="120°", command=lambda: self.set_servo_angle(120)).grid(row=0, column=4, padx=(0, 5))
        
        # Set button
        ttk.Button(servo_frame, text="Set Servo", command=self.set_servo).grid(row=3, column=0, columnspan=4, pady=(10, 0))
    
    def create_current_section(self, parent):
        current_frame = ttk.LabelFrame(parent, text="Current Control (0-1500mA)", padding="10")
        current_frame.grid(row=3, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        current_frame.columnconfigure(1, weight=1)  # Make current spinbox expand
        current_frame.columnconfigure(2, weight=2)  # Make slider expand more
        current_frame.columnconfigure(3, weight=1)  # Make device combo expand
        
        # Target mode
        self.current_target_var = tk.StringVar(value="all")
        ttk.Radiobutton(current_frame, text="All LEDs", variable=self.current_target_var, 
                       value="all", command=self.update_current_device_list).grid(row=0, column=0, sticky=tk.W)
        ttk.Radiobutton(current_frame, text="Individual", variable=self.current_target_var, 
                       value="individual", command=self.update_current_device_list).grid(row=0, column=1, sticky=tk.W, padx=(20, 0))
        
        # Device selection (for individual mode)
        ttk.Label(current_frame, text="Device:").grid(row=0, column=2, padx=(20, 5))
        self.current_device_var = tk.StringVar(value="000")
        self.current_device_combo = ttk.Combobox(current_frame, textvariable=self.current_device_var, 
                                                values=["000"], width=5, state="disabled")
        self.current_device_combo.grid(row=0, column=3, sticky=(tk.W, tk.E))
        
        # Current control
        ttk.Label(current_frame, text="Current (mA):").grid(row=1, column=0, sticky=tk.W, pady=(10, 0))
        self.current_value_var = tk.IntVar(value=0)
        current_value_spin = ttk.Spinbox(current_frame, from_=0, to=1500, 
                                        textvariable=self.current_value_var, width=10, increment=10)
        current_value_spin.grid(row=1, column=1, padx=(5, 0), pady=(10, 0), sticky=(tk.W, tk.E))
        
        # Configure current spinbox to show integer values
        current_value_spin.configure(format="%.0f")
        
        # Current slider
        self.current_slider = tk.Scale(current_frame, from_=0, to=1500, orient=tk.HORIZONTAL, 
                                       variable=self.current_value_var, length=200, resolution=10)
        self.current_slider.grid(row=1, column=2, columnspan=2, padx=(20, 0), pady=(10, 0), sticky=(tk.W, tk.E))
        
        # Preset buttons
        preset_frame = ttk.Frame(current_frame)
        preset_frame.grid(row=2, column=0, columnspan=4, pady=(10, 0), sticky=(tk.W, tk.E))
        preset_frame.columnconfigure(0, weight=1)
        preset_frame.columnconfigure(1, weight=1)
        preset_frame.columnconfigure(2, weight=1)
        preset_frame.columnconfigure(3, weight=1)
        preset_frame.columnconfigure(4, weight=1)
        
        ttk.Button(preset_frame, text="0mA", command=lambda: self.set_current_value(0)).grid(row=0, column=0, padx=(0, 5))
        ttk.Button(preset_frame, text="375mA", command=lambda: self.set_current_value(375)).grid(row=0, column=1, padx=(0, 5))
        ttk.Button(preset_frame, text="750mA", command=lambda: self.set_current_value(750)).grid(row=0, column=2, padx=(0, 5))
        ttk.Button(preset_frame, text="1125mA", command=lambda: self.set_current_value(1125)).grid(row=0, column=3, padx=(0, 5))
        ttk.Button(preset_frame, text="1500mA", command=lambda: self.set_current_value(1500)).grid(row=0, column=4, padx=(0, 5))
        
        # Set button
        ttk.Button(current_frame, text="Set Current", command=self.set_current).grid(row=3, column=0, columnspan=4, pady=(10, 0))
    
    def create_demo_section(self, parent):
        demo_frame = ttk.LabelFrame(parent, text="Demo Patterns", padding="10")
        demo_frame.grid(row=4, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        demo_frame.columnconfigure(0, weight=1)  # Make demo frames expand
        
        # Demo 1: Servo Dance
        dance_frame = ttk.Frame(demo_frame)
        dance_frame.grid(row=0, column=0, sticky=(tk.W, tk.E), pady=(0, 5))
        dance_frame.columnconfigure(0, weight=1)  # Make label expand
        ttk.Label(dance_frame, text="Servo sweep + Current flash", font=("Arial", 9)).grid(row=0, column=0, sticky=tk.W)
        ttk.Button(dance_frame, text="🕺 Servo Dance", command=lambda: self.run_demo("dance")).grid(row=0, column=1, padx=(5, 0))
        
        # Demo 2: Servo Wave
        wave_frame = ttk.Frame(demo_frame)
        wave_frame.grid(row=1, column=0, sticky=(tk.W, tk.E), pady=(0, 5))
        wave_frame.columnconfigure(0, weight=1)  # Make label expand
        ttk.Label(wave_frame, text="Smooth servo oscillation", font=("Arial", 9)).grid(row=0, column=0, sticky=tk.W)
        ttk.Button(wave_frame, text="🌊 Servo Wave", command=lambda: self.run_demo("wave")).grid(row=0, column=1, padx=(5, 0))
        
        # Demo 3: Current Rainbow
        rainbow_frame = ttk.Frame(demo_frame)
        rainbow_frame.grid(row=2, column=0, sticky=(tk.W, tk.E), pady=(0, 10))
        rainbow_frame.columnconfigure(0, weight=1)  # Make label expand
        ttk.Label(rainbow_frame, text="Progressive brightness fade", font=("Arial", 9)).grid(row=0, column=0, sticky=tk.W)
        ttk.Button(rainbow_frame, text="🌈 Current Rainbow", command=lambda: self.run_demo("rainbow")).grid(row=0, column=1, padx=(5, 0))
        
        # Stop button
        ttk.Button(demo_frame, text="Stop Demo", command=self.stop_demo).grid(row=3, column=0, pady=(5, 0))
    
    def create_log_section(self, parent):
        log_frame = ttk.LabelFrame(parent, text="Communication Log", padding="10")
        log_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        log_frame.columnconfigure(0, weight=1)
        log_frame.rowconfigure(0, weight=1)
        
        # Log text area
        self.log_text = scrolledtext.ScrolledText(log_frame, height=30, width=50)
        self.log_text.grid(row=0, column=0, columnspan=3, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # Log control buttons
        ttk.Button(log_frame, text="Clear Log", command=self.clear_log).grid(row=1, column=0, pady=(10, 0))
        ttk.Button(log_frame, text="Export Log", command=self.export_log).grid(row=1, column=1, pady=(10, 0))
        
        # Filter checkbox
        self.filter_debug_var = tk.BooleanVar(value=True)
        ttk.Checkbutton(log_frame, text="Filter DEBUG messages", variable=self.filter_debug_var).grid(row=1, column=2, pady=(10, 0))
    
    def update_device_lists(self):
        """Update device dropdown lists based on total devices detected"""
        if self.total_devices > 0:
            device_list = ["000"] + [f"{i:03d}" for i in range(1, self.total_devices + 1)]
            
            # Update servo device list
            current_servo_device = self.servo_device_var.get()
            self.servo_device_combo['values'] = device_list
            if current_servo_device not in device_list:
                self.servo_device_var.set("000")
            
            # Update current device list
            current_current_device = self.current_device_var.get()
            self.current_device_combo['values'] = device_list
            if current_current_device not in device_list:
                self.current_device_var.set("000")
    
    def update_servo_device_list(self):
        """Update servo device list based on mode selection"""
        if self.servo_target_var.get() == "all":
            self.servo_device_var.set("000")
            self.servo_device_combo.config(state="disabled")
        else:
            self.servo_device_combo.config(state="readonly")
            if self.total_devices > 0:
                device_list = ["000"] + [f"{i:03d}" for i in range(1, self.total_devices + 1)]
                self.servo_device_combo['values'] = device_list
                if self.servo_device_var.get() not in device_list:
                    self.servo_device_var.set("001")
    
    def update_current_device_list(self):
        """Update current device list based on mode selection"""
        if self.current_target_var.get() == "all":
            self.current_device_var.set("000")
            self.current_device_combo.config(state="disabled")
        else:
            self.current_device_combo.config(state="readonly")
            if self.total_devices > 0:
                device_list = ["000"] + [f"{i:03d}" for i in range(1, self.total_devices + 1)]
                self.current_device_combo['values'] = device_list
                if self.current_device_var.get() not in device_list:
                    self.current_device_var.set("001")
    
    def initial_auto_connect(self):
        """Initial auto-connect attempt when GUI starts"""
        if self.auto_connect_var.get() and not self.is_connected:
            self.refresh_ports()
    
    def refresh_ports(self):
        """Refresh available serial ports"""
        ports = [port.device for port in serial.tools.list_ports.comports()]
        self.port_combo['values'] = ports
        if ports and self.auto_connect_var.get():
            self.port_combo.set(ports[0])
            # Auto-connect to the first available port
            if not self.is_connected:
                self.root.after(100, self.toggle_connection)  # Small delay to ensure UI is updated
    
    def toggle_connection(self):
        """Toggle serial connection"""
        if not self.is_connected:
            try:
                port = self.port_var.get()
                baud = int(self.baud_var.get())
                
                if not port:
                    messagebox.showerror("Error", "Please select a port")
                    return
                
                self.serial_port = serial.Serial(port, baud, timeout=1)
                self.is_connected = True
                self.connect_btn.config(text="Disconnect")
                self.connection_status.config(text="Connected", foreground="green")
                self.port_combo.config(state="disabled")
                
                # Start serial reading thread
                self.read_thread = threading.Thread(target=self.read_serial, daemon=True)
                self.read_thread.start()
                
                # Auto-send device status command to populate system status
                self.root.after(500, self.get_status)  # Small delay to ensure connection is stable
                
                self.log_message("🔌 Connected to " + port + " at " + str(baud) + " baud")
                
            except Exception as e:
                messagebox.showerror("Connection Error", f"Failed to connect: {str(e)}")
                self.log_message("❌ Connection failed: " + str(e))
        else:
            try:
                if self.serial_port:
                    self.serial_port.close()
                self.is_connected = False
                self.connect_btn.config(text="Connect")
                self.connection_status.config(text="Disconnected", foreground="red")
                self.port_combo.config(state="normal")
                
                # Reset status indicators
                self.device_count_label.config(text="0")
                self.state_label.config(text="Unknown")
                self.ina226_label.config(text="Unknown")
                
                self.log_message("🔌 Disconnected")
                
            except Exception as e:
                self.log_message("❌ Disconnect error: " + str(e))
    

    
    def read_serial(self):
        """Read data from serial port in background thread"""
        while self.is_connected and self.serial_port:
            try:
                if self.serial_port.in_waiting:
                    line = self.serial_port.readline().decode('utf-8').strip()
                    if line:
                        self.root.after(0, self.process_serial_data, line)
            except Exception as e:
                self.root.after(0, self.log_message, f"Serial read error: {e}")
                break
    
    def process_serial_data(self, data):
        """Process incoming serial data"""
        # Filter out DEBUG messages if filter is enabled
        if self.filter_debug_var.get() and data.startswith("DEBUG:"):
            return
        
        # Parse status information
        if data.startswith("TOTAL:"):
            self.total_devices = int(data.split(":")[1])
            self.device_count_label.config(text=str(self.total_devices))
            # Update device lists when total devices changes
            self.root.after(0, self.update_device_lists)
        elif data.startswith("STATE:"):
            self.system_state = data.split(":")[1]
            self.state_label.config(text=self.system_state)
        elif data.startswith("INA226:"):
            self.ina226_status = data.split(":")[1]
            self.ina226_label.config(text=self.ina226_status)
        elif data.startswith("EOT"):
            self.log_message("✓ EOT")
        elif data.startswith("ERR:"):
            self.log_message(f"❌ Error: {data}")
        elif data.startswith("EMERGENCY:"):
            self.log_message(f"🚨 {data}")
        elif data.startswith("==="):
            # Current status header
            self.log_message(data)
        elif data.startswith("Target Current:") or data.startswith("Measured Current:") or \
             data.startswith("Bus Voltage:") or data.startswith("Shunt Voltage:") or \
             data.startswith("Power:") or data.startswith("DAC Value:") or \
             data.startswith("DAC Voltage:") or data.startswith("Control State:"):
            # Current status details
            self.log_message(data)
        else:
            # Log other messages
            self.log_message(data)
    
    def log_message(self, message):
        """Add message to log with timestamp"""
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{timestamp}] {message}\n")
        self.log_text.see(tk.END)
    
    def send_command(self, command):
        """Send command to serial port"""
        if self.is_connected and self.serial_port:
            try:
                self.serial_port.write((command + "\n").encode())
                self.log_message(f"→ {command}")
            except Exception as e:
                self.log_message(f"❌ Send error: {e}")
        else:
            messagebox.showerror("Error", "Not connected to device")
    
    def get_status(self):
        """Get system status"""
        self.send_command("status")
    
    def reinitialize(self):
        """Reinitialize device chain"""
        self.send_command("reinit")
    
    def get_current_status(self):
        """Get current measurement status"""
        self.send_command("current")
    
    def emergency_shutdown(self):
        """Emergency shutdown"""
        if messagebox.askyesno("Emergency Shutdown", "Are you sure you want to perform emergency shutdown?"):
            self.send_command("emergency")
    
    def set_servo_angle(self, angle):
        """Set servo angle value"""
        self.servo_angle_var.set(angle)
    
    def set_current_value(self, current):
        """Set current value"""
        self.current_value_var.set(current)
    
    def set_servo(self):
        """Send servo command"""
        angle = self.servo_angle_var.get()
        if self.servo_target_var.get() == "all":
            device = "000"
        else:
            device = self.servo_device_var.get()
        
        command = f"{device},servo,{angle}"
        self.send_command(command)
    
    def set_current(self):
        """Send current command"""
        current = self.current_value_var.get()
        if self.current_target_var.get() == "all":
            device = "000"
        else:
            device = self.current_device_var.get()
        
        command = f"{device},current,{current}"
        self.send_command(command)
    
    def run_demo(self, demo_type):
        """Run demo pattern"""
        if self.demo_running:
            messagebox.showwarning("Demo Running", "Please stop the current demo first")
            return
        
        if not self.is_connected:
            messagebox.showerror("Error", "Not connected to device")
            return
        
        self.demo_running = True
        self.demo_thread = threading.Thread(target=self._run_demo_thread, args=(demo_type,), daemon=True)
        self.demo_thread.start()
        self.log_message(f"🎬 Starting {demo_type} demo...")
    
    def _run_demo_thread(self, demo_type):
        """Demo thread function"""
        try:
            if demo_type == "dance":
                self._run_servo_dance()
            elif demo_type == "wave":
                self._run_servo_wave()
            elif demo_type == "rainbow":
                self._run_current_rainbow()
        except Exception as e:
            self.root.after(0, self.log_message, f"❌ Demo error: {e}")
        finally:
            self.demo_running = False
            self.root.after(0, self.log_message, "🎬 Demo completed")
    
    def _run_servo_dance(self):
        """Servo dance demo: servo sweep + current flash"""
        for cycle in range(2):
            if not self.demo_running:
                break
            
            # Servo sweep: 60° → 120° → 90°
            self.root.after(0, self.send_command, "000,servo,60")
            time.sleep(1)
            if not self.demo_running:
                break
            
            self.root.after(0, self.send_command, "000,servo,120")
            time.sleep(1)
            if not self.demo_running:
                break
            
            self.root.after(0, self.send_command, "000,servo,90")
            time.sleep(1)
            if not self.demo_running:
                break
            
            # Current flash: 50mA → 0mA
            self.root.after(0, self.send_command, "000,current,50")
            time.sleep(3)
            if not self.demo_running:
                break
            
            self.root.after(0, self.send_command, "000,current,0")
            time.sleep(1)
    
    def _run_servo_wave(self):
        """Servo wave demo: smooth oscillation"""
        angles = [60, 75, 90, 105, 120, 105, 90, 75]
        
        for cycle in range(2):
            if not self.demo_running:
                break
            
            for angle in angles:
                if not self.demo_running:
                    break
                self.root.after(0, self.send_command, f"000,servo,{angle}")
                time.sleep(0.5)
    
    def _run_current_rainbow(self):
        """Current rainbow demo: progressive brightness fade"""
        currents = [0, 250, 500, 650, 500, 250, 0]
        
        for cycle in range(2):
            if not self.demo_running:
                break
            
            for current in currents:
                if not self.demo_running:
                    break
                self.root.after(0, self.send_command, f"000,current,{current}")
                time.sleep(5)
    
    def stop_demo(self):
        """Stop running demo"""
        if self.demo_running:
            self.demo_running = False
            self.log_message("⏹️ Demo stopped")
        else:
            self.log_message("ℹ️ No demo running")
    
    def clear_log(self):
        """Clear communication log"""
        self.log_text.delete(1.0, tk.END)
    
    def export_log(self):
        """Export log to file"""
        try:
            filename = f"solar_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.txt"
            with open(filename, 'w') as f:
                f.write(self.log_text.get(1.0, tk.END))
            messagebox.showinfo("Export", f"Log exported to {filename}")
        except Exception as e:
            messagebox.showerror("Export Error", str(e))
    
    # Current status loop
    def start_current_status_timer(self):
        """Start periodic current status timer (every 60 seconds)"""
        def periodic_current_status():
            if self.is_connected:
                self.get_current_status()
            # Schedule next execution
            self.current_status_timer = self.root.after(60000, periodic_current_status)
        
        # Start the timer
        self.current_status_timer = self.root.after(60000, periodic_current_status)
    
    def on_closing(self):
        """Handle application closing"""
        if self.demo_running:
            self.stop_demo()
        if self.is_connected:
            # Disconnect using the toggle_connection method
            self.toggle_connection()
        if self.current_status_timer:
            self.root.after_cancel(self.current_status_timer)
        self.root.destroy()

def main():
    root = tk.Tk()
    app = SOLARGUI(root)
    root.protocol("WM_DELETE_WINDOW", app.on_closing)
    root.mainloop()

if __name__ == "__main__":
    main()
