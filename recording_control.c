#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include<sys/time.h>
#include <time.h>
#include "video_recording.h"
#include "recording_control.h"

#define DIR_PATH_MAX_LEN 128
static bool s_blStartRecording = false;
static unsigned long long s_s32RecordingMaxTime = 1800;                     //单个mp4文件默认最大时长:秒
static char s_mp4FileStoregeDirPath[DIR_PATH_MAX_LEN] = "/mnt/sd/movie";    //mp4文件默认存放目录



void setRecodingStat(bool stat)
{
    s_blStartRecording = stat;
    if (s_blStartRecording)
    {
        printf("start recording .......\n");
    }
    else
    {
        printf("end recording .......\n");
    }
}

bool getRecodingStat(void)
{
    return s_blStartRecording;
}

int setRecordingMaxTime(unsigned long long time)
{
    if (s_blStartRecording)
    {
        printf("ERROR:video recording has started, disallow set recording max time\n");
        return -1;
    }
    s_s32RecordingMaxTime = time;
    printf("set recording max time: %d s success\n", s_s32RecordingMaxTime);
    return 0;
}

unsigned long long getRecordingMaxTime(void)
{
    return s_s32RecordingMaxTime;
}

int setMp4FileStoregeDirPath(char *dirPath)
{
    if (s_blStartRecording)
    {
        printf("ERROR:video recording has started, disallow changing the storage path\n");
        return -1;
    }
    memset(s_mp4FileStoregeDirPath, 0, sizeof(s_mp4FileStoregeDirPath));
    if (dirPath == NULL || strlen(dirPath) >= DIR_PATH_MAX_LEN)
    {
        printf("ERROR:the maximum length of the directory path is %d\n", DIR_PATH_MAX_LEN);
        return -1;
    }
    strncpy(s_mp4FileStoregeDirPath, dirPath, DIR_PATH_MAX_LEN -1);
    return 0;
}

void getMp4FileStoregeDirPath(char *dirPath)
{
    strncpy(dirPath, s_mp4FileStoregeDirPath, DIR_PATH_MAX_LEN -1);
}

int recordingEnable(void)
{
    int ret = 0;
    ret = videoRecordingInit();
    if (0 != ret)
    {
        printf("ERROR:videoRecordingInit failed\n");
        return -1;
    }
    return 0;
}

void recordingDisable(void)
{
    videoRecordingUninit();
}