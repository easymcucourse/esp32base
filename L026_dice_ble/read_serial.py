import serial
import time

def read_serial(port='COM12', baudrate=115200):
    try:
        ser = serial.Serial(port, baudrate, timeout=1)
        print(f"Connected to {port} at {baudrate} baud.")
        while True:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    print(line, flush=True)
            time.sleep(0.01)
    except Exception as e:
        print(f"Error: {e}", flush=True)

if __name__ == "__main__":
    read_serial()
