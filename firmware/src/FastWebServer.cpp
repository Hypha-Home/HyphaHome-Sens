// ===================================================================
// File:            FastWebServer.cpp
// Author:          EideardVMR (full AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#include "FastWebServer.h"

/// Nachbau von WebServer::handleClient() aus dem Arduino-Core 2.0.17. Abweichung ist allein
/// die Wartezeit im Zustand HC_WAIT_READ; alles andere steht hier nur, weil sich die eine
/// Zeile sonst nicht austauschen lässt.
///
/// Der Zweig HC_WAIT_CLOSE ist im Original stillgelegt (ein Hinweis auf ein Chrome-Problem
/// steht dort als Kommentar), die Verbindung wird also direkt nach dem Beantworten geschlossen.
/// Das wird hier so beibehalten — der Zustand kann nur noch über einen früheren Durchlauf
/// erreicht werden und bleibt deshalb der Vollständigkeit halber stehen.
void FastWebServer::handleClient() {
    if (_currentStatus == HC_NONE) {
        _currentClient = _server.available();
        if (!_currentClient) {
            if (_nullDelay) {
                delay(1);
            }
            return;
        }
        _currentStatus = HC_WAIT_READ;
        _statusChange = millis();
    }

    bool keepCurrentClient = false;
    bool callYield = false;

    if (_currentClient.connected()) {
        switch (_currentStatus) {
            case HC_NONE:
                break;

            case HC_WAIT_READ:
                if (_currentClient.available()) {
                    if (_parseRequest(_currentClient)) {
                        // setTimeout() erwartet Sekunden, die Konstante steht in Millisekunden.
                        _currentClient.setTimeout(HTTP_MAX_SEND_WAIT / 1000);
                        _contentLength = CONTENT_LENGTH_NOT_SET;
                        _handleRequest();
                    }
                } else {
                    // Hier und nur hier weicht die Klasse vom Original ab: Dort steht
                    // HTTP_MAX_DATA_WAIT mit 5000 ms.
                    if (millis() - _statusChange <= FAST_WEBSERVER_DATA_WAIT_MS) {
                        keepCurrentClient = true;
                    }
                    callYield = true;
                }
                break;

            case HC_WAIT_CLOSE:
                if (millis() - _statusChange <= HTTP_MAX_CLOSE_WAIT) {
                    keepCurrentClient = true;
                    callYield = true;
                }
        }
    }

    if (!keepCurrentClient) {
        _currentClient = WiFiClient();
        _currentStatus = HC_NONE;
        _currentUpload.reset();
        _currentRaw.reset();
    }

    if (callYield) {
        yield();
    }
}
