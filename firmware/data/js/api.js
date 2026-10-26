/* api.js — gemeinsamer Zugriff auf die REST-Schnittstelle des Geräts.
 *
 * Wird von allen Seiten eingebunden und haelt das Sitzungstoken an einer Stelle. Bewusst ohne
 * Modulsyntax (import/export): Die Seiten werden direkt vom Geraet ausgeliefert, es gibt
 * keinen Build-Schritt, und ein globales Objekt laedt zuverlaessiger als ein Modul.
 *
 * Zum Token: Es wird in localStorage gehalten und bei jeder Anfrage als Kopfzeile
 * X-Auth-Token gesendet. Ein Cookie gibt es bewusst nicht: Das schickt der Browser auch dann
 * mit, wenn eine fremde Webseite eine Anfrage an die IP des Geraets ausloest. Eine Kopfzeile
 * kann nur setzen, wer das Token kennt.
 */

/* Abstand zwischen zwei Abfragen der Messwerte, in Millisekunden.
 *
 * Hier zentral, damit sich die Taktung aller Seiten an einer Stelle aendern laesst.
 *
 * Zur Einordnung: Das Geraet misst selbst nur alle 5 Sekunden (READ_INTERVAL_MS in main.cpp).
 * Bei 1000 ms liefern also rund vier von fuenf Abfragen denselben Wert noch einmal. Das ist
 * Absicht — die Anzeige reagiert dadurch spuerbar schneller auf eine frische Messung, und eine
 * Anfrage mit ~800 Byte JSON pro Sekunde ist fuer ein Geraet im eigenen Netz unerheblich.
 * Wer es genauer abgestimmt haben will, setzt hier 5000 und nimmt dafuer bis zu 5 Sekunden
 * Verzoegerung in Kauf.
 */
const POLL_INTERVAL_MS = 500;

const Api = (function () {
    'use strict';

    const TOKEN_KEY = 'hyphahome-sens.token';

    /* localStorage kann werfen: im privaten Modus, bei blockierten Seitendaten oder in einem
     * eingebetteten Rahmen. Deshalb jeder Zugriff abgesichert — ohne Speicher laeuft die
     * Seite weiter, man muss sich nur nach jedem Neuladen neu anmelden. */
    function readToken() {
        try {
            return localStorage.getItem(TOKEN_KEY) || '';
        } catch (e) {
            return '';
        }
    }

    function writeToken(token) {
        try {
            if (token) {
                localStorage.setItem(TOKEN_KEY, token);
            } else {
                localStorage.removeItem(TOKEN_KEY);
            }
        } catch (e) {
            /* Nicht schlimm, siehe oben. */
        }
    }

    /* Fuehrt eine Anfrage an die Schnittstelle aus und haengt das Token an.
     *
     * Wirft bei 401 einen Fehler mit unauthorized === true, damit der Aufrufer zwischen
     * "nicht angemeldet" und "etwas anderes ist schiefgegangen" unterscheiden kann. */
    async function request(path, options) {
        const opts = options || {};
        const headers = Object.assign({}, opts.headers);

        const token = readToken();
        if (token) headers['X-Auth-Token'] = token;
        if (opts.body) headers['Content-Type'] = 'application/json';

        const response = await fetch(path, Object.assign({}, opts, { headers: headers }));

        if (response.status === 401) {
            writeToken('');
            const error = new Error('Nicht angemeldet');
            error.unauthorized = true;
            throw error;
        }

        if (!response.ok) {
            /* Die Schnittstelle schickt einen Code ({ "error": "WRONG_PASSWORD" }), keinen Text. */
            let body = {};
            try { body = await response.json(); } catch (e) { /* kein JSON im Rumpf */ }
            if (body.detail) console.warn('Gerät meldet:', body.detail);
            const error = new Error(body.error
                ? I18n.error(body.error, { status: response.status })
                : I18n.t('error.HTTP', { status: response.status }));
            error.code = body.error || '';
            error.status = response.status;
            throw error;
        }

        return response.json();
    }

    return {
        /* Das gespeicherte Token, leer wenn keines da ist. */
        get token() {
            return readToken();
        },

        /* Meldet an und merkt sich das Token.
         * @returns true bei Erfolg, false bei falschem Passwort.
         * Wirft nur, wenn das Geraet gar nicht antwortet. */
        async login(password) {
            const response = await fetch('/api/login', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ password: password })
            });

            if (response.status === 401) return false;
            if (!response.ok) throw new Error(I18n.t('error.HTTP', { status: response.status }));

            const data = await response.json();
            writeToken(data.token);
            return true;
        },

        /* Meldet ab. Scheitert das (Geraet weg), wird das Token trotzdem verworfen — sonst
         * haengt die Oberflaeche in einem Zustand fest, aus dem sie nicht herauskommt. */
        async logout() {
            try {
                await request('/api/logout', { method: 'POST' });
            } catch (e) {
                /* egal */
            }
            writeToken('');
        },

        /* Fragt den Zustand der Sitzung ab, ohne einen Fehler zu werfen.
         * @returns { authenticated, protected, expiresIn, hostname } */
        async session() {
            try {
                return await request('/api/session');
            } catch (e) {
                return { authenticated: false, protected: true, expiresIn: 0 };
            }
        },

        /* Die vollstaendige Konfiguration samt Betriebszeit. */
        config() {
            return request('/api/config');
        },

        /* Alle Messwerte, roh und kalibriert. */
        sensors() {
            return request('/api/sensors');
        },

        /* Alles fuer die Startseite in einer Antwort: Sensorstatus, Verbindungen, System. */
        dashboard() {
            return request('/api/dashboard');
        },

        /* Schickt den Nutzer zur Anmeldung. Von jeder Seite aufzurufen, die einen
         * unauthorized-Fehler faengt. */
        toLogin() {
            window.location.href = '/login';
        },

        request: request,

        /* Ruft load() sofort und danach im Abstand von POLL_INTERVAL_MS auf.
         *
         * Nimmt drei Dinge ab, die sonst jede Seite selbst bedenken muesste:
         *   - In einem Hintergrund-Tab wird nicht abgefragt. Niemand sieht die Werte, das
         *     Geraet soll dafuer nicht arbeiten. Beim Zurueckwechseln wird sofort aktualisiert.
         *   - Bei 401 endet die Schleife und es geht zur Anmeldung, statt im Sekundentakt
         *     gegen eine abgelaufene Sitzung zu laufen.
         *   - Laeuft ein Aufruf noch, wird kein zweiter gestartet.
         *
         * Solange eine Seite pollt, laeuft die Sitzung uebrigens nicht ab: Jede Abfrage setzt
         * die Frist im Geraet neu.
         *
         * @param load Funktion, die ein Promise liefert
         * @returns Funktion zum Beenden der Schleife
         */
        poll(load) {
            let timer = null;
            let running = false;
            let stopped = false;

            async function once() {
                if (running || stopped || document.hidden) return;
                running = true;
                try {
                    await load();
                } catch (error) {
                    if (error && error.unauthorized) {
                        stop();
                        Api.toLogin();
                        return;
                    }
                    /* Alles andere ist meist nur ein Aussetzer im WLAN. Weiterlaufen lassen,
                     * die naechste Abfrage faengt sich in der Regel wieder. */
                } finally {
                    running = false;
                }
            }

            function stop() {
                stopped = true;
                if (timer !== null) clearInterval(timer);
                timer = null;
                document.removeEventListener('visibilitychange', onVisible);
            }

            function onVisible() {
                if (!document.hidden) once();
            }

            document.addEventListener('visibilitychange', onVisible);
            timer = setInterval(once, POLL_INTERVAL_MS);
            once();

            return stop;
        }
    };
})();

/* "Abmelden" in der Seitenleiste ist ein Link auf logout, den es auf dem Geraet nicht gibt:
 * Abmelden aendert den Zustand und gehoert nicht hinter ein GET, das jede fremde Seite per
 * Bild-Tag ausloesen koennte. Der Klick wird hier abgefangen. */
document.addEventListener('click', async function (event) {
    /* Der Link steht je nach Tiefe der Seite als "./logout", "../logout" ... im HTML. */
    const link = event.target.closest && event.target.closest('a[href$="logout"]');
    if (!link) return;
    event.preventDefault();
    await Api.logout();
    window.location.href = '/login';
});
