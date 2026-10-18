#pragma once
#include <Arduino.h>

namespace DeauthMonitor {

    enum class State : uint8_t { Idle, Monitoring, Unsupported };

    enum class Event : uint8_t {
        None = 0,
        DeauthFromAp,
        DeauthFromClient,
        DisassocFromAp,
        DeauthBroadcast
    };

    struct Counters {
        uint32_t mgmtTotal;
        uint32_t deauthTotal;
        uint32_t deauthFromAp;
        uint32_t deauthFromClient;
        uint32_t disassocTotal;
        uint32_t disassocFromAp;
        uint32_t broadcastDeauth;
        uint32_t distinctClients;
        uint16_t lastReason;
        int8_t   targetRssi;
        uint8_t  targetChannel;
        uint8_t  actualChannel;
        bool     channelMismatch;
        bool     hasClient;
        uint8_t  lastClient[6];
        Event    lastEvent;
        uint32_t sinceLastEventMs;
        bool     activity;

        // transmit side
        bool     txActive;
        bool     txBroadcast;
        uint16_t txReason;
        uint32_t txSent;
        uint32_t txSelfHeard;   // our own frames received back = TX is radiating
    };

    bool start(const char* ssid, const uint8_t* bssid, uint8_t channel, int8_t rssi);
    void stop();
    State state();
    Counters counters();

    void updateTransmit();
    void setTransmit(bool enabled);
    bool transmitting();
    void burst(uint8_t frames);
    void setReason(uint16_t reason);
    void setBroadcast(bool broadcast);
    bool broadcast();
    void printRxDiagnostics();
    bool runTxDiagnostic();

}