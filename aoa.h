/*	Beaglebone USB Driver for Android Device Using AOA v2.0 Protocol
 *
 *   Copyright (C) 2014  Azizul Hakim
 *   azizulfahim2002@gmail.com
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



#ifndef AOA_H
#define AOA_H	1



#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/usb/input.h>
#include <linux/hid.h>



#define DRIVER_VERSION "1.0"
#define DRIVER_AUTHOR "Azizul Hakim <azizulfahim2002@gmail.com>"
#define DRIVER_DESC "USB Audio Driver for Android Device with AOA Protocol"
#define DRIVER_LICENSE "GPL"

#define REQ_GET_PROTOCOL				51
#define REQ_SEND_ID						52
#define REQ_AOA_ACTIVATE				53
#define ACCESSORY_REGISTER_HID			54
#define ACCESSORY_UNREGISTER_HID		55
#define ACCESSORY_SET_HID_REPORT_DESC	56
#define ACCESSORY_SEND_HID_EVENT		57
#define ACCESSORY_REGISTER_AUDIO		58

#define MANU	"BeagleBone"
#define MODEL	"BeagleBone Black"
#define DESCRIPTION	"Development platform"
#define VERSION	"1.0"
#define URI		"http://beagleboard.org/"
#define SERIAL	"42"

#define ID_MANU		0
#define ID_MODEL	1
#define ID_DESC		2
#define ID_VERSION	3
#define ID_URI		4
#define ID_SERIAL	5

#define VAL_AOA_REQ		0
#define VAL_HID_MOUSE	1234
#define VAL_NO_AUDIO	0
#define VAL_AUDIO		1


int GetProtocol(struct usb_device *usbdev, char *buffer);
int SendIdentificationInfo(struct usb_device *usbdev, int id_index, char *id_info);
int SendAOAActivationRequest(struct usb_device *usbdev);
int SendAudioActivationRequest(struct usb_device *usbdev);
int SetConfiguration(struct usb_device *usbdev, char *buffer);
#endif
