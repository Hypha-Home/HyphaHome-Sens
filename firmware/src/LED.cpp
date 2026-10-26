// ===================================================================
// File:            LED.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "LED.h"

namespace {

    enum class Mode : uint8_t { Off, Solid, Blink, Fade };

    struct State {
        Mode mode = Mode::Off;
        LED::Color color = LED::Off;
        uint16_t periodMs = 1000;
        uint32_t startMs = 0;       // Zeitpunkt, ab dem die Phase des Effekts gezaehlt wird
    };

    const uint32_t TICK_MS = 20;    // eine Pause für den Task, damit dieser nicht die CPU auslastet.
    const uint8_t DEFAULT_BRIGHTNESS = 64;

    portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    State state;                    // vom Task gelesen, von den Effektfunktionen gesetzt
    uint8_t brightness = DEFAULT_BRIGHTNESS;
    TaskHandle_t task = nullptr;

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    void set(Mode mode, LED::Color color, uint16_t periodMs) {
        if (periodMs < 2) periodMs = 2;
        portENTER_CRITICAL(&lock);
        // Derselbe Effekt noch einmal (z.B. aus loop() bei jedem Durchlauf): Phase nicht neu
        // starten, sonst bliebe ein Fade am Anfang stehen.
        if (state.mode == mode && state.periodMs == periodMs &&
            state.color.r == color.r && state.color.g == color.g && state.color.b == color.b) {
            portEXIT_CRITICAL(&lock);
            return;
        }
        state.mode = mode;
        state.color = color;
        state.periodMs = periodMs;
        state.startMs = millis();
        portEXIT_CRITICAL(&lock);
    }

    /// @brief Bestimmt die Helligkeit eines Pixel zu einem bestimmten Zeitpunkt
    /// @param s Status der LED
    /// @param now Aktueller Zeitpunkt
    /// @return factor für die Helligkeitsanpassung
    uint8_t level(const State& s, uint32_t now) {
        switch (s.mode) {
            case Mode::Solid:
                return 255;
            case Mode::Blink:
                return ((now - s.startMs) % s.periodMs) < s.periodMs / 2 ? 255 : 0;
            case Mode::Fade: {
                // Dreieckswelle 0..255..0, danach quadriert: das Auge nimmt Helligkeit nicht
                // linear wahr, ohne das wirkt das Ausblenden zu abrupt.
                const uint32_t phase = ((now - s.startMs) % s.periodMs) * 510UL / s.periodMs;
                const uint32_t tri = phase < 255 ? phase : 510 - phase;
                return (uint8_t)(tri * tri / 255);
            }
            default:
                return 0;
        }
    }

    /// @brief Skaliert den Farbanteil anhand eines Faktors
    /// @param value Farbeanteil (z.B. r,g oder b)
    /// @param factor Faktor
    /// @return neuer Wert für den Farbanteil
    uint8_t scale(uint8_t value, uint32_t factor) {
        return (uint8_t)((value * factor) / 255);
    }

    /// @brief Die Loop des LED Tasks
    void taskLoop(void*) {
        uint8_t lastR = 0, lastG = 0, lastB = 0;
        bool first = true;

        for (;;) {
            State s;
            uint8_t bright;
            portENTER_CRITICAL(&lock);
            s = state;
            bright = brightness;
            portEXIT_CRITICAL(&lock);

            const uint32_t factor = (uint32_t)level(s, millis()) * bright / 255;
            const uint8_t r = scale(s.color.r, factor);
            const uint8_t g = scale(s.color.g, factor);
            const uint8_t b = scale(s.color.b, factor);

            // Nur schreiben, wenn sich etwas aendert: spart bei ruhigem Licht die RMT-Ausgabe.
            if (first || r != lastR || g != lastG || b != lastB) {
                neopixelWrite(PIN_LED, r, g, b);
                lastR = r; lastG = g; lastB = b;
                first = false;
            }
            vTaskDelay(pdMS_TO_TICKS(TICK_MS));
        }
    }
}

namespace LED {

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void begin() {
        if (task) return;
        neopixelWrite(PIN_LED, 0, 0, 0);
        xTaskCreatePinnedToCore(taskLoop, "LED", 3072, nullptr, 1, &task, 1);
    }

    void setBrightness(uint8_t value) {
        portENTER_CRITICAL(&lock);
        brightness = value;
        portEXIT_CRITICAL(&lock);
    }

    void off() { set(Mode::Off, Off, 1000); }

    void solid(Color color) { set(Mode::Solid, color, 1000); }

    void blink(Color color, uint16_t periodMs) { set(Mode::Blink, color, periodMs); }

    void fadeInOut(Color color, uint16_t periodMs) { set(Mode::Fade, color, periodMs); }
}
