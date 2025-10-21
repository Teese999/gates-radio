#!/bin/bash

# Скрипт для мониторинга Serial порта ESP32

echo "=================================================="
echo "  Serial Monitor - Умные Ворота"
echo "=================================================="
echo ""

# Поиск ESP32 порта
ESP32_PORT=$(ls /dev/cu.* 2>/dev/null | grep -E "usbserial|SLAB_USBtoUART|wchusbserial" | head -1)

if [ -z "$ESP32_PORT" ]; then
    echo "❌ ESP32 не обнаружен!"
    exit 1
fi

echo "✓ Подключение к порту: $ESP32_PORT"
echo "✓ Скорость: 115200"
echo ""
echo "Для выхода нажмите Ctrl+A затем K и подтвердите"
echo "=================================================="
echo ""

# Запуск screen
screen $ESP32_PORT 115200

