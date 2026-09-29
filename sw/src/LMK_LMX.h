#include "xspips.h"

#define LMK04828_count 136
#define LMX2594_count 113

typedef struct {
	//int XFrequency;
	unsigned int LMX2594_A[LMX2594_count];
} XClockingLmx;

typedef struct {
	// int Fosc;			//Fosc freq (KHz)
	// int axiFreq;		// axi clock freq (KHz)
	// int sysrefFreq;     // sysref freq (KHz)
	// int refClkFreq;		// refclk Frequency (KHz)
	// int refClkSrc;		// 1=refclk external
	unsigned int data[LMK04828_count];
} XClockingLmk;

#define LMK_SELECT  0
#define LMX_PLL1    1
#define LMX_PLL2    2

#define WRITE_STATUS_CMD	0x01
#define WRITE_CMD		0x02
#define READ_CMD		0x03
#define WRITE_DISABLE_CMD	0x04
#define READ_STATUS_CMD		0x05
#define WRITE_ENABLE_CMD	0x06

// void LMK04828_write(int slave_select);
// void LMX2594_write(int PLL_select);
// void LMK_LMX();

void write_clk(u8 slave_select);
