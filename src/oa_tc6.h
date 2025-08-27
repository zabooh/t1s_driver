/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * OPEN Alliance 10BASE‑T1x MAC‑PHY Serial Interface framework
 *
 * Link: https://opensig.org/download/document/OPEN_Alliance_10BASET1x_MAC-PHY_Serial_Interface_V1.1.pdf
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */
#ifndef _OA_TC6_H
#define _OA_TC6_H
#include <linux/etherdevice.h>
#include <linux/spi/spi.h>

/* Internal structure for MAC-PHY drivers */
struct oa_tc6 {
	struct device *dev;
	struct net_device *netdev;
	struct phy_device *phydev;
	struct mii_bus *mdiobus;
	struct spi_device *spi;
	struct mutex spi_ctrl_lock; /* Protects spi control transfer */
	void *spi_ctrl_tx_buf;
	void *spi_ctrl_rx_buf;
	void *spi_data_tx_buf;
	void *spi_data_rx_buf;
	struct sk_buff_head tx_skb_q;
	struct sk_buff *tx_skb;
	struct sk_buff *rx_skb;
	struct task_struct *spi_thread;
	wait_queue_head_t spi_wq;
	u16 tx_skb_offset;
	u16 spi_data_tx_buf_offset;
	u16 tx_credits;
	u8 rx_chunks_available;
	bool rx_buf_overflow;
	bool int_flag;
	bool ftse;
	bool rtsa;
	bool rtsp;
	bool incomplete_timestamp;
	u64  timestamp;
};

struct oa_tc6 *oa_tc6_init(struct spi_device *spi, struct net_device *netdev);
void oa_tc6_exit(struct oa_tc6 *tc6);
int oa_tc6_enable_timestamping(struct oa_tc6 *tc6);
int oa_tc6_disable_timestamping(struct oa_tc6 *tc6);
int oa_tc6_write_register(struct oa_tc6 *tc6, u32 address, u32 value);
int oa_tc6_write_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			   u8 length);
int oa_tc6_read_register(struct oa_tc6 *tc6, u32 address, u32 *value);
int oa_tc6_read_registers(struct oa_tc6 *tc6, u32 address, u32 value[],
			  u8 length);
netdev_tx_t oa_tc6_start_xmit(struct oa_tc6 *tc6, struct sk_buff *skb);
#endif //_OA_TC6_H