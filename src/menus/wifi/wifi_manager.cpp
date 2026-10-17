
#include "wifi_manager.h"

#include <algorithm>

namespace WifiManager {

    static State currentState = State::Off;

    static AccessPoint networks[MAX_NETWORKS];
    static uint8_t networkCount = 0;

    static uint32_t scanStartedAt = 0;
    static uint32_t connectStartedAt = 0;

    static String connectingSSID;
    static bool scanRunning = false;

    static bool startHardwareScan() {
        WiFi.scanDelete();

        // Async scan: keep the OLED/menu loop responsive.
        int16_t result = WiFi.scanNetworks(true, false);

        if (result == WIFI_SCAN_FAILED) {
            scanRunning = false;
            return false;
        }

        scanRunning = true;
        return true;
    }

    void begin() {
        WiFi.mode(WIFI_OFF);
        currentState = State::Off;
        networkCount = 0;
        scanRunning = false;
    }

    void setEnabled(bool enabled) {
        if (!enabled) {
            WiFi.scanDelete();
            WiFi.disconnect(true, false);
            WiFi.mode(WIFI_OFF);

            scanRunning = false;
            networkCount = 0;
            currentState = State::Off;
            return;
        }

        WiFi.mode(WIFI_STA);

        if (!WiFi.setTxPower(WIFI_POWER_7dBm)) {
            Serial.println("Wi-Fi: couldn't set 7 dBm TX power");
        }

        currentState =
            WiFi.status() == WL_CONNECTED
                ? State::Connected
                : State::Idle;
    }

    bool isEnabled() {
        return WiFi.getMode() != WIFI_OFF;
    }

    void startScan() {
        if (!isEnabled()) {
            setEnabled(true);
        }

        if (currentState == State::Connecting ||
            currentState == State::Scanning) {
            return;
        }

        networkCount = 0;
        scanStartedAt = millis();
        scanRunning = false;

        if (startHardwareScan()) {
            currentState = State::Scanning;
        } else {
            currentState = State::Failed;
        }
    }

    static void collectScanResults() {
        int16_t count = WiFi.scanComplete();

        if (count < 0) {
            return;
        }

        for (int16_t i = 0; i < count; ++i) {
            String ssid = WiFi.SSID(i);

            // Skip hidden/empty SSIDs in the selectable list.
            if (ssid.length() == 0) {
                continue;
            }

            AccessPoint ap;
            ap.ssid = ssid;
            ap.rssi = WiFi.RSSI(i);
            ap.channel = WiFi.channel(i);
            ap.security = WiFi.encryptionType(i);

            // Keep one entry per SSID, retaining its strongest signal.
            int existing = -1;

            for (uint8_t j = 0; j < networkCount; ++j) {
                if (networks[j].ssid == ap.ssid) {
                    existing = j;
                    break;
                }
            }

            if (existing >= 0) {
                if (ap.rssi > networks[existing].rssi) {
                    networks[existing] = ap;
                }
            } else if (networkCount < MAX_NETWORKS) {
                networks[networkCount++] = ap;
            }
        }

        WiFi.scanDelete();

        // Sort strongest signal first. RSSI closer to zero is stronger.
        std::sort(
            networks,
            networks + networkCount,
            [](const AccessPoint& a, const AccessPoint& b) {
                return a.rssi > b.rssi;
            }
        );
    }

    void update() {
        if (!isEnabled()) {
            currentState = State::Off;
            return;
        }

        if (currentState == State::Scanning) {
            if (scanRunning) {
                int16_t result = WiFi.scanComplete();

                if (result >= 0) {
                    collectScanResults();
                    scanRunning = false;
                } else if (result == WIFI_SCAN_FAILED) {
                    scanRunning = false;
                }
            }

            const bool durationElapsed =
                millis() - scanStartedAt >= SCAN_DURATION_MS;

            if (durationElapsed) {
                if (scanRunning) {
                    WiFi.scanDelete();
                    scanRunning = false;
                }

                currentState = State::ScanComplete;
            } else if (!scanRunning) {
                // Repeat asynchronous scans during the 10-second window.
                if (!startHardwareScan()) {
                    // Keep any results already collected.
                    // The duration window still runs to completion.
                    scanRunning = false;
                }
            }

            return;
        }

        if (currentState == State::Connecting) {
            if (WiFi.status() == WL_CONNECTED) {
                currentState = State::Connected;

                Serial.print("Wi-Fi connected: ");
                Serial.println(WiFi.localIP());
            } else if (
                millis() - connectStartedAt >= CONNECT_TIMEOUT_MS
            ) {
                WiFi.disconnect(false, false);
                currentState = State::Failed;
            }
        }
    }

    State getState() {
        return currentState;
    }

    uint8_t getNetworkCount() {
        return networkCount;
    }

    const AccessPoint* getNetwork(uint8_t index) {
        if (index >= networkCount) {
            return nullptr;
        }

        return &networks[index];
    }

    uint8_t getScanProgress() {
        if (currentState != State::Scanning) {
            return currentState == State::ScanComplete ? 100 : 0;
        }

        uint32_t elapsed = millis() - scanStartedAt;

        if (elapsed >= SCAN_DURATION_MS) {
            return 100;
        }

        return static_cast<uint8_t>(
            (elapsed * 100UL) / SCAN_DURATION_MS
        );
    }

    bool connect(const String& ssid, const String& password) {
        if (!isEnabled() ||
            currentState == State::Scanning ||
            ssid.length() == 0) {
            return false;
        }

        WiFi.disconnect(false, false);

        if (!WiFi.setTxPower(WIFI_POWER_7dBm)) {
            Serial.println("Wi-Fi: TX power setting failed");
        }

        connectingSSID = ssid;
        connectStartedAt = millis();
        currentState = State::Connecting;

        WiFi.begin(
            connectingSSID.c_str(),
            password.c_str()
        );

        return true;
    }

    void disconnect() {
        if (isEnabled()) {
            WiFi.disconnect(false, false);

            currentState = State::Idle;
        }
    }

    String getSSID() {
        if (WiFi.status() == WL_CONNECTED) {
            return WiFi.SSID();
        }

        return "";
    }

    String getIP() {
        if (WiFi.status() == WL_CONNECTED) {
            return WiFi.localIP().toString();
        }

        return "";
    }

    int32_t getRSSI() {
        if (WiFi.status() == WL_CONNECTED) {
            return WiFi.RSSI();
        }

        return 0;
    }

    String getSecurityName(wifi_auth_mode_t security) {
        switch (security) {
            case WIFI_AUTH_OPEN:
                return "Open";
            case WIFI_AUTH_WEP:
                return "WEP";
            case WIFI_AUTH_WPA_PSK:
                return "WPA";
            case WIFI_AUTH_WPA2_PSK:
                return "WPA2";
            case WIFI_AUTH_WPA_WPA2_PSK:
                return "WPA/WPA2";
            case WIFI_AUTH_WPA2_ENTERPRISE:
                return "WPA2 Enterprise";
            case WIFI_AUTH_WPA3_PSK:
                return "WPA3";
            case WIFI_AUTH_WPA2_WPA3_PSK:
                return "WPA2/WPA3";
            case WIFI_AUTH_WAPI_PSK:
                return "WAPI";
            default:
                return "Unknown";
        }
    }

}
