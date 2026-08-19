"""
Simplified script to integrate the show_size.py, show_partitions.py, and partition_upload.py 
functionality into PlatformIO custom targets.
"""
Import("env")

def show_size(source, target, env):
    """Run the show_size.py script"""
    import os
    import subprocess
    script_path = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "show_size.py")
    if os.path.exists(script_path):
        env.Execute("$PYTHONEXE " + script_path + " --env=" + env.subst("$PIOENV"))
    else:
        print(f"Error: Script not found at {script_path}")

def show_partitions(source, target, env):
    """Run the show_partitions.py script"""
    import os
    import subprocess
    script_path = os.path.join(env.subst("$PROJECT_DIR"), "scripts", "show_partitions.py")
    if os.path.exists(script_path):
        env.Execute("$PYTHONEXE " + script_path)
    else:
        print(f"Error: Script not found at {script_path}")

def upload_partition_table(source, target, env):
    """Upload just the partition table to the device"""
    import os
    import subprocess
    import serial.tools.list_ports
    
    # Get partition table binary location
    partition_bin = os.path.join(env.subst("$BUILD_DIR"), "partitions.bin")
    
    if not os.path.exists(partition_bin):
        print(f"Error: Partition table binary not found at {partition_bin}")
        print("Run 'pio run' first to build the partition table")
        return
    
    # Get upload port - properly handle auto-detection
    upload_port = env.subst("$UPLOAD_PORT")
    
    # If no port specified or auto-detection needed
    if not upload_port or upload_port == "":
        print("Auto-detecting upload port...")
        
        # Get list of connected ports
        ports = list(serial.tools.list_ports.comports())
        
        if not ports:
            print("Error: No serial ports found. Connect your device.")
            return
        
        # Look for ESP32 devices by common patterns
        esp_port = None
        for port in ports:
            port_name = port.device
            desc = port.description.lower()
            
            # Look for typical ESP32 descriptors
            if any(id_str in desc for id_str in [
                'cp210', 'ch340', 'ftdi', 'usb', 'uart', 'serial', 
                'esp32', 'esp', 'wch', 'usbserial', 'ttyusb', 'acm'
            ]):
                esp_port = port_name
                print(f"Found potential ESP32 device: {port_name} ({port.description})")
                break
        
        if esp_port:
            upload_port = esp_port
        else:
            # Fallback to first available port
            upload_port = ports[0].device
            print(f"No ESP32 device recognized. Using first available port: {upload_port}")
    
    print(f"Using port: {upload_port}")
    
    # Get esptool.py path and parameters from PlatformIO
    esptool = env.subst("$PYTHONEXE")
    esptool_path = env.subst("$UPLOADER")
    upload_speed = env.subst("$UPLOAD_SPEED")
    
    # Construct upload command as list to avoid shell escaping issues
    cmd = [
        esptool,
        esptool_path,
        "--chip", "esp32s3",
        "--port", upload_port,
        "--baud", upload_speed,
        "write_flash",
        "0x8000",  # Standard offset for partition table
        partition_bin
    ]
    
    # Run the command
    print("Uploading partition table...")
    print(f"Command: {' '.join(cmd)}")
    
    try:
        result = subprocess.run(cmd, check=True)
        print("Partition table uploaded successfully!")
    except subprocess.CalledProcessError as e:
        print(f"Error uploading partition table: {e}")
    except Exception as e:
        print(f"Unexpected error: {e}")

# Register custom targets
env.AddCustomTarget(
    name="uploadpart",
    dependencies=["buildfs"],
    actions=[upload_partition_table],
    title="Upload Partition Table",
    description="Upload only the partition table to the device"
)

env.AddCustomTarget(
    name="custom_showsize",
    dependencies=None,
    actions=[show_size],
    title="Show Firmware Size",
    description="Display detailed firmware size information"
)

env.AddCustomTarget(
    name="custom_showpart",
    dependencies=None,
    actions=[show_partitions],
    title="Show Partitions",
    description="Display partition table information"
)
