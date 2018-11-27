/*
 *  image.c - Utilities for image manipulation
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
#include "image.h"
#include "utils.h"
#include <stdlib.h>
#include <time.h>
#include <math.h>


#include <libavutil/pixfmt.h>

#include <libswscale/swscale.h>

#define DEBUG 1
#include "debug.h"

#undef  FOURCC
#define FOURCC IMAGE_FOURCC

Image *rgb_image_create(unsigned int width, unsigned int height)
{
    
    Image *img = NULL;
    unsigned int i, size;
    img = calloc(1, sizeof(*img));
    if (!img) {
        printf("[rxhu] image calloc failed!\n");
        goto error;
    }

    img->width          = width;
    img->height         = height;
    size                = width * height;

    img->pitches[0] = width * 4;

    img->num_planes = 1;
    img->offsets[0] = 0;
    img->data_size  = img->pitches[0] * height;

    if (!img->data_size)
        goto error;

    img->data = malloc(img->data_size);
    img->is_out_data = 0;

    if (!img->data)
        goto error;

    for (i = 0; i < img->num_planes; i++)
        img->pixels[i] = img->data + img->offsets[i];
    return img;
        
error:
    free(img);
    return NULL;    
}

