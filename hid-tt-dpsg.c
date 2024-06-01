#include <linux/module.h>
#include <linux/hid.h>
#include <linux/hidraw.h>
#include <linux/mutex.h>
#include <linux/hwmon.h>
#include <linux/math64.h>

// Heavily inspired by: gigabyte_waterforce.c

#define MAX_REPORT_SIZE		64
#define MAX_MODEL_SIZE          63 // 62 bytes + '\0'

#define REQUEST_TIMEOUT		(2 * 1000)      // ms


#define REQUEST_SENS_REPORT_ID          0x31
#define REQUEST_MODEL_REPORT_ID         0xfe

#define SENSOR_ID_12V                   0x34
#define SENSOR_ID_5V                    0x35
#define SENSOR_ID_33V                   0x36
#define SENSOR_ID_12I                   0x37
#define SENSOR_ID_5I                    0x38
#define SENSOR_ID_33I                   0x39
#define SENSOR_ID_TEMP                  0x3a
#define SENSOR_ID_FAN                   0x3b

static const u8 *const dpsg_voltage_sens_id[] = {
	SENSOR_ID_12V,
        SENSOR_ID_5V,
        SENSOR_ID_33V
};

static const u8 *const dpsg_current_sens_id[] = {
	SENSOR_ID_12I,
        SENSOR_ID_5I,
        SENSOR_ID_33I
};

static const u8 *const dpsg_temp_sens_id[] = {
	SENSOR_ID_TEMP
};

static const u8 *const dpsg_fan_sens_id[] = {
	SENSOR_ID_FAN
};

static const char *const dpsg_voltage_label[] = {
	"12V rail Voltage",
        "5V rail Voltage",
        "3.3V rail Voltage"
};

static const char *const dpsg_currant_label[] = {
	"12V rail Currant",
        "5V rail Currant",
        "3.3V rail Currant"
};

static const char *const dpsg_temp_label[] = {
	"PSU Temperature"
};

static const char *const dpsg_fan_label[] = {
	"Fan RPM",
};



struct dpsg_device {
	struct hid_device *hdev;
	struct device *hwmon_dev;
	/* For locking access to buffer */
	struct mutex buffer_lock;
	/* For queueing multiple readers */
	struct mutex status_report_request_mutex;
	/* For reinitializing the completion below */
	spinlock_t status_report_request_lock;
	struct completion sensor_report_received;
	struct completion model_processed;

	/* Sensor data */
        s32 in_input[3];
        s32 curr_input[3];
	s32 temp_input[1];
	u16 fan_input[1];
        char model[MAX_MODEL_SIZE]; // theoretical max 

	u8 *buf;
        unsigned long updated; // do we NEED jiffies?
};

// works
static int tt_dpsg_send(struct dpsg_device *ldev, __u8 *buf) 
{
        int ret;

        mutex_lock(&ldev->buffer_lock);

        /*
	 * buffer provided to hid_hw_raw_request must not be on the stack
	 * and must not be part of a data structure
	 */
        memcpy(ldev->buf, buf, MAX_REPORT_SIZE);

        ret = hid_hw_output_report(ldev->hdev, ldev->buf, MAX_REPORT_SIZE);

        mutex_unlock(&ldev->buffer_lock);
                                        // meaning "Message too long", gotta look into if thats the case
        return ret == MAX_REPORT_SIZE ? 0 : -EMSGSIZE;
}

// works
static int dpsg_get_model(struct dpsg_device *ldev) 
{
        int ret;

        __u8 buf[MAX_REPORT_SIZE] = { 0xfe, 0x31 };

        ret = tt_dpsg_send(ldev, buf);
        if (ret < 0)
                return ret;

	ret = wait_for_completion_interruptible_timeout(&ldev->model_processed,
							msecs_to_jiffies(REQUEST_TIMEOUT));
	if (ret == 0) {
		return -ETIMEDOUT;
        }
	else if (ret < 0) {
		return ret;
        }

	return 0;
}

static int dpsg_get_sensor(struct dpsg_device *ldev, enum hwmon_sensor_types type, int channel) 
{
        int ret;

        __u8 buf[MAX_REPORT_SIZE] = { REQUEST_SENS_REPORT_ID };

	switch (type) {
        case hwmon_in:
                buf[1] = dpsg_voltage_sens_id[channel];
                break;
        case hwmon_curr:
		buf[1] = dpsg_current_sens_id[channel];
		break;
	case hwmon_temp:
		buf[1] = dpsg_temp_sens_id[channel];
		break;
	case hwmon_pwm:
		buf[1] = dpsg_fan_sens_id[channel];
		break;
	default:
                return -EINVAL;
		break;
	}

        ret = tt_dpsg_send(ldev, buf);
        if (ret < 0)
                return ret;

	ret = wait_for_completion_interruptible_timeout(&ldev->sensor_report_received,
							msecs_to_jiffies(REQUEST_TIMEOUT));

        spin_lock_bh(&ldev->status_report_request_lock);
	reinit_completion(&ldev->sensor_report_received);
	spin_unlock_bh(&ldev->status_report_request_lock);

	if (ret == 0) {
		return -ETIMEDOUT;
        }
	else if (ret < 0) {
		return ret;
        }

	return 0;
} 

static umode_t dpsg_is_visible(const void *data,
				     enum hwmon_sensor_types type, u32 attr, int channel)
{
	switch (type) {
        case hwmon_in:
		switch (attr) {
		case hwmon_in_label:
		case hwmon_in_input:
			return 0444;
		default:
			break;
		}
		break;
        case hwmon_curr:
		switch (attr) {
		case hwmon_curr_label:
		case hwmon_curr_input:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_label:
		case hwmon_temp_input:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_pwm:
		switch (attr) {
		case hwmon_pwm_input:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return 0;
}



static int dpsg_read(struct device *dev, enum hwmon_sensor_types type,
			   u32 attr, int channel, long *val)
{
	struct dpsg_device *ldev = dev_get_drvdata(dev);
	int ret = dpsg_get_sensor(ldev, type, channel); // sends request for sensor data

	if (ret < 0)
		return ret;

	switch (type) {
        case hwmon_in:
		*val = ldev->in_input[channel];
		break;
        case hwmon_curr:
		*val = ldev->curr_input[channel];
		break;
	case hwmon_temp:
		*val = ldev->temp_input[channel];
		break;
	case hwmon_fan:
		*val = ldev->fan_input[channel];
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}

static int dpsg_read_string(struct device *dev, enum hwmon_sensor_types type,
				  u32 attr, int channel, const char **str)
{
	switch (type) {
        case hwmon_in:
		*str = dpsg_voltage_label[channel];
		break;
        case hwmon_curr:
		*str = dpsg_currant_label[channel];
		break;
	case hwmon_temp:
		*str = dpsg_temp_label[channel];
		break;
	case hwmon_fan:
		*str = dpsg_fan_label[channel];
		break;
	default:
		return -EOPNOTSUPP;	/* unreachable */
	}

	return 0;
}
// https://github.com/torvalds/linux/blob/master/drivers/hwmon/gigabyte_dpsg.c#L164
static const struct hwmon_ops dpsg_hwmon_ops = {
	.is_visible = dpsg_is_visible,
	.read = dpsg_read,
	.read_string = dpsg_read_string
};

// https://docs.kernel.org/hwmon/hwmon-kernel-api.html
static const struct hwmon_channel_info *dpsg_info[] = {
        HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_LABEL,
                           HWMON_C_INPUT | HWMON_C_LABEL,
                           HWMON_C_INPUT | HWMON_C_LABEL),
        HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_LABEL,
                           HWMON_I_INPUT | HWMON_I_LABEL,
                           HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_LABEL),
	NULL
};



static const struct hwmon_chip_info dpsg_chip_info = {
	.ops = &dpsg_hwmon_ops,
	.info = dpsg_info,
};

static int tt_dpsg_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
        printk(KERN_INFO "[*] start of TT_DPSG_probe\n", id->vendor, id->product);
        struct dpsg_device *ldev;
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
        hid_set_drvdata(hdev, ldev);

        ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW); // could i use hiddev
        if (ret)                                      // if so do i still need lock?         
		return ret;

        // taken straight from hidraw.c hidraw_open() 
        ret = hid_hw_power(hdev, PM_HINT_FULLON);
        if (ret < 0)
                goto fail_and_stop;

        ret = hid_hw_open(hdev);
        if (ret < 0) {
                hid_hw_power(hdev, PM_HINT_NORMAL);
                goto fail_and_stop;
        }

	mutex_init(&ldev->status_report_request_mutex);
	mutex_init(&ldev->buffer_lock);
	spin_lock_init(&ldev->status_report_request_lock);
	init_completion(&ldev->sensor_report_received);
	init_completion(&ldev->model_processed);

        hid_device_io_start(hdev); // allows raw_event to be called while in probe

        ret = dpsg_get_model(ldev);
        if (ret < 0)
                goto fail_and_close;


        // char *sanitized_model = hwmon_sanitize_name(&ldev->model);
        ldev->hwmon_dev = hwmon_device_register_with_info(&hdev->dev, "TT_DPS_G",
							  ldev, &dpsg_chip_info, NULL);

        if (IS_ERR(ldev->hwmon_dev)) {
		ret = PTR_ERR(ldev->hwmon_dev);
		hid_err(hdev, "hwmon registration failed with %d\n", ret);
		goto fail_and_close;
	}

        //hid_info(hdev, "%s initialized\n", <the the model number>);
        printk(KERN_INFO "[*] Thermaltake %.*s (%04X:%04X) plugged\n", MAX_MODEL_SIZE, ldev->model, id->vendor, id->product);
        
        return ret;

fail_and_close:
	hid_hw_close(hdev);
fail_and_stop:
	hid_hw_stop(hdev);
	return ret;
}

// This is where all of the response managemant will happen
// might need to have some sort of memory for each sensor, so that the sysfs files
// have something to read
static int tt_dpsg_raw_event(struct hid_device *hdev, struct hid_report *report,
	 u8 *data, int size)
{
        printk(KERN_INFO "[*] Raw event: %.*s %d %d\n", size, data, data[0], data[1]);


        struct dpsg_device *ldev = hid_get_drvdata(hdev);
        int i;

        if (data[0] == REQUEST_MODEL_REPORT_ID) {
                for (i = 2; i < MAX_REPORT_SIZE; i++) {
                        if (i < size && data[i] != 0) {
                                ldev->model[i-2] = data[i];
                        }
                }
                if (!completion_done(&ldev->model_processed))
                        complete_all(&ldev->model_processed);
                
                return 0;
        }

        u16 manissa = ((u16)(data[3] & 0x0f) << 8) + data[2];
        // cant POW in the kernel, the values were hard coded in the og source code anyway
        // s8 exponent = (s8)((data[3] & 0x80) || ((data[3] & 0x70) >> 4)); 
        u32 value;

        if (data[0] == REQUEST_SENS_REPORT_ID) {
                switch (data[1])
                {
                case SENSOR_ID_12V:
                        value = (manissa * 1000) / 64;
                        ldev->in_input[0] = value;
                        break;
                case SENSOR_ID_5V:
                        value = (manissa * 1000) / 64;
                        ldev->in_input[1] = (u32)(value * 1000);
                        break;
                case SENSOR_ID_33V:
                        value = (manissa * 1000) / 64;
                        ldev->in_input[2] = (u32)(value * 1000);
                        break;
                case SENSOR_ID_12I:
                        value = (manissa * 1000) / 4;
                        ldev->curr_input[0] = (u32)(value * 1000);
                        break;
                case SENSOR_ID_5I:
                        value = (manissa * 1000) / 16;
                        ldev->curr_input[1] = (u32)(value * 1000);
                        break;
                case SENSOR_ID_33I:
                        value = (manissa * 1000) / 16;
                        ldev->curr_input[2] = (u32)(value * 1000);
                        break;
                case SENSOR_ID_TEMP:
                        value = (manissa * 1000) / 4;
                        ldev->temp_input[0] = (u32)(value * 1000);
                        break;
                case SENSOR_ID_FAN:
                        value = (manissa * 1000) * 4;
                        ldev->fan_input[0] = (u32)(value * 1000);
                        break;
                default:
                        break;
                }
        }

        printk(KERN_INFO "[*] Value: %d\n", value);

        spin_lock_bh(&ldev->status_report_request_lock);
        if (!completion_done(&ldev->sensor_report_received))
		complete_all(&ldev->sensor_report_received);
	spin_unlock_bh(&ldev->status_report_request_lock);

        // lock and completion here

        return 0;
}

static void tt_dpsg_remove(struct hid_device *hdev) 
{
        struct dpsg_device *ldev = hid_get_drvdata(hdev);
        hwmon_device_unregister(ldev->hwmon_dev);

        hid_hw_close(hdev);
        hid_hw_stop(hdev); // how nessesary?

        printk(KERN_INFO "[*] Thermaltake DPS G PSU removed\n");
}

static struct hid_device_id tt_dpsg_table[] = {
                                        // vendor id, product id
        { HID_USB_DEVICE(0x264a, 0x2329) },
        {} /* Terminating entry */
};

MODULE_DEVICE_TABLE (hid, tt_dpsg_table); // what does this do?

static struct hid_driver tt_dpsg_driver = 
{
        .name = "hid-tt-dpsg",
        .id_table = tt_dpsg_table,
        .probe = tt_dpsg_probe,    // called to create the sysfs files
        .remove = tt_dpsg_remove,  // called to remove the sysfs files (if nessesary idk yet)
        .raw_event = tt_dpsg_raw_event,
};

module_hid_driver(tt_dpsg_driver);

MODULE_LICENSE("Dual BSD/GPL"); // hmm gotta ask janis about this
MODULE_AUTHOR("Toms Štrāls");   // hmm special characters
MODULE_DESCRIPTION("A PSU sensor driver");