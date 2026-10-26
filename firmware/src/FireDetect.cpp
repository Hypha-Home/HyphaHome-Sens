// ===================================================================
// File:            FireDetect.cpp
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "FireDetect.h"

namespace {

    using FireDetect::Channel;
    using FireDetect::State;

    /// @brief Messreihe einer Größe: Fenster der letzten Werte, Normalwert und Auswertung.
    struct Series {
        Channel info;
        float samples[FIRE_WINDOW_SAMPLES];
        uint8_t count = 0;
        uint8_t next = 0;
        bool hasBaseline = false;
        uint32_t readyAt = 0;       // ab wann der Kanal zählt (Lernphase)
    };

    /// @brief Schwellen einer Größe für "Verdacht" und "Wahrscheinlich" (Abstand und Anstieg).
    struct Thresholds {
        float suspectDelta, suspectRate, probableDelta, probableRate;
    };

    Series tvocSeries, co2Series, temperatureSeries;
    const Thresholds TVOC_LIMITS = { FIRE_TVOC_SUSPECT_DELTA, FIRE_TVOC_SUSPECT_RATE,
                                     FIRE_TVOC_PROBABLE_DELTA, FIRE_TVOC_PROBABLE_RATE };
    const Thresholds CO2_LIMITS = { FIRE_CO2_SUSPECT_DELTA, FIRE_CO2_SUSPECT_RATE,
                                    FIRE_CO2_PROBABLE_DELTA, FIRE_CO2_PROBABLE_RATE };
    const Thresholds TEMPERATURE_LIMITS = { FIRE_TEMP_SUSPECT_DELTA, FIRE_TEMP_SUSPECT_RATE,
                                            FIRE_TEMP_PROBABLE_DELTA, FIRE_TEMP_PROBABLE_RATE };

    State current = State::Disabled;    // Zustand ohne Test
    uint32_t lastSample = 0;
    uint32_t raiseSince = 0;            // seit wann ein höherer Zustand durchgehend ansteht
    uint32_t lowerSince = 0;            // seit wann ein niedrigerer durchgehend ansteht
    uint32_t testUntil = 0;
    uint32_t lastBeep = 0;
    bool wasAlarm = false;

    /// @brief Ob der ENS160 gültige TVOC-Werte liefert.
    bool tvocAvailable() {
        return Config::sensors.ens160.enabled && MyENS160::isConnected
            && MyENS160::validity == MyENS160::Validity::Normal && MyENS160::aqi > 0;
    }
    /// @brief Ob der SCD40 einen gültigen CO2-Wert liefert.
    bool co2Available() {
        return Config::sensors.scd40.enabled && MySCD40::isConnected && MySCD40::co2 > 0;
    }
    /// @brief Ob der SHT41 verbunden und eingeschaltet ist.
    bool temperatureAvailable() {
        return Config::sensors.sht41.enabled && MySHT41::isConnected;
    }

    /// @brief Verwirft Fenster und Normalwert einer Messreihe (Sensor fehlt oder wurde abgeschaltet).
    void reset(Series& s) {
        s.info = Channel();
        s.count = 0;
        s.next = 0;
        s.hasBaseline = false;
    }

    /// @brief Steigung der Messwerte im Fenster als Ausgleichsgerade.
    /// @return Anstieg je Minute, 0 bei zu wenigen Messungen
    float slopePerMinute(const Series& s) {
        if (s.count < FIRE_MIN_SAMPLES) return 0;
        const uint8_t n = s.count;
        const uint8_t first = (s.next + FIRE_WINDOW_SAMPLES - n) % FIRE_WINDOW_SAMPLES;
        float sumX = 0, sumY = 0, sumXY = 0, sumXX = 0;
        for (uint8_t i = 0; i < n; i++) {
            const float x = i * (FIRE_SAMPLE_S / 60.0f);
            const float y = s.samples[(first + i) % FIRE_WINDOW_SAMPLES];
            sumX += x; sumY += y; sumXY += x * y; sumXX += x * x;
        }
        const float denominator = n * sumXX - sumX * sumX;
        return denominator == 0 ? 0 : (n * sumXY - sumX * sumY) / denominator;
    }

    /// @brief Nimmt einen Messwert auf und aktualisiert Normalwert, Anstieg und Stufe der Reihe.
    /// @param available      Ob der Sensor gültige Werte liefert (sonst wird die Reihe zurückgesetzt)
    /// @param freezeBaseline true hält den Normalwert fest, solange ein Alarm läuft
    void feed(Series& s, bool available, float value, const Thresholds& limits, bool freezeBaseline, uint32_t now) {
        if (!available) {
            reset(s);
            return;
        }
        s.info.available = true;
        s.samples[s.next] = value;
        s.next = (s.next + 1) % FIRE_WINDOW_SAMPLES;
        if (s.count < FIRE_WINDOW_SAMPLES) s.count++;

        // Mittel der letzten drei Werte dämpft das Rauschen der Anzeige und des Abstands.
        float latest = 0;
        const uint8_t used = s.count < 3 ? s.count : 3;
        for (uint8_t i = 1; i <= used; i++) {
            latest += s.samples[(s.next + FIRE_WINDOW_SAMPLES - i) % FIRE_WINDOW_SAMPLES];
        }
        s.info.value = latest / used;

        if (!s.hasBaseline) {
            s.info.baseline = s.info.value;
            s.hasBaseline = true;
            s.readyAt = now + FIRE_LEARN_S * 1000UL;
        } else if (!freezeBaseline) {
            // Während eines Alarms bleibt der Normalwert stehen, sonst wanderte er dem Brand hinterher.
            s.info.baseline += (s.info.value - s.info.baseline) * (FIRE_SAMPLE_S / (float)FIRE_BASELINE_TAU_S);
        }

        s.info.rate = slopePerMinute(s);
        s.info.ready = (int32_t)(now - s.readyAt) >= 0;

        const float delta = s.info.value - s.info.baseline;
        if (delta >= limits.probableDelta && s.info.rate >= limits.probableRate) s.info.level = 2;
        else if (delta >= limits.suspectDelta && s.info.rate >= limits.suspectRate) s.info.level = 1;
        else s.info.level = 0;
    }

    /// @brief Zustand, den die Reihen gerade verlangen (ohne Bestätigungszeit).
    State candidate() {
        Series* all[] = { &tvocSeries, &co2Series, &temperatureSeries };
        uint8_t ready = 0, level1 = 0, level2 = 0;
        for (Series* s : all) {
            if (!s->info.available || !s->info.ready) continue;
            ready++;
            if (s->info.level >= 1) level1++;
            if (s->info.level >= 2) level2++;
        }
        if (ready == 0) return State::Ok;
        const uint8_t need = ready < FIRE_MIN_INDICATORS ? ready : FIRE_MIN_INDICATORS;
        if (level2 >= need) return State::Probable;
        if (level1 >= need) return State::Suspect;
        return State::Ok;
    }

    /// @brief Rangfolge eines Zustands zum Vergleichen (höher = schlimmer).
    uint8_t rank(State s) { return static_cast<uint8_t>(s); }

    /// @brief Ob ein Testalarm gerade läuft.
    bool testActive(uint32_t now) {
        return testUntil != 0 && (int32_t)(testUntil - now) > 0;
    }
}

namespace FireDetect {

    /// @brief Ob mindestens ein nutzbarer Sensor vorhanden und eingeschaltet ist.
    bool sensorsAvailable() {
        return (Config::sensors.ens160.enabled && MyENS160::isConnected)
            || (Config::sensors.scd40.enabled && MySCD40::isConnected)
            || (Config::sensors.sht41.enabled && MySHT41::isConnected);
    }

    /// @brief Wertet die Sensoren im Takt FIRE_SAMPLE_S aus, bestätigt Zustandswechsel und steuert den Summer.
    void update() {
        const uint32_t now = millis();

        if (now - lastSample >= FIRE_SAMPLE_S * 1000UL) {
            lastSample = now;

            if (!Config::system.fireDetection || !sensorsAvailable()) {
                reset(tvocSeries); reset(co2Series); reset(temperatureSeries);
                current = State::Disabled;
                raiseSince = lowerSince = 0;
            } else {
                const bool freeze = current != State::Ok && current != State::Disabled;
                feed(tvocSeries, tvocAvailable(), MyENS160::tvoc, TVOC_LIMITS, freeze, now);
                feed(co2Series, co2Available(), MySCD40::co2, CO2_LIMITS, freeze, now);
                feed(temperatureSeries, temperatureAvailable(), MySHT41::temperatureCalibrated, TEMPERATURE_LIMITS, freeze, now);

                if (current == State::Disabled) current = State::Ok;
                const State wanted = candidate();

                if (rank(wanted) > rank(current)) {
                    lowerSince = 0;
                    if (raiseSince == 0) raiseSince = now;
                    if (now - raiseSince >= FIRE_CONFIRM_S * 1000UL) {
                        current = wanted;
                        raiseSince = 0;
                    }
                } else if (rank(wanted) < rank(current)) {
                    raiseSince = 0;
                    if (lowerSince == 0) lowerSince = now;
                    if (now - lowerSince >= FIRE_CLEAR_S * 1000UL) {
                        current = wanted;
                        lowerSince = 0;
                    }
                } else {
                    raiseSince = lowerSince = 0;
                }
            }
        }

        // Summer: sofort beim Einsetzen des Alarms, danach im festen Takt.
        const State effective = state();
        const bool alarm = effective == State::Suspect || effective == State::Probable;
        if (alarm) {
            const uint32_t interval = effective == State::Probable ? FIRE_BEEP_INTERVAL_PROBABLE_MS : FIRE_BEEP_INTERVAL_MS;
            if (!wasAlarm || now - lastBeep >= interval) {
                Beeper::beepAsync(FIRE_BEEP_MS);
                lastBeep = now;
            }
        }
        wasAlarm = alarm;
    }

    /// @brief Gesamtzustand, einschließlich eines laufenden Tests.
    State state() {
        const uint32_t now = millis();
        if (testActive(now) && rank(current) < rank(State::Suspect)) return State::Suspect;
        return current;
    }

    /// @brief Schlüssel des Zustands für die Schnittstelle.
    const char* key(State s) {
        switch (s) {
            case State::Ok:       return "ok";
            case State::Suspect:  return "suspect";
            case State::Probable: return "probable";
            default:              return "disabled";
        }
    }

    /// @brief Text des Zustands für MQTT.
    const char* mqttText(State s) {
        switch (s) {
            case State::Ok:       return "OK";
            case State::Suspect:  return "Verdacht";
            case State::Probable: return "Wahrscheinlich";
            default:              return "Deaktiviert";
        }
    }

    /// @brief Löst für FIRE_TEST_S Sekunden einen Verdacht aus.
    /// @return false, wenn die Funktion aus ist oder kein Sensor nutzbar ist
    bool startTest() {
        if (!Config::system.fireDetection || !sensorsAvailable()) return false;
        testUntil = millis() + FIRE_TEST_S * 1000UL;
        if (testUntil == 0) testUntil = 1;
        return true;
    }

    /// @brief Messgröße TVOC (ENS160).
    const Channel& tvoc() { return tvocSeries.info; }

    /// @brief Messgröße CO2 (SCD40).
    const Channel& co2() { return co2Series.info; }

    /// @brief Messgröße Temperatur (SHT41).
    const Channel& temperature() { return temperatureSeries.info; }
}
