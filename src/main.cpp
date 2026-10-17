
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "ui/input.h"
#include "ui/menu_engine.h"
#include "ui/menu_icons.h"

// Menus
#include "menus/settings/settings.h"
#include "menus/about/about.h"
#include "menus/wifi/wifi_ui.h"
#include "menus/wifi/wifi_manager.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_SDA 8
#define OLED_SCL 9
#define OLED_ADDR 0x3C

Adafruit_SSD1306 display(
    SCREEN_WIDTH,
    SCREEN_HEIGHT,
    &Wire,
    -1);

enum class Screen
{
    MainMenu,
    Settings,
    About,
    WiFi
};

Screen currentScreen = Screen::MainMenu;

Menu *mainMenu = nullptr;

void openSettings()
{
    currentScreen = Screen::Settings;
}

void openAbout()
{
    currentScreen = Screen::About;
}

void openWiFi()
{
    currentScreen = Screen::WiFi;
}

// Grid items use their own bitmap pointers and labels.
MenuItem mainItems[] = {
    {"Games",
     MenuItemType::Action,
     nullptr, nullptr, nullptr,
     0, 0, nullptr, 0,
     epd_bitmap_gamepad},
    {"Settings",
     MenuItemType::Action,
     openSettings, nullptr, nullptr,
     0, 0, nullptr, 0,
     epd_bitmap_settings},
    {"TV",
     MenuItemType::Action,
     openWiFi, nullptr, nullptr,
     0, 0, nullptr, 0,
     epd_bitmap_tv},
    {"WiFi",
     MenuItemType::Action,
     openWiFi,
     nullptr, nullptr,
     0, 0, nullptr, 0,
     epd_bitmap_wifi},
    {"About",
     MenuItemType::Action,
     openAbout,
     nullptr,
     nullptr,
     0,
     0,
     nullptr,
     0,
     epd_bitmap_info},
};

constexpr uint8_t MAIN_ITEM_COUNT =
    sizeof(mainItems) / sizeof(mainItems[0]);

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println();
    Serial.println("========== DOOMPAD ZERO BOOT ==========");
    Serial.println("[DEBUG] Serial output is working");
    Serial.printf("[DEBUG] Chip model: %s\n", ESP.getChipModel());
    Serial.printf("[DEBUG] Free heap: %u bytes\n",
                  (unsigned int)ESP.getFreeHeap());
    Wire.begin(OLED_SDA, OLED_SCL);

    if (!display.begin(
            SSD1306_SWITCHCAPVCC,
            OLED_ADDR))
    {
        while (true)
        {
            delay(1000);
        }
    }

    Input::begin();

    mainMenu = new Menu(
        display,
        "Main Menu",
        mainItems,
        MAIN_ITEM_COUNT,
        MenuLayout::IconGrid);

    mainMenu->draw();
}

void loop()
{
    Input::update();
    WifiManager::update();
    static Screen previousScreen = Screen::MainMenu;

    switch (currentScreen)
    {
    case Screen::MainMenu:
        mainMenu->update();
        break;

    case Screen::Settings:
        settingsUpdate();

        if (settingsBackRequested())
        {
            currentScreen = Screen::MainMenu;
        }
        break;

    case Screen::About:
        aboutUpdate();

        if (aboutBackRequested())
        {
            currentScreen = Screen::MainMenu;
        }
        break;

    case Screen::WiFi:
        WifiUI::update();

        if (WifiUI::backRequested())
        {
            currentScreen = Screen::MainMenu;
        }
        break;
    }

    // Initialize screens when entering them.
    if (currentScreen != previousScreen)
    {
        if (currentScreen == Screen::Settings)
        {
            settingsBegin(display);
        }
        else if (currentScreen == Screen::About)
        {
            aboutBegin(display);
        }
        else if (currentScreen == Screen::WiFi)
        {
            WifiUI::begin(display);
        }
        else if (currentScreen == Screen::MainMenu)
        {
            mainMenu->draw();
        }

        previousScreen = currentScreen;
    }

    // Draw the active screen.
    switch (currentScreen)
    {
    case Screen::MainMenu:
        // Your existing Menu update/draw behavior.
        break;

    case Screen::Settings:
        settingsDraw();
        break;

    case Screen::About:
        aboutDraw();
        break;

    case Screen::WiFi:
        WifiUI::draw();
        break;
    }
}
