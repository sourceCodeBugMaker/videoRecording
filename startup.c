
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include "recording_control.h"

static char mp4Path[256];
// 信号处理函数
void signalHandler(int signal) {
    if (signal == SIGUSR1) {
        bool stat = getRecodingStat();
        setRecodingStat(!stat);
    }
}


int main(int argc, char *argv[])
{
    int ret = 0;
    memset(mp4Path, 0, sizeof(mp4Path));
    strcpy(mp4Path, argv[1]);
    printf("mp4 path is %s\n", mp4Path);
    
    ret = recordingEnable();
    if (ret != 0)
    {
        return -1;
    }

    signal(SIGUSR1, signalHandler);

    ret = setRecordingMaxTime(40);
    if (ret != 0)
    {
        recordingDisable();
        return -1;
    }
    setMp4FileStoregeDirPath(mp4Path);//设置录制文件保存目录
    if (ret != 0)
    {
        recordingDisable();
        return -1;
    }

    setRecodingStat(true);  //开启录制

    while(1)
    {
        usleep(1000);
    }
    return 0;
}