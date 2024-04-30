#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <time.h>
#include <libavcodec/avcodec.h> 
#include <libavdevice/avdevice.h> 
#include <libavformat/avformat.h> 
#include <libavfilter/avfilter.h> 
#include <libavutil/avutil.h> 
#include <libswscale/swscale.h>
#include "ringBuffer.h"
#include "stream_to_mp4.h"
#include "stream_pull.h"
#include "recording_control.h"

#define WRITE_MAX_FRAME 1000//在转到mp4文件的帧数达到就结束
#define MP4_FILE_PATH_MAX_LEN 256
typedef struct{
    int spsLen;
    char spsData[256];
    int ppsLen;
    char ppsData[128];
}CodecparExtradataPayload_ST;
static CodecparExtradataPayload_ST s_stExtrPayload; //用于暂存sps和pps,mp4ContextInit需要

static bool s_blStreamIsDumping = false;            //线程控制
static AVFormatContext* s_pstFormatCtx = NULL;      //mp4上下文，用于写mp4文件
static int s_s32VideoStreamdIdx = -1;               //mp4上下文中视频流的索引位置   
static int s_s32FrameCnt = 0;                       //累积写入到MP4中的帧计数:I帧 + P帧
static unsigned long long s_u64StartTime = 0;       //开始录制保存mp4时间
static unsigned long long s_u64LastFrameUsec = 0;   //写MP4文件的最后一帧的usec

//MP4文件名格式：固定前缀_飞行编号_子编号,例如:VIDEO_00001_1
static const char *s_fixedPrefix = "VIDEO";         //MP4文件名固定前缀
static int s_s32FlyNum = 0;                         //MP4文件名飞行编号
static int s_s32SubNum = 0;                         //MP4文件名子编号

static bool recordingTimeIsReached(unsigned long long currentTime)
{
    if (s_u64StartTime == 0)
    {
        printf("warn: recording did not start\n");
        return false;
    }
    if (s_u64StartTime > currentTime)
    {
        printf("error: time value is incorret\n");
        return false;
    }
    unsigned long long recordingMaxTime = getRecordingMaxTime() * 1000 * 1000;
    unsigned long long secDiff = currentTime - s_u64StartTime;
    if (secDiff > recordingMaxTime)
    {
        return true;
    }
    else
    {
        return false;
    }
}

static void getMp4FileName(char *fileName)
{

    char dir[128];
    char name[MP4_FILE_PATH_MAX_LEN];

    memset(dir , 0, sizeof(dir));
    memset(name , 0, sizeof(name));
    getMp4FileStoregeDirPath(dir);
    snprintf(name, sizeof(name), "%s/%s_%05d_%d.mp4", dir, s_fixedPrefix, s_s32FlyNum, s_s32SubNum);
    strcpy(fileName, name);
    printf("mp4 file name is %s \n", fileName);
}


static void writeVideo(bool isKeyFrame, Nalu_ST *pstNalu)
{
    
    s_s32FrameCnt++;
    s_u64LastFrameUsec = pstNalu->usec;

    // Init packet
    AVPacket pkt;
    av_init_packet(&pkt);
    pkt.stream_index = s_s32VideoStreamdIdx;

    pkt.data = pstNalu->frameData;
    pkt.size = pstNalu->size + 4; //4字节的NALU length

    // Wait for key frame_count
    pkt.dts = s_s32FrameCnt * 90000 / 25;
    pkt.pts = pkt.dts;
    pkt.duration = 90000 / 25;
    pkt.flags |= (isKeyFrame ? AV_PKT_FLAG_KEY : 0);

    int ret = av_interleaved_write_frame(s_pstFormatCtx, &pkt);
    if (ret != 0) 
    {
        printf("write interleaved frame failed\n");
    }
}


static int mp4ContextInit(void)
{
    int ret = 0;
    char recordFileName[MP4_FILE_PATH_MAX_LEN];
    memset(recordFileName, 0, sizeof(recordFileName));
    getMp4FileName(recordFileName);
    //mp4视频流编码器参数中的extradata,包含s_stExtrPayload中的sps和pps信息,按照avcc格式封装
    //mp4封装视频必须要先封装此数据,否则无法正常解码播放
    uint8_t *pu8CodecExtradata;
    pu8CodecExtradata = (uint8_t *)malloc(1024);
    if (NULL == pu8CodecExtradata)
    {
        printf("malloc failed\n");
        return -1;
    }
    memset(pu8CodecExtradata, 0, 1024);

    ret = avformat_alloc_output_context2(&s_pstFormatCtx, NULL, "mp4", recordFileName);
    if (ret < 0)
    {
        printf("avformat_alloc_output_context2 failed\n");
        free(pu8CodecExtradata);
        return -1;
    }
    AVOutputFormat *ofmt = s_pstFormatCtx->oformat;
    if (!(ofmt->flags & AVFMT_NOFILE)) 
    {
        ret = avio_open(&s_pstFormatCtx->pb, recordFileName, AVIO_FLAG_WRITE);
        if (ret < 0)
        {
            printf("Could not open output file %s", recordFileName);
            free(pu8CodecExtradata);
            avformat_free_context(s_pstFormatCtx);
            return -1;
        }
    }

    /*添加视频流,并配置初始化参数*/
    AVStream *outStream = avformat_new_stream(s_pstFormatCtx, NULL);
    outStream->codecpar->codec_id   = AV_CODEC_ID_H264;
    outStream->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    outStream->codecpar->width      = 3840;
    outStream->codecpar->height     = 2160;
    //outStream->id                   = s_pstFormatCtx->nb_streams - 1;
    outStream->time_base.num        = 1;
    outStream->time_base.den        = 90000;

    //按照avcc格式拼接extradata:sps和pps数据
    memset(pu8CodecExtradata, 0, sizeof(pu8CodecExtradata));
    uint8_t avccHead[7] = {0x01, 0x64, 0x00, 0x28, 0xff, 0xe1, 0x00};
    int hLen = sizeof(avccHead);
    memcpy(pu8CodecExtradata, avccHead, hLen);                                                                       //hlen bytes
    pu8CodecExtradata[hLen] = s_stExtrPayload.spsLen;                                                                //1 byte
    memcpy(pu8CodecExtradata + hLen + 1, s_stExtrPayload.spsData, s_stExtrPayload.spsLen);                           //s_stExtrPayload.spsLen bytes
    pu8CodecExtradata[s_stExtrPayload.spsLen + hLen + 1] = 0x01;                                                     //1 byte
    pu8CodecExtradata[s_stExtrPayload.spsLen + hLen + 2] = 0x00;                                                     //1 byte
    pu8CodecExtradata[s_stExtrPayload.spsLen + hLen + 3] = s_stExtrPayload.ppsLen;                                   //1 byte
    memcpy(pu8CodecExtradata + s_stExtrPayload.spsLen + hLen + 4, s_stExtrPayload.ppsData, s_stExtrPayload.ppsLen);  //s_stExtrPayload ppsLen bytes
    int avccLen = hLen + s_stExtrPayload.spsLen + s_stExtrPayload.ppsLen + 4;
    outStream->codecpar->extradata  = pu8CodecExtradata; //此后pu8CodecExtradata由avformat_free_context函数统一释放
    outStream->codecpar->extradata_size = avccLen;
    s_s32VideoStreamdIdx = outStream->index; //在把流写进MP4时需要
    
    av_dump_format(s_pstFormatCtx, 0, recordFileName, 1);

    ret = avformat_write_header(s_pstFormatCtx, NULL);
    if (ret < 0) 
    {
        printf("write header failed\n");
        avformat_free_context(s_pstFormatCtx);
        return -1;
    }

    return 0;
}


static void mp4ContextFinish(void)
{
    if (s_pstFormatCtx != NULL)
    {
        //av_interleaved_write_frame(s_pstFormatCtx, NULL);
        av_write_trailer(s_pstFormatCtx);
        avformat_free_context(s_pstFormatCtx);
        s_pstFormatCtx = NULL;
        s_s32VideoStreamdIdx = -1;
    }
}


void *streamDumpProc(void *argc)
{
    prctl(PR_SET_NAME, "streamDumpProc");
    if (pthread_detach(pthread_self()))
    {
        printf("pthrad detach error\n");
        return NULL;
    }

    int ret = 0;
    long timeDiff = 0;
    int s32SpsPpsFlag = 0;
    int dropNaluNum = 0;
    Nalu_ST stNalu;
    int pFrameCnt = 0;
    int lostPframeCnt = 0;
    bool isFirstIframe = true;
    int iframeCnt = 0;

    memset(&s_stExtrPayload, 0, sizeof(s_stExtrPayload));
    while(s_blStreamIsDumping)
    {
        if (getRecodingStat() != true)
        {
            mp4ContextFinish();
            if (s_s32FrameCnt > 0)
            {
                if(recordingTimeIsReached(s_u64LastFrameUsec))
                {
                    s_s32FlyNum++;
                    s_s32SubNum = 0;
                }
                else
                {
                    s_s32SubNum++;
                }
            }
            
            ringBufferReset();
            s_u64StartTime = 0;
            s_s32FrameCnt = 0;
            dropNaluNum = 0;
            isFirstIframe = true;
            pFrameCnt = 0;
            iframeCnt = 0;
            lostPframeCnt = 0;
            s32SpsPpsFlag = 0;
            memset(&s_stExtrPayload, 0, sizeof(s_stExtrPayload));
            //sleep(1);
            continue;
        }
        memset(&stNalu, 0, sizeof(stNalu));
        ret = ringBufferPop(&stNalu);
        if (ret != 0)
        {
            if (ret == -1)
            {
                //printf("=================ring buffer no data==================\n");
            }
            if (ret == -2)
            {
                //printf("=================coredump==================\n");
            }

            // if (getPullStat())
            // {
            //     printf("=========================pull complete=========================\n");
            //     printf("========================recording end=====total  frame = %d  iframe = %d  pFrameCnt=%d=============\n", s_s32FrameCnt, iframeCnt ,pFrameCnt);
            //     mp4ContextFinish();
            //     s_blStreamIsDumping = 0;
            //     break;
            // }
            //usleep(1000);
            continue;
        }

        if (s32SpsPpsFlag != 0x11)
        {
            if (stNalu.type == 0x07) //sps
            {
                memset(s_stExtrPayload.spsData, 0, sizeof(s_stExtrPayload.spsData));
                memcpy(s_stExtrPayload.spsData, &stNalu.frameData[4], stNalu.size);
                s_stExtrPayload.spsLen = stNalu.size;
                //printf("sps len = %d\n", s_stExtrPayload.spsLen);
                s32SpsPpsFlag |= 0x10;
            }
            else if (stNalu.type == 0x08) //pps
            {
                memset(s_stExtrPayload.ppsData, 0, sizeof(s_stExtrPayload.ppsData));
                memcpy(s_stExtrPayload.ppsData, &stNalu.frameData[4], stNalu.size);
                s_stExtrPayload.ppsLen = stNalu.size;
                //printf("pps len = %d\n", s_stExtrPayload.ppsLen);
                if (s32SpsPpsFlag == 0x10) //保证收到的sps和pps是连续的
                {
                    s32SpsPpsFlag |= 0x01;
                    ret = mp4ContextInit();
                    if (ret != 0)
                    {
                        printf("ERROR:mp4ContextInit failed\n");
                        break;
                    }
                    s_s32FrameCnt = 0;
                    printf("--------recording begin--------\n");
                }
            }
            else
            {
                printf("no consecutive sps and pps were received, drop......\n");
                dropNaluNum++;
                ////直接丢弃
            }
            continue;
        }

        //0x06 & 0x1f = 6, nalu为辅助增强信息 (SEI)；
        //0x67 & 0x1f = 7, nalu为SPS
        //0x68 & 0x1f = 8, nalu为PPS
        //0x65 & 0x1f = 5, nalu为I帧
        //0x21 & 0x1f = 1, nalu为P帧
        switch(stNalu.type)
        {
            case 0x07: //sps
                //printf("skip a sps\n");
                break;
            
            case 0x08: //pps
                //printf("skip a pps\n");
                break;

            case 0x05: //I帧   
                if(recordingTimeIsReached(stNalu.usec))
                {
                    //达到录制时长就保存一个MP4文件
        
                    mp4ContextFinish();
                    if (lostPframeCnt > 0)
                    {
                        printf("WARN: A total of %d P frames are lost\n", lostPframeCnt);
                    }
                    else
                    {
                        printf("no p frames lost\n");
                    }
                    printf("--------recording end... total frame = %d  iframe = %d--------\n", s_s32FrameCnt, iframeCnt);
                    dropNaluNum = 0;
                    isFirstIframe = true;
                    pFrameCnt = 0;
                    iframeCnt = 0;
                    lostPframeCnt = 0;
                    s_s32FlyNum++;
                    s_s32SubNum = 0;

                    ret = mp4ContextInit();
                    if (ret != 0)
                    {
                        printf("ERROR:mp4ContextInit failed\n");
                        break;
                    }
                    s_s32FrameCnt = 0;
                    printf("--------recording next begin--------\n");
                }
                if (pFrameCnt != 29 && !isFirstIframe)
                {
                    printf("WARN:P frames = %d, Lost %d P frames\n", pFrameCnt, 29 - pFrameCnt);
                    lostPframeCnt += (29 - pFrameCnt);
                }

                //printf("%d p frames have been written, reset p frame count\n", pFrameCnt);
                pFrameCnt = 0;

                if (isFirstIframe)
                {
                    s_u64StartTime = stNalu.usec;
                    isFirstIframe = false;
                }

                printf("write a I frame\n"); 
                iframeCnt++;
                writeVideo(true, &stNalu);
                break;

            case 0x01: //非关键帧
                printf("write a p frame\n");
                writeVideo(false, &stNalu);
                pFrameCnt++;
                break;

            default:
                printf("WARN: don't support nalu type %d\n", stNalu.type);
        }
        // if (s_s32FrameCnt == WRITE_MAX_FRAME)
        // {
        //     printf("========================recording end=====total frame = %d=====dropNaluNum=%d=============\n", s_s32FrameCnt,dropNaluNum);
        //     mp4ContextFinish();
        //     s_blStreamIsDumping = 0;
        //     if (lostPframeCnt > 0)
        //     {
        //         printf("ERROR: A total of %d P frames are lost\n", lostPframeCnt);
        //     }
        //     else
        //     {
        //         printf("Perfect, no p frames lost\n");
        //     }
        // }

    }
    printf("===========streamDumpProc thread exit===========\n");
    return NULL;
}

int streamDumpInit(void)
{
    int ret = 0;
    pthread_t pid;
    s_blStreamIsDumping = true;
    ret = pthread_create(&pid, NULL, streamDumpProc, NULL);
    if (ret)
    {
        printf("ERROR:pthread_create failed\n");
        s_blStreamIsDumping = false;
        return -1;
    }

    return 0;
}


void streamDumpUninit(void)
{
    s_blStreamIsDumping = false;
}