#ifndef RINGBUFFER_H
#define RINGBUFFER_H 1


#define BUFFER_SIZE	256

struct buffer{
	unsigned char *data;
};

typedef struct buffer RingBuffer;

void ringbuffer_init(void);
void insert(unsigned char *data);
unsigned char *get(void);


#endif
