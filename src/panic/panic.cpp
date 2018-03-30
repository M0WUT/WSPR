#include "panic.h"

static DogLcd *lcd = NULL;


void register_lcd_for_panic(DogLcd *newLcd)
{
	lcd = newLcd;
}

static void panic_loop(String uartMessage, String lcdMessage)
{
	pinMode(1,OUTPUT);
	lcd->clear();
	lcd->write(0, 0, lcdMessage);
	
	RPI.print("S"+uartMessage+";\n");
	
	while(1)
	{
		static bool flag;
		flag= !flag;
		digitalWrite(1, flag);
		if(flag) 
			PC.println(uartMessage);
		delay(500);
	}
	
	
	
	
}

void panic(int error, String value)
{
	String uartMessage;
	String lcdMessage;
	switch(error)
	{
		case INVALID_STATE_ACCESSED: uartMessage = "Tried to access invalid state: " + value; lcdMessage = "  Attempted to  access state " + value + " "; break;
		case PI_INCOMPLETE_TRANSMISSON: uartMessage = "Incorrect UART Transmission. Last character" + value; lcdMessage = " Incorrect UART  Termination: " + value + " "; break;
		case PI_UNKNOWN_CHARACTER: uartMessage = "Received unhandled string from Pi" + value; lcdMessage = "Unknown String: " + value.substring(0,16); break;
		
		
		
		
	};
	
	panic_loop(uartMessage + " Error: " + error, lcdMessage + "   Error: " + (error < 10) ? String("0") : String("") + String("   "));
	
	
}

void panic(int error)
{
	String uartMessage;
	String lcdMessage;
	switch(error)
	{
		case PI_NOT_RESPONDING:			uartMessage = "Pi not responding."; 								lcdMessage = "     Pi not        responding   "; break;
		case GPS_UART_NOT_REGISTERED: 	uartMessage = "No GPS module registered with supervisor."; 			lcdMessage = "    GPS not        registered   "; break;
		case PI_UART_NOT_REGISTERED: 	uartMessage = "No Pi UART registered with supervisor.";				lcdMessage = "     Pi not        registered   "; break;
		case PI_INCOMPLETE_TRANSMISSON: uartMessage = "Incomplete UART Transmission";						lcdMessage = "Incomplete UART   transmission  "; break;
		case TIME_SYNC_FAILED: 			uartMessage = "Time Sync Failed"; 									lcdMessage = "   Time Sync         Failed     "; break;
		case INVALID_SYNC_PARAMETERS: 	uartMessage = "Invalid Sync Parameters"; 							lcdMessage = "  Invalid Sync     Parameters   "; break;
		case SI5351_DIVIDER_ERROR: 		uartMessage = "Error calculating Si5351 divider"; 					lcdMessage = " Si5351 Divider      Error      "; break;
		case I2C_NOT_RESPONDING: 		uartMessage = "Si5351 not responding"; 								lcdMessage = "   Si5351 not      responding   "; break;
		case WEIRD_I2C_ERROR: 			uartMessage = "I2C is unhappy";										lcdMessage = "  Unknown I2C        Error      "; break;
		case INVALID_CLOCK: 			uartMessage = "Attempted to access Invalid Clock (>2) on Si5351"; 	lcdMessage = "  Si5351 Clock       Error      "; break;
		case INVALID_PLL:	 			uartMessage = "Attempted to access Invalid PLL on Si5351"; 			lcdMessage = "   Si5351 PLL        Error      "; break;
		case VCO_ERROR:		 			uartMessage = "VCO set outside valide range of 600-900MHz";			lcdMessage = "   Si5351 VCO        Error      "; break;
		case INCORRECT_CAPACITANCE:		uartMessage = "Si5351 Crystal Capacitance set wrong"; 				lcdMessage = "Si5351 Capacitor     Error      "; break;
		case INCORRECT_XTAL_FREQ:		uartMessage = "Si5351 Crystal Frequency not in valid range";		lcdMessage = " Si5351 Crystal      Error      "; break;
		
		
		
		
		
	};
	
	panic_loop(uartMessage + " Error: " + error, lcdMessage + "   Error: " + (error < 10) ? String("0") : String("") + String("   "));
}


