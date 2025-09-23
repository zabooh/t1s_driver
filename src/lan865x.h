// SPDX-License-Identifier: GPL-2.0+
/*
 * Microchip's LAN865x 10BASE-T1S MAC-PHY driver
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */
#ifndef LAN865X_H
#define LAN865X_H
#include "lan865x_ptp.h"
#define OA_TC6_PHYID_MCHP 0x00800F
#define OA_TC6_PHYID_ORG_OFFSET GENMASK(31, 10)
#define OA_TC6_PHYID_LAN865X 0x011011
#define OA_TC6_PHYID_MODEL_OFFSET GENMASK(9, 4)

/* OPEN Alliance TC6 registers */
/* Standard Capabilities Register */
#define OA_TC6_REG_STDCAP			0x0002
#define STDCAP_DIRECT_PHY_REG_ACCESS		BIT(8)
#define STDCAP_FTSC                         BIT(6) /* Frame Time Stamp Capability */

/* Reset Control and Status Register */
#define OA_TC6_REG_RESET			0x0003
#define RESET_SWRESET				BIT(0)	/* Software Reset */

/* Configuration Register #0 */
#define OA_TC6_REG_CONFIG0			0x0004
#define CONFIG0_SYNC				BIT(15)
#define CONFIG0_FTSS				BIT(6) /* Frame Time Stamp Select - 32 bit or 64 bit */
#define CONFIG0_FTSE				BIT(7)  /* Frame Time Stamp Enable */

/* MAC Network Control Register */
#define LAN865X_REG_MAC_NET_CTL		0x00010000
#define MAC_NET_CTL_TXEN		BIT(3) /* Transmit Enable */
#define MAC_NET_CTL_RXEN		BIT(2) /* Receive Enable */

#define LAN865X_REG_MAC_NET_CFG		0x00010001 /* MAC Network Configuration Reg */
#define MAC_NET_CFG_PROMISCUOUS_MODE	BIT(4)
#define MAC_NET_CFG_MULTICAST_MODE	BIT(6)
#define MAC_NET_CFG_UNICAST_MODE	BIT(7)

#define LAN865X_REG_MAC_L_HASH		0x00010020 /* MAC Hash Register Bottom */
#define LAN865X_REG_MAC_H_HASH		0x00010021 /* MAC Hash Register Top */
#define LAN865X_REG_MAC_L_SADDR1	0x00010022 /* MAC Specific Addr 1 Bottom Reg */
#define LAN865X_REG_MAC_H_SADDR1	0x00010023 /* MAC Specific Addr 1 Top Reg */

/* LAN8650/1 configuration fixup from AN1760 */
#define LAN865X_FIXUP_REG		0x00010077
#define LAN865X_FIXUP_VALUE		0x0028

/* OPEN Alliance Configuration Register #0 */
#define OA_TC6_REG_CONFIG0		0x0004
#define CONFIG0_ZARFE_ENABLE	BIT(12)

#define OA_TC6_CTRL_HEADER_MMS_PHY		4 /* Need to change to MMS4 for PHY access*/
/* Receive Match Mask High Register */
#define RXMMSKH				0x0053
#define RXMASKH				0x0000 /* Do not mask, apply all Pattern Match bits */
/* Receive Match Mask Low Register */
#define RXMMSKL				0x0054
#define RXMASKL				0x0000 /* Do not mask, apply all Pattern Match bits */
/* Receive Match Pattern High Register */
#define RXMPATH				0x0051
#define RXMPATH_PTP			0x88
/* Receive Match Pattern Low Register */
#define RXMPATL				0x0052
#define RXMPATL_PTP			0xF710
/* Receive Match Location Register */
#define RXMLOC				0x0055
#define RXMLOC_S_O_F		0x0
/* Receive Match Control Register */
#define RXMCTL				0x0050
#define RXME				0x2

/* Transmit Match Mask High Register */
#define TXMMSKH				0x0043
#define TXMASKH				0x0000 /* Do not mask, apply all Pattern Match bits */
#define TXMASKH_ALL			0x00FF /* Mask All Pattern Match bits */
/* Transmit Match Mask Low Register */
#define TXMMSKL				0x0044
#define TXMASKL				0x0000 /* Do not mask, apply all Pattern Match bits */
#define TXMASKL_ALL			0xFFFF /* Mask All Pattern Match bits */
/* Transmit Match Pattern High Register */
#define TXMPATH				0x0041
#define TXMPATH_PTP			0x88
/* Transmit Match Pattern Low Register */
#define TXMPATL				0x0042
#define TXMPATL_PTP			0xF710
/* Transmit Matched Packet Delay Register */
#define TXMDLY				0x49
#define TXMDLYEN			BIT(15)
#define TXMPKTDLY			GENMASK(10,0)
/* Transmit Match Location Register */
#define TXMLOC				0x0045
#define TXMLOC_S_O_F		0x0000
#define TXMLOC_PTP			30
/* Transmit Match Control Register */
#define TXMCTL				0x0040
#define TXME				0x2

#define OA_TC6_CTRL_HEADER_MMS_MAC		1 /* Need to change to MMS1 for MAC access*/
/* MAC Timer Seconds High Register */
#define MAC_TSH				0x70
/* MAC Timer Seconds Low Register */
#define MAC_TSL				0x74
/* MAC Timer NanoSeconds Register */
#define MAC_TN				0x75 
/* MAC Timer Adjust Register */
#define MAC_TA				0x76 
#define MAC_TA_ADJ			BIT(31)
#define MAC_TA_ITDT			GENMASK(29,0)
/* MAC Timer Increment Register */
#define MAC_TI				0x77
#define MAC_TISUBN			0x6F

/* CFGPRTCTL Register to protect PHY related settings */
#define CFGPRTCTL 			0x0099
#define MMS10				0x0A

struct lan865x_priv {
	struct work_struct multicast_work;
	struct net_device *netdev;
	struct spi_device *spi;
	struct oa_tc6 *tc6;
	struct lan865x_ptp ptp;
	u32 msg_enable;
};
#endif //LAN865X_H