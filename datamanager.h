#ifndef DATAMANGER_H
#define DATAMANGER_H

#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>

#include "beagleusb.h"
#include "ringbuffer.h"


int send_data(void* beagledev);
int manager_init(struct beagleusb *beagleusb);

#endif
