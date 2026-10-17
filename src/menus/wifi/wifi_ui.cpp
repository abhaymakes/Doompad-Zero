
#include "wifi_ui.h"

#include "wifi_manager.h"
#include "../../ui/input.h"
#include "../../ui/text_keyboard.h"

namespace WifiUI {

    enum class Page : uint8_t {
        Menu,
        Manage,
        Scan,
        Networks,
        Password,
        Connecting,
        Result
    };

    static Adafruit_SSD1306* screen = nullptr;
    static Page page = Page::Menu;

    static uint8_t menuSelection = 0;
    static uint8_t manageSelection = 0;
    static uint8_t networkSelection = 0;
    static uint8_t networkScroll = 0;

    static bool returnToMain = false;
    static bool keyboardStarted = false;

    static String selectedSSID;
    static String statusMessage;
    static uint32_t resultStartedAt = 0;

    static constexpr uint8_t MENU_ITEMS = 2;
    static constexpr uint8_t VISIBLE_NETWORKS = 4;

    static void goBack() {
        switch (page) {
            case Page::Menu:
                returnToMain = true;
                break;

            case Page::Manage:
                page = Page::Menu;
                break;

            case Page::Scan:
            case Page::Networks:
                page = Page::Menu;
                break;

            case Page::Password:
                page = Page::Networks;
                keyboardStarted = false;
                break;

            case Page::Connecting:
                // Do not abandon an in-progress connection silently.
                page = Page::Result;
                statusMessage = "Connection pending";
                resultStartedAt = millis();
                break;

            case Page::Result:
                page = Page::Menu;
                break;
        }
    }

    static void drawHeader(const char* title) {
        screen->clearDisplay();
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);
        screen->setCursor(0, 0);
        screen->print(title);
        screen->drawLine(0, 9, 127, 9, SSD1306_WHITE);
    }

    static void drawMenuRow(
        uint8_t y,
        const char* label,
        bool selected
    ) {
        if (selected) {
            screen->fillRect(0, y - 1, 128, 11, SSD1306_WHITE);
            screen->setTextColor(SSD1306_BLACK);
        } else {
            screen->setTextColor(SSD1306_WHITE);
        }

        screen->setCursor(4, y);
        screen->print(label);
        screen->setTextColor(SSD1306_WHITE);
    }

    static void drawSignalBar(uint8_t y, int32_t rssi) {
        uint8_t level = 0;

        if (rssi >= -55) {
            level = 4;
        } else if (rssi >= -67) {
            level = 3;
        } else if (rssi >= -75) {
            level = 2;
        } else if (rssi >= -85) {
            level = 1;
        }

        screen->setCursor(88, y);
        screen->print(rssi);
        screen->print("dBm ");

        for (uint8_t i = 0; i < 4; ++i) {
            const int16_t x = 109 + i * 4;
            const int16_t height = 2 + i * 2;
            const int16_t barY = y + 7 - height;

            if (i < level) {
                screen->fillRect(x, barY, 3, height, SSD1306_WHITE);
            } else {
                screen->drawRect(x, barY, 3, height, SSD1306_WHITE);
            }
        }
    }

    static void updateMenu() {
        if (Input::up()) {
            menuSelection = (menuSelection + MENU_ITEMS - 1) % MENU_ITEMS;
        }

        if (Input::down()) {
            menuSelection = (menuSelection + 1) % MENU_ITEMS;
        }

        if (Input::menuBack()) {
            goBack();
            return;
        }

        if (Input::select()) {
            if (menuSelection == 0) {
                page = Page::Manage;
            } else {
                networkSelection = 0;
                networkScroll = 0;

                WifiManager::startScan();
                page = Page::Scan;
            }
        }
    }

    static void updateManage() {
        // Manage rows:
        // 0 Toggle Wi-Fi
        // 1 Disconnect
        // 2 Back
        if (Input::up()) {
            manageSelection = (manageSelection + 2) % 3;
        }

        if (Input::down()) {
            manageSelection = (manageSelection + 1) % 3;
        }

        if (Input::menuBack()) {
            page = Page::Menu;
            return;
        }

        if (!Input::select()) {
            return;
        }

        switch (manageSelection) {
            case 0:
                WifiManager::setEnabled(!WifiManager::isEnabled());
                break;

            case 1:
                WifiManager::disconnect();
                statusMessage = "Disconnected";
                break;

            case 2:
                page = Page::Menu;
                break;
        }
    }

    static void updateScan() {
        if (Input::menuBack()) {
            goBack();
            return;
        }

        if (WifiManager::getState() == WifiManager::State::ScanComplete) {
            networkSelection = 0;
            networkScroll = 0;
            page = Page::Networks;
        } else if (WifiManager::getState() == WifiManager::State::Failed) {
            statusMessage = "Scan failed";
            page = Page::Result;
            resultStartedAt = millis();
        }
    }

    static void updateNetworks() {
        const uint8_t count = WifiManager::getNetworkCount();

        if (Input::menuBack()) {
            page = Page::Menu;
            return;
        }

        if (count == 0) {
            if (Input::select()) {
                WifiManager::startScan();
                page = Page::Scan;
            }
            return;
        }

        if (Input::up() && networkSelection > 0) {
            --networkSelection;
        }

        if (Input::down() && networkSelection + 1 < count) {
            ++networkSelection;
        }

        if (networkSelection < networkScroll) {
            networkScroll = networkSelection;
        } else if (networkSelection >= networkScroll + VISIBLE_NETWORKS) {
            networkScroll = networkSelection - VISIBLE_NETWORKS + 1;
        }

        if (Input::select()) {
            const WifiManager::AccessPoint* ap =
                WifiManager::getNetwork(networkSelection);

            if (ap) {
                selectedSSID = ap->ssid;

                if (ap->security == WIFI_AUTH_OPEN) {
                    if (WifiManager::connect(selectedSSID, "")) {
                        statusMessage = "Connecting...";
                        page = Page::Connecting;
                    }
                } else {
                    keyboardStarted = false;
                    page = Page::Password;
                }
            }
        }
    }

    static void updatePassword() {
        if (!keyboardStarted) {
            TextKeyboard::begin(
                *screen,
                "Wi-Fi password",
                "",
                63
            );
            keyboardStarted = true;
            return;
        }

        TextKeyboard::Result result = TextKeyboard::update();

        if (result == TextKeyboard::Result::Cancelled) {
            keyboardStarted = false;
            page = Page::Networks;
            return;
        }

        if (result == TextKeyboard::Result::Accepted) {
            String password = TextKeyboard::value();

            if (WifiManager::connect(selectedSSID, password)) {
                statusMessage = "Connecting...";
                page = Page::Connecting;
            } else {
                statusMessage = "Could not start";
                page = Page::Result;
                resultStartedAt = millis();
            }

            keyboardStarted = false;
        }
    }

    static void updateConnecting() {
        if (WifiManager::getState() == WifiManager::State::Connected) {
            statusMessage = "Connected!";
            page = Page::Result;
            resultStartedAt = millis();
        } else if (WifiManager::getState() == WifiManager::State::Failed) {
            statusMessage = "Connection failed";
            page = Page::Result;
            resultStartedAt = millis();
        }
    }

    static void updateResult() {
        if (Input::select() || Input::menuBack()) {
            page = Page::Menu;
            return;
        }

        if (millis() - resultStartedAt > 2500) {
            page = Page::Menu;
        }
    }

    void begin(Adafruit_SSD1306& display) {
        screen = &display;

        page = Page::Menu;
        menuSelection = 0;
        manageSelection = 0;
        networkSelection = 0;
        networkScroll = 0;

        returnToMain = false;
        keyboardStarted = false;

        statusMessage = "";
        WifiManager::begin();
    }

    void update() {
        if (!screen || returnToMain) {
            return;
        }

        WifiManager::update();

        switch (page) {
            case Page::Menu:
                updateMenu();
                break;

            case Page::Manage:
                updateManage();
                break;

            case Page::Scan:
                updateScan();
                break;

            case Page::Networks:
                updateNetworks();
                break;

            case Page::Password:
                updatePassword();
                break;

            case Page::Connecting:
                updateConnecting();
                break;

            case Page::Result:
                updateResult();
                break;
        }
    }

    void draw() {
        if (!screen || returnToMain) {
            return;
        }

        // TextKeyboard draws itself while active.
        if (page == Page::Password && keyboardStarted) {
            return;
        }

        switch (page) {
            case Page::Menu:
                drawHeader("Wi-Fi");
                drawMenuRow(20, "Manage", menuSelection == 0);
                drawMenuRow(34, "Connect Wi-Fi", menuSelection == 1);

                screen->setCursor(0, 56);
                screen->print("UP/DOWN  OK:Select");
                break;

            case Page::Manage: {
                drawHeader("Wi-Fi / Manage");

                screen->setCursor(2, 12);
                screen->print("Radio: ");
                screen->print(WifiManager::isEnabled() ? "ON" : "OFF");

                screen->setCursor(2, 22);
                screen->print("Status: ");
                screen->print(
                    WiFi.status() == WL_CONNECTED
                        ? "Connected"
                        : (WifiManager::isEnabled() ? "Not connected" : "Off")
                );

                screen->setCursor(2, 32);
                screen->print("SSID: ");
                String ssid = WifiManager::getSSID();
                screen->print(ssid.length() ? ssid.substring(0, 15) : "-");

                screen->setCursor(2, 42);
                screen->print("IP: ");
                String ip = WifiManager::getIP();
                screen->print(ip.length() ? ip : "-");

                if (WiFi.status() == WL_CONNECTED) {
                    drawSignalBar(42, WifiManager::getRSSI());
                }

                drawMenuRow(51, "Toggle Wi-Fi", manageSelection == 0);
                drawMenuRow(59, "Disconnect", manageSelection == 1);
                // Back remains available through long-UP.
                break;
            }

            case Page::Scan: {
                drawHeader("Scanning Wi-Fi");

                uint8_t progress = WifiManager::getScanProgress();

                screen->setCursor(0, 18);
                screen->print("Please wait...");

                screen->drawRect(0, 32, 128, 10, SSD1306_WHITE);
                screen->fillRect(2, 34, (progress * 124) / 100, 6, SSD1306_WHITE);

                screen->setCursor(0, 47);
                screen->print(progress);
                screen->print("%  (10 seconds)");
                break;
            }

            case Page::Networks: {
                drawHeader("Choose network");

                uint8_t count = WifiManager::getNetworkCount();

                if (count == 0) {
                    screen->setCursor(0, 20);
                    screen->print("No networks found");
                    screen->setCursor(0, 36);
                    screen->print("OK: Rescan");
                    break;
                }

                for (uint8_t row = 0; row < VISIBLE_NETWORKS; ++row) {
                    uint8_t index = networkScroll + row;

                    if (index >= count) {
                        break;
                    }

                    const WifiManager::AccessPoint* ap =
                        WifiManager::getNetwork(index);

                    if (!ap) {
                        continue;
                    }

                    const uint8_t y = 12 + row * 12;
                    bool selected = index == networkSelection;

                    if (selected) {
                        screen->fillRect(0, y - 1, 128, 11, SSD1306_WHITE);
                        screen->setTextColor(SSD1306_BLACK);
                    }

                    screen->setCursor(2, y);
                    String name = ap->ssid;
                    if (name.length() > 13) {
                        name = name.substring(0, 13);
                    }
                    screen->print(name);

                    screen->setCursor(91, y);
                    screen->print(ap->rssi);
                    screen->print("dB");

                    screen->setTextColor(SSD1306_WHITE);
                }

                screen->setCursor(0, 57);
                screen->print(networkSelection + 1);
                screen->print("/");
                screen->print(count);
                screen->print("  OK:Choose");
                break;
            }

            case Page::Password:
                // The keyboard owns this page's drawing.
                break;

            case Page::Connecting:
                drawHeader("Connecting");

                screen->setCursor(0, 19);
                screen->print(selectedSSID.substring(0, 20));

                screen->setCursor(0, 33);
                screen->print("Connecting to AP...");

                screen->setCursor(0, 49);
                screen->print("Please wait");
                break;

            case Page::Result:
                drawHeader("Wi-Fi result");

                screen->setCursor(0, 20);
                screen->print(statusMessage.substring(0, 21));

                if (WifiManager::getState() == WifiManager::State::Connected) {
                    screen->setCursor(0, 33);
                    screen->print(WifiManager::getIP());
                }

                screen->setCursor(0, 55);
                screen->print("OK:Continue");
                break;
        }

        screen->display();
    }

    bool backRequested() {
        return returnToMain;
    }

}
