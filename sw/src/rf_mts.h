/*
 * RF data converter start-up and multi-tile synchronization for the RFSoC4x2.
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#ifndef RF_MTS_H_
#define RF_MTS_H_

#include "xil_types.h"

#define MTS_TILES       0x5     /* tiles 0 and 2: DAC 228 / 230, ADC 224 / 226 */
#define MTS_REF_TILE    2       /* tile 2 PLLs distribute the sample clock */

int  rf_init(void);
void rf_status(void);
int  rf_mts(void);              /* 0 = both DAC and ADC groups synchronized */
void rf_reset_tiles(void);      /* restart tiles 0 and 2: alignment is lost */
int  rf_mts_done(void);

#endif
