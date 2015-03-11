/*	Beaglebone USB Driver for Android Device Using AOA v2.0 Protocol
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


#include "aoa.h"

char* utf8(const char *str)
{
	char *utf8;
	utf8 = kmalloc(1+(2*strlen(str)), GFP_KERNEL);

	if (utf8) {
		char *c = utf8;
		for (; *str; ++str) {
			if (*str & 0x80) {
				*c++ = *str;
			} else {
				*c++ = (char) (0xc0 | (unsigned) *str >> 6);
				*c++ = (char) (0x80 | (*str & 0x3f));
			}
		}
		*c++ = '\0';
	}
	return utf8;
}

int GetProtocol(struct usb_device *usbdev, char *buffer){
	//Debug_Print("BEAGLEDROID-KBD", "Get Protocol: AOA version");
	
	return usb_control_msg(usbdev,
			usb_rcvctrlpipe(usbdev, 0),
			REQ_GET_PROTOCOL,
			USB_DIR_IN | USB_TYPE_VENDOR,
			VAL_AOA_REQ,
			0,
			buffer,
			sizeof(buffer),
			HZ*5);	
}

int SendIdentificationInfo(struct usb_device *usbdev, int id_index, char *id_info){
	int ret = usb_control_msg(usbdev,
				usb_sndctrlpipe(usbdev, 0),
				REQ_SEND_ID,
				USB_DIR_OUT | USB_TYPE_VENDOR,
				VAL_AOA_REQ,
				id_index,
				utf8((char*)(id_info)),
				sizeof(id_info) + 1,
				HZ*5);

	return ret;
}

int SendAOAActivationRequest(struct usb_device *usbdev){
//	Debug_Print("BEAGLEDROID-KBD", "REQ FOR AOA Mode Activation");

	return usb_control_msg(usbdev,
							usb_sndctrlpipe(usbdev, 0),
							REQ_AOA_ACTIVATE,
							USB_DIR_OUT | USB_TYPE_VENDOR,
							VAL_AOA_REQ,
							0,
							NULL,
							0,
							HZ*5);
}

int SendAudioActivationRequest(struct usb_device *usbdev){
	return usb_control_msg(usbdev,
							usb_sndctrlpipe(usbdev, 0),
							ACCESSORY_REGISTER_AUDIO,
							USB_DIR_OUT | USB_TYPE_VENDOR,
							VAL_AUDIO,
							0,
							NULL,
							0,
							HZ*5);
}

int SetConfiguration(struct usb_device *usbdev, char *buffer){
	//Debug_Print("BEAGLEDROID-KBD", "Get Protocol: AOA version");
	
	return usb_control_msg(usbdev,
			usb_rcvctrlpipe(usbdev, 0),
			0x9,
			USB_DIR_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
			1,
			0,
			NULL,
			0,
			HZ*5);	
}
