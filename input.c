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

	if (mouse == NULL)
		goto error;

	control = (int)mouse->new[1];

	switch(control)
	{
		case MOUSEDOUBLECLICK:
			break;
		case MOUSESINGLECLICK:
			break;
		case MOUSELEFT:
			printk("Left\n");
			input_report_key(mouse->inputdev, BTN_LEFT,   0x01);
			input_report_key(mouse->inputdev, BTN_LEFT,   0x00);
			input_sync(mouse->inputdev);
			break;

		case MOUSERIGHT:
			printk("Right\n");
			input_report_key(mouse->inputdev, BTN_RIGHT,  0x01);
			input_sync(mouse->inputdev);
			input_report_key(mouse->inputdev, BTN_RIGHT,  0x00);
			input_sync(mouse->inputdev);
			break;
		case MOUSEMOVE:
			if (mouse->new[i+1] != 0 || mouse->new[i+3] != 0){
				printk("%d %d\n", (int)mouse->new[i+1], (int)mouse->new[i+3]);

				x = (int)mouse->new[i+1];
				y = (int)mouse->new[i+3];

				// x *= (int)(1366 / DISPLAYWIDTH);
				// y *= (int)(768 / DISPLAYHEIGHT);

				if (mouse->new[i] == 1) {
					x *= -1;
				}

				if (mouse->new[i+2]==1){
					y *= -1;
				}

				input_report_rel(mouse->inputdev, REL_X, x);
				input_report_rel(mouse->inputdev, REL_Y, y);
				input_sync(mouse->inputdev);
			}
			break;

		default:
			break;
	}

	return 0;

	error:
		return -1;
}

int handle_keyboard(struct beagleinput *kbd){
	int keyIndex;

	keyIndex = kbd->new[2];
	printk("keyIndex = %d\n", keyIndex);

	input_report_key(kbd->inputdev, usb_kbd_keycode[keyIndex], 0x01);
	input_report_key(kbd->inputdev, usb_kbd_keycode[keyIndex], 0x00);
	input_sync(kbd->inputdev);

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
	switch(filterId){
		case 64:
			handle_random_key(beagleusb->input);
			break;
		case KEYBOARDCONTROL:
			// handle keyboard
			handle_keyboard(beagleusb->input);
			break;
		case MOUSECONTROL:
			// handle mouse

			handle_mouse(beagleusb->input);
			break;

		case CONTROLMESSAGE:
			// handle contorl message
			handle_control(beagleusb->input);
			break;
		default:
			break;
	}

resubmit:
	i = usb_submit_urb (urb, GFP_ATOMIC);
	if (i)
		hid_err(urb->dev, "can't resubmit intr, %s-%s/input0, status %d",
			beagleusb->usbdev->bus->bus_name,
			beagleusb->usbdev->devpath, i);
}
