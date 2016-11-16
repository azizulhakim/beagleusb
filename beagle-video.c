/*
 * udlfb.c -- Framebuffer driver for DisplayLink USB controller
 *
 * Copyright (C) 2009 Roberto De Ioris <roberto@unbit.it>
 * Copyright (C) 2009 Jaya Kumar <jayakumar.lkml@gmail.com>
 * Copyright (C) 2009 Bernie Thompson <bernie@plugable.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License v2. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Layout is based on skeletonfb by James Simmons and Geert Uytterhoeven,
 * usb-skeleton by GregKH.
 *
 * Device-specific portions based on information from Displaylink, with work
 * from Florian Echtler, Henrik Bjerregaard Pedersen, and others.
 */

#define pr_fmt(fmt) "udlfb: " fmt

#include <linux/kthread.h> 
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/usb.h>
#include <linux/uaccess.h>
#include <linux/mm.h>
#include <linux/fb.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/prefetch.h>
#include <linux/delay.h>
#include <linux/version.h> /* many users build as module against old kernels*/

#include "datamanager.h"
#include "ringbuffer.h"
#include "beagleusb.h"
#include "beagle-video.h"

// A temp var just to see how many times hline_render is being called.
int vline_count = 0;


static struct fb_fix_screeninfo dlfb_fix = {
	.id =           "udlfb",
	.type =         FB_TYPE_PACKED_PIXELS,
	.visual =       FB_VISUAL_TRUECOLOR,
	.xpanstep =     0,
	.ypanstep =     0,
	.ywrapstep =    0,
	.accel =        FB_ACCEL_NONE,
};

static const u32 udlfb_info_flags = FBINFO_DEFAULT | FBINFO_READS_FAST |
#ifdef FBINFO_VIRTFB
		FBINFO_VIRTFB |
#endif
		FBINFO_HWACCEL_IMAGEBLIT | FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_COPYAREA | FBINFO_MISC_ALWAYS_SETPAR;


/* module options */
static bool console = 1; /* Allow fbcon to open framebuffer */
static bool fb_defio = 1;  /* Detect mmap writes using page faults */
static bool shadow = 1; /* Optionally disable shadow framebuffer */
static int pixel_limit; /* Optionally force a pixel resolution limit */

/*
 * When building as a separate module against an arbitrary kernel,
 * check on build presence of other kernel modules we have dependencies on.
 * In some cases we can't build at all without the dependency.
 * For others, we can build without them, but lose functionality.
 * When rebuilding entire kernel, our Kconfig should pull in everything we need.
 */

#ifndef CONFIG_FB_DEFERRED_IO
#warning CONFIG_FB_DEFERRED_IO kernel support required for fb_defio mmap support
#endif

#ifndef CONFIG_FB_SYS_IMAGEBLIT
#warning CONFIG_FB_SYS_IMAGEBLIT kernel support required for fb console
#endif

#ifndef CONFIG_FB_SYS_FOPS
#warning FB_SYS_FOPS kernel support required for filesystem char dev access
#endif

#ifndef CONFIG_FB_MODE_HELPERS
#warning CONFIG_FB_MODE_HELPERS required. Expect build break
#endif

/* dlfb keeps a list of urbs for efficient bulk transfers */
static void dlfb_urb_completion(struct urb *urb);
static struct urb *dlfb_get_urb(struct beagleusb *dev);
static int dlfb_submit_urb(struct beagleusb *dev, struct urb * urb, size_t len);
static int dlfb_alloc_urb_list(struct beagleusb *dev, int count, size_t size);
static void dlfb_free_urb_list(struct beagleusb *dev);

#if LAZZY_MODE
unsigned int lazzy_tracker[384][2];
int lazzy_run = 1;

struct task_struct* lazzy_thread;

void lazzy_update(void* data){
	struct beagleusb* beagleusb = (struct beagleusb*)data;
	struct urb* urb;
	int i = 0;

	while(lazzy_run){
		for (i=0; lazzy_run && i<384; i++){
			if (lazzy_tracker[i][0]){
				u8 *line_start = (u8 *)(beagleusb->video.info->fix.smem_start + lazzy_tracker[i][1]);
								
				lazzy_tracker[i][0] = 0;

				urb = dlfb_get_urb(beagleusb);
				if(urb != NULL && urb->transfer_buffer){
					*((u8*)urb->transfer_buffer) = (char)DATA_VIDEO;			// this is video data
					*((u8*)urb->transfer_buffer+1) = i;				// two byte page index
					*((u8*)urb->transfer_buffer+1+1) = i >> 8;
					memcpy(urb->transfer_buffer + PCM_HEADER_SIZE, line_start, PCM_DATA_SIZE);
					dlfb_submit_urb(beagleusb, urb, DATA_PACKET_SIZE);
				}
			}
		}
		msleep(frameRateController);
	}
}
#endif

/* Function added by me to fix make errors */
/*static void err (char *msg){
	printk(msg);
}*/


static int dlfb_ops_mmap(struct fb_info *info, struct vm_area_struct *vma)
{
	unsigned long start = vma->vm_start;
	unsigned long size = vma->vm_end - vma->vm_start;
	unsigned long offset = vma->vm_pgoff << PAGE_SHIFT;
	unsigned long page, pos;
	
	printk("dlfb_ops_mmap called\n");

	if (offset + size > info->fix.smem_len)
		return -EINVAL;

	pos = (unsigned long)info->fix.smem_start + offset;

	pr_notice("mmap() framebuffer addr:%lu size:%lu\n",
		  pos, size);

	while (size > 0) {
		page = vmalloc_to_pfn((void *)pos);
		if (remap_pfn_range(vma, start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;

		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		if (size > PAGE_SIZE)
			size -= PAGE_SIZE;
		else
			size = 0;
	}

	vma->vm_flags |= VM_RESERVED;	/* avoid to swap out this VMA */
	return 0;
}

/*
 * There are 3 copies of every pixel: The front buffer that the fbdev
 * client renders to, the actual framebuffer across the USB bus in hardware
 * (that we can only write to, slowly, and can never read), and (optionally)
 * our shadow copy that tracks what's been sent to that hardware buffer.
 */
static int dlfb_render_hline(struct beagleusb *dev, struct urb **urb_ptr,
			      const char *front, char **urb_buf_ptr,
			      u32 byte_offset, u32 byte_width,
			      int *ident_ptr, int *sent_ptr)
{				  
	const u8 *line_start;
	struct urb* urb;
	u16 page_index = byte_offset/4096;

	line_start = (u8 *) (front + byte_offset);


	urb = dlfb_get_urb(dev);
	if(urb->transfer_buffer){
		// Save page index
		*((u8*)urb->transfer_buffer) = (char)DATA_VIDEO;			// this is video data
		*((u8*)urb->transfer_buffer+1) = page_index;				// two byte page index
		*((u8*)urb->transfer_buffer+1+1) = page_index >> 8;
	}

	memcpy(urb->transfer_buffer + PCM_HEADER_SIZE, line_start, byte_width);

	vline_count++;
	
	// identical pixels value to zero.
	ident_ptr += 0;

	dlfb_submit_urb(dev, urb, DATA_PACKET_SIZE);

	return 0;
}

int dlfb_handle_damage(struct beagleusb *dev, int x, int y,
	       int width, int height, char *data)
{
	int i;
	char *cmd;
	cycles_t start_cycles, end_cycles;
	int bytes_sent = 0;
	int bytes_identical = 0;
	struct urb *urb;
	int aligned_x;
	
	printk("dlfb_handle_damage called\n");
	
	printk("handle damage x: %d, y:%d, width:%d, height:%d\n", x, y, width, height);
	
	/* -TODO- 
	 * -Remove this BB specific issue code
	 * Exit if width and height are something else.
	 */
	if((width - x) != XRES || (height - y) != YRES){
		printk("Dim. mismatch. Not sending\n");
		return 0; 
	}

	start_cycles = get_cycles();

	aligned_x = DL_ALIGN_DOWN(x, sizeof(unsigned long));
	width = DL_ALIGN_UP(width + (x-aligned_x), sizeof(unsigned long));
	x = aligned_x;

	if ((width <= 0) ||
	    (x + width > dev->video.info->var.xres) ||
	    (y + height > dev->video.info->var.yres))
		return -EINVAL;

	if (!atomic_read(&dev->video.usb_active))
		return 0;

	/* Modified: 
	 * From 1 line per transfer to 2 hlines per transfer
	 * Now all USB transfers would be of same length - 4096
	 */
	for (i = y; i < y + height ; i++) {
		const int line_offset = dev->video.info->fix.line_length * i;
		const int byte_offset = line_offset + (x * BPP);

		if (dlfb_render_hline(dev, &urb,
				      (char *) dev->video.info->fix.smem_start,
				      &cmd, byte_offset, width * BPP * 2,
				      &bytes_identical, &bytes_sent))
			goto error;
		i++;
	}

error:
	atomic_add(bytes_sent, &dev->video.bytes_sent);
	atomic_add(bytes_identical, &dev->video.bytes_identical);
	atomic_add(width*height*2, &dev->video.bytes_rendered);
	end_cycles = get_cycles();
	atomic_add(((unsigned int) ((end_cycles - start_cycles)
		    >> 10)), /* Kcycles */
		   &dev->video.cpu_kcycles_used);

	return 0;
}

static ssize_t dlfb_ops_read(struct fb_info *info, char __user *buf,
			 size_t count, loff_t *ppos)
{
	ssize_t result = -ENOSYS;
	
	printk("dlfb_ops_read called\n");

#if defined CONFIG_FB_SYS_FOPS
	result = fb_sys_read(info, buf, count, ppos);
#endif

	return result;
}

/*
 * Path triggered by usermode clients who write to filesystem
 * e.g. cat filename > /dev/fb1
 * Not used by X Windows or text-mode console. But useful for testing.
 * Slow because of extra copy and we must assume all pixels dirty.
 */
static ssize_t dlfb_ops_write(struct fb_info *info, const char __user *buf,
			  size_t count, loff_t *ppos)
{
	ssize_t result = -ENOSYS;
	
	printk("dlfb_ops_write called\n");

#if defined CONFIG_FB_SYS_FOPS

	struct beagleusb *dev = info->par;
	u32 offset = (u32) *ppos;

	result = fb_sys_write(info, buf, count, ppos);

	if (result > 0) {
		int start = max((int)(offset / info->fix.line_length) - 1, 0);
		int lines = min((u32)((result / info->fix.line_length) + 1),
				(u32)info->var.yres);

		dlfb_handle_damage(dev, 0, start, info->var.xres,
			lines, info->screen_base);
	}

#endif
	return result;
}

/* hardware has native COPY command (see libdlo), but not worth it for fbcon */
static void dlfb_ops_copyarea(struct fb_info *info,
				const struct fb_copyarea *area)
{
	printk("dlfb_ops_copyarea called\n");
	
#if defined CONFIG_FB_SYS_COPYAREA

	struct beagleusb *dev = info->par;

	sys_copyarea(info, area);

	dlfb_handle_damage(dev, area->dx, area->dy,
			area->width, area->height, info->screen_base);
#endif

}

static void dlfb_ops_imageblit(struct fb_info *info,
				const struct fb_image *image)
{
	printk("dlfb_ops_imageblit called\n");
	
#if defined CONFIG_FB_SYS_IMAGEBLIT

	struct beagleusb *dev = info->par;

	sys_imageblit(info, image);

	dlfb_handle_damage(dev, image->dx, image->dy,
			image->width, image->height, info->screen_base);

#endif

}

static void dlfb_ops_fillrect(struct fb_info *info,
			  const struct fb_fillrect *rect)
{
	printk("dlfb_ops_fillrect called\n");
	
#if defined CONFIG_FB_SYS_FILLRECT

	struct beagleusb *dev = info->par;

	sys_fillrect(info, rect);

	dlfb_handle_damage(dev, rect->dx, rect->dy, rect->width,
			      rect->height, info->screen_base);
#endif

}

#ifdef CONFIG_FB_DEFERRED_IO
/*
 * NOTE: fb_defio.c is holding info->fbdefio.mutex
 *   Touching ANY framebuffer memory that triggers a page fault
 *   in fb_defio will cause a deadlock, when it also tries to
 *   grab the same mutex.
 */
int count = 0;
static void dlfb_dpy_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	struct page *cur;
	struct fb_deferred_io *fbdefio = info->fbdefio;
	struct beagleusb *dev = info->par;
	cycles_t start_cycles;

	if (!fb_defio)
		return;

	if (!atomic_read(&dev->video.usb_active))
		return;

	start_cycles = get_cycles();


	/* walk the written page list and render each to device */
#if VAR_RES
	struct urb *urb = NULL;

	if (count == 0){
		int unsubmitted_urb = 0;
		u32 byte_offset = 0;
		u32 start_offset = 0;
		u32 end_offset = 0;
		char *transfer_buffer = NULL;
		u32 remaining_buffer = 0;

		list_for_each_entry(cur, &fbdefio->pagelist, lru) {
			start_offset = cur->index << PAGE_SHIFT;
			end_offset = start_offset + PAGE_SIZE;
			byte_offset = start_offset;

			while (byte_offset < end_offset){
				u16 page_index = byte_offset / (2 * XRES * BPP);
				if (unsubmitted_urb == 0){
					urb = dlfb_get_urb(dev);
					transfer_buffer = (char*)urb->transfer_buffer;
					*(transfer_buffer) = (char)DATA_VIDEO;			// this is video data
					*(transfer_buffer+1) = page_index;				// two byte page index
					*(transfer_buffer+1+1) = page_index >> 8;

					transfer_buffer += PCM_HEADER_SIZE;
					remaining_buffer = 2 * XRES * BPP;
				}

				if (byte_offset + remaining_buffer < end_offset){
					memcpy(transfer_buffer, (char *) info->fix.smem_start + byte_offset, remaining_buffer);
					dlfb_submit_urb(dev, urb, 4 + 2 * XRES * BPP);
					unsubmitted_urb = 0;
					byte_offset += remaining_buffer;
					remaining_buffer = 2 * XRES * BPP;
				}
				else{
					memcpy(transfer_buffer, (char *) info->fix.smem_start + byte_offset, end_offset - byte_offset);
					transfer_buffer += (end_offset - byte_offset);
					remaining_buffer = 2 * XRES * BPP - (end_offset - byte_offset);
					byte_offset += (end_offset - byte_offset);
					unsubmitted_urb = 1;
				}
			}
		}
		if (unsubmitted_urb == 1){
			dlfb_urb_completion(urb);
		}
	}
	if (dropFrameRatio != 0)
		count = (count + 1) % dropFrameRatio;
#elif LAZZY_MODE
	list_for_each_entry(cur, &fbdefio->pagelist, lru) {
		if (cur->index < 384){
			lazzy_tracker[cur->index][0] = 1;
			lazzy_tracker[cur->index][1] = cur->index << PAGE_SHIFT;
		}
	}
#else
	if (count == 0){
		list_for_each_entry(cur, &fbdefio->pagelist, lru) {

			if (dlfb_render_hline(dev, &urb, (char *) info->fix.smem_start,
					  &cmd, cur->index << PAGE_SHIFT,
					  PAGE_SIZE, &bytes_identical, &bytes_sent))
				goto error;
			bytes_rendered += PAGE_SIZE;
		}
	}
	if (dropFrameRatio != 0)
		count = (count + 1) % dropFrameRatio;

	error:
		atomic_add(bytes_sent, &dev->video.bytes_sent);
		atomic_add(bytes_identical, &dev->video.bytes_identical);
		atomic_add(bytes_rendered, &dev->video.bytes_rendered);
		end_cycles = get_cycles();
		atomic_add(((unsigned int) ((end_cycles - start_cycles)
				>> 10)), /* Kcycles */
			   &dev->video.cpu_kcycles_used);

#endif
}

#endif

static int dlfb_ops_ioctl(struct fb_info *info, unsigned int cmd,
				unsigned long arg)
{

	struct beagleusb *dev = info->par;
	
	printk("dlfb_ops_ioctl called\n");

	if (!atomic_read(&dev->video.usb_active))
		return 0;

	/* TODO: Update X server to get this from sysfs instead */
	if (cmd == DLFB_IOCTL_RETURN_EDID) {
		void __user *edid = (void __user *)arg;
		if (copy_to_user(edid, dev->video.edid, dev->video.edid_size))
			return -EFAULT;
		return 0;
	}

	/* TODO: Help propose a standard fb.h ioctl to report mmap damage */
	if (cmd == DLFB_IOCTL_REPORT_DAMAGE) {
		struct dloarea area;

		if (copy_from_user(&area, (void __user *)arg,
				  sizeof(struct dloarea)))
			return -EFAULT;

		/*
		 * If we have a damage-aware client, turn fb_defio "off"
		 * To avoid perf imact of unnecessary page fault handling.
		 * Done by resetting the delay for this fb_info to a very
		 * long period. Pages will become writable and stay that way.
		 * Reset to normal value when all clients have closed this fb.
		 */
#ifdef CONFIG_FB_DEFERRED_IO
		if (info->fbdefio)
			info->fbdefio->delay = DL_DEFIO_WRITE_DISABLE;
#endif
		if (area.x < 0)
			area.x = 0;

		if (area.x > info->var.xres)
			area.x = info->var.xres;

		if (area.y < 0)
			area.y = 0;

		if (area.y > info->var.yres)
			area.y = info->var.yres;

		dlfb_handle_damage(dev, area.x, area.y, area.w, area.h,
			   info->screen_base);
	}

	return 0;
}

/* taken from vesafb */
static int
dlfb_ops_setcolreg(unsigned regno, unsigned red, unsigned green,
	       unsigned blue, unsigned transp, struct fb_info *info)
{
	int err = 0;

	//printk("dlfb_ops_setcolreg called\n");	
	
	if (regno >= info->cmap.len)
		return 1;

	if (regno < 16) {
		if (info->var.red.offset == 10) {
			/* 1:5:5:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800) >> 1) |
			    ((green & 0xf800) >> 6) | ((blue & 0xf800) >> 11);
		} else {
			/* 0:5:6:5 */
			((u32 *) (info->pseudo_palette))[regno] =
			    ((red & 0xf800)) |
			    ((green & 0xfc00) >> 5) | ((blue & 0xf800) >> 11);
		}
	}

	return err;
}

/*
 * It's common for several clients to have framebuffer open simultaneously.
 * e.g. both fbcon and X. Makes things interesting.
 * Assumes caller is holding info->lock (for open and release at least)
 */
static int dlfb_ops_open(struct fb_info *info, int user)
{
	struct beagleusb *dev = info->par;
	
	printk("dlfb_ops_open called\n");

	/*
	 * fbcon aggressively connects to first framebuffer it finds,
	 * preventing other clients (X) from working properly. Usually
	 * not what the user wants. Fail by default with option to enable.
	 */
	if ((user == 0) && (!console))
		return -EBUSY;

	/* If the USB device is gone, we don't accept new opens */
	if (dev->video.virtualized)
		return -ENODEV;

	dev->video.fb_count++;

	kref_get(&dev->kref);

#ifdef CONFIG_FB_DEFERRED_IO
	if (fb_defio && (info->fbdefio == NULL)) {
		/* enable defio at last moment if not disabled by client */

		struct fb_deferred_io *fbdefio;

		fbdefio = kmalloc(sizeof(struct fb_deferred_io), GFP_KERNEL);

		if (fbdefio) {
			fbdefio->delay = DL_DEFIO_WRITE_DELAY;
			fbdefio->deferred_io = dlfb_dpy_deferred_io;
		}

		info->fbdefio = fbdefio;
		fb_deferred_io_init(info);
	}
#endif

	pr_notice("open /dev/fb%d user=%d fb_info=%p count=%d\n",
	    info->node, user, info, dev->video.fb_count);

	return 0;
}

/*
 * Called when all client interfaces to start transactions have been disabled,
 * and all references to our device instance (beagleusb) are released.
 * Every transaction must have a reference, so we know are fully spun down
 */
static void dlfb_free(struct kref *kref)
{
	struct beagleusb *dev = container_of(kref, struct beagleusb, kref);
	
	printk("dlfb_free called\n");

	if (dev->video.backing_buffer)
		vfree(dev->video.backing_buffer);

	kfree(dev->video.edid);

	pr_warn("freeing beagleusb %p\n", dev);

	kfree(dev);
}

static void dlfb_release_urb_work(struct work_struct *work)
{
	struct urb_node *unode = container_of(work, struct urb_node,
					      release_urb_work.work);
					      
	//printk("dlfb_release_urb_work called\n");

	up(&unode->dev->video.urbs.limit_sem);
}

static void dlfb_free_framebuffer(struct beagleusb *dev)
{
	struct fb_info *info = dev->video.info;
	
	printk("dlfb_free_framebuffer called\n");


	if (info) {
		int node = info->node;

		unregister_framebuffer(info);

		if (info->cmap.len != 0)
			fb_dealloc_cmap(&info->cmap);
		if (info->monspecs.modedb)
			fb_destroy_modedb(info->monspecs.modedb);
		if (info->screen_base)
			vfree(info->screen_base);

		fb_destroy_modelist(&info->modelist);

		dev->video.info = NULL;

		/* Assume info structure is freed after this point */
		framebuffer_release(info);

		pr_warn("fb_info for /dev/fb%d has been freed\n", node);
	}

	/* ref taken in probe() as part of registering framebfufer */
	kref_put(&dev->kref, dlfb_free);
}

static void dlfb_free_framebuffer_work(struct work_struct *work)
{
	struct beagleusb *dev = container_of(work, struct beagleusb,
					     free_framebuffer_work.work);
					     
	printk("dlfb_free_framebuffer_work called\n");
	
	dlfb_free_framebuffer(dev);
}
/*
 * Assumes caller is holding info->lock mutex (for open and release at least)  */
static int dlfb_ops_release(struct fb_info *info, int user)
{
	struct beagleusb *dev = info->par;

	printk("dlfb_ops_release called\n");	
	
	dev->video.fb_count--;

	/* We can't free fb_info here - fbmem will touch it when we return */
	if (dev->video.virtualized && (dev->video.fb_count == 0))
		schedule_delayed_work(&dev->free_framebuffer_work, HZ);

#ifdef CONFIG_FB_DEFERRED_IO
	if ((dev->video.fb_count == 0) && (info->fbdefio)) {
		fb_deferred_io_cleanup(info);
		kfree(info->fbdefio);
		info->fbdefio = NULL;
		info->fbops->fb_mmap = dlfb_ops_mmap;
	}
#endif

	pr_warn("released /dev/fb%d user=%d count=%d\n",
		  info->node, user, dev->video.fb_count);

	kref_put(&dev->kref, dlfb_free);

	return 0;
}

/*
 * Check whether a video mode is supported by the DisplayLink chip
 * We start from monitor's modes, so don't need to filter that here
 */
static int dlfb_is_valid_mode(struct fb_videomode *mode,
		struct fb_info *info)
{
	struct beagleusb *dev = info->par;
	
	printk("dlfb_is_valid_mode called\n");

	if (mode->xres * mode->yres > dev->video.sku_pixel_limit) {
		pr_warn("%dx%d beyond chip capabilities\n",
		       mode->xres, mode->yres);
		return 0;
	}

	pr_info("%dx%d @ %d Hz valid mode\n", mode->xres, mode->yres,
		mode->refresh);

	return 1;
}

static void dlfb_var_color_format(struct fb_var_screeninfo *var)
{
	const struct fb_bitfield red = { 11, 5, 0 };
	const struct fb_bitfield green = { 5, 6, 0 };
	const struct fb_bitfield blue = { 0, 5, 0 };
	
	printk("dlfb_var_color_format called\n");

	var->bits_per_pixel = 16;
	var->red = red;
	var->green = green;
	var->blue = blue;
}

static int dlfb_ops_check_var(struct fb_var_screeninfo *var,
				struct fb_info *info)
{
	struct fb_videomode mode;
	
	printk("dlfb_ops_check_var called\n");

	/* TODO: support dynamically changing framebuffer size */
	if ((var->xres * var->yres * 2) > info->fix.smem_len)
		return -EINVAL;

	/* set device-specific elements of var unrelated to mode */
	dlfb_var_color_format(var);

	fb_var_to_videomode(&mode, var);

	if (!dlfb_is_valid_mode(&mode, info))
		return -EINVAL;

	return 0;
}

static int dlfb_ops_set_par(struct fb_info *info)
{
	struct beagleusb *dev = info->par;
	int result = 0;
	u16 *pix_framebuffer;
	int i;
	
	printk("dlfb_ops_set_par called\n");

	pr_notice("set_par mode %dx%d\n", info->var.xres, info->var.yres);

	//result = dlfb_set_video_mode(dev, &info->var);

	if ((result == 0) && (dev->video.fb_count == 0)) {

		/* paint greenscreen */

		pix_framebuffer = (u16 *) info->screen_base;
		for (i = 0; i < info->fix.smem_len / 2; i++)
			pix_framebuffer[i] = 0x37e6;

		dlfb_handle_damage(dev, 0, 0, info->var.xres, info->var.yres,
				   info->screen_base);
	}
	
	printk("Painting green completed \n");

	return result;
}

/*
 * In order to come back from full DPMS off, we need to set the mode again
 */
static int dlfb_ops_blank(int blank_mode, struct fb_info *info)
{
	struct beagleusb *dev = info->par;
	
	printk("dlfb_ops_blank called\n");

	pr_info("/dev/fb%d FB_BLANK mode %d --> %d\n",
		info->node, dev->video.blank_mode, blank_mode);

	if ((dev->video.blank_mode == FB_BLANK_POWERDOWN) &&
	    (blank_mode != FB_BLANK_POWERDOWN)) {

		/* returning from powerdown requires a fresh modeset */
		//dlfb_set_video_mode(dev, &info->var);
	}

	return 0;
}

static struct fb_ops dlfb_ops = {
	.owner = THIS_MODULE,
	.fb_read = dlfb_ops_read,
	.fb_write = dlfb_ops_write,
	.fb_setcolreg = dlfb_ops_setcolreg,
	.fb_fillrect = dlfb_ops_fillrect,
	.fb_copyarea = dlfb_ops_copyarea,
	.fb_imageblit = dlfb_ops_imageblit,
	.fb_mmap = dlfb_ops_mmap,
	.fb_ioctl = dlfb_ops_ioctl,
	.fb_open = dlfb_ops_open,
	.fb_release = dlfb_ops_release,
	.fb_blank = dlfb_ops_blank,
	.fb_check_var = dlfb_ops_check_var,
	.fb_set_par = dlfb_ops_set_par,
};


/*
 * Assumes &info->lock held by caller
 * Assumes no active clients have framebuffer open
 */
static int dlfb_realloc_framebuffer(struct beagleusb *dev, struct fb_info *info)
{
	int retval = -ENOMEM;
	int old_len = info->fix.smem_len;
	int new_len;
	unsigned char *old_fb = info->screen_base;
	unsigned char *new_fb;
	unsigned char *new_back = 0;
	
	printk("dlfb_realloc_framebuffer called\n");

	pr_warn("Reallocating framebuffer. Addresses will change!\n");

	new_len = info->fix.line_length * info->var.yres;

	if (PAGE_ALIGN(new_len) > old_len) {
		/*
		 * Alloc system memory for virtual framebuffer
		 */
		new_fb = vmalloc(new_len);
		if (!new_fb) {
			pr_err("Virtual framebuffer alloc failed\n");
			goto error;
		}

		if (info->screen_base) {
			memcpy(new_fb, old_fb, old_len);
			vfree(info->screen_base);
		}

		info->screen_base = new_fb;
		info->fix.smem_len = PAGE_ALIGN(new_len);
		info->fix.smem_start = (unsigned long) new_fb;
		info->flags = udlfb_info_flags;

/*
 * For a range of kernels, must set these to workaround bad logic
 * that assumes all framebuffers are using PCI aperture.
 * If we don't do this, when we call register_framebuffer, fbmem.c will
 * forcibly unregister other framebuffers with smem_start of zero.  And
 * that's most of them (VESA, EFI, etc).
 */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 32) && \
	LINUX_VERSION_CODE <= KERNEL_VERSION(2, 6, 34))
		info->aperture_base = info->fix.smem_start;
		info->aperture_size = info->fix.smem_len;
#endif

		/*
		 * Second framebuffer copy to mirror the framebuffer state
		 * on the physical USB device. We can function without this.
		 * But with imperfect damage info we may send pixels over USB
		 * that were, in fact, unchanged - wasting limited USB bandwidth
		 */
		if (shadow)
			new_back = vzalloc(new_len);
		if (!new_back)
			pr_info("No shadow/backing buffer allocated\n");
		else {
			if (dev->video.backing_buffer)
				vfree(dev->video.backing_buffer);
			dev->video.backing_buffer = new_back;
		}
	}

	retval = 0;

error:
	return retval;
}

/*
 * 1) Get EDID from hw, or use sw defaultstatic int dlfb_setup_modes
 * 2) Parse into various fb_info structs
 * 3) Allocate virtual framebuffer memory to back highest res mode
 *
 * Parses EDID into three places used by various parts of fbdev:
 * fb_var_screeninfo contains the timing of the monitor's preferred mode
 * fb_info.monspecs is full parsed EDID info, including monspecs.modedb
 * fb_info.modelist is a linked list of all monitor & VESA modes which work
 *
 * If EDID is not readable/valid, then modelist is all VESA modes,
 * monspecs is NULL, and fb_var_screeninfo is set to safe VESA mode
 * Returns 0 if successful
 */
static int dlfb_setup_modes(struct beagleusb *dev,
			   struct fb_info *info,
			   char *default_edid, size_t default_edid_size)
{
	int i;
	const struct fb_videomode *default_vmode = NULL;
	int result = 0;
	char *edid = NULL;
	int tries = 3;
	
	printk("dlfb_setup_modes called\n");

	if (info->dev) /* only use mutex if info has been registered */
		mutex_lock(&info->lock);

	edid = kmalloc(EDID_LENGTH, GFP_KERNEL);
	if (!edid) {
		result = -ENOMEM;
		goto error;
	}

	fb_destroy_modelist(&info->modelist);
	memset(&info->monspecs, 0, sizeof(info->monspecs));

	/*
	 * Try to (re)read EDID from hardware first
	 * EDID data may return, but not parse as valid
	 * Try again a few times, in case of e.g. analog cable noise
	 */
	while (tries--) {

		//i = dlfb_get_edid(dev, edid, EDID_LENGTH);
		i = 128;
		
		if (i >= EDID_LENGTH){
			fb_edid_to_monspecs(edid, &info->monspecs);
		}

		if (info->monspecs.modedb_len > 0) {
			dev->video.edid = edid;
			dev->video.edid_size = i;
			break;
		}
	}

	/* If that fails, use a previously returned EDID if available */
	if (info->monspecs.modedb_len == 0) {

		pr_err("Unable to get valid EDID from device/display\n");

		if (dev->video.edid) {
			fb_edid_to_monspecs(dev->video.edid, &info->monspecs);
			if (info->monspecs.modedb_len > 0)
				pr_err("Using previously queried EDID\n");
		}
	}

	/* If that fails, use the default EDID we were handed */
	if (info->monspecs.modedb_len == 0) {
		if (default_edid_size >= EDID_LENGTH) {
			fb_edid_to_monspecs(default_edid, &info->monspecs);
			if (info->monspecs.modedb_len > 0) {
				memcpy(edid, default_edid, default_edid_size);
				dev->video.edid = edid;
				dev->video.edid_size = default_edid_size;
				pr_err("Using default/backup EDID\n");
			}
		}
	}

	/* If we've got modes, let's pick a best default mode */
	if (info->monspecs.modedb_len > 0) {

		for (i = 0; i < info->monspecs.modedb_len; i++) {
			if (dlfb_is_valid_mode(&info->monspecs.modedb[i], info))
				fb_add_videomode(&info->monspecs.modedb[i],
					&info->modelist);
			else {
				if (i == 0)
					/* if we've removed top/best mode */
					info->monspecs.misc
						&= ~FB_MISC_1ST_DETAIL;
			}
		}

		default_vmode = fb_find_best_display(&info->monspecs,
						     &info->modelist);
	}

#ifdef CONFIG_FB_MODE_HELPERS
	/* If everything else has failed, fall back to safe default mode */
	if (default_vmode == NULL) {

		struct fb_videomode fb_vmode = {0};

		/*
		 * Add the standard VESA modes to our modelist
		 * Since we don't have EDID, there may be modes that
		 * overspec monitor and/or are incorrect aspect ratio, etc.
		 * But at least the user has a chance to choose
		 */
		for (i = 0; i < VESA_MODEDB_SIZE; i++) {
			if (dlfb_is_valid_mode((struct fb_videomode *)
						&vesa_modes[i], info))
				fb_add_videomode(&vesa_modes[i],
						 &info->modelist);
		}

		/*
		 * default to resolution safe for projectors
		 * (since they are most common case without EDID)
		 */
		//fb_vmode.xres = 800;
		//fb_vmode.yres = 600;
		fb_vmode.xres = XRES;
		fb_vmode.yres = YRES;
		fb_vmode.refresh = 60;
		default_vmode = fb_find_nearest_mode(&fb_vmode,
						     &info->modelist);
	}
#endif
	/* If we have good mode and no active clients*/
	if ((default_vmode != NULL) && (dev->video.fb_count == 0)) {

		fb_videomode_to_var(&info->var, default_vmode);
		dlfb_var_color_format(&info->var);

		/*
		 * with mode size info, we can now alloc our framebuffer.
		 */
		memcpy(&info->fix, &dlfb_fix, sizeof(dlfb_fix));
		info->fix.line_length = info->var.xres *
			(info->var.bits_per_pixel / 8);

		result = dlfb_realloc_framebuffer(dev, info);

	} else
		result = -EINVAL;

error:
	if (edid && (dev->video.edid != edid))
		kfree(edid);

	if (info->dev)
		mutex_unlock(&info->lock);

	return result;
}

static ssize_t metrics_bytes_rendered_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("metrics_bytes_rendered_show called\n");
	
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->video.bytes_rendered));
}

static ssize_t metrics_bytes_identical_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("metrics_bytes_identical_show called\n");
	
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->video.bytes_identical));
}

static ssize_t metrics_bytes_sent_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("metrics_bytes_sent_show called\n");
	
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->video.bytes_sent));
}

static ssize_t metrics_cpu_kcycles_used_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("metrics_cpu_kcycles_used_show called\n");
	
	return snprintf(buf, PAGE_SIZE, "%u\n",
			atomic_read(&dev->video.cpu_kcycles_used));
}

static ssize_t monitor_show(struct device *fbdev,
				   struct device_attribute *a, char *buf) {
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	
	printk("monitor_show called\n");
	
	return snprintf(buf, PAGE_SIZE, "%s-%s\n",
			fb_info->monspecs.monitor,
			fb_info->monspecs.serial_no);
}

static ssize_t edid_show(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
			struct file *filp,
#endif
			struct kobject *kobj, struct bin_attribute *a,
			char *buf, loff_t off, size_t count) {
	struct device *fbdev = container_of(kobj, struct device, kobj);
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("edid_show called\n");

	if (dev->video.edid == NULL)
		return 0;

	if ((off >= dev->video.edid_size) || (count > dev->video.edid_size))
		return 0;

	if (off + count > dev->video.edid_size)
		count = dev->video.edid_size - off;

	pr_info("sysfs edid copy %p to %p, %d bytes\n",
		dev->video.edid, buf, (int) count);

	memcpy(buf, dev->video.edid, count);

	return count;
}

static ssize_t edid_store(
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35)
			struct file *filp,
#endif
			struct kobject *kobj, struct bin_attribute *a,
			char *src, loff_t src_off, size_t src_size) {
	struct device *fbdev = container_of(kobj, struct device, kobj);
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("edid_store called\n");

	/* We only support write of entire EDID at once, no offset*/
	if ((src_size != EDID_LENGTH) || (src_off != 0))
		return 0;

	dlfb_setup_modes(dev, fb_info, src, src_size);

	if (dev->video.edid && (memcmp(src, dev->video.edid, src_size) == 0)) {
		pr_info("sysfs written EDID is new default\n");
		dlfb_ops_set_par(fb_info);
		return src_size;
	} else
		return 0;
}

static ssize_t metrics_reset_store(struct device *fbdev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct fb_info *fb_info = dev_get_drvdata(fbdev);
	struct beagleusb *dev = fb_info->par;
	
	printk("metrics_reset_store called\n");

	atomic_set(&dev->video.bytes_rendered, 0);
	atomic_set(&dev->video.bytes_identical, 0);
	atomic_set(&dev->video.bytes_sent, 0);
	atomic_set(&dev->video.cpu_kcycles_used, 0);

	return count;
}

static struct bin_attribute edid_attr = {
	.attr.name = "edid",
	.attr.mode = 0666,
	.size = EDID_LENGTH,
	.read = edid_show,
	.write = edid_store
};

static struct device_attribute fb_device_attrs[] = {
	__ATTR_RO(metrics_bytes_rendered),
	__ATTR_RO(metrics_bytes_identical),
	__ATTR_RO(metrics_bytes_sent),
	__ATTR_RO(metrics_cpu_kcycles_used),
	__ATTR_RO(monitor),
	__ATTR(metrics_reset, S_IWUSR, NULL, metrics_reset_store),
};

/*
 * This is necessary before we can communicate with the display controller.
 */
static int dlfb_select_std_channel(struct beagleusb *dev)
{
	int ret;
	u8 set_def_chn[] = {	   0x57, 0xCD, 0xDC, 0xA7,
				0x1C, 0x88, 0x5E, 0x15,
				0x60, 0xFE, 0xC6, 0x97,
				0x16, 0x3D, 0x47, 0xF2  };

	printk("dlfb_select_std_channel called\n");

	ret = usb_control_msg(dev->usbdev, usb_sndctrlpipe(dev->usbdev, 0),
			NR_USB_REQUEST_CHANNEL,
			(USB_DIR_OUT | USB_TYPE_VENDOR), 0, 0,
			set_def_chn, sizeof(set_def_chn), USB_CTRL_SET_TIMEOUT);
	return ret;
}

//static void dlfb_init_framebuffer_work(struct work_struct *work);

int dlfb_video_init(struct beagleusb *dev){

	dev->video.sku_pixel_limit = 2048 * 1152; /* default to maximum */

	if (pixel_limit) {
		pr_warn("DL chip limit of %d overriden"
			" by module param to %d\n",
			dev->video.sku_pixel_limit, pixel_limit);
		dev->video.sku_pixel_limit = pixel_limit;
	}


	if (!dlfb_alloc_urb_list(dev, WRITES_IN_FLIGHT, DATA_PACKET_SIZE)) {
		//retval = -ENOMEM;
		pr_err("dlfb_alloc_urb_list failed\n");
		return -ENOMEM;
	}

	#if LAZZY_MODE
	printk("Init Thread\n");
	lazzy_thread = kthread_create(lazzy_update, (void*)dev, "lazzy_thread");

	if (lazzy_thread != NULL){
		printk(KERN_INFO "Data Manager Thread Created\n");
		wake_up_process(lazzy_thread);
	}
	#endif

	return 0;
}

void dlfb_init_framebuffer_work(struct work_struct *work)
{
	struct beagleusb *dev = container_of(work, struct beagleusb,
					     init_framebuffer_work.work);
	struct fb_info *info;
	int retval;
	int i;

	printk("dlfb_init_framebuffer_work called\n");

	/* allocates framebuffer driver structure, not framebuffer memory */
	info = framebuffer_alloc(0, dev->dev);
	if (!info) {
		retval = -ENOMEM;
		pr_err("framebuffer_alloc failed\n");
		goto error;
	}

	dev->video.info = info;
	info->par = dev;
	info->pseudo_palette = dev->video.pseudo_palette;
	info->fbops = &dlfb_ops;

	retval = fb_alloc_cmap(&info->cmap, 256, 0);
	if (retval < 0) {
		pr_err("fb_alloc_cmap failed %x\n", retval);
		goto error;
	}

	INIT_DELAYED_WORK(&dev->free_framebuffer_work,
			  dlfb_free_framebuffer_work);

	INIT_LIST_HEAD(&info->modelist);

	retval = dlfb_setup_modes(dev, info, NULL, 0);
	if (retval != 0) {
		pr_err("unable to find common mode for display and adapter\n");
		goto error;
	}

	/* ready to begin using device */

	atomic_set(&dev->video.usb_active, 1);
	dlfb_select_std_channel(dev);

	dlfb_ops_check_var(&info->var, info);
	dlfb_ops_set_par(info);
	
	/* -TODO- No debug messages here after */

	retval = register_framebuffer(info);
	printk("Registering framebuffer\n");
	if (retval < 0) {
		pr_err("register_framebuffer failed %d\n", retval);
		goto error;
	} else{
		printk("Frame buffer registered\n");
	}
	printk("Registering framebuffer done\n");

	for (i = 0; i < ARRAY_SIZE(fb_device_attrs); i++) {
		retval = device_create_file(info->dev, &fb_device_attrs[i]);
		if (retval) {
			pr_warn("device_create_file failed %d\n", retval);
		} else{
			printk("device_create_file success!\n");
		}
	}

	retval = device_create_bin_file(info->dev, &edid_attr);
	if (retval) {
		pr_warn("device_create_bin_file failed %d\n", retval);
	} else{
		printk("device_create_bin_file success\n");
	}

	pr_info("DisplayLink USB device /dev/fb%d attached. %dx%d resolution."
			" Using %dK framebuffer memory\n", info->node,
			info->var.xres, info->var.yres,
			((dev->video.backing_buffer) ?
			info->fix.smem_len * 2 : info->fix.smem_len) >> 10);
	return;

error:
	dlfb_free_framebuffer(dev);
}

void dlfb_usb_disconnect(struct beagleusb *dev)
{
	struct fb_info *info;
	int i;
	
	printk("dlfb_usb_disconnect called\n");

	#if LAZZY_MODE
	printk("Stop Lazzy_Update Thread\n");
	lazzy_run = 0;
	kthread_stop(lazzy_thread);
	#endif

	info = dev->video.info;

	pr_info("USB disconnect starting\n");

	/* we virtualize until all fb clients release. Then we free */
	dev->video.virtualized = true;

	/* When non-active we'll update virtual framebuffer, but no new urbs */
	atomic_set(&dev->video.usb_active, 0);

	/* this function will wait for all in-flight urbs to complete */
	/* -TODO- Free these urbs.
	 * temporarily removed to see if this fixes the disconnect freeze issue
	 */
	
	dlfb_free_urb_list(dev);

	if (info) {

		/* remove udlfb's sysfs interfaces */
		for (i = 0; i < ARRAY_SIZE(fb_device_attrs); i++)
			device_remove_file(info->dev, &fb_device_attrs[i]);
		device_remove_bin_file(info->dev, &edid_attr);

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 3, 0))
		unlink_framebuffer(info);
#endif
	}

	/* if clients still have us open, will be freed on last close */
	if (dev->video.fb_count == 0)
		schedule_delayed_work(&dev->free_framebuffer_work, 0);

	/* release reference taken by kref_init in probe() */
	//kref_put(&dev->kref, dlfb_free);

	return;
}


static void dlfb_urb_completion(struct urb *urb)
{
	struct urb_node *unode = urb->context;
	struct beagleusb *dev = unode->dev;
	unsigned long flags;

	//printk("dlfb_urb_completion called\n");

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN)) {
			pr_err("%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);
			atomic_set(&dev->video.lost_pixels, 1);
		}
	}

	urb->transfer_buffer_length = dev->video.urbs.size; /* reset to actual */

	spin_lock_irqsave(&dev->video.urbs.lock, flags);
	list_add_tail(&unode->entry, &dev->video.urbs.list);
	dev->video.urbs.available++;
	spin_unlock_irqrestore(&dev->video.urbs.lock, flags);

	/*
	 * When using fb_defio, we deadlock if up() is called
	 * while another is waiting. So queue to another process.
	 */
	if (fb_defio)
		schedule_delayed_work(&unode->release_urb_work, 0);
	else
		up(&dev->video.urbs.limit_sem);
}

static void dlfb_free_urb_list(struct beagleusb *dev)
{
	int count = dev->video.urbs.count;
	struct list_head *node;
	struct urb_node *unode;
	struct urb *urb;
	int ret;
	unsigned long flags;
	
	printk("dlfb_free_urb_list called\n");

	pr_notice("Freeing all render urbs\n");

	/* keep waiting and freeing, until we've got 'em all */
	while (count--) {

		/* Getting interrupted means a leak, but ok at disconnect */
		ret = down_interruptible(&dev->video.urbs.limit_sem);
		if (ret)
			break;

		spin_lock_irqsave(&dev->video.urbs.lock, flags);

		node = dev->video.urbs.list.next; /* have reserved one with sem */
		list_del_init(node);

		spin_unlock_irqrestore(&dev->video.urbs.lock, flags);

		unode = list_entry(node, struct urb_node, entry);
		urb = unode->urb;

		/* Free each separately allocated piece */
		usb_free_coherent(urb->dev, dev->video.urbs.size,
				  urb->transfer_buffer, urb->transfer_dma);
		usb_free_urb(urb);
		kfree(node);
	}

	dev->video.urbs.count = 0;
}

static int dlfb_alloc_urb_list(struct beagleusb *dev, int count, size_t size)
{
	int i = 0;
	struct urb *urb;
	struct urb_node *unode;
	
	printk("dlfb_alloc_urb_list called\n");

	spin_lock_init(&dev->video.urbs.lock);

	dev->video.urbs.size = size;
	INIT_LIST_HEAD(&dev->video.urbs.list);

	while (i < count) {
		unode = kzalloc(sizeof(struct urb_node), GFP_KERNEL);
		if (!unode)
			break;
		unode->dev = dev;

		INIT_DELAYED_WORK(&unode->release_urb_work,
			  dlfb_release_urb_work);

		urb = usb_alloc_urb(0, GFP_KERNEL);
		if (!urb) {
			kfree(unode);
			break;
		}
		unode->urb = urb;
		urb->transfer_buffer = kzalloc(DATA_PACKET_SIZE, GFP_KERNEL);

		/*buf = usb_alloc_coherent(dev->usbdev, DATA_PACKET_SIZE, GFP_KERNEL,
					 &urb->transfer_dma);
		if (!buf) {
			kfree(unode);
			usb_free_urb(urb);
			break;
		}*/

		// -TODO- Remove hardcoded bulkout address
		/* urb->transfer_buffer_length set to actual before submit */
		/*  */
		usb_fill_bulk_urb(urb, dev->usbdev, 
			usb_sndbulkpipe(dev->usbdev, dev->bulk_out_endpointAddr),
			urb->transfer_buffer, size, dlfb_urb_completion, unode);
		//urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

		list_add_tail(&unode->entry, &dev->video.urbs.list);

		i++;
	}

	sema_init(&dev->video.urbs.limit_sem, i);
	dev->video.urbs.count = i;
	dev->video.urbs.available = i;

	pr_notice("allocated %d %d byte urbs\n", i, (int) size);

	return i;
}

static struct urb *dlfb_get_urb(struct beagleusb *dev)
{
	int ret = 0;
	struct list_head *entry;
	struct urb_node *unode;
	struct urb *urb = NULL;
	unsigned long flags;
	
	//printk("dlfb_get_urb called\n");
	/* Wait for an in-flight buffer to complete and get re-queued */
	ret = down_timeout(&dev->video.urbs.limit_sem, GET_URB_TIMEOUT);
	if (ret) {
		atomic_set(&dev->video.lost_pixels, 1);
		pr_warn("wait for urb interrupted: %x available: %d\n",
		       ret, dev->video.urbs.available);
		goto error;
	}

	spin_lock_irqsave(&dev->video.urbs.lock, flags);

	BUG_ON(list_empty(&dev->video.urbs.list)); /* reserved one with limit_sem */
	entry = dev->video.urbs.list.next;
	list_del_init(entry);
	dev->video.urbs.available--;

	spin_unlock_irqrestore(&dev->video.urbs.lock, flags);

	unode = list_entry(entry, struct urb_node, entry);
	urb = unode->urb;

error:
	return urb;
}

static int dlfb_submit_urb(struct beagleusb *dev, struct urb *urb, size_t len)
{
	int ret;

	//printk("dlfb_submit_urb called\n");	
	
	//BUG_ON(len > dev->video.urbs.size);

	urb->transfer_buffer_length = len; /* set to actual payload len */
	
	/*
	 * Any urb submits are ignored and returned a success code.
	 * Right now, the build doesn't repond well when there are data
	 * transfers over usb, besides the frame data. Check git commits to
	 * to see what was deleted. Commit "Cleanup in dlfb_submit_urb"
	 */
	
	ret = 0;
	ret = usb_submit_urb(urb, GFP_KERNEL);
	
	return ret;
}

/*module_param(console, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(console, "Allow fbcon to open framebuffer");

module_param(fb_defio, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(fb_defio, "Page fault detection of mmap writes");

module_param(shadow, bool, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(shadow, "Shadow vid mem. Disable to save mem but lose perf");

module_param(pixel_limit, int, S_IWUSR | S_IRUSR | S_IWGRP | S_IRGRP);
MODULE_PARM_DESC(pixel_limit, "Force limit on max mode (in x*y pixels)");

MODULE_AUTHOR("Roberto De Ioris <roberto@unbit.it>, "
	      "Jaya Kumar <jayakumar.lkml@gmail.com>, "
	      "Bernie Thompson <bernie@plugable.com>");
MODULE_DESCRIPTION("DisplayLink kernel framebuffer driver");
MODULE_LICENSE("GPL");
*/
