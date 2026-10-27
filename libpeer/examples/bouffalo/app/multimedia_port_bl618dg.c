/*
 * Copyright (C) 2017-2022 Bouffalolab Group Holding Limited
 * Change Logs:
 *   Date        Author       Notes
 *   2019-03-25  MSP          the first version
 */

#include <stdio.h>

#include <board.h>
#include <msp/kernel.h>
#include <xutils/debug.h>
#include <msp_port.h>
#ifdef CONFIG_MEDIA_GRAPH
#include "mg_audio_io.h"
#endif
#include <bflb_gpio.h>
#if defined(BL616CL)
#include <bl616cl_gpio.h>
#include <bl616cl_glb.h>
#elif defined(BL618DG)
#include <bl618dg_glb.h>
#elif defined(BL616)
#include <bl616_gpio.h>
#include <bl616_glb.h>
#endif
uint32_t g_pa_delay_1 = 5;
uint32_t g_pa_delay_2 = 90;     /* this is time set according to the pa manual */

/***********user config************/

#if !defined(CONFIG_CODEC_USE_I2S)
/* codec pin config */
/* if use single mode. pin set as 255 */
#define INPUT_NEGATIVE_PIN      (28)
#define INPUT_POSITIVE_PIN      (27)

/* if use single mode. pin set as 255 */
#define OUTPUT_NEGATIVE_PIN     (14)
#define OUTPUT_POSITIVE_PIN     (15)
#endif

/* dma channel config */
#define DMA_TX_CHANNEL_ID       (0)
#define DMA_RX_CHANNEL_ID       (1)

/* Pa control pin */
#ifndef CONFIG_AUDIO_PA_PIN
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
#define CONFIG_AUDIO_PA_PIN     (19)
#else
#define CONFIG_AUDIO_PA_PIN     (30)    /* user config pa */
#endif
#endif

/*********** end user config************/

#if defined(CONFIG_CODEC_USE_I2S)
#include <bflb_gpio.h>
#include <bflb_core.h>
#include <bflb_i2c.h>
#include <bflb_i2s.h>
#include <audio_codec.h>
#include <audio_codec_es8388.h>
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
#include <audio_codec_jy6316.h>
#include <audio_codec_internal.h>
#include <xcodec.h>
#define I2S_FS_PIN    (GPIO_PIN_29)
#define I2S_DI_PIN    (GPIO_PIN_30)
#define I2S_DO_PIN    (GPIO_PIN_31)
#define I2S_BCLK_PIN  (GPIO_PIN_32)
#define I2S_MCLK_PIN  (GPIO_PIN_17)
#define I2C_SCL_PIN   (GPIO_PIN_20)
#define I2C_SDA_PIN   (GPIO_PIN_21)
#else

/* i2s gpio def */
#define I2S_FS_PIN    (GPIO_PIN_1)
#define I2S_DI_PIN    (GPIO_PIN_19)
#define I2S_DO_PIN    (GPIO_PIN_18)
#define I2S_BCLK_PIN  (GPIO_PIN_20)
/* mclk def */
#define I2S_MCLK_PIN  (GPIO_PIN_2)
/* i2c gpio def */
#define I2C_SCL_PIN  (GPIO_PIN_0)
#define I2C_SDA_PIN  (GPIO_PIN_3)
#endif

static struct bflb_device_s *i2s0;
static audio_codec_dev_t g_ext_codec_dev = { 0 };
static int g_codec_mute = 0;
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
static int g_codec_volume_percent = 60;
static msp_i2s_device_cfg_t g_jy_i2s_cfg;
static bool g_jy_i2s_ready;
static volatile bool g_jy_tx_armed;
static volatile bool g_jy_rx_armed;
static volatile bool g_jy_duplex_mode;
static volatile bool g_jy_duplex_started;
#else
static int g_codec_volume_percent = 30;
#endif

static void i2s_gpio_init(void)
{
    struct bflb_device_s *gpio;

    gpio = bflb_device_get_by_name("gpio");

    /* I2S_FS */
    bflb_gpio_init(gpio, I2S_FS_PIN, GPIO_FUNC_I2S | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
    /* I2S_DI */
    bflb_gpio_init(gpio, I2S_DI_PIN, GPIO_FUNC_I2S | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
    /* I2S_DO */
    bflb_gpio_init(gpio, I2S_DO_PIN, GPIO_FUNC_I2S | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
    /* I2S_BCLK */
    bflb_gpio_init(gpio, I2S_BCLK_PIN, GPIO_FUNC_I2S | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);
}

static struct bflb_device_s *es8388_i2c_gpio_init(void)
{
#if !defined(CONFIG_CODEC_USE_JY6316) || !CONFIG_CODEC_USE_JY6316
    struct bflb_device_s *gpio;
#endif
    struct bflb_device_s *i2c0;

#if !defined(CONFIG_CODEC_USE_JY6316) || !CONFIG_CODEC_USE_JY6316
    gpio = bflb_device_get_by_name("gpio");
#endif
    i2c0 = bflb_device_get_by_name("i2c0");

#if !defined(CONFIG_CODEC_USE_JY6316) || !CONFIG_CODEC_USE_JY6316
    /* I2C0_SCL */
    bflb_gpio_init(gpio, I2C_SCL_PIN, GPIO_FUNC_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_2);
    /* I2C0_SDA */
    bflb_gpio_init(gpio, I2C_SDA_PIN, GPIO_FUNC_I2C0 | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_2);
#endif

    bflb_i2c_init(i2c0, 200000);

    return i2c0;
}

#if !defined(CONFIG_CODEC_USE_JY6316) || !CONFIG_CODEC_USE_JY6316
static void mclk_out_gpio_init(void)
{
    struct bflb_device_s *gpio;

    gpio = bflb_device_get_by_name("gpio");

    /* MCLK CLKOUT */
    bflb_gpio_init(gpio, I2S_MCLK_PIN, GPIO_FUNC_CLKOUT | GPIO_ALTERNATE | GPIO_PULLUP | GPIO_SMT_EN | GPIO_DRV_1);

    /*!< output MCLK,
        Will change the clock source of i2s,
        It needs to be called before i2s is initialized
        clock source 24.576M */
    switch (I2S_MCLK_PIN % 4) {
        case 0:
            GLB_Set_Chip_Clock_Out0_Sel(GLB_CHIP_CLK_OUT_0_I2S_REF_CLK);
            break;
        case 1:
            GLB_Set_Chip_Clock_Out1_Sel(GLB_CHIP_CLK_OUT_1_I2S_REF_CLK);
            break;
        case 2:
            GLB_Set_Chip_Clock_Out2_Sel(GLB_CHIP_CLK_OUT_2_I2S_REF_CLK);
            break;
        case 3:
            GLB_Set_Chip_Clock_Out3_Sel(GLB_CHIP_CLK_OUT_3_I2S_REF_CLK);
            break;
        default:
            break;
    }
}
#endif

static int msp_i2s_set_mclk(uint32_t sample_rate)
{
    uint32_t mclk_hz;
    uint32_t ref_clk_div;
#if !defined(CONFIG_CODEC_USE_JY6316) || !CONFIG_CODEC_USE_JY6316
    uint32_t mclk_source_hz;
    uint32_t ref_clk_factor;
#endif

    board_audio_pll_config_for_rate(sample_rate);

    mclk_hz = audio_codec_calc_mclk_hz(&g_ext_codec_dev, sample_rate);
    if ((sample_rate == 0U) || (mclk_hz == 0U)) {
        return -1;
    }

#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    /* JY6316 derives converter clocks from BCLK; keep the board reference undivided. */
    ref_clk_div = 0U;
#else
    mclk_source_hz = ((sample_rate == 11025U) ||
                      (sample_rate == 22050U) ||
                      (sample_rate == 44100U)) ? 22579200U : 24576000U;

    if ((mclk_source_hz % mclk_hz) != 0U) {
        printf("unsupported i2s mclk: rate=%u mclk=%u src=%u\r\n",
               sample_rate, mclk_hz, mclk_source_hz);
        return -1;
    }

    ref_clk_factor = mclk_source_hz / mclk_hz;
    if ((ref_clk_factor == 0U) || (ref_clk_factor > 64U)) {
        return -1;
    }

    ref_clk_div = ref_clk_factor - 1U;
#endif
    if (GLB_Set_I2S_CLK(ENABLE, (uint8_t)ref_clk_div,
                        GLB_I2S_DI_SEL_I2S_DI_INPUT,
                        GLB_I2S_DO_SEL_I2S_DO_OUTPT) != SUCCESS) {
        return -1;
    }

    printf("i2s mclk configured: rate=%u mclk=%u div=%u\r\n",
           sample_rate, mclk_hz, ref_clk_div);

    return 0;
}

static int msp_i2s_slot_width_to_lhal(uint32_t slot_width)
{
    switch (slot_width) {
        case 8:
            return I2S_SLOT_WIDTH_8;
        case 16:
            return I2S_SLOT_WIDTH_16;
        case 24:
            return I2S_SLOT_WIDTH_24;
        case 32:
            return I2S_SLOT_WIDTH_32;
        default:
            return -1;
    }
}

static int msp_codec_bits_from_width(uint32_t bit_width)
{
    switch (bit_width) {
        case 16:
            return AUDIO_CODEC_BITS_16;
        case 24:
            return AUDIO_CODEC_BITS_24;
        case 32:
            return AUDIO_CODEC_BITS_32;
        default:
            return -1;
    }
}

static int msp_i2s_validate_config(const msp_i2s_device_cfg_t *config)
{
    if (config == NULL || config->sample_rate == 0U) {
        return -1;
    }
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (config->channels != 4U || config->bit_width != 16U || config->slot_width != 16U) {
        return -1;
    }
#else
    if ((config->channels != 1U) && (config->channels != 2U)) {
        return -1;
    }
    if ((config->bit_width != 16U) && (config->bit_width != 24U) && (config->bit_width != 32U)) {
        return -1;
    }
    if ((config->slot_width != 16U) && (config->slot_width != 32U)) {
        return -1;
    }
    if (config->bit_width > config->slot_width) {
        return -1;
    }
    if ((config->bit_width == 24U) && (config->slot_width != 32U)) {
        return -1;
    }
#endif

    return 0;
}

static int msp_codec_reconfigure(const msp_i2s_device_cfg_t *config)
{
    int ret;
    audio_codec_cfg_t codec_cfg = {
        .work_mode = AUDIO_CODEC_WORK_MODE_CODEC,
        .codec_master = false,
        .mic_input_mode = AUDIO_CODEC_MIC_INPUT_DIFF,
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .i2s_fmt = AUDIO_CODEC_I2S_FMT_DSP,
#else
        .i2s_fmt = AUDIO_CODEC_I2S_FMT_LEFT_J,
#endif
        .bits = (audio_codec_bits_t)0,
        .sample_rate_hz = 0,
        .channels = 0,
        .enable_adc = true,
        .enable_dac = true,
    };

    if (config == NULL || g_ext_codec_dev.i2c == NULL || g_ext_codec_dev.drv == NULL) {
        return -1;
    }

    codec_cfg.bits = (audio_codec_bits_t)msp_codec_bits_from_width(config->bit_width);
    if (codec_cfg.bits == 0U) {
        return -1;
    }

    codec_cfg.sample_rate_hz = config->sample_rate;
    codec_cfg.channels = (uint8_t)config->channels;

    ret = audio_codec_init(&g_ext_codec_dev, &codec_cfg);
    if (ret != 0) {
        return ret;
    }

    audio_codec_set_mute(&g_ext_codec_dev, g_codec_mute != 0);
    audio_codec_set_volume(&g_ext_codec_dev, g_codec_volume_percent);

    return 0;
}

void msp_i2s_port_init(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    const xcodec_pcm_layout_t layout = {
        .channels = 4,
        .bit_width = 16,
        .slot_width = 16,
        .fifo_sample_bytes = 4,
        .tx_slots = {0, 1, 0, 1},
        .rx_stereo_slots = {0, 2},
        .recoverable_xrun = 1,
    };
    xcodec_set_pcm_layout(&layout);
#endif
    msp_i2s_device_cfg_t default_config = {
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .sample_rate = 8000,
        .channels = 4,
#else
        .sample_rate = 16000,
        .channels = 2,
#endif
        .bit_width = 16,
        .slot_width = 16,
    };

    printf("codec port: configuring MCLK and I2S pins\r\n");
    /* Match the working i2s_codec board pin and MCLK setup. */
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    board_i2s_codec_gpio_init();
#else
    mclk_out_gpio_init();
    if (msp_i2s_set_mclk(default_config.sample_rate) != 0) {
        printf("mclk init failed: rate=%u\r\n", default_config.sample_rate);
        return;
    }
#endif

    /* Get I2C device */
    {
        int init_ret;
        struct bflb_device_s *i2c0 = es8388_i2c_gpio_init();
        if (i2c0 == NULL) {
            printf("codec port: i2c0 not found\r\n");
            return;
        }

        /* Init the selected external codec through the shared audio_codec API. */

        g_ext_codec_dev.i2c = i2c0;
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        if (audio_codec_jy6316_driver.probe(i2c0, JY6316_JY6311_I2C_ADDR) != 0) {
            uint8_t id = 0;
            int ret = audio_codec_i2c_read_reg8(i2c0, JY6316_JY6311_I2C_ADDR, 0xff, &id);
            printf("JY6311 probe: addr=0x%02x ret=%d id=0x%02x\r\n",
                   JY6316_JY6311_I2C_ADDR, ret, id);
            id = 0;
            ret = audio_codec_i2c_read_reg8(i2c0, JY6316_JY6166_I2C_ADDR, 0xff, &id);
            printf("JY6166 probe: addr=0x%02x ret=%d id=0x%02x\r\n",
                   JY6316_JY6166_I2C_ADDR, ret, id);
            printf("JY6316 probe failed (JY6311/JY6166)\r\n");
            return;
        }
        g_ext_codec_dev.i2c_addr = JY6316_JY6311_I2C_ADDR;
        g_ext_codec_dev.drv = &audio_codec_jy6316_driver;
        g_ext_codec_dev.type = AUDIO_CODEC_TYPE_JY6316;
#else
        g_ext_codec_dev.i2c_addr = 0x10;
        g_ext_codec_dev.drv = &audio_codec_es8388_driver;
        g_ext_codec_dev.type = AUDIO_CODEC_TYPE_ES8388;
#endif

        init_ret = msp_i2s_device_config(&default_config);
        printf("%s ready: init=%d mute=%d volume=%d\n\r",
               g_ext_codec_dev.drv->name, init_ret, g_codec_mute, g_codec_volume_percent);
        if (init_ret != 0) {
            return;
        }
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        /* About 30 dB, matching the working JY6166 i2s_dma example. */
        if (audio_codec_set_mic_pga(&g_ext_codec_dev, 70) != 0) {
            printf("JY6316 microphone PGA setup failed\r\n");
            return;
        }
#endif
    }
}

int msp_i2s_device_config(const msp_i2s_device_cfg_t *config)
{
    int frame_width;
    int data_width;
    uint64_t bclk_freq_hz;
    uint32_t frame_slots;
    struct bflb_i2s_config_s i2s_cfg = {
        .bclk_freq_hz = 0,
        .role = I2S_ROLE_MASTER,
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .format_mode = I2S_MODE_DSP_SHORT_FRAME_SYNC,
        .channel_mode = I2S_CHANNEL_MODE_NUM_4,
#else
        .format_mode = I2S_MODE_LEFT_JUSTIFIED,
        .channel_mode = I2S_CHANNEL_MODE_NUM_2,
#endif
        .frame_width = I2S_SLOT_WIDTH_16,
        .data_width = I2S_SLOT_WIDTH_16,
        .fs_offset_cycle =
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
            1,
#else
            0,
#endif
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .tx_fifo_threshold = 0,
        .rx_fifo_threshold = 0,
#else
        .tx_fifo_threshold = 7,
        .rx_fifo_threshold = 7,
#endif
    };

    if (msp_i2s_validate_config(config) != 0) {
        printf("invalid i2s config sample_rate=%u channels=%u bit_width=%u slot_width=%u\r\n",
               config ? config->sample_rate : 0U,
               config ? config->channels : 0U,
               config ? config->bit_width : 0U,
               config ? config->slot_width : 0U);
        return -1;
    }
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (g_jy_i2s_ready) {
        if (g_jy_i2s_cfg.sample_rate == config->sample_rate &&
            g_jy_i2s_cfg.channels == config->channels &&
            g_jy_i2s_cfg.bit_width == config->bit_width &&
            g_jy_i2s_cfg.slot_width == config->slot_width) {
            return 0;
        }
        if (g_jy_tx_armed || g_jy_rx_armed || g_jy_duplex_mode) {
            printf("i2s reconfigure rejected while audio is active\r\n");
            return -1;
        }
    }
#endif

    frame_width = msp_i2s_slot_width_to_lhal(config->slot_width);
    data_width = msp_i2s_slot_width_to_lhal(config->bit_width);
    if ((frame_width < 0) || (data_width < 0)) {
        return -1;
    }

    /* Keep a 2-slot wire frame for a mono ES8388 stream. JY6316 uses 4 slots. */
    frame_slots = (config->channels == 1U) ? 2U : config->channels;
    bclk_freq_hz = (uint64_t)config->sample_rate * config->slot_width * frame_slots;
    if (bclk_freq_hz > 0xffffffffULL) {
        return -1;
    }

    i2s_cfg.bclk_freq_hz = (uint32_t)bclk_freq_hz;
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    i2s_cfg.channel_mode = I2S_CHANNEL_MODE_NUM_4;
#else
    i2s_cfg.channel_mode = (config->channels == 1U) ? I2S_CHANNEL_MODE_NUM_1 : I2S_CHANNEL_MODE_NUM_2;
#endif
    i2s_cfg.frame_width = (uint8_t)frame_width;
    i2s_cfg.data_width = (uint8_t)data_width;

    if (msp_i2s_set_mclk(config->sample_rate) != 0) {
        return -1;
    }

    i2s_gpio_init();
    i2s0 = bflb_device_get_by_name("i2s0");
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    /* Reconfiguration must not leave an earlier capture running. */
    bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL,
                             I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX | I2S_ARG_CMD_RX);
#endif
    bflb_i2s_init(i2s0, &i2s_cfg);
    /* Match i2s_codec's TX/RX DMA request configuration. */
#if defined(CONFIG_CODEC_USE_I2S)
    bflb_i2s_link_txdma(i2s0, true);
    bflb_i2s_link_rxdma(i2s0, true);
#endif
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    bflb_i2s_feature_control(i2s0, I2S_CMD_CHANNEL_LR_MERGE, false);
#else
    bflb_i2s_feature_control(i2s0, I2S_CMD_CHANNEL_LR_MERGE, true);
#endif
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    /* Lingxin_Chat_bl618 keeps RX off until the recording DMA is ready. */
    bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_RX_FIFO, 0);
    /* Stream start enables clocks after codec configuration and DMA setup. */
#else
    bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                             I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX | I2S_ARG_CMD_RX);
#endif

    if (msp_codec_reconfigure(config) != 0) {
        return -1;
    }

#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    g_jy_i2s_cfg = *config;
    g_jy_i2s_ready = true;
#endif

    printf("i2s configured: rate=%u channels=%u bit_width=%u slot_width=%u bclk=%u frame_slots=%u\r\n",
           config->sample_rate, config->channels, config->bit_width,
           config->slot_width, i2s_cfg.bclk_freq_hz, frame_slots);

    return 0;
}

void msp_i2s_device_init(uint32_t sample_rate)
{
    msp_i2s_device_cfg_t config = {
        .sample_rate = sample_rate,
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .channels = 4,
#else
        .channels = 2,
#endif
        .bit_width = 16,
        .slot_width = 16,
    };

    (void)msp_i2s_device_config(&config);
}

void msp_i2s_tx_enable(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (i2s0 != NULL) {
        uintptr_t irq_state = bflb_irq_save();
        g_jy_tx_armed = true;
        if (g_jy_duplex_mode) {
            if (g_jy_rx_armed && !g_jy_duplex_started) {
                bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                                         I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX | I2S_ARG_CMD_RX);
                g_jy_duplex_started = true;
            } else if (g_jy_duplex_started) {
                bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                                         I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX);
            }
        } else {
            bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                                     I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX);
        }
        bflb_irq_restore(irq_state);
    }
#endif
}

void msp_i2s_tx_prepare(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (i2s0 != NULL) {
        uintptr_t irq_state = bflb_irq_save();
        bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL,
                                 g_jy_rx_armed ? I2S_ARG_CMD_TX : I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX);
        bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_TX_FIFO, 0);
        bflb_irq_restore(irq_state);
    }
#endif
}

void msp_i2s_tx_disable(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (i2s0 != NULL) {
        uintptr_t irq_state = bflb_irq_save();
        g_jy_tx_armed = false;
        bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL, I2S_ARG_CMD_TX);
        if (!g_jy_rx_armed) {
            bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL, I2S_ARG_CMD_MASTER);
            g_jy_duplex_mode = false;
            g_jy_duplex_started = false;
        }
        bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_TX_FIFO, 0);
        bflb_irq_restore(irq_state);
    }
#endif
}

void msp_i2s_rx_enable(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (i2s0 != NULL) {
        uintptr_t irq_state = bflb_irq_save();
        g_jy_rx_armed = true;
        if (g_jy_duplex_mode) {
            if (g_jy_tx_armed && !g_jy_duplex_started) {
                bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                                         I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX | I2S_ARG_CMD_RX);
                g_jy_duplex_started = true;
            } else if (g_jy_duplex_started) {
                bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                                         I2S_ARG_CMD_MASTER | I2S_ARG_CMD_RX);
            }
        } else {
            bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL,
                                     I2S_ARG_CMD_MASTER | I2S_ARG_CMD_RX);
        }
        bflb_irq_restore(irq_state);
    }
#endif
}

void msp_i2s_rx_prepare(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (i2s0 != NULL) {
        uintptr_t irq_state = bflb_irq_save();
        bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL,
                                 g_jy_tx_armed ? I2S_ARG_CMD_RX : I2S_ARG_CMD_MASTER | I2S_ARG_CMD_RX);
        bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_RX_FIFO, 0);
        bflb_irq_restore(irq_state);
    }
#endif
}

void msp_i2s_rx_disable(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    if (i2s0 != NULL) {
        uintptr_t irq_state = bflb_irq_save();
        g_jy_rx_armed = false;
        bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL, I2S_ARG_CMD_RX);
        if (!g_jy_tx_armed) {
            bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL, I2S_ARG_CMD_MASTER);
            g_jy_duplex_mode = false;
            g_jy_duplex_started = false;
        }
        bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_RX_FIFO, 0);
        bflb_irq_restore(irq_state);
    }
#endif
}

int msp_i2s_duplex_begin(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    uintptr_t irq_state = bflb_irq_save();
    if (!g_jy_i2s_ready || g_jy_duplex_mode || g_jy_tx_armed || g_jy_rx_armed) {
        bflb_irq_restore(irq_state);
        return -1;
    }
    bflb_i2s_feature_control(i2s0, I2S_CMD_DISABLE_CONTROL,
                             I2S_ARG_CMD_MASTER | I2S_ARG_CMD_TX | I2S_ARG_CMD_RX);
    bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_TX_FIFO, 0);
    bflb_i2s_feature_control(i2s0, I2S_CMD_CLEAR_RX_FIFO, 0);
    g_jy_duplex_mode = true;
    g_jy_duplex_started = false;
    bflb_irq_restore(irq_state);
    return 0;
#else
    return -1;
#endif
}

void msp_i2s_duplex_cancel(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    uintptr_t irq_state = bflb_irq_save();
    if (g_jy_duplex_mode && !g_jy_duplex_started) {
        uint32_t enable = 0U;
        if (g_jy_tx_armed || g_jy_rx_armed) {
            enable = I2S_ARG_CMD_MASTER | (g_jy_tx_armed ? I2S_ARG_CMD_TX : 0U) |
                     (g_jy_rx_armed ? I2S_ARG_CMD_RX : 0U);
        }
        if (enable != 0U) {
            bflb_i2s_feature_control(i2s0, I2S_CMD_ENABLE_CONTROL, enable);
        }
    }
    g_jy_duplex_mode = false;
    g_jy_duplex_started = false;
    bflb_irq_restore(irq_state);
#endif
}

int msp_i2s_duplex_requested(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    return g_jy_duplex_mode;
#else
    return 0;
#endif
}

int msp_i2s_duplex_rx_armed(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    return g_jy_rx_armed;
#else
    return 0;
#endif
}
#endif

/* dma config */
msp_dma_ch_cfg_t msp_dma_channel_config(void)
{
    msp_dma_ch_cfg_t dma_channel_cfg = {
        .dma_tx_ctrl_id = 0,        // default 0
        .dma_rx_ctrl_id = 0,        // default 0
        .dma_tx_ch_id = DMA_TX_CHANNEL_ID,
        .dma_rx_ch_id = DMA_RX_CHANNEL_ID,
#if defined(CONFIG_CODEC_USE_I2S)
        .dma_tx_dstPeriph = 17,     //13:DMA_REQ_AUDIO_TX, 17:DMA_REQ_I2S_TX
        .dma_rx_srcPeriph = 16,     //21:DMA_REQ_SOLO_RX, 16:DMA_REQ_I2S_RX
#else
        .dma_tx_dstPeriph = 13,     //13:DMA_REQ_AUDIO_TX, 17:DMA_REQ_I2S_TX
        .dma_rx_srcPeriph = 21,     //21:DMA_REQ_SOLO_RX, 16:DMA_REQ_I2S_RX
#endif
    };

    return dma_channel_cfg;
}

#if !defined(CONFIG_CODEC_USE_I2S)
/* codec pin config */
msp_codec_pin_cfg_t msp_codec_pin_config(void)
{
    msp_codec_pin_cfg_t codec_pin_cfg = {
        .input_negative_pin = INPUT_NEGATIVE_PIN,
        .input_positive_pin = INPUT_POSITIVE_PIN,
        .output_negative_pin = OUTPUT_NEGATIVE_PIN,
        .output_positive_pin = OUTPUT_POSITIVE_PIN,
    };

    return codec_pin_cfg;
}
#endif

void msp_codec_pa_init_pre(void)
{
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
    msp_gpio_output_config(CONFIG_AUDIO_PA_PIN, 0);
#else
    msp_gpio_output_config(CONFIG_AUDIO_PA_PIN, 1);
#endif
    MSP_LOGD("msp_codec_pa_init_pre\r\n");
}

void msp_codec_pa_init(void)
{
    msp_gpio_output_set(CONFIG_AUDIO_PA_PIN, 0);
    msp_msleep(1);
    MSP_LOGD("msp_codec_pa_init\r\n");
}

void msp_codec_pa_after_opendac(void)
{
    static int init = 0;
    msp_msleep(g_pa_delay_1);
    if (0 == init) {
    }
    msp_gpio_output_set(CONFIG_AUDIO_PA_PIN, 1);
    if (0 == init) {
        init = 1;
    }
    msp_msleep(g_pa_delay_2);
    MSP_LOGD("msp_codec_pa_after_opendac\r\n");
}

void msp_codec_pa_before_closedac(void)
{
    msp_gpio_output_set(CONFIG_AUDIO_PA_PIN, 0);
    msp_msleep(20);
    MSP_LOGD("msp_codec_pa_before_closedac\r\n");
}

#ifdef CONFIG_MEDIA_GRAPH
void mg_xrecord_source_config_init(struct mg_xrecord_source_config *config)
{
    *config = (struct mg_xrecord_source_config) {
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .format.channels = 4,
        .period_count = 10,
#else
        .format.channels = 2,
        .period_count = 7,
#endif
    };
}

void mg_audio_sink_config_init(struct mg_audio_sink_config *config)
{
    *config = (struct mg_audio_sink_config) {
        .format.channels = 2,
#if defined(CONFIG_CODEC_USE_JY6316) && CONFIG_CODEC_USE_JY6316
        .period_duration_ms = 20,
        .period_count = 4,
#else
        .period_duration_ms = 5,
        .period_count = 16,
#endif
    };
}
#endif
