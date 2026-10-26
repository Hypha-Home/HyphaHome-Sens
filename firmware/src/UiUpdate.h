// ===================================================================
// File:            UiUpdate.h
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <esp_partition.h>
#include "libs/WLANManager/WLANManager.h"
#include "webfiles.h"
#include "Session.h"
#include "config.h"

/// @brief Notfallseite zum Einspielen einer neuen Web-UI.
/// Ist die vorhandene UI nicht kompatibel mit der Firmware oder ist keine UI vorhanden, 
/// gibt es diese Hardcoded UI um ein Update der UI machen zu können.
namespace UiUpdate {

    /// @brief Meldet GET und POST /ui-update an. Nach dem Einhaengen des UI-Dateisystems aufrufen.
    void setupRoutes();

    /// @brief Schickt die Notfallseite. Webfiles ruft das anstelle jeder Seite auf, solange die
    /// Web-UI nicht zur Firmware passt.
    void sendPage();
}
