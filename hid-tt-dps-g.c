#include <linux/init.h>
#include <linux/module.h>
#include <linux/hid.h>

// #include <linux/hwmon.h>
// #include <linux/hwmon-sysfs.h>

static int dpsg_probe(struct hid_device *device, const struct hid_device_id *id)
{

        // add the sysfs files here
        // all the sensor readings
        // maybe also the fan speed controller
        // potentially the rgb light controller

        // TODO: Send request to psu to get the model number
        printk(KERN_INFO "[*] Thermaltake DPS G PSU (%04X:%04X) plugged\n", id->vendor, id->product);
        return 0; // returning 0 indicates that this driver will manage this device
}

static void dpsg_remove(struct hid_device *device) 
{                               // prolly need to rephrase this
        printk(KERN_INFO "[*] Thermaltake DPS G PSU removed\n");

        // remove the sysfs files here
}

static struct hid_device_id dpsg_table[] = {
                                        // vendor id, product id
        { HID_DEVICE(HID_BUS_ANY, HID_GROUP_ANY, 0x264a, 0x2329) },
        {} /* Terminating entry */
};
MODULE_DEVICE_TABLE (hid, dpsg_table); // what does this do?

static struct hid_driver dpsg_driver = 
{
        .name = "Thermaltake DPS G Driver",
        .id_table = dpsg_table,
        .probe = dpsg_probe,    // called to create the sysfs files
        .remove = dpsg_remove,  // called to remove the sysfs files (if nessesary idk yet)
};

module_hid_driver(dpsg_driver);

MODULE_LICENSE("Dual BSD/GPL"); // hmm gotta ask janis about this
MODULE_AUTHOR("Toms Štrāls");   // hmm special characters
MODULE_DESCRIPTION("A PSU sensor driver");