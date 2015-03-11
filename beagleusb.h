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

#define BEAGLEAUDIO_AUDIO_BUFFER	65536
#define PCM_PACKET_SIZE 			4096


struct beagleaudio {
	struct snd_card *snd;
	struct snd_pcm_substream *snd_substream;
	atomic_t snd_stream;
	struct urb *snd_bulk_urb;
	size_t snd_buffer_pos;
	size_t snd_period_pos;
};

struct beagleusb {
	struct device *dev;
	struct usb_device *usbdev;
	struct work_struct snd_trigger;
	__u8	bulk_in_endpointAddr;
	__u8	bulk_out_endpointAddr;

	/* audio */
	struct beagleaudio* audio;
};

int beagleaudio_audio_init(struct beagleusb *beagleusb);
void beagleaudio_audio_free(struct beagleusb *beagleusb);
void beagleaudio_audio_suspend(struct beagleusb *beagleusb);
void beagleaudio_audio_resume(struct beagleusb *beagleusb);


#endif
