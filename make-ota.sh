#!/bin/bash
# Формирование OTA-файлов: прошивка и образ SPIFFS (веб-интерфейс).
# Результат складывается в ota/ с датой в имени — эти файлы заливаются
# через страницу «Настройки → Обновление ПО» без провода.
set -e
cd "$(dirname "$0")"

STAMP=$(date +%Y-%m-%d_%H%M)
mkdir -p ota

echo "🔨 Сборка прошивки..."
platformio run
cp .pio/build/esp32dev/firmware.bin "ota/firmware-$STAMP.bin"

echo "⚛️  Сборка веб-интерфейса..."
(cd smart-gate-frontend && npm run build)
rm -rf data/*
cp -r smart-gate-frontend/build/* data/
rm -f data/asset-manifest.json data/robots.txt data/manifest.json data/favicon.ico data/logo*.png
rm -f data/static/css/*.map data/static/js/*.map data/static/js/*.LICENSE.txt

echo "📦 Сборка образа SPIFFS..."
platformio run --target buildfs
cp .pio/build/esp32dev/spiffs.bin "ota/spiffs-$STAMP.bin"

echo ""
echo "✅ Готово:"
echo "   ota/firmware-$STAMP.bin  → «Настройки → Обновление ПО → Прошивка»"
echo "   ota/spiffs-$STAMP.bin    → «Настройки → Обновление ПО → Веб-интерфейс»"
