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
        //case AV_CODEC_ID_HEVC:
        //    profile = VAProfileHEVCMain;
        //    break;
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
    VAAPIContext * const vaapi = vaapi_get_context();
    void *surface;
    surface = (void *)(uintptr_t)vaapi->surface_ids[vaapi->surface_tail];
    vaapi->surface_ids[vaapi->surface_tail] = (VASurfaceID)(uintptr_t)data;
    vaapi->surface_tail = (vaapi->surface_tail + 1) % vaapi->surface_nums;
}

static int get_buffer(struct AVCodecContext *avctx, AVFrame *frame, int flags)
{
    VAAPIContext * const vaapi = vaapi_get_context();
    void *surface;
    AVBufferRef *buf;
    if (!(avctx->codec->capabilities & AV_CODEC_CAP_DR1))
        return avcodec_default_get_buffer2(avctx, frame, flags);

    surface = (void *)(uintptr_t)vaapi->surface_ids[vaapi->surface_head];
    vaapi->surface_head = (vaapi->surface_head + 1) % vaapi->surface_nums;
    buf = av_buffer_create((uint8_t *)surface, 0,(void (*)(void *, uint8_t *))release_buffer, avctx,
        AV_BUFFER_FLAG_READONLY);
    if (!buf) {
        release_buffer(avctx, surface);
        return -1;
    }
    frame->buf[0] = buf;
    memset(frame->data, 0, sizeof(frame->data));
    frame->data[0] = (uint8_t *)(uintptr_t)surface;
    frame->data[3] = (uint8_t *)(uintptr_t)surface;
    memset(frame->linesize, 0, sizeof(frame->linesize));
    frame->linesize[0] = avctx->coded_width; /* XXX: 8-bit per sample only */   
    frame->data[5] = surface;
    return 0;
}

#endif

int ffmpeg_init_context(AVCodecContext *avctx)
{
#ifdef USE_FFMPEG_VAAPI
        avctx->thread_count    = 1;
        avctx->get_format      = get_format;
        avctx->get_buffer2      = get_buffer;
        avctx->draw_horiz_band = NULL;
        avctx->slice_flags     = SLICE_FLAG_CODED_ORDER|SLICE_FLAG_ALLOW_FIELD;
#endif
	return 0;
}
