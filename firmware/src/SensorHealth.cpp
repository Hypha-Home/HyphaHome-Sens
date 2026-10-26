// ===================================================================
// File:            SensorHealth.cpp
// Author:          EideardVMR
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "SensorHealth.h"

namespace SensorHealth {

    void reportOk(bool& present, State& s, const char* tag) {
        s.fails = 0;
        if (!present) {
            present = true;
            Serial.printf("[%s] wieder erreichbar\n", tag);
        }
    }

    void reportFail(bool& present, State& s, const char* tag) {
        // Bereits abgemeldet: nicht weiterzählen und nicht erneut loggen.
        if (!present) return;

        const uint32_t now = millis();
        if (s.fails == 0) s.failSince = now;
        if (s.fails < 255) s.fails++;

        const uint32_t elapsed = now - s.failSince;
        if (elapsed >= FAIL_TIMEOUT_MS) {
            present = false;
            Serial.printf("[%s] antwortet nicht mehr (seit %lu s, %u Fehlversuche)\n",
                          tag, (unsigned long)(elapsed / 1000), s.fails);
        }
    }

    bool retryDue(State& s, uint32_t interval) {
        uint32_t now = millis();
        if (now - s.lastRetry < interval) return false;
        s.lastRetry = now;
        return true;
    }

    bool i2cPing(uint8_t address) {
        Wire.beginTransmission(address);
        return Wire.endTransmission() == 0; // 0 = Gerät hat mit ACK geantwortet
    }

    void i2cRecoverBus() {
        Serial.println(F("[I2C] Bus blockiert - versuche Freigabe"));

        Wire.end();

        // SDA nur lesen, SCL selbst takten.
        pinMode(PIN_SDA, INPUT_PULLUP);
        pinMode(PIN_SCL, OUTPUT);

        // Neun Takte reichen, damit ein hängender Slave sein angefangenes Byte zu Ende
        // schreiben kann und SDA wieder loslässt.
        for (uint8_t i = 0; i < 9; i++) {
            digitalWrite(PIN_SCL, LOW);
            delayMicroseconds(5);
            digitalWrite(PIN_SCL, HIGH);
            delayMicroseconds(5);
            if (digitalRead(PIN_SDA) == HIGH) break; // Bus ist wieder frei
        }

        // Sauberes STOP erzeugen, damit alle Teilnehmer in den Ruhezustand zurückfinden.
        pinMode(PIN_SDA, OUTPUT);
        digitalWrite(PIN_SDA, LOW);
        delayMicroseconds(5);
        digitalWrite(PIN_SCL, HIGH);
        delayMicroseconds(5);
        digitalWrite(PIN_SDA, HIGH);
        delayMicroseconds(5);

        Wire.begin(PIN_SDA, PIN_SCL);
        Wire.setClock(100000);
    }
}
