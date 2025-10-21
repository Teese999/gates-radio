#!/bin/bash

# Скрипт для загрузки прошивки на ESP32

echo "=================================================="
echo "  Загрузка прошивки на ESP32 - Умные Ворота"
echo "=================================================="
echo ""

# Поиск ESP32 порта
echo "🔍 Поиск ESP32..."
ESP32_PORT=$(ls /dev/cu.* 2>/dev/null | grep -E "usbserial|SLAB_USBtoUART|wchusbserial" | head -1)

if [ -z "$ESP32_PORT" ]; then
    echo "❌ ESP32 не обнаружен!"
    echo ""
    echo "Доступные порты:"
    ls /dev/cu.* 2>/dev/null || echo "  Нет доступных портов"
    echo ""
    echo "Возможные причины:"
    echo "  1. ESP32 не подключен к USB"
    echo "  2. Не установлен драйвер (CH340 или CP2102)"
    echo "  3. Проблема с USB кабелем"
    echo ""
    echo "Установка драйверов:"
    echo "  CH340: brew install --cask wch-ch34x-usb-serial-driver"
    echo "  CP2102: https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers"
    exit 1
fi

echo "✓ ESP32 обнаружен на порту: $ESP32_PORT"
echo ""

# Сборка проекта
echo "🔨 Сборка проекта..."
platformio run

if [ $? -ne 0 ]; then
    echo "❌ Ошибка сборки!"
    exit 1
fi

echo "✓ Сборка успешна"
echo ""

# Загрузка прошивки
echo "📤 Загрузка прошивки на ESP32..."
platformio run --target upload --upload-port $ESP32_PORT

if [ $? -ne 0 ]; then
    echo "❌ Ошибка загрузки!"
    exit 1
fi

echo ""
echo "=================================================="
echo "  ✅ Прошивка успешно загружена!"
echo "=================================================="
echo ""
echo "Для просмотра логов выполните:"
echo "  platformio device monitor --port $ESP32_PORT --baud 115200"
echo ""



