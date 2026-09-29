/*
 * Sample alignment after MTS by training on the loopback chirp.
 * Copyright (c) 2026, Yijie Yu. BSD-3-Clause.
 */

#ifndef ALIGN_H_
#define ALIGN_H_

void   align_sweep(void);       /* delay of ADC_D vs ADC_B for 0..3 coarse delay steps, ADC and DAC side */
int    align_train(void);       /* 0 = ADC_D and ADC_B within half a sample */
void   align_clear(void);       /* coarse delays back to 0 */
double align_residual(void);    /* delay left after the last training, samples */

#endif
