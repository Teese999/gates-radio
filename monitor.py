#!/usr/bin/env python3
import serial
import sys

try:
    ser = serial.Serial('/dev/cu.usbserial-0001', 115200, timeout=1)
    print("=== Монитор Serial порта ESP32 ===")
    print("Нажмите Ctrl+C для выхода\n")
    
    while True:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').rstrip()
            print(line)
            sys.stdout.flush()
            
except KeyboardInterrupt:
    print("\n\nМониторинг остановлен")
    ser.close()
except Exception as e:
    print(f"Ошибка: {e}")
    sys.exit(1)

