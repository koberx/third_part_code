#include <X11/Xlib.h>
#include <X11/Xutil.h>      /* BitmapOpenFailed, etc. */
#include <sys/shm.h>
#include <X11/extensions/XShm.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <stdio.h>

#include <sys/time.h>

int main(int argc,char *argv[])
{
#ifdef USE_OUTPUT_IMAGE
    Display *display = XOpenDisplay(getenv("DISPLAY"));
    Window              win;
    XImage *ximage;
#endif
    int                 ret;
    AVFormatContext     *pFormatCtx;
    AVCodec             *pCodec;
    AVCodecContext      *videoCodecCtx;
    AVPacket            *packet;
    AVFrame             *pFrameYUV;
    AVFrame             *pFrame;
    struct SwsContext   *img_convert_ctx;
    int                 video_index;
    unsigned char       *video_out_buffer = NULL;
    int                 got_picture; 
    struct timeval      beginTv;
    struct timeval      endTv;
    
    avcodec_register_all();
    av_register_all();

#ifdef USE_OUTPUT_IMAGE
    if (display == NULL) {
        printf("[rxhu] Can't open the xserver\n");
        return -1;
    }    
    win = XCreateSimpleWindow(display, RootWindow(display, DefaultScreen(display)),
                0, 0, 1, 1, 0,
                BlackPixel(display, DefaultScreen(display)),
                BlackPixel(display, DefaultScreen(display)));

    XMapWindow(display, win);
    XSync(display, False);
#endif
    pFormatCtx = avformat_alloc_context();
    pFrame = av_frame_alloc(); 
    pFrameYUV = av_frame_alloc();
    packet= (AVPacket *)av_malloc(sizeof(AVPacket));

    if (avformat_open_input(&pFormatCtx,argv[1],NULL,NULL)!=0){
        printf("Couldn't open input stream.\n");         
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx,NULL)<0){// find stream 
        printf("Couldn't find stream information.");          
        return -1; 
    }

    video_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, video_index, -1, NULL, 0); 
    
    if (video_index >= 0) {
        videoCodecCtx = pFormatCtx->streams[video_index]->codec;
        pCodec = avcodec_find_decoder(videoCodecCtx->codec_id);
        if (pCodec == NULL) {
            printf("codec not found\n");
            return -1;
        }

        if (avcodec_open2(videoCodecCtx, pCodec, NULL) < 0) {
            printf("can't open the videoCodecCtx\n");
            return -1;
        }

        video_out_buffer = (unsigned char *)av_malloc(avpicture_get_size(AV_PIX_FMT_RGB32, videoCodecCtx->width, videoCodecCtx->height));
        avpicture_fill((AVPicture *)pFrameYUV, (const uint8_t *)video_out_buffer, AV_PIX_FMT_RGB32, videoCodecCtx->width, videoCodecCtx->height);
        img_convert_ctx = sws_getContext(videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt, 
            videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
        
    }

    av_dump_format(pFormatCtx,0, 0, 0);
#ifdef USE_OUTPUT_IMAGE
    XResizeWindow(display, win, videoCodecCtx->width, videoCodecCtx->height);
    XSync(display, False);
#endif
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == video_index) {
            gettimeofday(&beginTv, NULL);
            ret = avcodec_decode_video2(videoCodecCtx, pFrame, &got_picture, packet);
            if (ret < 0) {
                printf("Decode Error\n");
                return -1;
            }
            if (got_picture) {
                sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, videoCodecCtx->height, pFrameYUV->data, pFrameYUV->linesize);
#ifdef USE_OUTPUT_IMAGE
                ximage = XCreateImage(display, DefaultVisual(display, DefaultScreen(display)),
                    DefaultDepth(display, DefaultScreen(display)), ZPixmap, 0, pFrameYUV->data[0], pFrame->width, pFrame->height, 8, pFrameYUV->linesize[0]);
                XPutImage (display, win, DefaultGC(display, 0), ximage, 0, 0, 0, 0, pFrame->width, pFrame->height);
                XFlush(display);
#endif
                gettimeofday(&endTv, NULL);
                printf("[rxhu] decoded spend the time %ld ms\n",((endTv.tv_sec * 1000 + endTv.tv_usec / 1000) - (beginTv.tv_sec * 1000 + beginTv.tv_usec / 1000)));
            }
        }
        av_free_packet(packet);
    }
#ifdef USE_OUTPUT_IMAGE
    XDestroyImage(ximage);
#endif
    sws_freeContext(img_convert_ctx);
    av_frame_free(&pFrameYUV);
    av_frame_free(&pFrame);
    avcodec_close(videoCodecCtx);
    avformat_close_input(&pFormatCtx);

    return 0;
}




