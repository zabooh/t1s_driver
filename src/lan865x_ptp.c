/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2025 Microchip Technology Inc. */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/ptp_clock_kernel.h>
#include "oa_tc6.h"
#include "lan865x_ptp.h"

#define LAN865X_PTP_SEC_REG   0x1000  // Beispieladresse, anpassen!
#define LAN865X_PTP_NSEC_REG  0x1004  // Beispieladresse, anpassen!

static int lan865x_ptpci_gettime64(struct ptp_clock_info *ptpci, struct timespec64 *ts)
{
    struct lan865x_adapter *adapter = container_of(ptpci, struct lan865x_adapter, ptp_clock_info);
    u32 sec = 0, nsec = 0;
    oa_tc6_read_register(adapter->tc6, LAN865X_PTP_SEC_REG, &sec);
    oa_tc6_read_register(adapter->tc6, LAN865X_PTP_NSEC_REG, &nsec);
    ts->tv_sec = sec;
    ts->tv_nsec = nsec;
    return 0;
}

static int lan865x_ptpci_settime64(struct ptp_clock_info *ptpci, const struct timespec64 *ts)
{
    struct lan865x_adapter *adapter = container_of(ptpci, struct lan865x_adapter, ptp_clock_info);
    oa_tc6_write_register(adapter->tc6, LAN865X_PTP_SEC_REG, ts->tv_sec);
    oa_tc6_write_register(adapter->tc6, LAN865X_PTP_NSEC_REG, ts->tv_nsec);
    return 0;
}

static int lan865x_ptpci_adjtime(struct ptp_clock_info *ptpci, s64 delta)
{
    struct lan865x_adapter *adapter = container_of(ptpci, struct lan865x_adapter, ptp_clock_info);
    u32 sec = 0, nsec = 0;
    oa_tc6_read_register(adapter->tc6, LAN865X_PTP_SEC_REG, &sec);
    oa_tc6_read_register(adapter->tc6, LAN865X_PTP_NSEC_REG, &nsec);

    s64 nsec64 = (s64)nsec + delta;

    // Korrektur ohne 64-Bit-Division
    while (nsec64 >= 1000000000LL) {
        sec++;
        nsec64 -= 1000000000LL;
    }
    while (nsec64 < 0) {
        sec--;
        nsec64 += 1000000000LL;
    }
    nsec = (u32)nsec64;

    oa_tc6_write_register(adapter->tc6, LAN865X_PTP_SEC_REG, sec);
    oa_tc6_write_register(adapter->tc6, LAN865X_PTP_NSEC_REG, nsec);
    return 0;
}

static int lan865x_ptpci_adjfine(struct ptp_clock_info *ptpci, long scaled_ppm)
{
    // Hier muss die Umrechnung und Registeranpassung für die Frequenzkorrektur erfolgen.
    // Beispiel: oa_tc6_write_register(adapter->tc6, LAN865X_PTP_FREQ_ADJ_REG, regval);
    return 0;
}

static struct ptp_clock_info lan865x_ptp_clock_info = {
    .owner      = THIS_MODULE,
    .name       = "LAN865x PHC",
    .max_adj    = 999999999,
    .gettime64  = lan865x_ptpci_gettime64,
    .settime64  = lan865x_ptpci_settime64,
    .adjtime    = lan865x_ptpci_adjtime,
    .adjfine    = lan865x_ptpci_adjfine,
};

int lan865x_ptp_init(struct lan865x_adapter *adapter)
{
    adapter->ptp_clock = ptp_clock_register(&lan865x_ptp_clock_info, &adapter->netdev->dev);
    if (IS_ERR_OR_NULL(adapter->ptp_clock))
        return -ENODEV;
    adapter->ptp_clock_info = lan865x_ptp_clock_info;
    return 0;
}

void lan865x_ptp_remove(struct lan865x_adapter *adapter)
{
    if (adapter->ptp_clock)
        ptp_clock_unregister(adapter->ptp_clock);
}