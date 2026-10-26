// ===================================================================
// File:            main.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================

#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "pinmapping.h"
#include "MyBH1750.h"
#include "MySHT41.h"
#include "MySCD40.h"
#include "MyLD2410.h"
#include "MyAHT21.h"
#include "MyENS160.h"
#include "SensorHealth.h"
#include "MyMQTT.h"
#include "PasswordReset.h"
#include "Session.h"
#include "Runtime.h"
#include "libs/WLANManager/WLANManager.h"
#include "webinterface.h"
#include "webfiles.h"
#include "BuildInfo.h"
#include "Beeper.h"
#include "LED.h"
#include "FireDetect.h"
#include "HeatingControl.h"
#include "webfiles.h"