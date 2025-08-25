# Implementation Plan: Hardware PTP for LAN865x Linux Driver

This guide describes the necessary steps to implement the hardware PTP functionality of the Microchip LAN865x in the Linux driver. The goal is for applications like `ptp4l` to access the precise hardware clock via the PHC subsystem.

---

## 1. **Check Hardware Specification**
- Study the LAN865x datasheet and register description for PTP/Wallclock.
- Identify all relevant registers for:
  - Reading/setting time
  - Timestamp capture (TX/RX)
  - Event handling (EXTTS, PEROUT)
  - Frequency and time correction

---

## 2. **Create PTP Data Structures in the Driver**
- Define a dedicated structure for the PTP context (`struct lan865x_ptp`).
- Includes:
  - PTP clock handle (`struct ptp_clock *`)
  - PTP clock info (`struct ptp_clock_info`)
  - Locks for synchronization
  - Queues for TX/RX timestamps
  - Event and interrupt status

---

## 3. **Implement Register Access**
- Write functions to read/write the PTP registers:
  - Read time (`ptp_clock_get`)
  - Set time (`ptp_clock_set`)
  - Adjust time (`adjtime`, `adjfine`)
  - Event configuration (EXTTS, PEROUT)
- Use the existing SPI register functions (`oa_tc6_read_register`, `oa_tc6_write_register`).

---

## 4. **Fill and Register PTP Clock Info**
- Implement the methods for `struct ptp_clock_info`:
  - `gettime64`
  - `settime64`
  - `adjtime`
  - `adjfine`
  - `enable`
  - `do_aux_work` (optional)
- Register the PTP clock in the kernel with `ptp_clock_register`.

---

## 5. **Integrate TX/RX Timestamping**
- Capture hardware timestamps for outgoing and incoming packets:
  - Read the timestamps from the corresponding LAN865x registers.
  - Assign the timestamps to the respective packets (`struct sk_buff`).
  - Set the timestamps in the `skb_shared_hwtstamps` structure.
- Implement a queue for pending timestamps (as in LAN743x).

---

## 6. **Event and Interrupt Handling**
- Implement interrupt handlers for PTP events:
  - EXTTS (external time events)
  - PEROUT (periodic outputs)
  - Error and status events
- Report events to the PHC subsystem.

---

## 7. **GPIO/Pin Configuration for PTP**
- If the hardware uses special pins for PTP events:
  - Implement functions to configure the pins (e.g., for EXTTS, PEROUT).
  - Use multiplexing functions if needed, as in LAN743x.

---

## 8. **IOCTL and User Interface**
- Implement the IOCTL interface for hardware timestamping (`SIOCSHWTSTAMP`).
- Enable configuration of timestamping via `ethtool` and `ioctl`.

---

## 9. **Integration into Driver Lifecycle**
- Initialize the PTP functions during driver startup (`probe`, `init`).
- Register the PTP clock when opening the network interface.
- Release resources when closing/freeing.

---

## 10. **Testing and Validation**
- Test the integration with `ptp4l` and other PHC applications.
- Check the accuracy and reliability of the timestamps.
- Perform stress tests and error handling.

---

## Example: Important Functions (analogous to LAN743x)

```c
// Initialization
int lan865x_ptp_init(struct lan865x_adapter *adapter);

// Read time
static int lan865x_ptpci_gettime64(struct ptp_clock_info *ptpci, struct timespec64 *ts);

// Set time
static int lan865x_ptpci_settime64(struct ptp_clock_info *ptpci, const struct timespec64 *ts);

// Adjust time
static int lan865x_ptpci_adjtime(struct ptp_clock_info *ptpci, s64 delta);
static int lan865x_ptpci_adjfine(struct ptp_clock_info *ptpci, long scaled_ppm);

// TX/RX Timestamping
void lan865x_ptp_tx_timestamp_skb(struct lan865x_adapter *adapter, struct sk_buff *skb);
void lan865x_ptp_rx_timestamp_skb(struct lan865x_adapter *adapter, struct sk_buff *skb);

// Event Handling
void lan865x_ptp_isr(void *context);

// IOCTL
int lan865x_ptp_ioctl(struct net_device *netdev, struct ifreq *ifr, int cmd);
```

---

## **Summary**

With this plan, you can integrate the LAN865x hardware PTP functions into the driver so they are usable via the PHC subsystem and applications like `ptp4l`. The reference implementation of the LAN743x driver is a good guide for the structure and
