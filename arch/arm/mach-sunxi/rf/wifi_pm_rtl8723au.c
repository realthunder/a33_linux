/*
 * rtl8723au usb wifi power management API
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <mach/sys_config.h>
#include <mach/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/semaphore.h>
#include <linux/err.h>
#include "wifi_pm.h"

extern int sunxi_usb_disable_hcd(__u32 usbc_no);
extern int sunxi_usb_enable_hcd(__u32 usbc_no);

#define rtl8723au_msg(...)    do {printk("[rtl8723au]: "__VA_ARGS__);} while(0)

static char *axp_name[4] = {NULL};
static bool axp_power_on = false;
static int rtl8723au_wifi_power = 0;
static int rtl8723au_bt_power = 0;
static int usbc_id = 1;
static struct semaphore power_mutex;

// power control by axp
static int rtl8723au_module_power(int onoff)
{
	struct regulator* wifi_ldo[4] = {NULL};
	static int first = 1;
	int i = 0, ret = 0;

	rtl8723au_msg("rtl8188eu module power set by axp.\n");
	for (i = 0; axp_name[i] != NULL; i++){
	  wifi_ldo[i] = regulator_get(NULL, axp_name[i]);
	  if (IS_ERR(wifi_ldo[i])) {
      rtl8723au_msg("get power regulator %s failed.\n", axp_name[i]);
      break;
		}
	}

  wifi_ldo[i] = NULL;

	if (first) {
		rtl8723au_msg("first time\n");
		for (i = 0; wifi_ldo[i] != NULL; i++){
		  ret = regulator_force_disable(wifi_ldo[i]);
      if (ret < 0) {
        rtl8723au_msg("regulator_force_disable  %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
		
		first = 0;
		if (!onoff)
			goto exit; 
	}

	if (onoff) {
		rtl8723au_msg("regulator on.\n");
		
		for(i = 0; wifi_ldo[i] != NULL; i++){
      ret = regulator_set_voltage(wifi_ldo[i], 3300000, 3300000);
      if (ret < 0) {
        rtl8723au_msg("regulator_set_voltage %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }

      ret = regulator_enable(wifi_ldo[i]);
      if (ret < 0) {
        rtl8723au_msg("regulator_enable %s fail, return %d.\n", axp_name[i], ret);
        goto exit;
      }
		}
		
	} else {
		rtl8723au_msg("regulator off.\n");
		for(i = 0; wifi_ldo[i] != NULL; i++){
      ret = regulator_disable(wifi_ldo[i]);
      if (ret < 0) {
        rtl8723au_msg("regulator_disable %s fail, return %d.\n", axp_name[i], ret);
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

static int rtl8723au_gpio_ctrl(char* name, int level)
{
	static int usbc_enable = 0;

	if (down_interruptible(&power_mutex)){
	  rtl8723au_msg("down_interruptible failed\n");
	  return -ERESTARTSYS;
	}

	if (strcmp(name, "rtl8723au_wl") == 0) {
		if ((level && !rtl8723au_bt_power)	|| (!level && !rtl8723au_bt_power)) {
			rtl8723au_msg("rtl8723au is powered %s by wifi\n", level ? "up" : "down");
			goto power_change;
		} else {
			if (level) {
				rtl8723au_msg("rtl8723au is already on by bt\n");
			} else {
				rtl8723au_msg("rtl8723au should stay on because of bt\n");
			}
			goto state_change;
		}
	}

	if (strcmp(name, "rtl8723au_bt") == 0) {
		if ((level && !rtl8723au_wifi_power && !rtl8723au_bt_power)	|| (!level && !rtl8723au_wifi_power)) {
			rtl8723au_msg("rtl8723au is powered %s by bt\n", level ? "up" : "down");
			goto power_change;
		} else {
			if (level) {
				rtl8723au_msg("rtl8723au is already on by wifi\n");
			} else {
				rtl8723au_msg("rtl8723au should stay on because of wifi\n");
			}
			goto state_change;
		}
	}

  up(&power_mutex);
	return 0;

power_change:
	if (level) {
		if (!usbc_enable) {
			sunxi_usb_enable_hcd(usbc_id);
			usbc_enable = 1;
		}
	} else {
		if (usbc_enable) {
			sunxi_usb_disable_hcd(usbc_id);
			usbc_enable = 0;
		}
	}

state_change:
	if (strcmp(name, "rtl8723au_wl")==0)
		rtl8723au_wifi_power = level;
	if (strcmp(name, "rtl8723au_bt")==0)
		rtl8723au_bt_power = level;
	rtl8723au_msg("rtl8723au power state change: wifi %d, bt %d !!\n", rtl8723au_wifi_power, rtl8723au_bt_power);

	up(&power_mutex);
	return 0;
}

void rtl8723au_power(int mode, int *updown)
{
    if (mode) {
        if (*updown) {
        	rtl8723au_gpio_ctrl("rtl8723au_wl", 1);
        } else {
        	rtl8723au_gpio_ctrl("rtl8723au_wl", 0);
        }
    } else {
        if (rtl8723au_wifi_power)
            *updown = 1;
        else
            *updown = 0;
		rtl8723au_msg("usb wifi power state: %s\n", rtl8723au_wifi_power ? "on" : "off");
    }
    return;
}

static void rtl8723au_standby(int instadby)
{
	if (instadby) {
		//rtl8723au_module_power(0);
	} else {
		//rtl8723au_module_power(1);
	}
}

void rtl8723au_gpio_init(void)
{
	script_item_u val;
	script_item_value_type_e type;
	struct wifi_pm_ops *ops = &wifi_select_pm_ops;

	rtl8723au_msg("exec rtl8723au_wifi_gpio_init\n");

	type = script_get_item(wifi_para, "wifi_power", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		rtl8723au_msg("failed to fetch wifi_power\n");
	}

	axp_name[0] = val.str;
	rtl8723au_msg("module power name %s\n", axp_name[0]);

  type = script_get_item(wifi_para, "wifi_power_ext1", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		rtl8723au_msg("failed to fetch wifi_power_ext1\n");
	}
  axp_name[1] = val.str;
  rtl8723au_msg("module power ext1 name %s\n", axp_name[1]);
  
  type = script_get_item(wifi_para, "wifi_power_ext2", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_STR != type) {
		rtl8723au_msg("failed to fetch wifi_power_ext2\n");
	}
  axp_name[2] = val.str;
  rtl8723au_msg("module power ext2 name %s\n", axp_name[2]);

  axp_name[3] = NULL;

	type = script_get_item(wifi_para, "wifi_usbc_id", &val);
	if (SCIRPT_ITEM_VALUE_TYPE_INT != type) {
		rtl8723au_msg("failed to fetch wifi_usbc_id\n");
		return ;
	}
	usbc_id = val.val;

	ops->standby   = rtl8723au_standby;
	ops->gpio_ctrl = rtl8723au_gpio_ctrl;
	ops->power = rtl8723au_power;
	rtl8723au_wifi_power = 0;
	rtl8723au_bt_power = 0;

	sema_init(&power_mutex, 1);

	// make wifi & bt poer always on
	sunxi_usb_disable_hcd(usbc_id);

	mdelay(1);
	rtl8723au_module_power(1);
}
