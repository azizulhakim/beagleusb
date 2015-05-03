#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/usb.h>


#include "datamanager.h"
#include "ringbuffer.h"

struct task_struct*		managerthread;

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

int manager_init(struct beagleusb *beagleusb){
	managerthread = kthread_create(send_data, (void*)beagleusb, "managerthread");

	if (managerthread != NULL){
		printk(KERN_INFO "Data Manager Thread Created\n");
		wake_up_process(managerthread);
	}

	return 0;
}
