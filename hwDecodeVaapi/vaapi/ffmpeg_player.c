/*    gcc -o videoframe videoframe.c -lavformat -lavcodec -lavutil -lz -lm -lpthread -lswscale  */  
#include <X11/Xlib.h>
#include <X11/Xutil.h>      /* BitmapOpenFailed, etc. */
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <libavformat/avformat.h>  
#include <libavcodec/avcodec.h>  
#include <libavutil/avutil.h>  
#include <libswscale/swscale.h>  
#include <stdio.h>

#include "ffmpeg.h"
#include "sysdeps.h"
#include "vaapi_compat.h"
#include "vaapi.h"
#include "image.h"

static void SaveFrame(AVFrame *pFrame, int width, int height, int iFrame)  
{  
	FILE *pFile;  
	char szFilename[32];  
	int y;  
	sprintf(szFilename, "frame%d.ppm", iFrame);  
	pFile = fopen(szFilename, "wb");  
	if(!pFile)  
		return;  
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);  //ppm
	for(y=0; y<height; y++)  
		fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);  
	fclose(pFile);  
}  

int main(int argc, const char *argv[])  
{  
	Display *display = XOpenDisplay(getenv("DISPLAY"));
	
	/* this variable will store the ID of the newly created window. */
	Window win;
	XShmSegmentInfo shminfo;

	/* check the number of the default screen for our X server. */
	/* find the ID of the root window of the screen. */
	/* create the window, as specified earlier. */
	win = XCreateSimpleWindow(display, RootWindow(display, DefaultScreen(display)),
			0, 0, 1, 1, 0,
			BlackPixel(display, DefaultScreen(display)),
			BlackPixel(display, DefaultScreen(display)));

	XMapWindow(display, win);

	XSync(display, False);

	AVFormatContext *pFormatCtx = NULL;  
	int i, videoStream;  
	AVCodecContext *pCodecCtx;  
	AVCodec *pCodec;  
	AVFrame *pFrame;  
	AVFrame *pFrameRGB;  
	AVPacket packet;  
	int frameFinished;  
	int numBytes;  
	av_register_all();

	ffmpeg_vaapi_init(display);

	if(avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0) {  
		return -1;  
	}  
	if(avformat_find_stream_info(pFormatCtx, NULL) < 0) {  
		return -1;  
	}  
	av_dump_format(pFormatCtx, -1, argv[1], 0);  
	videoStream = -1;  
	for(i=0; i<pFormatCtx->nb_streams; i++)  
		if(pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {  
			videoStream = i;  
			break;  
		}  
	if(videoStream == -1) {  
		return -1;  
	}  
	pCodecCtx = pFormatCtx->streams[videoStream]->codec;
	printf("refs %d\n", pCodecCtx->refs);
	ffmpeg_init_context(pCodecCtx);
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);  
	printf("refs %d\n", pCodecCtx->refs);
	printf("codec name %s\n", pCodec->name);

	if(pCodec == NULL) {  
		return -1;  
	}  
	if(avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {  
		return -1;  
	}  
	printf("refs %d\n", pCodecCtx->refs);
	pFrame = av_frame_alloc();  
	if(pFrame == NULL) {  
		return -1;  
	}  
	pFrameRGB = av_frame_alloc();  
	if(pFrameRGB == NULL) {  
		return -1;  
	}  

	printf("compress level:%d\n", pCodecCtx->compression_level);

	numBytes = avpicture_get_size(AV_PIX_FMT_RGB32, pCodecCtx->width, pCodecCtx->height);  

	XResizeWindow(display, win, pCodecCtx->width, pCodecCtx->height);
	XSync(display, False);

	i = 0;  
	while(av_read_frame(pFormatCtx, &packet) >=0) {  
		if(packet.stream_index == videoStream) {  
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);  
			if(frameFinished) {
				vaapi_display(win, pCodecCtx->width, pCodecCtx->height, (VASurfaceID)(uintptr_t)pFrame->data[3]);
			} else {
				printf("no frame decoded\n");
			}
		}  
		av_free_packet(&packet);  
	}  
	av_free(pFrameRGB);  
	av_free(pFrame);
	ffmpeg_vaapi_exit();
	avcodec_close(pCodecCtx);  
	avformat_close_input(&pFormatCtx);  
	return 0;  
}  
