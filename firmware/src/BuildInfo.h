// ===================================================================
// File:            BuildInfo.h
// Author:          EideardVMR (and AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once

#include <Arduino.h>
#include <stdint.h>
#include <esp_partition.h>
#include <esp_ota_ops.h>
#include <string.h>
#include <stdlib.h>

/// @brief Kennung dieses Builds und der Firmware auf der jeweils anderen OTA-Partition.
/// scripts/build_info.py legt bei jedem Bauen eine Zeichenkette ("MSBUILD|v=..|b=..|t=..|g=..") in das Image.
namespace BuildInfo {

    enum class OtherState : uint8_t {
        Pending,    // noch nicht gesucht
        None,       // die Partitionstabelle hat keinen zweiten App-Platz
        Empty,      // dort liegt keine Firmware
        Unmarked,   // dort liegt eine Firmware ohne Kennung (gebaut vor Einfuehrung der Kennung)
        Found       // Kennung gefunden, info ist gueltig
    };

    struct Info {
        char version[16];   // VERSION aus config.h, z.B. "0.0.6"
        uint32_t build;     // Build-Nummer (build_number.txt)
        char time[20];      // Zeitpunkt des Bauens, "2026-10-10 14:03"
        char git[16];       // Commit-Hash, '+' wenn nicht eingecheckte Aenderungen vorlagen
    };

    struct Other {
        OtherState state = OtherState::Pending;
        char label[17] = "";            // Partitionsname, z.B. "app1"
        uint32_t imageSize = 0;         // Groesse der Firmware dort in Bytes, 0 = unbekannt
        uint32_t partitionSize = 0;     // Groesse des App-Platzes
        Info info = {};
    };

    /// @brief Kennung der laufenden Firmware.
    const Info& own();

    /// @brief Durchsucht die andere App-Partition einmal nach ihrer Kennung (dauert einige 100 ms).
    ///        Das Ergebnis liefert other(). Ein erneuter Aufruf sucht nicht noch einmal.
    void scanOtherApp();

    /// @brief Ergebnis von scanOtherApp(); davor state == Pending.
    const Other& other();
}
