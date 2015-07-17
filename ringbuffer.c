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


#include <linux/semaphore.h>


#include "ringbuffer.h"

spinlock_t bufferlock;
struct semaphore countingsemaphore;


RingBuffer buffer[BUFFER_SIZE];
int start = -1;
unsigned int end = 0;
int itemcount = 0;

void ringbuffer_init(void){
	start=-1;
	end = 0;
	itemcount = 0;
	spin_lock_init(&bufferlock);
	sema_init(&countingsemaphore, 0);
}

void insert(unsigned char *data){
	spin_lock(&bufferlock);

	buffer[end].data = data;

	end = (end + 1) % BUFFER_SIZE;

	if (start < 0) start = 0;

	if (end == start) start = (start + 1) % BUFFER_SIZE;

	itemcount = (itemcount + 1) % BUFFER_SIZE + 1;
//	if (itemcount < BUFFER_SIZE){
//		up(&countingsemaphore);
//	}

	//printk("start = %d end = %d\n", start, end); 

	spin_unlock(&bufferlock);
}

unsigned char* get(void){
	unsigned char* data;

//	down_interruptible(&countingsemaphore);
	spin_lock(&bufferlock);

	if (start < 0){
		spin_unlock(&bufferlock);
		return NULL;
	}

	data = buffer[start].data;
	start = (start + 1) % BUFFER_SIZE;

	if (start == end){
		start = -1;
		end = 0;
	}
	
	itemcount--;
	printk("GET: start = %d end = %d\n", start, end); 

	spin_unlock(&bufferlock);

	return data;
}
