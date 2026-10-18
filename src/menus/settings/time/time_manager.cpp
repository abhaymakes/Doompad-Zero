#include "time_manager.h"

#include <WiFi.h>
#include <time.h>
#include <stdio.h>

namespace TimeManager {

namespace {
    constexpr time_t VALID_EPOCH = 1700000000;
    constexpr uint32_t SYNC_TIMEOUT_MS = 20000;

    SyncStatus currentStatus = SyncStatus::NotSynced;
    uint32_t syncStartedAt = 0;
}

void begin()
{
    // Preserve the current system clock if it was already synchronized.
    if (time(nullptr) >= VALID_EPOCH) {
        currentStatus = SyncStatus::Synced;
    }
}

bool requestSync()
{
    if (WiFi.status() != WL_CONNECTED) {
        currentStatus = SyncStatus::WiFiDisconnected;
        return false;
    }

    // India Standard Time: UTC+05:30, no daylight-saving time.
    configTzTime(
        "IST-5:30",
        "pool.ntp.org",
        "time.google.com"
    );

    syncStartedAt = millis();
    currentStatus = SyncStatus::Syncing;
    return true;
}

void update()
{
    if (currentStatus != SyncStatus::Syncing) {
        return;
    }

    if (time(nullptr) >= VALID_EPOCH) {
        currentStatus = SyncStatus::Synced;
        return;
    }

    if (millis() - syncStartedAt >= SYNC_TIMEOUT_MS) {
        currentStatus = SyncStatus::Failed;
    }
}

SyncStatus status()
{
    // Keep the status accurate if another part of the firmware
    // has already synchronized the system clock.
    if (time(nullptr) >= VALID_EPOCH) {
        if (currentStatus != SyncStatus::Syncing) {
            currentStatus = SyncStatus::Synced;
        }
    }

    return currentStatus;
}

bool isSynced()
{
    return time(nullptr) >= VALID_EPOCH;
}

bool formatTime(char* buffer, size_t size)
{
    if (!buffer || size == 0) {
        return false;
    }

    const time_t now = time(nullptr);

    if (now < VALID_EPOCH) {
        snprintf(buffer, size, "--:--:--");
        return false;
    }

    struct tm localTime;
    localtime_r(&now, &localTime);

    strftime(buffer, size, "%H:%M:%S", &localTime);
    return true;
}

bool formatDate(char* buffer, size_t size)
{
    if (!buffer || size == 0) {
        return false;
    }

    const time_t now = time(nullptr);

    if (now < VALID_EPOCH) {
        snprintf(buffer, size, "-- --- ----");
        return false;
    }

    struct tm localTime;
    localtime_r(&now, &localTime);

    strftime(buffer, size, "%d %b %Y", &localTime);
    return true;
}

} // namespace TimeManager