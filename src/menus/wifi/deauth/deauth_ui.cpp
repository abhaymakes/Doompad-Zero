#include "deauth_ui.h"

#include "deauth_monitor.h"
#include "../wifi_manager.h"
#include "../../../ui/input.h"

namespace DeauthPage
{

    enum class Page : uint8_t
    {
        List,
        Status
    };

    static Adafruit_SSD1306 *screen = nullptr;
    static Page page = Page::List;

    static uint8_t selection = 0;
    static uint8_t scroll = 0;

    static bool returnToMenu = false;
    static bool dirty = true;
    static uint32_t lastDrawAt = 0;
    static WifiManager::State lastScanState = WifiManager::State::Off;

    static char selectedSsid[33];
    static uint8_t selectedBssid[6];

    static constexpr uint8_t VISIBLE_NETWORKS = 5;
    static constexpr uint32_t REFRESH_MS = 300;

    static void formatMac(const uint8_t *mac, char *out, uint8_t bytes)
    {
        static const char *hex = "0123456789ABCDEF";
        uint8_t o = 0;
        for (uint8_t i = 0; i < bytes; ++i)
        {
            out[o++] = hex[mac[i] >> 4];
            out[o++] = hex[mac[i] & 0x0F];
            if (i + 1 < bytes)
                out[o++] = ':';
        }
        out[o] = '\0';
    }

    static void drawScrollbar(uint8_t count)
    {
        if (count <= VISIBLE_NETWORKS)
            return;

        constexpr int16_t trackY = 1;
        constexpr int16_t trackHeight = 62;
        constexpr int16_t innerHeight = 60;

        int16_t thumbHeight = (innerHeight * VISIBLE_NETWORKS) / count;
        if (thumbHeight < 6)
            thumbHeight = 6;

        const uint8_t maxScroll = count - VISIBLE_NETWORKS;
        const int16_t thumbY =
            trackY + 1 + ((innerHeight - thumbHeight) * scroll) / maxScroll;

        screen->drawRect(124, trackY, 4, trackHeight, SSD1306_WHITE);
        screen->fillRect(125, thumbY, 2, thumbHeight, SSD1306_WHITE);
    }

    static void drawList()
    {
        screen->clearDisplay();
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);

        const WifiManager::State wifiState = WifiManager::getState();
        const uint8_t count = WifiManager::getNetworkCount();

        if (wifiState == WifiManager::State::Scanning)
        {
            screen->setCursor(0, 2);
            screen->print("Deauth: scanning");

            const uint8_t progress = WifiManager::getScanProgress();
            screen->drawRect(0, 26, 128, 10, SSD1306_WHITE);
            const uint8_t width = (progress * 124) / 100;
            if (width)
                screen->fillRect(2, 28, width, 6, SSD1306_WHITE);

            screen->setCursor(0, 44);
            screen->print(progress);
            screen->print("%  hold UP: exit");
            screen->display();
            return;
        }

        if (wifiState == WifiManager::State::Failed)
        {
            screen->setCursor(0, 2);
            screen->print("Deauth: scan failed");
            screen->setCursor(0, 26);
            screen->print("SELECT: retry");
            screen->display();
            return;
        }

        screen->setCursor(0, 2);
        screen->print("Deauth: pick AP");

        if (count == 0)
        {
            screen->setCursor(0, 30);
            screen->print("No networks");
            screen->display();
            return;
        }

        for (uint8_t row = 0; row < VISIBLE_NETWORKS; ++row)
        {
            const uint8_t index = scroll + row;
            if (index >= count)
                break;

            const WifiManager::AccessPoint *ap = WifiManager::getNetwork(index);
            if (!ap)
                continue;

            const uint8_t y = 13 + row * 10;
            const bool isSelected = (index == selection);

            if (isSelected)
            {
                screen->fillRect(0, y - 1, 122, 10, SSD1306_WHITE);
                screen->setTextColor(SSD1306_BLACK);
            }

            String name = ap->ssid;
            if (name.length() > 13)
                name = name.substring(0, 13);

            screen->setCursor(2, y);
            screen->print(name);
            screen->setCursor(86, y);
            screen->print(ap->rssi);
            screen->setCursor(114, y);
            screen->print(ap->channel);

            screen->setTextColor(SSD1306_WHITE);
        }

        drawScrollbar(count);
        screen->display();
    }

    static void drawStatus()
    {
        screen->clearDisplay();
        screen->setTextSize(1);
        screen->setTextColor(SSD1306_WHITE);

        const DeauthMonitor::Counters c = DeauthMonitor::counters();
        char line[26];

        strncpy(line, selectedSsid, 21);
        line[21] = '\0';
        screen->setCursor(0, 0);
        screen->print(line);

        char mac[18];
        formatMac(selectedBssid, mac, 6);
        screen->setCursor(0, 8);
        screen->print(mac);

        if (DeauthMonitor::state() == DeauthMonitor::State::Unsupported)
        {
            screen->setCursor(0, 16);
            screen->print("promiscuous FAILED");
        }
        else
        {
            if (c.channelMismatch)
                snprintf(line, sizeof(line), "MON ch%u! on %u",
                         c.targetChannel, c.actualChannel);
            else
                snprintf(line, sizeof(line), "MON ch%u  %ddBm",
                         c.actualChannel, c.targetRssi);
            screen->setCursor(0, 16);
            screen->print(line);

            if (c.activity)
                screen->fillRect(118, 16, 8, 8, SSD1306_WHITE);
            else
                screen->drawRect(118, 16, 8, 8, SSD1306_WHITE);
        }

        // Transmit state. Blinks while actively injecting.
        if (c.txActive)
        {
            const bool phase = (millis() / 400) & 1;
            if (phase)
                screen->fillRect(0, 24, 42, 8, SSD1306_WHITE);
            screen->setTextColor(phase ? SSD1306_BLACK : SSD1306_WHITE);
            screen->setCursor(1, 24);
            screen->print("TX ON");
            screen->setTextColor(SSD1306_WHITE);
        }
        else
        {
            screen->setCursor(0, 24);
            screen->print("TX off");
        }

        snprintf(line, sizeof(line), "%s rs%u",
                 c.txBroadcast ? "bcast" : "unicast", (unsigned)c.txReason);
        screen->setCursor(50, 24);
        screen->print(line);

        snprintf(line, sizeof(line), "sent %lu  rx %lu",
                 (unsigned long)c.txSent, (unsigned long)c.deauthTotal);
        screen->setCursor(0, 32);
        screen->print(line);

        snprintf(line, sizeof(line), "cli %lu  AP>%lu  c>%lu",
                 (unsigned long)c.distinctClients,
                 (unsigned long)c.deauthFromAp,
                 (unsigned long)c.deauthFromClient);
        screen->setCursor(0, 40);
        screen->print(line);

        snprintf(line, sizeof(line), "bc %lu  rs %u  own %lu",
                 (unsigned long)c.broadcastDeauth,
                 (unsigned)c.lastReason,
                 (unsigned long)c.txSelfHeard);
        screen->setCursor(0, 48);
        screen->print(line);

        if (c.lastEvent == DeauthMonitor::Event::None)
        {
            screen->setCursor(0, 56);
            screen->print("no deauth seen");
        }
        else
        {
            const char *what = "?";
            switch (c.lastEvent)
            {
            case DeauthMonitor::Event::DeauthFromAp:
                what = "DEAUTH AP";
                break;
            case DeauthMonitor::Event::DeauthFromClient:
                what = "DEAUTH cli";
                break;
            case DeauthMonitor::Event::DisassocFromAp:
                what = "DISASSOC AP";
                break;
            case DeauthMonitor::Event::DeauthBroadcast:
                what = "DEAUTH bcast";
                break;
            default:
                break;
            }
            char shortMac[10];
            formatMac(c.lastClient, shortMac, 3);
            snprintf(line, sizeof(line), "%s %s", what, shortMac);
            screen->setCursor(0, 56);
            screen->print(line);
        }

        screen->display();
    }

    static void updateList()
    {
        const WifiManager::State wifiState = WifiManager::getState();
        const uint8_t count = WifiManager::getNetworkCount();

        if (wifiState != lastScanState)
        {
            lastScanState = wifiState;
            dirty = true;
            if (wifiState == WifiManager::State::ScanComplete)
            {
                selection = 0;
                scroll = 0;
            }
        }

        if (Input::menuBack())
        {
            DeauthMonitor::stop();
            returnToMenu = true;
            return;
        }

        if (wifiState == WifiManager::State::Scanning)
            return;

        if (count == 0)
        {
            if (Input::select())
            {
                WifiManager::startScan();
                dirty = true;
            }
            return;
        }

        if (Input::up() && selection > 0)
        {
            --selection;
            dirty = true;
        }
        if (Input::down() && selection + 1 < count)
        {
            ++selection;
            dirty = true;
        }

        if (selection < scroll)
        {
            scroll = selection;
        }
        else if (selection >= scroll + VISIBLE_NETWORKS)
        {
            scroll = selection - VISIBLE_NETWORKS + 1;
        }

        if (Input::select())
        {
            const WifiManager::AccessPoint *ap = WifiManager::getNetwork(selection);
            if (!ap)
                return;

            strncpy(selectedSsid, ap->ssid.c_str(), 32);
            selectedSsid[32] = '\0';
            for (uint8_t i = 0; i < 6; ++i)
                selectedBssid[i] = ap->bssid[i];

            DeauthMonitor::start(selectedSsid, selectedBssid,
                                 ap->channel, static_cast<int8_t>(ap->rssi));

            page = Page::Status;
            dirty = true;
        }
    }

    static void updateStatus()
    {
        DeauthMonitor::updateTransmit();

        if (Input::menuBack())
        {
            DeauthMonitor::stop();
            page = Page::List;
            lastScanState = WifiManager::getState();
            dirty = true;
            return;
        }

        if (Input::select())
        {
            DeauthMonitor::setTransmit(!DeauthMonitor::transmitting());
            dirty = true;
        }

        if (Input::left())
        {
            DeauthMonitor::burst(5);
            dirty = true;
        }

        if (Input::right())
        {
            static const uint16_t reasons[] = {2, 4, 7, 15};
            static uint8_t idx = 0;
            idx = (idx + 1) % 4;
            DeauthMonitor::setReason(reasons[idx]);
            dirty = true;
        }

        if (Input::down())
        {
            DeauthMonitor::setBroadcast(!DeauthMonitor::broadcast());
            dirty = true;
        }
    }

    void begin(Adafruit_SSD1306 &display)
    {
        screen = &display;
        page = Page::List;
        selection = 0;
        scroll = 0;
        returnToMenu = false;
        dirty = true;
        selectedSsid[0] = '\0';
        for (uint8_t i = 0; i < 6; ++i)
            selectedBssid[i] = 0;

        // Monitor needs the radio; startScan() enables Wi-Fi if it was off.
        WifiManager::startScan();
        lastScanState = WifiManager::getState();
    }

    void update()
    {
        if (!screen || returnToMenu)
            return;

        if (page == Page::List)
            updateList();
        else
            updateStatus();
    }

    void draw()
    {
        if (!screen || returnToMenu)
            return;

        const uint32_t now = millis();
        if (!dirty && (uint32_t)(now - lastDrawAt) < REFRESH_MS)
            return;

        if (page == Page::List)
            drawList();
        else
            drawStatus();

        dirty = false;
        lastDrawAt = now;
    }

    void stop()
    {
        DeauthMonitor::stop();
        page = Page::List;
        returnToMenu = false;
    }

    bool backRequested()
    {
        if (returnToMenu)
        {
            returnToMenu = false;
            return true;
        }
        return false;
    }

} // namespace DeauthPage