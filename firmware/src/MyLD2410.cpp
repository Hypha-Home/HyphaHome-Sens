// ===================================================================
// File:            MyLD2410.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "MyLD2410.h"

namespace MyLD2410 {
    ld2410 radar;
    bool isConnected = false;
    bool presence = false;
    bool movingTarget = false;
    bool stationaryTarget = false;
    uint16_t movingDistance = 0;
    uint8_t movingEnergy = 0;
    uint16_t stationaryDistance = 0;
    uint8_t stationaryEnergy = 0;

    // Sensor Health
    static SensorHealth::State health;
    static const char* TAG = "LD2410";

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    /// Hält eine Meldung nach der letzten Erkennung noch eine Weile aufrecht.
    struct Hold {
        bool seen = false;       // seit dem Start schon einmal erkannt
        uint32_t lastSeen = 0;   // Zeitpunkt der letzten Erkennung

        /// @param detected  Rohwert des Sensors
        /// @param now       aktuelle Zeit in ms
        /// @param seconds   Haltezeit, 0 = nicht halten
        /// @return Rohwert, oder true solange die Haltezeit nach der letzten Erkennung läuft
        bool apply(bool detected, uint32_t now, uint16_t seconds) {
            if (detected) {
                seen = true;
                lastSeen = now;
                return true;
            }
            return seen && (now - lastSeen < (uint32_t)seconds * 1000UL);
        }
    };

    static Hold movingHold;
    static Hold stationaryHold;
    static Hold presenceHold;

    ///@brief Schreibt die Schwellen eines Gates in den Sensor. Gate 0xFF setzt laut Protokoll alle Gates.
    static bool setGateSensitivity(uint8_t gate, uint8_t movingSensitivity, uint8_t stationarySensitivity) {
        if (!isConnected) return false;

        bool ok = radar.setGateSensitivityThreshold(gate, movingSensitivity, stationarySensitivity);
        Serial.print(F("[LD2410] Gate "));
        Serial.print(gate);
        Serial.print(F(" Empfindlichkeit setzen: "));
        Serial.println(ok ? F("OK") : F("FEHLER"));
        return ok;
    }

    // ---- Entwicklermodus ----
    static bool engineeringOn = false;        // Sensor sendet gerade Engineering-Frames
    static uint32_t debugLeaseUntil = 0;      // 0 = nicht angefordert
    static uint32_t debugRetryAt = 0;         // frueheste Zeit fuer den naechsten Startversuch
    static bool thresholdsDirty = false;      // Schwellen im Sensor wurden veraendert
    static DebugGate debugGates[LD2410_GATE_COUNT];
    static uint8_t debugGateCount = 0;
    static uint16_t debugDetectionDistance = 0;

    /// @brief Startet UART und Radar inklusive Hintergrund-Task, ohne Ausgabe auf Serial.
    /// @return true bei Erfolg
    static bool startSensor() {
        Serial2.begin(256000, SERIAL_8N1, PIN_LD2410_RX, PIN_LD2410_TX);
        if (!radar.begin(Serial2)) return false;
        // Liest den UART-Puffer kontinuierlich im Hintergrund aus, damit er
        // bei einem zu hohen Messintervallen nicht überläuft.
        radar.autoReadTask();
        return true;
    }

    // --------------------------------------------------------------
    // Entwicklermodus
    // --------------------------------------------------------------

    /// Schwellen aus dem Sensor holen und den Engineering-Modus (neu) einschalten.
    /// Reihenfolge mit Absicht: erst lesen, dann einschalten - der Konfigurationsmodus, in dem
    /// das Lesen laeuft, darf den Engineering-Modus nicht wieder abbrechen.
    static bool startEngineering() {
        if (!radar.requestCurrentConfiguration()) return false;
        debugGateCount = radar.max_gate + 1;
        if (debugGateCount > LD2410_GATE_COUNT) debugGateCount = LD2410_GATE_COUNT;
        for (uint8_t g = 0; g < debugGateCount; g++) {
            debugGates[g].movingThreshold     = radar.motion_sensitivity[g];
            debugGates[g].stationaryThreshold = radar.stationary_sensitivity[g];
        }
        if (!radar.requestStartEngineeringMode()) return false;
        thresholdsDirty = false;
        return true;
    }

    /// Schaltet den Entwicklermodus nach Anforderung ein bzw. nach Ablauf aus und nimmt die
    /// Messwerte der Zonen auf. Laeuft aus read(), also im Takt der Hauptschleife (200 ms).
    static void serviceDebug(uint32_t now) {
        const bool wanted = debugLeaseUntil != 0 && (int32_t)(debugLeaseUntil - now) > 0;

        if (!wanted) {
            debugLeaseUntil = 0;
            if (engineeringOn) {
                radar.requestEndEngineeringMode();
                engineeringOn = false;
                Serial.println(F("[LD2410] Entwicklermodus aus"));
            }
            return;
        }

        if (!engineeringOn || thresholdsDirty) {
            if ((int32_t)(now - debugRetryAt) < 0) return;
            if (!startEngineering()) {
                debugRetryAt = now + 2000;
                Serial.println(F("[LD2410] Entwicklermodus: Start fehlgeschlagen"));
                return;
            }
            if (!engineeringOn) {
                // Nach einem Neustart sind Hoechstwerte und Zeitpunkte nicht mehr aussagekraeftig.
                debugResetPeaks();
                Serial.println(F("[LD2410] Entwicklermodus ein"));
            }
            engineeringOn = true;
            return;
        }

        if (!radar.engineeringRetrieved()) return;

        debugDetectionDistance = radar.detectionDistance();
        for (uint8_t g = 0; g < debugGateCount; g++) {
            DebugGate& gate = debugGates[g];
            gate.moving     = radar.movingEnergyAtGate(g);
            gate.stationary = radar.stationaryEnergyAtGate(g);
            if (gate.moving > gate.movingPeak) gate.movingPeak = gate.moving;
            if (gate.stationary > gate.stationaryPeak) gate.stationaryPeak = gate.stationary;
            // Der Sensor meldet erst, wenn die Energie ueber der Schwelle liegt.
            if (gate.moving > gate.movingThreshold)         gate.movingTriggeredAt = now ? now : 1;
            if (gate.stationary > gate.stationaryThreshold) gate.stationaryTriggeredAt = now ? now : 1;
        }
    }

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool probe() {
        return startSensor();
    }

    void init() {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.ld2410.enabled) {
            isConnected = false;
            Serial.println(F("[LD2410] Init: uebersprungen (deaktiviert)"));
            return;
        }

        isConnected = startSensor();
        Serial.print(F("[LD2410] Init: "));
        Serial.println(isConnected ? F("OK") : F("FEHLER"));

        if (isConnected) applyGateConfig();
    }

    bool applyGateConfig() {
        if (!isConnected) return false;

        // Zuerst holen, was im Sensor steht. Der LD2410 legt seine Einstellungen in eigenem
        // Flash ab und behält sie über das Abschalten hinweg — ohne Vergleich würde bei jedem
        // Hochfahren alles neu geschrieben und damit bei jedem Start ein Flash-Zyklus je Gate
        // verbraucht, obwohl sich meist gar nichts geändert hat.
        if (!radar.requestCurrentConfiguration()) {
            Serial.println(F("[LD2410] Gates: Konfiguration nicht lesbar, nichts geschrieben"));
            return false;
        }

        uint8_t changed = 0;
        bool allOk = true;

        for (uint8_t gate = 0; gate < LD2410_GATE_COUNT; gate++) {
            // Der Sensor bestimmt, wie viele Zonen es wirklich gibt. Das begrenzt zugleich den
            // Zugriff auf die Felder der Bibliothek, die fest 9 Einträge hat.
            if (gate > radar.max_gate) break;

            const Config::Ld2410GateCfg& cfg = Config::sensors.ld2410.gates[gate];
            const uint8_t currentMoving     = radar.motion_sensitivity[gate];
            const uint8_t currentStationary = radar.stationary_sensitivity[gate];

            // Nicht eingestellte Werte bleiben, wie sie sind. Die Bibliothek kann Bewegung und
            // Stillstand nur gemeinsam schreiben, deshalb wird für die leere Seite der Wert
            // eingesetzt, der ohnehin schon im Sensor steht.
            const uint8_t moving = cfg.moving == LD2410_SENSITIVITY_UNSET
                                   ? currentMoving : cfg.moving;
            const uint8_t stationary = cfg.stationary == LD2410_SENSITIVITY_UNSET
                                       ? currentStationary : cfg.stationary;

            if (moving == currentMoving && stationary == currentStationary) continue;

            allOk &= setGateSensitivity(gate, moving, stationary);
            changed++;
        }

        if (changed == 0) {
            Serial.println(F("[LD2410] Gates: stimmen bereits mit der Konfiguration ueberein"));
        } else {
            Serial.printf("[LD2410] Gates: %u geaendert\n", changed);
        }
        return allOk;
    }

    void read(bool print) {
        // Abbrechen, wenn der Sensor in der Konfiguration deaktiviert ist. Dann wird auch nicht versucht, ihn zu starten.
        if (!Config::sensors.ld2410.enabled) {
            if (isConnected) {
                isConnected = false;
                Serial.println(F("[LD2410] deaktiviert"));
            }
            return;
        }

        // Der LD2410 hängt am UART, es gibt also kein ACK wie bei I2C. Erkennungsmerkmal ist
        // stattdessen, ob überhaupt noch gültige Frames ankommen.
        //
        // Wichtig: presenceDetected() und die übrigen Getter lesen nur aus dem Puffer, den der
        // Hintergrund-Task füllt. Ohne diese Prüfung liefern sie nach einem Ausfall einfach die
        // zuletzt empfangenen Werte weiter - ein abgezogener Sensor hätte also dauerhaft
        // "Bewegung erkannt" gemeldet, ohne dass es jemand bemerkt.

        if (!radar.isAutoReadTaskRunning()) {
            // Ohne laufenden Task kommen gar keine Daten an -> kompletter Neustart nötig.
            if (SensorHealth::retryDue(health) && startSensor()) {
                engineeringOn = false;   // ein neu gestarteter Sensor sendet wieder Basis-Frames
                SensorHealth::reportOk(isConnected, health, TAG);
                applyGateConfig();
            } else {
                SensorHealth::reportFail(isConnected, health, TAG);
                return;
            }
        }

        // isConnected() ist true, solange das letzte gültige Frame weniger als 100 ms her ist.
        // Ist es älter, versucht die Methode selbst noch einmal zu lesen. Das kollidiert nicht
        // mit dem Hintergrund-Task: der ruft read_frame_() nur auf, wenn tatsächlich neue Bytes
        // angekommen sind - und genau dann ist der Zeitstempel ohnehin frisch.
        if (!radar.isConnected()) {
            SensorHealth::reportFail(isConnected, health, TAG);
            return;
        }

        SensorHealth::reportOk(isConnected, health, TAG);

        serviceDebug(millis());

        // Der LD2410 meldet Bewegung, Stillstand und Präsenz über UART nur im Augenblick der
        // Erkennung, seine eigene Nachlaufzeit wirkt dort nicht. Deshalb wird sie hier nachgebildet
        const uint32_t now = millis();
        const Config::Ld2410Cfg& cfg = Config::sensors.ld2410;
        presence         = presenceHold.apply(radar.presenceDetected(), now, cfg.presenceHoldSeconds);
        movingTarget     = movingHold.apply(radar.movingTargetDetected(), now, cfg.movingHoldSeconds);
        stationaryTarget = stationaryHold.apply(radar.stationaryTargetDetected(), now, cfg.stationaryHoldSeconds);
        movingDistance = radar.movingTargetDistance();
        movingEnergy = radar.movingTargetEnergy();
        stationaryDistance = radar.stationaryTargetDistance();
        stationaryEnergy = radar.stationaryTargetEnergy();

        // Ausgabe auf Serial, wenn gewünscht
        if (print) {
            Serial.print(F("Präsenz: "));
            Serial.print(presence ? F("JA") : F("NEIN"));
            if (movingTarget) {
                Serial.print(F("  Bewegt: "));
                Serial.print(movingDistance);
                Serial.print(F("cm/"));
                Serial.print(movingEnergy);
                Serial.print(F("%"));
            }
            if (stationaryTarget) {
                Serial.print(F("  Statisch: "));
                Serial.print(stationaryDistance);
                Serial.print(F("cm/"));
                Serial.print(stationaryEnergy);
                Serial.print(F("%"));
            }
            Serial.println();
        }
    }

    void printConfiguration() {
        if (!isConnected) return;
        if (!radar.requestCurrentConfiguration()) {
            Serial.println(F("[LD2410] Konfiguration konnte nicht abgefragt werden"));
            return;
        }

        Serial.print(F("[LD2410] Max Gate: "));
        Serial.print(radar.max_gate);
        Serial.print(F("  Max bewegt: "));
        Serial.print(radar.max_moving_gate);
        Serial.print(F("  Max statisch: "));
        Serial.print(radar.max_stationary_gate);
        Serial.print(F("  Idle-Time: "));
        Serial.print(radar.sensor_idle_time);
        Serial.println(F("s"));

        for (uint8_t gate = 0; gate <= radar.max_gate; gate++) {
            Serial.print(F("  Gate "));
            Serial.print(gate);
            Serial.print(F(": bewegt="));
            Serial.print(radar.motion_sensitivity[gate]);
            Serial.print(F(" statisch="));
            Serial.println(radar.stationary_sensitivity[gate]);
        }
    }

    // --------------------------------------------------------------
    // Entwicklermodus
    // --------------------------------------------------------------

    void debugKeepAlive() {
        debugLeaseUntil = millis() + LD2410_DEBUG_LEASE_MS;
    }

    void debugStop() {
        debugLeaseUntil = 0;
    }

    void debugResetPeaks() {
        for (uint8_t g = 0; g < LD2410_GATE_COUNT; g++) {
            debugGates[g].movingPeak = 0;
            debugGates[g].stationaryPeak = 0;
            debugGates[g].movingTriggeredAt = 0;
            debugGates[g].stationaryTriggeredAt = 0;
        }
    }

    bool debugSnapshot(DebugSnapshot& out) {
        out.active = engineeringOn && radar.engineeringRetrieved() && isConnected;
        out.gateCount = debugGateCount;
        out.detectionDistance = debugDetectionDistance;
        for (uint8_t g = 0; g < LD2410_GATE_COUNT; g++) out.gates[g] = debugGates[g];
        return out.active;
    }

}
