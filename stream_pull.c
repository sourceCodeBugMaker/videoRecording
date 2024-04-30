#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>  
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/prctl.h>
#include<sys/time.h>
#include "ringBuffer.h"
#include "stream_pull.h"
#include "stream_to_mp4.h"
#include "recording_control.h"

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 26464
static int s_s32SockFd = -1;
static bool s_blStreamIsPulling = false;
#define BUFFER_SIZE (128 * 1024)

static FILE* file = NULL;

static int pull_complete = 0;


int getPullStat(void)
{
    return pull_complete;
}

static int udpSocketInit(void)
{
    int ret = 0;
    int optVal;
    socklen_t optLen;
    struct sockaddr_in server_addr;
	
    //创建udp socket
	s_s32SockFd = socket(AF_INET, SOCK_DGRAM, 0);
	if (s_s32SockFd < 0) 
    {  
		printf("socket creation failed\n");  
		return -1;
	}  

	// 设置服务器地址信息  
	memset(&server_addr, 0, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;  
	server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);  
	server_addr.sin_port = htons(SERVER_PORT);  

    //获取设置后的缓冲区大小
    optLen = sizeof(optVal);
    ret = getsockopt(s_s32SockFd, SOL_SOCKET, SO_RCVBUF, &optVal, &optLen);
    if (ret < 0)
    {
        printf("getsockopt failed");
        close(s_s32SockFd);
        s_s32SockFd = -1;
        return -1;
    }
    printf("current receive buffer size: %d bytes\n", optVal); 

    if (optLen < BUFFER_SIZE)
    {
        //设置socket接收缓冲区大小
        optVal = BUFFER_SIZE; 
        ret = setsockopt(s_s32SockFd, SOL_SOCKET, SO_RCVBUF, &optVal, sizeof(optVal));
        if (ret < 0) 
        {  
            printf("setsockopt failed");
            close(s_s32SockFd);
            s_s32SockFd = -1;
            return -1;
        }

        //获取设置后的缓冲区大小
        optLen = sizeof(optVal);
        ret = getsockopt(s_s32SockFd, SOL_SOCKET, SO_RCVBUF, &optVal, &optLen);
        if (ret < 0)
        {
            printf("getsockopt failed");
            close(s_s32SockFd);
            s_s32SockFd = -1;
            return -1;
        }
        printf("actual receive buffer size: %d bytes\n", optVal); 
    }


	// 绑定socket到服务器地址  
	ret = bind(s_s32SockFd, (struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret < 0)
    {  
		printf("bind failed\n"); 
        close(s_s32SockFd);
        s_s32SockFd = -1; 
		return -1;
	}

    return 0;
}


static bool s_blIsFirstRecv = false;


static int streamPush(char buffer[], int len)
{
    int ret = 0;
    if (s_blIsFirstRecv)
    {
        if (!(buffer[0] == 0x00 
            && buffer[1] == 0x00 
            && buffer[2] == 0x00 
            && buffer[3] == 0x01))
        {
            return 0; //第一次接收如果没有找到00000001标记，直接丢弃
        }
        else
        {
            s_blIsFirstRecv = false;
        }
    }

    ret = ringBufferPush(buffer, len);
    if (ret != 0)
    {
        printf("ERROR: ring buffer space is not enough, drop current recv packet\n");
        return -1;
    }

    return 0;
}

static int fileOffset = 0;
static int readBufferFromFile(char buf[], int *len)
{
    printf("fileOffset = %d\n", fileOffset);
    fseek(file, fileOffset, SEEK_SET);
    int bytes;
    int length = 0;
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, BUFFER_SIZE);
    if ((bytes = fread(buffer, 1, sizeof(buffer), file)) > 0)
    {
        int startIdx = -1;
        int endIdx = -1;
        int i = 0;
        while(i < bytes)
        {
            if(buffer[i] == 0x00 && buffer[i + 1] == 0x00 && buffer[i + 2] == 0x00 && buffer[i + 3] == 0x01)
            {
                if (startIdx == -1)
                {
                    startIdx = i;
                }
                else
                {
                    endIdx = i;
                    length = endIdx - startIdx;
                    memcpy(buf, buffer, length);
                    *len = length;
                    fileOffset += length;
                    return 0;
                }       
            }
            i++;
        }
    }
    return -1;
}

static void *streamPullProc(void *argc)
{
    prctl(PR_SET_NAME, "streamPullProc");
    if (pthread_detach(pthread_self()))
    {
        printf("pthrad detach error\n");
        return NULL;
    }


    // file = fopen("raw_stream.h264", "rb");
    // if (file == NULL) {
    //     perror("Error opening file");
    //     return NULL;
    // }

    char buffer[BUFFER_SIZE];
    char tmpBuffer[BUFFER_SIZE];
    int recvLen;
    int totalRead = 0;
    pull_complete = 0;
    int s32PushFailedPacketCnt = 0;
    int ret = 0;
    s_blIsFirstRecv = true;
    while (s_blStreamIsPulling)
    { 
		memset(buffer, 0, BUFFER_SIZE);
		recvLen = recvfrom(s_s32SockFd, buffer, BUFFER_SIZE, 0, NULL, NULL);
		if (recvLen > 0)
		{
            if (getRecodingStat() != true)
            {
                printf("----------revc size = %d-----but don't begin recoring-----\n", recvLen);
                s32PushFailedPacketCnt = 0;
                s_blIsFirstRecv = true;
                continue;
            }
            printf("----------revc size = %d----------\n", recvLen);
            
            ret = streamPush(buffer, recvLen);
            if (ret != 0)
            {
                s32PushFailedPacketCnt++;
            }

            if (s32PushFailedPacketCnt > 0)
            {
                printf("WARN: push failed packet cnt = %d\n", s32PushFailedPacketCnt);
            }
		}

        // memset(buffer, 0, BUFFER_SIZE);
        // ret = readBufferFromFile(buffer, &recvLen);
        // if (ret < 0)
        // {
        //     pull_complete = 1;
        //     printf("===============read end  total = %d=========================\n", totalRead);
        //     sleep(20);
        //     break;
        // }
        // printf("recv len = %d\n", recvLen);
        // totalRead += recvLen;
        // printf("totalRead = %d\n", totalRead);
        // ret = streamPush(buffer, recvLen);
        // if (ret != 0)
        // {
        //     s32PushFailedPacketCnt++;
        // }
        // if (s32PushFailedPacketCnt > 0)
        // {
        //     printf("WARN: push failed packet cnt = %d\n", s32PushFailedPacketCnt);
        // }
        // usleep(2000);

    }
    
    close(s_s32SockFd);
    s_s32SockFd = -1;
}

int streamPullInit(void)
{
    int ret = 0;
    pthread_t pid;

    ret = udpSocketInit();
    if (ret != 0)
    {
        printf("udpSocketInit failed\n");
        return -1;
    }

    s_blStreamIsPulling = true;
    ret = pthread_create(&pid, NULL, streamPullProc, NULL);
    if (ret)
    {
        close(s_s32SockFd);
        s_s32SockFd = -1;
        s_blStreamIsPulling = false;
        printf("pthread_create failed\n");
        return -1;
    }

    return 0;
}

void streamPullUninit(void)
{
    s_blStreamIsPulling = false;
}