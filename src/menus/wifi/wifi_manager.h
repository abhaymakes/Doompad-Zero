#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace WifiManager {

    constexpr uint8_t TX_POWER_DBM = 10;
    constexpr uint32_t SCAN_DURATION_MS = 5000;
    constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;
    constexpr uint8_t MAX_NETWORKS = 30;

    enum class State : uint8_t {
        Off,
        Idle,
        Scanning,
        ScanComplete,
        Connecting,
        Connected,
        Failed
    };

    struct AccessPoint {
        String ssid;
        int32_t rssi;
        uint8_t channel;
        wifi_auth_mode_t security;
    };

    void begin();
    void setEnabled(bool enabled);
    bool isEnabled();

    void startScan();
    void update();

    State getState();
    uint8_t getNetworkCount();
    const AccessPoint* getNetwork(uint8_t index);
    uint8_t getScanProgress();

    bool connect(const String& ssid, const String& password);
    void disconnect();

    String getSSID();
    String getIP();
    int32_t getRSSI();
    String getSecurityName(wifi_auth_mode_t security);

}