// ===================================================================
// File:            version.h
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-25
// ===================================================================
#pragma once

#include <stdint.h>
#include <WString.h>

/// @brief Versionsnummer in der Form major.minor.patch (zum Beispiel "1.10.4").
struct Version {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;

    /// @brief Liest eine Versionsnummer aus einem Text wie "1.10.4".
    /// @param v Text mit zwei Punkten. Fehlt der erste Punkt, ist das Ergebnis 0.0.0.
    /// @return Die gelesene Version
    static Version parse(const String& v);

    /// @brief Gibt die Version als Text aus, zum Beispiel "1.10.4".
    String toString() const;

    // Vergleichsoperatoren: major vor minor vor patch.
    bool operator>(const Version& o) const;
    bool operator<(const Version& o) const;
    bool operator>=(const Version& o) const;
    bool operator<=(const Version& o) const;
    bool operator==(const Version& o) const;
    bool operator!=(const Version& o) const;
};
