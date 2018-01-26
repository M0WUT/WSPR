#include "panic.h"

static DogLcd *lcd = NULL;

void panic(int message){}

void register_lcd_for_panic(DogLcd *new_lcd)
{
	lcd = new_lcd;
}

void panic(String message)
{
	pinMode(1,OUTPUT);
	digitalWrite(1, HIGH);
	PC.print("\r\nERROR: ");
	PC.println(message);
	RPI.print("S"+message+";\n");
	
	while(1)
	{
		static bool flag;
		flag= !flag;
		digitalWrite(1, flag);
		if(flag) 
		{	
			PC.print("ERROR: ");
			PC.println(message);
		}	
		delay(500);
	}
}
void panic(int message, int value){}

void panic(String message, uint8_t error)
{
	String lcd_message;
	switch (error)
	{
		case 0:  lcd_message =  ""; break;
		
		case 18: lcd_message = 	"     Pi not        responding       Error 18    "; break;
		case 19: lcd_message = 	"Received unknown character          Error 19    "; break;
		case 20: lcd_message = 	"  Calibration    Signal Invalid     Error 20    "; break;
		case 22: lcd_message =  " Something went wrong. Send Help    Error 22    "; break;
	};
	
	lcd->setCursor(0,0);
	lcd->print(lcd_message);
	panic(message);
}