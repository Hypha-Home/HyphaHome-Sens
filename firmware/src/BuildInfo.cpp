// ===================================================================
// File:            BuildInfo.cpp
// Author:          EideardVMR (and AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "BuildInfo.h"

// In src/build_marker.cpp, erzeugt von scripts/build_info.py.
extern const char BUILD_MARKER[];

namespace BuildInfo {

    static const size_t MAGIC_LEN = 8;                  // "MSBUILD|"
    static const size_t MAX_RECORD = 96;                // Kennung nach dem Anfang, inkl. Endnull
    static const size_t CHUNK = 4096;
    static const uint8_t ESP_IMAGE_MAGIC_BYTE = 0xE9;   // erstes Byte jedes ESP32-Images

    // Der Suchanfang liegt nur verschluesselt im Code. Stuende "MSBUILD|" im Klartext darin,
    // fande die Suche in jedem Image zuerst diese Stelle statt der echten Kennung.
    static const uint8_t MAGIC_XOR = 0x5A;
    static const uint8_t MAGIC_ENCODED[MAGIC_LEN] = { 0x17, 0x09, 0x18, 0x0F, 0x13, 0x16, 0x1E, 0x26 };

    static void decodeMagic(uint8_t out[MAGIC_LEN]) {
        // volatile: Der Compiler soll die Entschluesselung nicht zur Klartextkonstanten falten.
        static volatile uint8_t key = MAGIC_XOR;
        for (size_t i = 0; i < MAGIC_LEN; i++) out[i] = MAGIC_ENCODED[i] ^ key;
    }

    static void copyText(char* dst, size_t capacity, const char* src) {
        strncpy(dst, src, capacity - 1);
        dst[capacity - 1] = '\0';
    }

    /// Liest "v=..|b=..|t=..|g=.." (der Teil hinter dem Anfang). Verlangt Version und Nummer.
    static bool parseRecord(const char* text, size_t available, Info& out) {
        const size_t limit = available < MAX_RECORD ? available : MAX_RECORD;
        const char* end = (const char*)memchr(text, '\0', limit);
        if (!end) return false;

        char buf[MAX_RECORD];
        memcpy(buf, text, end - text);
        buf[end - text] = '\0';

        memset(&out, 0, sizeof(out));
        bool haveVersion = false;
        bool haveBuild = false;

        char* field = buf;
        while (field && *field) {
            char* next = strchr(field, '|');
            if (next) *next++ = '\0';

            if (field[0] != '\0' && field[1] == '=') {
                const char* value = field + 2;
                switch (field[0]) {
                    case 'v': copyText(out.version, sizeof(out.version), value); haveVersion = true; break;
                    case 'b': out.build = strtoul(value, nullptr, 10); haveBuild = true; break;
                    case 't': copyText(out.time, sizeof(out.time), value); break;
                    case 'g': copyText(out.git, sizeof(out.git), value); break;
                }
            }
            field = next;
        }
        return haveVersion && haveBuild;
    }

    const Info& own() {
        static Info info;
        static bool parsed = false;
        if (!parsed) {
            parsed = true;
            if (!parseRecord(BUILD_MARKER + MAGIC_LEN, strlen(BUILD_MARKER) + 1 - MAGIC_LEN, info)) {
                memset(&info, 0, sizeof(info));
            }
        }
        return info;
    }

    static Other otherApp;

    /// Laenge des App-Images aus seinem Kopf: 24 Byte Kopf, je Segment 8 Byte Kopf + Daten, dann
    /// Auffuellung samt Pruefsumme bis zur naechsten 16er-Grenze, optional 32 Byte SHA-256.
    static uint32_t imageLength(const esp_partition_t* part) {
        uint8_t header[24];
        if (esp_partition_read(part, 0, header, sizeof(header)) != ESP_OK) return 0;

        uint32_t offset = sizeof(header);
        for (uint8_t i = 0; i < header[1]; i++) {
            uint32_t segment[2];  // Ladeadresse, Laenge
            if (offset + sizeof(segment) > part->size) return 0;
            if (esp_partition_read(part, offset, segment, sizeof(segment)) != ESP_OK) return 0;
            offset += sizeof(segment) + segment[1];
        }
        offset = (offset + 1 + 15) & ~15u;
        if (header[23]) offset += 32;
        return offset <= part->size ? offset : 0;
    }

    const Other& other() { return otherApp; }

    void scanOtherApp() {
        if (otherApp.state != OtherState::Pending) return;

        const esp_partition_t* part = esp_ota_get_next_update_partition(nullptr);
        if (!part || part == esp_ota_get_running_partition()) {
            otherApp.state = OtherState::None;
            return;
        }
        copyText(otherApp.label, sizeof(otherApp.label), part->label);
        otherApp.partitionSize = part->size;

        const uint32_t started = millis();

        uint8_t first = 0xFF;
        if (esp_partition_read(part, 0, &first, 1) != ESP_OK || first != ESP_IMAGE_MAGIC_BYTE) {
            otherApp.state = OtherState::Empty;
            Serial.printf("[BUILD] Andere App (%s): leer\n", otherApp.label);
            return;
        }

        otherApp.imageSize = imageLength(part);

        uint8_t magic[MAGIC_LEN];
        decodeMagic(magic);

        // Je Durchgang werden CHUNK Anfangspositionen geprueft; die Zugabe dahinter stellt sicher,
        // dass die Kennung auch ueber eine Blockgrenze hinweg ganz im Puffer liegt.
        const size_t window = CHUNK + MAGIC_LEN + MAX_RECORD;
        uint8_t* buf = (uint8_t*)malloc(window);
        if (!buf) {
            Serial.println(F("[BUILD] Andere App: kein Speicher fuer die Suche"));
            otherApp.state = OtherState::Unmarked;
            return;
        }

        otherApp.state = OtherState::Unmarked;
        for (size_t offset = 0; offset < part->size; offset += CHUNK) {
            size_t length = part->size - offset;
            if (length > window) length = window;
            if (esp_partition_read(part, offset, buf, length) != ESP_OK) break;

            const size_t starts = length < CHUNK ? length : CHUNK;
            bool found = false;
            for (size_t i = 0; i + MAGIC_LEN <= length && i < starts; i++) {
                if (buf[i] != magic[0] || memcmp(buf + i, magic, MAGIC_LEN) != 0) continue;
                if (parseRecord((const char*)buf + i + MAGIC_LEN, length - i - MAGIC_LEN, otherApp.info)) {
                    found = true;
                    break;
                }
            }
            if (found) {
                otherApp.state = OtherState::Found;
                break;
            }
        }
        free(buf);

        if (otherApp.state == OtherState::Found) {
            Serial.printf("[BUILD] Andere App (%s): %s, Build %lu, %s, Git %s (Suche %lu ms)\n",
                          otherApp.label, otherApp.info.version, (unsigned long)otherApp.info.build,
                          otherApp.info.time, otherApp.info.git, (unsigned long)(millis() - started));
        } else {
            Serial.printf("[BUILD] Andere App (%s): ohne Kennung (Suche %lu ms)\n",
                          otherApp.label, (unsigned long)(millis() - started));
        }
    }
}
