"""
Simplified script to integrate the show_size.py, show_partitions.py, and partition_upload.py 
functionality into PlatformIO custom targets.
"""
Import("env")

def patch_wifimanager_esp32c3():
    """
    Apply tzapu/WiFiManager PR #1865 (issue #1482) workaround to the WiFiManager
    library installed in libdeps for the current environment.

    Bug: on ESP32-S3/C3 the config portal AP is reported as started but the
    radio never broadcasts a usable SSID, so the portal times out and the
    device reboots in a loop. Fix (validated upstream): clean WiFi mode
    transitions before softAP, use channel 6, skip the preloaded async scan and
    the STA disable sequence.

    The patch is applied to the per-environment libdeps copy at script load
    time (before compilation starts) and is idempotent.
    """
    import os

    if env.subst("$PIOENV") not in ("seeed_xiao_esp32c3", "seeed_xiao_esp32s3"):
        return

    path = os.path.join(
        env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"), "WiFiManager", "WiFiManager.cpp"
    )
    if not os.path.isfile(path):
        print("[WM FIX] WiFiManager.cpp not found, skipping (run 'pio run' to install libs first)")
        return

    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        content = fh.read()

    marker = "WiFi.softAP(_apName.c_str(), (_apPassword != \"\") ? _apPassword.c_str() : nullptr, 6, _apHidden, 4);"
    if marker in content:
        print("[WM FIX] WiFiManager already patched for ESP32-S3/C3, nothing to do")
        return

    replacements = [
        # hunk 1: skip preloaded async WiFi scan on ESP32-S3/C3
        (
            "void WiFiManager::setupConfigPortal() {\n"
            "  setupHTTPServer();\n"
            "  _lastscan = 0; // reset network scan cache\n"
            "  if(_preloadwifiscan) WiFi_scanNetworks(true,true); // preload wifiscan , async\n"
            "}",
            "void WiFiManager::setupConfigPortal() {\n"
            "  setupHTTPServer();\n"
            "  _lastscan = 0; // reset network scan cache\n"
            "  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)\n"
            "  #else\n"
            "  if(_preloadwifiscan) WiFi_scanNetworks(true,true); // preload wifiscan , async\n"
            "  #endif\n"
            "}",
        ),
        # hunk 2: skip the STA disconnect/disable sequence on ESP32-S3/C3
        (
            "  // HANDLE issues with STA connections, shutdown sta if not connected, or else this will hang channel scanning and softap will not respond\n"
            "  if(_disableSTA || (!WiFi.isConnected() && _disableSTAConn)){",
            "  // HANDLE issues with STA connections, shutdown sta if not connected, or else this will hang channel scanning and softap will not respond\n"
            "  #if !defined(CONFIG_IDF_TARGET_ESP32S3) && !defined(CONFIG_IDF_TARGET_ESP32C3)\n"
            "  if(_disableSTA || (!WiFi.isConnected() && _disableSTAConn)){",
        ),
        (
            "  else {\n"
            "    // WiFi_enableSTA(true);\n"
            "  }",
            "  else {\n"
            "    // WiFi_enableSTA(true);\n"
            "  }\n"
            "  #endif",
        ),
        # hunk 3: clean mode transition + channel 6 softAP on ESP32-S3/C3
        (
            "  // start access point\n"
            "  #ifdef WM_DEBUG_LEVEL\n"
            "  DEBUG_WM(WM_DEBUG_VERBOSE,F(\"Enabling AP\"));\n"
            "  #endif\n"
            "  startAP();\n"
            "  WiFiSetCountry();",
            "  // start access point\n"
            "  #if defined(CONFIG_IDF_TARGET_ESP32S3) || defined(CONFIG_IDF_TARGET_ESP32C3)\n"
            "  WiFi.setTxPower(WIFI_POWER_11dBm);\n"
            "  WiFi.mode(WIFI_OFF);\n"
            "  delay(500);\n"
            "  WiFi.mode(WIFI_AP);\n"
            "  delay(500);\n"
            "  WiFi.softAP(_apName.c_str(), (_apPassword != \"\") ? _apPassword.c_str() : nullptr, 6, _apHidden, 4);\n"
            "  delay(500);\n"
            "  #ifdef WM_DEBUG_LEVEL\n"
            "  DEBUG_WM(WM_DEBUG_VERBOSE,F(\"Enabling AP\"));\n"
            "  DEBUG_WM(F(\"AP IP address:\"),WiFi.softAPIP());\n"
            "  #endif\n"
            "  #else\n"
            "  #ifdef WM_DEBUG_LEVEL\n"
            "  DEBUG_WM(WM_DEBUG_VERBOSE,F(\"Enabling AP\"));\n"
            "  #endif\n"
            "  startAP();\n"
            "  #endif\n"
            "  WiFiSetCountry();",
        ),
    ]

    original = content
    for old, new in replacements:
        if old in content:
            content = content.replace(old, new, 1)
        else:
            print("[WM FIX] WARNING: target text not found (WiFiManager version changed?), skipping patch")
            return

    if content == original:
        print("[WM FIX] no changes applied")
        return

    with open(path, "w", encoding="utf-8") as fh:
        fh.write(content)
    print(f"[WM FIX] WiFiManager.cpp patched for ESP32-S3/C3 (env: {env.subst('$PIOENV')})")

patch_wifimanager_esp32c3()

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
