#ifndef DATAMANGER_H
#define DATAMANGER_H

#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>

#include "beagleusb.h"
#include "ringbuffer.h"

typedef enum {FREE, OCCUPIED} ListType;

struct urb_list_{
	struct urb* urb;
	struct list_head list_member;
};


int send_data(void* beagledev);
int manager_init(struct beagleusb *beagleusb);
struct urb* get_free_urb(void);
void add_urb(struct urb* urb, ListType type);
void delete_urb(struct urb* urb, ListType type);

#endif
