// SPDX-License-Identifier: GPL-2.0+
/*
 * Microchip's LAN865x 10BASE-T1S MAC-PHY driver
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */
#ifndef LAN865X_PTP_H
#define LAN865X_PTP_H
#include "linux/ptp_clock_kernel.h"
#include "oa_tc6.h"
#define PTP_FLAG_PTP_CLOCK_REGISTERED 0x0815

struct lan865x_priv;
struct lan865x_ptp {
	int flags;

	/* command_lock: used to prevent concurrent ptp commands */
	struct mutex	command_lock;

	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
};

int lan865x_ptp_init(struct lan865x_priv *priv);
void lan865x_ptp_close(struct lan865x_priv *priv);
int lan865x_ptp_clock_set(struct lan865x_priv *priv,
				  u64 seconds, u32 nano_seconds,
				  u32 sub_nano_seconds);
#endif //LAN865X_PTP_H