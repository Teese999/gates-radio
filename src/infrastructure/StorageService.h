#ifndef STORAGE_SERVICE_H
#define STORAGE_SERVICE_H

#include <Arduino.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "domain/entities.h"

// Сервис для работы с постоянным хранилищем (NVS)
class StorageService {
public:
    static bool init();
    static void saveSystemState(const SystemState& state);
    static bool loadSystemState(SystemState& state);
    
private:
    static Preferences preferences;
};

#endif // STORAGE_SERVICE_H

