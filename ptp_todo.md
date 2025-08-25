# Implementierungsplan: Hardware-PTP für LAN865x Linux-Treiber

Dieser Leitfaden beschreibt die notwendigen Schritte, um die Hardware-PTP-Funktionalität des Microchip LAN865x im Linux-Treiber zu implementieren. Ziel ist, dass Anwendungen wie `ptp4l` über das PHC-Subsystem auf die präzise Hardwareuhr zugreifen können.

---

## 1. **Hardware-Spezifikation prüfen**
- Studiere das LAN865x-Datenblatt und die Registerbeschreibung für PTP/Wallclock.
- Identifiziere alle relevanten Register für:
  - Zeit lesen/setzen
  - Zeitstempel-Erfassung (TX/RX)
  - Event-Handling (EXTTS, PEROUT)
  - Frequenz- und Zeitkorrektur

---

## 2. **PTP-Datenstrukturen im Treiber anlegen**
- Lege eine eigene Struktur für den PTP-Kontext an (`struct lan865x_ptp`).
- Enthält:
  - PTP-Clock-Handle (`struct ptp_clock *`)
  - PTP-Clock-Info (`struct ptp_clock_info`)
  - Locks für Synchronisation
  - Queues für TX/RX-Timestamps
  - Event- und Interrupt-Status

---

## 3. **Registerzugriffe implementieren**
- Schreibe Funktionen zum Lesen/Schreiben der PTP-Register:
  - Zeit lesen (`ptp_clock_get`)
  - Zeit setzen (`ptp_clock_set`)
  - Zeit anpassen (`adjtime`, `adjfine`)
  - Event-Konfiguration (EXTTS, PEROUT)
- Nutze die vorhandenen SPI-Registerfunktionen (`oa_tc6_read_register`, `oa_tc6_write_register`).

---

## 4. **PTP-Clock-Info ausfüllen und registrieren**
- Implementiere die Methoden für `struct ptp_clock_info`:
  - `gettime64`
  - `settime64`
  - `adjtime`
  - `adjfine`
  - `enable`
  - `do_aux_work` (optional)
- Registriere die PTP-Clock im Kernel mit `ptp_clock_register`.

---

## 5. **TX/RX-Timestamping integrieren**
- Erfasse Hardware-Zeitstempel für ausgehende und eingehende Pakete:
  - Lese die Zeitstempel aus den entsprechenden LAN865x-Registern.
  - Ordne die Zeitstempel den jeweiligen Paketen (`struct sk_buff`) zu.
  - Setze die Zeitstempel in die `skb_shared_hwtstamps`-Struktur.
- Implementiere eine Queue für ausstehende Zeitstempel (wie im LAN743x).

---

## 6. **Event- und Interrupt-Handling**
- Implementiere Interrupt-Handler für PTP-Events:
  - EXTTS (externe Zeitereignisse)
  - PEROUT (periodische Ausgaben)
  - Fehler- und Status-Events
- Melde Events an das PHC-Subsystem weiter.

---

## 7. **GPIO/Pin-Konfiguration für PTP**
- Falls die Hardware spezielle Pins für PTP-Events nutzt:
  - Implementiere Funktionen zur Konfiguration der Pins (z.B. für EXTTS, PEROUT).
  - Nutze ggf. Multiplexing-Funktionen wie im LAN743x.

---

## 8. **IOCTL- und User-Interface**
- Implementiere die IOCTL-Schnittstelle für Hardware-Timestamping (`SIOCSHWTSTAMP`).
- Ermögliche die Konfiguration der Zeitstempelung über `ethtool` und `ioctl`.

---

## 9. **Integration in Treiber-Lifecycle**
- Initialisiere die PTP-Funktionen beim Treiberstart (`probe`, `init`).
- Registriere die PTP-Clock beim Öffnen der Netzwerkschnittstelle.
- Gib Ressourcen beim Schließen/Freigeben wieder frei.

---

## 10. **Test und Validierung**
- Teste die Integration mit `ptp4l` und anderen PHC-Anwendungen.
- Prüfe die Genauigkeit und Zuverlässigkeit der Zeitstempel.
- Führe Stresstests und Fehlerbehandlung durch.

---

## Beispiel: Wichtige Funktionen (analog zu LAN743x)

```c
// Initialisierung
int lan865x_ptp_init(struct lan865x_adapter *adapter);

// Zeit lesen
static int lan865x_ptpci_gettime64(struct ptp_clock_info *ptpci, struct timespec64 *ts);

// Zeit setzen
static int lan865x_ptpci_settime64(struct ptp_clock_info *ptpci, const struct timespec64 *ts);

// Zeit anpassen
static int lan865x_ptpci_adjtime(struct ptp_clock_info *ptpci, s64 delta);
static int lan865x_ptpci_adjfine(struct ptp_clock_info *ptpci, long scaled_ppm);

// TX/RX-Timestamping
void lan865x_ptp_tx_timestamp_skb(struct lan865x_adapter *adapter, struct sk_buff *skb);
void lan865x_ptp_rx_timestamp_skb(struct lan865x_adapter *adapter, struct sk_buff *skb);

// Event-Handling
void lan865x_ptp_isr(void *context);

// IOCTL
int lan865x_ptp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
```

---

## **Zusammenfassung**

Mit diesem Plan kannst du die Hardware-PTP-Funktionen des LAN865x so in den Treiber integrieren, dass sie über das PHC-Subsystem und Anwendungen wie `ptp4l` nutzbar sind. Die Referenzimplementierung des LAN743x-Treibers ist dabei eine gute Hilfe für den Aufbau und die Struktur der Funktionalität.
