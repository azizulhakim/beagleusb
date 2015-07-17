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
