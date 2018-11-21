/*
 *  ffmpeg.c - FFmpeg common code
 *
 *  hwdecode-demos (C) 2009-2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"
#include "ffmpeg.h"

#include "vaapi.h"
#include <libavcodec/vaapi.h>

#define DEBUG 1
#include "debug.h"

static FFmpegContext *ffmpeg_context;
struct vaapi_context *vaapi_context;

static int ffmpeg_init(void)
{
    FFmpegContext *ffmpeg;

    if (ffmpeg_context)
        return 0;
    
    if ((ffmpeg = calloc(1, sizeof(*ffmpeg))) == NULL)
        return -1;

    if ((ffmpeg->frame = av_frame_alloc()) == NULL) {
        free(ffmpeg);
        return -1;
    }

    //avcodec_init();
    avcodec_register_all();

    ffmpeg_context = ffmpeg;
    return 0;
}

static int ffmpeg_exit(void)
{
    FFmpegContext * const ffmpeg = ffmpeg_get_context();

    if (!ffmpeg)
        return 0;

    av_freep(&ffmpeg->frame);

    free(ffmpeg_context);
    ffmpeg_context = NULL;
    return 0;
}

#ifdef USE_FFMPEG_VAAPI
int ffmpeg_vaapi_init(Display *display)
{
    if (vaapi_init(vaGetDisplay(display)) < 0)
        return -1;
    if ((vaapi_context = calloc(1, sizeof(*vaapi_context))) == NULL)
        return -1;
    vaapi_context->display = vaapi_get_context()->display;
    return 0;
}

int ffmpeg_vaapi_exit(void)
{
    if (vaapi_exit() < 0)
        return -1;
    free(vaapi_context);
    return 0;
}
#endif

FFmpegContext *ffmpeg_get_context(void)
{
    return ffmpeg_context;
}

#ifdef USE_FFMPEG_VAAPI
static enum AVPixelFormat get_format(struct AVCodecContext *avctx,
                                   const enum AVPixelFormat *fmt)
{
    int i, profile;

    for (i = 0; fmt[i] != AV_PIX_FMT_NONE; i++) {
        if (fmt[i] != AV_PIX_FMT_VAAPI_VLD)
      //  if (fmt[i] != AV_PIX_FMT_NV12)
            continue;
        switch (avctx->codec_id) {
        case AV_CODEC_ID_MPEG2VIDEO:
            profile = VAProfileMPEG2Main;
            break;
        case AV_CODEC_ID_MPEG4:
        case AV_CODEC_ID_H263:
            profile = VAProfileMPEG4AdvancedSimple;
            break;
        case AV_CODEC_ID_H264:
            profile = VAProfileH264High;
            break;
        case AV_CODEC_ID_WMV3:
            profile = VAProfileVC1Main;
            break;
        case AV_CODEC_ID_VC1:
            profile = VAProfileVC1Advanced;
            break;
        case AV_CODEC_ID_HEVC:
            profile = VAProfileHEVCMain;
            break;
        default:
            profile = -1;
            break;
        }
        if (profile >= 0) {
            if (vaapi_init_decoder(profile, VAEntrypointVLD, avctx->width, avctx->height) == 0) {
                VAAPIContext * const vaapi = vaapi_get_context();
                vaapi_context->config_id   = vaapi->config_id;
                vaapi_context->context_id  = vaapi->context_id;
                avctx->hwaccel_context     = vaapi_context;
                return fmt[i];
            }
        }
    }
    return AV_PIX_FMT_NONE;
}
static void release_buffer(void *avctx, uint8_t *data)
{
    data = NULL;
}

static int get_buffer(struct AVCodecContext *avctx, AVFrame *pic, int flags)
{
#if 0
    VAAPIContext * const vaapi = vaapi_get_context();
    void *surface = (void *)(uintptr_t)vaapi->surface_ids[vaapi->surface_index];
    vaapi->surface_index++;
    vaapi->surface_index = vaapi->surface_index % vaapi->surface_nums;

    pic->data[0]        = surface;
    pic->data[1]        = NULL;
    pic->data[2]        = NULL;
    pic->data[3]        = surface;
    pic->linesize[0]    = 0;
    pic->linesize[1]    = 0;
    pic->linesize[2]    = 0;
    pic->linesize[3]    = 0;

    return avcodec_default_get_buffer2(avctx, pic, flags);
#else 
    VAAPIContext * const vaapi = vaapi_get_context();
    AVBufferRef *surf_buf;
    AVBufferRef *surf_buf_data3;
    void *surface = (void *)(uintptr_t)vaapi->surface_ids[vaapi->surface_index];
    vaapi->surface_index++;
    vaapi->surface_index = vaapi->surface_index % vaapi->surface_nums;
    pic->data[0] = (uint8_t*) surface;
    pic->data[1] = NULL;
    pic->data[2] = NULL;
    pic->data[3] = (uint8_t*) surface;
    pic->linesize[0] = 0;
    pic->linesize[1] = 0;
    pic->linesize[2] = 0;
    pic->linesize[3] = 0;

    surf_buf = av_buffer_create(pic->data[0], 0, release_buffer,
                                  NULL, AV_BUFFER_FLAG_READONLY);
    surf_buf_data3 = av_buffer_create(pic->data[3], 0, release_buffer,
                                  NULL, AV_BUFFER_FLAG_READONLY);
    pic->buf[0] = surf_buf;
    pic->buf[3] = surf_buf_data3;
    return 0;
#endif
}

#endif

int ffmpeg_init_context(AVCodecContext *avctx)
{
#ifdef USE_FFMPEG_VAAPI
        avctx->thread_count    = 1;
        avctx->get_format      = get_format;
        avctx->get_buffer2      = get_buffer;
        //avctx->reget_buffer    = get_buffer;
        //avctx->release_buffer  = release_buffer;
        avctx->draw_horiz_band = NULL;
        avctx->slice_flags     = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
#endif
	return 0;
}
