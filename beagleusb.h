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


#ifndef BEAGLEUSB_H
#define BEAGLEUSB_H 1

#include "beagle-audio.h"
#include "vid.h"

#define AUDIO 		0
#define INPUT		0
#define VIDEO		1
#define BUFFERING	0
#define VIDEO_URB	0

#define BEAGLEAUDIO_AUDIO_BUFFER	65536
#define PCM_HEADER_SIZE				4
#define PCM_DATA_SIZE	 			4096
#define DATA_PACKET_SIZE 			4100
#define DATA_AUDIO					1
#define DATA_VIDEO					2


#define NUM_URBS		8

struct pcm_urb {
	struct urb				instance;
	struct usb_anchor		submitted;
	u8*						buffer;
};

struct beagleinput{
	struct input_dev*		inputdev;
	unsigned char 			old[8];
	struct urb*				inputUrb;
	char 					name[128];
	char 					phys[64];

	unsigned char*			new;
	dma_addr_t 				new_dma;
};

struct beagleaudio {
	struct		snd_card *snd;
	struct		snd_pcm_substream *snd_substream;
	atomic_t	snd_stream;
	struct		urb *snd_bulk_urb;
	size_t		snd_buffer_pos;
	size_t		snd_period_pos;
};

struct beagleusb {
	struct 					device *dev;
	struct 					usb_device *usbdev;
	__u8					bulk_in_endpointAddr;
	__u8					bulk_out_endpointAddr;
	__u8					bInterval;
	size_t					bulk_in_size;
	char 					name[128];
	char 					phys[64];

	struct pcm_urb			outUrbs[NUM_URBS];

	struct kref				kref;

	/* audio */
	struct beagleaudio* 	audio;
	struct 					work_struct snd_trigger;

	/* video */
	struct beaglevideo video;
	struct delayed_work init_framebuffer_work;
	struct delayed_work free_framebuffer_work;

	/* input */
	struct beagleinput* 	input;
};

/* audio functions */
int beagleaudio_audio_init(struct beagleusb *beagleusb);
void beagleaudio_audio_free(struct beagleusb *beagleusb);
void beagleaudio_audio_suspend(struct beagleusb *beagleusb);
void beagleaudio_audio_resume(struct beagleusb *beagleusb);

/* input functions */
int handle_mouse(struct beagleinput* mouse);
int handle_keyboard(struct beagleinput* kbd);
int handle_control(struct beagleinput* input);
int handle_random_key(struct beagleinput* kbd);
void usb_inputurb_complete(struct urb *urb);
#endif
