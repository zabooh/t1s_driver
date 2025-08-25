/* SPDX-License-Identifier: GPL-2.0+ */
/* Copyright (C) 2025 Microchip Technology Inc. */

#ifndef _LAN865X_PTP_H_
#define _LAN865X_PTP_H_

#include <linux/ptp_clock_kernel.h>

struct lan865x_adapter {
    struct net_device *netdev;
    struct oa_tc6 *tc6;
    struct ptp_clock *ptp_clock;
    struct ptp_clock_info ptp_clock_info;
};

int lan865x_ptp_init(struct lan865x_adapter *adapter);
void lan865x_ptp_remove(struct lan865x_adapter *adapter);

#endif /* _LAN865X_PTP_H_ */