#ifndef RINGBUFFER_H
#define RINGBUFFER_H 1


#define BUFFER_SIZE	256



struct buffer{
	unsigned char *data;
};

static spinlock_t bufferlock;
static struct semaphore countingsemaphore;

static struct buffer buffer[BUFFER_SIZE];
static int start=-1;
static unsigned int end = 0;
static int itemcount = 0;

void ringbuffer_init(void);
void insert(unsigned char *data);
unsigned char *get(void);


#endif
