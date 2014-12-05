/*
 * rtl8189es sdio wifi power management API
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/sys_config.h>
#include <mach/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/err.h>
#include "wifi_pm.h"

#define rtl8189es_msg(...)    do {printk("[rtl8189es]: "__VA_ARGS__);} while(0)

static int rtl8189es_powerup = 0;
static int rtl8189es_suspend = 0;
static int rtl8189es_shdn = 0;
static char *axp_name[4] = {NULL};

// power control by axp
static int rtl8189es_module_power(int onoff)
{
	struct regulator* wifi_ldo[4] = {NULL};
	static int first = 1;
	int i = 0, ret = 0;

	rtl8189es_msg("rtl8189es module power set by axp.\n");
	for (i = 0; axp_name[i] != NULL; i++){
	  wifi_ldo[i] = regulator_get(NULL, axp_name[i]);
	  if (IS_ERR(wifi_ldo[i])) {
      rtl8189es_msg("get power regulator %s failed.\n", axp_name[i]);
      break;
		}
	}

  wifi_ldo[i] = NULL;

	if (first) {
		rtl8189es_msg("first time\n");
		for (i = 0; wifi_ldo[i] != NULL; i++){
		  ret = regulator_force_disable(wifi_ldo[i]);
      if (ret < 0) {
        rtl8189es_msg("regulator_force_disable  %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
		
		first = 0; 
	}

	if (onoff) {
		rtl8189es_msg("regulator on.\n");
		
		for(i = 0; wifi_ldo[i] != NULL; i++){
      ret = regulator_set_voltage(wifi_ldo[i], 3300000, 3300000);
      if (ret < 0) {
        rtl8189es_msg("regulator_set_voltage %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }

      ret = regulator_enable(wifi_ldo[i]);
      if (ret < 0) {
        rtl8189es_msg("regulator_enable %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
		
	} else {
		rtl8189es_msg("regulator off.\n");
		for(i = 0; wifi_ldo[i] != NULL; i++){
      ret = regulator_disable(wifi_ldo[i]);
      if (ret < 0) {
        rtl8189es_msg("regulator_disable %s fail, return %d.\n", axp_name[i], ret);
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

static int rtl8189es_gpio_ctrl(char* name, int level)
{
	int i = 0;	
	int gpio = 0;
	char * gpio_name[1] = {"rtl8189es_shdn"};

	for (i = 0; i < 1; i++) {
		if (strcmp(name, gpio_name[i]) == 0) {
			switch (i)
			{
				case 0: /*rtl8189es_shdn*/
					gpio = rtl8189es_shdn;
					break;
				default:
					rtl8189es_msg("no matched gpio.\n");
			}
			break;
		}
	}

  __gpio_set_value(gpio, level);
  printk("gpio %s set val %d, act val %d\n", name, level, __gpio_get_value(gpio));
	
	return 0;
}

static void rtl8189es_standby(int instadby)
{
	if (instadby) {
		if (rtl8189es_powerup) {
			rtl8189es_suspend = 1;
		}
	} else {
		if (rtl8189es_suspend) {
			rtl8189es_suspend = 0;
		}
	}
	rtl8189es_msg("sdio wifi : %s\n", instadby ? "suspend" : "resume");
}

static void rtl8189es_power(int mode, int *updown)
{
  if (mode) {
		if (*updown) {
			rtl8189es_gpio_ctrl("rtl8189es_shdn", 1);
			mdelay(100);
		} else {
			rtl8189es_gpio_ctrl("rtl8189es_shdn", 0);
			mdelay(100);
		}
		rtl8189es_msg("sdio wifi power state: %s\n", *updown ? "on" : "off");
	}
}

void rtl8189es_gpio_init(void)
{
	script_item_u val ;
	script_item_value_type_e type;
	struct wifi_pm_ops *ops = &wifi_select_pm_ops;

	rtl8189es_msg("exec rtl8189es_wifi_gpio_init\n");

	type = script_get_item(wifi_para, "wifi_power", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		rtl8189es_msg("failed to fetch wifi_power\n");
	} else {
    axp_name[0] = val.str;
    rtl8189es_msg("module power name %s\n", axp_name[0]);
  }

  type = script_get_item(wifi_para, "wifi_power_ext1", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		rtl8189es_msg("failed to fetch wifi_power\n");
	} else {
    axp_name[1] = val.str;
    rtl8189es_msg("module power ext1 name %s\n", axp_name[1]);
  }
  
  type = script_get_item(wifi_para, "wifi_power_ext2", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		rtl8189es_msg("failed to fetch wifi_power\n");
	} else {
    axp_name[2] = val.str;
    rtl8189es_msg("module power ext2 name %s\n", axp_name[2]);
  }

  axp_name[3] = NULL;
	
	type = script_get_item(wifi_para, "rtl8189es_shdn", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_PIO!=type) 
		rtl8189es_msg("get rtl8189es rtl8189es_shdn gpio failed\n");
	else
		rtl8189es_shdn = val.gpio.gpio;

	rtl8189es_powerup = 0;
	rtl8189es_suspend = 0;
	ops->standby 	  = rtl8189es_standby;
	ops->power 		  = rtl8189es_power;

	rtl8189es_module_power(1);
	mdelay(100);
	rtl8189es_gpio_ctrl("rtl8189es_shdn", 0);

}
