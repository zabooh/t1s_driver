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

#define DRV_NAME			"lan865x"

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
#define CONFIG0_ZARFE_ENABLE		BIT(12)

struct lan865x_priv {
	struct work_struct multicast_work;
	struct net_device *netdev;
	struct spi_device *spi;
	struct oa_tc6 *tc6;
};

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

	// Falls Hardware-Timestamping für TX angefordert ist
	if (skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP) {
		// Markiere das Paket für Hardware-Timestamping
		skb_shinfo(skb)->tx_flags |= SKBTX_IN_PROGRESS;
	}

	print_hex_dump(KERN_INFO, "TX skb: ", DUMP_PREFIX_OFFSET,
                   16, 1,
                   skb->data, skb->len, true);

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

static int lan865x_probe(struct spi_device *spi)
{
	struct net_device *netdev;
	struct lan865x_priv *priv;
	int ret;

	netdev = alloc_etherdev(sizeof(struct lan865x_priv));
	if (!netdev)
		return -ENOMEM;

	netdev_info(netdev, "lan865x_probe() function started....");
	
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

	// PTP-Unterstützung initialisieren
	ret = lan865x_ptp_init((struct lan865x_adapter *)priv);
    if (ret) {
        dev_err(&spi->dev, "Failed to initialize PTP: %d\n", ret);
        goto oa_tc6_exit;
    }

	ret = register_netdev(netdev);
	if (ret) {
		dev_err(&spi->dev, "Register netdev failed (ret = %d)", ret);
		goto oa_tc6_exit;
	}

	netdev_info(netdev, "lan865x_probe() function successful");
	
	return 0;


ptp_remove:
    lan865x_ptp_remove((struct lan865x_adapter *)priv);
oa_tc6_exit:
	oa_tc6_exit(priv->tc6);
free_netdev:
	free_netdev(priv->netdev);
	return ret;
}

static void lan865x_remove(struct spi_device *spi)
{
	struct lan865x_priv *priv = spi_get_drvdata(spi);

	cancel_work_sync(&priv->multicast_work);
	unregister_netdev(priv->netdev);

    // PTP-Unterstützung entfernen
    lan865x_ptp_remove((struct lan865x_adapter *)priv);

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
