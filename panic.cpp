#include <panic.h>
void panic(String message)
{
	pinMode(1,OUTPUT);
	digitalWrite(1, HIGH);
	PC.print("\r\nERROR: ");
	PC.println(message);
	
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

void panic(DogLcd lcd, String message, uint8_t error)
{
	String lcd_message;
	switch (error)
	{
		case 0:  lcd_message =  ""; break;
		case 1:  lcd_message = 	"Need 6 char loc in extended WSPR    Error 01    "; break;
		case 2:  lcd_message = 	"  Callsign not  null terminated.    Error 02    "; break;
		case 3:  lcd_message = 	"Only 1 / allowed  in callsign.      Error 03    "; break;
		case 4:  lcd_message = 	"  Callsign is       too long        Error 04    "; break;
		case 5:  lcd_message = 	"  Invalid char     in suffix        Error 05    "; break;
		case 6:  lcd_message = 	"2 char suffixes must be numbers     Error 06    "; break;
		case 7:  lcd_message = 	"  Invalid char     in prefix        Error 07    "; break;
		case 8:  lcd_message = 	" Use of / char   is unsupported     Error 08    "; break;
		case 9:  lcd_message = 	"  Invalid char    in callsign       Error 09    "; break;
		case 10: lcd_message = 	"Invalid CallsignNeed num in 2/3     Error 10    "; break;
		case 11: lcd_message = 	"  Invalid Main      Callsign        Error 11    "; break;
		case 12: lcd_message = 	" Invalid Power      Entered         Error 12    "; break;
		case 13: lcd_message = 	"Invalid Locator      Format         Error 13    "; break;
		case 14: lcd_message = 	"    No Idea!                        Error 14    "; break;
		case 15: lcd_message = 	"   Zero Length      Callsign        Error 15    "; break;
		case 16: lcd_message = 	"   Zero Length      Callsign        Error 16    "; break;
		case 17: lcd_message = 	"  Callsign is       too long        Error 17    "; break;
		case 18: lcd_message = 	"     Pi not        responding       Error 17    "; break;
		case 19: lcd_message = 	"Received unknown character from Pi  Error 17    "; break;
	};
	
	lcd.setCursor(0,0);
	lcd.print(lcd_message);
	panic(message);
}