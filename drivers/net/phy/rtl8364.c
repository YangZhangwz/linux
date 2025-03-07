// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Renesas PHY uPD60620.
 *
 * Copyright (C) 2015 Softing Industrial Automation GmbH
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/phy.h>

#define RTL8364_PHY_ID    0x12345678

/* Init PHY */

static int rtl8364_config_init(struct phy_device *phydev)
{
	return 0;
}

/* Get PHY status from common registers */

static int rtl8364_read_status(struct phy_device *phydev)
{
	phydev->link = 1;
	phydev->speed = SPEED_1000;
	phydev->duplex = DUPLEX_FULL;
	return 0;
}

MODULE_DESCRIPTION("Realtek 8364S PHY driver");
MODULE_AUTHOR("ZhuBin <zhu.bin@embedway.com>");
MODULE_LICENSE("GPL");

static struct phy_driver rtl8364_driver[1] = { {
	.phy_id         = RTL8364_PHY_ID,
	.phy_id_mask    = 0xfffffffe,
	.name           = "Realtek 8364S",
	/* PHY_BASIC_FEATURES */
	.flags          = 0,
	.config_init    = rtl8364_config_init,
	.read_status    = rtl8364_read_status,
} };

module_phy_driver(rtl8364_driver);

static struct mdio_device_id __maybe_unused rtl8364_tbl[] = {
	{ RTL8364_PHY_ID, 0xfffffffe },
	{ }
};

MODULE_DEVICE_TABLE(mdio, rtl8364_tbl);
