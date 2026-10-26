# Der Hypha Home - Sens
Dieses DIY Projekt ist ein Multisensor und wie der Name schon sagt, besitzt er mehrere Sensoren:
* LD2410 für die Bewegungs und Präsenzerkennung
* SHT41 für die Temperatur und Luftfeuchtemessung
* SCD41 für die CO2 Messung
* BHT1750 für eine Lichtmessung
* ENS160 für eine VOC messung (enthalten ist auch ein AHT21, der aber nur für den ENS160 zuständig ist.)

## Ziele
* Ein Sensor pro Raum der schnell und zuverlässig Daten sendet.
* Ersatz für Raumthermostate. 
* Feste Spannungsversorgung (nicht Batteriebetrieben)
* Kostengünstig und flexibel in der Ausstattung

Der Sensor kann entweder mit einem Steckernetzteil oder (wer sowieso saniert/renoviert) an einer Unterputzdose mit fester 5-30VDC Spannungsversorgung montiert werden.

Mit dem SHT41 Temperatursensor und eingerichtetem WLAN kann der Hypha Home - Sens direkt mit einem Shelly kommunizieren und diesen steuern. Damit fungiert er ebenfalls als Raumthermostat für eine Fußbodenheizung oder eines Heizkörpers wenn der zuständige Stellantrieb über den Shelly gesteuert wird.

Alle Sensoren werden via MQTT an einen eigenen Broker geschickt. Für Home Assistant ist sogar ein Discovery Protokoll vorhanden. Damit ist eine Aufnahme in das Home Assistant Smart-Home kinderleicht. 

Im Home-Assistant kann man sich dann vieles ausdenken: 
* Das Licht bei bewegung einschalten, aber nur wenn hell genug ist. 
* Einen Alarm auslösen wenn die Luftqualität schlecht ist und es mal wieder zeit wäre zu lüften.
* ...

## Aktuell umgesetzt
Aktuell befindet sich das Projekt im Prototypstadium. Die Software funktioniert schon sehr zuverlässig. Die Platine hat aber noch viele Mängel. Hier wird es eine neue Revision geben. Auch das Gehäuse ist noch sehr rudimentär. 
> Vor allem beim Platinen- und Gehäusedesign würde ich mich über unterstützung freuen! 

Aktuell befinden sich 2 Prototypen bei mir im Haus im einsatz (seit knapp 20 Tagen) und verrichten dort einen schon sehr zuverlässigen Dienst. Das Licht schaltet schnell ein und auch nur dann wenn es gebraucht wird (zu dunkel). Die Heizungssteuerung funktioniert auch, wenn auch noch sehr rudimentär! Und damit kommen wir zu dem was noch fehlt:

## Was noch fehlt
Die Heizungssteuerung wird noch neue Modi, basierend auf einer 2-Punkt steuerung und einer PWM Steuerung, erhalten. 
Ebenfalls soll der Sens direkt die Lichtsteuerung über einen Shelly übernehmen können. (Diese Funktionen sind nur verbaut, wenn z.B. das Home Assistant ausfällt, sollen Kernfunktionen weiterhin bereitstehen)

## Besondere funktionen (ungetestet)
### Brandfrüherkennung
Der sensor kann anhand der Temperatur, des CO2 Werts und den VOC Wertes erkennen ob es im Raum ggf. brennt. Dies ist keine sher genaue Meldung und vor allem ersetzt es keineswegs einen zertifizierten Rauchmelder. Aber es kann dennoch helfen. Bei mir gab es bisher keinen Fehlalarm. Aber auch keinen wirklichen Alarm (zum Glück)