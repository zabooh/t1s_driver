// SPDX-License-Identifier: GPL-2.0+
/*
 * Microchip's LAN865x 10BASE-T1S MAC-PHY driver
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */

#include <linux/netdevice.h>

#include <linux/ptp_clock_kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/net_tstamp.h>
#include "oa_tc6.h"
#include "lan865x_ptp.h"
#include "lan865x.h"
#define LAN865X_PTP_MAX_FREQ_ADJ_IN_PPB		(31249999) // taken from LAN743x - to be reviewed
#define LAN865X_PTP_MAX_FINE_ADJ_IN_SCALED_PPM	(2047999934) // taken from LAN743x - to be reviewed
#define PTP_CLOCK_RATE_ADJ_RED 1//negative adjustment

static bool lan865x_ptp_is_enabled(struct lan865x_priv *priv);
static void lan865x_ptp_enable(struct lan865x_priv *priv);
static void lan865x_ptp_disable(struct lan865x_priv *priv);
static void lan865x_ptp_reset(struct lan865x_priv *priv);
static int lan865x_ptpci_adjfine(struct ptp_clock_info *ptpci, long scaled_ppm)
{
	int ret;
	struct lan865x_ptp *ptp = 
		container_of(ptpci, struct lan865x_ptp, ptp_clock_info);
	struct lan865x_priv *priv =
		container_of(ptp, struct lan865x_priv, ptp);
	u32 lan865x_rate_adj_ns = 0;
	u32 lan865x_rate_adj_subns = 0;
	u64 u64_delta;
	
	if ((scaled_ppm < (-LAN865X_PTP_MAX_FINE_ADJ_IN_SCALED_PPM)) ||
	    scaled_ppm > LAN865X_PTP_MAX_FINE_ADJ_IN_SCALED_PPM) {
		return -EINVAL;
	}

	/* diff_by_scaled_ppm returns true if the difference is negative */
	if (diff_by_scaled_ppm(1ULL << 35, scaled_ppm, &u64_delta))
		lan865x_rate_adj_ns = (u32)u64_delta;
	else
		lan865x_rate_adj_ns = (u32)u64_delta | PTP_CLOCK_RATE_ADJ_RED;
	
	/* Convert into Nano Seconds Increment and Sub Nano Seconds Increment per Clock Cycle */
	
//	lan865x_rate_adj_H2B = lan865x_rate_adj & 0xFFFF00;
//	lan865x_rate_adj_LB  = lan865x_rate_adj & 0x0000FF;

	ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TI, lan865x_rate_adj_ns);
	ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TISUBN, lan865x_rate_adj_subns);
	return ret;
}

int lan865x_ptp_clock_set(struct lan865x_priv *priv,
				  u64 seconds, u32 nano_seconds,
				  u32 sub_nano_seconds)
{
	struct lan865x_ptp *ptp = &priv->ptp;
	int ret = 0;

	mutex_lock(&ptp->command_lock);
	ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TSH, upper_32_bits(seconds));
	if (ret)
		goto wc_out_unlock_mutex;
	ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TSL, lower_32_bits(seconds));
	if (ret)
		goto wc_out_unlock_mutex;
	ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TN, nano_seconds);
	if (ret)
		goto wc_out_unlock_mutex;
wc_out_unlock_mutex:
	mutex_unlock(&ptp->command_lock);
	return ret;
}

static int lan865x_ptp_clock_get(struct lan865x_priv *priv,
				  u64 *seconds, u32 *nano_seconds, 
				  u32 *sub_nano_seconds)
{
	u32 value;
	int ret = 0;
	struct lan865x_ptp *ptp = &priv->ptp;
	
	mutex_lock(&ptp->command_lock);
	if (seconds) {
		ret = oa_tc6_read_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TSH, &value);
		if (ret)
			return ret;
		(*seconds) = value;
		ret = oa_tc6_read_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TSL, &value);
		if (ret)
			return ret;
		(*seconds) = ((*seconds) << 32) | value;
	} if (nano_seconds) {
		ret = oa_tc6_read_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TN, &value);
		if (ret)
			return ret;
		(*nano_seconds) = value;
	}
	mutex_unlock(&ptp->command_lock);
	return ret;
}

static int lan865x_ptp_clock_step(struct lan865x_priv *priv,
				   s64 time_step_ns)
{
	int ret;
	struct lan865x_ptp *ptp = &priv->ptp;
	u32 nano_seconds_step = 0;
	u64 abs_time_step_ns = 0;
	u64 unsigned_seconds = 0;
	u32 nano_seconds = 0;
	u32 remainder = 0;
	s64 seconds = 0;

	if (time_step_ns >  15000000000LL) { // need to understand about this value why 15s?
		/* convert to clock set */
		ret = lan865x_ptp_clock_get(priv, &unsigned_seconds,
					      &nano_seconds, NULL);
		if(ret)
			return ret;
		unsigned_seconds += div_u64_rem(time_step_ns, 1000000000LL,
						&remainder);
		nano_seconds += remainder;
		if (nano_seconds >= 1000000000) {
			unsigned_seconds++;
			nano_seconds -= 1000000000;
		}
		ret = lan865x_ptp_clock_set(priv, unsigned_seconds,
				      nano_seconds, 0);
		return ret;
	} else if (time_step_ns < -15000000000LL) {
		/* convert to clock set */
		time_step_ns = -time_step_ns;

		ret = lan865x_ptp_clock_get(priv, &unsigned_seconds,
					      &nano_seconds, NULL);
		if(ret)
			return ret;
		unsigned_seconds -= div_u64_rem(time_step_ns, 1000000000LL,
						&remainder);
		nano_seconds_step = remainder;
		if (nano_seconds < nano_seconds_step) {
			unsigned_seconds--;
			nano_seconds += 1000000000;
		}
		nano_seconds -= nano_seconds_step;
		ret = lan865x_ptp_clock_set(priv, unsigned_seconds,
				      nano_seconds, 0);
		return ret;
	}

	/* do clock step */
	if (time_step_ns >= 0) {
		abs_time_step_ns = (u64)(time_step_ns);
		seconds = (s32)div_u64_rem(abs_time_step_ns, 1000000000,
					   &remainder);
		nano_seconds = (u32)remainder;
	} else {
		abs_time_step_ns = (u64)(-time_step_ns);
		seconds = -((s32)div_u64_rem(abs_time_step_ns, 1000000000,
					     &remainder));
		nano_seconds = (u32)remainder;
		if (nano_seconds > 0) {
			/* subtracting nano seconds is not allowed
			 * convert to subtracting from seconds,
			 * and adding to nanoseconds
			 */
			seconds--;
			nano_seconds = (1000000000 - nano_seconds);
		}
	}

	if (nano_seconds > 0) {
		/* add 8 ns to cover the likely normal increment */
		nano_seconds += 8;
	}

	if (nano_seconds >= 1000000000) {
		/* carry into seconds */
		seconds++;
		nano_seconds -= 1000000000;
	}

	while (seconds) {
		mutex_lock(&ptp->command_lock);
		if (seconds > 0) {
			u32 adjustment_value = (u32)seconds;

			if (adjustment_value > 0xF)
				adjustment_value = 0xF;
			for (int i=0; i< adjustment_value; i++)
				ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TA, ((0x1 << 31) | (1000000000 & 0x3FFFFFFF)));
			seconds -= ((s32)adjustment_value);
		} else {
			u32 adjustment_value = (u32)(-seconds);

			if (adjustment_value > 0xF)
				adjustment_value = 0xF;
			for (int i=0; i< adjustment_value; i++)
				ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TA, ((0x0 << 31) | (1000000000 & 0x3FFFFFFF)));
			seconds += ((s32)adjustment_value);
		}
		mutex_unlock(&ptp->command_lock);
	}
	if (nano_seconds) {
		mutex_lock(&ptp->command_lock);
			ret = oa_tc6_write_register(priv->tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TA, ((0x0 << 31) | (nano_seconds & 0x3FFFFFFF)));
		mutex_unlock(&ptp->command_lock);
	}
	return ret;
}

static int lan865x_ptpci_adjtime(struct ptp_clock_info *ptpci, s64 delta)
{
	int ret;
    struct lan865x_ptp *ptp = container_of(ptpci, struct lan865x_ptp, ptp_clock_info);
    struct lan865x_priv *priv = container_of(ptp, struct lan865x_priv, ptp);
    ret = lan865x_ptp_clock_step(priv, delta);
    return ret;
}

static int lan865x_ptpci_settime64(struct ptp_clock_info *ptpci,
				   const struct timespec64 *ts)
{
	int ret = 0;
	struct lan865x_ptp *ptp =
		container_of(ptpci, struct lan865x_ptp, ptp_clock_info);
	struct lan865x_priv *priv =
		container_of(ptp, struct lan865x_priv, ptp);
	u32 nano_seconds = 0;
	u32 seconds = 0;

	if (ts) {
		if (ts->tv_sec > 0xFFFFFFFFLL ||
		    ts->tv_sec < 0) {
			netif_warn(priv, drv, priv->netdev,
				   "ts->tv_sec out of range, %lld\n",
				   ts->tv_sec);
			return -ERANGE;
		}
		if (ts->tv_nsec >= 1000000000L ||
		    ts->tv_nsec < 0) {
			netif_warn(priv, drv, priv->netdev,
				   "ts->tv_nsec out of range, %ld\n",
				   ts->tv_nsec);
			return -ERANGE;
		}
		seconds = ts->tv_sec;
		nano_seconds = ts->tv_nsec;
		ret = lan865x_ptp_clock_set(priv, seconds, nano_seconds, 0);
	} else {
		netif_warn(priv, drv, priv->netdev, "ts == NULL\n");
		return -EINVAL;
	}

	return ret;
}

static int lan865x_ptpci_gettime64(struct ptp_clock_info *ptpci,
				   struct timespec64 *ts)
{
	int ret = 0;
	struct lan865x_ptp *ptp =
		container_of(ptpci, struct lan865x_ptp, ptp_clock_info);
	struct lan865x_priv *priv =
		container_of(ptp, struct lan865x_priv, ptp);
	u32 nano_seconds = 0;
	u64 seconds = 0;

	lan865x_ptp_clock_get(priv, &seconds, &nano_seconds, NULL);
	ts->tv_sec = seconds;
	ts->tv_nsec = nano_seconds;

	return ret;
}

int lan865x_ptp_init(struct lan865x_priv *priv)
{
	struct lan865x_ptp *ptp = &priv->ptp;
	
	mutex_init(&ptp->command_lock);

    ptp->ptp_clock_info.owner = THIS_MODULE;
    snprintf(ptp->ptp_clock_info.name, 16, "%pm", priv->netdev->dev_addr);
    ptp->ptp_clock_info.max_adj = 536870912; // Example value, adjust as needed
    ptp->ptp_clock_info.n_alarm = 0;
    ptp->ptp_clock_info.n_ext_ts = 0; // Adjust if EXTTS supported
    ptp->ptp_clock_info.n_per_out = 0; // Adjust if PEROUT supported
    ptp->ptp_clock_info.n_pins = 0; // Adjust if pins supported
    ptp->ptp_clock_info.pps = 0; // Adjust if PPS supported
	ptp->ptp_clock_info.adjfine = lan865x_ptpci_adjfine;
    ptp->ptp_clock_info.adjtime = lan865x_ptpci_adjtime;
    ptp->ptp_clock_info.gettime64 = lan865x_ptpci_gettime64;
    ptp->ptp_clock_info.settime64 = lan865x_ptpci_settime64;
    // Add other callbacks as needed

    ptp->ptp_clock = ptp_clock_register(&ptp->ptp_clock_info, priv->tc6->dev);
    if (IS_ERR(ptp->ptp_clock)) {
        ptp->ptp_clock = NULL;
		netif_err(priv, ifup, priv->netdev,
			  "ptp_clock_register failed\n");
        return -ENODEV;
    }
    ptp->flags |= PTP_FLAG_PTP_CLOCK_REGISTERED;
	netif_info(priv, ifup, priv->netdev,
		   "successfully registered ptp clock\n");
    return 0;
}

static bool lan865x_ptp_is_enabled(struct lan865x_priv *priv)
{
	if (priv->tc6->ftse)
		return true;
	return false;
}

void lan865x_ptp_close(struct lan865x_priv *priv)
{
    struct lan865x_ptp *ptp = &priv->ptp;
    if (ptp->ptp_clock) {
        ptp_clock_unregister(ptp->ptp_clock);
        ptp->ptp_clock = NULL;
        ptp->flags &= ~1;
    }
    lan865x_ptp_disable(priv);
}

static void lan865x_ptp_disable(struct lan865x_priv *priv)
{
	struct lan865x_ptp *ptp = &priv->ptp;

	mutex_lock(&ptp->command_lock);
	if (!lan865x_ptp_is_enabled(priv)) {
		netif_warn(priv, drv, priv->netdev,
			   "PTP already disabled\n");
		goto done;
	}
	oa_tc6_disable_timestamping(priv->tc6);
done:
	mutex_unlock(&ptp->command_lock);
}