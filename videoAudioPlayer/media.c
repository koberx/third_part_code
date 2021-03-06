/* thread */
#include "media.h"

void localPacketReadThread(void *arg)
{
	player_t    *mediaContext = (player_t *)arg;
	AVPacket *packet = av_packet_alloc();
	 while (av_read_frame(mediaContext->pFormatCtx, packet) >= 0) {
	 	if (packet->stream_index == mediaContext->videoStream.video_index) {
			if (video_packet_enque(mediaContext, packet) == false) {
				printf("[rxhu] video_packet_enque failed!\n");
				break;
			}
			av_packet_unref(packet);
		} else if (packet->stream_index == mediaContext->audioStream.audio_index) {
			if (audio_packet_enque(mediaContext, packet) == false) {
				printf("[rxhu] audio_packet_enque failed!\n");
				break;
			}
			av_packet_unref(packet);
		} else {
			av_packet_unref(packet);
		}		
	 }
	 printf("[rxhu] localPacketReadThread readThreadExit\n");
	 pthread_mutex_lock(&mediaContext->videoStream.packetQueueMtx);
	 mediaContext->readThreadExit = true;
	 pthread_cond_broadcast(&mediaContext->videoStream.packetQueueCond);
	 pthread_mutex_unlock(&mediaContext->videoStream.packetQueueMtx);

	 pthread_mutex_lock(&mediaContext->audioStream.packetQueueMtxAudio);
	 mediaContext->readThreadExit = true;
	 pthread_cond_broadcast(&mediaContext->audioStream.packetQueueCondAudio);
	 pthread_mutex_unlock(&mediaContext->audioStream.packetQueueMtxAudio);
	 av_packet_free(&packet);
}

void localVideoPacketDecodeThread(void * arg)
{
	player_t    *mediaContext = (player_t *)arg;
	AVPacket 	packet;
	AVFrame     *pFrame = av_frame_alloc();
	int ret;
	int got_picture;
	while (true) {
		if (video_packet_deque(mediaContext, &packet) == false) {
			printf("[rxhu] video_packet_deque failed!\n");
			break;
		}
		ret = avcodec_decode_video2(mediaContext->videoStream.videoCodecCtx, pFrame, &got_picture, &packet);
		if (ret < 0) {
			printf("[rxhu] Decode Error\n");
			break;
		}
		if (got_picture) {
			if (video_frame_enque(mediaContext, pFrame) == false) {
				printf("[rxhu] video_frame_enque failed!\n");
				break;
			}
			av_frame_unref(pFrame);
		}
	}
	pthread_mutex_lock(&mediaContext->videoStream.frameQueueMtx);
	mediaContext->decThreadExit = true;
	pthread_cond_broadcast(&mediaContext->videoStream.frameQueueCond);
    pthread_mutex_unlock(&mediaContext->videoStream.frameQueueMtx);	
}

void localVideoFrameShowThread(void * arg)
{
	player_t    *mediaContext = (player_t *)arg;
	frameData_t *frameData;
	AVFrame     *pFrameRgb;
	while (true) {
		frameData = video_frame_deque(mediaContext);
		if (frameData == NULL) {
			printf("[rxhu] video_frame_deque failed\n");
			break;
		}
		pFrameRgb = frameData->frame;
		sws_scale(mediaContext->videoStream.img_convert_ctx, (const uint8_t* const*)pFrameRgb->data, // 转码
			pFrameRgb->linesize, 0, mediaContext->videoStream.videoCodecCtx->height, 
			mediaContext->videoStream.displayFrame->data, mediaContext->videoStream.displayFrame->linesize);

		mediaContext->x11display.ximage = XCreateImage(mediaContext->x11display.display, //创建Ximage
            DefaultVisual(mediaContext->x11display.display, DefaultScreen(mediaContext->x11display.display)),
            DefaultDepth(mediaContext->x11display.display, DefaultScreen(mediaContext->x11display.display)), ZPixmap,
            0, mediaContext->videoStream.displayFrame->data[0], pFrameRgb->width, pFrameRgb->height, 8,
            mediaContext->videoStream.displayFrame->linesize[0]);		
		if (mediaContext->x11display.ximage == NULL) {
			printf("[rxhu] XCreateImage failed!\n");
			/*释放*/
			av_frame_free(&frameData->frame);
			free(frameData);
			break;
		}
		
		XPutImage (mediaContext->x11display.display, mediaContext->x11display.win, DefaultGC(mediaContext->x11display.display, 0),  // 显示
            mediaContext->x11display.ximage, 0, 0, 0, 0, pFrameRgb->width, pFrameRgb->height);
		XFlush(mediaContext->x11display.display);
		
		/*释放*/
		av_frame_free(&frameData->frame);
		free(frameData);
	}
}

void localAudioPacketPlayThread(void * arg)
{
	player_t    *mediaContext = (player_t *)arg;
	AVPacket 	packet;
	AVFrame     *frame = av_frame_alloc();
	int ret;
	int got_frame;
	int out_buffer_size;
	audioParamInit(mediaContext, mediaContext->audioStream.audioCodecCtx, mediaContext->audioStream.pCodecAudio);
    ret = initPcmDevice(mediaContext);
    if (ret < 0) {
    	printf("[rxhu] initPcmDevice init failed!\n");
        return ;
    }

	while (true) {
		if (audio_packet_deque(mediaContext, &packet) == false) {
			printf("[rxhu] audio_packet_deque failed!\n");
			break;
		}
		ret = avcodec_decode_audio4(mediaContext->audioStream.audioCodecCtx, frame, &got_frame, &packet);
        if (ret < 0) {
        	printf("[rxhu] audio decode the frame finish\n");
			break;
        }
		if (got_frame) {
			swr_convert(mediaContext->audioStream.audioSwrContext, &mediaContext->audioStream.out_buffer, 2 * mediaContext->audioStream.out_sample_rate, 
				(const uint8_t **)frame->data, frame->nb_samples);

			out_buffer_size = av_samples_get_buffer_size(NULL, mediaContext->audioStream.out_channel_nb, frame->nb_samples,
                       mediaContext->audioStream.out_sample_fmt, 1);
			if ((ret = snd_pcm_writei(mediaContext->audioStream.pcm, mediaContext->audioStream.out_buffer, out_buffer_size / 4)) < 0) {
				printf("[rxhu] can't send the pcm to the sound card! ret = %d %s\n", ret, snd_strerror(ret));
				break;
			}
		}
		
	}
	av_frame_free(&frame);
}

/* function */
bool audio_packet_enque(player_t *mediaContext, AVPacket *packet)
{
	packetData_t *packetData;
	packetData = (packetData_t *)malloc(sizeof(packetData_t));
    if (packetData == NULL) {
        printf("[rxhu] malloc the packetData_t failed\n");
        return false;
    }
	packetData->packet = av_packet_alloc();
    if (av_packet_ref(packetData->packet, packet) < 0) {
        return false;
    }
	pthread_mutex_lock(&mediaContext->audioStream.packetQueueMtxAudio);
    list_add_tail(&packetData->list, &mediaContext->audioStream.audioPacketQueue);
    mediaContext->audioStream.packetSizeAudio++;
    pthread_cond_broadcast(&mediaContext->audioStream.packetQueueCondAudio);
    pthread_mutex_unlock(&mediaContext->audioStream.packetQueueMtxAudio);
    return true;
}

bool audio_packet_deque(player_t * mediaContext, AVPacket * packet)
{
	packetData_t *data_pos, *n;
    pthread_mutex_lock(&mediaContext->audioStream.packetQueueMtxAudio);
    while(list_empty (&mediaContext->audioStream.audioPacketQueue)) {
        if (mediaContext->readThreadExit == true) {
            printf("[rxhu] audio_packet_deque get readThreadExit signal\n");
            return false;
        } else {
            pthread_cond_wait(&mediaContext->audioStream.packetQueueCondAudio, &mediaContext->audioStream.packetQueueMtxAudio);
       }
    }
    list_for_each_entry_safe(data_pos, n, &mediaContext->audioStream.audioPacketQueue, list) {
        list_del(&data_pos->list);
        mediaContext->audioStream.packetSizeAudio--;
        break;
    }
    pthread_mutex_unlock(&mediaContext->audioStream.packetQueueMtxAudio);
    if (av_packet_ref(packet, data_pos->packet) < 0) {
        return false;
    }
    av_packet_free(&data_pos->packet);
    free(data_pos);
    return true;
}

bool video_packet_enque(player_t *mediaContext, AVPacket *packet)
{
    packetData_t *packetData;
    packetData = (packetData_t *)malloc(sizeof(packetData_t));
    if (packetData == NULL) {
        printf("[rxhu] malloc the packetData_t failed\n");
        return false;
    }
    packetData->packet = av_packet_alloc();
    if (av_packet_ref(packetData->packet, packet) < 0) {
        return false;
    }
    pthread_mutex_lock(&mediaContext->videoStream.packetQueueMtx);
    list_add_tail(&packetData->list, &mediaContext->videoStream.videoPacketQueue);
    mediaContext->videoStream.packetSize++;
    pthread_cond_broadcast(&mediaContext->videoStream.packetQueueCond);
    pthread_mutex_unlock(&mediaContext->videoStream.packetQueueMtx);
    return true;
}

bool video_packet_deque(player_t *mediaContext, AVPacket *packet)
{
    packetData_t *data_pos, *n;
    pthread_mutex_lock(&mediaContext->videoStream.packetQueueMtx);
    while(list_empty (&mediaContext->videoStream.videoPacketQueue)) {
        if (mediaContext->readThreadExit == true) {
            printf("[rxhu] video_packet_deque readThreadExit\n");
            return false;
        } else {
            pthread_cond_wait(&mediaContext->videoStream.packetQueueCond, &mediaContext->videoStream.packetQueueMtx);
       }
    }
    list_for_each_entry_safe(data_pos, n, &mediaContext->videoStream.videoPacketQueue, list) {
        list_del(&data_pos->list);
        mediaContext->videoStream.packetSize--;
        break;
    }
    pthread_mutex_unlock(&mediaContext->videoStream.packetQueueMtx);
    if (av_packet_ref(packet, data_pos->packet) < 0) {
        return false;
    }
    av_packet_free(&data_pos->packet);
    free(data_pos);
    return true;
}

bool video_frame_enque(player_t *mediaContext, AVFrame *frame)
{
    frameData_t *frameData;
    int ret;
    frameData = (frameData_t *)malloc(sizeof(frameData_t));
    if (frameData == NULL) {
        printf("[rxhu] malloc the frameData_t failed\n");
        return false;
    }
    frameData->frame = av_frame_alloc();
    ret = av_frame_ref(frameData->frame, frame);
    if (ret < 0) {
		printf("[rxhu] video_frame_enque av_frame_ref failed\n");
        return false;
    }
    pthread_mutex_lock(&mediaContext->videoStream.frameQueueMtx);
    list_add_tail(&frameData->list, &mediaContext->videoStream.videoFrameQueue);
    mediaContext->videoStream.frameSize++;
    pthread_cond_broadcast(&mediaContext->videoStream.frameQueueCond);
    pthread_mutex_unlock(&mediaContext->videoStream.frameQueueMtx);
    return true;
}

frameData_t *video_frame_deque(player_t *mediaContext)
{
    frameData_t *data_pos, *n;
    pthread_mutex_lock(&mediaContext->videoStream.frameQueueMtx);
    while(list_empty (&mediaContext->videoStream.videoFrameQueue)) {
        if (mediaContext->decThreadExit == true) {
            printf("[rxhu] video_frame_deque decThreadExit\n");
            return NULL;
        } else {
            pthread_cond_wait(&mediaContext->videoStream.frameQueueCond, &mediaContext->videoStream.frameQueueMtx);
        }
    }
    list_for_each_entry_safe(data_pos, n, &mediaContext->videoStream.videoFrameQueue, list) {
        list_del(&data_pos->list);
        mediaContext->videoStream.frameSize--;
        break;
    }
    pthread_mutex_unlock(&mediaContext->videoStream.frameQueueMtx);
    return data_pos;
}



int initDisplay(player_t *mediaContext)
{
    mediaContext->x11display.display = XOpenDisplay(getenv("DISPLAY"));
	if (mediaContext->x11display.display == NULL) {
			printf("[rxhu] Can't open the xserver\n");
			return -1;
	}
    mediaContext->x11display.win = XCreateSimpleWindow(mediaContext->x11display.display, RootWindow(mediaContext->x11display.display, DefaultScreen(mediaContext->x11display.display)),
                0, 0, 1, 1, 0,
                BlackPixel(mediaContext->x11display.display, DefaultScreen(mediaContext->x11display.display)),
                BlackPixel(mediaContext->x11display.display, DefaultScreen(mediaContext->x11display.display)));  
    XMapWindow(mediaContext->x11display.display, mediaContext->x11display.win);
    XSync(mediaContext->x11display.display, False);
}

void displayResize(player_t *mediaContext) 
{
    XResizeWindow(mediaContext->x11display.display, mediaContext->x11display.win, mediaContext->videoStream.videoCodecCtx->width, mediaContext->videoStream.videoCodecCtx->height);
    XSync(mediaContext->x11display.display, False);
}

void mediaDeinit(player_t *mediaContext)
{
    //XDestroyImage(mediaContext->x11display.ximage);
    sws_freeContext(mediaContext->videoStream.img_convert_ctx);
	swr_free(&mediaContext->audioStream.audioSwrContext);
    av_frame_free(&mediaContext->videoStream.displayFrame);
    avcodec_close(mediaContext->videoStream.videoCodecCtx);
	avcodec_close(mediaContext->audioStream.audioCodecCtx);
	av_free(mediaContext->audioStream.out_buffer);
    XCloseDisplay(mediaContext->x11display.display);
	pcmClose(mediaContext);
    avformat_close_input(&mediaContext->pFormatCtx);
}

void msleep(int ms)
{
	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 1000 * ms;
	select(1, NULL, NULL, NULL, &timeout);
	return;
}

int openInput(player_t *mediaContext, char *mediaFile)
{
	int                 ret;
    AVFormatContext     *pFormatCtx;
    AVCodec             *pCodec;
    AVCodecContext      *videoCodecCtx;
    AVFrame             *pFrameRGB;
    struct SwsContext   *img_convert_ctx;
    int                 video_index;
	int					audio_index;
    unsigned char       *video_out_buffer = NULL; 

	AVCodec				*pCodecAudio;
	AVCodecContext		*audioCodecCtx;

	int i;

	pFormatCtx = avformat_alloc_context();
    pFrameRGB = av_frame_alloc();

    if (avformat_open_input(&pFormatCtx,mediaFile,NULL,NULL)!=0){
        printf("Couldn't open input stream.\n");         
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx,NULL)<0){// find stream 
        printf("Couldn't find stream information.");          
        return -1; 
    }

    video_index = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, video_index, -1, NULL, 0); // video stream
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_index = i;
            break;
        }
    }
    
    if (video_index >= 0) {  // video stream init
        videoCodecCtx = pFormatCtx->streams[video_index]->codec;
        pCodec = avcodec_find_decoder(videoCodecCtx->codec_id);
        if (pCodec == NULL) {
            printf("[rxhu] video codec not found\n");
            return -1;
        }

        if (avcodec_open2(videoCodecCtx, pCodec, NULL) < 0) {
            printf("[rxhu] can't open the videoCodecCtx\n");
            return -1;
        }

        video_out_buffer = (unsigned char *)av_malloc(avpicture_get_size(AV_PIX_FMT_RGB32, videoCodecCtx->width, videoCodecCtx->height));
        avpicture_fill((AVPicture *)pFrameRGB, (const uint8_t *)video_out_buffer, AV_PIX_FMT_RGB32, videoCodecCtx->width, videoCodecCtx->height);
        img_convert_ctx = sws_getContext(videoCodecCtx->width, videoCodecCtx->height, videoCodecCtx->pix_fmt, 
            videoCodecCtx->width, videoCodecCtx->height, AV_PIX_FMT_RGB32, SWS_BICUBIC, NULL, NULL, NULL);
        
    }

	if (audio_index >= 0) {  // audio stream init
		audioCodecCtx = pFormatCtx->streams[audio_index]->codec;
		pCodecAudio = avcodec_find_decoder(audioCodecCtx->codec_id);
		if (pCodecAudio == NULL) {
			printf("[rxhu] audio codec not found!\n");
			return -1;
		}

		if (avcodec_open2(audioCodecCtx, pCodecAudio, NULL) < 0) {
			printf("[rxhu] can't open the audioCodecCtx \n");
			return -1;
		}
	}

    av_dump_format(pFormatCtx,0, 0, 0);
	
	mediaContext->videoStream.pCodecVideo = pCodec;
	mediaContext->videoStream.videoCodecCtx = videoCodecCtx;
	mediaContext->videoStream.img_convert_ctx = img_convert_ctx;
	mediaContext->videoStream.video_index = video_index;
	mediaContext->audioStream.audio_index = audio_index;
	mediaContext->pFormatCtx = pFormatCtx;
	mediaContext->videoStream.displayFrame = pFrameRGB;
	mediaContext->audioStream.audioCodecCtx = audioCodecCtx;
	mediaContext->audioStream.pCodecAudio = pCodecAudio;

	return 0;
}

void audioParamInit(player_t *mediaContext, AVCodecContext	*audioCodecCtx,AVCodec	*pCodecAudio)
{
	mediaContext->audioStream.audioSwrContext = swr_alloc();
	mediaContext->audioStream.in_sample_fmt = audioCodecCtx->sample_fmt;
	mediaContext->audioStream.out_sample_fmt = AV_SAMPLE_FMT_S16;
	mediaContext->audioStream.in_sample_rate = audioCodecCtx->sample_rate;
	mediaContext->audioStream.out_sample_rate = audioCodecCtx->sample_rate;
	mediaContext->audioStream.in_ch_layout = audioCodecCtx->channel_layout;
	mediaContext->audioStream.out_ch_layout = audioCodecCtx->channel_layout;

	swr_alloc_set_opts(mediaContext->audioStream.audioSwrContext, mediaContext->audioStream.out_ch_layout, 
		mediaContext->audioStream.out_sample_fmt, mediaContext->audioStream.out_sample_rate, mediaContext->audioStream.in_ch_layout, 
		mediaContext->audioStream.in_sample_fmt, mediaContext->audioStream.in_sample_rate, 0, NULL);
	swr_init(mediaContext->audioStream.audioSwrContext);
	mediaContext->audioStream.out_channel_nb = av_get_channel_layout_nb_channels(mediaContext->audioStream.out_ch_layout);
	mediaContext->audioStream.out_buffer = (uint8_t *) av_malloc(2 * mediaContext->audioStream.out_sample_rate);

}

int initPcmDevice(player_t *mediaContext)
{
	int dir;
    int ret;

    /*init pcm snd device*/
    ret = snd_pcm_open(&mediaContext->audioStream.pcm, "hw:0,0", SND_PCM_STREAM_PLAYBACK, 0);
    if (ret < 0) {
        printf("[rxhu] open PCM device failed\n");
        return -1;
    }
    snd_pcm_hw_params_alloca(&mediaContext->audioStream.params);
    ret = snd_pcm_hw_params_any(mediaContext->audioStream.pcm, mediaContext->audioStream.params);
    if (ret < 0) {
        printf("[rxhu] snd_pcm_hw_params_any failed!\n");
        return -1;
    }

    ret = snd_pcm_hw_params_set_access(mediaContext->audioStream.pcm, mediaContext->audioStream.params, SND_PCM_ACCESS_RW_INTERLEAVED);
    if (ret < 0) {
        printf("[rxhu] snd_pcm_hw_params_set_access failed!\n");
        return -1;
    }

    ret = snd_pcm_hw_params_set_format(mediaContext->audioStream.pcm, mediaContext->audioStream.params, SND_PCM_FORMAT_S16_LE);
    if (ret < 0) {
        printf("[rxhu] snd_pcm_hw_params_set_format failed!\n");
        return -1;
    }

    ret = snd_pcm_hw_params_set_channels(mediaContext->audioStream.pcm, mediaContext->audioStream.params, mediaContext->audioStream.out_channel_nb);
    if (ret < 0) {
        printf("[rxhu] snd_pcm_hw_params_set_channels failed!\n");
        return -1;
    }

    ret = snd_pcm_hw_params_set_rate_near(mediaContext->audioStream.pcm, mediaContext->audioStream.params,
            (unsigned int *)(&mediaContext->audioStream.out_sample_rate), &dir);
    if (ret < 0) {
        printf("[rxhu] snd_pcm_hw_params_set_rate_near failed!\n");
        return -1;
    }

    printf("[rxhu] out_sample_rate = %d out_channel_nb = %d\n", mediaContext->audioStream.out_sample_rate, mediaContext->audioStream.out_channel_nb);

	ret = snd_pcm_hw_params(mediaContext->audioStream.pcm, mediaContext->audioStream.params);
    if (ret < 0) {
        printf("[rxhu] snd_pcm_hw_params failed!\n");
        return -1;
    }

    ret = snd_pcm_prepare (mediaContext->audioStream.pcm);
    if (ret < 0) {
        printf("[rxhu] cannot prepare audio interface for use (%s)\n",snd_strerror (ret));
        return -1;
    }

    printf("the pcm device set ok!\n");
	return 0;
	
}

void pcmClose(player_t *mediaContext)
{
	snd_pcm_close(mediaContext->audioStream.pcm);
}

