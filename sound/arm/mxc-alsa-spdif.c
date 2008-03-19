/*
 * Copyright 2007-2008 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @defgroup SOUND_DRV MXC Sound Driver for ALSA
 */

/*!
 * @file       mxc-alsa-spdif.c
 * @brief      this fle       mxc-alsa-spdif.c
 * @brief      this file implements mxc alsa driver for spdif.
 *             The spdif tx supports consumer channel for linear PCM and
 *	       compressed audio data. The supported sample rates are
 *	       48KHz, 44.1KHz and 32KHz.
 *
 * @ingroup SOUND_DRV
 */

#include <sound/driver.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/ioctl.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/soundcard.h>
#include <linux/clk.h>
#ifdef CONFIG_PM
#include <linux/pm.h>
#endif

#include <asm/arch/dma.h>
#include <asm/mach-types.h>
#include <asm/io.h>
#include <asm/irq.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/asoundef.h>
#include <sound/initval.h>
#include <sound/control.h>

#define MXC_SPDIF_NAME "MXC_SPDIF"

#define SPDIF_MAX_BUF_SIZE      (32*1024)
#define SPDIF_DMA_BUF_SIZE	(8*1024)
#define SPDIF_MIN_PERIOD_SIZE	64
#define SPDIF_MIN_PERIOD	2
#define SPDIF_MAX_PERIOD	255

#define SPDIF_REG_SCR   0x00
#define SPDIF_REG_SIE   0x0C
#define SPDIF_REG_SIS   0x10
#define SPDIF_REG_SIC   0x10
#define SPDIF_REG_STL   0x2C
#define SPDIF_REG_STR   0x30
#define SPDIF_REG_STCSCH        0x34
#define SPDIF_REG_STCSCL        0x38
#define SPDIF_REG_STC   0x50

/* SPDIF Configuration register */
#define SCR_TXFIFO_AUTOSYNC     (1 << 17)
#define SCR_TXFIFO_ZERO         (0 << 10)
#define SCR_TXFIFO_NORMAL       (1 << 10)
#define SCR_TXFIFO_ONESAMPLE    (2 << 10)
#define SCR_DMA_TX_ENABLE       (1 << 8)
#define SCR_DMA_TX_DISABLE      (0 << 8)
#define SCR_TXSEL_OFF           (0 << 2)
#define SCR_TXSEL_RX            (1 << 2)
#define SCR_TXSEL_NORMAL        (5 << 2)

#define INT_TXFIFO_UNOV         (1 << 19)
#define INT_TXFIFO_RESYNC       (1 << 18)
#define INT_TX_EMPTY            (1 << 1)

enum spdif_clk_accuracy {
	SPDIF_CLK_ACCURACY_LEV2 = 0,
	SPDIF_CLK_ACCURACY_LEV1 = 2,
	SPDIF_CLK_ACCURACY_LEV3 = 1,
	SPDIF_CLK_ACCURACY_RESV = 3
};

enum spdif_max_wdl {
	SPDIF_MAX_WDL_20,
	SPDIF_MAX_WDL_24
};

enum spdif_wdl {
	SPDIF_WDL_DEFAULT = 0,
	SPDIF_WDL_FIFTH = 4,
	SPDIF_WDL_FOURTH = 3,
	SPDIF_WDL_THIRD = 2,
	SPDIF_WDL_SECOND = 1,
	SPDIF_WDL_MAX = 5
};

static unsigned long spdif_base_addr;

/*!
 * @brief Enable/Disable spdif DMA request
 *
 * This function is called to enable or disable the dma transfer
 */
static int spdif_txdma_cfg(int txdma)
{
	unsigned long value;

	value = __raw_readl(SPDIF_REG_SCR + spdif_base_addr) & 0xfffeff;
	value |= txdma;
	__raw_writel(value, SPDIF_REG_SCR + spdif_base_addr);

	return 0;
}

/*!
 * @brief Enable spdif interrupt
 *
 * This function is called to enable relevant interrupts.
 */
static int spdif_intr_enable(int intr)
{
	unsigned long value;

	value = __raw_readl(SPDIF_REG_SIE + spdif_base_addr) & 0xffffff;
	value |= intr;
	__raw_writel(value, SPDIF_REG_SIE + spdif_base_addr);

	return 0;
}

/*!
 * @brief Disable spdif interrupt
 *
 * This function is called to diable relevant interrupts.
 */
static int spdif_intr_disable(int intr)
{
	unsigned long value;

	value = __raw_readl(SPDIF_REG_SIE + spdif_base_addr) & 0xffffff;
	value &= ~intr;
	__raw_writel(value, SPDIF_REG_SIE + spdif_base_addr);

	return 0;
}

/*!
 * @brief Set the clock accuracy level in the channel bit
 *
 * This function is called to set the clock accuracy level
 */
static int spdif_set_clk_accuracy(enum spdif_clk_accuracy level)
{
	unsigned long value;

	value = __raw_readl(SPDIF_REG_STCSCL + spdif_base_addr) & 0xffffcf;
	value |= (level << 4);
	__raw_writel(value, SPDIF_REG_STCSCL + spdif_base_addr);

	return 0;
}

/*!
 * @brief Set the audio sample rate in the channel status bit
 *
 * This function is called to set the audio sample rate to be transfered.
 */
static int spdif_set_sample_rate(int src_44100, int src_48000, int sample_rate)
{
	unsigned long value;

	value = __raw_readl(SPDIF_REG_STCSCL + spdif_base_addr) & 0xfffff0;

	switch (sample_rate) {
	case 44100:
		__raw_writel(value, SPDIF_REG_STCSCL + spdif_base_addr);
		value = (src_44100 << 8) | 0x07;
		__raw_writel(value, SPDIF_REG_STC + spdif_base_addr);
		break;
	case 48000:
		value |= 0x04;
		__raw_writel(value, SPDIF_REG_STCSCL + spdif_base_addr);
		value = (src_48000 << 8) | 0x07;
		__raw_writel(value, SPDIF_REG_STC + spdif_base_addr);
		break;
	case 32000:
		value |= 0x0c;
		__raw_writel(value, SPDIF_REG_STCSCL + spdif_base_addr);
		value = (src_48000 << 8) | 0x0b;
		__raw_writel(value, SPDIF_REG_STC + spdif_base_addr);
		break;
	}

	return 0;
}

/*!
 * @brief Set the lchannel status bit
 *
 * This function is called to set the channel status
 */
static int spdif_set_channel_status(int value, unsigned long reg)
{
	__raw_writel(value & 0xffffff, reg + spdif_base_addr);

	return 0;
}

/*!
 * @brief Get spdif interrupt status and clear the interrupt
 *
 * This function is called to check relevant interrupt status
 */
static int spdif_intr_status(void)
{
	unsigned long value;

	value = __raw_readl(SPDIF_REG_SIS + spdif_base_addr) & 0xffffff;

	__raw_writel(0, SPDIF_REG_SIC + spdif_base_addr);

	return value;
}

/*!
 * @brief spdif interrupt handler
 */
static irqreturn_t spdif_isr(int irq, void *dev_id)
{
	unsigned long status, value;

	status = spdif_intr_status();
	value = __raw_readl(SPDIF_REG_SIE + spdif_base_addr);

	if ((value & (1 << 19)) && (status & 0x80000))
		pr_info("spdif tx underrun\n");
	else if ((value & (1 << 18)) && (status & 0x40000))
		pr_info("spdif tx resync\n");
	else if ((value & (1 << 1)) && (status & 0x2))
		pr_info("spdif tx fifo empty\n");

	return IRQ_HANDLED;
}

/*!
 * @brief Initialize spdif module
 *
 * This function is called to set the spdif to initial state.
 */
static int spdif_tx_init(void)
{
	unsigned long value;

	value = 0x20414;
	__raw_writel(value, SPDIF_REG_SCR + spdif_base_addr);

	value = 0xc0000;
	/*Spdif has no watermark, so the underrun interrupt can't be enabled */
	/*__raw_writel(value, SPDIF_REG_SIE);*/

	/* Default clock source from EXTAL, divider by 8, generate 44.1kHz
	   sample rate */
	value = 0x07;
	__raw_writel(value, SPDIF_REG_STC + spdif_base_addr);

	/* spdif intrrupt bug */
	if (request_irq(MXC_INT_SPDIF, spdif_isr, 0, "spdif", NULL))
		return -1;

	return 0;

}

/*!
 * @brief deinitialize spdif module
 *
 * This function is called to stop the spdif
 */
static int spdif_tx_exit(void)
{
	int value;

	value = __raw_readl(SPDIF_REG_SCR + spdif_base_addr) & 0xffffe3;
	value |= SCR_TXSEL_OFF;
	__raw_writel(value, SPDIF_REG_SCR + spdif_base_addr);
	disable_irq(MXC_INT_SPDIF);
	free_irq(MXC_INT_SPDIF, NULL);

	return 0;
}

/*!
 * This structure represents an audio stream in term of
 * channel DMA, HW configuration on spdif controller.
 */
struct mxc_spdif_stream {

	/*!
	 * identification string
	 */
	char *id;
	/*!
	 * device identifier for DMA
	 */
	int dma_wchannel;
	/*!
	 * we are using this stream for transfer now
	 */
	int active:1;
	/*!
	 * current transfer period
	 */
	int period;
	/*!
	 * current count of transfered periods
	 */
	int periods;
	/*!
	 * are we recording - flag used to do DMA trans. for sync
	 */
	int tx_spin;
	/*!
	 * for locking in DMA operations
	 */
	spinlock_t dma_lock;
	/*!
	 * Alsa substream pointer
	 */
	struct snd_pcm_substream *stream;
};

struct spdif_mixer_control {

	/*!
	 * IEC958 channel status bit
	 */
	unsigned char ch_status[4];
};

struct mxc_spdif_device {
	/*!
	 * SPDIF module register base address
	 */
	unsigned long __iomem *reg_base;

	/*!
	 * spdif tx available or not
	 */
	int mxc_spdif_tx;

	/*!
	 * spdif rx available or not
	 */
	int mxc_spdif_rx;

	/*!
	 * spdif 44100 clock src
	 */
	int spdif_txclk_44100;

	/*!
	 * spdif 48000 clock src
	 */
	int spdif_txclk_48000;

	/*!
	 * ALSA SPDIF sound card handle
	 */
	struct snd_card *card;

	/*!
	 * ALSA spdif driver type handle
	 */
	struct snd_pcm *pcm;

	struct mxc_spdif_stream s[2];
};

static struct spdif_mixer_control mxc_spdif_control;

static unsigned int spdif_playback_rates[] = { 32000, 44100, 48000 };

/*!
  * this structure represents the sample rates supported
  * by SPDIF
  */
static struct snd_pcm_hw_constraint_list hw_playback_rates_stereo = {
	.count = ARRAY_SIZE(spdif_playback_rates),
	.list = spdif_playback_rates,
	.mask = 0,
};

/*!
  * This function configures the DMA channel used to transfer
  * audio from MCU to SPDIF or from SPDIF to MCU
  *
  * @param	s	pointer to the structure of the current stream.
  * @param      callback        pointer to function that will be
  *                              called when a SDMA TX transfer finishes.
  *
  * @return              0 on success, -1 otherwise.
  */
static int
spdif_configure_dma_channel(struct mxc_spdif_stream *s,
			    mxc_dma_callback_t callback)
{
	int ret = -1;
	int channel = -1;

	if (s->dma_wchannel != 0)
		mxc_dma_free(s->dma_wchannel);

	if (s->stream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		if (s->stream->runtime->sample_bits > 16) {
			channel =
			    mxc_dma_request(MXC_DMA_SPDIF_32BIT_TX,
					    "SPDIF TX DMA");
		} else {
			channel =
			    mxc_dma_request(MXC_DMA_SPDIF_16BIT_TX,
					    "SPDIF TX DMA");
		}

		pr_debug("spdif_configure_dma_channel: %d\n", channel);
	} else if (s->stream->stream == SNDRV_PCM_STREAM_CAPTURE) {

	}
	ret = mxc_dma_callback_set(channel,
				   (mxc_dma_callback_t) callback, (void *)s);
	if (ret != 0) {
		pr_info("spdif_configure_dma_channel - err\n");
		mxc_dma_free(channel);
		return -1;
	}
	s->dma_wchannel = channel;
	return 0;
}

/*!
  * This function gets the dma pointer position during playback.
  * Our DMA implementation does not allow to retrieve this position
  * when a transfert is active, so, it answers the middle of
  * the current period beeing transfered
  *
  * @param	s	pointer to the structure of the current stream.
  *
  */
static u_int spdif_get_playback_dma_pos(struct mxc_spdif_stream *s)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned int offset = 0;
	substream = s->stream;
	runtime = substream->runtime;

	/* tx_spin value is used here to check if a transfer is active */
	if (s->tx_spin) {
		offset =
		    (runtime->period_size * (s->periods)) +
		    (runtime->period_size >> 1);
		if (offset >= runtime->buffer_size)
			offset = runtime->period_size >> 1;
		pr_debug("MXC: audio_get_dma_pos offset  %d, buffer_size %d\n",
			 offset, (int)runtime->buffer_size);
	} else {
		offset = (runtime->period_size * (s->periods));
		if (offset >= runtime->buffer_size)
			offset = 0;
		pr_debug
		    ("MXC: spdif_get_dma_pos BIS offset  %d, buffer_size %d\n",
		     offset, (int)runtime->buffer_size);
	}
	return offset;
}

/*!
  * This function stops the current dma transfert for playback
  * and clears the dma pointers.
  *
  * @param	s	pointer to the structure of the current stream.
  *
  */
static void spdif_stop_tx(struct mxc_spdif_stream *s)
{
	unsigned long flags;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned int dma_size;
	unsigned int offset;
	substream = s->stream;
	runtime = substream->runtime;
	dma_size = frames_to_bytes(runtime, runtime->period_size);
	offset = dma_size * s->periods;
	spin_lock_irqsave(&s->dma_lock, flags);
	s->active = 0;
	s->period = 0;
	s->periods = 0;

	/* this stops the dma channel and clears the buffer ptrs */
	mxc_dma_disable(s->dma_wchannel);
	spdif_txdma_cfg(SCR_DMA_TX_DISABLE);
	dma_unmap_single(NULL, runtime->dma_addr + offset, dma_size,
			 DMA_TO_DEVICE);
	spin_unlock_irqrestore(&s->dma_lock, flags);
}

/*!
  * This function is called whenever a new audio block needs to be
  * transferred to SPDIF. The function receives the address and the size
  * of the new block and start a new DMA transfer.
  *
  * @param	s	pointer to the structure of the current stream.
  *
  */
static void spdif_start_tx(struct mxc_spdif_stream *s)
{
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned int dma_size = 0;
	unsigned int offset;
	int ret = 0;
	mxc_dma_requestbuf_t dma_request;
	substream = s->stream;
	runtime = substream->runtime;
	memset(&dma_request, 0, sizeof(mxc_dma_requestbuf_t));
	if (s->active) {
		dma_size = frames_to_bytes(runtime, runtime->period_size);
		offset = dma_size * s->period;
		dma_request.src_addr =
		    (dma_addr_t) (dma_map_single
				  (NULL, runtime->dma_area + offset, dma_size,
				   DMA_TO_DEVICE));

		dma_request.dst_addr = (dma_addr_t) (SPDIF_BASE_ADDR + 0x2c);

		dma_request.num_of_bytes = dma_size;
		mxc_dma_config(s->dma_wchannel, &dma_request, 1,
			       MXC_DMA_MODE_WRITE);
		ret = mxc_dma_enable(s->dma_wchannel);
		spdif_txdma_cfg(SCR_DMA_TX_ENABLE);
		s->tx_spin = 1;
		if (ret) {
			pr_info("audio_process_dma: cannot queue DMA \
				buffer\n");
			return;
		}
		s->period++;
		s->period %= runtime->periods;

		if ((s->period > s->periods)
		    && ((s->period - s->periods) > 1)) {
			pr_debug("audio playback chain dma: already double \
				buffered\n");
			return;
		}

		if ((s->period < s->periods)
		    && ((s->period + runtime->periods - s->periods) > 1)) {
			pr_debug("audio playback chain dma: already double  \
				buffered\n");
			return;
		}

		if (s->period == s->periods) {
			pr_debug("audio playback chain dma: s->period == \
				s->periods\n");
			return;
		}

		if (snd_pcm_playback_hw_avail(runtime) <
		    2 * runtime->period_size) {
			pr_debug("audio playback chain dma: available data \
				is not enough\n");
			return;
		}

		pr_debug
		    ("audio playback chain dma:to set up the 2nd dma buffer\n");

		offset = dma_size * s->period;
		dma_request.src_addr =
		    (dma_addr_t) (dma_map_single
				  (NULL, runtime->dma_area + offset, dma_size,
				   DMA_TO_DEVICE));
		mxc_dma_config(s->dma_wchannel, &dma_request, 1,
			       MXC_DMA_MODE_WRITE);
		ret = mxc_dma_enable(s->dma_wchannel);
		s->period++;
		s->period %= runtime->periods;

	}
	return;
}

/*!
  * This is a callback which will be called
  * when a TX transfer finishes. The call occurs
  * in interrupt context.
  *
  * @param	data	pointer to the structure of the current stream
  * @param	error	DMA error flag
  * @param	count	number of bytes transfered by the DMA
  */
static void spdif_tx_callback(void *data, int error, unsigned int count)
{
	struct mxc_spdif_stream *s;
	struct snd_pcm_substream *substream;
	struct snd_pcm_runtime *runtime;
	unsigned int dma_size;
	unsigned int previous_period;
	unsigned int offset;
	s = data;
	substream = s->stream;
	runtime = substream->runtime;
	previous_period = s->periods;
	dma_size = frames_to_bytes(runtime, runtime->period_size);
	offset = dma_size * previous_period;
	s->tx_spin = 0;
	s->periods++;
	s->periods %= runtime->periods;
	dma_unmap_single(NULL, runtime->dma_addr + offset, dma_size,
			 DMA_TO_DEVICE);
	if (s->active)
		snd_pcm_period_elapsed(s->stream);
	spin_lock(&s->dma_lock);
	spdif_start_tx(s);
	spin_unlock(&s->dma_lock);
}

/*!
  * This function is a dispatcher of command to be executed
  * by the driver for playback.
  *
  * @param	substream	pointer to the structure of the current stream.
  * @param	cmd		command to be executed
  *
  * @return              0 on success, -1 otherwise.
  */
static int
snd_mxc_spdif_playback_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct mxc_spdif_device *chip;
	struct mxc_spdif_stream *s;
	int err = 0;
	chip = snd_pcm_substream_chip(substream);
	s = &chip->s[SNDRV_PCM_STREAM_PLAYBACK];
	spin_lock(&s->dma_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
		s->tx_spin = 0;
		s->active = 1;
		spdif_start_tx(s);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		spdif_stop_tx(s);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		s->active = 0;
		s->periods = 0;
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		s->active = 1;
		s->tx_spin = 0;
		spdif_start_tx(s);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		s->active = 0;
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		s->active = 1;
		s->tx_spin = 0;
		spdif_start_tx(s);
		break;
	default:
		err = -EINVAL;
		break;
	}
	spin_unlock(&s->dma_lock);
	return err;
}

/*!
  * This function configures the hardware to allow audio
  * playback operations. It is called by ALSA framework.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  * @return              0 on success, -1 otherwise.
  */
static int snd_mxc_spdif_playback_prepare(struct snd_pcm_substream *substream)
{
	struct mxc_spdif_device *chip;
	struct snd_pcm_runtime *runtime;
	int err;
	unsigned int ch_status;
	chip = snd_pcm_substream_chip(substream);
	runtime = substream->runtime;
	spdif_tx_init();
	ch_status =
	    ((mxc_spdif_control.ch_status[2] << 16) | (mxc_spdif_control.
						       ch_status[1] << 8) |
	     mxc_spdif_control.ch_status[0]);
	spdif_set_channel_status(ch_status, SPDIF_REG_STCSCH);
	ch_status = mxc_spdif_control.ch_status[3];
	spdif_set_channel_status(ch_status, SPDIF_REG_STCSCL);
	spdif_intr_enable(INT_TXFIFO_RESYNC);
	spdif_set_sample_rate(chip->spdif_txclk_44100, chip->spdif_txclk_48000,
			      runtime->rate);
	spdif_set_clk_accuracy(SPDIF_CLK_ACCURACY_LEV2);
	/* setup DMA controller for spdif tx */
	err = spdif_configure_dma_channel(&chip->
					  s[SNDRV_PCM_STREAM_PLAYBACK],
					  spdif_tx_callback);
	if (err < 0) {
		pr_info("snd_mxc_spdif_playback_prepare - err < 0\n");
		return err;
	}
	return 0;
}

/*!
  * This function gets the current playback pointer position.
  * It is called by ALSA framework.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  */
static snd_pcm_uframes_t
snd_mxc_spdif_playback_pointer(struct snd_pcm_substream *substream)
{
	struct mxc_spdif_device *chip;
	chip = snd_pcm_substream_chip(substream);
	return spdif_get_playback_dma_pos(&chip->s[SNDRV_PCM_STREAM_PLAYBACK]);
}

static struct snd_pcm_hardware snd_spdif_playback_hw = {
	.info =
	    (SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
	     SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_HALF_DUPLEX |
	     SNDRV_PCM_INFO_MMAP_VALID | SNDRV_PCM_INFO_PAUSE |
	     SNDRV_PCM_INFO_RESUME),
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE,
	.rates =
	    SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000,
	.rate_min = 32000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = SPDIF_MAX_BUF_SIZE,
	.period_bytes_min = SPDIF_MIN_PERIOD_SIZE,
	.period_bytes_max = SPDIF_DMA_BUF_SIZE,
	.periods_min = SPDIF_MIN_PERIOD,
	.periods_max = SPDIF_MAX_PERIOD,
	.fifo_size = 0,
};

/*!
  * This function opens a spdif device in playback mode
  * It is called by ALSA framework.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  * @return              0 on success, -1 otherwise.
  */
static int snd_card_mxc_spdif_playback_open(struct snd_pcm_substream *substream)
{
	struct mxc_spdif_device *chip;
	struct snd_pcm_runtime *runtime;
	int err;
	struct mxc_spdif_platform_data *spdif_data;

	chip = snd_pcm_substream_chip(substream);
	spdif_data = chip->card->dev->platform_data;
	clk_enable(spdif_data->spdif_clk);
	runtime = substream->runtime;
	chip->s[SNDRV_PCM_STREAM_PLAYBACK].stream = substream;
	runtime->hw = snd_spdif_playback_hw;
	err = snd_pcm_hw_constraint_list(runtime, 0,
					 SNDRV_PCM_HW_PARAM_RATE,
					 &hw_playback_rates_stereo);
	if (err < 0)
		return err;
	err =
	    snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (err < 0)
		return err;
	return 0;
}

/*!
  * This function closes an spdif device for playback.
  * It is called by ALSA framework.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  * @return              0 on success, -1 otherwise.
  */
static int snd_card_mxc_spdif_playback_close(struct snd_pcm_substream
					     *substream)
{
	struct mxc_spdif_device *chip;
	struct mxc_spdif_platform_data *spdif_data;

	chip = snd_pcm_substream_chip(substream);
	spdif_data = chip->card->dev->platform_data;
	spdif_intr_disable(INT_TXFIFO_UNOV | INT_TXFIFO_RESYNC | INT_TX_EMPTY);
	spdif_tx_exit();
	clk_disable(spdif_data->spdif_clk);
	mxc_dma_free(chip->s[SNDRV_PCM_STREAM_PLAYBACK].dma_wchannel);
	chip->s[SNDRV_PCM_STREAM_PLAYBACK].dma_wchannel = 0;
	return 0;
}

/*!
  * This function configure the Audio HW in terms of memory allocation.
  * It is called by ALSA framework.
  *
  * @param	substream	pointer to the structure of the current stream.
  * @param	hw_params	Pointer to hardware paramters structure
  *
  * @return              0 on success, -1 otherwise.
  */
static int snd_mxc_spdif_hw_params(struct snd_pcm_substream
				   *substream, struct snd_pcm_hw_params
				   *hw_params)
{
	struct snd_pcm_runtime *runtime;
	int ret = 0;
	runtime = substream->runtime;
	ret =
	    snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
	if (ret < 0) {
		pr_info("snd_mxc_spdif_hw_params - ret: %d\n", ret);
		return ret;
	}
	runtime->dma_addr = virt_to_phys(runtime->dma_area);
	return ret;
}

/*!
  * This function frees the spdif hardware at the end of playback.
  *
  * @param	substream	pointer to the structure of the current stream.
  *
  * @return              0 on success, -1 otherwise.
  */
static int snd_mxc_spdif_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

/*!
  * This structure is the list of operation that the driver
  * must provide for the playback interface
  */
static struct snd_pcm_ops snd_card_mxc_spdif_playback_ops = {
	.open = snd_card_mxc_spdif_playback_open,
	.close = snd_card_mxc_spdif_playback_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = snd_mxc_spdif_hw_params,
	.hw_free = snd_mxc_spdif_hw_free,
	.prepare = snd_mxc_spdif_playback_prepare,
	.trigger = snd_mxc_spdif_playback_trigger,
	.pointer = snd_mxc_spdif_playback_pointer,
};

static struct snd_pcm_ops snd_card_mxc_spdif_capture_ops = {
	.open = NULL,
	.close = NULL,
	.ioctl = NULL,
	.hw_params = NULL,
	.hw_free = NULL,
	.prepare = NULL,
	.trigger = NULL,
	.pointer = NULL,
};

/*!
  * This functions initializes the playback audio device supported by
  * spdif
  *
  * @param	mxc_spdif	pointer to the sound card structure.
  *
  */
void mxc_init_spdif_device(struct mxc_spdif_device *mxc_spdif)
{
	if (mxc_spdif->mxc_spdif_tx) {
		mxc_spdif->s[SNDRV_PCM_STREAM_PLAYBACK].id = "spdif tx";
		mxc_spdif_control.ch_status[0] =
		    IEC958_AES0_CON_NOT_COPYRIGHT |
		    IEC958_AES0_CON_EMPHASIS_5015;
		mxc_spdif_control.ch_status[1] = IEC958_AES1_CON_DIGDIGCONV_ID;
		mxc_spdif_control.ch_status[2] = 0x00;
		mxc_spdif_control.ch_status[3] =
		    IEC958_AES3_CON_FS_44100 | IEC958_AES3_CON_CLOCK_1000PPM;
	}
	if (mxc_spdif->mxc_spdif_rx) {

		/*Add code here if capture is available */
	}

}

static int mxc_pb_spdif_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int mxc_pb_spdif_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *uvalue)
{
	uvalue->value.iec958.status[0] = mxc_spdif_control.ch_status[0];
	uvalue->value.iec958.status[1] = mxc_spdif_control.ch_status[1];
	uvalue->value.iec958.status[2] = mxc_spdif_control.ch_status[2];
	uvalue->value.iec958.status[3] = mxc_spdif_control.ch_status[3];
	return 0;
}

static int mxc_pb_spdif_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *uvalue)
{
	unsigned int ch_status;
	mxc_spdif_control.ch_status[0] = uvalue->value.iec958.status[0];
	mxc_spdif_control.ch_status[1] = uvalue->value.iec958.status[1];
	mxc_spdif_control.ch_status[2] = uvalue->value.iec958.status[2];
	mxc_spdif_control.ch_status[3] = uvalue->value.iec958.status[3];
	ch_status =
	    ((mxc_spdif_control.ch_status[2] << 16) | (mxc_spdif_control.
						       ch_status[1] << 8) |
	     mxc_spdif_control.ch_status[0]);
	spdif_set_channel_status(ch_status, SPDIF_REG_STCSCH);
	ch_status = mxc_spdif_control.ch_status[3];
	spdif_set_channel_status(ch_status, SPDIF_REG_STCSCL);
	return 0;
}

/*!
 * This structure defines the spdif control interface
 */
struct snd_kcontrol_new snd_mxc_spdif_playback __devinitdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.info = mxc_pb_spdif_info,
	.get = mxc_pb_spdif_get,
	.put = mxc_pb_spdif_put,
};

/*!
  * This function the soundcard structure.
  *
  * @param	mxc_spdif	pointer to the sound card structure.
  *
  * @return              0 on success, -1 otherwise.
  */
static int snd_card_mxc_spdif_pcm(struct mxc_spdif_device *mxc_spdif)
{
	struct snd_pcm *pcm;
	int err;
	err = snd_pcm_new(mxc_spdif->card, MXC_SPDIF_NAME, 0,
			  mxc_spdif->mxc_spdif_tx,
			  mxc_spdif->mxc_spdif_rx, &pcm);
	if (err < 0)
		return err;

	snd_pcm_lib_preallocate_pages_for_all(pcm,
					      SNDRV_DMA_TYPE_CONTINUOUS,
					      snd_dma_continuous_data
					      (GFP_KERNEL),
					      SPDIF_MAX_BUF_SIZE * 2,
					      SPDIF_MAX_BUF_SIZE * 2);
	if (mxc_spdif->mxc_spdif_tx)
		snd_pcm_set_ops(pcm,
				SNDRV_PCM_STREAM_PLAYBACK,
				&snd_card_mxc_spdif_playback_ops);
	if (mxc_spdif->mxc_spdif_rx)
		snd_pcm_set_ops(pcm,
				SNDRV_PCM_STREAM_CAPTURE,
				&snd_card_mxc_spdif_capture_ops);
	pcm->private_data = mxc_spdif;
	pcm->info_flags = 0;
	strncpy(pcm->name, MXC_SPDIF_NAME, sizeof(pcm->name));
	mxc_spdif->pcm = pcm;
	mxc_init_spdif_device(mxc_spdif);
	return 0;
}

/*!
  * This function initializes the driver in terms of memory of the soundcard
  * and some basic HW clock settings.
  *
  * @param	pdev	Pointer to the platform device
  * @return              0 on success, -1 otherwise.
  */
static int mxc_alsa_spdif_probe(struct platform_device
				*pdev)
{
	int err;
	struct snd_card *card;
	struct mxc_spdif_device *chip;
	struct resource *res;
	struct mxc_spdif_platform_data *spdif_platform_data;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOENT;

	/* register the soundcard */
	card = snd_card_new(SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1, THIS_MODULE,
			    sizeof(struct mxc_spdif_device));
	if (card == NULL)
		return -ENOMEM;
	chip = card->private_data;
	chip->card = card;
	card->dev = &pdev->dev;
	chip->reg_base = ioremap(res->start, res->end - res->start + 1);
	spdif_base_addr = (unsigned long)chip->reg_base;
	spdif_platform_data =
	    (struct mxc_spdif_platform_data *)pdev->dev.platform_data;
	chip->mxc_spdif_tx = spdif_platform_data->spdif_tx;
	chip->mxc_spdif_rx = spdif_platform_data->spdif_rx;
	chip->spdif_txclk_44100 = spdif_platform_data->spdif_clk_44100;
	chip->spdif_txclk_48000 = spdif_platform_data->spdif_clk_48000;
	err = snd_card_mxc_spdif_pcm(chip);
	if (err < 0)
		goto nodev;
	err = snd_ctl_add(card,
			  snd_ctl_new1(&snd_mxc_spdif_playback,
				       &mxc_spdif_control));
	if (err < 0)
		return err;

	if (chip->mxc_spdif_tx)
		spin_lock_init(&chip->s[SNDRV_PCM_STREAM_PLAYBACK].dma_lock);
	if (chip->mxc_spdif_rx)
		spin_lock_init(&chip->s[SNDRV_PCM_STREAM_CAPTURE].dma_lock);
	strcpy(card->driver, MXC_SPDIF_NAME);
	strcpy(card->shortname, "MXC SPDIF TX/RX");
	sprintf(card->longname, "MXC Freescale with SPDIF");
	err = snd_card_register(card);
	if (err == 0) {
		pr_debug(KERN_INFO "MXC spdif support initialized\n");
		platform_set_drvdata(pdev, card);
		return 0;
	}

      nodev:
	snd_card_free(card);
	return err;
}

/*!
  * This function releases the sound card and unmap the io address
  *
  * @param      pdev    Pointer to the platform device
  * @return              0 on success, -1 otherwise.
  */

static int mxc_alsa_spdif_remove(struct platform_device *pdev)
{
	struct mxc_spdif_device *chip;
	struct snd_card *card;
	card = platform_get_drvdata(pdev);
	chip = card->private_data;
	iounmap(chip->reg_base);
	snd_card_free(card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

#ifdef CONFIG_PM
/*!
  * This function suspends all active streams.
  *
  * TBD
  *
  * @param	card	pointer to the sound card structure.
  * @param	state	requested state
  *
  * @return              0 on success, -1 otherwise.
  */
static int mxc_alsa_spdif_suspend(struct platform_device *pdev,
				  pm_message_t state)
{
	return 0;
}

/*!
  * This function resumes all suspended streams.
  *
  * TBD
  *
  * @param	card	pointer to the sound card structure.
  * @param	state	requested state
  *
  * @return              0 on success, -1 otherwise.
  */
static int mxc_alsa_spdif_resume(struct platform_device *pdev)
{
	return 0;
}
#endif

static struct platform_driver mxc_alsa_spdif_driver = {
	.probe = mxc_alsa_spdif_probe,
	.remove = mxc_alsa_spdif_remove,
#ifdef CONFIG_PM
	.suspend = mxc_alsa_spdif_suspend,
	.resume = mxc_alsa_spdif_resume,
#endif
	.driver = {
		   .name = "mxc_alsa_spdif",
		   },
};

/*!
  * This function registers the sound driver structure.
  *
  */
static int __init mxc_alsa_spdif_init(void)
{
	return platform_driver_register(&mxc_alsa_spdif_driver);
}

/*!
  * This function frees the sound driver structure.
  *
  */
static void __exit mxc_alsa_spdif_exit(void)
{
	platform_driver_unregister(&mxc_alsa_spdif_driver);
}

module_init(mxc_alsa_spdif_init);
module_exit(mxc_alsa_spdif_exit);
MODULE_AUTHOR("FREESCALE SEMICONDUCTOR");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("MXC ALSA driver for SPDIF");
