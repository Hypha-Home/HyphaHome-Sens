// ===================================================================
// File:            FireDetect.h
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <stdint.h>
#include "config.h"
#include "Beeper.h"
#include "MyENS160.h"
#include "MySCD40.h"
#include "MySHT41.h"

#ifndef FIRE_SAMPLE_S
    /// @brief Abstand der Messungen für die Auswertung, in Sekunden.
    #define FIRE_SAMPLE_S                   5
#endif

#ifndef FIRE_WINDOW_SAMPLES
    /// @brief Größe des Fensters für die Steigung, in Messungen (24 x 5 s = 2 min, höchstens 255).
    #define FIRE_WINDOW_SAMPLES             24
#endif

#ifndef FIRE_MIN_SAMPLES
    /// @brief Ab so vielen Messungen wird die Steigung berechnet.
    #define FIRE_MIN_SAMPLES                12
#endif

#ifndef FIRE_BASELINE_TAU_S
    /// @brief Zeitkonstante des nachlaufenden Normalwerts, in Sekunden (1800 = 30 min).
    #define FIRE_BASELINE_TAU_S             1800
#endif

#ifndef FIRE_LEARN_S
    /// @brief Lernphase nach dem Start oder Einschalten eines Sensors, in Sekunden.
    #define FIRE_LEARN_S                    300
#endif

#ifndef FIRE_CONFIRM_S
    /// @brief So lange müssen die Indizien anstehen, bevor der Alarm eintritt, in Sekunden.
    #define FIRE_CONFIRM_S                  30
#endif

#ifndef FIRE_CLEAR_S
    /// @brief So lange müssen die Indizien fehlen, bevor der Alarm endet, in Sekunden.
    #define FIRE_CLEAR_S                    180
#endif

#ifndef FIRE_MIN_INDICATORS
    /// @brief Anzahl gleichzeitiger Indizien für einen Alarm (bei weniger Sensoren entsprechend weniger).
    #define FIRE_MIN_INDICATORS             2
#endif

#ifndef FIRE_TVOC_SUSPECT_DELTA
    /// @brief TVOC: Abstand zum Normalwert für "Verdacht", in ppb.
    #define FIRE_TVOC_SUSPECT_DELTA         300.0f
#endif

#ifndef FIRE_TVOC_SUSPECT_RATE
    /// @brief TVOC: Anstieg für "Verdacht", in ppb/min.
    #define FIRE_TVOC_SUSPECT_RATE          100.0f
#endif

#ifndef FIRE_TVOC_PROBABLE_DELTA
    /// @brief TVOC: Abstand zum Normalwert für "Wahrscheinlich", in ppb.
    #define FIRE_TVOC_PROBABLE_DELTA        1000.0f
#endif

#ifndef FIRE_TVOC_PROBABLE_RATE
    /// @brief TVOC: Anstieg für "Wahrscheinlich", in ppb/min.
    #define FIRE_TVOC_PROBABLE_RATE         300.0f
#endif

#ifndef FIRE_CO2_SUSPECT_DELTA
    /// @brief CO2: Abstand zum Normalwert für "Verdacht", in ppm.
    #define FIRE_CO2_SUSPECT_DELTA          50.0f
#endif

#ifndef FIRE_CO2_SUSPECT_RATE
    /// @brief CO2: Anstieg für "Verdacht", in ppm/min.
    #define FIRE_CO2_SUSPECT_RATE           30.0f
#endif

#ifndef FIRE_CO2_PROBABLE_DELTA
    /// @brief CO2: Abstand zum Normalwert für "Wahrscheinlich", in ppm.
    #define FIRE_CO2_PROBABLE_DELTA         150.0f
#endif

#ifndef FIRE_CO2_PROBABLE_RATE
    /// @brief CO2: Anstieg für "Wahrscheinlich", in ppm/min.
    #define FIRE_CO2_PROBABLE_RATE          100.0f
#endif

#ifndef FIRE_TEMP_SUSPECT_DELTA
    /// @brief Temperatur: Abstand zum Normalwert für "Verdacht", in K.
    #define FIRE_TEMP_SUSPECT_DELTA         1.0f
#endif

#ifndef FIRE_TEMP_SUSPECT_RATE
    /// @brief Temperatur: Anstieg für "Verdacht", in K/min.
    #define FIRE_TEMP_SUSPECT_RATE          0.5f
#endif

#ifndef FIRE_TEMP_PROBABLE_DELTA
    /// @brief Temperatur: Abstand zum Normalwert für "Wahrscheinlich", in K.
    #define FIRE_TEMP_PROBABLE_DELTA        3.0f
#endif

#ifndef FIRE_TEMP_PROBABLE_RATE
    /// @brief Temperatur: Anstieg für "Wahrscheinlich", in K/min.
    #define FIRE_TEMP_PROBABLE_RATE         2.0f
#endif

#ifndef FIRE_BEEP_MS
    /// @brief Dauer eines Summertons, in Millisekunden.
    #define FIRE_BEEP_MS                    500
#endif

#ifndef FIRE_BEEP_INTERVAL_MS
    /// @brief Abstand der Summertöne bei "Verdacht", in Millisekunden.
    #define FIRE_BEEP_INTERVAL_MS           5000
#endif

#ifndef FIRE_BEEP_INTERVAL_PROBABLE_MS
    /// @brief Abstand der Summertöne bei "Wahrscheinlich", in Millisekunden.
    #define FIRE_BEEP_INTERVAL_PROBABLE_MS  2000
#endif

#ifndef FIRE_TEST_S
    /// @brief Dauer des Testalarms, in Sekunden.
    #define FIRE_TEST_S                     15
#endif

/// @brief Brandfrüherkennung aus den vorhandenen Sensoren. Ersetzt KEINEN zertifizierten Rauchmelder.
///
/// Beobachtet drei Größen: TVOC (ENS160), CO2 (SCD40) und Temperatur (SHT41). Für jede wird ein
/// langsam nachlaufender Normalwert geführt und der Anstieg über ein kurzes Zeitfenster berechnet.
/// Ein Indiz gilt, wenn sowohl der Abstand zum Normalwert als auch die Steigung ihre Schwelle
/// überschreiten. Sensoren, die fehlen oder abgeschaltet sind, zählen nicht; verlangt werden
/// FIRE_MIN_INDICATORS gleichzeitige Indizien, bei weniger vorhandenen Sensoren entsprechend weniger.
namespace FireDetect {

    /// @brief Zustand der Brandfrüherkennung.
    enum class State : uint8_t {
        Disabled,   // ausgeschaltet oder kein nutzbarer Sensor
        Ok,         // nichts auffällig (auch während der Lernphase nach dem Start)
        Suspect,    // Verdacht: mehrere Indizien auf der unteren Stufe
        Probable    // wahrscheinlich: mehrere Indizien auf der oberen Stufe
    };

    /// @brief Messgröße mit Normalwert und Anstieg, zur Anzeige und Abstimmung der Schwellen.
    struct Channel {
        bool  available = false;    // Sensor vorhanden, eingeschaltet und liefert gültige Werte
        bool  ready = false;        // Lernphase vorbei, zählt für die Entscheidung
        float value = 0;            // aktueller (geglätteter) Wert
        float baseline = 0;         // Normalwert
        float rate = 0;             // Anstieg je Minute
        uint8_t level = 0;          // 0 = unauffällig, 1 = Verdacht, 2 = wahrscheinlich
    };

    /// @brief Wertet die Sensoren aus und steuert den Summer. Regelmäßig aus loop() aufrufen.
    void update();

    /// @brief Gesamtzustand, einschließlich eines laufenden Tests.
    State state();

    /// @brief Schlüssel für die Schnittstelle: "disabled", "ok", "suspect", "probable".
    const char* key(State state);

    /// @brief Text für MQTT: "Deaktiviert", "OK", "Verdacht", "Wahrscheinlich".
    const char* mqttText(State state);

    /// @brief Ob mindestens ein nutzbarer Sensor vorhanden und eingeschaltet ist.
    ///        Ohne ihn lässt sich die Funktion nicht einschalten.
    bool sensorsAvailable();

    /// @brief Löst für FIRE_TEST_S Sekunden einen Verdacht aus (LED, Summer, MQTT).
    /// @return false, wenn die Funktion ausgeschaltet oder kein Sensor nutzbar ist
    bool startTest();

    /// @brief Messgröße TVOC (ENS160).
    const Channel& tvoc();

    /// @brief Messgröße CO2 (SCD40).
    const Channel& co2();

    /// @brief Messgröße Temperatur (SHT41).
    const Channel& temperature();
}
