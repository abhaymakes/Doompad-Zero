#include "wifi_ui.h"

#include "wifi_manager.h"
#include "../../ui/input.h"
#include "../../ui/text_keyboard.h"

namespace WifiUI
{

    enum class Page : uint8_t
    {
        Menu,
        Manage,
        Scan,
        Networks,
        Password,
        Connecting,
        Result
    };

    static Adafruit_SSD1306 *screen = nullptr;
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
    static constexpr uint8_t VISIBLE_NETWORKS = 5;

    static void goBack()
    {
        switch (page)
        {
        case Page::Menu:
            returnToMain = true;
            break;
        case Page::Manage:
        case Page::Scan:
        case Page::Networks:
        case Page::Result:
            page = Page::Menu;
            break;
        case Page::Password:
            page = Page::Networks;
            keyboardStarted = false;
            break;
        case Page::Connecting:
            // Keep the connection attempt running, but let the user leave this view.
            page = Page::Result;
            statusMessage = "Connection pending";
            resultStartedAt = millis();
            break;
        }
    }

    static void prepareFrame()
    {
        screen->clearDisplay();
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);
    }

    static void drawNetworkScrollbar(uint8_t count)
    {
        if (count <= VISIBLE_NETWORKS)
            return;

        constexpr int16_t trackY = 1;
        constexpr int16_t trackHeight = 62;
        constexpr int16_t trackInnerHeight = 60;

        int16_t thumbHeight =
            (trackInnerHeight * VISIBLE_NETWORKS) / count;

        if (thumbHeight < 6)
            thumbHeight = 6;

        const uint8_t maxScroll = count - VISIBLE_NETWORKS;

        const int16_t thumbY =
            trackY + 1 +
            ((trackInnerHeight - thumbHeight) * networkScroll) / maxScroll;

        screen->drawRect(124, trackY, 4, trackHeight, SSD1306_WHITE);
        screen->fillRect(125, thumbY, 2, thumbHeight, SSD1306_WHITE);
    }

    static void drawMenuRow(uint8_t y, const char *label, bool selected,
                            uint8_t rowHeight = 10)
    {
        if (selected)
        {
            screen->fillRect(0, y - 1, 128, rowHeight, SSD1306_WHITE);
            screen->setTextColor(SSD1306_BLACK);
        }
        else
        {
            screen->setTextColor(SSD1306_WHITE);
        }

        screen->setCursor(4, y);
        screen->print(label);
        screen->setTextColor(SSD1306_WHITE);
    }

    static void drawSignalBar(uint8_t y, int32_t rssi)
    {
        uint8_t level = 0;
        if (rssi >= -55)
            level = 4;
        else if (rssi >= -67)
            level = 3;
        else if (rssi >= -75)
            level = 2;
        else if (rssi >= -85)
            level = 1;

        screen->setCursor(83, y);
        screen->print(rssi);
        screen->print("dBm");

        for (uint8_t i = 0; i < 4; ++i)
        {
            const int16_t x = 108 + i * 4;
            const int16_t height = 2 + i * 2;
            const int16_t barY = y + 7 - height;
            if (i < level)
                screen->fillRect(x, barY, 3, height, SSD1306_WHITE);
            else
                screen->drawRect(x, barY, 3, height, SSD1306_WHITE);
        }
    }

    static void updateMenu()
    {
        if (Input::up())
            menuSelection = (menuSelection + MENU_ITEMS - 1) % MENU_ITEMS;
        if (Input::down())
            menuSelection = (menuSelection + 1) % MENU_ITEMS;

        if (Input::menuBack())
        {
            goBack();
            return;
        }

        if (Input::select())
        {
            if (menuSelection == 0)
            {
                page = Page::Manage;
            }
            else
            {
                networkSelection = 0;
                networkScroll = 0;
                WifiManager::startScan();
                page = Page::Scan;
            }
        }
    }

    static void updateManage()
    {
        if (Input::up())
            manageSelection = (manageSelection + 2) % 3;
        if (Input::down())
            manageSelection = (manageSelection + 1) % 3;

        if (Input::menuBack())
        {
            page = Page::Menu;
            return;
        }

        if (!Input::select())
            return;

        switch (manageSelection)
        {
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

    static void updateScan()
    {
        if (Input::menuBack())
        {
            goBack();
            return;
        }

        if (WifiManager::getState() == WifiManager::State::ScanComplete)
        {
            networkSelection = 0;
            networkScroll = 0;
            page = Page::Networks;
        }
        else if (WifiManager::getState() == WifiManager::State::Failed)
        {
            statusMessage = "Scan failed";
            page = Page::Result;
            resultStartedAt = millis();
        }
    }

    static void updateNetworks()
    {
        const uint8_t count = WifiManager::getNetworkCount();

        if (Input::menuBack())
        {
            page = Page::Menu;
            return;
        }

        if (count == 0)
        {
            if (Input::select())
            {
                WifiManager::startScan();
                page = Page::Scan;
            }
            return;
        }

        if (Input::up() && networkSelection > 0)
            --networkSelection;
        if (Input::down() && networkSelection + 1 < count)
            ++networkSelection;

        if (networkSelection < networkScroll)
            networkScroll = networkSelection;
        else if (networkSelection >= networkScroll + VISIBLE_NETWORKS)
            networkScroll = networkSelection - VISIBLE_NETWORKS + 1;

        if (Input::select())
        {
            const WifiManager::AccessPoint *ap = WifiManager::getNetwork(networkSelection);
            if (!ap)
                return;

            selectedSSID = ap->ssid;
            if (ap->security == WIFI_AUTH_OPEN)
            {
                if (WifiManager::connect(selectedSSID, ""))
                {
                    statusMessage = "Connecting...";
                    page = Page::Connecting;
                }
            }
            else
            {
                keyboardStarted = false;
                page = Page::Password;
            }
        }
    }

    static void updatePassword()
    {
        if (!keyboardStarted)
        {
            TextKeyboard::begin(*screen, "Wi-Fi password", "", 63);
            keyboardStarted = true;
            return;
        }

        const TextKeyboard::Result result = TextKeyboard::update();
        if (result == TextKeyboard::Result::Cancelled)
        {
            keyboardStarted = false;
            page = Page::Networks;
            return;
        }

        if (result == TextKeyboard::Result::Accepted)
        {
            const String password = TextKeyboard::value();
            if (WifiManager::connect(selectedSSID, password))
            {
                statusMessage = "Connecting...";
                page = Page::Connecting;
            }
            else
            {
                statusMessage = "Could not start";
                page = Page::Result;
                resultStartedAt = millis();
            }
            keyboardStarted = false;
        }
    }

    static void updateConnecting()
    {
        const WifiManager::State state = WifiManager::getState();
        if (state == WifiManager::State::Connected)
        {
            statusMessage = "Connected!";
            page = Page::Result;
            resultStartedAt = millis();
        }
        else if (state == WifiManager::State::Failed)
        {
            statusMessage = "Connection failed";
            page = Page::Result;
            resultStartedAt = millis();
        }
        else if (state == WifiManager::State::Off || state == WifiManager::State::Idle)
        {
            statusMessage = "Connection stopped";
            page = Page::Result;
            resultStartedAt = millis();
        }
    }

    static void updateResult()
    {
        if (Input::select() || Input::menuBack())
        {
            page = Page::Menu;
            return;
        }
        if (millis() - resultStartedAt > 2500)
            page = Page::Menu;
    }

    void begin(Adafruit_SSD1306 &display)
    {
        screen = &display;
        page = Page::Menu;
        menuSelection = 0;
        manageSelection = 0;
        networkSelection = 0;
        networkScroll = 0;
        returnToMain = false;
        keyboardStarted = false;
        statusMessage = "";
    }

    void update()
    {
        if (!screen || returnToMain)
            return;

        // WifiManager::update() is called once per main loop from main.cpp so
        // diagnostics and reconnect handling continue even when this UI is closed.
        switch (page)
        {
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

    void draw()
    {
        if (!screen || returnToMain)
            return;

        // The password keyboard draws its own frame and flushes it itself.
        if (page == Page::Password && keyboardStarted)
            return;

        prepareFrame();

        switch (page)
        {
        case Page::Menu:
            drawMenuRow(5, "Manage", menuSelection == 0, 12);
            drawMenuRow(21, "Connect Wi-Fi", menuSelection == 1, 12);
            break;

        case Page::Manage:
        {
            // Always render live status; STA mode alone does not mean connected.
            const WifiManager::State wifiState = WifiManager::getState();
            const char *linkText = "OFF";
            if (WifiManager::isEnabled())
            {
                switch (wifiState)
                {
                case WifiManager::State::Connected:
                    linkText = "CONNECTED";
                    break;
                case WifiManager::State::Connecting:
                    linkText = "CONNECTING";
                    break;
                case WifiManager::State::Scanning:
                    linkText = "SCANNING";
                    break;
                case WifiManager::State::Failed:
                    linkText = "FAILED";
                    break;
                case WifiManager::State::ScanComplete:
                    linkText = "ENABLED";
                    break;
                case WifiManager::State::Idle:
                    linkText = "ENABLED";
                    break;
                case WifiManager::State::Off:
                    linkText = "OFF";
                    break;
                }
            }
            screen->setCursor(0, 0);
            screen->print("Wi-Fi: ");
            screen->print(linkText);
            screen->setCursor(0, 9);
            screen->print("SSID: ");
            const String ssid = WifiManager::getSSID();
            screen->print(ssid.length() ? ssid.substring(0, 18) : "-");
            screen->setCursor(0, 18);
            screen->print("IP: ");
            const String ip = WifiManager::getIP();
            screen->print(ip.length() ? ip : "-");

            drawMenuRow(30, "Toggle Wi-Fi", manageSelection == 0, 10);
            drawMenuRow(40, "Disconnect", manageSelection == 1, 10);
            drawMenuRow(50, "Back", manageSelection == 2, 10);
            break;
        }

        case Page::Scan:
        {
            screen->setCursor(0, 2);
            screen->print("Scanning for networks");
            const uint8_t progress = WifiManager::getScanProgress();
            screen->drawRect(0, 24, 128, 10, SSD1306_WHITE);
            const uint8_t width = (progress * 124) / 100;
            if (width)
                screen->fillRect(2, 26, width, 6, SSD1306_WHITE);
            screen->setCursor(0, 42);
            screen->print(progress);
            screen->print("%  (up to 5 sec)");
            break;
        }

        case Page::Networks:
        {
            const uint8_t count = WifiManager::getNetworkCount();
            if (count == 0)
            {
                screen->setCursor(0, 8);
                screen->print("No networks found");
                break;
            }

            for (uint8_t row = 0; row < VISIBLE_NETWORKS; ++row)
            {
                const uint8_t index = networkScroll + row;
                if (index >= count)
                    break;

                const WifiManager::AccessPoint *ap = WifiManager::getNetwork(index);
                if (!ap)
                    continue;

                const uint8_t y = 2 + row * 12;
                const bool selected = index == networkSelection;
                if (selected)
                {
                    screen->fillRect(0, y - 1, 122, 11, SSD1306_WHITE);
                    screen->setTextColor(SSD1306_BLACK);
                }

                String name = ap->ssid;
                if (name.length() > 13)
                    name = name.substring(0, 13);
                screen->setCursor(2, y);
                screen->print(name);
                screen->setCursor(91, y);
                screen->print(ap->rssi);
                screen->print("dBm");
                screen->setTextColor(SSD1306_WHITE);
            }

            drawNetworkScrollbar(count);
            break;
        }

        case Page::Password:
            // Keyboard owns this page's drawing.
            break;

        case Page::Connecting:
            screen->setCursor(0, 8);
            screen->print("Connecting to:");
            screen->setCursor(0, 20);
            screen->print(selectedSSID.substring(0, 21));
            screen->setCursor(0, 36);
            screen->print("Please wait...");
            break;

        case Page::Result:
            screen->setCursor(0, 10);
            screen->print(statusMessage.substring(0, 21));
            if (WifiManager::getState() == WifiManager::State::Connected)
            {
                screen->setCursor(0, 27);
                screen->print(WifiManager::getIP());
            }
            break;
        }

        screen->display();
    }

    bool backRequested()
    {
        return returnToMain;
    }

} // namespace WifiUI
