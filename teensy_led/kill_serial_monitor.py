import subprocess
Import("env")

def kill_serial_monitor(*args, **kwargs):
    """Kill Arduino IDE's serial-monitor process before upload"""
    try:
        # Find and kill any serial-monitor processes
        result = subprocess.run(
            ["pkill", "-f", "serial-monitor"],
            capture_output=True,
            text=True
        )
        if result.returncode == 0:
            print("Killed Arduino serial-monitor process")
    except Exception as e:
        pass  # Ignore errors if no process found

env.AddPreAction("upload", kill_serial_monitor)
