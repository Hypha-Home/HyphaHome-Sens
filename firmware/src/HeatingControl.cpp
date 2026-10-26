// ===================================================================
// File:            HeatingControl.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#include "HeatingControl.h"

namespace HeatingControl {

    // Von außen geschrieben (requestSync), vom Task gelesen.
    static volatile bool syncRequested = false;

    static portMUX_TYPE statusLock = portMUX_INITIALIZER_UNLOCKED;
    static Status shared;

    // ---- Stand des Tasks ----
    static Desired desired = Desired::Unknown;
    static Actual  actual = Actual::Unknown;
    static uint32_t lastChange = 0;         // Zeitpunkt, zu dem der Soll-Zustand zuletzt gewechselt hat
    static bool     haveChange = false;     // false: seit dem Start noch nicht gewechselt, also keine Sperre
    static uint32_t lastValidTemp = 0;      // letzter Zeitpunkt mit gültiger Temperatur
    static uint32_t lastSync = 0;
    static bool     synced = false;
    static bool     wasEnabled = false;

    /// Ein Kanal, wie er für eine Abfrage festgehalten wird.
    enum class Channel : uint8_t { Unknown, Off, On, Unreachable };

    struct OutputRef {
        String  key;
        uint8_t channel = 0;
    };

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------


    static void publish(const Status& value) {
        portENTER_CRITICAL(&statusLock);
        shared = value;
        portEXIT_CRITICAL(&statusLock);
    }

    /// @brief Fragt alle Kanäle ab, gleicht sie an den Soll-Zustand an und bestimmt den Ist-Zustand.
    static void syncOutputs() {
        // Die Ausgänge festhalten, bevor der erste Aufruf blockiert: Die Konfiguration kann sich währenddessen ändern.
        OutputRef refs[CONTROL_MAX_OUTPUTS];
        const uint8_t count = min<uint8_t>(Config::controls.heating.outputs.count, CONTROL_MAX_OUTPUTS);
        for (uint8_t i = 0; i < count; i++) {
            refs[i].key = Config::controls.heating.outputs.items[i].shelly;
            refs[i].channel = Config::controls.heating.outputs.items[i].channel;
        }

        Channel state[CONTROL_MAX_OUTPUTS];
        ShellySwitch::Target targets[CONTROL_MAX_OUTPUTS];

        for (uint8_t i = 0; i < count; i++) {
            state[i] = Channel::Unreachable;
            const Config::ShellyCfg* shelly = Config::findShelly(refs[i].key);
            if (!shelly) {
                Serial.printf("[HEIZUNG] Ausgang %u: kein eingetragener Shelly (%s)\n", (unsigned)i, refs[i].key.c_str());
                continue;
            }
            targets[i].address  = shelly->address;
            targets[i].password = shelly->password;
            targets[i].gen      = shelly->gen;

            bool on = false;
            if (ShellySwitch::read(targets[i], refs[i].channel, on)) {
                state[i] = on ? Channel::On : Channel::Off;
            }
        }

        // Soll aus dem Ist übernehmen, solange es unbekannt ist.
        if (desired == Desired::Unknown) {
            bool allKnown = count > 0;
            bool anyOn = false, anyOff = false;
            for (uint8_t i = 0; i < count; i++) {
                if (state[i] == Channel::Unreachable) allKnown = false;
                else if (state[i] == Channel::On) anyOn = true;
                else anyOff = true;
            }
            if (allKnown) {
                desired = (anyOn && !anyOff) ? Desired::On : Desired::Off;
                haveChange = false;   // wann zuletzt geschaltet wurde, weiß nach einem Start niemand
                Serial.printf("[HEIZUNG] Soll aus Ist uebernommen: %s%s\n", key(desired),
                              (anyOn && anyOff) ? " (Kanaele uneins)" : "");
            }
        }

        // Angleichen.
        if (desired != Desired::Unknown) {
            const Channel wanted = (desired == Desired::On) ? Channel::On : Channel::Off;
            for (uint8_t i = 0; i < count; i++) {
                if (state[i] == Channel::Unreachable || state[i] == wanted) continue;
                if (ShellySwitch::write(targets[i], refs[i].channel, desired == Desired::On)) {
                    state[i] = wanted;   // beim nächsten Durchlauf wird es gegengeprüft
                } else {
                    state[i] = Channel::Unreachable;
                }
            }
        }

        bool anyUnreachable = false, anyOn = false, anyOff = false;
        for (uint8_t i = 0; i < count; i++) {
            if (state[i] == Channel::Unreachable) anyUnreachable = true;
            else if (state[i] == Channel::On) anyOn = true;
            else anyOff = true;
        }
        if (count == 0)           actual = Actual::Unknown;
        else if (anyUnreachable)  actual = Actual::Unreachable;
        else if (anyOn && anyOff) actual = Actual::Mixed;
        else                      actual = anyOn ? Actual::On : Actual::Off;

        lastSync = millis();
        synced = true;
    }

    /// @brief setzte alle Parameter zurück
    static void forget() {
        desired = Desired::Unknown;
        actual = Actual::Unknown;
        haveChange = false;
        synced = false;
    }

    /// @brief Task Loop
    static void task(void*) {
        uint32_t nextSync = 0;

        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(HEATINGCONTROL_TICK_MS));

            const Config::HeatingCfg& cfg = Config::controls.heating;
            const bool enabled = cfg.enabled && cfg.outputs.count > 0;
            const uint32_t now = millis();

            if (!enabled) {
                if (wasEnabled) {
                    forget();
                    Serial.println(F("[HEIZUNG] Regelung aus"));
                }
                wasEnabled = false;
                syncRequested = false;
                publish(Status{});
                continue;
            }

            if (!wasEnabled) {
                wasEnabled = true;
                nextSync = now;                 // gleich abfragen
                lastValidTemp = now;            // der Sensorverlust zählt ab jetzt
                Serial.println(F("[HEIZUNG] Regelung ein"));
            }

            const bool sensorOk = Config::sensors.sht41.enabled && MySHT41::isConnected && !isnan(MySHT41::temperatureSmoothed);
            if (sensorOk) lastValidTemp = now;

            bool syncNow = syncRequested;
            syncRequested = false;

            if (desired != Desired::Unknown) {
                if (sensorOk) {
                    // Wunsch: an, solange es kälter als das Ziel ist.
                    const bool wantOn = MySHT41::temperatureSmoothed < cfg.targetTemp;
                    const bool isOn = (desired == Desired::On);
                    if (wantOn != isOn) {
                        // Der Wunsch ist ein anderer als der Soll-Zustand. Geschaltet wird erst, wenn
                        // die Sperre seit dem letzten Schalten abgelaufen ist: nach dem Ausschalten
                        // gilt die Einschalthysterese, nach dem Einschalten die Ausschalthysterese.
                        const uint32_t lockMs = (uint32_t)(wantOn ? cfg.hysteresisOnMin : cfg.hysteresisOffMin) * 60000UL;
                        if (!haveChange || now - lastChange >= lockMs) {
                            desired = wantOn ? Desired::On : Desired::Off;
                            lastChange = now;
                            haveChange = true;
                            syncNow = true;
                            Serial.printf("[HEIZUNG] Soll -> %s (%.1f C, Ziel %.1f C)\n", key(desired),
                                          (double)MySHT41::temperatureSmoothed, (double)cfg.targetTemp);
                        }
                    }
                } else {
                    // Ohne Messwert wird nichts neu entschieden. Dauert es zu lange, Sicherheitsabschaltung
                    // (ohne Rücksicht auf die Sperre).
                    const uint32_t lossMs = (uint32_t)HEATING_SENSOR_LOSS_OFF_MIN * 60000UL;
                    if (desired == Desired::On && now - lastValidTemp >= lossMs) {
                        desired = Desired::Off;
                        lastChange = now;
                        haveChange = true;
                        syncNow = true;
                        Serial.println(F("[HEIZUNG] Kein Messwert, Soll -> AUS (Sicherheit)"));
                    }
                }
            }

            if (syncNow || (int32_t)(now - nextSync) >= 0) {
                syncOutputs();
                nextSync = millis() + HEATING_SYNC_INTERVAL_MS;
            }

            Status st;
            st.enabled = true;
            st.sensorOk = sensorOk;
            st.desired = desired;
            st.actual = actual;
            // Ein Wechsel steht an, wenn der Wunsch vom Soll abweicht, die Sperre ihn aber noch hält.
            if (sensorOk && desired != Desired::Unknown && haveChange) {
                const bool wantOn = MySHT41::temperatureSmoothed < cfg.targetTemp;
                if (wantOn != (desired == Desired::On)) {
                    const uint32_t lockMs = (uint32_t)(wantOn ? cfg.hysteresisOnMin : cfg.hysteresisOffMin) * 60000UL;
                    const uint32_t elapsed = millis() - lastChange;
                    if (elapsed < lockMs) st.pendingSeconds = (int32_t)((lockMs - elapsed + 999) / 1000);
                }
            }
            if (synced) st.lastSyncAgoSeconds = (int32_t)((millis() - lastSync) / 1000);
            publish(st);
        }
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void begin() {
        // 8 KB: WiFiClient, Antworttext und JSON-Dokument der Shelly-Abfragen liegen auf dem Stack.
        if (xTaskCreate(task, "heating", 8192, nullptr, 1, nullptr) != pdPASS) {
            Serial.println(F("[HEIZUNG] Task konnte nicht gestartet werden"));
        }
    }

    void requestSync() {
        syncRequested = true;
    }

    Status status() {
        Status copy;
        portENTER_CRITICAL(&statusLock);
        copy = shared;
        portEXIT_CRITICAL(&statusLock);
        return copy;
    }

    const char* key(Desired value) {
        switch (value) {
            case Desired::On:  return "on";
            case Desired::Off: return "off";
            default:           return "unknown";
        }
    }

    const char* key(Actual value) {
        switch (value) {
            case Actual::On:          return "on";
            case Actual::Off:         return "off";
            case Actual::Mixed:       return "mixed";
            case Actual::Unreachable: return "unreachable";
            default:                  return "unknown";
        }
    }
}
