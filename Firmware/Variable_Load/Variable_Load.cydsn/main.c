#include "project.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include "terminal.h"

#define KPN 19
#define KPD 100
#define KIN 1
#define KID 10

#define DEFAULT_I_LIM 0.0


/* Defines for SourceDMA */
#define SourceDMA_BYTES_PER_BURST 2
#define SourceDMA_REQUEST_PER_BURST 1
#define SourceDMA_SRC_BASE (CYDEV_PERIPH_BASE)
#define SourceDMA_DST_BASE (CYDEV_SRAM_BASE)

/* Variable declarations for SourceDMA */
/* Move these variable declarations to the top of the function */
#define ADCSAMPLES 64
static uint8 SourceDMA_Chan;
static uint8 SourceDMA_TD[1];
static uint16 SourceData[ADCSAMPLES];

void DoPid();

volatile static int32 systemTimer = 0;

static float fiLimit;
volatile static int iLimit = DEFAULT_I_LIM;
volatile static int vSource = 0;
volatile static int iSource = 0;
volatile static uint32 dt = 0;
volatile static int vMin = 2000;

bool enableOutput = false;

int main(void)
{
	int i;
    int vAve;
    
    CyGlobalIntEnable; /* Enable global interrupts. */

	// Create our timing "tick" variables. These are used to record when the last
	// iteration of an action in the main loop happened and to trigger the next
	// iteration when some number of clock ticks have passed.
	int32_t SlowTick = 0;
    
	Gate_Drive_Tune_Start();
	Offset_Start();
	Offset_Gain_Start();
	GDT_Buffer_Start();
	O_Buffer_Start();
	Source_ADC_Start();
	I_Source_ADC_Start();
    
	bool upPressed = false;
	bool downPressed = false;
	bool entPressed = false;
	bool backPressed = false;

	char buff[64];
	uint8_t inBuff[64];
	uint8_t floatBuff[64];
	uint8_t incCharIndex = 0;

	USBUART_Start(0, USBUART_5V_OPERATION);
	CapSense_Start();
	CapSense_InitializeAllBaselines();
	ConversionClock_Start();
	LCD_Start();
	LCD_DisplayOn();
	LCD_PrintString("Hello, world");

    /* DMA Configuration for SourceDMA */
    SourceDMA_Chan = SourceDMA_DmaInitialize(SourceDMA_BYTES_PER_BURST, SourceDMA_REQUEST_PER_BURST, 
        HI16(SourceDMA_SRC_BASE), HI16(SourceDMA_DST_BASE));
    SourceDMA_TD[0] = CyDmaTdAllocate();
    CyDmaTdSetConfiguration(SourceDMA_TD[0], 2*ADCSAMPLES, SourceDMA_TD[0], CY_DMA_TD_INC_DST_ADR | CY_DMA_TD_AUTO_EXEC_NEXT);
    CyDmaTdSetAddress(SourceDMA_TD[0], LO16((uint32)Source_ADC_SAR_WRK0_PTR), LO16((uint32)SourceData));
    CyDmaChSetInitialTd(SourceDMA_Chan, SourceDMA_TD[0]);
    CyDmaChEnable(SourceDMA_Chan, 1);
	Source_ADC_StartConvert();
    
	init();

	for(;;)
	{
		if (0u != USBUART_IsConfigurationChanged())
		{
			if (0u != USBUART_GetConfiguration())
			{
				USBUART_CDC_Init();
			}
		}
		
		if(0u == CapSense_IsBusy())
		{
			CapSense_UpdateEnabledBaselines();
			CapSense_ScanEnabledWidgets();
		}
		
		// Handle a press of the back key
		if (CapSense_CheckIsWidgetActive(CapSense_BACK__BTN) && backPressed == false)
		{
			backPressed = true;
			iLimit = DEFAULT_I_LIM;
			Output_On_LED_Write(0);
			enableOutput = false;
		}
		else if (!CapSense_CheckIsWidgetActive(CapSense_BACK__BTN) && backPressed == true)
		{
			backPressed = false;
		}
		
		if (CapSense_CheckIsWidgetActive(CapSense_ENTER__BTN) && entPressed == false)
		{
			entPressed = true;
			enableOutput = !enableOutput;
			if (enableOutput)
			{
				Output_On_LED_Write(1);
			}
			else
			{
				Output_On_LED_Write(0);
			}
		}
		else if (!CapSense_CheckIsWidgetActive(CapSense_ENTER__BTN) && entPressed == true)
		{
			entPressed = false;
		}
		
		if (CapSense_CheckIsWidgetActive(CapSense_DOWN__BTN) && downPressed == false)
		{
			downPressed = true;
			if (iLimit < 10) iLimit = 0;
			else if (iLimit < 101) {if (iLimit >= 10) iLimit -= 10;}
			else if (iLimit < 501) iLimit -= 50;
			else if (iLimit < 1001) iLimit -= 100;
			else iLimit -= 500;
		}
		else if (!CapSense_CheckIsWidgetActive(CapSense_DOWN__BTN) && downPressed == true)
		{
			downPressed = false;
		}
		
		if (CapSense_CheckIsWidgetActive(CapSense_UP__BTN) && upPressed == false)
		{
			upPressed = true;
			if (iLimit < 99) iLimit += 10;
			else if (iLimit < 500) iLimit += 50;
			else if (iLimit < 1000) iLimit += 100;
			else if (iLimit < 3500) iLimit += 500;
			else iLimit = 4000;
		}
		else if (!CapSense_CheckIsWidgetActive(CapSense_UP__BTN) && upPressed == true)
		{
			upPressed = false;
		}
		
		
		// Fetch any waiting characters from the USB UART.
		int charCount = USBUART_GetCount();
		if (charCount > 0)
		{
			int i = USBUART_GetAll(inBuff);

			for (i = 0; i < charCount; i++)
			{
				floatBuff[incCharIndex++] = inBuff[i];
				if (incCharIndex > 63) incCharIndex = 0;
				if (inBuff[i] == '\r' ||
						inBuff[i] == '\n')
				{
					floatBuff[incCharIndex] = '\0';
					break;
				}
			}
		}
		
// Average the DMA data from the Source Voltage ADC
        vAve = 0;
        for (i = 0; i < ADCSAMPLES; i++)
            vAve += SourceData[i];

        vAve /= ADCSAMPLES;
        
	    vSource = 10*Source_ADC_CountsTo_mVolts(vAve);
		
        if (systemTimer - 200 > SlowTick)
		{
			SlowTick = systemTimer;
			cls();
			goToPos(1,1);
			putString("I Source:");
			goToPos(1,2);
			putString("I Limit:");
			goToPos(1,3);
			putString("V Source:");
			goToPos(1,4);
			putString("V Min:");
			goToPos(12,1);
			if (iSource < 0) iSource *= -1;
			sprintf(buff, "%.3f", iSource/1000.0f);
			putString(buff);
			goToPos(12,2);
			sprintf(buff, "%.3f", iLimit/1000.0f);
			putString(buff);
			goToPos(12,3);
			sprintf(buff, "%.3f", vSource/1000.0f);
			putString(buff);
			goToPos(12,4);
			sprintf(buff, "%.3f", vMin/1000.0f);
			putString(buff);
			
			sprintf(buff, "I: %.2f V: %.2f", iLimit/1000.0f, vSource/1000.0f);
			LCD_Position(0,0);
			LCD_PrintString(buff);
			sprintf(buff, "Imeas: %.2f", iSource/1000.0f);
			LCD_Position(1,0);
			LCD_PrintString(buff);
			

			if (floatBuff[incCharIndex] == '\0')
			{
				fiLimit = atoff((char*)floatBuff);
				memset(floatBuff, 1, 64);
				incCharIndex = 0;
				if (fiLimit > 4.000 ||
						iLimit < 0.0) iLimit = 0.0;
                iLimit = 1000.0*fiLimit;
			}
		}
	}
}

void I_Source_ADC_ISR1_EntryCallback()
{
    systemTimer++;
	DoPid();
}


void DoPid()
{
	static int error = 0.0;
	static int integral = 0.0;
	static int32_t iSourceRaw = 0;
	static uint16_t grossSetPoint = 0;
	static uint16_t fineSetPoint = 0;
	static int setPoint = 0.0;
    
	iSourceRaw = I_Source_ADC_GetResult32();
	iSource = 10 * I_Source_ADC_CountsTo_mVolts(iSourceRaw);

	error = iLimit - iSource;
	integral = integral + error;
	setPoint = (KPN * iLimit)/KPD + (KIN * integral)/KID + 2000; // Use feed forward plus integral
    if (setPoint < 0)
        setPoint = 0;
    
	// setPoint is a voltage. We need to convert that
	//  into an integer that can be fed into our DACs.
	// First, find our grossSetPoint. This is a largish voltage that
	//  represents a coarse 0-4V offset in 16mV steps.
	grossSetPoint = (int)(setPoint/16);
	// We want to limit our gross offset to 255, since it's an 8-bit
	//  DAC output.
	if (grossSetPoint > 255)
	    grossSetPoint = 255;
	// Now, find the fineSetPoint. This is a 4mV step 8-bit DAC which
	//  allows us to tune the set point a little more finely.
	fineSetPoint = (setPoint - grossSetPoint*16)/4;
    if (fineSetPoint > 255)
	    fineSetPoint = 255;

	// Finally, one last check: if the source voltage is below 2.0V,
	//  the output is disabled, or the total power is greater than 15W,
	//  we want to clear everything and zero the gate drive.
	if ((vSource < vMin) ||
			(enableOutput == false) ||
			(vSource * iLimit > 15000000))
	{
		error = 0;
		integral = 0;
		grossSetPoint = 0;
		fineSetPoint = 0;
	}
	Offset_SetValue(grossSetPoint);
	Gate_Drive_Tune_SetValue(fineSetPoint);
}

