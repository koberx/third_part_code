#include <X11/Xlib.h>
#include <X11/Xutil.h>      /* BitmapOpenFailed, etc. */
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <libavformat/avformat.h>  
#include <libavcodec/avcodec.h>  
#include <libavutil/avutil.h>  
#include <libswscale/swscale.h>  
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#include <stdbool.h>
#include "ffmpeg.h"
#include "sysdeps.h"
#include "vaapi_compat.h"
#include "vaapi.h"
#include "image.h"
#include "linuxList.h"
#include "threadPool.h"
#include <sys/time.h>

typedef struct {
	struct list_head    list;
	VAImage             vaImageData;
	uint8_t            *data;
	bool				image_inited;
}vaframe_queue_t;

typedef struct {
	Display *display;
	Window win;
	AVFormatContext *pFormatCtx;
	AVCodecContext *pCodecCtx;
	AVCodec *pCodec;
	XImage *ximage;
	AVFrame *pFrame;
	pthread_mutex_t  image_queue_mtx;
	pthread_cond_t   image_queue_cond;
	int image_queue_size;
	int videoStream;
	struct list_head image_queue_head;
	struct list_head image_empty_head;
	bool 			 decodeThreadExit;
}ffmpeg_decode_t;

VAImageFormat *image_format = NULL;

void imageShow(ffmpeg_decode_t *decoder, vaframe_queue_t *vaframe)
{
	 decoder->ximage = XCreateImage(decoder->display, DefaultVisual(decoder->display, DefaultScreen(decoder->display)),
                    DefaultDepth(decoder->display, DefaultScreen(decoder->display)), ZPixmap, 0, vaframe->data, decoder->pCodecCtx->width, 
                    decoder->pCodecCtx->height, 8, vaframe->vaImageData.pitches[0]);
                XPutImage (decoder->display,decoder->win, DefaultGC(decoder->display, 0), decoder->ximage, 
					0, 0, 0, 0, decoder->pCodecCtx->width, decoder->pCodecCtx->height);
                XFlush(decoder->display);  
}

VAImage *get_decoded_data(ffmpeg_decode_t *decoder, VASurfaceID surface, VAImageFormat *image_format)
{
	vaframe_queue_t *data_pos, *n;
	void *p_base = NULL;
	int ret;
	pthread_mutex_lock (&decoder->image_queue_mtx);
	while(list_empty (&decoder->image_empty_head)) {
		pthread_cond_wait(&decoder->image_queue_cond, &decoder->image_queue_mtx);
	}
	list_for_each_entry_safe(data_pos, n, &decoder->image_empty_head, list) {
        list_del(&data_pos->list);
        decoder->image_queue_size--;
        break;
    }
	pthread_mutex_unlock (&decoder->image_queue_mtx);

	data_pos->image_inited = false;
	if (create_rgbx_image(&data_pos->vaImageData, image_format) != 0) {
		printf("[rxhu] create_rgbx_image failed\n");
		return NULL;
	}

	if (data_pos->image_inited == false) {
		ret = get_rgbx_picture(surface, &data_pos->vaImageData, &data_pos->data ,image_format);
		if (ret < 0) {
			printf("[rxhu] get_rgbx_picture failed!\n");
			return NULL;
		}
		data_pos->image_inited = true;
	}

	return &data_pos->vaImageData;
	
}

void put_decoded_data_list(ffmpeg_decode_t *decoder, VAImage *vaDecImage) 
{
	vaframe_queue_t *data_pos;
	data_pos = list_entry(vaDecImage, vaframe_queue_t, vaImageData);
	pthread_mutex_lock (&decoder->image_queue_mtx);
	list_add_tail(&data_pos->list, &decoder->image_queue_head);
	pthread_cond_broadcast(&decoder->image_queue_cond);
	pthread_mutex_unlock (&decoder->image_queue_mtx);
}

void put_empty_data_list(ffmpeg_decode_t *decoder, VAImage *vaDecImage)
{
	vaframe_queue_t *data_pos;
	data_pos = list_entry(vaDecImage, vaframe_queue_t, vaImageData);
	pthread_mutex_lock (&decoder->image_queue_mtx);
	list_add_tail(&data_pos->list, &decoder->image_empty_head);
	pthread_cond_broadcast(&decoder->image_queue_cond);
	pthread_mutex_unlock (&decoder->image_queue_mtx);
}

void decoder_thread(void *arg)
{
	AVPacket packet; 
	int frameFinished;
	VAImage *vaDecImage;
	ffmpeg_decode_t *decoder = (ffmpeg_decode_t *)arg;
	while(av_read_frame(decoder->pFormatCtx, &packet) >= 0) {
		if(packet.stream_index == decoder->videoStream) {
			avcodec_decode_video2(decoder->pCodecCtx, decoder->pFrame, &frameFinished, &packet);  
			if(frameFinished) {
				vaDecImage = get_decoded_data(decoder, (VASurfaceID)(uintptr_t)decoder->pFrame->data[3], image_format);
				if (vaDecImage == NULL) {
					printf("[rxhu] get_empty_data failed!\n");
					exit(1);
				}
				put_decoded_data_list(decoder, vaDecImage);
			}
		} else {
			printf("[rxhu] no frame decoded\n");
		}
		av_free_packet(&packet);
	}
	decoder->decodeThreadExit = true;
}

void image_unmap(VAImage *image)
{
    VAStatus status;
    VAAPIContext * const vaapi = vaapi_get_context();
    status = vaUnmapBuffer(vaapi->display, image->buf);
    if (!vaapi_check_status(status, "vaUnmapBuffer()"))
        exit(-1);
     if (image->image_id != VA_INVALID_ID) {
        status = vaDestroyImage(vaapi->display, image->image_id);
        if (!vaapi_check_status(status, "vaDestroyImage()"))
           exit(-1);
    }
}

void image_destroy(VAImage *image)
{
	VAStatus status;
	VAAPIContext * const vaapi = vaapi_get_context();
	if (image->image_id != VA_INVALID_ID) {
        status = vaDestroyImage(vaapi->display, image->image_id);
        if (!vaapi_check_status(status, "vaDestroyImage()"))
            exit(-1);
    }
}

void image_show_thread(void *arg)
{
	vaframe_queue_t *data_pos, *n;
	ffmpeg_decode_t *decoder = (ffmpeg_decode_t *)arg;
	
	while (decoder->decodeThreadExit == false) {
		pthread_mutex_lock (&decoder->image_queue_mtx);
		while (list_empty(&decoder->image_queue_head)) {
			pthread_cond_wait(&decoder->image_queue_cond, &decoder->image_queue_mtx);
		}
		list_for_each_entry_safe(data_pos, n, &decoder->image_queue_head, list) {
	        list_del(&data_pos->list);
	        decoder->image_queue_size--;
	        break;
    	}
		pthread_mutex_unlock(&decoder->image_queue_mtx);
		/*show*/
		imageShow(decoder, data_pos);
		/*取消映射，并进入空队列*/
		data_pos->image_inited = false;
		image_unmap(&data_pos->vaImageData);
		put_empty_data_list(decoder, &data_pos->vaImageData);
	}	

	if (!list_empty(&decoder->image_queue_head)) {
		list_for_each_entry_safe(data_pos, n, &decoder->image_queue_head, list) {
	        list_del(&data_pos->list);
	        decoder->image_queue_size--;
	        /*show*/
			imageShow(decoder, data_pos);
			data_pos->image_inited = false;
			image_unmap(&data_pos->vaImageData);
			put_empty_data_list(decoder, &data_pos->vaImageData);
    	}
	}

	if (!list_empty(&decoder->image_empty_head)) {
		list_for_each_entry_safe(data_pos, n, &decoder->image_empty_head, list) {
			list_del(&data_pos->list);
			image_destroy(&data_pos->vaImageData);
		}
	}
	
}

int main(int argc, const char *argv[]) 
{   
    int i;  
    struct timeval beginTv;
    struct timeval endTv;
	 
	vaframe_queue_t *buf_queue;
	ffmpeg_decode_t *decoder;
	decoder = (ffmpeg_decode_t *)malloc(sizeof(ffmpeg_decode_t));
	if (decoder == NULL) {
		printf("[rxhu] malloc ffmpeg_decode_t failed\n");
		return -1;
	}
/*init the x11 display*/
    decoder->display = XOpenDisplay(getenv("DISPLAY")); 
    decoder->win = XCreateSimpleWindow(decoder->display, RootWindow(decoder->display, DefaultScreen(decoder->display)),
            0, 0, 1, 1, 0,
            BlackPixel(decoder->display, DefaultScreen(decoder->display)),
            BlackPixel(decoder->display, DefaultScreen(decoder->display)));

    XMapWindow(decoder->display, decoder->win);

    XSync(decoder->display, False);

    av_register_all();
    ffmpeg_vaapi_init(decoder->display);
    if(avformat_open_input(&decoder->pFormatCtx, argv[1], NULL, NULL) != 0) {  
        return -1;  
    }  
    if(avformat_find_stream_info(decoder->pFormatCtx, NULL) < 0) {  
        return -1;  
    }  
    av_dump_format(decoder->pFormatCtx, -1, argv[1], 0);  
    decoder->videoStream = -1;  
    for(i=0; i < decoder->pFormatCtx->nb_streams; i++)  
        if(decoder->pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {  
            decoder->videoStream = i;  
            break;  
        }  
    if(decoder->videoStream == -1) {  
        return -1;  
    }  
    decoder->pCodecCtx = decoder->pFormatCtx->streams[decoder->videoStream]->codec;
    printf("refs %d\n", decoder->pCodecCtx->refs);

    ffmpeg_init_context(decoder->pCodecCtx);
    decoder->pCodec = avcodec_find_decoder(decoder->pCodecCtx->codec_id);  
    printf("refs %d\n", decoder->pCodecCtx->refs);
    printf("codec name %s\n", decoder->pCodec->name);

    if(decoder->pCodec == NULL) {  
        return -1;  
    }  
    if(avcodec_open2(decoder->pCodecCtx, decoder->pCodec, NULL) < 0) {  
        return -1;  
    }  
    printf("refs %d\n",decoder->pCodecCtx->refs);
    decoder->pFrame = av_frame_alloc();  
    if(decoder->pFrame == NULL) {  
        return -1;  
    }  

    printf("compress level:%d\n", decoder->pCodecCtx->compression_level);

    XResizeWindow(decoder->display, decoder->win, decoder->pCodecCtx->width, decoder->pCodecCtx->height);
    XSync(decoder->display, False);
    set_image_format(VA_FOURCC_BGRX, &image_format);
    /*初始化各个队列头*/
	INIT_LIST_HEAD(&decoder->image_queue_head);
    	INIT_LIST_HEAD(&decoder->image_empty_head);
	decoder->image_queue_size = 0;
	decoder->decodeThreadExit = false;
/*初始化队列*/
	for(i = 0; i < NUM_VIDEO_SURFACES_H264; i++) {
		buf_queue = (vaframe_queue_t *)malloc(sizeof(vaframe_queue_t));
		if(buf_queue == NULL) {
			printf("[rxhu] initalized the queue failed\n");
			return -1;
		}
	//	buf_queue->image_inited = false;
	//	if (create_rgbx_image(&buf_queue->vaImageData, image_format) != 0) {
	//		printf("[rxhu] create_rgbx_image failed\n");
	//		return -1;
	//	}
		list_add_tail(&buf_queue->list, &decoder->image_empty_head);
	}
	decoder->decodeThreadExit = false;
/*创建解码线程和显示线程*/
	threadpool thpool = thpool_init(4);
	thpool_add_work(thpool, (void *)(decoder_thread), decoder);
	thpool_add_work(thpool, (void *)(image_show_thread), decoder);
    printf("[rxhu] init ok please check!\n");
	thpool_wait(thpool);
	thpool_destroy(thpool);
	av_free(decoder->pFrame);
    ffmpeg_vaapi_exit();
    avcodec_close(decoder->pCodecCtx);  
    avformat_close_input(&decoder->pFormatCtx);
	free(decoder);
    return 0;
}



