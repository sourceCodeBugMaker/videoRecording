#ifndef __RECORDING_CONTROL_H__
#define __RECORDING_CONTROL_H__

/*设置MP4文件存放目录：绝对路径且不要最后的/, 例如/mnt/sd/movie */
int setMp4FileStoregeDirPath(char *dirPath);

/*获取MP4文件存放目录*/
void getMp4FileStoregeDirPath(char *dirPath);

/*设置单个MP4文件录制的最大时长*/
int setRecordingMaxTime(unsigned long long time);

/*获取单个MP4文件录制的最大时长*/
unsigned long long getRecordingMaxTime(void);

/*开启录制功能*/
int recordingEnable(void); 

/*关闭录制功能*/
void recordingDisable(void);

/*true:开始录制 false:结束录制*/
void setRecodingStat(bool stat);

/*获取录制状态*/
bool getRecodingStat(void);

#endif