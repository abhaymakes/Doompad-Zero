#include "wifi_manager.h"

#include <Preferences.h>
#include <algorithm>

namespace WifiManager
{

    static State currentState = State::Off;
    static AccessPoint networks[MAX_NETWORKS];
    static uint8_t networkCount = 0;
    static uint32_t scanStartedAt = 0;
    static uint32_t connectStartedAt = 0;
    static bool scanRunning = false;

    // Persistent Wi-Fi configuration.
    static Preferences wifiPrefs;
    static bool prefsReady = false;
    static bool savedWiFiEnabled = false;
    static String savedSSID;
    static String savedPassword;
    static String connectingSSID; // SSID for the active connection attempt and its diagnostics.

    // Credentials are committed only after a successful connection.
    static String pendingSSID;
    static String pendingPassword;
    static bool pendingCredentials = false;
    static uint32_t lastDebugAt = 0;
    static bool lastRadioOn = false;
    static wl_status_t lastLoggedStatus = WL_IDLE_STATUS;

    static const char *stateName(State state)
    {
        switch (state)
        {
        case State::Off:
            return "OFF";
        case State::Idle:
            return "IDLE";
        case State::Scanning:
            return "SCANNING";
        case State::ScanComplete:
            return "SCAN_COMPLETE";
        case State::Connecting:
            return "CONNECTING";
        case State::Connected:
            return "CONNECTED";
        case State::Failed:
            return "FAILED";
        default:
            return "UNKNOWN";
        }
    }

    static const char *modeName(wifi_mode_t mode)
    {
        switch (mode)
        {
        case WIFI_OFF:
            return "OFF";
        case WIFI_STA:
            return "STA";
        case WIFI_AP:
            return "AP";
        case WIFI_AP_STA:
            return "AP_STA";
        default:
            return "UNKNOWN";
        }
    }

    static const char *statusName(wl_status_t status)
    {
        switch (status)
        {
        case WL_IDLE_STATUS:
            return "IDLE";
        case WL_NO_SSID_AVAIL:
            return "NO_SSID_AVAILABLE";
        case WL_SCAN_COMPLETED:
            return "SCAN_COMPLETED";
        case WL_CONNECTED:
            return "CONNECTED";
        case WL_CONNECT_FAILED:
            return "CONNECT_FAILED";
        case WL_CONNECTION_LOST:
            return "CONNECTION_LOST";
        case WL_DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
        }
    }

    static void logRadioVerification(const char *operation)
    {
        const wifi_mode_t mode = WiFi.getMode();
        const bool radioOn = mode != WIFI_OFF;
        Serial.printf("[WiFi] %s: requested=%s, actual_mode=%s, radio=%s, driver_status=%s (%d)\n",
                      operation, savedWiFiEnabled ? "ON" : "OFF", modeName(mode),
                      radioOn ? "ON" : "OFF", statusName(WiFi.status()),
                      static_cast<int>(WiFi.status()));
        if (radioOn != savedWiFiEnabled)
        {
            Serial.println("[WiFi][WARN] Saved enabled preference and actual radio mode disagree.");
        }
        lastRadioOn = radioOn;
        lastLoggedStatus = WiFi.status();
    }

    static void debugTick()
    {
        const uint32_t now = millis();
        if (now - lastDebugAt < 5000UL)
            return;
        lastDebugAt = now;

        const wifi_mode_t mode = WiFi.getMode();
        const wl_status_t status = WiFi.status();
        const bool radioOn = mode != WIFI_OFF;
        Serial.println("[WiFi][5s] ----- diagnostics -----");
        Serial.printf("[WiFi][5s] RAM preference enabled=%s; actual radio=%s; mode=%s\n",
                      savedWiFiEnabled ? "true" : "false",
                      radioOn ? "ON" : "OFF", modeName(mode));
        Serial.printf("[WiFi][5s] state=%s; driver_status=%s (%d); scanning=%s; networks=%u; pending_credentials=%s\n",
                      stateName(currentState), statusName(status), static_cast<int>(status),
                      scanRunning ? "true" : "false", static_cast<unsigned>(networkCount),
                      pendingCredentials ? "true" : "false");
        Serial.printf("[WiFi][5s] connected_ssid='%s'; IP='%s'; saved_ssid='%s'; saved_password=%s\n",
                      WiFi.status() == WL_CONNECTED ? WiFi.SSID().c_str() : "<not connected>",
                      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "<none>",
                      savedSSID.length() ? savedSSID.c_str() : "<empty>",
                      savedPassword.length() ? "<stored; hidden>" : "<empty>");

        if (prefsReady)
        {
            const bool prefEnabled = wifiPrefs.getBool("enabled", false);
            const String prefSSID = wifiPrefs.getString("ssid", "");
            const String prefPassword = wifiPrefs.getString("password", "");
            Serial.printf("[WiFi][5s] Preferences namespace='doomwifi': enabled=%s; ssid='%s'; password=%s\n",
                          prefEnabled ? "true" : "false",
                          prefSSID.length() ? prefSSID.c_str() : "<empty>",
                          prefPassword.length() ? "<stored; hidden>" : "<empty>");
            if (prefEnabled != savedWiFiEnabled || prefSSID != savedSSID || prefPassword != savedPassword)
            {
                Serial.println("[WiFi][WARN] RAM configuration differs from Preferences values.");
            }
        }
        else
        {
            Serial.println("[WiFi][ERROR] Preferences unavailable; values cannot be verified/persisted.");
        }

        if (radioOn != lastRadioOn || status != lastLoggedStatus)
        {
            Serial.printf("[WiFi][EVENT] Radio/status changed: mode=%s status=%s (%d)\n",
                          modeName(mode), statusName(status), static_cast<int>(status));
            lastRadioOn = radioOn;
            lastLoggedStatus = status;
        }
        Serial.println("[WiFi][5s] -----------------------");
    }

    static void clearPendingCredentials()
    {
        pendingSSID = "";
        pendingPassword = "";
        pendingCredentials = false;
    }

    static void stopScan()
    {
        if (scanRunning)
        {
            // scanDelete releases any results owned by the Wi-Fi driver. The next
            // scan is always started from a clean state.
            WiFi.scanDelete();
        }
        scanRunning = false;
    }

    static bool startHardwareScan()
    {
        WiFi.scanDelete();
        Serial.println("[WiFi][SCAN] Requesting asynchronous network scan...");
        const int16_t result = WiFi.scanNetworks(true, false); // async, visible APs
        Serial.printf("[WiFi][SCAN] scanNetworks() returned %d\n", static_cast<int>(result));
        if (result == WIFI_SCAN_FAILED)
        {
            scanRunning = false;
            Serial.println("[WiFi][SCAN][ERROR] Driver rejected scan start.");
            return false;
        }
        scanRunning = true;
        Serial.println("[WiFi][SCAN] Scan started/driver reports scan in progress.");
        return true;
    }

    static void collectScanResults()
    {
        const int16_t count = WiFi.scanComplete();
        if (count < 0)
        {
            return;
        }

        for (int16_t i = 0; i < count; ++i)
        {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0)
            {
                continue; // hidden SSIDs cannot be selected by name
            }

            AccessPoint ap;
            ap.ssid = ssid;

            const uint8_t *bssid = WiFi.BSSID(i);
            if (bssid)
                memcpy(ap.bssid, bssid, 6);
            else
                memset(ap.bssid, 0, 6);

            ap.rssi = WiFi.RSSI(i);
            ap.channel = WiFi.channel(i);
            ap.security = WiFi.encryptionType(i);

            int existing = -1;
            for (uint8_t j = 0; j < networkCount; ++j)
            {
                if (memcmp(networks[j].bssid, ap.bssid, 6) == 0)
                {
                    existing = static_cast<int>(j);
                    break;
                }
            }

            if (existing >= 0)
            {
                if (ap.rssi > networks[existing].rssi)
                {
                    networks[existing] = ap;
                }
            }
            else if (networkCount < MAX_NETWORKS)
            {
                networks[networkCount++] = ap;
            }
        }

        Serial.printf("[WiFi][SCAN] Scan results collected: %u unique visible SSIDs.\n",
                      static_cast<unsigned>(networkCount));
        WiFi.scanDelete();
        std::sort(networks, networks + networkCount,
                  [](const AccessPoint &a, const AccessPoint &b)
                  {
                      return a.rssi > b.rssi; // closer to zero is stronger
                  });
    }

    static void beginConnection(const String &ssid, const String &password,
                                bool isNewCredentialAttempt)
    {
        connectingSSID = ssid;
        if (isNewCredentialAttempt)
        {
            pendingSSID = ssid;
            pendingPassword = password;
            pendingCredentials = true;
        }
        else
        {
            clearPendingCredentials();
        }

        Serial.printf("[WiFi][CONNECT] Starting connection to '%s' (new_credentials=%s).\n",
                      ssid.c_str(), isNewCredentialAttempt ? "true" : "false");
        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(WIFI_POWER_7dBm);
        WiFi.setAutoReconnect(true);
        WiFi.disconnect(false, false);
        const wl_status_t beginStatus = WiFi.begin(ssid.c_str(), password.c_str());
        Serial.printf("[WiFi][CONNECT] WiFi.begin() returned status=%s (%d); mode=%s\n",
                      statusName(beginStatus), static_cast<int>(beginStatus), modeName(WiFi.getMode()));
        connectStartedAt = millis();
        currentState = State::Connecting;
    }

    static void finishSuccessfulConnection()
    {
        currentState = State::Connected;
        Serial.printf("[WiFi][CONNECT] VERIFIED connected: SSID='%s', IP='%s', RSSI=%ld dBm.\n",
                      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                      static_cast<long>(WiFi.RSSI()));

        if (pendingCredentials)
        {
            // Keep the successful credentials in RAM even if Preferences could
            // not be opened. Persist them whenever Preferences is available.
            savedSSID = pendingSSID;
            savedPassword = pendingPassword;
            savedWiFiEnabled = true;
            if (prefsReady)
            {
                const size_t ssidWritten = wifiPrefs.putString("ssid", savedSSID);
                const size_t passWritten = wifiPrefs.putString("password", savedPassword);
                const size_t enabledWritten = wifiPrefs.putBool("enabled", true);
                const bool verify = wifiPrefs.getString("ssid", "") == savedSSID &&
                                    wifiPrefs.getString("password", "") == savedPassword &&
                                    wifiPrefs.getBool("enabled", false);
                Serial.printf("[WiFi][PREF] Saved successful credentials (SSID bytes=%u, password bytes=%u, enabled bytes=%u); verify=%s.\n",
                              static_cast<unsigned>(ssidWritten), static_cast<unsigned>(passWritten),
                              static_cast<unsigned>(enabledWritten), verify ? "PASS" : "FAIL");
            }
            else
            {
                Serial.println("[WiFi][PREF][ERROR] Connected, but could not persist credentials: Preferences unavailable.");
            }
            clearPendingCredentials();
        }

        Serial.print("Wi-Fi connected; IP: ");
        Serial.println(WiFi.localIP());
    }

    void begin()
    {
        stopScan();
        networkCount = 0;
        clearPendingCredentials();
        currentState = State::Off;

        Serial.println("[WiFi][BOOT] Initializing Wi-Fi manager and opening Preferences...");
        prefsReady = wifiPrefs.begin("doomwifi", false);
        Serial.printf("[WiFi][BOOT] Preferences begin: %s\n", prefsReady ? "SUCCESS" : "FAILED");
        savedWiFiEnabled = false;
        savedSSID = "";
        savedPassword = "";

        if (prefsReady)
        {
            savedWiFiEnabled = wifiPrefs.getBool("enabled", false);
            savedSSID = wifiPrefs.getString("ssid", "");
            savedPassword = wifiPrefs.getString("password", "");
        }

        Serial.printf("[WiFi][BOOT] Loaded preferences: enabled=%s, saved_ssid='%s', password=%s\n",
                      savedWiFiEnabled ? "true" : "false",
                      savedSSID.length() ? savedSSID.c_str() : "<empty>",
                      savedPassword.length() ? "<stored; hidden>" : "<empty>");

        WiFi.persistent(false);
        WiFi.setAutoReconnect(true);

        if (!savedWiFiEnabled)
        {
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_OFF);
            currentState = State::Off;
            logRadioVerification("BOOT Wi-Fi disabled");
            return;
        }

        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(WIFI_POWER_7dBm);
        logRadioVerification("BOOT Wi-Fi enabled");

        if (savedSSID.length() > 0)
        {
            beginConnection(savedSSID, savedPassword, false);
        }
        else
        {
            currentState = State::Idle;
            Serial.println("[WiFi][BOOT] Wi-Fi enabled but no saved SSID; scan is available.");
        }
        lastDebugAt = millis();
    }

    void setEnabled(bool enabled)
    {
        Serial.printf("[WiFi][MENU] Toggle requested: %s\n", enabled ? "ON" : "OFF");
        savedWiFiEnabled = enabled;
        if (prefsReady)
        {
            const size_t written = wifiPrefs.putBool("enabled", enabled);
            const bool readBack = wifiPrefs.getBool("enabled", !enabled);
            Serial.printf("[WiFi][PREF] Write enabled=%s returned=%u; read-back=%s\n",
                          enabled ? "true" : "false", static_cast<unsigned>(written),
                          readBack ? "true" : "false");
            if (readBack != enabled)
                Serial.println("[WiFi][PREF][ERROR] Enabled preference did not verify!");
        }
        else
        {
            Serial.println("[WiFi][PREF][ERROR] Cannot save enabled preference: Preferences not ready.");
        }

        if (!enabled)
        {
            stopScan();
            networkCount = 0;
            clearPendingCredentials();
            WiFi.disconnect(false, false);
            WiFi.mode(WIFI_OFF);
            currentState = State::Off;
            logRadioVerification("MENU toggle OFF");
            return;
        }

        // Idempotent enable: don't interrupt an already healthy connection or
        // restart an active connection attempt.
        if (WiFi.getMode() != WIFI_OFF && WiFi.status() == WL_CONNECTED)
        {
            currentState = State::Connected;
            logRadioVerification("MENU toggle ON (already connected; no restart needed)");
            return;
        }
        if (currentState == State::Connecting && WiFi.getMode() != WIFI_OFF)
        {
            logRadioVerification("MENU toggle ON (connection already in progress)");
            Serial.printf("[WiFi][CONNECT] Existing attempt for '%s' remains active.\n",
                          connectingSSID.c_str());
            return;
        }

        WiFi.mode(WIFI_STA);
        WiFi.setTxPower(WIFI_POWER_7dBm);
        WiFi.setAutoReconnect(true);
        logRadioVerification("MENU toggle ON");

        if (savedSSID.length() > 0)
        {
            Serial.printf("[WiFi][RECONNECT] Attempting saved network '%s'.\n", savedSSID.c_str());
            beginConnection(savedSSID, savedPassword, false);
        }
        else
        {
            currentState = State::Idle;
            Serial.println("[WiFi][RECONNECT] No saved SSID; Wi-Fi is on but not connected.");
        }
    }

    bool isEnabled()
    {
        // Wi-Fi being in STA mode is not the same as being connected. This reports
        // the user's enabled preference plus the actual radio mode.
        return savedWiFiEnabled && WiFi.getMode() != WIFI_OFF;
    }

    void startScan()
    {
        Serial.printf("[WiFi][SCAN] User requested scan. enabled=%s mode=%s state=%s\n",
                      isEnabled() ? "true" : "false", modeName(WiFi.getMode()), stateName(currentState));
        if (!isEnabled())
        {
            setEnabled(true);
        }
        if (!isEnabled())
        {
            currentState = State::Off;
            return;
        }
        if (currentState == State::Scanning)
        {
            return;
        }

        // Choosing "Connect Wi-Fi" means scan now. If enabling Wi-Fi started an
        // automatic reconnect, cancel that attempt so it cannot block scanning.
        if (currentState == State::Connecting)
        {
            WiFi.disconnect(false, false);
            clearPendingCredentials();
            currentState = State::Idle;
        }

        stopScan();
        networkCount = 0; // never show stale networks as results from this scan
        scanStartedAt = millis();
        Serial.printf("[WiFi][SCAN] Clearing old results. actual_mode=%s status=%s (%d)\n",
                      modeName(WiFi.getMode()), statusName(WiFi.status()), static_cast<int>(WiFi.status()));

        if (startHardwareScan())
        {
            currentState = State::Scanning;
        }
        else
        {
            currentState = State::Failed;
            Serial.println("[WiFi][SCAN][ERROR] Unable to start scan; check radio mode and driver status.");
        }
    }

    void update()
    {
        debugTick();

        // The persisted preference is authoritative. If Wi-Fi is meant to be ON
        // but the driver mode unexpectedly became OFF, restore the radio instead
        // of silently remaining stuck in OFF forever.
        if (!savedWiFiEnabled)
        {
            if (WiFi.getMode() != WIFI_OFF)
            {
                Serial.println("[WiFi][RECOVERY] Preference says OFF; forcing radio OFF.");
                WiFi.disconnect(false, false);
                WiFi.mode(WIFI_OFF);
            }
            stopScan();
            networkCount = 0;
            currentState = State::Off;
            return;
        }

        if (WiFi.getMode() == WIFI_OFF)
        {
            Serial.println("[WiFi][RECOVERY][WARN] Preference says ON but radio mode is OFF; restoring STA mode.");
            WiFi.mode(WIFI_STA);
            WiFi.setTxPower(WIFI_POWER_7dBm);
            WiFi.setAutoReconnect(true);
            logRadioVerification("RECOVERY restore radio");
            if (savedSSID.length() > 0)
            {
                beginConnection(savedSSID, savedPassword, false);
            }
            else
            {
                currentState = State::Idle;
            }
        }

        // Reconcile state against the driver first. This also catches a connection
        // that succeeds via ESP32 auto-reconnect while our state was Idle/Failed.
        // A scan can run while already associated with an AP. Do not let the
        // connected-state shortcut prevent the asynchronous scan from completing.
        if (currentState != State::Scanning && WiFi.status() == WL_CONNECTED)
        {
            if (currentState != State::Connected)
            {
                finishSuccessfulConnection();
            }
            return;
        }

        if (currentState == State::Connected && WiFi.status() != WL_CONNECTED)
        {
            // Link dropped. Keep Wi-Fi enabled; ESP32 auto-reconnect may recover it.
            Serial.printf("[WiFi][EVENT] Link lost; driver_status=%s (%d). Waiting for auto-reconnect.\n",
                          statusName(WiFi.status()), static_cast<int>(WiFi.status()));
            currentState = State::Idle;
        }

        if (currentState == State::Scanning)
        {
            if (scanRunning)
            {
                const int16_t result = WiFi.scanComplete();
                if (result >= 0)
                {
                    Serial.printf("[WiFi][SCAN] Driver reports %d scan entries.\n", static_cast<int>(result));
                    collectScanResults();
                    scanRunning = false;
                }
                else if (result == WIFI_SCAN_FAILED)
                {
                    Serial.println("[WiFi][SCAN][WARN] scanComplete() reports WIFI_SCAN_FAILED; will retry until scan window expires.");
                    scanRunning = false;
                    WiFi.scanDelete();
                }
            }

            if (static_cast<uint32_t>(millis() - scanStartedAt) >= SCAN_DURATION_MS)
            {
                // Capture a completed scan before finalizing the scan session.
                if (scanRunning)
                {
                    const int16_t result = WiFi.scanComplete();
                    if (result >= 0)
                    {
                        collectScanResults();
                    }
                    else
                    {
                        WiFi.scanDelete();
                    }
                    scanRunning = false;
                }
                currentState = State::ScanComplete;
                Serial.printf("[WiFi][SCAN] Scan session finished: %u unique networks found.\n",
                              static_cast<unsigned>(networkCount));
                if (networkCount == 0)
                {
                    Serial.println("[WiFi][SCAN][WARN] No visible SSIDs found. Check 2.4 GHz availability, signal, hidden SSIDs, and board antenna.");
                }
                return;
            }

            // Repeat short driver scans during the configured scan window. Keep
            // already collected results, merging duplicate SSIDs by strongest RSSI.
            if (!scanRunning)
            {
                if (!startHardwareScan())
                {
                    // Remain in Scanning and retry on the next loop iteration.
                    return;
                }
            }
            return;
        }

        if (currentState == State::Connecting)
        {
            if (WiFi.status() == WL_CONNECTED)
            {
                finishSuccessfulConnection();
                return;
            }
            if (static_cast<uint32_t>(millis() - connectStartedAt) >= CONNECT_TIMEOUT_MS)
            {
                Serial.printf("[WiFi][CONNECT][ERROR] Timeout connecting to '%s'; driver_status=%s (%d).\n",
                              connectingSSID.c_str(), statusName(WiFi.status()), static_cast<int>(WiFi.status()));
                WiFi.disconnect(false, false);
                clearPendingCredentials(); // don't overwrite last known-good creds
                currentState = State::Failed;
            }
        }
    }

    // State getState() {
    //     // Return the live connection state even if update() has not run yet.
    //     if (!isEnabled()) return State::Off;
    //     if (WiFi.status() == WL_CONNECTED) return State::Connected;
    //     return currentState;
    // }

    State getState()
    {
        if (!isEnabled())
            return State::Off;

        // Scan states must take priority over the live connection status.
        if (currentState == State::Scanning ||
            currentState == State::ScanComplete)
        {
            return currentState;
        }

        if (WiFi.status() == WL_CONNECTED)
            return State::Connected;

        return currentState;
    }

    uint8_t getNetworkCount()
    {
        return networkCount;
    }

    const AccessPoint *getNetwork(uint8_t index)
    {
        if (index >= networkCount)
            return nullptr;
        return &networks[index];
    }

    uint8_t getScanProgress()
    {
        if (currentState != State::Scanning)
        {
            return currentState == State::ScanComplete ? 100 : 0;
        }
        const uint32_t elapsed = static_cast<uint32_t>(millis() - scanStartedAt);
        if (elapsed >= SCAN_DURATION_MS)
            return 100;
        return static_cast<uint8_t>((elapsed * 100UL) / SCAN_DURATION_MS);
    }

    bool connect(const String &ssid, const String &password)
    {
        if (!isEnabled() || currentState == State::Scanning || ssid.length() == 0)
        {
            return false;
        }

        stopScan();
        savedWiFiEnabled = true;
        if (prefsReady)
            wifiPrefs.putBool("enabled", true);
        beginConnection(ssid, password, true);
        return true;
    }

    void disconnect()
    {
        Serial.println("[WiFi][MENU] Explicit disconnect requested.");
        if (!isEnabled())
        {
            Serial.println("[WiFi][MENU] Disconnect ignored: Wi-Fi is off.");
            return;
        }
        stopScan();
        clearPendingCredentials();
        WiFi.setAutoReconnect(false); // explicit disconnect should stay disconnected
        WiFi.disconnect(false, false);
        currentState = State::Idle;
        logRadioVerification("MENU disconnect (radio should remain on)");
    }

    String getSSID()
    {
        return isEnabled() && WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("");
    }

    String getIP()
    {
        return isEnabled() && WiFi.status() == WL_CONNECTED
                   ? WiFi.localIP().toString()
                   : String("");
    }

    int32_t getRSSI()
    {
        return isEnabled() && WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    }

    String getSecurityName(wifi_auth_mode_t security)
    {
        switch (security)
        {
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

} // namespace WifiManager
