#include <linux/kthread.h> 
#include <linux/sched.h>
#include <linux/delay.h>

#include "datamanager.h"
#include "ringbuffer.h"

struct task_struct *managerthread;

int send_data(void){
	unsigned char* data;

	printk("Start data capture\n");
	while(1){
		data = get();

		if (data != NULL)
			printk("data[0] = %d\n", data[0]);
		else
			printk("data = NULL\n");
	}

	return 0;
}

int manager_init(void){
	managerthread = kthread_create(send_data, NULL, "managerthread");

	if (managerthread != NULL){
		printk(KERN_INFO "Data Manager Thread Created\n");
		wake_up_process(managerthread);
	}

	return 0;
}
