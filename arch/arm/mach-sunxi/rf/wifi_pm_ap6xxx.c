/*
 * ap6xxx sdio wifi power management API
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/sys_config.h>
#include <mach/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/pinctrl/pinconf-sunxi.h>
#include <linux/pinctrl/consumer.h>
#include <linux/clk.h>
#include <linux/clk/sunxi.h>
#include <linux/err.h>
#include <linux/arisc/arisc.h>
#include "wifi_pm.h"

#define ap6xxx_msg(...)    do {printk("[ap6xxx]: "__VA_ARGS__);} while(0)

static int ap6xxx_wl_regon;
static int ap6xxx_bt_regon;
static int wifi_power_switch;
static char *axp_name[4] = {NULL};

static void ap6xxx_config_32k_clk(void);

static int sunxi_ap6xxx_gpio_req(struct gpio_config *gpio)
{
	int            ret = 0;
	char           pin_name[8] = {0};
	unsigned long  config;

	sunxi_gpio_to_name(gpio->gpio, pin_name);
	config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_FUNC, gpio->mul_sel);
	ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
	if (ret) {
        ap6xxx_msg("set gpio %s mulsel failed.\n",pin_name);
        return -1;
    }

	if (gpio->pull != GPIO_PULL_DEFAULT){
        config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_PUD, gpio->pull);
        ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
        if (ret) {
                ap6xxx_msg("set gpio %s pull mode failed.\n",pin_name);
                return -1;
        }
	}

	if (gpio->drv_level != GPIO_DRVLVL_DEFAULT){
        config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_DRV, gpio->drv_level);
        ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
        if (ret) {
            ap6xxx_msg("set gpio %s driver level failed.\n",pin_name);
            return -1;
        }
    }

	if (gpio->data != GPIO_DATA_DEFAULT) {
        config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_DAT, gpio->data);
        ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
        if (ret) {
            ap6xxx_msg("set gpio %s initial val failed.\n",pin_name);
            return -1;
        }
    }

  return 0;
}

// power control by axp
static int ap6xxx_module_power(int onoff)
{
	struct regulator* wifi_ldo[4] = {NULL};
	static int first = 1;
	int i = 0, ret = 0;

	ap6xxx_msg("ap6xxx module power set by axp.\n");

    for (i = 0; axp_name[i] != NULL; i++){
        wifi_ldo[i] = regulator_get(NULL, axp_name[i]);
        if (IS_ERR(wifi_ldo[i])) {
            ap6xxx_msg("get power regulator %s failed.\n", axp_name[i]);
            break;
        }
    }

    wifi_ldo[i] = NULL;

    if (first) {
        ap6xxx_msg("first time\n");

        for (i = 0; wifi_ldo[i] != NULL; i++){
            ret = regulator_force_disable(wifi_ldo[i]);
            if (ret < 0) {
                ap6xxx_msg("regulator_force_disable  %s fail, return %d.\n", axp_name[i], ret);
                goto exit;
            }
        }
        first = 0;
        if (!onoff)
			    goto exit;
    }

    if (onoff) {
		ap6xxx_msg("regulator on.\n");

        for(i = 0; wifi_ldo[i] != NULL; i++){
            ret = regulator_set_voltage(wifi_ldo[i], 3300000, 3300000);

            if (ret < 0) {
                    ap6xxx_msg("regulator_set_voltage %s fail, return %d.\n", axp_name[i], ret);
                    goto exit;
            }

            ret = regulator_enable(wifi_ldo[i]);
            if (ret < 0) {
                    ap6xxx_msg("regulator_enable %s fail, return %d.\n", axp_name[i], ret);
                    goto exit;
            }
        }
	} else {
		ap6xxx_msg("regulator off.\n");
        for(i = 0; wifi_ldo[i] != NULL; i++){
            ret = regulator_disable(wifi_ldo[i]);
            if (ret < 0) {
                ap6xxx_msg("regulator_disable %s fail, return %d.\n", axp_name[i], ret);
                goto exit;
            }
        }
	}

exit:
	for(i = 0; wifi_ldo[i] != NULL; i++){
        regulator_put(wifi_ldo[i]);
	}

  if (wifi_power_switch != -1) {
    if (onoff) {
      __gpio_set_value(wifi_power_switch, 1);
    } else {
      __gpio_set_value(wifi_power_switch, 0);
    }
  }
  
	return ret;
}

static int ap6xxx_gpio_ctrl(char* name, int level)
{
	int i = 0;
	int gpio = 0;
	char * gpio_name[2] = {"ap6xxx_wl_regon", "ap6xxx_bt_regon"};

	for (i = 0; i < 2; i++) {
		if (strcmp(name, gpio_name[i]) == 0) {
			switch (i)
			{
				case 0: /*ap6xxx_wl_regon*/
					gpio = ap6xxx_wl_regon;
					break;
				case 1: /*ap6xxx_bt_regon*/
					gpio = ap6xxx_bt_regon;
					break;
				default:
					ap6xxx_msg("no matched gpio.\n");
			}
			break;
		}
	}

	__gpio_set_value(gpio, level);
	printk("gpio %s set val %d, act val %d\n", name, level, __gpio_get_value(gpio));

	return 0;
}

void ap6xxx_power(int mode, int *updown)
{
	if (mode) {
		if (*updown) {
			ap6xxx_gpio_ctrl("ap6xxx_wl_regon", 1);
			mdelay(100);
        	} else {
        		ap6xxx_gpio_ctrl("ap6xxx_wl_regon", 0);
        		mdelay(100);
        	}
        	ap6xxx_msg("sdio wifi power state: %s\n", *updown ? "on" : "off");
	}
	return;
}

void ap6xxx_gpio_init(void)
{
	script_item_u val;
	script_item_value_type_e type;
	int lpo_use_apclk = 0;
	struct wifi_pm_ops *ops = &wifi_select_pm_ops;
	struct gpio_config  *gpio_p = NULL;

	type = script_get_item(wifi_para, "wifi_power", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		ap6xxx_msg("failed to fetch wifi_power\n");
	}

	axp_name[0] = val.str;
	ap6xxx_msg("module power name %s\n", axp_name[0]);

    type = script_get_item(wifi_para, "wifi_power_ext1", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		ap6xxx_msg("failed to fetch wifi_power_ext1\n");
	}
    axp_name[1] = val.str;
    ap6xxx_msg("module power ext1 name %s\n", axp_name[1]);

    type = script_get_item(wifi_para, "wifi_power_ext2", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		ap6xxx_msg("failed to fetch wifi_power_ext2\n");
	}
    axp_name[2] = val.str;
    ap6xxx_msg("module power ext2 name %s\n", axp_name[2]);

    axp_name[3] = NULL;

	type = script_get_item(wifi_para, "ap6xxx_wl_regon", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_PIO!=type)
		ap6xxx_msg("get ap6xxx ap6xxx_wl_regon gpio failed\n");
	else
		gpio_p = &val.gpio;
	ap6xxx_wl_regon = gpio_p->gpio;
	sunxi_ap6xxx_gpio_req(gpio_p);

	type = script_get_item(wifi_para, "ap6xxx_bt_regon", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_PIO!=type)
		ap6xxx_msg("get ap6xxx ap6xxx_bt_regon gpio failed\n");
	else
		gpio_p = &val.gpio;
	ap6xxx_bt_regon = gpio_p->gpio;
	sunxi_ap6xxx_gpio_req(gpio_p);

	type = script_get_item(wifi_para, "ap6xxx_lpo_use_apclk", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_INT!=type)
		ap6xxx_msg("get ap6xxx ap6xxx_lpo_use_apclk failed\n");
	else
		lpo_use_apclk = val.val;

  wifi_power_switch = -1;
  type = script_get_item(wifi_para, "wifi_power_switch", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_PIO!=type)
		ap6xxx_msg("get ap6xxx wifi_power_switch failed\n");
	else
		wifi_power_switch = val.val;

	ops->gpio_ctrl	= ap6xxx_gpio_ctrl;
	ops->power = ap6xxx_power;

	ap6xxx_module_power(1);

	if (lpo_use_apclk){
		ap6xxx_config_32k_clk();
	}

}

static void ap6xxx_config_32k_clk(void)
{
	struct clk *ap_32k = NULL;
	int ret = 0;

	ap_32k = clk_get(NULL, "losc_out");
	if (!ap_32k){
		ap6xxx_msg("Get ap 32k clk out failed!\n");
		return ;
	}

	ret = clk_prepare_enable(ap_32k);
	if (ret){
		ap6xxx_msg("losc out enable failed!\n");
	}
}
