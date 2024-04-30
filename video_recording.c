
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "video_recording.h"
#include "stream_pull.h"
#include "stream_to_mp4.h"
#include "ringBuffer.h"


int videoRecordingInit(void)
{
    int ret = 0;
    ret = ringBufferInit();
    if (ret != 0)
    {
        printf("ringBufferInit failed\n");
        return -1;
    }

    ret = streamPullInit();
    if (ret != 0)
    {
        printf("streamPullInit failed\n");
        ringBufferUninit();
        return -1;
    }

    ret = streamDumpInit();
    if(ret != 0)
    {
        printf("streamDumpInit failed\n");
        streamPullUninit();
        ringBufferUninit();
        return -1;
    }

    return 0;
}

void videoRecordingUninit(void)
{
    streamDumpUninit();
    streamPullUninit();
    ringBufferUninit();
}
