// ===================================================================
// File:            webfiles.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#pragma once

#include <stdint.h>
#include <LittleFS.h>
#include "libs/WLANManager/WLANManager.h"
#include "config.h"
#include "Version.h"
#include <ArduinoJson.h>
#include "UiUpdate.h"
#include "FirmwareUpdate.h"

/// @brief Liefert die Seiten des Webinterface aus dem LittleFS aus (nicht API).
/// Die API-Schnittstelle (/api/...) steht in webinterface.cpp.
namespace Webfiles {


    /// Wie gut die Web-UI zur API (= Firmwareversion) passt. Massgeblich sind die Angaben in der
    /// UI selbst, data/version.json.
    enum class UiState : uint8_t {
        Ok,         // Firmware >= recommended_api_version: voll kompatibel
        Limited,    // min_api_version <= Firmware < recommended_api_version: laeuft, aber mit Luecken
        TooOld,     // Firmware < min_api_version: die UI braucht eine neuere Firmware
        Unknown     // /version.json fehlt oder ist unvollstaendig
    };

    struct UiInfo {
        UiState state = UiState::Unknown;
        char version[16] = "";      // ui_version
        char name[32] = "";         // ui_name, z.B. "default"
        char vendor[48] = "";       // vendor
        char link[96] = "";         // link
        char minApi[16] = "";       // min_api_version
        char recommendedApi[16] = "";   // recommended_api_version
    };

    /// @brief Legt die Routen fest
    void setupRoutes();

    /// @brief Angaben der Web-UI im Dateisystem, beim Start einmal gelesen (setupRoutes) und
    ///        mit der Firmwareversion verglichen.
    const UiInfo& ui();

    /// @brief true, wenn die Seiten ausgeliefert werden duerfen (Ok oder Limited). Sonst zeigt
    ///        die Firmware die Notfallseite.
    bool uiUsable();

    /// @brief Liest /version.json neu, etwa nachdem eine neue Web-UI eingespielt wurde.
    void recheckUi();
}
