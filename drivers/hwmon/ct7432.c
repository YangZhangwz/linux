// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * An hwmon driver for the Microchip CT7432
 *
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/sysfs.h>

/* CT7432 registers */
#define LOCAL_TEMP_MSB 0x00
#define LOCAL_TEMP_LSB 0x29
#define REMOTE1_TEMP_MSB 0x01
#define REMOTE1_TEMP_LSB 0x10
#define REMOTE2_TEMP_MSB 0x23
#define REMOTE2_TEMP_LSB 0x24
#define REG_CONFIG1 0x03
#define REG_CONFIG2 0x09

struct ct7432_data {
	struct i2c_client *client;
	struct mutex lock; /* atomic read data updates */
	bool valid; /* validity of fields below */
	unsigned long next_update; /* In jiffies */
	s16 temp_input[3]; /* 0:local, 1:remote1, 2:remote2, 原始12位数据 */
};

enum {
	local,
	remote1,
	remote2,
};

static int ct7432_read_temp(struct i2c_client *client, int channel, s16 *raw)
{
	int msb, lsb;
	switch (channel) {
	case local:
		msb = i2c_smbus_read_byte_data(client, LOCAL_TEMP_MSB);
		lsb = i2c_smbus_read_byte_data(client, LOCAL_TEMP_LSB);
		break;
	case remote1:
		msb = i2c_smbus_read_byte_data(client, REMOTE1_TEMP_MSB);
		lsb = i2c_smbus_read_byte_data(client, REMOTE1_TEMP_LSB);
		break;
	case remote2:
		msb = i2c_smbus_read_byte_data(client, REMOTE2_TEMP_MSB);
		lsb = i2c_smbus_read_byte_data(client, REMOTE2_TEMP_LSB);
		break;
	default:
		return -EINVAL;
	}
	if (msb < 0 || lsb < 0)
		return -EIO;

	*raw = (msb << 8) | lsb;
	pr_debug("ct7432: channel=%d, msb=0x%02x, lsb=0x%02x, raw=0x%04x\n", channel, msb, lsb, *raw);

	return 0;
}

static int ct7432_update_device(struct device *dev)
{
	struct ct7432_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret = 0, i;
	s16 raw;

	ret = mutex_lock_interruptible(&data->lock);
	if (ret)
		return ret;

	if (time_after(jiffies, data->next_update) || !data->valid) {
		for (i = 0; i < 3; i++) {
			ret = ct7432_read_temp(client, i, &raw);
			if (ret)
				goto ret_unlock;
			data->temp_input[i] = raw;
		}
		data->next_update = jiffies + HZ / 4;
		data->valid = true;
	}

ret_unlock:
	mutex_unlock(&data->lock);
	return ret;
}

// 12位温度原始值转摄氏度，分辨率0.0625°C
static int ct7432_raw_to_millicelsius(s16 raw)
{
	int temp;
	raw >>= 4; // 只保留高12位
	// 判断范围位（bit 11）
	if (raw & 0x800) // 负温度（扩展范围）
		raw -= 0x1000;
	temp = raw * 625 / 10; // 0.0625°C = 62.5m°C
	return temp;
}

static ssize_t temp_input_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct ct7432_data *data = dev_get_drvdata(dev);
	int ret, index = to_sensor_dev_attr(attr)->index;
	s16 raw;

	ret = ct7432_update_device(dev);
	if (ret)
		return ret;

	raw = data->temp_input[index];
	return sprintf(buf, "%d\n", ct7432_raw_to_millicelsius(raw));
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, temp_input, 0); // local
static SENSOR_DEVICE_ATTR_RO(temp2_input, temp_input, 1); // remote1
static SENSOR_DEVICE_ATTR_RO(temp3_input, temp_input, 2); // remote2

static struct attribute *ct7432_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp3_input.dev_attr.attr,
	NULL
};

ATTRIBUTE_GROUPS(ct7432);

static int ct7432_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ct7432_data *data;
	struct device *hwmon_dev;
	s32 conf;
	printk("yucca: ct7432 is discovery\n");

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -EOPNOTSUPP;

	data = devm_kzalloc(dev, sizeof(struct ct7432_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->lock);

	/* Make sure the chip is powered up. */
	conf = i2c_smbus_read_byte_data(client, REG_CONFIG1);

	if (conf & 0x3f) {
		dev_err(dev, "invalid initial state(expect 0x00): 0x%x\n", conf);
		return -ENODEV;
	}

	/* Clear STANDBY bit */
	if (conf & BIT(6)) {
		s32 ret;
		conf &= ~BIT(6);
		ret = i2c_smbus_write_byte_data(client, REG_CONFIG1, conf);
		if (ret)
			dev_warn(dev, "unable to disable STANDBY\n");
	}

	hwmon_dev = devm_hwmon_device_register_with_groups(dev, client->name,
							   data, ct7432_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id ct7432_id[] = { { "ct7432", 0 }, {} };
MODULE_DEVICE_TABLE(i2c, ct7432_id);

static struct i2c_driver ct7432_driver = {
	.driver = {
		.name	= "ct7432",
	},
	.probe = ct7432_probe,
	.id_table = ct7432_id,
};

module_i2c_driver(ct7432_driver);

MODULE_AUTHOR("yang.zhang@yuccacloud.com");

MODULE_DESCRIPTION("CT7432 driver");
MODULE_LICENSE("GPL");
