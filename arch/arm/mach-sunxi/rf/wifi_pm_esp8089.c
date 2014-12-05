/*
 * eagle sdio wifi power management API
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
#include "wifi_pm.h"

#define ESP_GPIO_DBG

#ifdef ESP_GPIO_DBG
#define esp_msg(...)    do {printk("[esp8089]: "__VA_ARGS__);} while(0)
#else
#define esp_msg(...) 
#endif //ESP_GPIO_DBG

static int esp_wl_chip_en = 0;
static int esp_wl_rst = 0;
static char *axp_name[4] = {NULL};
static int esp_gpio_ctrl(char*, int);

static int sunxi_esp_gpio_req(struct gpio_config *gpio)
{
	int            ret = 0;
	char           pin_name[8] = {0};
	unsigned long  config;

	sunxi_gpio_to_name(gpio->gpio, pin_name);
	config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_FUNC, gpio->mul_sel);
  ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
  if (ret) {
    esp_msg("set gpio %s mulsel failed.\n",pin_name);
    return -1;
  }

	if (gpio->pull != GPIO_PULL_DEFAULT){
    config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_PUD, gpio->pull);
    ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
    if (ret) {
      esp_msg("set gpio %s pull mode failed.\n",pin_name);
      return -1;
    }
	}

	if (gpio->drv_level != GPIO_DRVLVL_DEFAULT){
    config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_DRV, gpio->drv_level);
    ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
    if (ret) {
      esp_msg("set gpio %s driver level failed.\n",pin_name);
      return -1;
    }
	}

	if (gpio->data != GPIO_DATA_DEFAULT) {
    config = SUNXI_PINCFG_PACK(SUNXI_PINCFG_TYPE_DAT, gpio->data);
    ret = pin_config_set(SUNXI_PINCTRL, pin_name, config);
    if (ret) {
      esp_msg("set gpio %s initial val failed.\n",pin_name);
      return -1;
    }
  }

  return 0;
}

// power control by axp
static int esp_module_power(int onoff)
{
	struct regulator* wifi_ldo[4] = {NULL};
	static int first = 1;
	int i = 0, ret = 0;

	esp_msg("esp module power set by axp.\n");
	for (i = 0; axp_name[i] != NULL; i++){
	  wifi_ldo[i] = regulator_get(NULL, axp_name[i]);
	  if (IS_ERR(wifi_ldo[i])) {
      esp_msg("get power regulator %s failed.\n", axp_name[i]);
      break;
		}
	}

  wifi_ldo[i] = NULL;

	if (first) {
		esp_msg("first time\n");
		for (i = 0; wifi_ldo[i] != NULL; i++){
		  ret = regulator_force_disable(wifi_ldo[i]);
      if (ret < 0) {
        esp_msg("regulator_force_disable  %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
		
		first = 0;
		if (!onoff)
			goto exit; 
	}

	if (onoff) {
		esp_msg("regulator on.\n");
		
		for(i = 0; wifi_ldo[i] != NULL; i++){
      ret = regulator_set_voltage(wifi_ldo[i], 3300000, 3300000);
      if (ret < 0) {
        esp_msg("regulator_set_voltage %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }

      ret = regulator_enable(wifi_ldo[i]);
      if (ret < 0) {
        esp_msg("regulator_enable %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
		
	} else {
		esp_msg("regulator off.\n");
		for(i = 0; wifi_ldo[i] != NULL; i++){
      ret = regulator_disable(wifi_ldo[i]);
      if (ret < 0) {
        esp_msg("regulator_disable %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
	}

exit:	
	for(i = 0; wifi_ldo[i] != NULL; i++){
	  regulator_put(wifi_ldo[i]);
	}
	
	return ret;
}

static int esp_gpio_ctrl(char* name, int level)
{
	int i = 0;
	int ret = 0;
	int gpio = 0;
	unsigned long flags = 0;
	char * gpio_name[2] = {"esp_wl_chip_en", "esp_wl_rst"};

  for (i = 0; i < 2; i++) {
    if (strcmp(name, gpio_name[i]) == 0) {
			switch (i)
			{
				case 0:
					gpio = esp_wl_chip_en;
					break;
				case 1:
					gpio = esp_wl_rst;
					break;
				default:
					esp_msg("no matched gpio.\n");
			}
			break;	
		}
  }

  __gpio_set_value(gpio, level);
  printk("gpio %s set val %d, act val %d\n", name, level, __gpio_get_value(gpio));

	return 0;
}

static void esp_standby(int instadby)
{
	if(instadby){
    esp_msg("suspend");
	}else{
		esp_msg("resume");
	}
}
static void esp_wl_power(int mode, int *updown)
{
    if (mode) {
        if (*updown) {
        	  esp_gpio_ctrl("esp_wl_chip_en", 1);
            mdelay(10);
        } else {
			      esp_gpio_ctrl("esp_wl_chip_en", 0);
			      mdelay(10);
        }
        esp_msg("sdio wifi power state: %s\n", *updown ? "on" : "off");
    }
   
    return;
}

void esp8089_gpio_init(void)
{
	script_item_u val ;
	script_item_value_type_e type;
	struct wifi_pm_ops *ops = &wifi_select_pm_ops;

	esp_msg("%s\n", __func__);

  type = script_get_item(wifi_para, "wifi_power", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		esp_msg("failed to fetch wifi_power\n");
	}

	axp_name[0] = val.str;
	esp_msg("module power name %s\n", axp_name[0]);

  type = script_get_item(wifi_para, "wifi_power_ext1", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		esp_msg("failed to fetch wifi_power\n");
	}
  axp_name[1] = val.str;
  esp_msg("module power ext1 name %s\n", axp_name[1]);
  
  type = script_get_item(wifi_para, "wifi_power_ext2", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		esp_msg("failed to fetch wifi_power\n");
	}
  axp_name[2] = val.str;
  esp_msg("module power ext2 name %s\n", axp_name[2]);

  axp_name[3] = NULL;

	type = script_get_item(wifi_para, "esp_wl_chip_en", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_PIO!=type) 
		esp_msg("get esp_wl_chip_en failed\n");
	else
		esp_wl_chip_en = val.gpio.gpio;
	sunxi_esp_gpio_req(&val.gpio);

  type = script_get_item(wifi_para, "esp_wl_rst", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_PIO!=type) 
		esp_msg("get esp_wl_chip_en failed\n");
	else {
		esp_wl_rst = val.gpio.gpio;
    sunxi_esp_gpio_req(&val.gpio);
  }

	ops->standby 	  = esp_standby;
	ops->gpio_ctrl   = esp_gpio_ctrl;
	ops->power 	 = esp_wl_power;
	
	esp_module_power(1);
}

EXPORT_SYMBOL(esp8089_gpio_init);

