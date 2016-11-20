/*	Beaglebone USB Driver for Android Device Using AOA v2.0 Protocol
 *
 *   Copyright (C) 2014  Azizul Hakim
 *   azizulfahim2002@gmail.com
 *   
 *	 Special thanks to Federico Simoncelli for his Fushicai USBTV007 
 *   Audio-Video Grabber driver which helped me to gather knowledge 
 *   and develop this part driver.
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

#include <sound/core.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>
#include <sound/pcm_params.h>
#include <linux/module.h>
#include <linux/version.h>

#include "beagleusb.h"
#include "beagle-audio.h"

static struct snd_pcm_hardware snd_beagleaudio_digital_hw = {
  .info = (SNDRV_PCM_INFO_MMAP |
           SNDRV_PCM_INFO_INTERLEAVED |
           SNDRV_PCM_INFO_BLOCK_TRANSFER |
           SNDRV_PCM_INFO_MMAP_VALID),
  .formats =          SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8,
  .rates =            SNDRV_PCM_RATE_16000,
  .rate_min =         16000,
  .rate_max =         16000,
  .channels_min =     1,
  .channels_max =     1,
  .buffer_bytes_max = 32768,
  .period_bytes_min = 4096,
  .period_bytes_max = 32768,
  .periods_min =      1,
  .periods_max =      1024,
};

//int counter = 0;
int audio_running;

static int snd_beagleaudio_pcm_open(struct snd_pcm_substream *substream)
{
	struct beagleusb *beagleusb = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	frameRateController *= 3;

	beagleusb->audio->snd_substream = substream;
	runtime->hw = snd_beagleaudio_digital_hw;

	return 0;
}

static int snd_beagleaudio_pcm_close(struct snd_pcm_substream *substream)
{
	struct beagleusb *beagleusb = snd_pcm_substream_chip(substream);

	if (atomic_read(&beagleusb->audio->snd_stream)) {
		atomic_set(&beagleusb->audio->snd_stream, 0);
		schedule_work(&beagleusb->snd_trigger);
	}

	frameRateController /= 3;

	return 0;
}

static int snd_beagleaudio_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *hw_params)
{
	int rv;

	rv = snd_pcm_lib_malloc_pages(substream,
		params_buffer_bytes(hw_params));

	if (rv < 0) {
		printk("pcm audio buffer allocation failure\n");
		return rv;
	}

	return 0;
}

static int snd_beagleaudio_hw_free(struct snd_pcm_substream *substream)
{
	snd_pcm_lib_free_pages(substream);

	return 0;
}

static int snd_beagleaudio_prepare(struct snd_pcm_substream *substream)
{
	struct beagleusb *beagleusb = snd_pcm_substream_chip(substream);

	beagleusb->audio->snd_buffer_pos = 0;
	beagleusb->audio->snd_period_pos = 0;

	return 0;
}

static void beagleaudio_audio_urb_received(struct urb *urb)
{
	struct beagleusb *beagleusb = urb->context;
	struct snd_pcm_substream *substream = beagleusb->audio->snd_substream;
	struct snd_pcm_runtime *runtime = substream->runtime;
	int period_elapsed;
	unsigned int pcm_buffer_size;
	unsigned int len, ret;
	char *dataPointer;
	size_t frame_bytes, chunk_length;

	switch (urb->status) {
	case 0:
		break;
	case -ETIMEDOUT:
		printk("case ETIMEDOUT\n");
		return;
	case -ENOENT:
		printk("case ENOENT\n");
		return;
	case -EPROTO:
		printk("case EPROTO\n");
		return;
	case -ECONNRESET:
		printk("case ECONNRESET\n");
		return;
	case -ESHUTDOWN:
		printk("case ESHUTDOWN\n");
		return;
	default:
		printk("unknown audio urb status %i\n", urb->status);
	}

	if (audio_running){
		if (!atomic_read(&beagleusb->audio->snd_stream))
			return;

		snd_pcm_stream_lock(substream);

		pcm_buffer_size = snd_pcm_lib_buffer_bytes(substream);
		frame_bytes = runtime->frame_bits >> 2;
		chunk_length = PCM_DATA_SIZE / frame_bytes;
		if (beagleusb->audio->snd_buffer_pos + chunk_length <= pcm_buffer_size){
			memcpy(beagleusb->audio->snd_bulk_urb->transfer_buffer + PCM_HEADER_SIZE, runtime->dma_area + beagleusb->audio->snd_buffer_pos, PCM_DATA_SIZE);
		}
		else{
			len = pcm_buffer_size - beagleusb->audio->snd_buffer_pos;

			memcpy(beagleusb->audio->snd_bulk_urb->transfer_buffer + PCM_HEADER_SIZE, runtime->dma_area + beagleusb->audio->snd_buffer_pos, len);
			memcpy(beagleusb->audio->snd_bulk_urb->transfer_buffer + PCM_HEADER_SIZE + len, runtime->dma_area, PCM_DATA_SIZE - len);	
		}
		dataPointer = (char*)(beagleusb->audio->snd_bulk_urb->transfer_buffer);		// this is audio packet
		dataPointer[0] = (char)DATA_AUDIO;


		period_elapsed = 0;
		beagleusb->audio->snd_buffer_pos += chunk_length;
		if (beagleusb->audio->snd_buffer_pos >= pcm_buffer_size)
			beagleusb->audio->snd_buffer_pos -= pcm_buffer_size;

		beagleusb->audio->snd_period_pos += chunk_length;
		if (beagleusb->audio->snd_period_pos >= runtime->period_size) {
			beagleusb->audio->snd_period_pos %= runtime->period_size;
			period_elapsed = 1;
		}


		snd_pcm_stream_unlock(substream);

		if (period_elapsed)
			snd_pcm_period_elapsed(substream);

		ret = usb_submit_urb(urb, GFP_ATOMIC);
	}
}

static int beagleaudio_audio_start(struct beagleusb* beagleusb)
{
	unsigned int pipe = usb_sndbulkpipe(beagleusb->usbdev, beagleusb->bulk_out_endpointAddr);
	int ret;

	audio_running = 1;
	ret = usb_clear_halt(beagleusb->usbdev, pipe);
	ret = usb_submit_urb(beagleusb->audio->snd_bulk_urb, GFP_ATOMIC);

	return 0;
}

static int beagleaudio_audio_stop(struct beagleusb* beagleusb)
{
	audio_running = 0;
	/*if (beagleusb->audio->snd_bulk_urb) {
		usb_kill_urb(beagleusb->audio->snd_bulk_urb);
		kfree(beagleusb->audio->snd_bulk_urb->transfer_buffer);
		printk("kill urb = %p\n", beagleusb->audio->snd_bulk_urb);
		usb_free_urb(beagleusb->audio->snd_bulk_urb);
		beagleusb->audio->snd_bulk_urb = NULL;
	}*/

	return 0;
}

void beagleaudio_audio_suspend(struct beagleusb *beagleusb)
{
	if (atomic_read(&beagleusb->audio->snd_stream) && beagleusb->audio->snd_bulk_urb)
		usb_kill_urb(beagleusb->audio->snd_bulk_urb);
}

void beagleaudio_audio_resume(struct beagleusb *beagleusb)
{
	if (atomic_read(&beagleusb->audio->snd_stream) && beagleusb->audio->snd_bulk_urb){
		usb_submit_urb(beagleusb->audio->snd_bulk_urb, GFP_ATOMIC);
	}
}

static void snd_beagleaudio_trigger(struct work_struct *work)
{
	struct beagleusb *beagleusb = container_of(work, struct beagleusb, snd_trigger);

	if (atomic_read(&beagleusb->audio->snd_stream))
		beagleaudio_audio_start(beagleusb);
	else
		beagleaudio_audio_stop(beagleusb);
}

static int snd_beagleaudio_card_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct beagleusb *chip = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		atomic_set(&chip->audio->snd_stream, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		atomic_set(&chip->audio->snd_stream, 0);
		break;
	default:
		return -EINVAL;
	}

	schedule_work(&chip->snd_trigger);

	return 0;
}

static snd_pcm_uframes_t snd_beagleaudio_pointer(struct snd_pcm_substream *substream)
{
	struct beagleusb *beagleusb = snd_pcm_substream_chip(substream);

	return bytes_to_frames(substream->runtime, beagleusb->audio->snd_buffer_pos);
}

static struct snd_pcm_ops snd_beagleaudio_pcm_ops = {
	.open = snd_beagleaudio_pcm_open,
	.close = snd_beagleaudio_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_beagleaudio_hw_params,
	.hw_free = snd_beagleaudio_hw_free,
	.prepare = snd_beagleaudio_prepare,
	.trigger = snd_beagleaudio_card_trigger,
	.pointer = snd_beagleaudio_pointer,
};

int beagleaudio_audio_init(struct beagleusb *beagleusb)
{
	int rv;
	struct snd_card *card;
	struct snd_pcm *pcm;
	unsigned int pipe;


	INIT_WORK(&beagleusb->snd_trigger, snd_beagleaudio_trigger);
	atomic_set(&beagleusb->audio->snd_stream, 0);

	#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,16,0)
	rv = snd_card_new(beagleusb->dev, SNDRV_DEFAULT_IDX1, "beagleaudio", THIS_MODULE, 0, &card);
	#else
	rv = snd_card_create(SNDRV_DEFAULT_IDX1, "beagleaudio", THIS_MODULE, 0,&card);
	#endif

	if (rv < 0)
		return rv;

	strncpy(card->driver, beagleusb->dev->driver->name,
		sizeof(card->driver) - 1);
	strncpy(card->shortname, "beagleaudio", sizeof(card->shortname) - 1);
	snprintf(card->longname, sizeof(card->longname),
		"BEAGLEAUDIO at bus %d device %d", beagleusb->usbdev->bus->busnum,
		beagleusb->usbdev->devnum);

	snd_card_set_dev(card, beagleusb->dev);

	beagleusb->audio->snd = card;

	rv = snd_pcm_new(card, "BEAGLEAUDIO", 0, 1, 0, &pcm);
	if (rv < 0)
		goto err;

	strncpy(pcm->name, "BeagleAudio Input", sizeof(pcm->name) - 1);
	pcm->info_flags = 0;
	pcm->private_data = beagleusb;


	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_beagleaudio_pcm_ops);
	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL), BEAGLEAUDIO_AUDIO_BUFFER,
		BEAGLEAUDIO_AUDIO_BUFFER);


	rv = snd_card_register(card);
	if (rv)
		goto err;

	beagleusb->audio->snd_bulk_urb = usb_alloc_urb(0, GFP_KERNEL);
	if (beagleusb->audio->snd_bulk_urb == NULL)
		goto err;

	pipe = usb_sndbulkpipe(beagleusb->usbdev, beagleusb->bulk_out_endpointAddr); //beagleusb->audio->bulk_out_pipe;

	beagleusb->audio->snd_bulk_urb->transfer_buffer = kzalloc(
		DATA_PACKET_SIZE, GFP_KERNEL);

	if (beagleusb->audio->snd_bulk_urb->transfer_buffer == NULL)
		goto err_transfer_buffer;

	usb_fill_bulk_urb(beagleusb->audio->snd_bulk_urb, beagleusb->usbdev, pipe,
		beagleusb->audio->snd_bulk_urb->transfer_buffer, DATA_PACKET_SIZE,
		beagleaudio_audio_urb_received, beagleusb);

	return 0;

err_transfer_buffer:
	printk("kill urb = %p\n", beagleusb->audio->snd_bulk_urb);
	usb_free_urb(beagleusb->audio->snd_bulk_urb);
	beagleusb->audio->snd_bulk_urb = NULL;

err:
	beagleusb->audio->snd = NULL;
	snd_card_free(card);

	return rv;
}

void beagleaudio_audio_free(struct beagleusb *beagleusb)
{
	if (beagleusb->audio->snd && beagleusb->usbdev) {
		snd_card_free(beagleusb->audio->snd);
		beagleusb->audio->snd = NULL;
	}
}
