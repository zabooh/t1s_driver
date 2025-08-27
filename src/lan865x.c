// SPDX-License-Identifier: GPL-2.0+
/*
 * Microchip's LAN865x 10BASE-T1S MAC-PHY driver
 *
 * Author: Parthiban Veerasooran <parthiban.veerasooran@microchip.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/phy.h>
#include "oa_tc6.h"
#include "lan865x_ptp.h"
#include "lan865x.h"


#define DRV_NAME			"lan865x"
#define DEBUG_REG_DUMP		true

/*struct lan865x_priv {
	struct work_struct multicast_work;
	struct net_device *netdev;
	struct spi_device *spi;
	struct oa_tc6 *tc6;
	struct lan865x_ptp ptp;
};*/

static int lan865x_set_hw_macaddr_low_bytes(struct oa_tc6 *tc6, const u8 *mac)
{
	u32 regval;

	regval = (mac[3] << 24) | (mac[2] << 16) | (mac[1] << 8) | mac[0];

	return oa_tc6_write_register(tc6, LAN865X_REG_MAC_L_SADDR1, regval);
}

static int lan865x_set_hw_macaddr(struct lan865x_priv *priv, const u8 *mac)
{
	int restore_ret;
	u32 regval;
	int ret;

	/* Configure MAC address low bytes */
	ret = lan865x_set_hw_macaddr_low_bytes(priv->tc6, mac);
	if (ret)
		return ret;

	/* Prepare and configure MAC address high bytes */
	regval = (mac[5] << 8) | mac[4];
	ret = oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_H_SADDR1, regval);
	if (!ret)
		return 0;

	/* Restore the old MAC address low bytes from netdev if the new MAC
	 * address high bytes setting failed.
	 */
	restore_ret = lan865x_set_hw_macaddr_low_bytes(priv->tc6,
						       priv->netdev->dev_addr);
	if (restore_ret)
		return restore_ret;

	return ret;
}

static void
lan865x_get_drvinfo(struct net_device *netdev, struct ethtool_drvinfo *info)
{
	strscpy(info->driver, DRV_NAME, sizeof(info->driver));
	strscpy(info->bus_info, dev_name(netdev->dev.parent),
		sizeof(info->bus_info));
}

static const struct ethtool_ops lan865x_ethtool_ops = {
	.get_drvinfo        = lan865x_get_drvinfo,
	.get_link_ksettings = phy_ethtool_get_link_ksettings,
	.set_link_ksettings = phy_ethtool_set_link_ksettings,
};

static int lan865x_set_mac_address(struct net_device *netdev, void *addr)
{
	struct lan865x_priv *priv = netdev_priv(netdev);
	struct sockaddr *address = addr;
	int ret;

	ret = eth_prepare_mac_addr_change(netdev, addr);
	if (ret < 0)
		return ret;

	if (ether_addr_equal(address->sa_data, netdev->dev_addr))
		return 0;

	ret = lan865x_set_hw_macaddr(priv, address->sa_data);
	if (ret)
		return ret;

	eth_hw_addr_set(netdev, address->sa_data);

	return 0;
}

static u32 lan865x_hash(u8 addr[ETH_ALEN])
{
	return (ether_crc(ETH_ALEN, addr) >> 26) & GENMASK(5, 0);
}

static void lan865x_set_specific_multicast_addr(struct net_device *netdev)
{
	struct lan865x_priv *priv = netdev_priv(netdev);
	struct netdev_hw_addr *ha;
	u32 hash_lo = 0;
	u32 hash_hi = 0;

	netdev_for_each_mc_addr(ha, netdev) {
		u32 bit_num = lan865x_hash(ha->addr);
		u32 mask = BIT(bit_num);

		/* 5th bit of the 6 bits hash value is used to determine which
		 * bit to set in either a high or low hash register.
		 */
		if (bit_num & BIT(5))
			hash_hi |= mask;
		else
			hash_lo |= mask;
	}

	/* Enabling specific multicast addresses */
	if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_H_HASH, hash_hi)) {
		netdev_err(netdev, "Failed to write reg_hashh");
		return;
	}

	if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_L_HASH, hash_lo))
		netdev_err(netdev, "Failed to write reg_hashl");
}

static void lan865x_multicast_work_handler(struct work_struct *work)
{
	struct lan865x_priv *priv = container_of(work, struct lan865x_priv,
						 multicast_work);
	u32 regval = 0;

	if (priv->netdev->flags & IFF_PROMISC) {
		/* Enabling promiscuous mode */
		regval |= MAC_NET_CFG_PROMISCUOUS_MODE;
		regval &= (~MAC_NET_CFG_MULTICAST_MODE);
		regval &= (~MAC_NET_CFG_UNICAST_MODE);
	} else if (priv->netdev->flags & IFF_ALLMULTI) {
		/* Enabling all multicast mode */
		regval &= (~MAC_NET_CFG_PROMISCUOUS_MODE);
		regval |= MAC_NET_CFG_MULTICAST_MODE;
		regval &= (~MAC_NET_CFG_UNICAST_MODE);
	} else if (!netdev_mc_empty(priv->netdev)) {
		lan865x_set_specific_multicast_addr(priv->netdev);
		regval &= (~MAC_NET_CFG_PROMISCUOUS_MODE);
		regval &= (~MAC_NET_CFG_MULTICAST_MODE);
		regval |= MAC_NET_CFG_UNICAST_MODE;
	} else {
		/* enabling local mac address only */
		if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_H_HASH, 0)) {
			netdev_err(priv->netdev, "Failed to write reg_hashh");
			return;
		}
		if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_L_HASH, 0)) {
			netdev_err(priv->netdev, "Failed to write reg_hashl");
			return;
		}
	}
	if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_NET_CFG, regval))
		netdev_err(priv->netdev,
			   "Failed to enable promiscuous/multicast/normal mode");
}

static void lan865x_set_multicast_list(struct net_device *netdev)
{
	struct lan865x_priv *priv = netdev_priv(netdev);

	schedule_work(&priv->multicast_work);
}

static netdev_tx_t lan865x_send_packet(struct sk_buff *skb,
				       struct net_device *netdev)
{
	struct lan865x_priv *priv = netdev_priv(netdev);

	return oa_tc6_start_xmit(priv->tc6, skb);
}

static int lan865x_hw_disable(struct lan865x_priv *priv)
{
	u32 regval;

	if (oa_tc6_read_register(priv->tc6, LAN865X_REG_MAC_NET_CTL, &regval))
		return -ENODEV;

	regval &= ~(MAC_NET_CTL_TXEN | MAC_NET_CTL_RXEN);

	if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_NET_CTL, regval))
		return -ENODEV;

	return 0;
}

static int lan865x_net_close(struct net_device *netdev)
{
	struct lan865x_priv *priv = netdev_priv(netdev);
	int ret;

	netif_stop_queue(netdev);
	phy_stop(netdev->phydev);
	ret = lan865x_hw_disable(priv);
	if (ret) {
		netdev_err(netdev, "Failed to disable the hardware: %d\n", ret);
		return ret;
	}

	return 0;
}

static int lan865x_hw_enable(struct lan865x_priv *priv)
{
	u32 regval;

	if (oa_tc6_read_register(priv->tc6, LAN865X_REG_MAC_NET_CTL, &regval))
		return -ENODEV;

	regval |= MAC_NET_CTL_TXEN | MAC_NET_CTL_RXEN;

	if (oa_tc6_write_register(priv->tc6, LAN865X_REG_MAC_NET_CTL, regval))
		return -ENODEV;

	return 0;
}

static int lan865x_net_open(struct net_device *netdev)
{
	struct lan865x_priv *priv = netdev_priv(netdev);
	int ret;

	ret = lan865x_hw_enable(priv);
	if (ret) {
		netdev_err(netdev, "Failed to enable hardware: %d\n", ret);
		return ret;
	}

	phy_start(netdev->phydev);

	return 0;
}

static const struct net_device_ops lan865x_netdev_ops = {
	.ndo_open		= lan865x_net_open,
	.ndo_stop		= lan865x_net_close,
	.ndo_start_xmit		= lan865x_send_packet,
	.ndo_set_rx_mode	= lan865x_set_multicast_list,
	.ndo_set_mac_address	= lan865x_set_mac_address,
};

static int lan865x_configure_fixup(struct lan865x_priv *priv)
{
	return oa_tc6_write_register(priv->tc6, LAN865X_FIXUP_REG,
				     LAN865X_FIXUP_VALUE);
}

static int lan865x_set_zarfe(struct lan865x_priv *priv)
{
	u32 regval;
	int ret;

	ret = oa_tc6_read_register(priv->tc6, OA_TC6_REG_CONFIG0, &regval);
	if (ret)
		return ret;

	/* Set Zero-Align Receive Frame Enable */
	regval |= CONFIG0_ZARFE_ENABLE;

	return oa_tc6_write_register(priv->tc6, OA_TC6_REG_CONFIG0, regval);
}

static int lan865x_init_wallclock(struct lan865x_priv *priv){
	
	struct oa_tc6 *tc6 = priv->tc6;
	struct timespec64 ts;
	int ret = 0;
	struct lan865x_ptp *ptp = &priv->ptp;
	/* Setting Wallclock to current kernel time */
	/* Referenced from LAN743x_ptp & LAN9662_ptp */
	ktime_get_clocktai_ts64(&ts);
	netdev_info(priv->netdev, "Current Kernel Time: Timestamp_sec %llx; Timestamp_nsec %lx ", ts.tv_sec, ts.tv_nsec);
	/* Update MAC-PHY Wallclock */
	ret = lan865x_ptp_clock_set(priv, ts.tv_sec, ts.tv_nsec, 0);
	return ret;
}

static int lan865x_setup_rx_phy_timestamping(struct oa_tc6 *tc6){
	int ret;
	
	/* Set the Receive match Pattern to 0x88F710 by */
	/* Writing 0x0088 to RXMPATH */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | RXMPATH, RXMPATH_PTP);
	if (ret)
		return ret;
	/* Writing 0xF710 to RXMPATL */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | RXMPATL, RXMPATL_PTP);
	if (ret)
		return ret;
	/* Set the Receive match mask to 0x000000 by */
	/* Writing 0x0000 to RXMMSKH */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | RXMMSKH, RXMASKH);
	if (ret)
		return ret;
	/* Writing 0x0000 to RXMMSKL */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | RXMMSKL, RXMASKL);
	if (ret)
		return ret;
	/* Writing 0x0 to RXMLOC */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | RXMLOC, RXMLOC_S_O_F);
	if (ret)
		return ret;
	/* Writing 0x2 to RXMCTL */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | RXMCTL, RXME);
	if (ret)
		return ret;
	return 0;
}

static int lan865x_setup_tx_phy_timestamping(struct oa_tc6 *tc6){
	int ret;
	
	/* Set the Transmit match Pattern to 0x88F710 by */
	/* Writing 0x0088 to TXMPATH */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | TXMPATH, TXMPATL_PTP);
	if (ret)
		return ret;
	/* Writing 0xF710 to TXMPATL */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | TXMPATL, TXMPATL_PTP);
	if (ret)
		return ret;
	/* Set the Transmit match mask to 0xFFFFFF by*/
	/* Writing 0x0000 to TXMMSKH */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | TXMMSKH, TXMASKH_ALL);
	if (ret)
		return ret;
	/* Writing 0x0000 to TXMMSKL */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | TXMMSKL, TXMASKL_ALL);
	if (ret)
		return ret;
	/* Writing 0x0 to TXMLOC */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | TXMLOC, TXMLOC_S_O_F);
	if (ret)
		return ret;
	/* Writing 0x2 to TXMCTL */
	ret = oa_tc6_write_register(tc6, (OA_TC6_CTRL_HEADER_MMS_PHY << 16) | TXMCTL, TXME);
	if (ret)
		return ret;
	
	return 0;
}

static int lan865x_enable_timestamping(struct lan865x_priv *priv) /* DT added for Timestamping PTP in lan865x */
{
	struct oa_tc6 *tc6 = priv->tc6;
	u32 value;
	int ret;

	ret = oa_tc6_read_register(tc6, OA_TC6_REG_STDCAP, &value);
	if (ret)
		return ret;
	if (value & STDCAP_FTSC) {
		netdev_info(priv->netdev, "TimeStamp Capable MAC-PHY - proceeding enabling Timestamping");
		ret = lan865x_init_wallclock(priv);
		if (ret)
			return ret;

#ifdef DEBUG_REG_DUMP		
		ret = oa_tc6_read_register(tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TI, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: MAC_TI %x", value);
		ret = oa_tc6_read_register(tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TN, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: MAC_TN %x", value);	
		ret = oa_tc6_read_register(tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TSL, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: MAC_TSL %x", value);
		ret = oa_tc6_read_register(tc6, (OA_TC6_CTRL_HEADER_MMS_MAC << 16) | MAC_TSH, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: MAC_TSH %x", value);			
		
		ret = oa_tc6_read_register(tc6, (MMS10 << 16) | CFGPRTCTL, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: CFGPRTCTL %x", value);
#endif
		/* Following the Baremetal documentation one needs to */
		/* Probably will need to add a TX TSE Bit as well, so that the MAC is generating a timestamp at the next plca cycle*/
		ret = lan865x_setup_tx_phy_timestamping(tc6);
		if (ret)
			return ret;
		ret = lan865x_setup_rx_phy_timestamping(tc6);
		if (ret)
			return ret;
		
#ifdef DEBUG_REG_DUMP
		ret = oa_tc6_read_register(tc6, (4 << 16) | RXMMSKH, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: RXMMSKH %x", value);
		ret = oa_tc6_read_register(tc6, (4 << 16) | RXMMSKL, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: RXMMSKL %x", value);	
		ret = oa_tc6_read_register(tc6, (4 << 16) | RXMLOC, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: RXMLOC %x", value);
		ret = oa_tc6_read_register(tc6, (4 << 16) | RXMCTL, &value);
		if (ret)
			return ret;
		netdev_info(priv->netdev, "Reading back: RXMCTL %x", value);
#endif	

		ret = oa_tc6_enable_timestamping(tc6);
		if (ret)
			return ret;
		
		return 0;
	}
	return -EINVAL;
}

static int lan865x_probe(struct spi_device *spi)
{
	struct net_device *netdev;
	struct lan865x_priv *priv;
	int ret;

	netdev = alloc_etherdev(sizeof(struct lan865x_priv));
	if (!netdev)
		return -ENOMEM;

	priv = netdev_priv(netdev);
	priv->netdev = netdev;
	priv->spi = spi;
	spi_set_drvdata(spi, priv);
	INIT_WORK(&priv->multicast_work, lan865x_multicast_work_handler);

	priv->tc6 = oa_tc6_init(spi, netdev);
	if (!priv->tc6) {
		ret = -ENODEV;
		goto free_netdev;
	}

	ret = lan865x_configure_fixup(priv);
	if (ret) {
		dev_err(&spi->dev, "Failed to configure fixup: %d\n", ret);
		goto oa_tc6_exit;
	}
	
	/* As per the point s3 in the below errata, SPI receive Ethernet frame
	 * transfer may halt when starting the next frame in the same data block
	 * (chunk) as the end of a previous frame. The RFA field should be
	 * configured to 01b or 10b for proper operation. In these modes, only
	 * one receive Ethernet frame will be placed in a single data block.
	 * When the RFA field is written to 01b, received frames will be forced
	 * to only start in the first word of the data block payload (SWO=0). As
	 * recommended, ZARFE bit in the OPEN Alliance CONFIG0 register is set
	 * to 1 for proper operation.
	 *
	 * https://ww1.microchip.com/downloads/aemDocuments/documents/AIS/ProductDocuments/Errata/LAN8650-1-Errata-80001075.pdf
	 */
	ret = lan865x_set_zarfe(priv);
	if (ret) {
		dev_err(&spi->dev, "Failed to set ZARFE: %d\n", ret);
		goto oa_tc6_exit;
	}

	ret = lan865x_ptp_init(priv);
	if (ret) {
		netdev_err(netdev, "Failed to enable PTP: %d\n",
			ret);
		goto oa_tc6_exit;
	}

	ret = lan865x_enable_timestamping(priv);
	if (ret) {
		netdev_err(netdev, "Failed to enable timestamping: %d\n",
			ret);
		goto oa_tc6_exit;
	}

	/* Get the MAC address from the SPI device tree node */
	if (device_get_ethdev_address(&spi->dev, netdev))
		eth_hw_addr_random(netdev);

	ret = lan865x_set_hw_macaddr(priv, netdev->dev_addr);
	if (ret) {
		dev_err(&spi->dev, "Failed to configure MAC: %d\n", ret);
		goto oa_tc6_exit;
	}

	netdev->if_port = IF_PORT_10BASET;
	netdev->irq = spi->irq;
	netdev->netdev_ops = &lan865x_netdev_ops;
	netdev->ethtool_ops = &lan865x_ethtool_ops;

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(&spi->dev, "Register netdev failed (ret = %d)", ret);
		goto oa_tc6_exit;
	}

	return 0;

oa_tc6_exit:
	lan865x_ptp_close(priv);
	oa_tc6_exit(priv->tc6);
free_netdev:
	free_netdev(priv->netdev);
	return ret;
}

static void lan865x_remove(struct spi_device *spi)
{
	struct lan865x_priv *priv = spi_get_drvdata(spi);

	cancel_work_sync(&priv->multicast_work);
	lan865x_ptp_close(priv);
	unregister_netdev(priv->netdev);
	oa_tc6_exit(priv->tc6);
	free_netdev(priv->netdev);
}

static const struct spi_device_id spidev_spi_ids[] = {                           
        { .name = "lan8650" },
        { .name = "lan8651" },
        {},                                                                      
};

static const struct of_device_id lan865x_dt_ids[] = {
	{ .compatible = "microchip,lan8650" },
	{ /* Sentinel */ }
};
MODULE_DEVICE_TABLE(of, lan865x_dt_ids);

static struct spi_driver lan865x_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = lan865x_dt_ids,
	 },
	.probe = lan865x_probe,
	.remove = lan865x_remove,
	.id_table = spidev_spi_ids,
};
module_spi_driver(lan865x_driver);

MODULE_DESCRIPTION(DRV_NAME " 10Base-T1S MACPHY Ethernet Driver");
MODULE_AUTHOR("Parthiban Veerasooran <parthiban.veerasooran@microchip.com>");
MODULE_LICENSE("GPL");
