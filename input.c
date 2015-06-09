/*	 Beaglebone USB Driver for Android Device Using AOA v2.0 Protocol
 *
 *   Copyright (C) 2014  Azizul Hakim
 *
 *   Special thanks to Vojtech Pavlik, the writer of linux USB Keyboard
 *   and mouse driver whose work helped me to accomodate support for beaglebone 
 *   to be compatible with AOA supported android devices.
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


#include <linux/usb/input.h>
#include <linux/hid.h>

#include "beagleusb.h"
#include "inputcontrol.h"
#include "input.h"

int handle_mouse(struct beagleinput* mouse){
	int i = 2;
	int x,y;
	int control = 0;
	signed char *data = mouse->new;
	int	btn_left = data[MOUSE_BTN_INDEX] & (char)MOUSE_BTN_LEFT;
	int btn_right = data[MOUSE_BTN_INDEX] & (char)MOUSE_BTN_RIGHT;

	if (mouse == NULL)
		goto error;

	input_report_key(mouse->inputdev, BTN_LEFT,   btn_left);
	input_report_key(mouse->inputdev, BTN_RIGHT,   btn_right);

	x = (int)data[REL_X_INDEX];
	y = (int)data[REL_Y_INDEX];

	input_report_rel(mouse->inputdev, REL_X, x);
	input_report_rel(mouse->inputdev, REL_Y, y);



	return 0;

	error:
		return -1;
}

int start_metakey(struct beagleinput *kbd){

	input_report_key(kbd->inputdev, 42, kbd->new[METAKEY_INDEX] & SHIFT);		// SHIFT
	
	return 0;
}

int stop_metakey(struct beagleinput *kbd){
	input_report_key(kbd->inputdev, 42, 0);		// SHIFT

	return 0;
}

int handle_keyboard(struct beagleinput *kbd){
	int keyIndex;

	keyIndex = kbd->new[KEY_INDEX];
	printk("keyIndex = %d\n", keyIndex);

	input_report_key(kbd->inputdev, usb_kbd_keycode[keyIndex], 0x01);
	input_report_key(kbd->inputdev, usb_kbd_keycode[keyIndex], 0x00);

	return 0;
}

int handle_control(struct beagleinput* input){
	int controlType;
	int dataPointer;
	int i;

	controlType = input->new[1];

	for (i=0; i<8; i++)
		printk("CONTROL: %d  ", (int)input->new[i]);

	switch (controlType){
		case CNTRL_RESOLUTION:
			dataPointer = (int)((input->new[2] << 8) | input->new[3]);
			//DISPLAYWIDTH = (int)((input->new[4] << 8) | input->new[5]);

			//printk("height = %d width = %d\n", DISPLAYHEIGHT, DISPLAYWIDTH);

			break;
		
		case CNTRL_DROPFRAME:
			dataPointer = (int)(input->new[2]);
			if (dataPointer <= 0) dropFrameRatio = 1;
			else dropFrameRatio = dataPointer;
			
		default:
			break;
	}

	return 0;
}

int handle_random_key(struct beagleinput* kbd){
	int i = 0;

	for (i=57; i<=57; i++){
		printk("index = %d\n", i);
		input_report_key(kbd->inputdev, usb_kbd_keycode[i], 0x01);
		input_report_key(kbd->inputdev, usb_kbd_keycode[i], 0x00);
		printk("\n");
		input_sync(kbd->inputdev);
	}

	return 0;
}

void usb_inputurb_complete(struct urb *urb)
{
	struct beagleusb* beagleusb = urb->context;
	struct beagleinput* input = beagleusb->input;
	signed char *data = input->new;
	int filterId;
	int i;

	switch (urb->status) {
	case 0:			/* success */
		break;
	case -ECONNRESET:	/* unlink */
	case -ENOENT:
	case -ESHUTDOWN:
		return;
	/* -EPIPE:  should clear the halt */
	default:		/* error */
		goto resubmit;
	}

	filterId = beagleusb->input->new[0];
	printk("filterId = %d\n", filterId);

	if (filterId != CONTROLMESSAGE){
		start_metakey(input);
		handle_keyboard(input);
		handle_mouse(input);
		stop_metakey(input);
		input_sync(input->inputdev);
	}
	else{
		handle_control(beagleusb->input);
	}

resubmit:
	i = usb_submit_urb (urb, GFP_ATOMIC);
	if (i)
		hid_err(urb->dev, "can't resubmit intr, %s-%s/input0, status %d",
			beagleusb->usbdev->bus->bus_name,
			beagleusb->usbdev->devpath, i);
}
