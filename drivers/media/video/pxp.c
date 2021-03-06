/*
 * Freescale STMP378X PxP driver
 *
 * Author: Matt Porter <mporter@embeddedalley.com>
 *
 * Copyright 2008-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008-2009 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/dma-mapping.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/vmalloc.h>

#include <media/videobuf-dma-contig.h>
#include <media/v4l2-common.h>
#include <media/v4l2-dev.h>
#include <media/v4l2-ioctl.h>

#include <mach/stmp3xxx_regs.h>
#include <mach/regs-pxp.h>

#include "pxp.h"

#define PXP_DRIVER_NAME			"stmp3xxx-pxp"
#define PXP_DRIVER_MAJOR		1
#define PXP_DRIVER_MINOR		0

#define PXP_DEF_BUFS			2
#define PXP_MIN_PIX			8

#define V4L2_OUTPUT_TYPE_INTERNAL	4

static struct pxp_data_format pxp_s0_formats[] = {
	{
		.name = "24-bit RGB",
		.bpp = 4,
		.fourcc = V4L2_PIX_FMT_RGB24,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ctrl_s0_fmt = BV_PXP_CTRL_S0_FORMAT__RGB888,
	}, {
		.name = "16-bit RGB 5:6:5",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_RGB565,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ctrl_s0_fmt = BV_PXP_CTRL_S0_FORMAT__RGB565,
	}, {
		.name = "16-bit RGB 5:5:5",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_RGB555,
		.colorspace = V4L2_COLORSPACE_SRGB,
		.ctrl_s0_fmt = BV_PXP_CTRL_S0_FORMAT__RGB555,
	}, {
		.name = "YUV 4:2:0 Planar",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_YUV420,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.ctrl_s0_fmt = BV_PXP_CTRL_S0_FORMAT__YUV420,
	}, {
		.name = "YUV 4:2:2 Planar",
		.bpp = 2,
		.fourcc = V4L2_PIX_FMT_YUV422P,
		.colorspace = V4L2_COLORSPACE_JPEG,
		.ctrl_s0_fmt = BV_PXP_CTRL_S0_FORMAT__YUV422,
	},
};

struct v4l2_queryctrl pxp_controls[] = {
	{
		.id 		= V4L2_CID_HFLIP,
		.type 		= V4L2_CTRL_TYPE_BOOLEAN,
		.name 		= "Horizontal Flip",
		.minimum 	= 0,
		.maximum 	= 1,
		.step 		= 1,
		.default_value	= 0,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_VFLIP,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
		.name		= "Vertical Flip",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE,
		.type		= V4L2_CTRL_TYPE_INTEGER,
		.name		= "Rotation",
		.minimum	= 0,
		.maximum	= 270,
		.step		= 90,
		.default_value	= 0,
		.flags		= 0,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE + 1,
		.name		= "Background Color",
		.minimum	= 0,
		.maximum	= 0xFFFFFF,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
		.type		= V4L2_CTRL_TYPE_INTEGER,
	}, {
		.id		= V4L2_CID_PRIVATE_BASE + 2,
		.name		= "YUV Colorspace",
		.minimum	= 0,
		.maximum	= 1,
		.step		= 1,
		.default_value	= 0,
		.flags		= 0,
		.type		= V4L2_CTRL_TYPE_BOOLEAN,
	},
};

static void pxp_set_ctrl(struct pxps *pxp)
{
	u32 ctrl;

	ctrl = BF_PXP_CTRL_S0_FORMAT(pxp->s0_fmt->ctrl_s0_fmt);
	ctrl |=
	BF_PXP_CTRL_OUTPUT_RGB_FORMAT(BV_PXP_CTRL_OUTPUT_RGB_FORMAT__RGB888);
	ctrl |= BM_PXP_CTRL_CROP;

	if (pxp->scaling)
		ctrl |= BM_PXP_CTRL_SCALE;
	if (pxp->vflip)
		ctrl |= BM_PXP_CTRL_VFLIP;
	if (pxp->hflip)
		ctrl |= BM_PXP_CTRL_HFLIP;
	if (pxp->rotate)
		ctrl |= BF_PXP_CTRL_ROTATE(pxp->rotate/90);

	ctrl |= BM_PXP_CTRL_IRQ_ENABLE;
	if (pxp->active)
		ctrl |= BM_PXP_CTRL_ENABLE;

	HW_PXP_CTRL_WR(ctrl);
}

static void pxp_set_rgbbuf(struct pxps *pxp)
{
	HW_PXP_RGBBUF_WR(pxp->outb_phys);
	/* Always equal to the FB size */
	HW_PXP_RGBSIZE_WR(BF_PXP_RGBSIZE_WIDTH(pxp->fb.fmt.width) |
			  BF_PXP_RGBSIZE_HEIGHT(pxp->fb.fmt.height));
}

static void pxp_set_colorkey(struct pxps *pxp)
{
	/* Low and high are set equal. V4L does not allow a chromakey range */
	HW_PXP_S0COLORKEYLOW_WR(pxp->chromakey);
	HW_PXP_S0COLORKEYHIGH_WR(pxp->chromakey);
}

static void pxp_set_oln(struct pxps *pxp)
{
	HW_PXP_OLn_WR(0, (u32)pxp->fb.base);
	HW_PXP_OLnSIZE_WR(0, BF_PXP_OLnSIZE_WIDTH(pxp->fb.fmt.width >> 3) |
				BF_PXP_OLnSIZE_HEIGHT(pxp->fb.fmt.height >> 3));
}

static void pxp_set_olparam(struct pxps *pxp)
{
	u32 olparam;
	struct v4l2_pix_format *fmt = &pxp->fb.fmt;

	olparam = BF_PXP_OLnPARAM_ALPHA(pxp->global_alpha);
	if (fmt->pixelformat == V4L2_PIX_FMT_RGB24)
		olparam |=
		BF_PXP_OLnPARAM_FORMAT(BV_PXP_OLnPARAM_FORMAT__RGB888);
	else
		olparam |=
		BF_PXP_OLnPARAM_FORMAT(BV_PXP_OLnPARAM_FORMAT__RGB565);
	if (pxp->global_alpha_state)
		olparam |= BF_PXP_OLnPARAM_ALPHA_CNTL(
				BV_PXP_OLnPARAM_ALPHA_CNTL__Override);
	if (pxp->chromakey_state)
		olparam |= BM_PXP_OLnPARAM_ENABLE_COLORKEY;
	if (pxp->overlay_state)
		olparam |= BM_PXP_OLnPARAM_ENABLE;
	HW_PXP_OLnPARAM_WR(0, olparam);
}

static void pxp_set_s0param(struct pxps *pxp)
{
	u32 s0param;

	s0param = BF_PXP_S0PARAM_XBASE(pxp->drect.left >> 3);
	s0param |= BF_PXP_S0PARAM_YBASE(pxp->drect.top >> 3);
	s0param |= BF_PXP_S0PARAM_WIDTH(pxp->srect.width >> 3);
	s0param |= BF_PXP_S0PARAM_HEIGHT(pxp->srect.height >> 3);
	HW_PXP_S0PARAM_WR(s0param);
}

static void pxp_set_s0crop(struct pxps *pxp)
{
	u32 s0crop;

	s0crop = BF_PXP_S0CROP_XBASE(pxp->srect.left >> 3);
	s0crop |= BF_PXP_S0CROP_YBASE(pxp->srect.top >> 3);
	s0crop |= BF_PXP_S0CROP_WIDTH(pxp->drect.width >> 3);
	s0crop |= BF_PXP_S0CROP_HEIGHT(pxp->drect.height >> 3);
	HW_PXP_S0CROP_WR(s0crop);
}

static int pxp_set_scaling(struct pxps *pxp)
{
	int ret = 0;
	u32 xscale, yscale, s0scale;

	if ((pxp->s0_fmt->fourcc != V4L2_PIX_FMT_YUV420) &&
		(pxp->s0_fmt->fourcc != V4L2_PIX_FMT_YUV422P)) {
		pxp->scaling = 0;
		ret = -EINVAL;
		goto out;
	}

	if ((pxp->srect.width == pxp->drect.width) &&
		(pxp->srect.height == pxp->drect.height)) {
		pxp->scaling = 0;
		goto out;
	}

	pxp->scaling = 1;
	xscale = pxp->srect.width * 0x1000 / pxp->drect.width;
	yscale = pxp->srect.height * 0x1000 / pxp->drect.height;
	s0scale = BF_PXP_S0SCALE_YSCALE(yscale) |
		  BF_PXP_S0SCALE_XSCALE(xscale);
	HW_PXP_S0SCALE_WR(s0scale);

out:
	pxp_set_ctrl(pxp);

	return ret;
}

static int pxp_set_fbinfo(struct pxps *pxp)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct v4l2_framebuffer *fb = &pxp->fb;
	int err;

	err = stmp3xxxfb_get_info(&var, &fix);

	fb->fmt.width = var.xres;
	fb->fmt.height = var.yres;
	if (var.bits_per_pixel == 16)
		fb->fmt.pixelformat = V4L2_PIX_FMT_RGB565;
	else
		fb->fmt.pixelformat = V4L2_PIX_FMT_RGB24;
	fb->base = (void *)fix.smem_start;
	return err;
}

static void pxp_set_s0bg(struct pxps *pxp)
{
	HW_PXP_S0BACKGROUND_WR(pxp->s0_bgcolor);
}

static void pxp_set_csc(struct pxps *pxp)
{
	if (pxp->yuv) {
		/* YUV colorspace */
		HW_PXP_CSCCOEFF0_WR(0x04030000);
		HW_PXP_CSCCOEFF1_WR(0x01230208);
		HW_PXP_CSCCOEFF2_WR(0x076b079c);
	} else {
		/* YCrCb colorspace */
		HW_PXP_CSCCOEFF0_WR(0x84ab01f0);
		HW_PXP_CSCCOEFF1_WR(0x01230204);
		HW_PXP_CSCCOEFF2_WR(0x0730079c);
	}
}

static int pxp_set_cstate(struct pxps *pxp, struct v4l2_control *vc)
{

	if (vc->id == V4L2_CID_HFLIP)
		pxp->hflip = vc->value;
	else if (vc->id == V4L2_CID_VFLIP)
		pxp->vflip = vc->value;
	else if (vc->id == V4L2_CID_PRIVATE_BASE) {
		if (vc->value % 90)
			return -ERANGE;
		pxp->rotate = vc->value;
	} else if (vc->id == V4L2_CID_PRIVATE_BASE + 1) {
		pxp->s0_bgcolor = vc->value;
		pxp_set_s0bg(pxp);
	} else if (vc->id == V4L2_CID_PRIVATE_BASE + 2) {
		pxp->yuv = vc->value;
		pxp_set_csc(pxp);
	}

	pxp_set_ctrl(pxp);

	return 0;
}

static int pxp_get_cstate(struct pxps *pxp, struct v4l2_control *vc)
{
	if (vc->id == V4L2_CID_HFLIP)
		vc->value = pxp->hflip;
	else if (vc->id == V4L2_CID_VFLIP)
		vc->value = pxp->vflip;
	else if (vc->id == V4L2_CID_PRIVATE_BASE)
		vc->value = pxp->rotate;
	else if (vc->id == V4L2_CID_PRIVATE_BASE + 1)
		vc->value = pxp->s0_bgcolor;
	else if (vc->id == V4L2_CID_PRIVATE_BASE + 2)
		vc->value = pxp->yuv;

	return 0;
}

static int pxp_enumoutput(struct file *file, void *fh,
			struct v4l2_output *o)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	if ((o->index < 0) || (o->index > 1))
		return -EINVAL;

	memset(o, 0, sizeof(struct v4l2_output));
	if (o->index == 0) {
		strcpy(o->name, "PxP Display Output");
		pxp->output = 0;
	} else {
		strcpy(o->name, "PxP Virtual Output");
		pxp->output = 1;
	}
	o->type = V4L2_OUTPUT_TYPE_INTERNAL;
	o->std = 0;
	o->reserved[0] = pxp->outb_phys;

	return 0;
}

static int pxp_g_output(struct file *file, void *fh,
			unsigned int *i)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	*i = pxp->output;

	return 0;
}

static int pxp_s_output(struct file *file, void *fh,
			unsigned int i)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_pix_format *fmt = &pxp->fb.fmt;
	int bpp;

	if ((i < 0) || (i > 1))
		return -EINVAL;

	if (pxp->outb)
		goto out;

	/* Output buffer is same format as fbdev */
	if (fmt->pixelformat == V4L2_PIX_FMT_RGB24)
		bpp = 4;
	else
		bpp = 2;

	pxp->outb = kmalloc(fmt->width * fmt->height * bpp, GFP_KERNEL);
	pxp->outb_phys = virt_to_phys(pxp->outb);
	dma_map_single(NULL, pxp->outb,
			fmt->width * fmt->height * bpp, DMA_TO_DEVICE);

out:
	pxp_set_rgbbuf(pxp);

	return 0;
}

static int pxp_enum_fmt_video_output(struct file *file, void *fh,
				struct v4l2_fmtdesc *fmt)
{
	enum v4l2_buf_type type = fmt->type;
	int index = fmt->index;

	if ((fmt->index < 0) || (fmt->index >= ARRAY_SIZE(pxp_s0_formats)))
		return -EINVAL;

	memset(fmt, 0, sizeof(struct v4l2_fmtdesc));
	fmt->index = index;
	fmt->type = type;
	fmt->pixelformat = pxp_s0_formats[index].fourcc;
	strcpy(fmt->description, pxp_s0_formats[index].name);

	return 0;
}

static int pxp_g_fmt_video_output(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct v4l2_pix_format *pf = &f->fmt.pix;
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct pxp_data_format *fmt = pxp->s0_fmt;

	pf->width = pxp->srect.width;
	pf->height = pxp->srect.height;
	pf->pixelformat = fmt->fourcc;
	pf->field = V4L2_FIELD_NONE;
	pf->bytesperline = fmt->bpp * pf->width;
	pf->sizeimage = pf->bytesperline * pf->height;
	pf->colorspace = fmt->colorspace;
	pf->priv = 0;

	return 0;
}

static struct pxp_data_format *pxp_get_format(struct v4l2_format *f)
{
	struct pxp_data_format *fmt;
	int i;

	for (i = 0; i < ARRAY_SIZE(pxp_s0_formats); i++) {
		fmt = &pxp_s0_formats[i];
		if (fmt->fourcc == f->fmt.pix.pixelformat)
			break;
	}

	if (i == ARRAY_SIZE(pxp_s0_formats))
		return NULL;

	return &pxp_s0_formats[i];
}

static int pxp_try_fmt_video_output(struct file *file, void *fh,
				struct v4l2_format *f)
{
	int w = f->fmt.pix.width;
	int h = f->fmt.pix.height;
	struct pxp_data_format *fmt = pxp_get_format(f);

	if (!fmt)
		return -EINVAL;

	w = min(w, 2040);
	w = max(w, 8);
	h = min(h, 2040);
	h = max(h, 8);
	f->fmt.pix.field = V4L2_FIELD_NONE;
	f->fmt.pix.width = w;
	f->fmt.pix.height = h;
	f->fmt.pix.pixelformat = fmt->fourcc;

	return 0;
}

static int pxp_s_fmt_video_output(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_pix_format *pf = &f->fmt.pix;
	int ret = pxp_try_fmt_video_output(file, fh, f);

	if (ret == 0) {
		pxp->s0_fmt = pxp_get_format(f);
		pxp->srect.width = pf->width;
		pxp->srect.height = pf->height;
		pxp_set_ctrl(pxp);
		pxp_set_s0param(pxp);
	}

	return ret;
}

static int pxp_g_fmt_output_overlay(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_window *wf = &f->fmt.win;

	memset(wf, 0, sizeof(struct v4l2_window));
	wf->chromakey = pxp->chromakey;
	wf->global_alpha = pxp->global_alpha;
	wf->field = V4L2_FIELD_NONE;
	wf->clips = NULL;
	wf->clipcount = 0;
	wf->bitmap = NULL;
	wf->w.left = pxp->srect.left;
	wf->w.top = pxp->srect.top;
	wf->w.width = pxp->srect.width;
	wf->w.height = pxp->srect.height;

	return 0;
}

static int pxp_try_fmt_output_overlay(struct file *file, void *fh,
				struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_window *wf = &f->fmt.win;
	u32 chromakey = wf->chromakey;
	u8 global_alpha = wf->global_alpha;

	pxp_g_fmt_output_overlay(file, fh, f);

	wf->chromakey = chromakey;
	wf->global_alpha = global_alpha;

	/* Constrain parameters to the input buffer */
	wf->w.left = pxp->srect.left;
	wf->w.top = pxp->srect.top;
	wf->w.width = pxp->srect.width;
	wf->w.height = pxp->srect.height;

	return 0;
}

static int pxp_s_fmt_output_overlay(struct file *file, void *fh,
					struct v4l2_format *f)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	struct v4l2_window *wf = &f->fmt.win;
	int ret = pxp_try_fmt_output_overlay(file, fh, f);

	if (ret == 0) {
		pxp->srect.left = wf->w.left;
		pxp->srect.top = wf->w.top;
		pxp->srect.width = wf->w.width;
		pxp->srect.height = wf->w.height;
		pxp->global_alpha = wf->global_alpha;
		pxp->chromakey = wf->chromakey;
		pxp_set_s0param(pxp);
		pxp_set_s0crop(pxp);
		pxp_set_scaling(pxp);
		pxp_set_olparam(pxp);
		pxp_set_colorkey(pxp);
	}

	return ret;
}

static int pxp_reqbufs(struct file *file, void *priv,
			struct v4l2_requestbuffers *r)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_reqbufs(&pxp->s0_vbq, r);
}

static int pxp_querybuf(struct file *file, void *priv,
			struct v4l2_buffer *b)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_querybuf(&pxp->s0_vbq, b);
}

static int pxp_qbuf(struct file *file, void *priv,
			struct v4l2_buffer *b)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_qbuf(&pxp->s0_vbq, b);
}

static int pxp_dqbuf(struct file *file, void *priv,
			struct v4l2_buffer *b)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	return videobuf_dqbuf(&pxp->s0_vbq, b, file->f_flags & O_NONBLOCK);
}

static int pxp_streamon(struct file *file, void *priv,
			enum v4l2_buf_type t)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret = 0;

	if ((t != V4L2_BUF_TYPE_VIDEO_OUTPUT))
		return -EINVAL;

	ret = videobuf_streamon(&pxp->s0_vbq);

	if (!ret && (pxp->output == 0))
		stmp3xxxfb_cfg_pxp(1, pxp->outb_phys);

	return ret;
}

static int pxp_streamoff(struct file *file, void *priv,
			enum v4l2_buf_type t)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret = 0;

	if ((t != V4L2_BUF_TYPE_VIDEO_OUTPUT))
		return -EINVAL;

	ret = videobuf_streamoff(&pxp->s0_vbq);

	if (!ret)
		stmp3xxxfb_cfg_pxp(0, 0);

	return ret;
}

static int pxp_buf_setup(struct videobuf_queue *q,
			unsigned int *count, unsigned *size)
{
	struct pxps *pxp = q->priv_data;

	*size = pxp->srect.width * pxp->srect.height * pxp->s0_fmt->bpp;

	if (0 == *count)
		*count = PXP_DEF_BUFS;

	return 0;
}

static void pxp_buf_free(struct videobuf_queue *q, struct videobuf_buffer *vb)
{
	if (in_interrupt())
		BUG();

	videobuf_dma_contig_free(q, vb);

	vb->state = VIDEOBUF_NEEDS_INIT;
}

static int pxp_buf_prepare(struct videobuf_queue *q,
			struct videobuf_buffer *vb,
			enum v4l2_field field)
{
	struct pxps *pxp = q->priv_data;
	int ret = 0;

	vb->width = pxp->srect.width;
	vb->height = pxp->srect.height;
	vb->size = vb->width * vb->height * pxp->s0_fmt->bpp;
	vb->field = V4L2_FIELD_NONE;
	vb->state = VIDEOBUF_NEEDS_INIT;


	ret = videobuf_iolock(q, vb, NULL);
	if (ret)
		goto fail;
	vb->state = VIDEOBUF_PREPARED;

	return 0;

fail:
	pxp_buf_free(q, vb);
	return ret;
}

static void pxp_buf_output(struct pxps *pxp)
{
	dma_addr_t Y, U, V;

	if (pxp->active) {
		pxp->active->state = VIDEOBUF_ACTIVE;
		Y = videobuf_to_dma_contig(pxp->active);
		HW_PXP_S0BUF_WR(Y);
		if ((pxp->s0_fmt->fourcc == V4L2_PIX_FMT_YUV420) ||
		    (pxp->s0_fmt->fourcc == V4L2_PIX_FMT_YUV422P)) {
			int s = 1;	/* default to YUV 4:2:2 */
			if (pxp->s0_fmt->fourcc == V4L2_PIX_FMT_YUV420)
				s = 2;
			U = Y + (pxp->srect.width * pxp->srect.height);
			V = U + ((pxp->srect.width * pxp->srect.height) >> s);
			HW_PXP_S0UBUF_WR(U);
			HW_PXP_S0VBUF_WR(V);
		}
		HW_PXP_CTRL_SET(BM_PXP_CTRL_ENABLE);
	}
}

static void pxp_buf_queue(struct videobuf_queue *q,
			struct videobuf_buffer *vb)
{
	struct pxps *pxp = q->priv_data;
	unsigned long flags;

	spin_lock_irqsave(&pxp->lock, flags);

	list_add_tail(&vb->queue, &pxp->outq);
	vb->state = VIDEOBUF_QUEUED;

	if (!pxp->active) {
		pxp->active = vb;
		pxp_buf_output(pxp);
	}

	spin_unlock_irqrestore(&pxp->lock, flags);
}

static void pxp_buf_release(struct videobuf_queue *q,
			struct videobuf_buffer *vb)
{
	pxp_buf_free(q, vb);
}

static struct videobuf_queue_ops pxp_vbq_ops = {
	.buf_setup	= pxp_buf_setup,
	.buf_prepare	= pxp_buf_prepare,
	.buf_queue	= pxp_buf_queue,
	.buf_release	= pxp_buf_release,
};

static int pxp_querycap(struct file *file, void *fh,
			struct v4l2_capability *cap)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	memset(cap, 0, sizeof(*cap));
	strcpy(cap->driver, "pxp");
	strcpy(cap->card, "pxp");
	strlcpy(cap->bus_info, pxp->pdev->dev.bus_id, sizeof(cap->bus_info));

	cap->version = (PXP_DRIVER_MAJOR << 8) + PXP_DRIVER_MINOR;

	cap->capabilities = V4L2_CAP_VIDEO_OUTPUT |
				V4L2_CAP_VIDEO_OUTPUT_OVERLAY |
				V4L2_CAP_STREAMING;

	return 0;
}

static int pxp_g_fbuf(struct file *file, void *priv,
			struct v4l2_framebuffer *fb)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	memset(fb, 0, sizeof(*fb));

	fb->capability = V4L2_FBUF_CAP_EXTERNOVERLAY |
			 V4L2_FBUF_CAP_CHROMAKEY |
			 V4L2_FBUF_CAP_LOCAL_ALPHA |
			 V4L2_FBUF_CAP_GLOBAL_ALPHA;

	if (pxp->global_alpha_state)
		fb->flags |= V4L2_FBUF_FLAG_GLOBAL_ALPHA;
	if (pxp->local_alpha_state)
		fb->flags |= V4L2_FBUF_FLAG_LOCAL_ALPHA;
	if (pxp->chromakey_state)
		fb->flags |= V4L2_FBUF_FLAG_CHROMAKEY;

	return 0;
}

static int pxp_s_fbuf(struct file *file, void *priv,
			struct v4l2_framebuffer *fb)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	pxp->overlay_state =
		(fb->flags & V4L2_FBUF_FLAG_OVERLAY) != 0;
	pxp->global_alpha_state =
		(fb->flags & V4L2_FBUF_FLAG_GLOBAL_ALPHA) != 0;
	pxp->local_alpha_state =
		(fb->flags & V4L2_FBUF_FLAG_LOCAL_ALPHA) != 0;
	/* Global alpha overrides local alpha if both are requested */
	if (pxp->global_alpha_state && pxp->local_alpha_state)
		pxp->local_alpha_state = 0;
	pxp->chromakey_state =
		(fb->flags & V4L2_FBUF_FLAG_CHROMAKEY) != 0;

	pxp_set_olparam(pxp);
	pxp_set_s0crop(pxp);
	pxp_set_scaling(pxp);

	return 0;
}

static int pxp_g_crop(struct file *file, void *fh,
			struct v4l2_crop *c)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	if (c->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY)
		return -EINVAL;

	c->c.left = pxp->drect.left;
	c->c.top = pxp->drect.top;
	c->c.width = pxp->drect.width;
	c->c.height = pxp->drect.height;

	return 0;
}

static int pxp_s_crop(struct file *file, void *fh,
			struct v4l2_crop *c)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int l = c->c.left;
	int t = c->c.top;
	int w = c->c.width;
	int h = c->c.height;
	int fbw = pxp->fb.fmt.width;
	int fbh = pxp->fb.fmt.height;

	if (c->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_OVERLAY)
		return -EINVAL;

	/* Constrain parameters to FB limits */
	w = min(w, fbw);
	w = max(w, PXP_MIN_PIX);
	h = min(h, fbh);
	h = max(h, PXP_MIN_PIX);
	if ((l + w) > fbw)
		l = 0;
	if ((t + h) > fbh)
		t = 0;

	/* Round up values to PxP pixel block */
	l = roundup(l, PXP_MIN_PIX);
	t = roundup(t, PXP_MIN_PIX);
	w = roundup(w, PXP_MIN_PIX);
	h = roundup(h, PXP_MIN_PIX);

	pxp->drect.left = l;
	pxp->drect.top = t;
	pxp->drect.width = w;
	pxp->drect.height = h;

	pxp_set_s0param(pxp);
	pxp_set_s0crop(pxp);
	pxp_set_scaling(pxp);

	return 0;
}

static int pxp_queryctrl(struct file *file, void *priv,
			 struct v4l2_queryctrl *qc)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pxp_controls); i++)
		if (qc->id && qc->id == pxp_controls[i].id) {
			memcpy(qc, &(pxp_controls[i]), sizeof(*qc));
			return 0;
		}

	return -EINVAL;
}

static int pxp_g_ctrl(struct file *file, void *priv,
			 struct v4l2_control *vc)
{
	int i;

	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	for (i = 0; i < ARRAY_SIZE(pxp_controls); i++)
		if (vc->id == pxp_controls[i].id)
			return pxp_get_cstate(pxp, vc);

	return -EINVAL;
}

static int pxp_s_ctrl(struct file *file, void *priv,
			 struct v4l2_control *vc)
{
	int i;
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	for (i = 0; i < ARRAY_SIZE(pxp_controls); i++)
		if (vc->id == pxp_controls[i].id) {
			if (vc->value < pxp_controls[i].minimum ||
			    vc->value > pxp_controls[i].maximum)
				return -ERANGE;
			return pxp_set_cstate(pxp, vc);
		}

	return -EINVAL;
}

void pxp_release(struct video_device *vfd)
{
	struct pxps *pxp = video_get_drvdata(vfd);

	spin_lock(&pxp->lock);
	video_device_release(vfd);
	spin_unlock(&pxp->lock);
}

static int pxp_hw_init(struct pxps *pxp)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	int err;

	err = stmp3xxxfb_get_info(&var, &fix);
	if (err)
		return err;

	/* Pull PxP out of reset */
	HW_PXP_CTRL_WR(0);

	/* Config defaults */
	pxp->active = NULL;

	pxp->s0_fmt = &pxp_s0_formats[0];
	pxp->drect.left = pxp->srect.left = 0;
	pxp->drect.top = pxp->srect.top = 0;
	pxp->drect.width = pxp->srect.width = var.xres;
	pxp->drect.height = pxp->srect.height = var.yres;
	pxp->s0_bgcolor = 0;

	pxp->output = 0;
	err = pxp_set_fbinfo(pxp);
	if (err)
		return err;

	pxp->scaling = 0;
	pxp->hflip = 0;
	pxp->vflip = 0;
	pxp->rotate = 0;
	pxp->yuv = 0;

	pxp->overlay_state = 0;
	pxp->global_alpha_state = 0;
	pxp->global_alpha = 0;
	pxp->local_alpha_state = 0;
	pxp->chromakey_state = 0;
	pxp->chromakey = 0;

	/* Write default h/w config */
	pxp_set_ctrl(pxp);
	pxp_set_s0param(pxp);
	pxp_set_s0crop(pxp);
	pxp_set_oln(pxp);
	pxp_set_olparam(pxp);
	pxp_set_colorkey(pxp);
	pxp_set_csc(pxp);

	return 0;
}

static int pxp_open(struct inode *inode, struct file *file)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret = 0;

	mutex_lock(&pxp->mutex);
	pxp->users++;

	if (pxp->users > 1) {
		pxp->users--;
		ret = -EBUSY;
		goto out;
	}

out:
	mutex_unlock(&pxp->mutex);
	if (ret)
		return ret;

	videobuf_queue_dma_contig_init(&pxp->s0_vbq,
				&pxp_vbq_ops,
				&pxp->pdev->dev,
				&pxp->lock,
				V4L2_BUF_TYPE_VIDEO_OUTPUT,
				V4L2_FIELD_NONE,
				sizeof(struct videobuf_buffer),
				pxp);

	return 0;
}

static int pxp_close(struct inode *inode, struct file *file)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));

	videobuf_stop(&pxp->s0_vbq);
	videobuf_mmap_free(&pxp->s0_vbq);

	mutex_lock(&pxp->mutex);
	pxp->users--;
	mutex_unlock(&pxp->mutex);

	return 0;
}

static int pxp_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pxps *pxp = video_get_drvdata(video_devdata(file));
	int ret;

	ret = videobuf_mmap_mapper(&pxp->s0_vbq, vma);

	return ret;
}

static const struct file_operations pxp_fops = {
	.owner		= THIS_MODULE,
	.open		= pxp_open,
	.release	= pxp_close,
	.ioctl		= video_ioctl2,
	.mmap		= pxp_mmap,
};

static const struct v4l2_ioctl_ops pxp_ioctl_ops = {
	.vidioc_querycap		= pxp_querycap,

	.vidioc_reqbufs			= pxp_reqbufs,
	.vidioc_querybuf		= pxp_querybuf,
	.vidioc_qbuf			= pxp_qbuf,
	.vidioc_dqbuf			= pxp_dqbuf,

	.vidioc_streamon		= pxp_streamon,
	.vidioc_streamoff		= pxp_streamoff,

	.vidioc_enum_output		= pxp_enumoutput,
	.vidioc_g_output		= pxp_g_output,
	.vidioc_s_output		= pxp_s_output,

	.vidioc_enum_fmt_vid_out	= pxp_enum_fmt_video_output,
	.vidioc_try_fmt_vid_out		= pxp_try_fmt_video_output,
	.vidioc_g_fmt_vid_out		= pxp_g_fmt_video_output,
	.vidioc_s_fmt_vid_out		= pxp_s_fmt_video_output,

	.vidioc_try_fmt_vid_out_overlay	= pxp_try_fmt_output_overlay,
	.vidioc_g_fmt_vid_out_overlay	= pxp_g_fmt_output_overlay,
	.vidioc_s_fmt_vid_out_overlay	= pxp_s_fmt_output_overlay,

	.vidioc_g_fbuf			= pxp_g_fbuf,
	.vidioc_s_fbuf			= pxp_s_fbuf,

	.vidioc_g_crop			= pxp_g_crop,
	.vidioc_s_crop			= pxp_s_crop,

	.vidioc_queryctrl		= pxp_queryctrl,
	.vidioc_g_ctrl			= pxp_g_ctrl,
	.vidioc_s_ctrl			= pxp_s_ctrl,
};

static const struct video_device pxp_template = {
	.name				= "PxP",
	.vfl_type			= VID_TYPE_OVERLAY |
					  VID_TYPE_CLIPPING |
					  VID_TYPE_SCALES,
	.fops				= &pxp_fops,
	.release			= pxp_release,
	.minor				= -1,
	.ioctl_ops			= &pxp_ioctl_ops,
};

static irqreturn_t pxp_irq(int irq, void *dev_id)
{
	struct pxps *pxp = (struct pxps *)dev_id;
	struct videobuf_buffer *vb;
	unsigned long flags;

	spin_lock_irqsave(&pxp->lock, flags);

	HW_PXP_STAT_CLR(BM_PXP_STAT_IRQ);

	vb = pxp->active;
	vb->state = VIDEOBUF_DONE;
	do_gettimeofday(&vb->ts);
	vb->field_count++;

	list_del_init(&vb->queue);

	if (list_empty(&pxp->outq)) {
		pxp->active = NULL;
		goto out;
	}

	pxp->active = list_entry(pxp->outq.next,
				struct videobuf_buffer,
				queue);

	pxp_buf_output(pxp);

out:
	wake_up(&vb->done);

	spin_unlock_irqrestore(&pxp->lock, flags);

	return IRQ_HANDLED;
}

static int pxp_probe(struct platform_device *pdev)
{
	struct pxps *pxp;
	struct resource *res;
	int irq;
	int err = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_get_irq(pdev, 0);
	if (!res || irq < 0) {
		err = -ENODEV;
		goto exit;
	}

	pxp = kzalloc(sizeof(*pxp), GFP_KERNEL);
	if (!pxp) {
		dev_err(&pdev->dev, "failed to allocate control object\n");
		err = -ENOMEM;
		goto exit;
	}

	dev_set_drvdata(&pdev->dev, pxp);
	pxp->res = res;
	pxp->irq = irq;

	INIT_LIST_HEAD(&pxp->outq);
	spin_lock_init(&pxp->lock);
	mutex_init(&pxp->mutex);

	if (!request_mem_region(res->start, res->end - res->start + 1,
				PXP_DRIVER_NAME)) {
		err = -EBUSY;
		goto freepxp;
	}

	pxp->regs = (void __iomem *)res->start; /* it is already ioremapped */
	pxp->pdev = pdev;

	err = request_irq(pxp->irq, pxp_irq, 0, PXP_DRIVER_NAME, pxp);

	if (err) {
		dev_err(&pdev->dev, "interrupt register failed\n");
		goto release;
	}

	pxp->vdev = video_device_alloc();
	if (!pxp->vdev) {
		dev_err(&pdev->dev, "video_device_alloc() failed\n");
		err = -ENOMEM;
		goto freeirq;
	}

	memcpy(pxp->vdev, &pxp_template, sizeof(pxp_template));
	video_set_drvdata(pxp->vdev, pxp);

	err = video_register_device(pxp->vdev, VFL_TYPE_GRABBER, 0);
	if (err) {
		dev_err(&pdev->dev, "failed to register video device\n");
		goto freevdev;
	}

	err = pxp_hw_init(pxp);
	if (err) {
		dev_err(&pdev->dev, "failed to initialize hardware\n");
		goto freevdev;
	}

	dev_info(&pdev->dev, "initialized\n");

exit:
	return err;

freevdev:
	video_device_release(pxp->vdev);

freeirq:
	free_irq(pxp->irq, pxp);

release:
	release_mem_region(res->start, res->end - res->start + 1);

freepxp:
	kfree(pxp);

	return err;
}

static int __devexit pxp_remove(struct platform_device *pdev)
{
	struct pxps *pxp = platform_get_drvdata(pdev);

	video_unregister_device(pxp->vdev);
	video_device_release(pxp->vdev);

	kfree(pxp->outb);
	kfree(pxp);

	return 0;
}

static struct platform_driver pxp_driver = {
	.driver 	= {
		.name	= PXP_DRIVER_NAME,
	},
	.probe		= pxp_probe,
	.remove		= __exit_p(pxp_remove),
};


static int __devinit pxp_init(void)
{
	return platform_driver_register(&pxp_driver);
}

static void __exit pxp_exit(void)
{
	platform_driver_unregister(&pxp_driver);
}

module_init(pxp_init);
module_exit(pxp_exit);

MODULE_DESCRIPTION("STMP37xx PxP driver");
MODULE_AUTHOR("Matt Porter <mporter@embeddedalley.com>");
MODULE_LICENSE("GPL");
