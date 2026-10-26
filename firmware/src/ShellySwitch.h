// ===================================================================
// File:            ShellySwitch.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "ShellyProbe.h"

/// @brief Liest und schaltet den Ausgang (Relais) eines Shelly (Gen 1 und >1).
namespace ShellySwitch {

    /// @brief Wie ein Gerät angesprochen wird. Eine Kopie der nötigen Felder aus ShellyCfg, damit
    /// der Aufrufer die Konfiguration nicht über die Dauer der Anfrage festhalten muss.
    struct Target {
        String  address;
        String  password;
        uint8_t gen = 0;   ///< 1 bis 4; 0 (noch nicht erkannt) ist nicht ansprechbar
    };

    /// @brief Fragt ab, ob der Kanal eingeschaltet ist.
    /// @param on  Erhält den Zustand, nur bei Rückgabe true gültig
    /// @return false wenn das Gerät nicht antwortet oder die Antwort nicht passt
    bool read(const Target& target, uint8_t channel, bool& on);

    /// @brief Schaltet den Kanal ein oder aus.
    /// @return true wenn das Gerät den Befehl angenommen hat
    bool write(const Target& target, uint8_t channel, bool on);
}
