/* Mehrsprachigkeit der Oberflaeche.
 *
 * Die Schnittstelle des Geraets schickt keine Texte, nur Schluessel: Fehler als Code
 * ("WRONG_PASSWORD"), Zustaende als Wort ("connected", "taskWatchdog"). Was der Nutzer liest,
 * entscheidet allein diese Schicht.
 *
 * Zwei Arten von Texten:
 *   - Statische Texte in den Seiten: Das deutsche Original steht im HTML, das Element traegt
 *     data-t="schluessel". Ist die Sprache nicht Deutsch, setzt apply() den Text aus dem
 *     Woerterbuch ein. Fehlt der Schluessel dort, bleibt das deutsche Original stehen.
 *       <h2 data-t="common.operation">Betrieb</h2>
 *       <input data-t-attr="placeholder:login.password_hint;title:login.tip">
 *     Der Inhalt eines solchen Elements wird als HTML ersetzt (kleine Auszeichnungen wie
 *     <strong> sind erlaubt); Elemente mit eigenen Kindern, die man braucht (id, data-field),
 *     duerfen deshalb nicht selbst data-t tragen.
 *   - Texte aus dem Skript (Meldungen, Zustaende): I18n.t('schluessel', { name: wert }).
 *     Diese stehen auch im deutschen Woerterbuch (lang-de.js).
 *
 * Woerterbuecher: lang-de.js (nur Skripttexte, steckt in jeder Seite) und fuer jede weitere
 * Sprache lang-xx.js mit allen Texten. Das Paket einer fremden Sprache wird nicht mit der Seite
 * ausgeliefert, sondern vom Kopf der Seite vor dem Zeichnen nachgeladen (siehe I18N_LOADER in
 * den Seiten). Bis die Texte ersetzt sind, ist der Seiteninhalt unsichtbar (Klasse i18n-wait),
 * damit nie erst die falsche Sprache aufblitzt.
 *
 * Gewaehlt wird die Sprache aus localStorage 'hyphahome-sens.lang', sonst aus dem Browser; fehlt
 * sie oder ein einzelner Schluessel, gilt Deutsch. Ist auch dort nichts zu finden, kommt der
 * Schluessel selbst zurueck, damit eine Luecke sichtbar bleibt statt leer zu sein.
 */
const I18n = (function () {
    'use strict';

    const LANG_KEY = 'hyphahome-sens.lang';
    const FALLBACK = 'de';
    const dictionaries = {};

    function readLanguage() {
        try {
            const stored = localStorage.getItem(LANG_KEY);
            if (stored) return stored;
        } catch (e) {
            /* Speicher gesperrt: Browsersprache nehmen */
        }
        return ((navigator.language || FALLBACK) + '').slice(0, 2).toLowerCase();
    }

    const language = readLanguage();

    function register(lang, dictionary) {
        dictionaries[lang] = Object.assign(dictionaries[lang] || {}, dictionary);
    }

    function lookup(key) {
        const wanted = dictionaries[language];
        if (wanted && Object.prototype.hasOwnProperty.call(wanted, key)) return wanted[key];
        const fallback = dictionaries[FALLBACK];
        if (fallback && Object.prototype.hasOwnProperty.call(fallback, key)) return fallback[key];
        return null;
    }

    /* Ersetzt {name} durch params.name. */
    function fill(text, params) {
        if (!params) return text;
        return text.replace(/\{(\w+)\}/g, function (whole, name) {
            return params[name] === undefined || params[name] === null ? whole : String(params[name]);
        });
    }

    /* Pakete, die der Seitenkopf schon geladen hat (lang-xx.js legt sie in window.I18N_PACK ab,
     * noch bevor dieses Skript existiert). */
    Object.keys(window.I18N_PACK || {}).forEach(function (lang) {
        register(lang, window.I18N_PACK[lang]);
    });

    /* Setzt die Texte der Seite (oder eines Teilbaums) in der gewaehlten Sprache ein. */
    function apply(root) {
        const scope = root || document;

        scope.querySelectorAll('[data-t]').forEach(function (element) {
            const text = lookup(element.dataset.t);
            if (text !== null) element.innerHTML = text;
        });

        scope.querySelectorAll('[data-t-attr]').forEach(function (element) {
            element.dataset.tAttr.split(';').forEach(function (pair) {
                const colon = pair.indexOf(':');
                if (colon < 1) return;
                const text = lookup(pair.slice(colon + 1).trim());
                if (text !== null) element.setAttribute(pair.slice(0, colon).trim(), text);
            });
        });
    }

    /* Die Sprachen, zwischen denen man waehlen kann: Kuerzel und Name in der Sprache selbst
     * (nicht uebersetzt, damit man sie auch erkennt, wenn man die Sprache nicht versteht).
     * Eine neue Sprache braucht zusaetzlich lang-xx.js und einen Eintrag im Seitenkopf. */
    const LANGUAGES = { de: 'Deutsch', en: 'English' };

    function setLanguage(lang) {
        try { localStorage.setItem(LANG_KEY, lang); } catch (e) { /* egal */ }
        window.location.reload();
    }

    /* Auswahl direkt ueber dem Abmelden-Link der Seitenleiste (sofern die Seite eine hat). */
    function mountSwitcher() {
        const footer = document.querySelector('.sidebar-footer');
        if (!footer) return;

        const select = document.createElement('select');
        select.className = 'lang-select';
        select.setAttribute('aria-label', 'Language');
        Object.keys(LANGUAGES).forEach(function (code) {
            const option = document.createElement('option');
            option.value = code;
            option.textContent = LANGUAGES[code];
            select.appendChild(option);
        });
        select.value = LANGUAGES[language] ? language : FALLBACK;
        select.addEventListener('change', function () { setLanguage(select.value); });
        footer.insertBefore(select, footer.firstChild);
    }

    document.documentElement.lang = language;
    apply();
    mountSwitcher();
    document.documentElement.classList.remove('i18n-wait');

    return {
        language: language,
        register: register,
        apply: apply,

        /* Merkt die Sprache und laedt die Seite neu, damit alles in der neuen Sprache erscheint. */
        setLanguage: setLanguage,

        /* @param key      Schluessel, z.B. 'wlan.state.connected'
         * @param params   Optional, Werte fuer {name}-Platzhalter
         * @param fallback Optional, Text wenn der Schluessel unbekannt ist (sonst der Schluessel) */
        t(key, params, fallback) {
            const text = lookup(key);
            return text === null ? (fallback !== undefined ? fallback : key) : fill(text, params);
        },

        /* Einzahl/Mehrzahl: sucht key + '.one' bei n === 1, sonst key + '.other'; {n} ist n. */
        plural(key, n, params) {
            return this.t(key + (n === 1 ? '.one' : '.other'), Object.assign({ n: n }, params));
        },

        /* Text zu einem Fehlercode der Schnittstelle. Ein unbekannter Code zeigt sich samt Code,
         * damit er sich melden laesst. */
        error(code, params) {
            const text = lookup('error.' + code);
            if (text !== null) return fill(text, params);
            return code ? lookup('error.UNKNOWN') + ' (' + code + ')' : lookup('error.UNKNOWN');
        }
    };
})();
