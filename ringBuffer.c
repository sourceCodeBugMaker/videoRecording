#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "ringBuffer.h"


static RingBuffer_ST s_stRingbuffer;
bool ringBufferIsInitialized = false;

void ringBufferReset(void)
{
    if (ringBufferIsInitialized)
    {
        pthread_mutex_lock(&s_stRingbuffer.mutex);
        memset(s_stRingbuffer.data, 0, DEFAULT_RINGBUFFER_SIZE);
        s_stRingbuffer.front = s_stRingbuffer.rear = 0;
        pthread_mutex_unlock(&s_stRingbuffer.mutex);
    }
}

int ringBufferInit(void)
{
    ringBufferIsInitialized = true;
    memset(&s_stRingbuffer, 0, sizeof(s_stRingbuffer));
    pthread_mutex_init(&s_stRingbuffer.mutex, NULL);
    s_stRingbuffer.data = (char *)malloc(DEFAULT_RINGBUFFER_SIZE);
    if (s_stRingbuffer.data == NULL)
    {
        printf("ERROR:ring buffer malloc failed\n");
        pthread_mutex_destroy(&s_stRingbuffer.mutex);
        return -1;
    }
    memset(s_stRingbuffer.data, 0, DEFAULT_RINGBUFFER_SIZE);
    s_stRingbuffer.capacity = DEFAULT_RINGBUFFER_SIZE;
    return 0;
}

void ringBufferUninit(void)
{
    pthread_mutex_destroy(&s_stRingbuffer.mutex);
    free(s_stRingbuffer.data);
    s_stRingbuffer.data = NULL;
}

static bool ringBufferSpaceIsNotEnough(int len)
{
    int rear = s_stRingbuffer.rear;
    int front = s_stRingbuffer.front;
    int capacity = s_stRingbuffer.capacity;

    if (rear > front)
    {
        if (len + rear >= capacity)
        {
            if ((len + rear)%capacity >= front)
            {
                return true;
            }
        }
        else
        {
            return false;
        }

    }
    else if(rear < front)
    {
        if (len + rear >= front)
        {
            return true;
        }
    }
    else
    {
        return false;
    }


    return false;
}

int ringBufferPush(char *data, int len)
{
    pthread_mutex_lock(&s_stRingbuffer.mutex);
    int rear = s_stRingbuffer.rear;
    int front = s_stRingbuffer.front;
    if (ringBufferSpaceIsNotEnough(len))
    {
        printf("ERROR:ring buffer space is not enough\n");
        pthread_mutex_unlock(&s_stRingbuffer.mutex);
        return -1;
    }
    else
    {
        if (rear + len > s_stRingbuffer.capacity) 
        {
            int first_chunk_size = s_stRingbuffer.capacity - rear;
            memcpy(&s_stRingbuffer.data[rear], data, first_chunk_size);
            memcpy(&s_stRingbuffer.data[0], &data[first_chunk_size], (len - first_chunk_size));
            s_stRingbuffer.rear = len - first_chunk_size;
        }
        else 
        {
            memcpy(&s_stRingbuffer.data[rear], data, len);
            s_stRingbuffer.rear = (rear + len) % s_stRingbuffer.capacity;
        }
    }
    pthread_mutex_unlock(&s_stRingbuffer.mutex);
    return 0;
}



int ringBufferPop(Nalu_ST *pstNalu)
{
    pthread_mutex_lock(&s_stRingbuffer.mutex);
    int i = 0;
    int startIdx = 0;
    int copyLen = 0;
    int capacity = s_stRingbuffer.capacity;
    if (s_stRingbuffer.front == s_stRingbuffer.rear)
    {
        pthread_mutex_unlock(&s_stRingbuffer.mutex);
        //printf("ring buffer is null\n");
        return -1;
    }

    char *data = s_stRingbuffer.data;
    i = s_stRingbuffer.front;
    if (!(data[i % capacity] == 0x00 
    && data[(i + 1) % capacity] == 0x00 
    && data[(i + 2) % capacity] == 0x00 
    && data[(i + 3) % capacity] == 0x01))
    {
        printf("ERROR:ring buffer data is incorret\n");
        pthread_mutex_unlock(&s_stRingbuffer.mutex);
        return -1;
    }

    i = (s_stRingbuffer.front + 4) % capacity; //先偏移4个标记位 00 00 00 01
    startIdx = i;
    int cnt = 0;
    int tmpIdx = 0;
    while (i != s_stRingbuffer.rear)
    {
        //0x5a5aa5a5时间戳标记是和媒体层约定的
        if (data[i % capacity] == 0x5a 
        && data[(i + 1) % capacity] == 0x5a 
        && data[(i + 2) % capacity] == 0xa5 
        && data[(i + 3) % capacity] == 0xa5)
        {
            tmpIdx = (i + 4) % capacity;  //偏移4字节时间戳标记
            //媒体层读取到的时间戳字节是小端的
            for (int j = 7; j >= 0; j--) 
            {
                pstNalu->usec = (pstNalu->usec << 8) | data[(tmpIdx + j) % capacity];
            }
            //printf("usec = %llu\n", pstNalu->usec);

            i = (i + 12) % capacity;         
            if (cnt > ONE_NALU_MAX_SIZE)
            {
                printf("ERROR:the nalu size is more than %d coredump will happen\n", ONE_NALU_MAX_SIZE);
                pthread_mutex_unlock(&s_stRingbuffer.mutex);
                return -2;
            }
            pstNalu->size = cnt;
            pstNalu->frameData[0] = pstNalu->size >> 24;
            pstNalu->frameData[1] = pstNalu->size >> 16;
            pstNalu->frameData[2] = pstNalu->size >> 8;
            pstNalu->frameData[3] = pstNalu->size & 0xff;
            if (startIdx + cnt > s_stRingbuffer.capacity) //分两次拷贝
            {
                copyLen = s_stRingbuffer.capacity - startIdx;
                memcpy(&pstNalu->frameData[4], &data[startIdx], copyLen);
                memcpy(&pstNalu->frameData[copyLen + 4], &data[0], cnt - copyLen);
            }
            else//一次拷贝完
            {
                memcpy(&pstNalu->frameData[4], &data[startIdx], cnt);
            }        
            pstNalu->type = (pstNalu->frameData[4] & 0x1f);
            s_stRingbuffer.front = i;
            pthread_mutex_unlock(&s_stRingbuffer.mutex);
            return 0;

        }

        cnt++;
        i = (i + 1) % capacity;
    }
    pthread_mutex_unlock(&s_stRingbuffer.mutex);
    return -1;
}
