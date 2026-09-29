/*
 * RF data converter start-up and multi-tile synchronization for the RFSoC4x2.
 *
 * Tile 2 PLLs (DAC 230, ADC 226) run from the 500 MHz LMX2594 references and distribute
 * the 4 GHz sample clock to the other tiles (all four tiles are enabled because the analog
 * SYSREF is chained through every tile). MTS aligns DAC tiles 228 / 230 and ADC tiles
 * 224 / 226 to SYSREF (PL_SYSREF sampled in the fabric, see rtl/mts_sync.v).
 *
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#include <stdio.h>
#include <stdarg.h>
#include "xparameters.h"
#include "xil_printf.h"
#include "sleep.h"
#include "xrfdc.h"
#include "xrfdc_mts.h"
#include <metal/log.h>
#include <metal/sys.h>
#include "rf_mts.h"

static XRFdc rfdc;
static XRFdc_MultiConverter_Sync_Config dac_sync, adc_sync;
static int mts_ok;

static void log_handler(enum metal_log_level level, const char *format, ...)
{
    char msg[256];
    va_list args;
    va_start(args, format);
    vsnprintf(msg, sizeof(msg), format, args);
    va_end(args);
    if (level <= METAL_LOG_WARNING)
        xil_printf("metal: %s\r", msg);
}

static void start_tiles(void)
{
    /* tile 2 first: it owns the PLL that clocks the other three */
    int order[4] = {2, 3, 1, 0};
    for (int i = 0; i < 4; i++) {
        XRFdc_StartUp(&rfdc, XRFDC_DAC_TILE, order[i]);
        XRFdc_StartUp(&rfdc, XRFDC_ADC_TILE, order[i]);
    }
    usleep(100000);
}

int rf_init(void)
{
    static int metal_done;
    if (!metal_done) {
        struct metal_init_params p = METAL_INIT_DEFAULTS;
        p.log_handler = log_handler;
        if (metal_init(&p)) {
            xil_printf("libmetal init failed\r\n");
            return -1;
        }
        metal_done = 1;
    }
    XRFdc_Config *cfg = XRFdc_LookupConfig(XPAR_XRFDC_0_DEVICE_ID);
    if (!cfg || XRFdc_CfgInitialize(&rfdc, cfg) != XRFDC_SUCCESS) {
        xil_printf("RFDC init failed\r\n");
        return -1;
    }
    start_tiles();
    mts_ok = 0;
    rf_status();

    XRFdc_IPStatus st;
    XRFdc_GetIPStatus(&rfdc, &st);
    for (int t = 0; t <= 2; t += 2)
        if (st.DACTileStatus[t].TileState != 0xF || st.ADCTileStatus[t].TileState != 0xF)
            return -1;
    return 0;
}

void rf_status(void)
{
    static const char *dac_name[] = {"228 (DAC_B)", "229", "230 (DAC_A)", "231"};
    static const char *adc_name[] = {"224 (ADC_C/D)", "225", "226 (ADC_A/B)", "227"};
    XRFdc_IPStatus st;
    u32 lock;

    XRFdc_GetIPStatus(&rfdc, &st);
    for (int t = 0; t < 4; t++) {
        XRFdc_GetPLLLockStatus(&rfdc, XRFDC_DAC_TILE, t, &lock);
        xil_printf("DAC tile %-12s state 0x%x %s", dac_name[t], st.DACTileStatus[t].TileState,
                   t == 2 ? (lock == XRFDC_PLL_LOCKED ? "PLL locked" : "PLL UNLOCKED") : "clock from tile 2");
        XRFdc_GetPLLLockStatus(&rfdc, XRFDC_ADC_TILE, t, &lock);
        xil_printf(" | ADC tile %-14s state 0x%x %s\r\n", adc_name[t], st.ADCTileStatus[t].TileState,
                   t == 2 ? (lock == XRFDC_PLL_LOCKED ? "PLL locked" : "PLL UNLOCKED") : "clock from tile 2");
    }
    if (mts_ok) {
        xil_printf("MTS: DAC latency tile0 %d tile2 %d (offset %d / %d), "
                   "ADC latency tile0 %d tile2 %d (offset %d / %d)\r\n",
                   dac_sync.Latency[0], dac_sync.Latency[2], dac_sync.Offset[0], dac_sync.Offset[2],
                   adc_sync.Latency[0], adc_sync.Latency[2], adc_sync.Offset[0], adc_sync.Offset[2]);
    } else {
        xil_printf("MTS: not synchronized\r\n");
    }
}

static int sync_group(u32 type, XRFdc_MultiConverter_Sync_Config *c)
{
    XRFdc_MultiConverter_Init(c, 0, 0);
    c->Tiles = MTS_TILES;
    c->RefTile = MTS_REF_TILE;
    c->Target_Latency = -1;
    c->SysRef_Enable = 1;
    return XRFdc_MultiConverter_Sync(&rfdc, type, c) == XRFDC_MTS_OK ? 0 : -1;
}

int rf_mts(void)
{
    int d = sync_group(XRFDC_DAC_TILE, &dac_sync);
    int a = sync_group(XRFDC_ADC_TILE, &adc_sync);
    mts_ok = (d == 0 && a == 0);
    xil_printf("MTS DAC tiles 0x%x: %s, ADC tiles 0x%x: %s\r\n",
               MTS_TILES, d ? "FAILED" : "ok", MTS_TILES, a ? "FAILED" : "ok");
    if (mts_ok)
        rf_status();
    return mts_ok ? 0 : -1;
}

void rf_reset_tiles(void)
{
    for (int t = 3; t >= 0; t--) {
        XRFdc_Reset(&rfdc, XRFDC_DAC_TILE, t);
        XRFdc_Reset(&rfdc, XRFDC_ADC_TILE, t);
    }
    start_tiles();
    mts_ok = 0;
    xil_printf("tiles restarted (MTS alignment cleared)\r\n");
}

int rf_mts_done(void) { return mts_ok; }

int rf_coarse_delay(u32 type, u32 tile, u32 block, u32 steps)
{
    /* the 4 GSPS ADCs take the new value on a tile event, the DACs at once */
    int hs = (type == XRFDC_ADC_TILE) && XRFdc_IsHighSpeedADC(&rfdc, tile);
    XRFdc_CoarseDelay_Settings s;
    s.CoarseDelay = steps;
    s.EventSource = hs ? XRFDC_EVNT_SRC_TILE : XRFDC_EVNT_SRC_IMMEDIATE;
    if (XRFdc_SetCoarseDelaySettings(&rfdc, type, tile, block, &s) != XRFDC_SUCCESS)
        return -1;
    if (hs && XRFdc_UpdateEvent(&rfdc, type, tile, block, XRFDC_EVENT_CRSE_DLY) != XRFDC_SUCCESS)
        return -1;
    return 0;
}
