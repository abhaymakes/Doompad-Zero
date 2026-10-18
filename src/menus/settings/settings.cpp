#include "settings.h"

#include "../../ui/menu_engine.h"
#include "../../ui/input.h"
#include "time/time_manager.h"
#include "time/time_manager_ui.h"

namespace
{

    Adafruit_SSD1306 *display = nullptr;

    bool invertDisplay = false;

    int brightnessLevel = 2;
    int screenTimeout = 90;
    int testValue1 = 50;
    int testValue2 = 10;

    unsigned long lastActivity = 0;
    bool displaySleeping = false;

    Menu *settingsMenu = nullptr;

    bool backRequested = false;

    enum class SettingsPage : uint8_t
    {
        Menu,
        Time
    };

    SettingsPage currentPage = SettingsPage::Menu;

    // ------------------------------------------------------------
    // Brightness options
    // ------------------------------------------------------------

    const char *brightnessChoices[] = {
        "LOW",
        "MEDIUM",
        "HIGH"};

    // ------------------------------------------------------------
    // Apply OLED settings
    // ------------------------------------------------------------

    void applySettings()
    {

        if (!display)
        {
            return;
        }

        uint8_t contrast;

        switch (brightnessLevel)
        {

        case 0:
            contrast = 0x20;
            break;

        case 1:
            contrast = 0x70;
            break;

        default:
            contrast = 0xCF;
            break;
        }

        display->ssd1306_command(SSD1306_SETCONTRAST);
        display->ssd1306_command(contrast);

        display->invertDisplay(invertDisplay);
    }

    // ------------------------------------------------------------
    // Reset
    // ------------------------------------------------------------

    void resetSettings()
    {

        brightnessLevel = 2;
        invertDisplay = false;
        screenTimeout = 90;

        displaySleeping = false;
        lastActivity = millis();

        applySettings();

        settingsMenu->draw();
    }

    // ------------------------------------------------------------
    // Settings menu
    // ------------------------------------------------------------

    void openTime()
    {
        currentPage = SettingsPage::Time;

        if (display)
        {
            TimeManagerUI::begin(*display);
        }
    }

    MenuItem settingsItems[] = {

        {"Brightness",
         MenuItemType::Choice,
         nullptr,
         nullptr,
         &brightnessLevel,
         0,
         0,
         brightnessChoices,
         3},

        {"Invert Display",
         MenuItemType::Toggle,
         nullptr,
         &invertDisplay,
         nullptr,
         0,
         0,
         nullptr,
         0},

        {"Screen Timeout",
         MenuItemType::Value,
         nullptr,
         nullptr,
         &screenTimeout,
         0,
         120,
         nullptr,
         0},
        {"Test Value 1",
         MenuItemType::Value,
         nullptr,
         nullptr,
         &testValue1,
         0,
         100,
         nullptr,
         0},

        {"Test Value 2",
         MenuItemType::Value,
         nullptr,
         nullptr,
         &testValue2,
         0,
         20,
         nullptr,
         0},

        {"Time",
         MenuItemType::Action,
         openTime,
         nullptr,
         nullptr,
         0,
         0,
         nullptr,
         0},

        {"Reset Settings",
         MenuItemType::Action,
         resetSettings,
         nullptr,
         nullptr,
         0,
         0,
         nullptr,
         0}};

    const uint8_t SETTINGS_COUNT =
        sizeof(settingsItems) / sizeof(settingsItems[0]);

}

// ------------------------------------------------------------
// Begin
// ------------------------------------------------------------

void settingsBegin(Adafruit_SSD1306 &oled)
{

    display = &oled;

    currentPage = SettingsPage::Menu;

    if (!settingsMenu)
    {
        settingsMenu = new Menu(
            *display,
            "Settings",
            settingsItems,
            SETTINGS_COUNT);
    }

    settingsMenu->resetEditMode();

    displaySleeping = false;
    lastActivity = millis();
    backRequested = false;

    applySettings();
    settingsMenu->draw();
}

// ------------------------------------------------------------
// Update
// ------------------------------------------------------------

void settingsUpdate()
{

    if (Input::menuBack())
    {
        if (currentPage == SettingsPage::Time)
        {
            currentPage = SettingsPage::Menu;
            settingsMenu->draw();
        }
        else
        {
            backRequested = true;
        }
        return;
    }

    if (currentPage == SettingsPage::Time)
    {
        if (Input::menuBack())
        {
            currentPage = SettingsPage::Menu;
            settingsMenu->draw();
            return;
        }

        TimeManagerUI::update();
        return;
    }

    if (displaySleeping)
    {

        if (Input::left() ||
            Input::right() ||
            Input::up() ||
            Input::down() ||
            Input::select())
        {

            displaySleeping = false;
            lastActivity = millis();

            applySettings();
            settingsMenu->draw();
        }

        return;
    }

    settingsMenu->update();

    applySettings();

    static int lastBrightness = brightnessLevel;
    static bool lastInvert = invertDisplay;
    static int lastTimeout = screenTimeout;

    if (brightnessLevel != lastBrightness)
    {
        lastBrightness = brightnessLevel;
        lastActivity = millis();
    }

    if (invertDisplay != lastInvert)
    {
        lastInvert = invertDisplay;
        lastActivity = millis();
    }

    if (screenTimeout != lastTimeout)
    {
        lastTimeout = screenTimeout;
        lastActivity = millis();
    }

    if (
        screenTimeout > 0 &&
        millis() - lastActivity >=
            (unsigned long)screenTimeout * 1000UL)
    {

        display->clearDisplay();
        display->display();

        displaySleeping = true;
    }
}

// ------------------------------------------------------------
// Draw
// ------------------------------------------------------------

void settingsDraw()
{

    if (!settingsMenu)
    {
        return;
    }

    if (currentPage == SettingsPage::Time) {
        TimeManager::update();
        TimeManagerUI::draw();
        return;
    }

    applySettings();

    settingsMenu->draw();

    lastActivity = millis();
}

// ------------------------------------------------------------
// Back
// ------------------------------------------------------------

bool settingsBackRequested()
{

    if (backRequested)
    {
        backRequested = false;
        return true;
    }

    return false;
}