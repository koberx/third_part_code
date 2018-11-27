/*
 *  image.h - Utilities for image manipulation
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

#ifndef IMAGE_H
#define IMAGE_H

#include <stdint.h>

#define MAX_IMAGE_PLANES 3

typedef struct _Image Image;

struct _Image {
	uint32_t            format;
	unsigned int        width;
	unsigned int        height;
	uint8_t            *data;
	unsigned int        data_size;
	unsigned int        num_planes;
	uint8_t            *pixels[MAX_IMAGE_PLANES];
	unsigned int        offsets[MAX_IMAGE_PLANES];
	unsigned int        pitches[MAX_IMAGE_PLANES];
	int		    is_out_data;
};

extern Image *rgb_image_create(unsigned int width, unsigned int height);

#endif /* IMAGE_H */
