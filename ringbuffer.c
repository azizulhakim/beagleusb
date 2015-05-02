#include <linux/semaphore.h>


#include "ringbuffer.h"

void ringbuffer_init(void){
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
	if (itemcount < BUFFER_SIZE){
//		up(&countingsemaphore);
	}

	printk("start = %d end = %d\n", start, end); 

	spin_unlock(&bufferlock);
}

unsigned char* get(void){
	unsigned char* data;

	down_interruptible(&countingsemaphore);
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
	printk("start = %d end = %d\n", start, end); 

	spin_unlock(&bufferlock);

	return data;
}
