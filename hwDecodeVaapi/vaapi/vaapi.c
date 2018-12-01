/*
 *  vaapi.c - VA API common code
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

#include <libavcodec/avcodec.h>

#include "sysdeps.h"
#include "vaapi.h"
#include "vaapi_compat.h"
#include "utils.h"

#if USE_GLX
#include "glx.h"
#endif

#if USE_VAAPI_GLX
#include <va/va_glx.h>
#endif

#define DEBUG 1
#include "debug.h"

typedef struct _Rectangle Rectangle;
struct _Rectangle {
	int x;
	int y;
	unsigned int width;
	unsigned int height;
};
static VAAPIContext *vaapi_context;

static inline const char *string_of_VAImageFormat(VAImageFormat *imgfmt)
{
	return string_of_FOURCC(imgfmt->fourcc);
}

static const char *string_of_VAProfile(VAProfile profile)
{
	switch (profile) {
#define PROFILE(profile) \
		case VAProfile##profile: return "VAProfile" #profile
					 PROFILE(MPEG2Simple);
					 PROFILE(MPEG2Main);
					 PROFILE(MPEG4Simple);
					 PROFILE(MPEG4AdvancedSimple);
					 PROFILE(MPEG4Main);
					 PROFILE(H264Baseline);
					 PROFILE(H264Main);
					 PROFILE(H264High);
					 PROFILE(VC1Simple);
					 PROFILE(VC1Main);
					 PROFILE(VC1Advanced);
#undef PROFILE
		default: break;
	}
	return "<unknown>";
}

static const char *string_of_VAEntrypoint(VAEntrypoint entrypoint)
{
	switch (entrypoint) {
#define ENTRYPOINT(entrypoint) \
		case VAEntrypoint##entrypoint: return "VAEntrypoint" #entrypoint
					       ENTRYPOINT(VLD);
					       ENTRYPOINT(IZZ);
					       ENTRYPOINT(IDCT);
					       ENTRYPOINT(MoComp);
					       ENTRYPOINT(Deblocking);
#undef ENTRYPOINT
		default: break;
	}
	return "<unknown>";
}

static const char *string_of_VADisplayAttribType(VADisplayAttribType type)
{
	switch (type) {
#define TYPE(type) \
		case VADisplayAttrib##type: return "VADisplayAttrib" #type
					    TYPE(Brightness);
					    TYPE(Contrast);
					    TYPE(Hue);
					    TYPE(Saturation);
					    TYPE(BackgroundColor);
					    TYPE(DirectSurface);
#undef TYPE
		default: break;
	}
	return "<unknown>";
}

static void *alloc_buffer(VAAPIContext *vaapi, int type, unsigned int size, VABufferID *buf_id)
{
	VAStatus status;
	void *data = NULL;

	*buf_id = 0;
	status = vaCreateBuffer(vaapi->display, vaapi->context_id,
			type, size, 1, NULL, buf_id);
	if (!vaapi_check_status(status, "vaCreateBuffer()"))
		return NULL;

	status = vaMapBuffer(vaapi->display, *buf_id, &data);
	if (!vaapi_check_status(status, "vaMapBuffer()"))
		return NULL;

	return data;
}

static void destroy_buffers(VADisplay display, VABufferID *buffers, unsigned int n_buffers)
{
	unsigned int i;
	for (i = 0; i < n_buffers; i++) {
		if (buffers[i]) {
			vaDestroyBuffer(display, buffers[i]);
			buffers[i] = 0;
		}
	}
}

int vaapi_init(VADisplay display)
{
#if 0
	CommonContext * common = common_get_context();
#endif
	VAAPIContext *vaapi;
	int major_version, minor_version;
	int i, num_display_attrs, max_display_attrs;
	VADisplayAttribute *display_attrs;
	VAStatus status;

	if (vaapi_context)
		return 0;

	if (!display)
		return -1;
	D(bug("VA display %p\n", display));

	status = vaInitialize(display, &major_version, &minor_version);
	if (!vaapi_check_status(status, "vaInitialize()"))
		return -1;
	D(bug("VA API version %d.%d\n", major_version, minor_version));

	max_display_attrs = vaMaxNumDisplayAttributes(display);
	display_attrs = malloc(max_display_attrs * sizeof(display_attrs[0]));
	if (!display_attrs)
		return -1;

	num_display_attrs = 0; /* XXX: workaround old GMA500 bug */
	status = vaQueryDisplayAttributes(display, display_attrs, &num_display_attrs);
	if (!vaapi_check_status(status, "vaQueryDisplayAttributes()")) {
		free(display_attrs);
		return -1;
	}
	D(bug("%d display attributes available\n", num_display_attrs));
	for (i = 0; i < num_display_attrs; i++) {
		VADisplayAttribute * const display_attr = &display_attrs[i];
		D(bug("  %-32s (%s/%s) min %d max %d value 0x%x\n",
					string_of_VADisplayAttribType(display_attr->type),
					(display_attr->flags & VA_DISPLAY_ATTRIB_GETTABLE) ? "get" : "---",
					(display_attr->flags & VA_DISPLAY_ATTRIB_SETTABLE) ? "set" : "---",
					display_attr->min_value,
					display_attr->max_value,
					display_attr->value));
	}
	free(display_attrs);


	if ((vaapi = calloc(1, sizeof(*vaapi))) == NULL)
		return -1;
	vaapi->display               = display;
	vaapi->surface_index = 0;
	vaapi->surface_nums = 0;
	vaapi->subpic_image.image_id = VA_INVALID_ID;
	for (i = 0; i < ARRAY_ELEMS(vaapi->subpic_ids); i++)
		vaapi->subpic_ids[i]     = VA_INVALID_ID;

	vaapi_context = vaapi;
	return 0;
}

int vaapi_exit(void)
{
	VAAPIContext * const vaapi = vaapi_get_context();
	unsigned int i;

	if (!vaapi)
		return 0;

	destroy_buffers(vaapi->display, &vaapi->pic_param_buf_id, 1);
	destroy_buffers(vaapi->display, &vaapi->iq_matrix_buf_id, 1);
	destroy_buffers(vaapi->display, &vaapi->bitplane_buf_id, 1);
	destroy_buffers(vaapi->display, vaapi->slice_buf_ids, vaapi->n_slice_buf_ids);

	if (vaapi->subpic_flags) {
		free(vaapi->subpic_flags);
		vaapi->subpic_flags = NULL;
	}

	if (vaapi->subpic_formats) {
		free(vaapi->subpic_formats);
		vaapi->subpic_formats = NULL;
		vaapi->n_subpic_formats = 0;
	}

	if (vaapi->image_formats) {
		free(vaapi->image_formats);
		vaapi->image_formats = NULL;
		vaapi->n_image_formats = 0;
	}

	if (vaapi->entrypoints) {
		free(vaapi->entrypoints);
		vaapi->entrypoints = NULL;
		vaapi->n_entrypoints = 0;
	}

	if (vaapi->profiles) {
		free(vaapi->profiles);
		vaapi->profiles = NULL;
		vaapi->n_profiles = 0;
	}

	if (vaapi->slice_params) {
		free(vaapi->slice_params);
		vaapi->slice_params = NULL;
		vaapi->slice_params_alloc = 0;
		vaapi->n_slice_params = 0;
	}

	if (vaapi->slice_buf_ids) {
		free(vaapi->slice_buf_ids);
		vaapi->slice_buf_ids = NULL;
		vaapi->n_slice_buf_ids = 0;
	}

	if (vaapi->subpic_image.image_id != VA_INVALID_ID) {
		vaDestroyImage(vaapi->display, vaapi->subpic_image.image_id);
		vaapi->subpic_image.image_id = VA_INVALID_ID;
	}

	for (i = 0; i < ARRAY_ELEMS(vaapi->subpic_ids); i++) {
		if (vaapi->subpic_ids[i] != VA_INVALID_ID) {
			vaDestroySubpicture(vaapi->display, vaapi->subpic_ids[i]);
			vaapi->subpic_ids[i] = VA_INVALID_ID;
		}
	}

	if (vaapi->surface_ids) {
		vaDestroySurfaces(vaapi->display, vaapi->surface_ids, vaapi->surface_nums);
		//vaapi->surface_ids = 0;
		vaapi->surface_nums = 0;
		vaapi->surface_index = 0;
	}

	if (vaapi->context_id) {
		vaDestroyContext(vaapi->display, vaapi->context_id);
		vaapi->context_id = 0;
	}

	if (vaapi->config_id) {
		vaDestroyConfig(vaapi->display, vaapi->config_id);
		vaapi->config_id = 0;
	}

	if (vaapi->display) {
		vaTerminate(vaapi->display);
		vaapi->display = NULL;
	}

	free(vaapi_context);
	vaapi_context = NULL;
	return 0;
}

VAAPIContext *vaapi_get_context(void)
{
	return vaapi_context;
}

int vaapi_check_status(VAStatus status, const char *msg)
{
	if (status != VA_STATUS_SUCCESS) {
		fprintf(stderr, "[%s] %s: %s\n", "ffmpeg_player", msg, vaErrorStr(status));
		return 0;
	}
	return 1;
}

static int has_profile(VAAPIContext *vaapi, VAProfile profile);

int has_ffmpeg_profile(int codec_id, int ffmpeg_profile)
{
	VAAPIContext *vaapi = vaapi_get_context();
	VAProfile profile;
	switch (codec_id) {
		case AV_CODEC_ID_MPEG2VIDEO:
			profile = VAProfileMPEG2Main;
			break;
		case AV_CODEC_ID_MPEG4:
			profile = VAProfileMPEG4Main;
			break;
		case AV_CODEC_ID_H264:
			profile = VAProfileH264High;
			break;
		case AV_CODEC_ID_VC1:
			profile = VAProfileVC1Advanced;
			break;
		default:
			profile = -1;
			break;
	}

	if (profile != -1)
		return has_profile(vaapi, profile);

	return 0;
}

static int has_profile(VAAPIContext *vaapi, VAProfile profile)
{
	VAStatus status;
	int i;

	if (!vaapi->profiles || vaapi->n_profiles == 0) {
		vaapi->profiles = calloc(vaMaxNumProfiles(vaapi->display), sizeof(vaapi->profiles[0]));

		status = vaQueryConfigProfiles(vaapi->display,
				vaapi->profiles,
				&vaapi->n_profiles);
		if (!vaapi_check_status(status, "vaQueryConfigProfiles()"))
			return 0;

		D(bug("%d profiles available\n", vaapi->n_profiles));
		for (i = 0; i < vaapi->n_profiles; i++)
			D(bug("  %s\n", string_of_VAProfile(vaapi->profiles[i])));
	}

	for (i = 0; i < vaapi->n_profiles; i++) {
		if (vaapi->profiles[i] == profile)
			return 1;
	}
	return 0;
}

static int has_entrypoint(VAAPIContext *vaapi, VAProfile profile, VAEntrypoint entrypoint)
{
	VAStatus status;
	int i;

	if (!vaapi->entrypoints || vaapi->n_entrypoints == 0) {
		vaapi->entrypoints = calloc(vaMaxNumEntrypoints(vaapi->display), sizeof(vaapi->entrypoints[0]));

		status = vaQueryConfigEntrypoints(vaapi->display, profile,
				vaapi->entrypoints,
				&vaapi->n_entrypoints);
		if (!vaapi_check_status(status, "vaQueryConfigEntrypoints()"))
			return 0;

		D(bug("%d entrypoints available for %s\n", vaapi->n_entrypoints,
					string_of_VAProfile(profile)));
		for (i = 0; i < vaapi->n_entrypoints; i++)
			D(bug("  %s\n", string_of_VAEntrypoint(vaapi->entrypoints[i])));
	}

	for (i = 0; i < vaapi->n_entrypoints; i++) {
		if (vaapi->entrypoints[i] == entrypoint)
			return 1;
	}
	return 0;
}

int vaapi_init_decoder(VAProfile    profile,
		VAEntrypoint entrypoint,
		unsigned int picture_width,
		unsigned int picture_height)
{
	VAAPIContext * const vaapi = vaapi_get_context();
	VAConfigAttrib attrib;
	VAConfigID config_id = 0;
	VAContextID context_id = 0;
	VASurfaceID *surface_ids;
	int surface_nums = 0;
	VAStatus status;

	if (!vaapi)
		return -1;
#if 0
	if (common_init_decoder(picture_width, picture_height) < 0)
		return -1;
#endif

	if (!has_profile(vaapi, profile))
		return -1;
	if (!has_entrypoint(vaapi, profile, entrypoint))
		return -1;

#if 0
	if (vaapi->profile != profile || vaapi->entrypoint != entrypoint) {
#endif
	if (vaapi->config_id)
		vaDestroyConfig(vaapi->display, vaapi->config_id);

	attrib.type = VAConfigAttribRTFormat;
	status = vaGetConfigAttributes(vaapi->display, profile, entrypoint,
			&attrib, 1);
	if (!vaapi_check_status(status, "vaGetConfigAttributes()"))
		return -1;
	if ((attrib.value & VA_RT_FORMAT_YUV420) == 0)
		return -1;

	status = vaCreateConfig(vaapi->display, profile, entrypoint,
			&attrib, 1, &config_id);
	if (!vaapi_check_status(status, "vaCreateConfig()"))
		return -1;
#if 0
	}
	else
		config_id = vaapi->config_id;
#endif

#if 1
	switch (profile) {
		case VAProfileMPEG2Main:
			surface_nums = NUM_VIDEO_SURFACES_MPEG2;
			break;
		case VAProfileMPEG4Main:
			surface_nums = NUM_VIDEO_SURFACES_MPEG4;
			break;
		case VAProfileH264High:
			surface_nums = NUM_VIDEO_SURFACES_H264;
			break;
		case VAProfileVC1Advanced:
			surface_nums = NUM_VIDEO_SURFACES_VC1;
			break;
		default:
			surface_nums = 1;
			break;
	}
#else
	surface_nums = MAX_VIDEO_SURFACES;
#endif

#if 0
	if (vaapi->picture_width != picture_width || vaapi->picture_height != picture_height || surface_nums != vaapi->surface_nums) {
#endif
	if (vaapi->surface_nums != 0) {
		vaDestroySurfaces(vaapi->display, vaapi->surface_ids, vaapi->surface_nums);
		vaapi->surface_nums = 0;
	}
	
	if (vaapi->context_id)
		vaDestroyContext(vaapi->display, vaapi->context_id);

	surface_ids = vaapi->surface_ids;//(VASurfaceID *) malloc(surface_nums * sizeof(VASurfaceID));
	
#ifdef __CM_BOX_PLATFORM_32
	status = vaCreateSurfaces(vaapi->display, 
							  picture_width, 
							  picture_height,
							  VA_RT_FORMAT_YUV420, 
							  surface_nums, 
							  surface_ids);
#else
	status = vaCreateSurfaces(vaapi->display,
	                          VA_RT_FORMAT_YUV420,
	                          picture_width, 
	                          picture_height,
                              surface_ids, 
                              surface_nums, NULL, 0);
#endif

	if (!vaapi_check_status(status, "vaCreateSurfaces()"))
		return -1;

	status = vaCreateContext(vaapi->display, config_id,
			picture_width, picture_height,
			VA_PROGRESSIVE,
			surface_ids, surface_nums,
			&context_id);
	if (!vaapi_check_status(status, "vaCreateContext()"))
		return -1;
#if 0
	}
	else {
		context_id = vaapi->context_id;
		surface_ids = vaapi->surface_ids;
	}
#endif

	vaapi->config_id      = config_id;
	vaapi->context_id     = context_id;
	//vaapi->surface_ids    = surface_ids;
	vaapi->surface_nums   = surface_nums;
	vaapi->surface_index  = 0;
	vaapi->profile        = profile;
	vaapi->entrypoint     = entrypoint;
	vaapi->picture_width  = picture_width;
	vaapi->picture_height = picture_height;
	return 0;
}

/*设置解码器格式*/
int set_image_format(uint32_t fourcc, VAImageFormat **image_format)
{
	VAStatus status;
	int i;
	VAAPIContext * const vaapi = vaapi_get_context();
	if (image_format)
		*image_format = NULL;
	if (!vaapi->image_formats || vaapi->n_image_formats == 0) {
		vaapi->image_formats = calloc(vaMaxNumImageFormats(vaapi->display),
				sizeof(vaapi->image_formats[0]));
		if (!vaapi->image_formats) {
			printf("[rxhu] vaMaxNumImageFormats calloc failed\n");
			return -1;
		}

		status = vaQueryImageFormats(vaapi->display,
				vaapi->image_formats,
				&vaapi->n_image_formats);
		if (!vaapi_check_status(status, "vaQueryImageFormats()"))
			return -1;
        printf("[rxhu] come to here!\n"); 
		D(bug("%d image formats\n", vaapi->n_image_formats));
		for (i = 0; i < vaapi->n_image_formats; i++)
			D(bug("  %s\n", string_of_VAImageFormat(&vaapi->image_formats[i])));
	}

	for (i = 0; i < vaapi->n_image_formats; i++) {
		if (vaapi->image_formats[i].fourcc == fourcc) {
			*image_format = &vaapi->image_formats[i];
			break;
		}
	}

	D(bug("selected %s image format for getimage\n",string_of_VAImageFormat(*image_format)));
	
	return 0;
}

int create_rgbx_image(VAImage *image,VAImageFormat *image_format)
{
	VAAPIContext * const vaapi = vaapi_get_context();
	VAStatus status;
	image->image_id = VA_INVALID_ID;
        image->buf      = VA_INVALID_ID;
	D(bug("[rxhu] begin vaCreateImage %s\n", string_of_VAImageFormat(image_format)));
	status = vaCreateImage(vaapi->display, image_format,vaapi->picture_width, vaapi->picture_height,image);
	 if (!vaapi_check_status(status, "vaCreateImage()")) {
        return -1;
    }
	D(bug("created image with id 0x%08x and buffer id 0x%08x\n",image->image_id, image->buf));

	return 0;
}

int get_rgbx_picture(VASurfaceID surface, VAImage *image, uint8_t **data,  VAImageFormat *image_format)
{
    VAStatus status;
    void *va_image_data;
    VAAPIContext * const vaapi = vaapi_get_context();
    //image->image_id = VA_INVALID_ID;
    //image->buf      = VA_INVALID_ID;

    if (vaSyncSurface(vaapi->display, vaapi->context_id, surface)) {
        D(bug("vaSyncSurface failed\n"));
        return -1;
    }

    VARectangle src_rect;

    src_rect.x      = 0;
    src_rect.y      = 0;
    src_rect.width  = vaapi->picture_width;
    src_rect.height = vaapi->picture_height;

    D(bug("src rect (%d,%d):%ux%u\n",src_rect.x, src_rect.y, src_rect.width, src_rect.height));
    printf("[rxhu] the image->id = 0x%08x image->buf_id = 0x%08x\n",image->image_id,image->buf);
    status = vaGetImage(vaapi->display, surface,src_rect.x, src_rect.y, src_rect.width, src_rect.height,image->image_id);
    if (!vaapi_check_status(status, "vaGetImage()")) {
            vaDestroyImage(vaapi->display, image->image_id);
            return -1;
    }

    if (image->num_planes > MAX_IMAGE_PLANES)
        return -1;

    status = vaMapBuffer(vaapi->display, image->buf, &va_image_data);
    if (!vaapi_check_status(status, "vaMapBuffer()"))
        return -1;

    
    *data = va_image_data + image->offsets[0];
    return 0;
}

int release_rgb_image(VAImage *image)
{
    VAStatus status;
    VAAPIContext * const vaapi = vaapi_get_context();
    status = vaUnmapBuffer(vaapi->display, image->buf);
    if (!vaapi_check_status(status, "vaUnmapBuffer()"))
        return -1;

    if (image->image_id != VA_INVALID_ID) {
        status = vaDestroyImage(vaapi->display, image->image_id);
        if (!vaapi_check_status(status, "vaDestroyImage()"))
            return -1;
    }

}


