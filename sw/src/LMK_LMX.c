/*
 * RFSoC4x2 clock chips over PS SPI0 (bare metal):
 *   SS0 = LMK04828 (500 MHz PL_CLK + PL_SYSREF), SS1/SS2 = LMX2594 (500 MHz ADC/DAC refclk).
 * Register values from TICS Pro, matching the PYNQ RFSoC4x2 files
 * LMK04828_500.0.txt and LMX2594_500.0.txt (RFSoC-MTS boards/RFSoC4x2/xrfclk):
 * 500 MHz LVDS FPGA_REFCLK (PL_CLK) and SYS_REF_FPGA for the MTS design.
 * Builds with the classic (device ID) and the SDT (base address) Vitis flows.
 * Define CLK_DEBUG to print the SPI read-back of every register.
 * Note: LMK PLL1 may not lock during the first minutes after power-on; rerun if needed.
 */
#include "LMK_LMX.h"
#include "xparameters.h"
#include <stdio.h>
#include "sleep.h"
#include "xgpiops.h"

#ifdef CLK_DEBUG
#define CLK_DBG(...) printf(__VA_ARGS__)
#else
#define CLK_DBG(...)
#endif

#define MIO_LMK_RST 7
#define MIO_LMK_CLK_IN_SEL0 8
#define MIO_LMK_CLK_IN_SEL1 12

//u8 WriteBuffer[136*3];//LMK has 136 regs, LMX has 113 regs, each reg has 3 bytes

u32 ClockingLmx_reg[LMX2594_count] = {
    0x700000,
    0x6F0000,
    0x6E0000,
    0x6D0000,
    0x6C0000,
    0x6B0000,
    0x6A0000,
    0x690021,
    0x680000,
    0x670000,
    0x663F80,
    0x650011,
    0x640000,
    0x630000,
    0x620200,
    0x610888,
    0x600000,
    0x5F0000,
    0x5E0000,
    0x5D0000,
    0x5C0000,
    0x5B0000,
    0x5A0000,
    0x590000,
    0x580000,
    0x570000,
    0x560000,
    0x55D300,
    0x540001,
    0x530000,
    0x521E00,
    0x510000,
    0x506666,
    0x4F0026,
    0x4E00E5,
    0x4D0000,
    0x4C000C,
    0x4B0940,
    0x4A0000,
    0x49003F,
    0x480001,
    0x470081,
    0x46C350,
    0x450000,
    0x4403E8,
    0x430000,
    0x4201F4,
    0x410000,
    0x401388,
    0x3F0000,
    0x3E0322,
    0x3D00A8,
    0x3C0000,
    0x3B0001,
    0x3A8001,
    0x390020,
    0x380000,
    0x370000,
    0x360000,
    0x350000,
    0x340820,
    0x330080,
    0x320000,
    0x314180,
    0x300300,
    0x2F0300,
    0x2E07FC,
    0x2DC0DF,
    0x2C1E20,
    0x2B0000,
    0x2A0000,
    0x290000,
    0x280000,
    0x270001,
    0x260000,
    0x250104,
    0x2400A0,
    0x230004,
    0x220000,
    0x211E21,
    0x200393,
    0x1F43EC,
    0x1E318C,
    0x1D318C,
    0x1C0488,
    0x1B0002,
    0x1A0DB0,
    0x190624,
    0x18071A,
    0x17007C,
    0x160001,
    0x150401,
    0x14C848,
    0x1327B7,
    0x120064,
    0x110117,
    0x100080,
    0x0F064F,
    0x0E1E40,
    0x0D4000,
    0x0C5002,
    0x0B0058,
    0x0A10D8,
    0x090604,
    0x082000,
    0x0740B2,
    0x06C802,
    0x0500C8,
    0x040C43,
    0x030642,
    0x020500,
    0x01080A,
    0x00241C,
};

u32 ClockingLmk_reg[LMK04828_count] ={
    0x000090,
    0x000010,
    0x000200,
    0x000306,
    0x0004D0,
    0x00055B,
    0x000600,
    0x000C51,
    0x000D04,
    0x010006,
    0x010155,
    0x010255,
    0x010301,
    0x010422,
    0x010500,
    0x010670,
    0x010733,
    0x010806,
    0x010955,
    0x010A55,
    0x010B00,
    0x010C22,
    0x010D00,
    0x010EF0,
    0x010F30,
    0x011006,
    0x011155,
    0x011255,
    0x011301,
    0x011422,
    0x011500,
    0x011673,
    0x011703,
    0x011866,
    0x011955,
    0x011A55,
    0x011B01,
    0x011C22,
    0x011D00,
    0x011E70,
    0x011F03,
    0x012066,
    0x012155,
    0x012255,
    0x012301,
    0x012422,
    0x012500,
    0x012670,
    0x012733,
    0x012866,
    0x012955,
    0x012A55,
    0x012B00,
    0x012C22,
    0x012D00,
    0x012EF0,
    0x012F30,
    0x013066,
    0x013155,
    0x013255,
    0x013301,
    0x013422,
    0x013500,
    0x013673,
    0x013703,
    0x013820,
    0x013903,
    0x013A02,
    0x013B58,
    0x013C00,
    0x013D01,
    0x013E03,
    0x013F0D,
    0x014009,
    0x014100,
    0x014200,
    0x014331,
    0x0144DD,
    0x01457F,
    0x01461B,
    0x01474A,
    0x014806,
    0x014946,
    0x014A06,
    0x014B06,
    0x014C00,
    0x014D00,
    0x014EC0,
    0x014F7F,
    0x015013,
    0x015102,
    0x015200,
    0x015300,
    0x015402,
    0x015500,
    0x015602,
    0x015703,
    0x0158C0,
    0x015900,
    0x015A01,
    0x015BDA,
    0x015C20,
    0x015D00,
    0x015E00,
    0x015F3E,
    0x016000,
    0x016120,
    0x016244,
    0x016300,
    0x016400,
    0x0165A0,
    0x0171AA,
    0x017202,
    0x017C15,
    0x017D33,
    0x016600,
    0x016701,
    0x01682C,
    0x016959,
    0x016A20,
    0x016B00,
    0x016C00,
    0x016D00,
    0x016E1B,
    0x017300,
    0x018200,
    0x018300,
    0x018400,
    0x018500,
    0x018800,
    0x018900,
    0x018A00,
    0x018B00,
    0x1FFD00,
    0x1FFE00,
    0x1FFF53,
};


static XSpiPs SpiInstance;
void write_clk(u8 slave_select){
    
    XSpiPs_Config *SpiConfig;
    // XSpiPs SpiInstance;
    XSpiPs *SpiInstancePtr = &SpiInstance;
    int Status;
    u8 TempBuffer[3];//each time write 3 bytes data
    u8 TempBufferread[3];
#ifdef SDT
    SpiConfig = XSpiPs_LookupConfig(XPAR_XSPIPS_0_BASEADDR);
#else
    SpiConfig = XSpiPs_LookupConfig(XPAR_XSPIPS_0_DEVICE_ID);
#endif
    XSpiPs_CfgInitialize(SpiInstancePtr, SpiConfig,
				      SpiConfig->BaseAddress);

    Status = XSpiPs_SelfTest(SpiInstancePtr);
	if (Status != XST_SUCCESS) {
		printf("self test fail\n");
	}

    XSpiPs_SetOptions(SpiInstancePtr, XSPIPS_MASTER_OPTION | XSPIPS_FORCE_SSELECT_OPTION);
    //XSpiPs_SetOptions(SpiInstancePtr, XSPIPS_MASTER_OPTION );

    XSpiPs_SetClkPrescaler(SpiInstancePtr, XSPIPS_CLK_PRESCALE_32);
    
	//XSpiPs_SetSlaveSelect(SpiInstancePtr, slave_select);


    if (slave_select == 0) {
        XGpioPs Gpio;
        XGpioPs_Config *GpioConfigPtr;
#ifdef SDT
        GpioConfigPtr = XGpioPs_LookupConfig(XPAR_GPIO_BASEADDR);
#else
        GpioConfigPtr = XGpioPs_LookupConfig(XPAR_XGPIOPS_0_DEVICE_ID);
#endif
        XGpioPs_CfgInitialize(&Gpio, GpioConfigPtr,
				       GpioConfigPtr->BaseAddr);
        XGpioPs_SetDirectionPin(&Gpio, MIO_LMK_RST, 1);
	    XGpioPs_SetOutputEnablePin(&Gpio, MIO_LMK_RST, 1);
	    XGpioPs_WritePin(&Gpio, MIO_LMK_RST, 1);//lmk_reset.write(1)
        XGpioPs_WritePin(&Gpio, MIO_LMK_RST, 0);//lmk_reset.write(0)

        XGpioPs_SetDirectionPin(&Gpio, MIO_LMK_CLK_IN_SEL0, 1);
	    XGpioPs_SetOutputEnablePin(&Gpio, MIO_LMK_CLK_IN_SEL0, 1);
        XGpioPs_WritePin(&Gpio, MIO_LMK_CLK_IN_SEL0, 0);//lmk_clk_sel0.write(0)
        
        XGpioPs_SetDirectionPin(&Gpio, MIO_LMK_CLK_IN_SEL1, 1);
	    XGpioPs_SetOutputEnablePin(&Gpio, MIO_LMK_CLK_IN_SEL1, 1);
        XGpioPs_WritePin(&Gpio, MIO_LMK_CLK_IN_SEL1, 0);//lmk_clk_sel1.write(0)

        XSpiPs_SetSlaveSelect(SpiInstancePtr, slave_select);
        int i;
        for (i = 0; i < LMK04828_count ; i++) {
            //memset(TempBuffer,0,3);
            TempBuffer[2] = (ClockingLmk_reg[i]) & 0xFF;
            TempBuffer[1] = (ClockingLmk_reg[i]>>8) & 0xFF;
            TempBuffer[0] = (ClockingLmk_reg[i]>>16) & 0xFF;

            
            Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
            TempBufferread, 3);
            CLK_DBG("0x%02x%02x%02x\n",TempBufferread[0],TempBufferread[1],TempBufferread[2]);
            if (Status != XST_SUCCESS) {
                xil_printf("SPI Transfer Failed\n");
            }

        }
        printf("LMK end\n");
    }
    else if (slave_select == 1) {
        //memset(TempBuffer,0,3);

        u32 reset = 0x020000;//write reset
        TempBuffer[2] = reset & 0xFF;
        TempBuffer[1] = (reset>>8) & 0xFF;
        TempBuffer[0] = (reset>>16) & 0xFF;
        
        XSpiPs_SetSlaveSelect(SpiInstancePtr, slave_select);
        Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
        NULL, 3);

        reset = 0x000000;//remove reset
        TempBuffer[2] = reset & 0xFF;
        TempBuffer[1] = (reset>>8) & 0xFF;
        TempBuffer[0] = (reset>>16) & 0xFF;
        
        Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
        NULL, 3);
        
        int i;
        for (i = 0; i < LMX2594_count ; i++) {

            TempBuffer[2] = ClockingLmx_reg[i] & 0xFF;
            TempBuffer[1] = (ClockingLmx_reg[i]>>8) & 0xFF;
            TempBuffer[0] = (ClockingLmx_reg[i]>>16) & 0xFF;

            Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
            TempBufferread, 3);
            CLK_DBG("0x%02x%02x%02x\n",TempBufferread[0],TempBufferread[1],TempBufferread[2]);        
            if (Status != XST_SUCCESS) {
                xil_printf("SPI Transfer Failed\n");
            }

        }
        u32 stable = ClockingLmx_reg[112];
        TempBuffer[2] = stable & 0xFF;
        TempBuffer[1] = (stable>>8) & 0xFF;
        TempBuffer[0] = (stable>>16) & 0xFF;
        
        Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, NULL, 3);
        
        printf("LMX%d end\n",slave_select);
    }
    else if (slave_select == 2) {
        //memset(TempBuffer,0,3);
        u32 reset = 0x020000;//write reset
        TempBuffer[2] = reset & 0xFF;
        TempBuffer[1] = (reset>>8) & 0xFF;
        TempBuffer[0] = (reset>>16) & 0xFF;
        
        XSpiPs_SetSlaveSelect(SpiInstancePtr, slave_select);
        Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
        NULL, 3);
        
        reset = 0x000000;//remove reset
        TempBuffer[2] = reset & 0xFF;
        TempBuffer[1] = (reset>>8) & 0xFF;
        TempBuffer[0] = (reset>>16) & 0xFF;
        
        Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
        NULL, 3);

        int i;
        for (i = 0; i < LMX2594_count ; i++) {

            TempBuffer[2] = ClockingLmx_reg[i] & 0xFF;
            TempBuffer[1] = (ClockingLmx_reg[i]>>8) & 0xFF;
            TempBuffer[0] = (ClockingLmx_reg[i]>>16) & 0xFF;

            XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, 
            TempBufferread, 3);
            CLK_DBG("0x%02x%02x%02x\n",TempBufferread[0],TempBufferread[1],TempBufferread[2]);
            if (Status != XST_SUCCESS) {
                xil_printf("SPI Transfer Failed\n");
            }

        }
        u32 stable = ClockingLmx_reg[112];
        TempBuffer[2] = stable & 0xFF;
        TempBuffer[1] = (stable>>8) & 0xFF;
        TempBuffer[0] = (stable>>16) & 0xFF;
        Status = XSpiPs_PolledTransfer(SpiInstancePtr, TempBuffer, NULL, 3);

        printf("LMX%d end\n",slave_select);
    }
}
