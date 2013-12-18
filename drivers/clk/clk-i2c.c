#include <linux/clk-provider.h>

u8 clk_i2c_readb(struct regmap *regmap, unsigned int reg)
{
	unsigned int val;
	int err = regmap_read(regmap, reg, &val);

	if (err) {
		pr_err("%s: read from device failed\n", __func__);
		return 0;
	}

	return val;
}

void clk_i2c_writeb(u8 val, struct regmap *regmap,
		unsigned int reg)
{
	int err = regmap_write(regmap, reg, val);
	if (err)
		pr_err("%s: write to device failed\n", __func__);
}
