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



#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/usb.h>


#include "datamanager.h"
#include "ringbuffer.h"

struct urb_list_head{
	int count;
	struct list_head	head;
};

struct task_struct*		managerthread;
struct urb_list_head 	free_urb_head;
struct urb_list_head 	occupied_urb_head;

spinlock_t urblock;

int send_data(void* beagledev){
	unsigned char* data;
//	struct beagleusb* beagleusb = (struct beagleusb*)beagledev;
//	int transferred = 0;
//	int retval = 0;

	printk("Start data capture\n");
	while(1){
		data = get();

		if (data != NULL)
			printk("data[0] = %d\n", data[0]);
		else
			printk("data = NULL\n");

/*		if(data){
			retval = usb_bulk_msg(beagleusb->usbdev, 
								  usb_sndbulkpipe(beagleusb->usbdev, beagleusb->bulk_out_endpointAddr),
								  data,
								  DATA_PACKET_SIZE,
								  &transferred,
								  HZ*5);

			printk("Return: %d transferred: %d,  data[0] = %d \n", retval, transferred, data[0]);
		
			kfree(data);
		}
*/
	}

	return 0;
}

struct urb* get_free_urb(void){
	struct urb* urb = NULL;
    struct urb_list_ *freeUrbPtr;
	printk("Get Free Urb\n");

	if (free_urb_head.count == 0){
		urb = usb_alloc_urb(0, GFP_KERNEL);
		urb->transfer_buffer = kzalloc(DATA_PACKET_SIZE, GFP_KERNEL);
	}else{
		freeUrbPtr = list_entry(&free_urb_head.head, struct urb_list_, list_member);
		urb = freeUrbPtr->urb;
		list_del(&freeUrbPtr->list_member);
		free_urb_head.count--;
	}

	printk("Free Urb Ready\n");

	return urb;
}

void add_urb(struct urb* urb, ListType type)
{
    struct urb_list_ *freeUrbList = (struct urb_list_ *)kmalloc(sizeof(struct urb_list_), GFP_KERNEL);
	struct urb_list_head urb_head = free_urb_head;

	printk("Add Urb\n");

	spin_lock(&urblock);

	if (type == OCCUPIED) urb_head = occupied_urb_head;

    WARN_ON(freeUrbList == NULL);
    
    freeUrbList->urb = urb;
    INIT_LIST_HEAD(&freeUrbList->list_member);
    list_add(&freeUrbList->list_member, &urb_head.head);

	urb_head.count++;

	spin_unlock(&urblock);

	printk("Added Urb\n");
}

void delete_urb(struct urb* urb, ListType type){
	struct urb_list_head urb_head = free_urb_head;

    struct urb_list_ *freeUrbPtr;

	printk("Delete Urb\n");

	spin_lock(&urblock);

	if (type == OCCUPIED) urb_head = occupied_urb_head;
	
	freeUrbPtr = list_entry(&urb_head.head, struct urb_list_, urb);
	list_del(&freeUrbPtr->list_member);

	urb_head.count--;

	spin_unlock(&urblock);

	printk("Deleted Urb\n");
}

void delete_all(struct list_head *head)
{
    struct list_head *iter;
    struct urb_list_ *objPtr;
    
  redo:
    list_for_each(iter, head) {
        objPtr = list_entry(iter, struct urb_list_, list_member);
        list_del(&objPtr->list_member);
        kfree(objPtr);
        goto redo;
    }
}

int manager_init(struct beagleusb *beagleusb){
/*	managerthread = kthread_create(send_data, (void*)beagleusb, "managerthread");

	if (managerthread != NULL){
		printk(KERN_INFO "Data Manager Thread Created\n");
		wake_up_process(managerthread);
	}*/

	printk("URB LIST HEAD INIT\n");

	INIT_LIST_HEAD(&free_urb_head.head);
	INIT_LIST_HEAD(&occupied_urb_head.head);
	spin_lock_init(&urblock);

	printk("URB LIST HEAD INIT COMPLETE\n");

	return 0;
}
