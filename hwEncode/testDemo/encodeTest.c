
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <va/va.h>
#include <va/va_enc_h264.h>
#include "va_display.h"
#include "loadsurface.h"
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>

/* the define function  */
#define SURFACE_NUM 16 /* 16 surfaces for source YUV */
#define SURFACE_NUM 16 /* 16 surfaces for reference */
#define current_slot (current_frame_display % SURFACE_NUM)
#define SRC_SURFACE_IN_ENCODING 0
#define SRC_SURFACE_IN_STORAGE  1
#define FRAME_P 0
#define FRAME_B 1
#define FRAME_I 2
#define FRAME_IDR 7
#define NAL_REF_IDC_NONE        0
#define NAL_REF_IDC_LOW         1
#define NAL_REF_IDC_MEDIUM      2
#define NAL_REF_IDC_HIGH        3
#define NAL_NON_IDR             1
#define NAL_IDR                 5
#define NAL_SPS                 7
#define NAL_PPS                 8
#define NAL_SEI			6

#define SLICE_TYPE_P            0
#define SLICE_TYPE_B            1
#define SLICE_TYPE_I            2
#define IS_P_SLICE(type) (SLICE_TYPE_P == (type))
#define IS_B_SLICE(type) (SLICE_TYPE_B == (type))
#define IS_I_SLICE(type) (SLICE_TYPE_I == (type))
#define BITSTREAM_ALLOCATE_STEPPING     4096
#define PROFILE_IDC_BASELINE    66
#define PROFILE_IDC_MAIN        77
#define PROFILE_IDC_HIGH        100

/* global value (init_va)*/
static  VADisplay va_dpy;
static  VAProfile h264_profile = ~0;
static  int ip_period = 1;
static  int constraint_set_flag = 0;
static  int h264_entropy_mode = 1; 
static  VAConfigAttrib attrib[VAConfigAttribTypeMax];
static  VAConfigAttrib config_attrib[VAConfigAttribTypeMax];
static  int config_attrib_num = 0, enc_packed_header_idx;
static  int rc_mode = -1;
static  int rc_default_modes[] = {
    VA_RC_VBR,
    VA_RC_CQP,
    VA_RC_VBR_CONSTRAINED,
    VA_RC_CBR,
    VA_RC_VCM,
    VA_RC_NONE,
};
static  int h264_packedheader = 0; 
static  int h264_maxref = (1<<16|1);

/* global value (setup_encode) */
static  VAConfigID config_id;
static  VAContextID context_id;
static  VASurfaceID src_surface[SURFACE_NUM];
static  VABufferID  coded_buf[SURFACE_NUM];
static  VASurfaceID ref_surface[SURFACE_NUM];
static  int frame_width_mbaligned;
static  int frame_height_mbaligned;

/* global value (encode_frames) */
static  int srcsurface_status[SURFACE_NUM];
static  VAEncSequenceParameterBufferH264 seq_param;
static  VAEncPictureParameterBufferH264 pic_param;
static  VAEncSliceParameterBufferH264 slice_param;
static  int encode_syncmode = 0;
static  unsigned long long current_frame_encoding = 0;
static  unsigned long long current_frame_display = 0;
static  unsigned long long current_IDR_display = 0;
static  unsigned int current_frame_num = 0;
static  int current_frame_type;
static  int intra_period = 30;
static  int intra_idr_period = 60;
static  unsigned int numShortTerm = 0;
static  unsigned int frame_count = 60;
static unsigned int UploadPictureTicks=0;
static unsigned int BeginPictureTicks=0;
static unsigned int RenderPictureTicks=0;
static unsigned int EndPictureTicks=0;
static  pthread_t encode_thread;
/* thread to save coded data/upload source YUV */
struct storage_task_t {
    void *next;
    unsigned long long display_order;
    unsigned long long encode_order;
};
static  struct storage_task_t *storage_task_header = NULL, *storage_task_tail = NULL;
static  pthread_mutex_t encode_mutex = PTHREAD_MUTEX_INITIALIZER;
static  pthread_cond_t  encode_cond = PTHREAD_COND_INITIALIZER;
static  unsigned int frame_coded = 0;

/* load surface */
static  int frame_rate = 30;
static  int frame_width = 176;
static  int frame_height = 144;
static  int screen_width;
static  int screen_height;
static 	unsigned char *g_yuv_buffer = NULL;
static 	Window desktop;
static 	Display* dsp;
static  int srcyuv_fourcc = VA_FOURCC_NV12;

/* save picture */
static 	unsigned int SyncPictureTicks=0;
static 	unsigned int SavePictureTicks=0;
static 	unsigned int num_ref_frames = 2;
static 	unsigned int TotalTicks=0;
static  unsigned int MaxFrameNum = (2<<16);
static  VAPictureH264 CurrentCurrPic;
static  VAPictureH264 ReferenceFrames[16], RefPicList0_P[32], RefPicList0_B[32], RefPicList1_B[32];
static  FILE *coded_fp = NULL;
static  double frame_size = 0;
static  unsigned int MaxPicOrderCntLsb = (2<<8);
/* render data */
static  unsigned int frame_bitrate = 0;
static  unsigned int Log2MaxPicOrderCntLsb = 8;
static  unsigned int Log2MaxFrameNum = 16;
static  int initial_qp = 26;
static  int minimal_qp = 0;
static  int misc_priv_type = 0;
static  int misc_priv_value = 0;
struct __bitstream {
    unsigned int *buffer;
    int bit_offset;
    int max_size_in_dword;
};
typedef struct __bitstream bitstream;
static  char *coded_fn = NULL;
static  unsigned int frame_slices = 1;
/* end render data*/

/* ffmpeg init data */
uint8_t *src_data[4];
int src_linesize[4];

uint8_t *dst_data[4];
int dst_linesize[4];

enum AVPixelFormat src_pixfmt;
enum AVPixelFormat dst_pixfmt;
struct SwsContext *img_convert_ctx;

/*function decalare*/
static unsigned int va_swap32(unsigned int val);
static void bitstream_put_se(bitstream *bs, int val);
static void bitstream_put_ui(bitstream *bs, unsigned int val, int size_in_bits);
static void bitstream_put_ue(bitstream *bs, unsigned int val);
static void nal_header(bitstream *bs, int nal_ref_idc, int nal_unit_type);


/* basic local function  */
static char *rc_to_string(int rcmode)
{
    switch (rc_mode) {
    case VA_RC_NONE:
        return "NONE";
    case VA_RC_CBR:
        return "CBR";
    case VA_RC_VBR:
        return "VBR";
    case VA_RC_VCM:
        return "VCM";
    case VA_RC_CQP:
        return "CQP";
    case VA_RC_VBR_CONSTRAINED:
        return "VBR_CONSTRAINED";
    default:
        return "Unknown";
    }
}

/*
 * Helper function for profiling purposes
 */
static unsigned int GetTickCount()
{
    struct timeval tv;
    if (gettimeofday(&tv, NULL))
        return 0;
    return tv.tv_usec/1000+tv.tv_sec*1000;
}

/* end basic local function */

/* the x11 init function*/
int xDispalyInit()
{
	dsp = XOpenDisplay(":0");/* Connect to a local display */
	if (NULL==dsp) {
		//sprintf(err_str,"Error:Cannot connect to local display\n");
		printf("Error:Cannot connect to local display\n");
		return -1;
	}
	desktop = RootWindow(dsp,0);/* Refer to the root window */
	if (0==desktop) {
		printf("cannot get root window\n");
		return -1;
	}
	
	/* Retrive the width and the height of the screen */
	screen_width = DisplayWidth(dsp,0);
	screen_height = DisplayHeight(dsp,0);
        return 0;
}

void deinitXDisplay()
{
	XCloseDisplay(dsp);
}
/* end the x11 init function */

/* ffmpeg init function */
int ffmpegInit()
{
	int ret;
	src_pixfmt=PIX_FMT_RGB32;
	dst_pixfmt=AV_PIX_FMT_YUV420P;
	printf("screen_width = %d screen_height =%d\n", screen_width, screen_height);	
	ret= av_image_alloc(src_data, src_linesize,screen_width, screen_height, src_pixfmt, 1);
	if (ret< 0) {
		printf( "Could not allocate source image\n");
		return -1;
	}
	ret = av_image_alloc(dst_data, dst_linesize,screen_width, screen_height, dst_pixfmt, 1);
	if (ret< 0) {
		printf( "Could not allocate destination image\n");
		return -1;
	}
	img_convert_ctx = sws_getContext(screen_width, screen_height,src_pixfmt, screen_width, screen_height, dst_pixfmt, 
		SWS_POINT, NULL, NULL, NULL); 
		
	if(img_convert_ctx == NULL)
	{
		printf( "Could not sws_getContext\n");
		return -1;
	}

        return 0;
}
/* end ffmpeg init function */

/* the render implement */
#define partition(ref, field, key, ascending)   \
    while (i <= j) {                            \
        if (ascending) {                        \
            while (ref[i].field < key)          \
                i++;                            \
            while (ref[j].field > key)          \
                j--;                            \
        } else {                                \
            while (ref[i].field > key)          \
                i++;                            \
            while (ref[j].field < key)          \
                j--;                            \
        }                                       \
        if (i <= j) {                           \
            tmp = ref[i];                       \
            ref[i] = ref[j];                    \
            ref[j] = tmp;                       \
            i++;                                \
            j--;                                \
        }                                       \
    }                                           \

static void sort_one(VAPictureH264 ref[], int left, int right,
                     int ascending, int frame_idx)
{
    int i = left, j = right;
    unsigned int key;
    VAPictureH264 tmp;

    if (frame_idx) {
        key = ref[(left + right) / 2].frame_idx;
        partition(ref, frame_idx, key, ascending);
    } else {
        key = ref[(left + right) / 2].TopFieldOrderCnt;
        partition(ref, TopFieldOrderCnt, (signed int)key, ascending);
    }
    
    /* recursion */
    if (left < j)
        sort_one(ref, left, j, ascending, frame_idx);
    
    if (i < right)
        sort_one(ref, i, right, ascending, frame_idx);
}

static void sort_two(VAPictureH264 ref[], int left, int right, unsigned int key, unsigned int frame_idx,
                     int partition_ascending, int list0_ascending, int list1_ascending)
{
    int i = left, j = right;
    VAPictureH264 tmp;

    if (frame_idx) {
        partition(ref, frame_idx, key, partition_ascending);
    } else {
        partition(ref, TopFieldOrderCnt, (signed int)key, partition_ascending);
    }
    

    sort_one(ref, left, i-1, list0_ascending, frame_idx);
    sort_one(ref, j+1, right, list1_ascending, frame_idx);
}

static int calc_poc(int pic_order_cnt_lsb)
{
    static int PicOrderCntMsb_ref = 0, pic_order_cnt_lsb_ref = 0;
    int prevPicOrderCntMsb, prevPicOrderCntLsb;
    int PicOrderCntMsb, TopFieldOrderCnt;
    
    if (current_frame_type == FRAME_IDR)
        prevPicOrderCntMsb = prevPicOrderCntLsb = 0;
    else {
        prevPicOrderCntMsb = PicOrderCntMsb_ref;
        prevPicOrderCntLsb = pic_order_cnt_lsb_ref;
    }
    
    if ((pic_order_cnt_lsb < prevPicOrderCntLsb) &&
        ((prevPicOrderCntLsb - pic_order_cnt_lsb) >= (int)(MaxPicOrderCntLsb / 2)))
        PicOrderCntMsb = prevPicOrderCntMsb + MaxPicOrderCntLsb;
    else if ((pic_order_cnt_lsb > prevPicOrderCntLsb) &&
             ((pic_order_cnt_lsb - prevPicOrderCntLsb) > (int)(MaxPicOrderCntLsb / 2)))
        PicOrderCntMsb = prevPicOrderCntMsb - MaxPicOrderCntLsb;
    else
        PicOrderCntMsb = prevPicOrderCntMsb;
    
    TopFieldOrderCnt = PicOrderCntMsb + pic_order_cnt_lsb;

    if (current_frame_type != FRAME_B) {
        PicOrderCntMsb_ref = PicOrderCntMsb;
        pic_order_cnt_lsb_ref = pic_order_cnt_lsb;
    }
    
    return TopFieldOrderCnt;
}

static int update_RefPicList(void)
{
    unsigned int current_poc = CurrentCurrPic.TopFieldOrderCnt;
    
    if (current_frame_type == FRAME_P) {
        memcpy(RefPicList0_P, ReferenceFrames, numShortTerm * sizeof(VAPictureH264));
        sort_one(RefPicList0_P, 0, numShortTerm-1, 0, 1);
    }
    
    if (current_frame_type == FRAME_B) {
        memcpy(RefPicList0_B, ReferenceFrames, numShortTerm * sizeof(VAPictureH264));
        sort_two(RefPicList0_B, 0, numShortTerm-1, current_poc, 0,
                 1, 0, 1);

        memcpy(RefPicList1_B, ReferenceFrames, numShortTerm * sizeof(VAPictureH264));
        sort_two(RefPicList1_B, 0, numShortTerm-1, current_poc, 0,
                 0, 1, 0);
    }
    
    return 0;
}

static unsigned int va_swap32(unsigned int val)
{
    unsigned char *pval = (unsigned char *)&val;

    return ((pval[0] << 24)     |
            (pval[1] << 16)     |
            (pval[2] << 8)      |
            (pval[3] << 0));
}

static void bitstream_put_se(bitstream *bs, int val)
{
    unsigned int new_val;

    if (val <= 0)
        new_val = -2 * val;
    else
        new_val = 2 * val - 1;

    bitstream_put_ue(bs, new_val);
}

static void bitstream_put_ui(bitstream *bs, unsigned int val, int size_in_bits)
{
    int pos = (bs->bit_offset >> 5);
    int bit_offset = (bs->bit_offset & 0x1f);
    int bit_left = 32 - bit_offset;

    if (!size_in_bits)
        return;

    bs->bit_offset += size_in_bits;

    if (bit_left > size_in_bits) {
        bs->buffer[pos] = (bs->buffer[pos] << size_in_bits | val);
    } else {
        size_in_bits -= bit_left;
        bs->buffer[pos] = (bs->buffer[pos] << bit_left) | (val >> size_in_bits);
        bs->buffer[pos] = va_swap32(bs->buffer[pos]);

        if (pos + 1 == bs->max_size_in_dword) {
            bs->max_size_in_dword += BITSTREAM_ALLOCATE_STEPPING;
            bs->buffer = realloc(bs->buffer, bs->max_size_in_dword * sizeof(unsigned int));
        }

        bs->buffer[pos + 1] = val;
    }
}

static void bitstream_put_ue(bitstream *bs, unsigned int val)
{
    int size_in_bits = 0;
    int tmp_val = ++val;

    while (tmp_val) {
        tmp_val >>= 1;
        size_in_bits++;
    }

    bitstream_put_ui(bs, 0, size_in_bits - 1); // leading zero
    bitstream_put_ui(bs, val, size_in_bits);
}

static void nal_header(bitstream *bs, int nal_ref_idc, int nal_unit_type)
{
    bitstream_put_ui(bs, 0, 1);                /* forbidden_zero_bit: 0 */
    bitstream_put_ui(bs, nal_ref_idc, 2);
    bitstream_put_ui(bs, nal_unit_type, 5);
}

static void bitstream_byte_aligning(bitstream *bs, int bit)
{
    int bit_offset = (bs->bit_offset & 0x7);
    int bit_left = 8 - bit_offset;
    int new_val;

    if (!bit_offset)
        return;

    assert(bit == 0 || bit == 1);

    if (bit)
        new_val = (1 << bit_left) - 1;
    else
        new_val = 0;

    bitstream_put_ui(bs, new_val, bit_left);
}

static void nal_start_code_prefix(bitstream *bs)
{
    bitstream_put_ui(bs, 0x00000001, 32);
}

static void bitstream_start(bitstream *bs)
{
    bs->max_size_in_dword = BITSTREAM_ALLOCATE_STEPPING;
    bs->buffer = calloc(bs->max_size_in_dword * sizeof(int), 1);
    bs->bit_offset = 0;
}

static void bitstream_end(bitstream *bs)
{
    int pos = (bs->bit_offset >> 5);
    int bit_offset = (bs->bit_offset & 0x1f);
    int bit_left = 32 - bit_offset;

    if (bit_offset) {
        bs->buffer[pos] = va_swap32((bs->buffer[pos] << bit_left));
    }
}

static void slice_header(bitstream *bs)
{
    int first_mb_in_slice = slice_param.macroblock_address;

    bitstream_put_ue(bs, first_mb_in_slice);        /* first_mb_in_slice: 0 */
    bitstream_put_ue(bs, slice_param.slice_type);   /* slice_type */
    bitstream_put_ue(bs, slice_param.pic_parameter_set_id);        /* pic_parameter_set_id: 0 */
    bitstream_put_ui(bs, pic_param.frame_num, seq_param.seq_fields.bits.log2_max_frame_num_minus4 + 4); /* frame_num */

    /* frame_mbs_only_flag == 1 */
    if (!seq_param.seq_fields.bits.frame_mbs_only_flag) {
        /* FIXME: */
        assert(0);
    }

    if (pic_param.pic_fields.bits.idr_pic_flag)
        bitstream_put_ue(bs, slice_param.idr_pic_id);		/* idr_pic_id: 0 */

    if (seq_param.seq_fields.bits.pic_order_cnt_type == 0) {
        bitstream_put_ui(bs, pic_param.CurrPic.TopFieldOrderCnt, seq_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 + 4);
        /* pic_order_present_flag == 0 */
    } else {
        /* FIXME: */
        assert(0);
    }

    /* redundant_pic_cnt_present_flag == 0 */
    /* slice type */
    if (IS_P_SLICE(slice_param.slice_type)) {
        bitstream_put_ui(bs, slice_param.num_ref_idx_active_override_flag, 1);            /* num_ref_idx_active_override_flag: */

        if (slice_param.num_ref_idx_active_override_flag)
            bitstream_put_ue(bs, slice_param.num_ref_idx_l0_active_minus1);

        /* ref_pic_list_reordering */
        bitstream_put_ui(bs, 0, 1);            /* ref_pic_list_reordering_flag_l0: 0 */
    } else if (IS_B_SLICE(slice_param.slice_type)) {
        bitstream_put_ui(bs, slice_param.direct_spatial_mv_pred_flag, 1);            /* direct_spatial_mv_pred: 1 */

        bitstream_put_ui(bs, slice_param.num_ref_idx_active_override_flag, 1);       /* num_ref_idx_active_override_flag: */

        if (slice_param.num_ref_idx_active_override_flag) {
            bitstream_put_ue(bs, slice_param.num_ref_idx_l0_active_minus1);
            bitstream_put_ue(bs, slice_param.num_ref_idx_l1_active_minus1);
        }

        /* ref_pic_list_reordering */
        bitstream_put_ui(bs, 0, 1);            /* ref_pic_list_reordering_flag_l0: 0 */
        bitstream_put_ui(bs, 0, 1);            /* ref_pic_list_reordering_flag_l1: 0 */
    }

    if ((pic_param.pic_fields.bits.weighted_pred_flag &&
         IS_P_SLICE(slice_param.slice_type)) ||
        ((pic_param.pic_fields.bits.weighted_bipred_idc == 1) &&
         IS_B_SLICE(slice_param.slice_type))) {
        /* FIXME: fill weight/offset table */
        assert(0);
    }

    /* dec_ref_pic_marking */
    if (pic_param.pic_fields.bits.reference_pic_flag) {     /* nal_ref_idc != 0 */
        unsigned char no_output_of_prior_pics_flag = 0;
        unsigned char long_term_reference_flag = 0;
        unsigned char adaptive_ref_pic_marking_mode_flag = 0;

        if (pic_param.pic_fields.bits.idr_pic_flag) {
            bitstream_put_ui(bs, no_output_of_prior_pics_flag, 1);            /* no_output_of_prior_pics_flag: 0 */
            bitstream_put_ui(bs, long_term_reference_flag, 1);            /* long_term_reference_flag: 0 */
        } else {
            bitstream_put_ui(bs, adaptive_ref_pic_marking_mode_flag, 1);            /* adaptive_ref_pic_marking_mode_flag: 0 */
        }
    }

    if (pic_param.pic_fields.bits.entropy_coding_mode_flag &&
        !IS_I_SLICE(slice_param.slice_type))
        bitstream_put_ue(bs, slice_param.cabac_init_idc);               /* cabac_init_idc: 0 */

    bitstream_put_se(bs, slice_param.slice_qp_delta);                   /* slice_qp_delta: 0 */

    /* ignore for SP/SI */

    if (pic_param.pic_fields.bits.deblocking_filter_control_present_flag) {
        bitstream_put_ue(bs, slice_param.disable_deblocking_filter_idc);           /* disable_deblocking_filter_idc: 0 */

        if (slice_param.disable_deblocking_filter_idc != 1) {
            bitstream_put_se(bs, slice_param.slice_alpha_c0_offset_div2);          /* slice_alpha_c0_offset_div2: 2 */
            bitstream_put_se(bs, slice_param.slice_beta_offset_div2);              /* slice_beta_offset_div2: 2 */
        }
    }

    if (pic_param.pic_fields.bits.entropy_coding_mode_flag) {
        bitstream_byte_aligning(bs, 1);
    }
}


static int build_packed_slice_buffer(unsigned char **header_buffer)
{
    bitstream bs;
    int is_idr = !!pic_param.pic_fields.bits.idr_pic_flag;
    int is_ref = !!pic_param.pic_fields.bits.reference_pic_flag;

    bitstream_start(&bs);
    nal_start_code_prefix(&bs);

    if (IS_I_SLICE(slice_param.slice_type)) {
        nal_header(&bs, NAL_REF_IDC_HIGH, is_idr ? NAL_IDR : NAL_NON_IDR);
    } else if (IS_P_SLICE(slice_param.slice_type)) {
        nal_header(&bs, NAL_REF_IDC_MEDIUM, NAL_NON_IDR);
    } else {
        assert(IS_B_SLICE(slice_param.slice_type));
        nal_header(&bs, is_ref ? NAL_REF_IDC_LOW : NAL_REF_IDC_NONE, NAL_NON_IDR);
    }

    slice_header(&bs);
    bitstream_end(&bs);

    *header_buffer = (unsigned char *)bs.buffer;
    return bs.bit_offset;
}

static void render_packedslice()
{
    VAEncPackedHeaderParameterBuffer packedheader_param_buffer;
    VABufferID packedslice_para_bufid, packedslice_data_bufid, render_id[2];
    unsigned int length_in_bits;
    unsigned char *packedslice_buffer = NULL;
    VAStatus va_status;

    length_in_bits = build_packed_slice_buffer(&packedslice_buffer);
    packedheader_param_buffer.type = VAEncPackedHeaderSlice;
    packedheader_param_buffer.bit_length = length_in_bits;
    packedheader_param_buffer.has_emulation_bytes = 0;

    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncPackedHeaderParameterBufferType,
                               sizeof(packedheader_param_buffer), 1, &packedheader_param_buffer,
                               &packedslice_para_bufid);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");

    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncPackedHeaderDataBufferType,
                               (length_in_bits + 7) / 8, 1, packedslice_buffer,
                               &packedslice_data_bufid);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");

    render_id[0] = packedslice_para_bufid;
    render_id[1] = packedslice_data_bufid;
    va_status = vaRenderPicture(va_dpy,context_id, render_id, 2);
    CHECK_VASTATUS(va_status,"vaRenderPicture");

    free(packedslice_buffer);
}

static void rbsp_trailing_bits(bitstream *bs)
{
    bitstream_put_ui(bs, 1, 1);
    bitstream_byte_aligning(bs, 0);
}

static void sps_rbsp(bitstream *bs)
{
    int profile_idc = PROFILE_IDC_BASELINE;

    if (h264_profile  == VAProfileH264High)
        profile_idc = PROFILE_IDC_HIGH;
    else if (h264_profile  == VAProfileH264Main)
        profile_idc = PROFILE_IDC_MAIN;

    bitstream_put_ui(bs, profile_idc, 8);               /* profile_idc */
    bitstream_put_ui(bs, !!(constraint_set_flag & 1), 1);                         /* constraint_set0_flag */
    bitstream_put_ui(bs, !!(constraint_set_flag & 2), 1);                         /* constraint_set1_flag */
    bitstream_put_ui(bs, !!(constraint_set_flag & 4), 1);                         /* constraint_set2_flag */
    bitstream_put_ui(bs, !!(constraint_set_flag & 8), 1);                         /* constraint_set3_flag */
    bitstream_put_ui(bs, 0, 4);                         /* reserved_zero_4bits */
    bitstream_put_ui(bs, seq_param.level_idc, 8);      /* level_idc */
    bitstream_put_ue(bs, seq_param.seq_parameter_set_id);      /* seq_parameter_set_id */

    if ( profile_idc == PROFILE_IDC_HIGH) {
        bitstream_put_ue(bs, 1);        /* chroma_format_idc = 1, 4:2:0 */ 
        bitstream_put_ue(bs, 0);        /* bit_depth_luma_minus8 */
        bitstream_put_ue(bs, 0);        /* bit_depth_chroma_minus8 */
        bitstream_put_ui(bs, 0, 1);     /* qpprime_y_zero_transform_bypass_flag */
        bitstream_put_ui(bs, 0, 1);     /* seq_scaling_matrix_present_flag */
    }

    bitstream_put_ue(bs, seq_param.seq_fields.bits.log2_max_frame_num_minus4); /* log2_max_frame_num_minus4 */
    bitstream_put_ue(bs, seq_param.seq_fields.bits.pic_order_cnt_type);        /* pic_order_cnt_type */

    if (seq_param.seq_fields.bits.pic_order_cnt_type == 0)
        bitstream_put_ue(bs, seq_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4);     /* log2_max_pic_order_cnt_lsb_minus4 */
    else {
        assert(0);
    }

    bitstream_put_ue(bs, seq_param.max_num_ref_frames);        /* num_ref_frames */
    bitstream_put_ui(bs, 0, 1);                                 /* gaps_in_frame_num_value_allowed_flag */

    bitstream_put_ue(bs, seq_param.picture_width_in_mbs - 1);  /* pic_width_in_mbs_minus1 */
    bitstream_put_ue(bs, seq_param.picture_height_in_mbs - 1); /* pic_height_in_map_units_minus1 */
    bitstream_put_ui(bs, seq_param.seq_fields.bits.frame_mbs_only_flag, 1);    /* frame_mbs_only_flag */

    if (!seq_param.seq_fields.bits.frame_mbs_only_flag) {
        assert(0);
    }

    bitstream_put_ui(bs, seq_param.seq_fields.bits.direct_8x8_inference_flag, 1);      /* direct_8x8_inference_flag */
    bitstream_put_ui(bs, seq_param.frame_cropping_flag, 1);            /* frame_cropping_flag */

    if (seq_param.frame_cropping_flag) {
        bitstream_put_ue(bs, seq_param.frame_crop_left_offset);        /* frame_crop_left_offset */
        bitstream_put_ue(bs, seq_param.frame_crop_right_offset);       /* frame_crop_right_offset */
        bitstream_put_ue(bs, seq_param.frame_crop_top_offset);         /* frame_crop_top_offset */
        bitstream_put_ue(bs, seq_param.frame_crop_bottom_offset);      /* frame_crop_bottom_offset */
    }
    
    //if ( frame_bit_rate < 0 ) { //TODO EW: the vui header isn't correct
    if ( 1 ) {
        bitstream_put_ui(bs, 0, 1); /* vui_parameters_present_flag */
    } else {
        bitstream_put_ui(bs, 1, 1); /* vui_parameters_present_flag */
        bitstream_put_ui(bs, 0, 1); /* aspect_ratio_info_present_flag */
        bitstream_put_ui(bs, 0, 1); /* overscan_info_present_flag */
        bitstream_put_ui(bs, 0, 1); /* video_signal_type_present_flag */
        bitstream_put_ui(bs, 0, 1); /* chroma_loc_info_present_flag */
        bitstream_put_ui(bs, 1, 1); /* timing_info_present_flag */
        {
            bitstream_put_ui(bs, 15, 32);
            bitstream_put_ui(bs, 900, 32);
            bitstream_put_ui(bs, 1, 1);
        }
        bitstream_put_ui(bs, 1, 1); /* nal_hrd_parameters_present_flag */
        {
            // hrd_parameters 
            bitstream_put_ue(bs, 0);    /* cpb_cnt_minus1 */
            bitstream_put_ui(bs, 4, 4); /* bit_rate_scale */
            bitstream_put_ui(bs, 6, 4); /* cpb_size_scale */
           
            bitstream_put_ue(bs, frame_bitrate - 1); /* bit_rate_value_minus1[0] */
            bitstream_put_ue(bs, frame_bitrate*8 - 1); /* cpb_size_value_minus1[0] */
            bitstream_put_ui(bs, 1, 1);  /* cbr_flag[0] */

            bitstream_put_ui(bs, 23, 5);   /* initial_cpb_removal_delay_length_minus1 */
            bitstream_put_ui(bs, 23, 5);   /* cpb_removal_delay_length_minus1 */
            bitstream_put_ui(bs, 23, 5);   /* dpb_output_delay_length_minus1 */
            bitstream_put_ui(bs, 23, 5);   /* time_offset_length  */
        }
        bitstream_put_ui(bs, 0, 1);   /* vcl_hrd_parameters_present_flag */
        bitstream_put_ui(bs, 0, 1);   /* low_delay_hrd_flag */ 

        bitstream_put_ui(bs, 0, 1); /* pic_struct_present_flag */
        bitstream_put_ui(bs, 0, 1); /* bitstream_restriction_flag */
    }

    rbsp_trailing_bits(bs);     /* rbsp_trailing_bits */
}

static int build_packed_seq_buffer(unsigned char **header_buffer)
{
    bitstream bs;

    bitstream_start(&bs);
    nal_start_code_prefix(&bs);
    nal_header(&bs, NAL_REF_IDC_HIGH, NAL_SPS);
    sps_rbsp(&bs);
    bitstream_end(&bs);

    *header_buffer = (unsigned char *)bs.buffer;
    return bs.bit_offset;
}

static void pps_rbsp(bitstream *bs)
{
    bitstream_put_ue(bs, pic_param.pic_parameter_set_id);      /* pic_parameter_set_id */
    bitstream_put_ue(bs, pic_param.seq_parameter_set_id);      /* seq_parameter_set_id */

    bitstream_put_ui(bs, pic_param.pic_fields.bits.entropy_coding_mode_flag, 1);  /* entropy_coding_mode_flag */

    bitstream_put_ui(bs, 0, 1);                         /* pic_order_present_flag: 0 */

    bitstream_put_ue(bs, 0);                            /* num_slice_groups_minus1 */

    bitstream_put_ue(bs, pic_param.num_ref_idx_l0_active_minus1);      /* num_ref_idx_l0_active_minus1 */
    bitstream_put_ue(bs, pic_param.num_ref_idx_l1_active_minus1);      /* num_ref_idx_l1_active_minus1 1 */

    bitstream_put_ui(bs, pic_param.pic_fields.bits.weighted_pred_flag, 1);     /* weighted_pred_flag: 0 */
    bitstream_put_ui(bs, pic_param.pic_fields.bits.weighted_bipred_idc, 2);	/* weighted_bipred_idc: 0 */

    bitstream_put_se(bs, pic_param.pic_init_qp - 26);  /* pic_init_qp_minus26 */
    bitstream_put_se(bs, 0);                            /* pic_init_qs_minus26 */
    bitstream_put_se(bs, 0);                            /* chroma_qp_index_offset */

    bitstream_put_ui(bs, pic_param.pic_fields.bits.deblocking_filter_control_present_flag, 1); /* deblocking_filter_control_present_flag */
    bitstream_put_ui(bs, 0, 1);                         /* constrained_intra_pred_flag */
    bitstream_put_ui(bs, 0, 1);                         /* redundant_pic_cnt_present_flag */
    
    /* more_rbsp_data */
    bitstream_put_ui(bs, pic_param.pic_fields.bits.transform_8x8_mode_flag, 1);    /*transform_8x8_mode_flag */
    bitstream_put_ui(bs, 0, 1);                         /* pic_scaling_matrix_present_flag */
    bitstream_put_se(bs, pic_param.second_chroma_qp_index_offset );    /*second_chroma_qp_index_offset */

    rbsp_trailing_bits(bs);
}

static int build_packed_pic_buffer(unsigned char **header_buffer)
{
    bitstream bs;

    bitstream_start(&bs);
    nal_start_code_prefix(&bs);
    nal_header(&bs, NAL_REF_IDC_HIGH, NAL_PPS);
    pps_rbsp(&bs);
    bitstream_end(&bs);

    *header_buffer = (unsigned char *)bs.buffer;
    return bs.bit_offset;
}

static int render_packedpicture(void)
{
    VAEncPackedHeaderParameterBuffer packedheader_param_buffer;
    VABufferID packedpic_para_bufid, packedpic_data_bufid, render_id[2];
    unsigned int length_in_bits;
    unsigned char *packedpic_buffer = NULL;
    VAStatus va_status;

    length_in_bits = build_packed_pic_buffer(&packedpic_buffer); 
    packedheader_param_buffer.type = VAEncPackedHeaderPicture;
    packedheader_param_buffer.bit_length = length_in_bits;
    packedheader_param_buffer.has_emulation_bytes = 0;

    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncPackedHeaderParameterBufferType,
                               sizeof(packedheader_param_buffer), 1, &packedheader_param_buffer,
                               &packedpic_para_bufid);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");

    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncPackedHeaderDataBufferType,
                               (length_in_bits + 7) / 8, 1, packedpic_buffer,
                               &packedpic_data_bufid);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");

    render_id[0] = packedpic_para_bufid;
    render_id[1] = packedpic_data_bufid;
    va_status = vaRenderPicture(va_dpy,context_id, render_id, 2);
    CHECK_VASTATUS(va_status,"vaRenderPicture");

    free(packedpic_buffer);
    
    return 0;
}


static int render_packedsequence(void)
{
    VAEncPackedHeaderParameterBuffer packedheader_param_buffer;
    VABufferID packedseq_para_bufid, packedseq_data_bufid, render_id[2];
    unsigned int length_in_bits;
    unsigned char *packedseq_buffer = NULL;
    VAStatus va_status;

    length_in_bits = build_packed_seq_buffer(&packedseq_buffer); 
    
    packedheader_param_buffer.type = VAEncPackedHeaderSequence;
    
    packedheader_param_buffer.bit_length = length_in_bits; /*length_in_bits*/
    packedheader_param_buffer.has_emulation_bytes = 0;
    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncPackedHeaderParameterBufferType,
                               sizeof(packedheader_param_buffer), 1, &packedheader_param_buffer,
                               &packedseq_para_bufid);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");

    va_status = vaCreateBuffer(va_dpy,
                               context_id,
                               VAEncPackedHeaderDataBufferType,
                               (length_in_bits + 7) / 8, 1, packedseq_buffer,
                               &packedseq_data_bufid);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");

    render_id[0] = packedseq_para_bufid;
    render_id[1] = packedseq_data_bufid;
    va_status = vaRenderPicture(va_dpy,context_id, render_id, 2);
    CHECK_VASTATUS(va_status,"vaRenderPicture");

    free(packedseq_buffer);
    
    return 0;
}

static int render_slice(void)
{
    VABufferID slice_param_buf;
    VAStatus va_status;
    int i;

    update_RefPicList();
    
    /* one frame, one slice */
    slice_param.macroblock_address = 0;
    slice_param.num_macroblocks = frame_width_mbaligned * frame_height_mbaligned/(16*16); /* Measured by MB */
    slice_param.slice_type = (current_frame_type == FRAME_IDR)?2:current_frame_type;
    if (current_frame_type == FRAME_IDR) {
        if (current_frame_encoding != 0)
            ++slice_param.idr_pic_id;
    } else if (current_frame_type == FRAME_P) {
        int refpiclist0_max = h264_maxref & 0xffff;
        memcpy(slice_param.RefPicList0, RefPicList0_P, refpiclist0_max*sizeof(VAPictureH264));

        for (i = refpiclist0_max; i < 32; i++) {
            slice_param.RefPicList0[i].picture_id = VA_INVALID_SURFACE;
            slice_param.RefPicList0[i].flags = VA_PICTURE_H264_INVALID;
        }
    } else if (current_frame_type == FRAME_B) {
        int refpiclist0_max = h264_maxref & 0xffff;
        int refpiclist1_max = (h264_maxref >> 16) & 0xffff;

        memcpy(slice_param.RefPicList0, RefPicList0_B, refpiclist0_max*sizeof(VAPictureH264));
        for (i = refpiclist0_max; i < 32; i++) {
            slice_param.RefPicList0[i].picture_id = VA_INVALID_SURFACE;
            slice_param.RefPicList0[i].flags = VA_PICTURE_H264_INVALID;
        }

        memcpy(slice_param.RefPicList1, RefPicList1_B, refpiclist1_max*sizeof(VAPictureH264));
        for (i = refpiclist1_max; i < 32; i++) {
            slice_param.RefPicList1[i].picture_id = VA_INVALID_SURFACE;
            slice_param.RefPicList1[i].flags = VA_PICTURE_H264_INVALID;
        }
    }

    slice_param.slice_alpha_c0_offset_div2 = 0;
    slice_param.slice_beta_offset_div2 = 0;
    slice_param.direct_spatial_mv_pred_flag = 1;
    slice_param.pic_order_cnt_lsb = (current_frame_display - current_IDR_display) % MaxPicOrderCntLsb;
    

    if (h264_packedheader &&
        config_attrib[enc_packed_header_idx].value & VA_ENC_PACKED_HEADER_SLICE)
        render_packedslice();

    va_status = vaCreateBuffer(va_dpy,context_id,VAEncSliceParameterBufferType,
                               sizeof(slice_param),1,&slice_param,&slice_param_buf);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");;

    va_status = vaRenderPicture(va_dpy,context_id, &slice_param_buf, 1);
    CHECK_VASTATUS(va_status,"vaRenderPicture");
    
    return 0;
}

static int render_picture(void)
{
    VABufferID pic_param_buf;
    VAStatus va_status;
    int i = 0;

    pic_param.CurrPic.picture_id = ref_surface[current_slot];
    pic_param.CurrPic.frame_idx = current_frame_num;
    pic_param.CurrPic.flags = 0;
    pic_param.CurrPic.TopFieldOrderCnt = calc_poc((current_frame_display - current_IDR_display) % MaxPicOrderCntLsb);
    pic_param.CurrPic.BottomFieldOrderCnt = pic_param.CurrPic.TopFieldOrderCnt;
    CurrentCurrPic = pic_param.CurrPic;

    if (getenv("TO_DEL")) { /* set RefPicList into ReferenceFrames */
        update_RefPicList(); /* calc RefPicList */
        memset(pic_param.ReferenceFrames, 0xff, 16 * sizeof(VAPictureH264)); /* invalid all */
        if (current_frame_type == FRAME_P) {
            pic_param.ReferenceFrames[0] = RefPicList0_P[0];
        } else if (current_frame_type == FRAME_B) {
            pic_param.ReferenceFrames[0] = RefPicList0_B[0];
            pic_param.ReferenceFrames[1] = RefPicList1_B[0];
        }
    } else {
        memcpy(pic_param.ReferenceFrames, ReferenceFrames, numShortTerm*sizeof(VAPictureH264));
        for (i = numShortTerm; i < SURFACE_NUM; i++) {
            pic_param.ReferenceFrames[i].picture_id = VA_INVALID_SURFACE;
            pic_param.ReferenceFrames[i].flags = VA_PICTURE_H264_INVALID;
        }
    }
    
    pic_param.pic_fields.bits.idr_pic_flag = (current_frame_type == FRAME_IDR);
    pic_param.pic_fields.bits.reference_pic_flag = (current_frame_type != FRAME_B);
    pic_param.pic_fields.bits.entropy_coding_mode_flag = h264_entropy_mode;
    pic_param.pic_fields.bits.deblocking_filter_control_present_flag = 1;
    pic_param.frame_num = current_frame_num;
    pic_param.coded_buf = coded_buf[current_slot];
    pic_param.last_picture = (current_frame_encoding == frame_count);
    pic_param.pic_init_qp = initial_qp;

    va_status = vaCreateBuffer(va_dpy, context_id,VAEncPictureParameterBufferType,
                               sizeof(pic_param),1,&pic_param, &pic_param_buf);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");;

    va_status = vaRenderPicture(va_dpy,context_id, &pic_param_buf, 1);
    CHECK_VASTATUS(va_status,"vaRenderPicture");

    return 0;
}

static int render_sequence(void)
{
    VABufferID seq_param_buf, rc_param_buf, misc_param_tmpbuf, render_id[2];
    VAStatus va_status;
    VAEncMiscParameterBuffer *misc_param, *misc_param_tmp;
    VAEncMiscParameterRateControl *misc_rate_ctrl;
    
    seq_param.level_idc = 41 /*SH_LEVEL_3*/;
    seq_param.picture_width_in_mbs = frame_width_mbaligned / 16;
    seq_param.picture_height_in_mbs = frame_height_mbaligned / 16;
    seq_param.bits_per_second = frame_bitrate;

    seq_param.intra_period = intra_period;
    seq_param.intra_idr_period = intra_idr_period;
    seq_param.ip_period = ip_period;

    seq_param.max_num_ref_frames = num_ref_frames;
    seq_param.seq_fields.bits.frame_mbs_only_flag = 1;
    seq_param.time_scale = 900;
    seq_param.num_units_in_tick = 15; /* Tc = num_units_in_tick / time_sacle */
    seq_param.seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4 = Log2MaxPicOrderCntLsb - 4;
    seq_param.seq_fields.bits.log2_max_frame_num_minus4 = Log2MaxFrameNum - 4;;
    seq_param.seq_fields.bits.frame_mbs_only_flag = 1;
    seq_param.seq_fields.bits.chroma_format_idc = 1;
    seq_param.seq_fields.bits.direct_8x8_inference_flag = 1;
    
    if (frame_width != frame_width_mbaligned ||
        frame_height != frame_height_mbaligned) {
        seq_param.frame_cropping_flag = 1;
        seq_param.frame_crop_left_offset = 0;
        seq_param.frame_crop_right_offset = (frame_width_mbaligned - frame_width)/2;
        seq_param.frame_crop_top_offset = 0;
        seq_param.frame_crop_bottom_offset = (frame_height_mbaligned - frame_height)/2;
    }
    
    va_status = vaCreateBuffer(va_dpy, context_id,
                               VAEncSequenceParameterBufferType,
                               sizeof(seq_param),1,&seq_param,&seq_param_buf);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");
    
    va_status = vaCreateBuffer(va_dpy, context_id,
                               VAEncMiscParameterBufferType,
                               sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterRateControl),
                               1,NULL,&rc_param_buf);
    CHECK_VASTATUS(va_status,"vaCreateBuffer");
    
    vaMapBuffer(va_dpy, rc_param_buf,(void **)&misc_param);
    misc_param->type = VAEncMiscParameterTypeRateControl;
    misc_rate_ctrl = (VAEncMiscParameterRateControl *)misc_param->data;
    memset(misc_rate_ctrl, 0, sizeof(*misc_rate_ctrl));
    misc_rate_ctrl->bits_per_second = frame_bitrate;
    misc_rate_ctrl->target_percentage = 66;
    misc_rate_ctrl->window_size = 1000;
    misc_rate_ctrl->initial_qp = initial_qp;
    misc_rate_ctrl->min_qp = minimal_qp;
    misc_rate_ctrl->basic_unit_size = 0;
    vaUnmapBuffer(va_dpy, rc_param_buf);

    render_id[0] = seq_param_buf;
    render_id[1] = rc_param_buf;
    
    va_status = vaRenderPicture(va_dpy,context_id, &render_id[0], 2);
    CHECK_VASTATUS(va_status,"vaRenderPicture");;

    if (misc_priv_type != 0) {
        va_status = vaCreateBuffer(va_dpy, context_id,
                                   VAEncMiscParameterBufferType,
                                   sizeof(VAEncMiscParameterBuffer),
                                   1, NULL, &misc_param_tmpbuf);
        CHECK_VASTATUS(va_status,"vaCreateBuffer");
        vaMapBuffer(va_dpy, misc_param_tmpbuf,(void **)&misc_param_tmp);
        misc_param_tmp->type = misc_priv_type;
        misc_param_tmp->data[0] = misc_priv_value;
        vaUnmapBuffer(va_dpy, misc_param_tmpbuf);
    
        va_status = vaRenderPicture(va_dpy,context_id, &misc_param_tmpbuf, 1);
    }
    
    return 0;
}
/* end the render implemet */

/* the work function */
static int update_ReferenceFrames(void)
{
    int i;
    
    if (current_frame_type == FRAME_B)
        return 0;

    CurrentCurrPic.flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    numShortTerm++;
    if (numShortTerm > num_ref_frames)
        numShortTerm = num_ref_frames;
    for (i=numShortTerm-1; i>0; i--)
        ReferenceFrames[i] = ReferenceFrames[i-1];
    ReferenceFrames[0] = CurrentCurrPic;
    
    if (current_frame_type != FRAME_B)
        current_frame_num++;
    if (current_frame_num > MaxFrameNum)
        current_frame_num = 0;
    
    return 0;
}

static int storage_task_queue(unsigned long long display_order, unsigned long long encode_order)
{
    struct storage_task_t *tmp;

    tmp = calloc(1, sizeof(struct storage_task_t));
    tmp->display_order = display_order;
    tmp->encode_order = encode_order;

    pthread_mutex_lock(&encode_mutex);
    
    if (storage_task_header == NULL) {
        storage_task_header = tmp;
        storage_task_tail = tmp;
    } else {
        storage_task_tail->next = tmp;
        storage_task_tail = tmp;
    }

    srcsurface_status[display_order % SURFACE_NUM] = SRC_SURFACE_IN_STORAGE;
    pthread_cond_signal(&encode_cond);
    
    pthread_mutex_unlock(&encode_mutex);
    
    return 0;
}

static struct storage_task_t * storage_task_dequeue(void)
{
    struct storage_task_t *header;

    pthread_mutex_lock(&encode_mutex);

    header = storage_task_header;    
    if (storage_task_header != NULL) {
        if (storage_task_tail == storage_task_header)
            storage_task_tail = NULL;
        storage_task_header = header->next;
    }
    
    pthread_mutex_unlock(&encode_mutex);
    
    return header;
}

static int save_codeddata(unsigned long long display_order, unsigned long long encode_order)
{    
    VACodedBufferSegment *buf_list = NULL;
    VAStatus va_status;
    unsigned int coded_size = 0;

    va_status = vaMapBuffer(va_dpy,coded_buf[display_order % SURFACE_NUM],(void **)(&buf_list));
    CHECK_VASTATUS(va_status,"vaMapBuffer");
    while (buf_list != NULL) {
        coded_size += fwrite(buf_list->buf, 1, buf_list->size, coded_fp);
        buf_list = (VACodedBufferSegment *) buf_list->next;

        frame_size += coded_size;
    }
    vaUnmapBuffer(va_dpy,coded_buf[display_order % SURFACE_NUM]);
#if 0
    printf("\r      "); /* return back to startpoint */
    switch (encode_order % 4) {
        case 0:
            printf("|");
            break;
        case 1:
            printf("/");
            break;
        case 2:
            printf("-");
            break;
        case 3:
            printf("\\");
            break;
    }
    printf("%08lld", encode_order);
    printf("(%06d bytes coded)",coded_size);
#endif

    fflush(coded_fp);
    
    return 0;
}

int writeYuvFile(uint8_t *rgb_buffer, uint8_t *yuv_buffer ,int src_w, int src_h)
{
	int dst_w=src_w,dst_h=src_h;
	sws_scale(img_convert_ctx, (const uint8_t * const*)&rgb_buffer, src_linesize, 0, src_h, dst_data, dst_linesize);
	memcpy(yuv_buffer,dst_data[0],dst_w*dst_h);
	yuv_buffer += dst_w*dst_h;
	memcpy(yuv_buffer ,dst_data[1],dst_w*dst_h/4);
	yuv_buffer += dst_w*dst_h/4;
	memcpy(yuv_buffer ,dst_data[2],dst_w*dst_h/4);
	return 0;
}

static int load_surface(VASurfaceID surface_id, unsigned long long display_order)
{
    int u_pos = screen_width * screen_height;
    int v_pos = (screen_width / 2) * (screen_height / 2);
    unsigned char *srcyuv_ptr = NULL, *src_Y = NULL, *src_U = NULL, *src_V = NULL;

    XImage * img;
    img = XGetImage(dsp,desktop,0,0,screen_width,screen_height,~0,ZPixmap);

    //paint_mouse_pointer(img, dsp, screen_width, screen_height);
    writeYuvFile((unsigned char *)img->data, (unsigned char *)g_yuv_buffer, screen_width, screen_height);
    srcyuv_ptr = (unsigned char *) g_yuv_buffer;//(unsigned char *)mmap_ptr +  (frame_start & 0xfff);

    src_Y = srcyuv_ptr;
    src_U = src_Y + u_pos;
    src_V = src_U + v_pos;

    XDestroyImage(img);
    upload_surface_yuv(va_dpy, surface_id,
                       srcyuv_fourcc, frame_width, frame_height,
                       src_Y, src_U, src_V);
    return 0;
}

static void storage_task(unsigned long long display_order, unsigned long long encode_order)
{
    unsigned int tmp;
    VAStatus va_status;
    
    tmp = GetTickCount();
    va_status = vaSyncSurface(va_dpy, src_surface[display_order % SURFACE_NUM]);
    CHECK_VASTATUS(va_status,"vaSyncSurface");
    SyncPictureTicks += GetTickCount() - tmp;
    tmp = GetTickCount();
    save_codeddata(display_order, encode_order);
    SavePictureTicks += GetTickCount() - tmp;

    //save_recyuv(ref_surface[display_order % SURFACE_NUM], display_order, encode_order);

    /* reload a new frame data */
    tmp = GetTickCount();
    if (1)
        load_surface(src_surface[display_order % SURFACE_NUM], display_order + SURFACE_NUM);
    UploadPictureTicks += GetTickCount() - tmp;

    pthread_mutex_lock(&encode_mutex);
    srcsurface_status[display_order % SURFACE_NUM] = SRC_SURFACE_IN_ENCODING;
    pthread_mutex_unlock(&encode_mutex);
}


static void * storage_task_thread(void *t)
{
    while (1) {
        struct storage_task_t *current;
        
        current = storage_task_dequeue();
        if (current == NULL) {
            pthread_mutex_lock(&encode_mutex);
            pthread_cond_wait(&encode_cond, &encode_mutex);
            pthread_mutex_unlock(&encode_mutex);
            continue;
        }
        
        storage_task(current->display_order, current->encode_order);
        
        free(current);

        /* all frames are saved, exit the thread */
        if (++frame_coded >= frame_count)
            break;
    }

    return 0;
}

void encoding2display_order(
    unsigned long long encoding_order,int intra_period,
    int intra_idr_period,int ip_period,
    unsigned long long *displaying_order,
    int *frame_type)
{
    int encoding_order_gop = 0;

    if (intra_period == 1) { /* all are I/IDR frames */
        *displaying_order = encoding_order;
        if (intra_idr_period == 0)
            *frame_type = (encoding_order == 0)?FRAME_IDR:FRAME_I;
        else
            *frame_type = (encoding_order % intra_idr_period == 0)?FRAME_IDR:FRAME_I;
        return;
    }

    if (intra_period == 0)
        intra_idr_period = 0;

    /* new sequence like
     * IDR PPPPP IPPPPP
     * IDR (PBB)(PBB)(IBB)(PBB)
     */
    encoding_order_gop = (intra_idr_period == 0)? encoding_order:
        (encoding_order % (intra_idr_period + ((ip_period == 1)?0:1)));
         
    if (encoding_order_gop == 0) { /* the first frame */
        *frame_type = FRAME_IDR;
        *displaying_order = encoding_order;
    } else if (((encoding_order_gop - 1) % ip_period) != 0) { /* B frames */
	*frame_type = FRAME_B;
        *displaying_order = encoding_order - 1;
    } else if ((intra_period != 0) && /* have I frames */
               (encoding_order_gop >= 2) &&
               ((ip_period == 1 && encoding_order_gop % intra_period == 0) || /* for IDR PPPPP IPPPP */
                /* for IDR (PBB)(PBB)(IBB) */
                (ip_period >= 2 && ((encoding_order_gop - 1) / ip_period % (intra_period / ip_period)) == 0))) {
	*frame_type = FRAME_I;
	*displaying_order = encoding_order + ip_period - 1;
    } else {
	*frame_type = FRAME_P;
	*displaying_order = encoding_order + ip_period - 1;
    }
}

/* end the work function */

/* the intel hardware init function */
static int init_va(void)
{
    VAProfile profile_list[]={VAProfileH264High,VAProfileH264Main,VAProfileH264Baseline,VAProfileH264ConstrainedBaseline};
    VAEntrypoint *entrypoints;
    int num_entrypoints, slice_entrypoint;
    int support_encode = 0;    
    int major_ver, minor_ver;
    VAStatus va_status;
    unsigned int i;

    va_dpy = va_open_display();
    va_status = vaInitialize(va_dpy, &major_ver, &minor_ver);
    CHECK_VASTATUS(va_status, "vaInitialize");

    num_entrypoints = vaMaxNumEntrypoints(va_dpy);
    entrypoints = malloc(num_entrypoints * sizeof(*entrypoints));
    if (!entrypoints) {
        fprintf(stderr, "error: failed to initialize VA entrypoints array\n");
        exit(1);
    }

    /* use the highest profile */
    for (i = 0; i < sizeof(profile_list)/sizeof(profile_list[0]); i++) {
        if ((h264_profile != ~0) && h264_profile != profile_list[i])
            continue;
        
        h264_profile = profile_list[i];
        vaQueryConfigEntrypoints(va_dpy, h264_profile, entrypoints, &num_entrypoints);
        for (slice_entrypoint = 0; slice_entrypoint < num_entrypoints; slice_entrypoint++) {
            if (entrypoints[slice_entrypoint] == VAEntrypointEncSlice) {
                support_encode = 1;
                break;
            }
        }
        if (support_encode == 1)
            break;
    }
    
    if (support_encode == 0) {
        printf("Can't find VAEntrypointEncSlice for H264 profiles\n");
        exit(1);
    } else {
        switch (h264_profile) {
            case VAProfileH264Baseline:
                printf("Use profile VAProfileH264Baseline\n");
                ip_period = 1;
                constraint_set_flag |= (1 << 0); /* Annex A.2.1 */
                h264_entropy_mode = 0;
                break;
            case VAProfileH264ConstrainedBaseline:
                printf("Use profile VAProfileH264ConstrainedBaseline\n");
                constraint_set_flag |= (1 << 0 | 1 << 1); /* Annex A.2.2 */
                ip_period = 1;
                break;

            case VAProfileH264Main:
                printf("Use profile VAProfileH264Main\n");
                constraint_set_flag |= (1 << 1); /* Annex A.2.2 */
                break;

            case VAProfileH264High:
                constraint_set_flag |= (1 << 3); /* Annex A.2.4 */
                printf("Use profile VAProfileH264High\n");
                break;
            default:
                printf("unknow profile. Set to Baseline");
                h264_profile = VAProfileH264Baseline;
                ip_period = 1;
                constraint_set_flag |= (1 << 0); /* Annex A.2.1 */
                break;
        }
    }

    /* find out the format for the render target, and rate control mode */
    for (i = 0; i < VAConfigAttribTypeMax; i++)
        attrib[i].type = i;

    va_status = vaGetConfigAttributes(va_dpy, h264_profile, VAEntrypointEncSlice,
                                      &attrib[0], VAConfigAttribTypeMax);
    CHECK_VASTATUS(va_status, "vaGetConfigAttributes");
    /* check the interested configattrib */
    if ((attrib[VAConfigAttribRTFormat].value & VA_RT_FORMAT_YUV420) == 0) {
        printf("Not find desired YUV420 RT format\n");
        exit(1);
    } else {
        config_attrib[config_attrib_num].type = VAConfigAttribRTFormat;
        config_attrib[config_attrib_num].value = VA_RT_FORMAT_YUV420;
        config_attrib_num++;
    }
    
    if (attrib[VAConfigAttribRateControl].value != VA_ATTRIB_NOT_SUPPORTED) {
        int tmp = attrib[VAConfigAttribRateControl].value;

        printf("Support rate control mode (0x%x):", tmp);
        
        if (tmp & VA_RC_NONE)
            printf("NONE ");
        if (tmp & VA_RC_CBR)
            printf("CBR ");
        if (tmp & VA_RC_VBR)
            printf("VBR ");
        if (tmp & VA_RC_VCM)
            printf("VCM ");
        if (tmp & VA_RC_CQP)
            printf("CQP ");
        if (tmp & VA_RC_VBR_CONSTRAINED)
            printf("VBR_CONSTRAINED ");

        printf("\n");

        if (rc_mode == -1 || !(rc_mode & tmp))  {
            if (rc_mode != -1) {
                printf("Warning: Don't support the specified RateControl mode: %s!!!, switch to ", rc_to_string(rc_mode));
            }

            for (i = 0; i < sizeof(rc_default_modes) / sizeof(rc_default_modes[0]); i++) {
                if (rc_default_modes[i] & tmp) {
                    rc_mode = rc_default_modes[i];
                    break;
                }
            }

            printf("RateControl mode: %s\n", rc_to_string(rc_mode));
        }

        config_attrib[config_attrib_num].type = VAConfigAttribRateControl;
        config_attrib[config_attrib_num].value = rc_mode;
        config_attrib_num++;
    }
    

    if (attrib[VAConfigAttribEncPackedHeaders].value != VA_ATTRIB_NOT_SUPPORTED) {
        int tmp = attrib[VAConfigAttribEncPackedHeaders].value;

        printf("Support VAConfigAttribEncPackedHeaders\n");
        
        h264_packedheader = 1;
        config_attrib[config_attrib_num].type = VAConfigAttribEncPackedHeaders;
        config_attrib[config_attrib_num].value = VA_ENC_PACKED_HEADER_NONE;
        
        if (tmp & VA_ENC_PACKED_HEADER_SEQUENCE) {
            printf("Support packed sequence headers\n");
            config_attrib[config_attrib_num].value |= VA_ENC_PACKED_HEADER_SEQUENCE;
        }
        
        if (tmp & VA_ENC_PACKED_HEADER_PICTURE) {
            printf("Support packed picture headers\n");
            config_attrib[config_attrib_num].value |= VA_ENC_PACKED_HEADER_PICTURE;
        }
        
        if (tmp & VA_ENC_PACKED_HEADER_SLICE) {
            printf("Support packed slice headers\n");
            config_attrib[config_attrib_num].value |= VA_ENC_PACKED_HEADER_SLICE;
        }
        
        if (tmp & VA_ENC_PACKED_HEADER_MISC) {
            printf("Support packed misc headers\n");
            config_attrib[config_attrib_num].value |= VA_ENC_PACKED_HEADER_MISC;
        }
        
        enc_packed_header_idx = config_attrib_num;
        config_attrib_num++;
    }

    if (attrib[VAConfigAttribEncInterlaced].value != VA_ATTRIB_NOT_SUPPORTED) {
        int tmp = attrib[VAConfigAttribEncInterlaced].value;
        
        printf("Support VAConfigAttribEncInterlaced\n");

        if (tmp & VA_ENC_INTERLACED_FRAME)
            printf("support VA_ENC_INTERLACED_FRAME\n");
        if (tmp & VA_ENC_INTERLACED_FIELD)
            printf("Support VA_ENC_INTERLACED_FIELD\n");
        if (tmp & VA_ENC_INTERLACED_MBAFF)
            printf("Support VA_ENC_INTERLACED_MBAFF\n");
        if (tmp & VA_ENC_INTERLACED_PAFF)
            printf("Support VA_ENC_INTERLACED_PAFF\n");
        
        config_attrib[config_attrib_num].type = VAConfigAttribEncInterlaced;
        config_attrib[config_attrib_num].value = VA_ENC_PACKED_HEADER_NONE;
        config_attrib_num++;
    }
    
    if (attrib[VAConfigAttribEncMaxRefFrames].value != VA_ATTRIB_NOT_SUPPORTED) {
        h264_maxref = attrib[VAConfigAttribEncMaxRefFrames].value;
        
        printf("Support %d RefPicList0 and %d RefPicList1\n",
               h264_maxref & 0xffff, (h264_maxref >> 16) & 0xffff );
    }

    if (attrib[VAConfigAttribEncMaxSlices].value != VA_ATTRIB_NOT_SUPPORTED)
        printf("Support %d slices\n", attrib[VAConfigAttribEncMaxSlices].value);

    if (attrib[VAConfigAttribEncSliceStructure].value != VA_ATTRIB_NOT_SUPPORTED) {
        int tmp = attrib[VAConfigAttribEncSliceStructure].value;
        
        printf("Support VAConfigAttribEncSliceStructure\n");

        if (tmp & VA_ENC_SLICE_STRUCTURE_ARBITRARY_ROWS)
            printf("Support VA_ENC_SLICE_STRUCTURE_ARBITRARY_ROWS\n");
        if (tmp & VA_ENC_SLICE_STRUCTURE_POWER_OF_TWO_ROWS)
            printf("Support VA_ENC_SLICE_STRUCTURE_POWER_OF_TWO_ROWS\n");
        if (tmp & VA_ENC_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS)
            printf("Support VA_ENC_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS\n");
    }
    if (attrib[VAConfigAttribEncMacroblockInfo].value != VA_ATTRIB_NOT_SUPPORTED) {
        printf("Support VAConfigAttribEncMacroblockInfo\n");
    }

    free(entrypoints);
    return 0;
}

static int setup_encode()
{
    VAStatus va_status;
    VASurfaceID *tmp_surfaceid;
    int codedbuf_size, i;
    
    va_status = vaCreateConfig(va_dpy, h264_profile, VAEntrypointEncSlice,
            &config_attrib[0], config_attrib_num, &config_id);
    CHECK_VASTATUS(va_status, "vaCreateConfig");

    /* create source surfaces */
    va_status = vaCreateSurfaces(va_dpy,
                                 VA_RT_FORMAT_YUV420, frame_width_mbaligned, frame_height_mbaligned,
                                 &src_surface[0], SURFACE_NUM,
                                 NULL, 0);
    CHECK_VASTATUS(va_status, "vaCreateSurfaces");

    /* create reference surfaces */
    va_status = vaCreateSurfaces(
        va_dpy,
        VA_RT_FORMAT_YUV420, frame_width_mbaligned, frame_height_mbaligned,
        &ref_surface[0], SURFACE_NUM,
        NULL, 0
        );
    CHECK_VASTATUS(va_status, "vaCreateSurfaces");

    tmp_surfaceid = calloc(2 * SURFACE_NUM, sizeof(VASurfaceID));
    memcpy(tmp_surfaceid, src_surface, SURFACE_NUM * sizeof(VASurfaceID));
    memcpy(tmp_surfaceid + SURFACE_NUM, ref_surface, SURFACE_NUM * sizeof(VASurfaceID));
    
    /* Create a context for this encode pipe */
    va_status = vaCreateContext(va_dpy, config_id,
                                frame_width_mbaligned, frame_height_mbaligned,
                                VA_PROGRESSIVE,
                                tmp_surfaceid, 2 * SURFACE_NUM,
                                &context_id);
    CHECK_VASTATUS(va_status, "vaCreateContext");
    free(tmp_surfaceid);

    codedbuf_size = (frame_width_mbaligned * frame_height_mbaligned * 400) / (16*16);

    for (i = 0; i < SURFACE_NUM; i++) {
        /* create coded buffer once for all
         * other VA buffers which won't be used again after vaRenderPicture.
         * so APP can always vaCreateBuffer for every frame
         * but coded buffer need to be mapped and accessed after vaRenderPicture/vaEndPicture
         * so VA won't maintain the coded buffer
         */
        va_status = vaCreateBuffer(va_dpy,context_id,VAEncCodedBufferType,
                codedbuf_size, 1, NULL, &coded_buf[i]);
        CHECK_VASTATUS(va_status,"vaCreateBuffer");
    }
    
    return 0;
}

static int encode_frames(void)
{
    unsigned int i, tmp;
    VAStatus va_status;
    //VASurfaceStatus surface_status;

    /* upload RAW YUV data into all surfaces */
    tmp = GetTickCount();
    if (1) {
        for (i = 0; i < SURFACE_NUM; i++)
            load_surface(src_surface[i], i);
    }
    UploadPictureTicks += GetTickCount() - tmp;
    
    /* ready for encoding */
    memset(srcsurface_status, SRC_SURFACE_IN_ENCODING, sizeof(srcsurface_status));
    
    memset(&seq_param, 0, sizeof(seq_param));
    memset(&pic_param, 0, sizeof(pic_param));
    memset(&slice_param, 0, sizeof(slice_param));

    if (encode_syncmode == 0)
        pthread_create(&encode_thread, NULL, storage_task_thread, NULL);  
    
    for (current_frame_encoding = 0; current_frame_encoding < frame_count; current_frame_encoding++) {
        encoding2display_order(current_frame_encoding, intra_period, intra_idr_period, ip_period,
                               &current_frame_display, &current_frame_type); 
        if (current_frame_type == FRAME_IDR) {
            numShortTerm = 0;
            current_frame_num = 0;
            current_IDR_display = current_frame_display;
        }

        /* check if the source frame is ready */
        while (srcsurface_status[current_slot] != SRC_SURFACE_IN_ENCODING) {
            usleep(1);
        }
        
        tmp = GetTickCount();
        va_status = vaBeginPicture(va_dpy, context_id, src_surface[current_slot]);
        CHECK_VASTATUS(va_status,"vaBeginPicture");
        BeginPictureTicks += GetTickCount() - tmp;
        
        tmp = GetTickCount();
        if (current_frame_type == FRAME_IDR) {
            render_sequence();
            render_picture();            
            if (h264_packedheader) {
                render_packedsequence();
                render_packedpicture();
            }
        } else {
            render_picture();
        }
        render_slice();
        RenderPictureTicks += GetTickCount() - tmp;
        
        tmp = GetTickCount();
        va_status = vaEndPicture(va_dpy,context_id);
        CHECK_VASTATUS(va_status,"vaEndPicture");
        EndPictureTicks += GetTickCount() - tmp;

        if (encode_syncmode)
            storage_task(current_frame_display, current_frame_encoding); // 保存编码数据，重新加载源数据(yuv)
        else /* queue the storage task queue */
            storage_task_queue(current_frame_display, current_frame_encoding);
        
        update_ReferenceFrames();        
    }

    if (encode_syncmode == 0) {
        int ret;
        pthread_join(encode_thread, (void **)&ret);
    }
    
    return 0;
}

static int release_encode()
{
    int i;
    
    vaDestroySurfaces(va_dpy,&src_surface[0],SURFACE_NUM);
    vaDestroySurfaces(va_dpy,&ref_surface[0],SURFACE_NUM);

    for (i = 0; i < SURFACE_NUM; i++)
        vaDestroyBuffer(va_dpy,coded_buf[i]);
    
    vaDestroyContext(va_dpy,context_id);
    vaDestroyConfig(va_dpy,config_id);

    return 0;
}

static int deinit_va()
{ 
    vaTerminate(va_dpy);

    va_close_display(va_dpy);

    return 0;
}

void parameterInit()
{
	frame_width = screen_width;
	frame_height = screen_height;
	frame_count = 60;
	frame_bitrate = frame_width * frame_height * 12 * frame_rate / 50;
	frame_slices = 1;
	intra_period = 30;
	intra_idr_period = 60;
	ip_period = 1;
	initial_qp = 26;
	minimal_qp = 0;
	coded_fn = "output.264";
	frame_width_mbaligned = (frame_width + 15) & (~15);
    	frame_height_mbaligned = (frame_height + 15) & (~15);
	srcyuv_fourcc = VA_FOURCC_IYUV;

	/* store coded data into a file */
    	coded_fp = fopen(coded_fn,"w+");
    	if (coded_fp == NULL) {
        	printf("Open file %s failed, exit\n", coded_fn);
        	exit(1);
    	}
//    	h264_profile = VAProfileH264Baseline;

}

int main(void)
{
	unsigned int start;
	int ret = 0;
	ret = xDispalyInit();
    	if (ret < 0) {
        	printf("xDispalyInit exe failed !\n");
		return -1;
    	}

    	g_yuv_buffer = (unsigned char *)malloc(screen_width * screen_height * 4); 

    	ret = ffmpegInit();
    	if (ret < 0) {
		printf("ffmpegInit exe failed !\n");
		return -1;
    	} 

	parameterInit();
	start = GetTickCount();
	init_va();
	setup_encode();
	encode_frames();

	release_encode();
	deinit_va();

	deinitXDisplay();
    	sws_freeContext(img_convert_ctx);
    	av_freep(&src_data[0]);
    	av_freep(&dst_data[0]);
    	free(g_yuv_buffer);

	TotalTicks += GetTickCount() - start;

	return 0;	
}
