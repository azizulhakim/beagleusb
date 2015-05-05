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

#include "datamanager.h"
#include "beagleusb.h"
#include "aoa.h"
#include "input.h"
//#include "video.h"

struct beagleusb* beagle_allocate_device(void){
	int i;
	struct beagleusb *beagleusb;

	beagleusb = kzalloc(sizeof(struct beagleusb), GFP_KERNEL);
	if (beagleusb == NULL)
		goto ERROR;

	#if AUDIO
	beagleusb->audio = kzalloc(sizeof(struct beagleaudio), GFP_KERNEL);
	if (beagleusb->audio == NULL)
		goto AUDIO_ALLOC_ERROR;
	#endif

	#if INPUT
	beagleusb->input = kzalloc(sizeof(struct beagleinput), GFP_KERNEL);
	if (beagleusb->input == NULL)
		goto INPUT_ALLOC_ERROR;
	#endif

	for(i=0; i<NUM_URBS; i++){
		beagleusb->outUrbs[i].buffer = kzalloc(DATA_PACKET_SIZE, GFP_KERNEL);
		usb_init_urb(&beagleusb->outUrbs[i].instance);
		
/*		usb_fill_bulk_urb(&beagleusb->outUrbs[i].instance, beagleusb->usbdev,
			  usb_sndbulkpipe(beagleusb->usbdev, beagleusb->bulk_out_endpointAddr), 
			  (void *)beagleusb->outUrbs[i].buffer,
			  DATA_PACKET_SIZE,
			  beagleaudio_audio_urb_received,
			  beagleusb);
*/
	}

	return beagleusb;

#if INPUT
INPUT_ALLOC_ERROR:
	kfree(beagleusb->audio);
#endif

#if AUDIO
AUDIO_ALLOC_ERROR:
	kfree(beagleusb);
#endif
	
ERROR:
	return NULL;
}

static int beagle_input_open(struct input_dev *dev)
{
	struct beagleusb *beagleusb = input_get_drvdata(dev);
	struct beagleinput* beagleinput = beagleusb->input;

	beagleinput->inputUrb->dev = beagleusb->usbdev;
	if (usb_submit_urb(beagleinput->inputUrb, GFP_KERNEL))
		return -EIO;

	return 0;
}

static void beagle_input_close(struct input_dev *dev)
{
	struct beagleusb *beagleusb = input_get_drvdata(dev);
	struct beagleinput* beagleinput = beagleusb->input;

	usb_kill_urb(beagleinput->inputUrb);
}

int init_input_device(struct beagleusb* beagleusb, struct usb_interface *iface){
	struct input_dev* input_dev;
	int error = -ENOMEM;
	int i;

	input_dev = input_allocate_device();

	if (input_dev == NULL)
		return -1;

	beagleusb->input->inputdev = input_dev;

	if (!(beagleusb->input->inputUrb = usb_alloc_urb(0, GFP_KERNEL)))
		return -1;
	if (!(beagleusb->input->new = usb_alloc_coherent(beagleusb->usbdev, 8, GFP_ATOMIC, &beagleusb->input->new_dma)))
		return -1;

	usb_make_path(beagleusb->usbdev, beagleusb->phys, sizeof(beagleusb->phys));
	strlcat(beagleusb->phys, "/input0", sizeof(beagleusb->phys));

	input_dev->name = beagleusb->name;
	input_dev->phys = beagleusb->phys;
	usb_to_input_id(beagleusb->usbdev, &input_dev->id);
	input_dev->dev.parent = &iface->dev;

	input_set_drvdata(input_dev, beagleusb);

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_LED) |
		BIT_MASK(EV_REP) | BIT_MASK(EV_REL);

	input_dev->keybit[BIT_WORD(BTN_MOUSE)] = BIT_MASK(BTN_LEFT) |
             BIT_MASK(BTN_RIGHT) | BIT_MASK(BTN_MIDDLE);

	input_dev->relbit[0] = BIT_MASK(REL_X) | BIT_MASK(REL_Y);

	input_dev->keybit[BIT_WORD(BTN_MOUSE)] |= BIT_MASK(BTN_SIDE) |
			BIT_MASK(BTN_EXTRA);

	input_dev->relbit[0] |= BIT_MASK(REL_WHEEL);

	for (i = 0; i < 255; i++)
		set_bit(usb_kbd_keycode[i], input_dev->keybit);
	clear_bit(0, input_dev->keybit);

	input_dev->open = beagle_input_open;
	input_dev->close = beagle_input_close;

	usb_fill_int_urb(beagleusb->input->inputUrb, 
					 beagleusb->usbdev, 
					 usb_rcvbulkpipe(beagleusb->usbdev, beagleusb->bulk_in_endpointAddr),
					 beagleusb->input->new,
					 8,
					 usb_inputurb_complete,
					 beagleusb, beagleusb->bInterval);

	beagleusb->input->inputUrb->transfer_dma = beagleusb->input->new_dma;
	beagleusb->input->inputUrb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

	error = input_register_device(beagleusb->input->inputdev);

	return 0;
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
	__u8   inputEndPointAddress = 0;
	__u8   outputEndPointAddress = 0;
	__u8   bInterval = 0;

	if (id->idVendor == 0x18D1 && (id->idProduct >= 0x2D00 && id->idProduct <= 0x2D05)){
		printk("BEAGLEDROID-USBAUDIO:  [%04X:%04X] Connected in AOA mode\n", id->idVendor, id->idProduct);


		printk("BEAGLEDUSB: intf.num_altsetting: %d\n", intf->num_altsetting);
		if (intf->altsetting != NULL)
			printk("BEAGLEUSB: intf->altsetting[0].des.bNumEndpoints : %d\n", intf->altsetting[0].desc.bNumEndpoints);

		interface_descriptor = intf->cur_altsetting;

		for(i=0; i<interface_descriptor->desc.bNumEndpoints; i++){
			endpoint = &interface_descriptor->endpoint[i].desc;

			if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT) && 
				(endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK
			   ){
					printk("BEAGLEUSB: Bulk out endpoint");
					outputEndPointAddress = endpoint->bEndpointAddress;
					//beagleusb->bulk_out_endpointAddr = endpoint->bEndpointAddress;
					break;
				}
		}

		for(i=0; i<interface_descriptor->desc.bNumEndpoints; i++){
			endpoint = &interface_descriptor->endpoint[i].desc;

			if (((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN) && 
				(endpoint->bmAttributes & USB_ENDPOINT_XFERTYPE_MASK) == USB_ENDPOINT_XFER_BULK
			   ){
					printk("BEAGLEUSB: Bulk in endpoint\n");
					inputEndPointAddress = endpoint->bEndpointAddress;
					bInterval = endpoint->bInterval;
					//beagleusb->bulk_in_endpointAddr = endpoint->bEndpointAddress;
					//beagleusb->bInterval = endpoint->bInterval;
					break;
				}
		}


		/* Device structure */
		beagleusb = beagle_allocate_device();
		if (beagleusb == NULL)
			return -ENOMEM;

		#if BUFFERING
		ringbuffer_init();
		//manager_init(beagleusb);
		#endif
		manager_init(beagleusb);

		beagleusb->dev = dev;
		beagleusb->usbdev = usb_get_dev(interface_to_usbdev(intf));
		beagleusb->bulk_in_endpointAddr = inputEndPointAddress;
		beagleusb->bulk_out_endpointAddr = outputEndPointAddress;


		#if INPUT
		init_input_device(beagleusb, intf);
		#endif


		usb_set_intfdata(intf, beagleusb);
		device_set_wakeup_enable(&beagleusb->usbdev->dev, 1);

		#if AUDIO
		printk("Audio Init start\n");
		ret = beagleaudio_audio_init(beagleusb);
		if (ret < 0)
			goto beagleaudio_audio_fail;
		printk("Audio Init end\n");
		#endif

		#if VIDEO
		printk("Video Init start\n");
		if (dlfb_video_init(beagleusb)){
			ret = -ENOMEM;
			goto beaglevideo_fail;
		}

		kref_get(&beagleusb->kref); /* matching kref_put in free_framebuffer_work */

		/* We don't register a new USB class. Our client interface is fbdev */

		/* Workitem keep things fast & simple during USB enumeration */
		INIT_DELAYED_WORK(&beagleusb->init_framebuffer_work,
				  dlfb_init_framebuffer_work);
		schedule_delayed_work(&beagleusb->init_framebuffer_work, 0);
		printk("Video Init end\n");
		#endif

		printk("BeagleBone USB Keyboard, Mouse, Audio Playback Driver\n");

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

#if AUDIO
beagleaudio_audio_fail:
	printk("beagleaudio_audio_fail\n");
	kfree(beagleusb->audio);
	kfree(beagleusb);
#endif

#if VIDEO
beaglevideo_fail:
	printk("beagule_video fail\n");
#endif

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

	#if AUDIO
	beagleaudio_audio_free(beagleusb);
	#endif

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
	{ USB_DEVICE_AND_INTERFACE_INFO(0x18D1, 0x2D00, 255, 255, 0) },
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
