#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <usb.h>
#include <stdint.h>

#define DRIVER_NAME_LEN	1024
#define USB_BUF_LEN	1024

#define AQ_GREEN	1000
#define AQ_YELLOW	1500
#define AQ_RED		4000

#define USB_VENDOR_CO2_STICK	0x03eb
#define USB_PRODUCT_CO2_STICK	0x2013

enum led_state {
	GREEN = 0,
	YELLOW,
	RED,
	UNKNOWN,
};

static const char * const air_quality[] = {
	[GREEN]		= "GREEN",
	[YELLOW]	= "YELLOW",
	[RED]		= "RED",
	[UNKNOWN]	= "UNKNOWN",
};


static int
read_one_sensor (struct usb_device *dev)
{
	int ret;
	uint16_t value;
	enum led_state colour;
	struct usb_dev_handle *devh;
	char driver_name[DRIVER_NAME_LEN] = "";
	char usb_io_buf[USB_BUF_LEN] =	"\x40\x68\x2a\x54"
					"\x52\x0a\x40\x40"
					"\x40\x40\x40\x40"
					"\x40\x40\x40\x40";
	unsigned int highbit;

	/* Open USB device.  */
	devh = usb_open (dev);
	if (! dev) {
		fprintf (stderr, "Failed to usb_open(%p)\n", (void *) dev);
		ret = -1;
		goto out;
	}

	/* Ensure that the device isn't claimed.  */
	ret = usb_get_driver_np (devh, 0/*intrf*/, driver_name, sizeof (driver_name));
	if (! ret) {
		fprintf (stderr, "Warning: device is claimed by driver %s, "
			 "trying to unbind it.\n", driver_name);
		ret = usb_detach_kernel_driver_np (devh, 0/*intrf*/);
		if (ret) {
			fprintf (stderr, "Failed to detatch kernel driver.\n");
			ret = -2;
			goto out;
		}
	}

	/* Claim device.  */
	ret = usb_claim_interface (devh, 0/*intrf*/);
	if (ret) {
		fprintf (stderr, "usb_claim_interface() failed with error %d=%s\n",
			 ret, strerror (-ret));
		ret = -3;
		goto out;
	}

	/* Send query command.  */
	ret = usb_interrupt_write (devh, 0x0002/*endpoint*/,
				   usb_io_buf, 0x10/*len*/, 1000/*msec*/);
	if (ret < 0) {
		fprintf (stderr, "Failed to usb_interrupt_write() the initial buffer, ret = %d\n", ret);
		ret = -4;
		goto out_unlock;
	}

	/* Read answer.  */
	do {
		printf ("read\n");
		ret = usb_interrupt_read (devh, 0x0081/*endpoint*/,
					  usb_io_buf, 0x10/*len*/, 1000/*msec*/);
		if (ret < 0) {
			fprintf (stderr, "Failed to usb_interrupt_read() #1\n");
			ret = -5;
			goto out_unlock;
		} else if (ret != 0x10) {
			fprintf (stderr, "usb_interrupt_read() returned %d\n", ret);
		}
	} while (ret != 0x10 /* == len */);

	/* Prepare value from first read.  */
	value =   ((unsigned char *) usb_io_buf)[3] << 8
		| ((unsigned char *) usb_io_buf)[2] << 0;

	/* Mask off MSB from `value'.  */
	highbit = !! (value & 0x8000);
	value = abs ((int16_t) value);

	/* Classify `value'.  */
	if (value <= AQ_GREEN)
		colour = GREEN;
	else if (value <= AQ_YELLOW)
		colour = YELLOW;
	else if (value < 15000)
		colour = RED;
	else
		colour = UNKNOWN;

	printf ("Device %s:%d, highbit=%u value = %u, quality = %s\n",
		dev->bus->dirname,
		dev->devnum,
		highbit,
		(unsigned int) value, air_quality[colour]);

out_unlock:
	ret = usb_release_interface (devh, 0/*intrf*/);
	if (ret) {
		fprintf (stderr, "Failed to usb_release_interface()\n");
		ret = -5;
	}
out:
	return ret;
}

static int
find_devices (int vendor, int product) {
	struct usb_bus *bus;
	struct usb_device *dev;
	int ret = 0;

	for (bus = usb_get_busses (); bus; bus = bus->next)
		for (dev = bus->devices; dev; dev = dev->next)
			if (dev->descriptor.idVendor == vendor
			    && dev->descriptor.idProduct == product)
				ret |= read_one_sensor (dev);

	return ret;
}
 
int
main (int argc, char *argv[])
{
	int ret;

	usb_init ();
	usb_set_debug (0);
	usb_find_busses ();
	usb_find_devices ();
 
	ret = find_devices (USB_VENDOR_CO2_STICK, USB_PRODUCT_CO2_STICK);

	exit (ret? EXIT_FAILURE: EXIT_SUCCESS);
}
