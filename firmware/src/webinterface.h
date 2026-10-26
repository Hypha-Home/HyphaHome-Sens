// ===================================================================
// File:            webinterface.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#pragma once
#include <LittleFS.h>
#include <esp_ota_ops.h>
#include <core_version.h>
#include <esp_system.h>
#include "libs/WLANManager/WLANManager.h"
#include "config.h"
#include "PasswordReset.h"
#include "Session.h"
#include "Runtime.h"
#include "MyBH1750.h"
#include "MySHT41.h"
#include "MySCD40.h"
#include "MyAHT21.h"
#include "MyENS160.h"
#include "MyLD2410.h"
#include "HeatingControl.h"
#include "MyMQTT.h"
#include "ShellyProbe.h"
#include "BuildInfo.h"
#include "FireDetect.h"
#include "webfiles.h"

/// @brief Liefert die API-Schnittstelle
namespace Webinterface {

    /// @brief Legt die Routen für das Webinterface an.
    void setupWebserverRoutes();
}
