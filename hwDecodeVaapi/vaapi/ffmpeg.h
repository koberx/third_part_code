/*
 *  ffmpeg.h - FFmpeg common code
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

#ifndef FFMPEG_H
#define FFMPEG_H

#include <libavcodec/avcodec.h>
#include <X11/Xlib.h>

typedef struct _FFmpegContext FFmpegContext;

struct _FFmpegContext {
	AVFrame            *frame;
};

FFmpegContext *ffmpeg_get_context(void);

int ffmpeg_init_context(AVCodecContext *avctx);
extern int ffmpeg_vaapi_init(Display *display);
extern int ffmpeg_vaapi_exit(void);

#endif /* FFMPEG_H */
