// ===================================================================
// File:            Beeper.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#include "Beeper.h"

namespace {

    esp_timer_handle_t stopTimer = nullptr;

    /// @brief Schaltet den Buzzer aus. Läuft im Zeitgeber-Task, wenn die Tondauer abgelaufen ist.
    void stopTone(void*) {
        digitalWrite(PIN_BEEP, LOW);
    }
}

namespace Beeper {

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void begin() {
        pinMode(PIN_BEEP, OUTPUT);
        digitalWrite(PIN_BEEP, LOW);
    }

    void beep(uint16_t durationMs) {
        pinMode(PIN_BEEP, OUTPUT);
        digitalWrite(PIN_BEEP, HIGH);
        delay(durationMs);
        digitalWrite(PIN_BEEP, LOW);
    }

    void beepAsync(uint16_t durationMs) {
        if (!stopTimer) {
            esp_timer_create_args_t args = {};
            args.callback = stopTone;
            args.name = "beep";
            if (esp_timer_create(&args, &stopTimer) != ESP_OK) return;
        }
        esp_timer_stop(stopTimer);      // läuft noch ein Ton, wird dessen Ende verschoben
        pinMode(PIN_BEEP, OUTPUT);
        digitalWrite(PIN_BEEP, HIGH);
        esp_timer_start_once(stopTimer, (uint64_t)durationMs * 1000ULL);
    }
}
