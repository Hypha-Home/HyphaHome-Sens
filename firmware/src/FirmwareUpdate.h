// ===================================================================
// File:            FirmwareUpdate.h
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include "libs/WLANManager/WLANManager.h"
#include "Session.h"
#include "Runtime.h"

/// @brief Firmware-Update per Upload (OTA) aus den Einstellungen der Web-UI.
namespace FirmwareUpdate {

    /// @brief Meldet die Route an.
    void setupRoutes();
}
