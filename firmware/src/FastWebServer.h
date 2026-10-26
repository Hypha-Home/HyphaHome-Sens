// ===================================================================
// File:            FastWebServer.h
// Author:          EideardVMR (full AI)
// Year:            2026
// Last modified:   2026-10-24
// ===================================================================
#pragma once
#include <WebServer.h>

/// @brief Wartezeit auf die Anfragedaten einer frisch angenommenen Verbindung, in Millisekunden.
///
/// Der Ersatz für HTTP_MAX_DATA_WAIT (5000 ms) aus der Bibliothek. Im eigenen Netz liegen
/// zwischen dem Zustandekommen der Verbindung und dem Eintreffen der Anfrage wenige
/// Millisekunden — der Browser schickt sie unmittelbar hinterher. 250 ms sind dafür reichlich.
///
/// Was passiert, wenn der Wert zu knapp ist: Die Verbindung wird geschlossen, der Browser
/// merkt das und baut eine neue auf. Es geht also nichts verloren, es kostet nur einen zweiten
/// Versuch. Deshalb darf man hier ruhig knapp sein — im Zweifel diesen Wert erhöhen.
#define FAST_WEBSERVER_DATA_WAIT_MS 250

/// @brief WebServer, der leerlaufende Verbindungen schnell wieder freigibt.
///
/// **Das Problem.** Der `WebServer` der Bibliothek bedient genau eine TCP-Verbindung
/// gleichzeitig: Eine neue nimmt er erst an, wenn die vorige abgeschlossen ist. Angenommen
/// wird sie über `lwip_accept()`, und das liefert eine Verbindung bereits, sobald der
/// TCP-Handshake steht — **bevor ein einziges Byte der Anfrage da ist**. Kommt dann nichts,
/// hält `handleClient()` diese stumme Verbindung bis zu `HTTP_MAX_DATA_WAIT` = 5000 ms fest
/// und nimmt in dieser Zeit niemanden sonst an.
///
/// Genau das tritt regelmäßig ein, weil jede Antwort der Bibliothek unbedingt
/// `Connection: close` trägt. Jede Anfrage braucht dadurch einen neuen Socket, und Browser
/// reagieren darauf, indem sie Verbindungen auf Vorrat öffnen. Jeder solche Socket, den das
/// Gerät vor der echten Anfrage aufgreift, kostet fünf Sekunden Stillstand. Gemessen am
/// 10.10.2026: Seitenaufbau 15,9 s, einzelne Anfragen 4,5 bis 5,4 s, manche gescheitert.
///
/// **Die Lösung hier.** `handleClient()` ist `virtual` und alle benötigten Member sind
/// `protected`. Diese Klasse schreibt die Methode deshalb mit derselben Logik neu und tauscht
/// nur die eine Wartezeit aus. Die Routen, die Handler und der übrige Umgang mit dem Server
/// bleiben unverändert.
///
/// **Was damit nicht behoben ist:** Verbindungen werden weiterhin nacheinander bedient. Bei
/// 250 ms statt 5000 ms fällt das nicht mehr auf, aber die Bauart bleibt. Wer echte
/// Nebenläufigkeit braucht — etwa für Server-Sent Events statt Polling — kommt um einen
/// Wechsel auf `ESPAsyncWebServer` nicht herum. Dann entfällt diese Klasse ersatzlos.
class FastWebServer : public WebServer {
public:
    using WebServer::WebServer;

    /// @brief Wie WebServer::handleClient(), aber mit FAST_WEBSERVER_DATA_WAIT_MS statt
    /// HTTP_MAX_DATA_WAIT.
    void handleClient() override;
};
