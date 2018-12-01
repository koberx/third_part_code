#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include <libswresample/swresample.h>
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdbool.h>


int main (int argc[], char *argv[])
{
	AVFormatContext 		*pFormatCtx;
	snd_pcm_t				*pcm;
	snd_pcm_hw_params_t		*params;
	int audio_stream_idx = -1;
    int i = 0;
	AVCodecContext *pCodeCtx;
	AVCodec *pCodec;
	AVPacket *packet;
	AVFrame *frame;
	SwrContext *swrCtx;
	enum AVSampleFormat in_sample_fmt;
	enum AVSampleFormat out_sample_fmt;
	int in_sample_rate;
	int out_sample_rate;
	uint64_t in_ch_layout;
	uint64_t out_ch_layout;
	int out_channel_nb;
	uint8_t *out_buffer;
	int dir;
	int ret, got_frame, framecount = 0;
	int out_buffer_size;
	
    av_register_all();
    
    pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, argv[1], NULL, NULL) != 0) {
        printf("[rxhu] open the media file failed!\n");
        return -1;
    }
    
    if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
        printf("[rxhu] get the media info failed!\n");
        return -1;
    }

    for (i = 0; i < pFormatCtx->nb_streams; i++) {
        if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_AUDIO) {
            audio_stream_idx = i;
            break;
        }
    }
       
    pCodeCtx = pFormatCtx->streams[audio_stream_idx]->codec;
    
    pCodec = avcodec_find_decoder(pCodeCtx->codec_id);
    if (pCodec == NULL) {
        printf("[rxhu] Can't find the decoder\n");
        return -1;
    }
    
    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        printf("[rxhu] the decoder open failed!\n");
        return -1;
    }
    
   	packet = av_malloc(sizeof(AVPacket));
    
    frame = av_frame_alloc();
    
    //frame->16bit 44100 PCM 统一音频采样格式与采样率
    swrCtx = swr_alloc();
    //重采样设置选项-----------------------------------------------------------start
    //输入的采样格式
    in_sample_fmt = pCodeCtx->sample_fmt;
    //输出的采样格式 16bit PCM
    out_sample_fmt = AV_SAMPLE_FMT_S16;
    //输入的采样率
    in_sample_rate = pCodeCtx->sample_rate;
    //输出的采样率
    out_sample_rate = pCodeCtx->sample_rate;
    //输入的声道布局
    in_ch_layout = pCodeCtx->channel_layout;
    //输出的声道布局
    out_ch_layout = AV_CH_LAYOUT_MONO;

    swr_alloc_set_opts(swrCtx, out_ch_layout, out_sample_fmt, out_sample_rate, in_ch_layout, in_sample_fmt,
            in_sample_rate, 0, NULL);
    swr_init(swrCtx);
    //重采样设置选项-----------------------------------------------------------end
    //获取输出的声道个数
    out_channel_nb = av_get_channel_layout_nb_channels(out_ch_layout);
    //存储pcm数据
    out_buffer = (uint8_t *) av_malloc(2 * out_sample_rate);
#if 0
    FILE *fp_pcm = fopen("out.pcm", "wb");
#else
	/*init pcm snd device*/
	ret = snd_pcm_open(&pcm, "plughw:1,0", SND_PCM_STREAM_PLAYBACK, 0);
	if (ret < 0) {
		printf("[rxhu] open PCM device failed\n");
		return -1;
	}

	snd_pcm_hw_params_alloca(&params); 
	ret = snd_pcm_hw_params_any(pcm, params);
	if (ret < 0) {
		printf("[rxhu] snd_pcm_hw_params_any failed!\n");
		return -1;
	}

	ret = snd_pcm_hw_params_set_access(pcm, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (ret < 0) {
		printf("[rxhu] snd_pcm_hw_params_set_access failed!\n");
		return -1;
	}

	ret = snd_pcm_hw_params_set_format(pcm, params, SND_PCM_FORMAT_S16_LE);
	if (ret < 0) {
		printf("[rxhu] snd_pcm_hw_params_set_format failed!\n");
		return -1;
	}

	ret = snd_pcm_hw_params_set_channels(pcm, params, out_channel_nb);
	if (ret < 0) {
		printf("[rxhu] snd_pcm_hw_params_set_channels failed!\n");
		return -1;
	}

	ret = snd_pcm_hw_params_set_rate_near(pcm, params, (unsigned int *)(&out_sample_rate), &dir);
	if (ret < 0) {
		printf("[rxhu] snd_pcm_hw_params_set_rate_near failed!\n");
		return -1;
	}

	ret = snd_pcm_hw_params(pcm, params);
	if (ret < 0) {
		printf("[rxhu] snd_pcm_hw_params failed!\n");
		return -1;
	}

	ret = snd_pcm_prepare (pcm);
	if (ret < 0) {
		printf("[rxhu] cannot prepare audio interface for use (%s)\n",snd_strerror (ret));
		return -1;
	}

	printf("the pcm device set ok!\n");
#endif
    //6.一帧一帧读取压缩的音频数据AVPacket
    while (av_read_frame(pFormatCtx, packet) >= 0) {
        if (packet->stream_index == audio_stream_idx) {
            //解码AVPacket->AVFrame
            ret = avcodec_decode_audio4(pCodeCtx, frame, &got_frame, packet);
            if (ret < 0) {
                printf("[rxhu] decode the frame finish\n");
            }
            //非0，正在解码
            if (got_frame) {
                //printf("解码%d帧", framecount++);
                swr_convert(swrCtx, &out_buffer, 2 * out_sample_rate, (const uint8_t **)frame->data, frame->nb_samples);
                //获取sample的size
                out_buffer_size = av_samples_get_buffer_size(NULL, out_channel_nb, frame->nb_samples,
                        out_sample_fmt, 1);
				#if 0 
                //写入文件进行测试
                fwrite(out_buffer, 1, out_buffer_size, fp_pcm);
				#else
				if (snd_pcm_writei(pcm, out_buffer, out_buffer_size / 2) < 0) {
					printf("[rxhu] can't send the pcm to the sound card!\n");
					return -1;
				}
				#endif
            }
        }
        av_free_packet(packet);
    }
#if 0
    fclose(fp_pcm);
#endif
    av_frame_free(&frame);
    av_free(out_buffer);
    swr_free(&swrCtx);
    avcodec_close(pCodeCtx);
    avformat_close_input(&pFormatCtx);
    snd_pcm_close(pcm);
    return 0;
}


