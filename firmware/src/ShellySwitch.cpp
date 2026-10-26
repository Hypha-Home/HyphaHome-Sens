// ===================================================================
// File:            ShellySwitch.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "ShellySwitch.h"

namespace ShellySwitch {

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    bool read(const Target& target, uint8_t channel, bool& on) {
        if (target.gen == 0 || target.address.length() == 0) return false;

        // API Pfad je nach Generation bestimmen.
        const String path = (target.gen == 1)
            ? "/relay/" + String(channel)
            : "/rpc/Switch.GetStatus?id=" + String(channel);

        JsonDocument doc;
        int code = 0;
        if (!ShellyProbe::getJson(target.address, path, target.password, target.gen, doc, code)) {
            Serial.printf("[SHELLY] %s: Zustand lesen fehlgeschlagen (HTTP %d)\n", target.address.c_str(), code);
            return false;
        }

        const JsonVariant value = (target.gen == 1) ? doc["ison"] : doc["output"];
        if (!value.is<bool>()) {
            Serial.printf("[SHELLY] %s: Antwort ohne Schaltzustand\n", target.address.c_str());
            return false;
        }
        on = value.as<bool>();
        return true;
    }

    bool write(const Target& target, uint8_t channel, bool on) {
        if (target.gen == 0 || target.address.length() == 0) return false;

        // API Pfad je nach Generation bestimmen.
        const String path = (target.gen == 1)
            ? "/relay/" + String(channel) + "?turn=" + (on ? "on" : "off")
            : "/rpc/Switch.Set?id=" + String(channel) + "&on=" + (on ? "true" : "false");

        JsonDocument doc;
        int code = 0;
        if (!ShellyProbe::getJson(target.address, path, target.password, target.gen, doc, code)) {
            Serial.printf("[SHELLY] %s: Schalten fehlgeschlagen (HTTP %d)\n", target.address.c_str(), code);
            return false;
        }
        Serial.printf("[SHELLY] %s Kanal %u: %s\n", target.address.c_str(), (unsigned)channel, on ? "EIN" : "AUS");
        return true;
    }
}
