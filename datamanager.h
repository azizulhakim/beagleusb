#ifndef DATAMANGER_H
#define DATAMANGER_H

#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>

#include "ringbuffer.h"

int send_data(void);
int manager_init(void);

#endif
