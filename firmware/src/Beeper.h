// ===================================================================
// File:            Beeper.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#pragma once

#include <stdint.h>
#include <Arduino.h>
#include <esp_timer.h>
#include "pinmapping.h"

#ifndef PIN_BEEP
    #define PIN_BEEP 1
#endif

/// @brief Aktiver Buzzer an PIN_BEEP. 
namespace Beeper {

    /// @brief Setzt den Pin als Ausgang auf LOW.
    void begin();

    /// @brief Gibt einen Ton aus und wartet, bis er fertig ist (blockiert für durationMs).
    /// @param durationMs Dauer in Millisekunden.
    void beep(uint16_t durationMs = 80);

    /// @brief Wie beep(), kehrt aber sofort zurück; ein Zeitgeber beendet den Ton.
    /// Für wiederkehrende Signale aus loop(), die nichts aufhalten dürfen.
    /// @param durationMs Dauer in Millisekunden. Läuft noch ein Ton, wird dessen Ende verschoben.
    void beepAsync(uint16_t durationMs = 500);
}
