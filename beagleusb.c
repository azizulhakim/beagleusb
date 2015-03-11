/*	 Beaglebone USB Driver for Android Device Using AOA v2.0 Protocol
 *
 *   Copyright (C) 2014  Azizul Hakim
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>

#include "beagleusb.h"
#include "aoa.h"

struct beagleusb* beagle_allocate_device(){
	struct beagleusb *beagleusb;

	beagleusb = kzalloc(sizeof(struct beagleusb), GFP_KERNEL);
	if (beagleusb == NULL)
		goto ERROR;

	beagleusb->audio = kzalloc(sizeof(struct beagleaudio), GFP_KERNEL);
	if (beagleusb->audio == NULL)
		goto AUDIO_ALLOC_ERROR;

	return beagleusb;

AUDIO_ALLOC_ERROR:
	kfree(beagleusb);
	
ERROR:
	return NULL;
}


static int beagleusb_probe(struct usb_interface *intf,
	const struct usb_device_id *id)
{
	int ret = -1;
	int i;
	int datalen;
	u8 data[8];
	struct device *dev = &intf->dev;
	struct beagleusb *beagleusb;
	struct usb_device *usb_dev = interface_to_usbdev(intf);
	struct usb_host_interface *interface_descriptor;
	struct usb_endpoint_descriptor *endpoint;

	if (id->idVendor == 0x18D1 && (id->idProduct >= 0x2D00 && id->idProduct <= 0x2D05)){
		printk("BEAGLEDROID-USBAUDIO:  [%04X:%04X] Connected in AOA mode\n", id->idVendor, id->idProduct);


		printk("BEAGLEDUSB: intf.num_altsetting: %d\n", intf->num_altsetting);
		if (intf->altsetting != NULL)
			printk("BEAGLEUSB: intf->altsetting[0].des.bNumEndpoints : %d\n", intf->altsetting[0].desc.bNumEndpoints);

		interface_descriptor = intf->cur_altsetting;

		/* Device structure */
		beagleusb = beagle_allocate_device();
		if (beagleusb == NULL)
			return -ENOMEM;

		beagleusb->dev = dev;
		beagleusb->usbdev = usb_get_dev(interface_to_usbdev(intf));


		for(i=0; i<interface_descriptor->desc.bNumEndpoints; i++){
			endpoint = &interface_descriptor->endpoint[i].desc;

			if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) && 
				(endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK
			   ){
					printk("BEAGLEUSB: Bulk out endpoint");
					beagleusb->bulk_out_endpointAddr = endpoint->bEndpointAddress;
					break;

					//beagleusb->bulk_out_pipe = usb_sndbulkpipe(beagleaudio->udev, endpoint->bEndpointAddress);
				}
		}

		for(i=0; i<interface_descriptor->desc.bNumEndpoints; i++){
			endpoint = &interface_descriptor->endpoint[i].desc;

			if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) && 
				(endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK
			   ){
					printk("BEAGLEUSB: Bulk in endpoint\n");
					beagleusb->bulk_in_endpointAddr = endpoint->bEndpointAddress;
					break;

					//inputPipe = usb_rcvbulkpipe(beagleaudio->udev, endpoint->bEndpointAddress);
					///interval = endpoint->bInterval;
				}
		}

		usb_set_intfdata(intf, beagleusb);


		ret = beagleaudio_audio_init(beagleusb);
		if (ret < 0)
			goto beagleaudio_audio_fail;

		dev_info(beagleusb->dev, "BeagleBone USB Keyboard, Mouse, Audio Playback Driver\n");

		return 0;
	}
	else{
		datalen = GetProtocol(usb_dev, (char*)data);

		if (datalen < 0) {
			printk("BEAGLEUSB: Date Length : [%d]", datalen);
		}
		else{
			printk("BEAGLEDUSB: AOA version = %d\n", data[0]);
		}

		SendIdentificationInfo(usb_dev, ID_MANU, (char*)MANU);
		SendIdentificationInfo(usb_dev, ID_MODEL, (char*)MODEL);
		SendIdentificationInfo(usb_dev, ID_DESC, (char*)DESCRIPTION);
		SendIdentificationInfo(usb_dev, ID_VERSION, (char*)VERSION);
		SendIdentificationInfo(usb_dev, ID_URI, (char*)URI);
		SendIdentificationInfo(usb_dev, ID_SERIAL, (char*)SERIAL);

		SendAOAActivationRequest(usb_dev);

		return 0;
	}

	goto exit;

beagleaudio_audio_fail:
	printk("beagleaudio_audio_fail\n");
	kfree(beagleusb->audio);
	kfree(beagleusb);

exit:
	return ret;
}

static void beagleusb_disconnect(struct usb_interface *intf)
{
	struct beagleusb *beagleusb = usb_get_intfdata(intf);

	printk("beagleaudio_disconnect\n");

	usb_set_intfdata(intf, NULL);

	if (!beagleusb)
		return;

	beagleaudio_audio_free(beagleusb);

	if (beagleusb->usbdev != NULL){
		printk("usb_put_dev start\n");
		usb_put_dev(beagleusb->usbdev);
		printk("usb_put_dev end\n");
	}
	beagleusb->usbdev = NULL;
}

struct usb_device_id beagleusb_id_table[] = {
	{ USB_DEVICE_AND_INTERFACE_INFO(0x18D1, 0x4E41, 255, 255, 0) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x18D1, 0x4E42, 255, 255, 0) },
	{ USB_DEVICE_AND_INTERFACE_INFO(0x18D1, 0x2D01, 255, 255, 0) },
	{}
};

MODULE_DEVICE_TABLE(usb, beagleusb_id_table);

MODULE_AUTHOR("Azizul Hakim");
MODULE_DESCRIPTION("BeagleBone USB Driver For Keyboard, Mouse, Audio Playback");
MODULE_LICENSE("GPL");

struct usb_driver beagleusb_usb_driver = {
	.name = "beagleusb",
	.id_table = beagleusb_id_table,
	.probe = beagleusb_probe,
	.disconnect = beagleusb_disconnect,
};

module_usb_driver(beagleusb_usb_driver);
