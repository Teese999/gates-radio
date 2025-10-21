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

# Сборка React приложения
echo "⚛️  Сборка React приложения..."
cd smart-gate-frontend
npm run build
if [ $? -ne 0 ]; then
    echo "❌ Ошибка сборки React!"
    exit 1
fi
echo "✓ React приложение собрано"
echo ""

# Копирование в data
echo "📦 Копирование файлов в data/..."
cd ..
rm -rf data/*
cp -r smart-gate-frontend/build/* data/
rm -f data/asset-manifest.json data/robots.txt data/manifest.json data/favicon.ico data/logo*.png
rm -f data/static/css/*.map data/static/js/*.map data/static/js/*.LICENSE.txt
echo "✓ Файлы скопированы"
echo ""

# Сборка прошивки ESP32
echo "🔨 Сборка прошивки ESP32..."
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
    echo "❌ Ошибка загрузки прошивки!"
    exit 1
fi
echo "✓ Прошивка загружена"
echo ""

# Загрузка файлов в SPIFFS
echo "📁 Загрузка файлов в SPIFFS..."
platformio run --target uploadfs

if [ $? -ne 0 ]; then
    echo "❌ Ошибка загрузки файлов!"
    exit 1
fi

echo ""
echo "=================================================="
echo "  ✅ Загрузка завершена!"
echo "=================================================="
echo ""
echo "📱 Подключение:"
echo "  WiFi: SmartGate-Config"
echo "  Пароль: 12345678"
echo "  Адрес: http://smartgate.local"
echo "  Или: http://192.168.4.1"
echo ""
echo "📊 Для просмотра логов:"
echo "  platformio device monitor --port $ESP32_PORT --baud 115200"
echo ""
