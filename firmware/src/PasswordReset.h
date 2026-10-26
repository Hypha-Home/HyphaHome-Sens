// ===================================================================
// File:            PasswordReset.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

#ifndef PWRESET_VALUD_MS
///@brief Countdown bis das Gerät neu gestartet werden muss um einen Passwort-reset einzuleiten.
#define PWRESET_VALUD_MS 5UL * 60UL * 1000UL
#endif

#ifndef PWRESET_NVS_NAMESPACE
///@brief Name des NVS Namespace
#define PWRESET_NVS_NAMESPACE "pwreset"
#endif

#ifndef PWRESET_NVS_KEY_ARMED
///@brief Name des Key im NVS-Namespace
#define PWRESET_NVS_KEY_ARMED "armed"
#endif

/// @brief Notfall-Zurücksetzen des Geräte-Passworts, falls das Passwort vergessen wurde.
///
/// Ablauf:
/// 1. Über das Webinterface wird das Zurücksetzen freigegeben: arm() 
/// 2. Ein Timer läuft VALID_MS lang. 
/// 3. Das Gerät startet in dieser Zeit neu, dann wird mit applyIfArmed() beim hochfahren das Passwort 
///    auf Standard zurückgesetzt. Alternativ, wird die freigabe wiederrufen.
namespace PasswordReset {

    /// @brief Gibt das Zurücksetzen des Geräte-Passworts frei.
    void arm();

    /// @brief Verbleibende Gültigkeit der Freigabe.
    /// @return Sekunden bis zum Ablauf, 0 wenn keine Freigabe aktiv ist
    uint32_t remainingSeconds();

    /// @brief Löscht beim Start das Geräte-Passwort, falls die Freigabe im NVS gesetzt ist, und
    /// verbraucht die Freigabe anschließend.
    ///
    /// Aufrufreihenfolge in setup() beachten: 
    /// - nach Config::loadConfig(), weil dieses device_login sonst wieder überschreibt 
    /// - vor dem Setzen des automatischen Hostnamens, weil hier Config::saveConfig() aufgerufen wird und der automatische Name sonst mitgespeichert würde.
    void applyIfArmed();

    /// @brief Löscht die Freigabe nach Ablauf der Frist. In jedem loop()-Durchlauf aufrufen.
    void handleTimeout();
}
