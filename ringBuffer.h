#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__
#include<sys/time.h>

typedef struct{
    int capacity;
    int front;
    int rear;
    pthread_mutex_t mutex;
    char *data;
}RingBuffer_ST;

#define ONE_NALU_MAX_SIZE (128 * 1024)
#define STRUCT_TIMEVAL_SIZE (sizeof(struct timeval))

typedef struct{
    unsigned long long usec;
    int type;
    int size;
    char frameData[ONE_NALU_MAX_SIZE];
}Nalu_ST;


#define DEFAULT_RINGBUFFER_SIZE (4 * 1024 * 1024)

int ringBufferInit(void);
void ringBufferUninit(void);
int ringBufferPush(char *data, int len);
int ringBufferPop(Nalu_ST *pstNalu);
void ringBufferReset(void);

#endif