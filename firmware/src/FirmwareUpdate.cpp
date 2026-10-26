// ===================================================================
// File:            FirmwareUpdate.cpp
// Author:          EideardVMR (with AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "FirmwareUpdate.h"

namespace {

    const size_t MULTIPART_OVERHEAD = 4096;   // Grenzen und Kopfzeilen des Formularfelds

    enum class Result : uint8_t {
        Pending,        // Upload laeuft oder wurde noch nicht begonnen
        Ok,
        Unauthorized,
        NoPartition,
        BadSize,
        BadImage,
        WriteError,
        Aborted
    };

    struct Job {
        Result result = Result::Pending;
        bool begun = false;               // Update.begin() ist gelaufen
        String detail;                    // Fehlertext der Update-Bibliothek
    };
    Job job;

    // --------------------------------------------------------------
    // Static helper functions
    // --------------------------------------------------------------

    void fail(Result result) {
        job.result = result;
        if (job.begun) {
            Update.abort();
            job.begun = false;
        }
    }

    /// @brief Event wenn der Upload gestartet/durchgeführt wird.
    void onUpload() {
        WebServer& ws = wlanManager.getWebserver();
        HTTPUpload& up = ws.upload();

        if (up.status == UPLOAD_FILE_START) {
            job = Job();

            if (!Session::isValid(ws.header("X-Auth-Token"))) {
                job.result = Result::Unauthorized;
                return;
            }
            const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
            if (!target) {
                job.result = Result::NoPartition;
                return;
            }
            const long length = ws.clientContentLength();
            if (length <= 0 || length > (long)(target->size + MULTIPART_OVERHEAD)) {
                job.result = Result::BadSize;
                return;
            }
            Serial.printf("[FirmwareUpdate] Upload beginnt, Ziel %s\n", target->label);

        } else if (up.status == UPLOAD_FILE_WRITE) {
            if (job.result != Result::Pending) return;

            if (!job.begun) {
                // Erst jetzt beginnen: Das erste Byte jeder ESP-Firmware ist 0xE9. Alles andere
                // wird abgelehnt, bevor die Bibliothek etwas loescht.
                if (up.buf[0] != 0xE9) {
                    job.result = Result::BadImage;
                    return;
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    job.detail = Update.errorString();
                    job.result = Result::WriteError;
                    return;
                }
                job.begun = true;
            }
            if (Update.write(up.buf, up.currentSize) != up.currentSize) {
                job.detail = Update.errorString();
                fail(Result::WriteError);
            }

        } else if (up.status == UPLOAD_FILE_END) {
            if (job.result != Result::Pending) return;
            if (!job.begun) {
                job.result = Result::BadSize;
                return;
            }
            // true: auch bei kuerzerer Datei als der Partition abschliessen. end() prueft das
            // Abbild (Kopf, Pruefsumme) und stellt erst bei Erfolg den Start darauf um.
            if (Update.end(true)) {
                job.begun = false;
                job.result = Result::Ok;
            } else {
                job.detail = Update.errorString();
                job.begun = false;
                job.result = Result::BadImage;
            }

        } else if (up.status == UPLOAD_FILE_ABORTED) {
            if (job.result == Result::Pending) fail(Result::Aborted);
        }
    }

    /// @brief Event wenn der Upload beendet wurde. Schreibt die Firmware
    void onUploadDone() {
        WebServer& ws = wlanManager.getWebserver();
        if (job.begun) fail(Result::Aborted);

        const Result result = job.result;
        // code: Schluessel, den die Web-UI selbst uebersetzt
        int status = 200;
        const char* code = "";
        switch (result) {
            case Result::Ok:           break;
            case Result::Unauthorized: status = 401; code = "NOT_AUTHENTICATED"; break;
            case Result::NoPartition:  status = 500; code = "NO_PARTITION"; break;
            case Result::BadSize:      status = 400; code = "FW_BAD_SIZE"; break;
            case Result::BadImage:     status = 400; code = "FW_BAD_IMAGE"; break;
            case Result::WriteError:   status = 500; code = "WRITE_FAILED"; break;
            case Result::Aborted:      status = 500; code = "ABORTED"; break;
            default:                   status = 400; code = "NO_FILE"; break;
        }

        JsonDocument doc;
        doc["ok"] = (result == Result::Ok);
        if (result == Result::Ok) {
            doc["restart"] = true;
        } else {
            doc["error"] = code;
            if (job.detail.length() > 0) doc["detail"] = job.detail;
        }
        String out;
        serializeJson(doc, out);
        ws.sendHeader("Cache-Control", "no-store");
        ws.send(status, "application/json", out);

        job = Job();

        if (result == Result::Ok) {
            // Pause, damit die Antwort noch hinausgeht; Betriebszeit sichern wie beim Neustart
            // ueber die Einstellungen.
            Runtime::flush();
            delay(500);
            ESP.restart();
        }
    }
}

namespace FirmwareUpdate {

    // --------------------------------------------------------------
    // Public functions
    // --------------------------------------------------------------

    void setupRoutes() {
        wlanManager.getWebserver().on("/api/update/firmware", HTTP_POST, onUploadDone, onUpload);
    }
}
