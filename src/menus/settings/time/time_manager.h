#pragma once

#include <Arduino.h>
#include <stddef.h>

namespace TimeManager {

enum class SyncStatus : uint8_t {
    NotSynced,
    Syncing,
    Synced,
    WiFiDisconnected,
    Failed
};

void begin();
bool requestSync();
void update();

SyncStatus status();
bool isSynced();

bool formatTime(char* buffer, size_t size);
bool formatDate(char* buffer, size_t size);

} // namespace TimeManager