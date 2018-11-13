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
	fprintf(pFile, "P6\n%d %d\n255\n", width, height);  //ppm 文件头  
	for(y=0; y<height; y++)  
		fwrite(pFrame->data[0] + y * pFrame->linesize[0], 1, width * 3, pFile);  
	fclose(pFile);  
}  

int main(int argc, const char *argv[])  
{  
	Display *display = XOpenDisplay(getenv("DISPLAY"));
	
	/* this variable will store the ID of the newly created window. */
	Window win;
	XImage *ximage;
	XShmSegmentInfo shminfo;

	/* check the number of the default screen for our X server. */
	/* find the ID of the root window of the screen. */
	/* create the window, as specified earlier. */
	win = XCreateSimpleWindow(display, RootWindow(display, DefaultScreen(display)),
			0, 0, 1, 1, 0,
			BlackPixel(display, DefaultScreen(display)),
			BlackPixel(display, DefaultScreen(display)));

    //事实上我们创建窗口并不意味着它将会被立刻显示在屏幕上，在缺省情况下，新建的窗口将不会被映射到屏幕上-它们是不可见的。
    //为了能让我们创建的窗口能被显示到屏幕上，我们使用函数XMapWindow()：
	XMapWindow(display, win);

	//XSync()也刷新所有处于等待状态的请求，接着等待X服务器处理完所有的请求再继续。
	XSync(display, False);

#if 1   
	AVFormatContext *pFormatCtx = NULL;  
	int i, videoStream;  
	AVCodecContext *pCodecCtx;  
	AVCodec *pCodec;  
	AVFrame *pFrame;  
	AVFrame *pFrameRGB;  
	AVPacket packet;  
	int frameFinished;  
	int numBytes;  
	uint8_t *buffer;  
#endif    
	//avcodec_init();
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
	//pCodecCtx->skip_frame = AVDISCARD_NONREF;
	//pCodecCtx->skip_idct = AVDISCARD_DEFAULT;
	//pCodecCtx->skip_loop_filter = AVDISCARD_DEFAULT;
	//pCodecCtx->refs = 1;
#if 0
#ifdef USE_XVBA
	if (AV_CODEC_ID_H264 == pCodecCtx->codec_id)
		pCodec = &ff_h264_xvba_decoder;
	else if (AV_CODEC_ID_VC1 == pCodecCtx->codec_id)
		pCodec = &ff_vc1_xvba_decoder;
	else
#endif
#endif
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
	pFrame = avcodec_alloc_frame();  
	if(pFrame == NULL) {  
		return -1;  
	}  
	pFrameRGB = avcodec_alloc_frame();  
	if(pFrameRGB == NULL) {  
		return -1;  
	}  

	printf("compress level:%d\n", pCodecCtx->compression_level);

	numBytes = avpicture_get_size(PIX_FMT_RGB32, pCodecCtx->width, pCodecCtx->height);  
	//buffer = av_malloc(numBytes);  
	//avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_RGB32, pCodecCtx->width, pCodecCtx->height);  

	XResizeWindow(display, win, pCodecCtx->width, pCodecCtx->height);
	XSync(display, False);

#if 0
	ximage = XShmCreateImage(display, DefaultVisual(display, 0), DefaultDepth(display, 0),
			ZPixmap, 0, &shminfo, pCodecCtx->width, pCodecCtx->height);

	shminfo.shmid = shmget(IPC_PRIVATE, numBytes, IPC_CREAT | 0777);
	if (shminfo.shmid < 0)
		exit(-1);

	shminfo.shmaddr = ximage->data = (unsigned char *)shmat(shminfo.shmid, 0, 0);
	if(shminfo.shmaddr == (char *) -1)
		exit(-1);

	Image *image = image_create(pCodecCtx->width, pCodecCtx->height, IMAGE_BGRA, ximage->data);
	if (!image) {
		printf("image create error\n");
	}

	avpicture_fill((AVPicture *)pFrameRGB, ximage->data, PIX_FMT_RGB32, pCodecCtx->width, pCodecCtx->height);  

	shminfo.readOnly = False;
	XShmAttach(display, &shminfo);
#endif

	i = 0;  
	while(av_read_frame(pFormatCtx, &packet) >=0) {  
		if(packet.stream_index == videoStream) {  
			//printf("pts %ld dts %ld\n", packet.pts, packet.dts);
			avcodec_decode_video2(pCodecCtx, pFrame, &frameFinished, &packet);  
			if(frameFinished) {
				//printf("frame finish %d\n", frameFinished);
				vaapi_display(win, pCodecCtx->width, pCodecCtx->height, (VASurfaceID)(uintptr_t)pFrame->data[3]);
#if 0
				get_image((VASurfaceID)(uintptr_t)pFrame->data[3], image);
				XShmPutImage(display, win, DefaultGC(display, 0), ximage, 0, 0, 0, 0, pCodecCtx->width, pCodecCtx->height, False);
				XFlush(display);
				i++;
#endif
			} else {
				printf("no frame decoded\n");
			}
		}  
		av_free_packet(&packet);  
		//usleep(20000);
	}  
	av_free(buffer);  
	XDestroyImage(ximage);
	av_free(pFrameRGB);  
	av_free(pFrame);
	ffmpeg_vaapi_exit();
	avcodec_close(pCodecCtx);  
	avformat_close_input(&pFormatCtx);  
	return 0;  
}  
