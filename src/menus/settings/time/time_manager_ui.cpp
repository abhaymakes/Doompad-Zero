#include "time_manager_ui.h"
#include "time_manager.h"

#include "../../../ui/input.h"

namespace TimeManagerUI {

namespace {
    Adafruit_SSD1306* screen = nullptr;
    bool syncSelected = true;

    void drawStatus()
    {
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);
        screen->setCursor(0, 56);

        switch (TimeManager::status()) {
        case TimeManager::SyncStatus::NotSynced:
            screen->print("Not synced");
            break;

        case TimeManager::SyncStatus::Syncing:
            screen->print("Syncing...");
            break;

        case TimeManager::SyncStatus::Synced:
            screen->print("Synced - IST");
            break;

        case TimeManager::SyncStatus::WiFiDisconnected:
            screen->print("Connect to WiFi");
            break;

        case TimeManager::SyncStatus::Failed:
            screen->print("Sync failed");
            break;
        }
    }
}

void begin(Adafruit_SSD1306& display)
{
    screen = &display;
    syncSelected = true;
    TimeManager::begin();
    draw();
}

void update()
{
    if (!screen) {
        return;
    }

    TimeManager::update();

    // Only one selectable option for now.
    if (Input::up() || Input::down()) {
        syncSelected = !syncSelected;
        draw();
    }

    if (Input::select() && syncSelected) {
        TimeManager::requestSync();
        draw();
    }
}

void draw()
{
    if (!screen) {
        return;
    }

    char timeBuffer[12];
    char dateBuffer[16];

    TimeManager::formatTime(timeBuffer, sizeof(timeBuffer));
    TimeManager::formatDate(dateBuffer, sizeof(dateBuffer));

    screen->clearDisplay();
    screen->setTextColor(SSD1306_WHITE);

    // Heading
    screen->setTextSize(1);
    screen->setCursor(0, 0);
    screen->print("Time");
    screen->drawFastHLine(0, 9, 128, SSD1306_WHITE);

    // Sync action
    if (syncSelected) {
        screen->fillRect(0, 12, 128, 11, SSD1306_WHITE);
        screen->setTextColor(SSD1306_BLACK);
    }

    screen->setTextSize(1);
    screen->setCursor(4, 14);
    screen->print("Sync Time");

    screen->setTextColor(SSD1306_WHITE);

    // Live clock
    screen->setTextSize(2);
    screen->setCursor(4, 27);
    screen->print(timeBuffer);

    // Date
    screen->setTextSize(1);
    screen->setCursor(4, 46);
    screen->print(dateBuffer);

    // Synchronization state
    drawStatus();

    screen->display();
}

} // namespace TimeManagerUI