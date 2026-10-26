/* ui.js — Bausteine der Oberflaeche, die mehrere Seiten brauchen.
 *
 * Getrennt von api.js: Dort geht es um die Schnittstelle zum Geraet, hier um Darstellung.
 *
 * Bewusst eine Datei und nicht drei: Jede zusaetzliche Datei ist eine weitere
 * TCP-Verbindung, und der Webserver des Geraets bedient nur eine gleichzeitig
 * (siehe FastWebServer.h).
 */

/* Anzeigedauer einer Meldung in Millisekunden, ab dem Einfliegen gerechnet. */
const TOAST_DURATION_MS = 3000;

/* Kurze Meldung, die oben mittig einfliegt und von selbst wieder verschwindet.
 *
 * Die Technik heisst "Toast" (manchmal auch Snackbar): eine Meldung, die sich nicht wegklicken
 * laesst und den Arbeitsfluss nicht unterbricht — anders als ein Dialog, der auf eine Antwort
 * wartet. Passend fuer "gespeichert", unpassend fuer alles, was eine Entscheidung braucht.
 *
 * Mehrere Aufrufe hintereinander ueberschreiben dieselbe Meldung, statt sich zu stapeln. Bei
 * einem Speichern-Knopf ist das das gewuenschte Verhalten: Es zaehlt die letzte Rueckmeldung.
 */
const Toast = (function () {
    'use strict';

    let element = null;
    let hideTimer = null;
    let removeTimer = null;

    /* Das Element wird erst beim ersten Aufruf angelegt. So muss keine Seite daran denken,
     * einen Platzhalter ins HTML zu schreiben. */
    function ensureElement() {
        if (element) return element;
        element = document.createElement('div');
        element.className = 'toast';
        element.setAttribute('role', 'status');      /* Vorlesesoftware liest die Meldung vor */
        element.setAttribute('aria-live', 'polite'); /* aber ohne zu unterbrechen */
        document.body.appendChild(element);
        return element;
    }

    /* @param text Der anzuzeigende Text
     * @param kind 'ok', 'err' oder 'warn' — bestimmt die Farbgebung */
    function show(text, kind) {
        const node = ensureElement();

        clearTimeout(hideTimer);
        clearTimeout(removeTimer);

        node.textContent = text;
        node.className = 'toast ' + (kind || 'ok');

        /* Ein erzwungener Layout-Durchlauf, damit der Browser den Startzustand uebernimmt,
         * bevor die Klasse zum Einfliegen dazukommt. Ohne das springt die Meldung ohne
         * Uebergang an ihren Platz, weil Hinzufuegen und Verschieben in einem Durchgang
         * zusammenfallen wuerden. */
        void node.offsetHeight;
        node.classList.add('toast-visible');

        hideTimer = setTimeout(function () {
            node.classList.remove('toast-visible');
            /* Erst nach dem Ausfliegen wirklich entfernen, sonst bricht der Uebergang ab. */
            removeTimer = setTimeout(function () {
                if (node.parentNode) node.parentNode.removeChild(node);
                element = null;
            }, 400);
        }, TOAST_DURATION_MS);
    }

    return {
        show: show,
        ok(text)    { show(text, 'ok'); },
        error(text) { show(text, 'err'); },
        warn(text)  { show(text, 'warn'); }
    };
})();


/* Anzeige von Messwerten ueber data-Attribute.
 *
 * Lag vorher in jeder Seite einzeln und war nach der ersten Erweiterung bereits
 * auseinandergelaufen: data-good gab es nur in sensors.html. Deshalb jetzt hier, einmal.
 *
 * Unterstuetzte Attribute an einem Element:
 *
 *   data-field="sht41.temperature"  Pfad in der Antwort von GET /api/sensors
 *   data-digits="1"                 Nachkommastellen
 *   data-unit="°C"                  Einheit, mit umbruchfestem Abstand angehaengt; steht unter
 *                                   unit.<Einheit> ein Eintrag im Woerterbuch, wird er benutzt
 *   data-bool="Ja|Nein"             als Abzeichen gruen/grau statt als Text
 *   data-good="normal,good"         Zustandsschluessel als Abzeichen: gruen bei Treffer, sonst orange
 *   data-i18n="level"               Wert ist ein Schluessel der Schnittstelle; angezeigt wird
 *                                   I18n.t('level.' + Wert). Zusammen mit data-good vergleicht
 *                                   data-good gegen den Schluessel, nicht gegen den Text.
 *   data-status="sht41"             dreistufiges Verbindungsabzeichen fuer diesen Sensor
 *   data-visible-if="sht41.smoothing>1"
 *                                   Element nur zeigen, wenn die Bedingung stimmt. Pfad wie bei
 *                                   data-field, Vergleich mit >, >=, <, <= oder == und einer Zahl.
 *                                   Sinnvoll an einer ganzen Zeile (.row).
 *
 * Eine zusaetzliche Zeile braucht dadurch kein JavaScript, nur ein weiteres Element.
 */
const SensorView = (function () {
    'use strict';

    const EMPTY = '–';     /* Gedankenstrich, wenn kein Wert vorliegt */
    const NBSP  = ' ';     /* Abstand vor der Einheit, umbruchfest */

    /* Holt "sht41.temperature" aus dem verschachtelten Objekt. */
    function valueAt(source, path) {
        return path.split('.').reduce(function (node, key) {
            return (node === null || node === undefined) ? undefined : node[key];
        }, source);
    }

    function badge(text, kind) {
        return '<span class="badge ' + kind + '">' + text + '</span>';
    }

    /* Drei Zustaende, nicht zwei: "aus" und "fehlt" bedeuten fuer den Nutzer etwas voellig
     * Verschiedenes. Genau dafuer liefert die Schnittstelle enabled und connected getrennt. */
    function statusBadge(sensor) {
        if (!sensor) return badge(I18n.t('status.unknown'), 'muted');
        if (!sensor.enabled) return badge(I18n.t('status.disabled'), 'muted');
        return sensor.connected ? badge(I18n.t('status.connected'), 'ok') : badge(I18n.t('status.missing'), 'err');
    }

    /* Uebersetzt den Schluessel, wenn die Zelle data-i18n traegt; sonst bleibt der Wert. */
    function shown(cell, value) {
        return cell.dataset.i18n !== undefined
            ? I18n.t(cell.dataset.i18n + '.' + value, null, value)
            : value;
    }

    function renderCell(cell, data) {
        const path   = cell.dataset.field;
        const sensor = data[path.split('.')[0]];
        const value  = valueAt(data, path);

        /* Ein fehlender oder abgeschalteter Sensor zeigt Striche statt seines letzten
         * Messwerts. Der steht im Geraet noch in der Variablen, waere hier aber eine
         * Behauptung ueber einen Zustand, der gerade nicht gemessen wird. */
        const stale = !sensor || !sensor.enabled || !sensor.connected;
        const empty = stale || value === undefined || value === null;

        if (cell.dataset.bool !== undefined) {
            const labels = cell.dataset.bool.split('|').map(function (label) {
                return I18n.t('bool.' + label, null, label);
            });
            cell.innerHTML = empty ? EMPTY
                : badge(value ? labels[0] : labels[1], value ? 'ok' : 'muted');
            return;
        }

        if (cell.dataset.good !== undefined) {
            const good = cell.dataset.good.split(',');
            const key  = String(value);
            cell.innerHTML = empty ? EMPTY : badge(shown(cell, key), good.includes(key) ? 'ok' : 'warn');
            return;
        }

        if (empty) {
            cell.textContent = EMPTY;
            return;
        }

        let text = (cell.dataset.digits !== undefined && typeof value === 'number')
            ? value.toFixed(Number(cell.dataset.digits))
            : shown(cell, String(value));
        if (cell.dataset.unit) text += NBSP + I18n.t('unit.' + cell.dataset.unit, null, cell.dataset.unit);
        cell.textContent = text;
    }

    /* Wertet "pfad>zahl" gegen die Antwort aus. Fehlt der Wert, gilt die Bedingung als nicht
     * erfuellt, das Element bleibt also verborgen. */
    function conditionHolds(condition, data) {
        const match = /^([\w.]+)\s*(>=|<=|==|>|<)\s*(-?[\d.]+)$/.exec(condition);
        if (!match) return true;
        const value = valueAt(data, match[1]);
        const limit = Number(match[3]);
        if (typeof value !== 'number') return false;
        switch (match[2]) {
            case '>':  return value > limit;
            case '>=': return value >= limit;
            case '<':  return value < limit;
            case '<=': return value <= limit;
            default:   return value === limit;
        }
    }

    return {
        /* Sammelt die Zellen einmal ein und liefert die Zeichenfunktion dafuer.
         * @param root Optionaler Teilbaum, sonst das ganze Dokument
         * @returns { render(data) } */
        create(root) {
            const scope = root || document;
            const valueCells  = Array.from(scope.querySelectorAll('[data-field]'));
            const statusCells = Array.from(scope.querySelectorAll('[data-status]'));
            const conditionals = Array.from(scope.querySelectorAll('[data-visible-if]'));

            return {
                render(data) {
                    conditionals.forEach(function (element) {
                        element.style.display =
                            conditionHolds(element.dataset.visibleIf, data) ? '' : 'none';
                    });
                    statusCells.forEach(function (cell) {
                        cell.innerHTML = statusBadge(data[cell.dataset.status]);
                    });
                    valueCells.forEach(function (cell) {
                        renderCell(cell, data);
                    });
                }
            };
        },

        badge: badge,
        statusBadge: statusBadge,
        valueAt: valueAt,
        EMPTY: EMPTY,
        NBSP: NBSP
    };
})();


/* Tabelle mit Kalibrierpunkten: Zeilen hinzufuegen, entfernen, auslesen.
 *
 * Gemeinsam genutzt von sensor-sht41.html und sensor-bh1750.html. Die beiden unterscheiden
 * sich nur in Messgroesse, Einheit und Wertebereich — die Mechanik ist dieselbe.
 *
 * Eine Falle beim Grid: Die drei Elemente einer Zeile sind Geschwister, nicht in ein
 * Zeilen-div verpackt. Ein Grid ordnet nur seine *direkten* Kinder in die Spalten ein, ein
 * Zwischenelement zerstoert die Ausrichtung. Zusammengehalten werden sie ueber eine
 * gemeinsame data-row-Kennung — entsprechend zaehlt count() unterschiedliche Kennungen und
 * nicht Elemente.
 */
const CalibrationTable = (function () {
    'use strict';

    return {
        /* @param opts.grid        Grid-Element, enthaelt bereits die Kopfzeile
         * @param opts.addButton   Knopf "Punkt hinzufuegen"
         * @param opts.hint        Element fuer die Zeile unter der Tabelle
         * @param opts.min         Kleinster zulaessiger Wert
         * @param opts.max         Groesster zulaessiger Wert
         * @param opts.step        Schrittweite der Zahlenfelder
         * @param opts.digits      Nachkommastellen beim Vorbelegen aus dem Rohwert
         * @param opts.rangeText   Fuer die Fehlermeldung, z.B. "-40 und 125 °C"
         * @param opts.currentRaw  Funktion, liefert den aktuellen Rohwert oder null
         * @param opts.addLabel    Funktion(rohwertText|null) -> Beschriftung des Knopfes
         * @returns { load(points, limit), collect(), count(), refreshAddLabel() } */
        create(opts) {
            let maxPoints = 0;

            function count() {
                const ids = new Set();
                opts.grid.querySelectorAll('[data-row]').forEach(function (cell) {
                    ids.add(cell.dataset.row);
                });
                return ids.size;
            }

            function makeInput(key, value) {
                const input = document.createElement('input');
                input.type = 'number';
                input.step = opts.step;
                input.min = String(opts.min);
                input.max = String(opts.max);
                input.dataset.key = key;
                input.value = (value === undefined || value === null) ? '' : String(value);
                input.addEventListener('input', function () {
                    input.classList.remove('invalid');
                });
                return input;
            }

            function updateAddLabel() {
                const raw = opts.currentRaw();
                opts.addButton.innerHTML =
                    opts.addLabel(raw === null ? null : raw.toFixed(opts.digits));
            }

            function updateHint() {
                const n = count();
                opts.addButton.disabled = n >= maxPoints;

                if (n === 0) {
                    opts.hint.textContent = I18n.t('js.calib.no_points');
                } else {
                    opts.hint.textContent = I18n.plural('js.calib.points_of_max', n, { max: maxPoints })
                        + (n >= maxPoints && n > 1 ? I18n.t('js.calib.limit_reached') : '');
                }
                updateAddLabel();
            }

            function addRow(measured, reference) {
                if (count() >= maxPoints) return;

                const id = String(Date.now()) + String(Math.random()).slice(2, 7);

                function cell(child) {
                    const div = document.createElement('div');
                    div.dataset.row = id;
                    div.appendChild(child);
                    return div;
                }

                const remove = document.createElement('button');
                remove.type = 'button';   /* sonst wuerde der Knopf das Formular abschicken */
                remove.className = 'grid-remove';
                remove.title = I18n.t('js.calib.remove_point');
                remove.setAttribute('aria-label', I18n.t('js.calib.remove_point'));
                remove.innerHTML = '&times;';
                remove.addEventListener('click', function () {
                    opts.grid.querySelectorAll('[data-row="' + id + '"]').forEach(function (c) {
                        c.remove();
                    });
                    updateHint();
                });

                opts.grid.appendChild(cell(makeInput('measured', measured)));
                opts.grid.appendChild(cell(makeInput('reference', reference)));
                opts.grid.appendChild(cell(remove));
                updateHint();
            }

            opts.addButton.addEventListener('click', function () {
                /* Den Rohwert schon eintragen: Man liest ihn sonst oben ab und schreibt ihn
                 * von Hand daneben. Einzutragen bleibt dann nur der Referenzwert. */
                const raw = opts.currentRaw();
                addRow(raw === null ? undefined : Number(raw.toFixed(opts.digits)), undefined);
            });

            return {
                count: count,
                refreshAddLabel: updateAddLabel,

                /* Uebernimmt Obergrenze und vorhandene Punkte vom Geraet. */
                load(points, limit) {
                    maxPoints = limit || 0;
                    (points || []).forEach(function (point) {
                        addRow(point.measured, point.reference);
                    });
                    updateHint();
                },

                /* Liest die Zeilen aus.
                 *
                 * Eine Zeile mit *beiden* Feldern leer wird uebergangen — das ist eine neu
                 * angelegte, noch nicht ausgefuellte Zeile. Ist nur *eines* leer, ist es ein
                 * Fehler: Ein halber Punkt ergibt keine Gerade.
                 *
                 * @returns Punkte-Array, oder null wenn etwas unzulaessig ist; dann wurde
                 *          bereits eine Meldung gezeigt */
                collect() {
                    const rows = new Map();
                    opts.grid.querySelectorAll('[data-row]').forEach(function (cellEl) {
                        const input = cellEl.querySelector('input');
                        if (!input) return;
                        if (!rows.has(cellEl.dataset.row)) rows.set(cellEl.dataset.row, {});
                        rows.get(cellEl.dataset.row)[input.dataset.key] = input;
                    });

                    const points = [];
                    let error = null;

                    const bad = function (v) {
                        return !Number.isFinite(v) || v < opts.min || v > opts.max;
                    };

                    rows.forEach(function (inputs) {
                        const a = inputs.measured, b = inputs.reference;
                        const rawA = a.value.trim(), rawB = b.value.trim();

                        if (rawA === '' && rawB === '') return;

                        if (rawA === '' || rawB === '') {
                            (rawA === '' ? a : b).classList.add('invalid');
                            error = error || I18n.t('js.calib.both_values');
                            return;
                        }

                        const x = Number(rawA), y = Number(rawB);
                        if (bad(x) || bad(y)) {
                            if (bad(x)) a.classList.add('invalid');
                            if (bad(y)) b.classList.add('invalid');
                            error = error
                                || I18n.t('js.calib.out_of_range', { range: opts.rangeText });
                            return;
                        }

                        points.push({ measured: x, reference: y });
                    });

                    if (error) {
                        Toast.error(error);
                        return null;
                    }
                    return points;
                }
            };
        }
    };
})();


/* Schalter "Sensor eingeschaltet" einer Sensorseite.
 *
 * Erwartet im HTML ein Formular #enableForm mit der Checkbox #enableBox und dem Knopf
 * #enableSave. Gespeichert wird nur sensors.<sensor>.enabled, alles andere bleibt unberuehrt;
 * die Firmware uebernimmt die Aenderung sofort (der Lesetakt des Sensors prueft das Flag).
 *
 * Aufruf: const enable = SensorEnable.bind('sht41');  und spaeter enable.load(config).
 */
const SensorEnable = (function () {
    'use strict';

    return {
        bind(sensor) {
            const form = document.getElementById('enableForm');
            const box  = document.getElementById('enableBox');
            const save = document.getElementById('enableSave');

            form.addEventListener('submit', async function (event) {
                event.preventDefault();

                save.disabled = true;
                save.textContent = I18n.t('common.saving');

                const body = { sensors: {} };
                body.sensors[sensor] = { enabled: box.checked };

                try {
                    await Api.request('/api/config', { method: 'POST', body: JSON.stringify(body) });
                    Toast.ok(I18n.t(box.checked ? 'sensor.enabled_toast' : 'sensor.disabled_toast'));
                } catch (error) {
                    if (error.unauthorized) { Api.toLogin(); return; }
                    Toast.error(I18n.t('common.save_failed', { message: error.message }));
                }

                save.disabled = false;
                save.textContent = I18n.t('common.save');
            });

            return {
                /* @param config Antwort von GET /api/config */
                load(config) {
                    const cfg = config.sensors && config.sensors[sensor];
                    box.checked = !!(cfg && cfg.enabled);
                    box.disabled = false;
                    save.disabled = false;
                }
            };
        }
    };
})();

/* Umschalter Hell/Dunkel: der Knopf #themeToggle steht in jeder Seite ausser Anmeldung und
 * Passwort-Reset. Er setzt data-theme auf <html> (der Anfangswert kommt vom Skript im Seitenkopf)
 * und merkt sich die Wahl fuer alle Seiten. Das Symbol zeigt das Design, zu dem er wechselt. */
(function () {
    'use strict';
    const root = document.documentElement;
    const button = document.getElementById('themeToggle');
    if (!button) return;

    function show() { button.textContent = root.getAttribute('data-theme') === 'dark' ? '☀' : '☾'; }

    button.addEventListener('click', function () {
        const next = root.getAttribute('data-theme') === 'dark' ? 'light' : 'dark';
        root.setAttribute('data-theme', next);
        try { localStorage.setItem('hyphahome-sens.theme', next); } catch (e) {}
        show();
    });
    show();
})();

/* Hostname in der Seitenleiste: statt des festen Titels steht dort der Hostname des Geraets.
 * Der zuletzt bekannte Name kommt sofort aus dem Speicher, damit beim Seitenwechsel nichts
 * umspringt; /api/session (ohne Anmeldung abrufbar) liefert den aktuellen und frischt ihn auf.
 * Ohne Verbindung zum Geraet, etwa beim Ansehen der Datei, bleibt der Text aus dem HTML. */
(function () {
    'use strict';
    const KEY = 'hyphahome-sens.hostname';
    const title = document.querySelector('.brand-title');
    if (!title) return;

    try {
        const cached = localStorage.getItem(KEY);
        if (cached) title.textContent = cached;
    } catch (e) {}

    Api.session().then(function (session) {
        if (!session || !session.hostname) return;
        title.textContent = session.hostname;
        try { localStorage.setItem(KEY, session.hostname); } catch (e) {}
    });
})();
