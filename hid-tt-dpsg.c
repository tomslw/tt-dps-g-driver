#include <linux/module.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/mutex.h>

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

        /*
	 * buffer provided to hid_hw_raw_request must not be on the stack
	 * and must not be part of a data structure
	 */
        memcpy(ldev->buf, buf, MAX_REPORT_SIZE);

        ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
                                MAX_REPORT_SIZE,
                                HID_FEATURE_REPORT,
                                HID_REQ_SET_REPORT);

        mutex_unlock(&ldev->lock);

        if (ret < 0)
                return ret;

        return ret; // in the example: ret == ldev->config->report_size ? 0 : -EMSGSIZE;
}

static int tt_dpsg_recv(struct tt_dpsg_device *ldev, __u8 *buf) 
{
        int ret;

        mutex_lock(&ldev->lock);

        memcpy(ldev->buf, buf, MAX_REPORT_SIZE);

        ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
                                MAX_REPORT_SIZE,
                                HID_FEATURE_REPORT,
                                HID_REQ_SET_REPORT);
        
        if (ret < 0) {
                printk(KERN_INFO "[*] Error gotten after SET_REPORT: %d", ret);
                goto err;

        }

        ret = hid_hw_raw_request(ldev->hdev, buf[0], ldev->buf,
                                MAX_REPORT_SIZE,
                                HID_FEATURE_REPORT,
                                HID_REQ_GET_REPORT);

        memcpy(buf, ldev->buf, MAX_REPORT_SIZE);
err:
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

        // add the sysfs files here
        // all the sensor readings
        // maybe also the fan speed controller
        // potentially the rgb light controller

        printk(KERN_INFO "[*] Attempting to fetch model number");

        __u8 buf[MAX_REPORT_SIZE] = { 0x31, 0xfe };

        ret = tt_dpsg_recv(ldev, buf);
        if (ret)
                return ret;


        //hid_info(hdev, "%s initialized\n", <the the model number>);
        printk(KERN_INFO "[*] Thermaltake %.*s (%04X:%04X) plugged\n", MAX_REPORT_SIZE, buf, id->vendor, id->product);
        return 0;
}

static void tt_dpsg_remove(struct hid_device *hdev) 
{                               // prolly need to rephrase this
        printk(KERN_INFO "[*] Thermaltake DPS G PSU removed\n");

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
};

module_hid_driver(tt_dpsg_driver);

MODULE_LICENSE("Dual BSD/GPL"); // hmm gotta ask janis about this
MODULE_AUTHOR("Toms Štrāls");   // hmm special characters
MODULE_DESCRIPTION("A PSU sensor driver");