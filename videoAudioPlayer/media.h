#ifndef _MEDIA_H_
#define _MEDIA_H_

#include <X11/Xlib.h>
#include <X11/Xutil.h>      /* BitmapOpenFailed, etc. */
//#include <sys/shm.h>
//#include <X11/extensions/XShm.h>
#include <pthread.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <stdio.h>

#include <sys/time.h>
#include <stdbool.h>
#include <alsa/asoundlib.h>
#include "threadPool.h"
#include "linuxList.h"

typedef struct {
    AVPacket            *packet;
    struct list_head    list;
}packetData_t;

typedef struct {
    AVFrame     *frame;
    struct list_head    list;
}frameData_t;

typedef struct {
    Display         *display;
    Window           win;
    XImage          *ximage;
}display_t;

typedef struct {
    AVCodec             *pCodecVideo;
    AVCodecContext      *videoCodecCtx;
    struct SwsContext   *img_convert_ctx;
    int                 video_index;
    struct list_head    videoPacketQueue;
    struct list_head    videoFrameQueue;
    AVFrame             *displayFrame;
    int                 packetSize;
    int                 frameSize;
    pthread_mutex_t     packetQueueMtx;
    pthread_cond_t      packetQueueCond;
    pthread_mutex_t     frameQueueMtx;
    pthread_cond_t      frameQueueCond;
}videoStream_t;

typedef struct {
	AVCodec				*pCodecAudio;
	AVCodecContext		*audioCodecCtx;
	SwrContext			*audioSwrContext;
	enum AVSampleFormat in_sample_fmt;
	enum AVSampleFormat out_sample_fmt;
	int 				in_sample_rate;
	int 				out_sample_rate;
	uint64_t 			in_ch_layout;
	uint64_t 			out_ch_layout;
	int 				out_channel_nb;
	uint8_t 			*out_buffer;
	snd_pcm_t           *pcm;
    snd_pcm_hw_params_t *params;
	int					audio_index;
	int                 packetSizeAudio;
	struct list_head    audioPacketQueue;
	pthread_mutex_t     packetQueueMtxAudio;
    pthread_cond_t      packetQueueCondAudio;
}audioStream_t;

typedef struct {
    AVFormatContext     *pFormatCtx;
    videoStream_t       videoStream;
	audioStream_t		audioStream;
    display_t           x11display;
    threadpool          thpool;
    bool                decThreadExit;
    bool                readThreadExit;
}player_t;

extern bool video_packet_enque(player_t *mediaContext, AVPacket *packet);
extern bool video_packet_deque(player_t *mediaContext, AVPacket *packet);
extern bool audio_packet_enque(player_t *mediaContext, AVPacket *packet);
extern bool audio_packet_deque(player_t * mediaContext, AVPacket * packet);
extern bool video_frame_enque(player_t *mediaContext, AVFrame *frame);
extern frameData_t *video_frame_deque(player_t *mediaContext);
extern int openInput(player_t *mediaContext, char *mediaFile);
extern int initDisplay(player_t *mediaContext);
extern void displayResize(player_t *mediaContext);
extern void mediaDeinit(player_t *mediaContext);
extern void localPacketReadThread(void *arg);
extern void localVideoPacketDecodeThread(void * arg);
extern void localVideoFrameShowThread(void * arg);
extern void localAudioPacketPlayThread(void * arg);
extern void msleep(int ms);
extern void pcmClose(player_t *mediaContext);
extern int initPcmDevice(player_t *mediaContext);
extern void audioParamInit(player_t *mediaContext, AVCodecContext	*audioCodecCtx,AVCodec	*pCodecAudio);
#endif
