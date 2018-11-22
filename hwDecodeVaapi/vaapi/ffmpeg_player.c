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
#include <sys/time.h>


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
    uint8_t *RGBAbuffer; 
	AVPacket packet;  
	int frameFinished;  
	int numBytes;  
    struct SwsContext *sws_convert;  //add rxhu
    XImage *ximage;  // add rxhu
    struct timeval beginTv;
    struct timeval endTv;
    FILE *pFile;
	i = 0;  

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

	printf("compress level:%d\n", pCodecCtx->compression_level);

	numBytes = avpicture_get_size(AV_PIX_FMT_RGB32, pCodecCtx->width, pCodecCtx->height);  

    printf("[rxhu] numBytes= %d\n", numBytes);
	XResizeWindow(display, win, pCodecCtx->width, pCodecCtx->height);
	XSync(display, False);

    pFile = fopen("output.yuv","w+");
    Image *image = image_create(pCodecCtx->width, pCodecCtx->height, IMAGE_BGRA, NULL);
    if (!image) {
        printf("image create error\n");
        return -1;
    }

	while(av_read_frame(pFormatCtx, &packet) >=0) {  
		if(packet.stream_index == videoStream) {  
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);  
			if(frameFinished) {
                gettimeofday(&beginTv,NULL);
                get_image((VASurfaceID)(uintptr_t)pFrame->data[3], image, pFile); 
                ximage = XCreateImage(display, DefaultVisual(display, DefaultScreen(display)),
                    DefaultDepth(display, DefaultScreen(display)), ZPixmap, 0, image->pixels[0], pFrame->width, pFrame->height, 8, image->pitches[0]);
                XPutImage (display, win, DefaultGC(display, 0), ximage, 0, 0, 0, 0, pFrame->width, pFrame->height);
                XFlush(display);
                gettimeofday(&endTv, NULL);
                printf("[rxhu] decoded spend the time %ld ms\n",((endTv.tv_sec * 1000 + endTv.tv_usec / 1000) - (beginTv.tv_sec * 1000 + beginTv.tv_usec / 1000)));
		    } else {
		        printf("no frame decoded\n");
		    }
		}  
		av_free_packet(&packet);  
	}  
    fclose(pFile);
    XDestroyImage(ximage);
	av_free(pFrame);
	ffmpeg_vaapi_exit();
	avcodec_close(pCodecCtx);  
	avformat_close_input(&pFormatCtx);  
	return 0;  
}  
