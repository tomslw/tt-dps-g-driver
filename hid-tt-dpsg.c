#include <linux/module.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/mutex.h>
#include <linux/usb.h>
#include <linux/delay.h>

#include "usbhid/usbhid.h"

// #include <linux/hwmon.h>
// #include <linux/hwmon-sysfs.h>

// Heavily inspired by: hid-led.c

struct tt_dpsg_device {
	struct hid_device       *hdev;
	u8			*buf;
	struct mutex		lock;
};

#define MAX_REPORT_SIZE		32      // ? hmm 64?

static int tt_dpsg_send(struct tt_dpsg_device *ldev, __u8 *buf) 
{
        int ret;

        mutex_lock(&ldev->lock);

        memcpy(ldev->buf, buf, MAX_REPORT_SIZE);

        ret = hid_hw_output_report(ldev->hdev, ldev->buf, MAX_REPORT_SIZE);
        
        if (ret < 0) {
                printk(KERN_INFO "[*] Error gotten after SET_REPORT: %d", ret);
        } else {
                memcpy(buf, ldev->buf, MAX_REPORT_SIZE);    
        }
        
        mutex_unlock(&ldev->lock);

        return ret < 0 ? ret : 0;
        
}

static int tt_dpsg_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
        struct tt_dpsg_device *ldev;
        int ret;

        ldev = devm_kzalloc(&hdev->dev, sizeof(*ldev), GFP_KERNEL);
        if (!ldev)
                return -ENOMEM;

        ldev->buf = devm_kmalloc(&hdev->dev, MAX_REPORT_SIZE, GFP_KERNEL);
        if (!ldev->buf)
                return -ENOMEM;

        // might be unnesessaey
        ret = hid_parse(hdev);
	if (ret) // couldn't an error code be negative aswell? idk what to expect here
		return ret;

        ldev->hdev = hdev;
        mutex_init(&ldev->lock);

        ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW); // could i use hiddev
        if (ret)                                      // if so do i still need lock?         
		return ret;

        // ======= way too low level zone! bad boys only =======
        // taken from a makro from usbhid/usbhid.h (cant import, maybe i can but cmon)
        struct usb_device *dev = to_usb_device(hdev->dev.parent->parent);
        
        ret = usb_driver_set_configuration(dev, 1);   // this function isn't beeing used in 
                                                // usb-hid.c
        if (ret)
                return ret;
        
        // sends an empty interrupt request
        // no clue why its needed, but it seems to start boot looping without it
        struct usbhid_device *usbhid = hdev->driver_data;
        
        // for some reason this gets sent after the previous request
        // but why tho hmm, its because the usb requests get sent after
        // this function finishes.
        int bytes_transfered;
        ret = usb_interrupt_msg(dev, usbhid->urbout->pipe,
				NULL, 0, &bytes_transfered,
				0);
        if (ret)                        
                return ret;

        // could call the sleep func here aswell

        // then in wire shark theres a random empty URB_INTERRUPT

        // =======

        // add the sysfs files here
        // all the sensor readings
        // maybe also the fan speed controller
        // potentially the rgb light controller

        // printk(KERN_INFO "[*] Attempting to fetch model number");

        // __u8 buf[MAX_REPORT_SIZE] = { 0x31, 0xfe };

        // ret = tt_dpsg_recv(ldev, buf);
        // if (ret)
        //         return ret;


        //hid_info(hdev, "%s initialized\n", <the the model number>);
        printk(KERN_INFO "[*] Thermaltake (%04X:%04X) plugged\n", id->vendor, id->product);
        // printk(KERN_INFO "[*] Thermaltake %.*s (%04X:%04X) plugged\n", MAX_REPORT_SIZE, buf, id->vendor, id->product);
        return 0;
}

static int tt_dpsg_raw_event(struct hid_device *hid, struct hid_report *report,
	 u8 *data, int size)
{
        printk(KERN_INFO "[*] tt_dpsg Raw event, report id: %d\n", report->id);
        return 0;
}

static void tt_dpsg_remove(struct hid_device *hdev) 
{                               // prolly need to rephrase this
        printk(KERN_INFO "[*] Thermaltake DPS G PSU removed\n");
        // run a "stop" ll_driver func indirectly here
        // remove the sysfs files here
}

static struct hid_device_id tt_dpsg_table[] = {
                                        // vendor id, product id
        { HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY, 0x264a, 0x2329) },
        {} /* Terminating entry */
};
MODULE_DEVICE_TABLE (hid, tt_dpsg_table); // what does this do?

static struct hid_driver tt_dpsg_driver = 
{
        .name = "Thermaltake DPS G Driver",
        .id_table = tt_dpsg_table,
        .probe = tt_dpsg_probe,    // called to create the sysfs files
        .remove = tt_dpsg_remove,  // called to remove the sysfs files (if nessesary idk yet)
        .raw_event = tt_dpsg_raw_event,
};

module_hid_driver(tt_dpsg_driver);

MODULE_LICENSE("Dual BSD/GPL"); // hmm gotta ask janis about this
MODULE_AUTHOR("Toms Štrāls");   // hmm special characters
MODULE_DESCRIPTION("A PSU sensor driver");