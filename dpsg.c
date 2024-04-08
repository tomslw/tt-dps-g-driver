#include <linux/init.h>
#include <linux/module.h>
#include <linux/usb.h>

// Based off of
// https://www.youtube.com/watch?v=juGNPLdjLH4
// https://www.youtube.com/watch?v=5IDL070RtoQ
// 


// probe function
// used on device insertion if no other driver has been called first
static int dpsg_probe(struct usb_interface *interface, const struct usb_device_id *id)
{
        // TODO: Send request to psu to get the model number
        printk(KERN_INFO "[*] Thermaltake DPS G PSU (%04X:%04X) plugged\n", id->idVendor, id->idProduct);
        return 0; // returning 0 indicates that this driver will manage this device
}

// disconnect
static void dpsg_disconnect(struct usb_interface *interface) 
{                               // prolly need to rephrase this
        printk(KERN_INFO "[*] Thermaltake DPS G PSU removed\n");
}

static struct usb_device_id dpsg_table[] = {
                // vendor id, product id
        {USB_DEVICE(0xffff, 0xffff)}, // usb id information goes here
        // {USB_DEVICE(v, p)}, // if supporting more than one device
        {} /* Terminating entry */
};
MODULE_DEVICE_TABLE (usb, dpsg_table); // what does this do?

static struct usb_driver dpsg_driver = 
{
        .name = "Thermaltake DPS G Driver",
        .id_table = dpsg_table, // this is how the kernel knows which driver to call to handle a device
        // more stuff? idk, there was more stuff for a pen drive, but this aint one
        .probe = dpsg_probe, // would be called when device gets attached
        .disconnect = dpsg_disconnect, // how nessesary are these?
};



static int __init dpsg_init(void)
{
        int ret = -1;
        printk(KERN_ALERT "[*] Thermaltake DPS G Constructor of driver\n");
        printk(KERN_ALERT "\tRegistering Driver with Kernel");
        ret = usb_register(&dpsg_driver);
        printk(KERN_ALERT "\tRegistration is complete");

        return ret;
}


        // find out why __thing is nessesary
static void __exit dpsg_exit(void)
{
        printk(KERN_ALERT "[*] Thermaltake DPS G Destructor of driver\n");
        usb_deregister(&dpsg_driver);
        printk(KERN_ALERT "Unregistration complete\n");
}

module_init(dpsg_init);
module_exit(dpsg_exit);

MODULE_LICENSE("Dual BSD/GPL"); // hmm gotta ask janis about this
MODULE_AUTHOR("Toms Štrāls");   // hmm special characters
MODULE_DESCRIPTION("A PSU sensor driver");