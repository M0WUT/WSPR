#include "WSPR_config.h"

String callsign="M0WUT";
String locator="AA00aa";

int tx_percentage = 20;

char symbols[162]; //Used to store encoded WSPR symbols
char symbols2[162]; //Used to store more WSPR symbols in case of extended mode

Si5351 osc;
TinyGPSPlus gps;
DogLcd lcd(20, 21, 24, 22);
bool calibration_flag, pi_rx_flag, state_initialised=0, editing_flag=0, warning = 0, gps_enabled=1, extended_mode = 0; //flags used to indicate to the main loop that an interrupt driven event has completed
uint32_t calibration_value; //Contains the number of pulses from 2.5MHz ouput of Si5351 in 80 seconds (should be 200e6) 
int substate=0;

enum menu_state{START, UNCONFIGURED, UNLOCKED, HOME, PANIC, CALLSIGN, CALLSIGN_CHECK, EXTENDED_CHECK, LOCATOR, LOCATOR_CHECK, POWER, POWER_WARNING, POWER_QUESTION, TX_PERCENTAGE, IP, BAND, OTHER_BAND_WARNING, OTHER_BAND_QUESTION, ENCODING, DATE_FORMAT, CALIBRATING};
enum power_t{dbm0, dbm3, dbm7, dbm10, dbm13, dbm17, dbm20, dbm23, dbm27, dbm30, dbm33, dbm37, dbm40, dbm43, dbm47, dbm50, dbm53, dbm57, dbm60};
enum band_t{BAND_160, BAND_80, BAND_60, BAND_40, BAND_30, BAND_20, BAND_17, BAND_15, BAND_12, BAND_10, BAND_OTHER};
const String band_strings[] = {"160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "Other"}; 
const String dbm_strings[] = {"0dBm", "3dBm", "7dBm", "10dBm", "13dBm", "17dBm", "20dBm", "23dBm", "27dBm", "30dBm", "33dBm", "37dBm", "40dBm", "43dBm", "47dBm", "50dBm", "53dBm", "57dBm", "60dBm"};
const String watt_strings[] = {"1mW", "2mW", "5mW", "10mW", "20mW", "50mW", "100mW", "200mW", "500mW", "1W", "2W", "5W", "10W", "20W", "50W", "100W", "200W", "500W", "1kW"};
const double band_freq[] = {1836600.0, 3592600.0, 5287200.0, 7038600.0, 10138700.0, 14095600.0, 18104600.0, 21094600.0, 24924600.0, 28124600.0};
enum date_t {BRITISH, AMERICAN, GLOBAL};
const String date_strings[] = {"DD/MM/YY", "MM/DD/YY", "YY/MM/DD"};
const uint32_t wspr_tone_delay = (uint32_t)(256000.0 * (double)CORE_TICK_RATE/375.0);

const char letters[] = {'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z','0','1','2','3','4','5','6','7','8','9','/',' ','A'}; //the extra A means the index can be incremented from '/', next time it searches for 'A' it will returns 0 not 37 as it loops from the starts
const String blank_line = "                ";
menu_state state = START;
power_t power = dbm23;
band_t band = BAND_20;
date_t date_format = BRITISH;
char frequency_char[9] = {'0','0','1','3','6','0','0','0',0};
uint32_t frequency = 136e3;
double tx_frequency=0;


void setup()
{
	lcd.begin(DOG_LCD_M163);
	lcd.noCursor();
	register_lcd_for_panic(&lcd);
	GPS.begin(9600);
	RPI.begin(115200);
	PC.begin(9600); //Used for debugging and stuff
	
	pinMode(GPS_PPS, INPUT);
	pinMode(MENU_BTN, INPUT);
	pinMode(EDIT_BTN, INPUT);
	pinMode(LED, OUTPUT);
	pinMode(RPI_UART_PROD, OUTPUT);
	digitalWrite(LED, LOW);
	digitalWrite(RPI_UART_PROD, LOW);
	
	osc.begin(XTAL_10pF, 25000000,GPS_ENABLED);
	
	callsign.reserve(10);
	locator.reserve(6);
}

void lcd_write(int row, int col, String data)
{
	lcd.setCursor(row, col);
	lcd.print(data);
}

int letters_find(char x) //returns the index of char x in the array "letters"
{
	for (int i=0; i<39; i++)
	{
		if(letters[i]==x) return i;
	}
	return -1;
}

void state_clean() //clears anything that needs to be cleaned up before changing state
{
	lcd.clear();
	lcd.noBlink();
	lcd.noCursor();
	warning = 0;
	state_initialised=0;
	editing_flag=0;
	substate=0;
}

bool callsign_check()
{
	static bool zero_found;
	for (int i =0; i<10; i++)
	{
		if(callsign[i]==' ')//Once we find the first empty character
		{
			for (int j = i; j<10; j++)
			{
				if(callsign[j] != ' ') return 1; //valid data has been found after the blank
			}
			return 0; //All good
		}
	}
	return 0;
}

void check_frequency()
{
	
	static int counter = 0;
	counter++;
	if(counter==80)
	{
		calibration_value = TMR2;
		TMR2 = 0;
		calibration_flag = 1; //Notify main loop
		counter=0;
	}

}

bool menu_pressed()
{
	return !digitalRead(MENU_BTN);
}

bool edit_pressed()
{
	return !digitalRead(EDIT_BTN);
}

void request_from_PI(String command, String &return_string, int max_length)
{
	while(RPI.available()) RPI.read();
	return_string="";
	RPI.print(command+";\n"); //Send the request to the PI
	while(!RPI.available()) {static int counter = 0; counter++; if (counter==TIMEOUT) panic("No response from Pi",18);}
	if(RPI.read() != command[0]) panic("Unexpected character received from Pi", 19);
	while(!RPI.available()) {static int counter = 0; counter++; if (counter==TIMEOUT) panic("No response from Pi",18);}
	if(RPI.read() != command[1]) panic("Unexpected character received from Pi", 19);
	while(!RPI.available()) {static int counter = 0; counter++; if (counter==TIMEOUT) panic("No response from Pi",18);}
	char x;
	for(int i = 0; i < max_length; i++)
	{
		x = RPI.read();
		if(x != ';') return_string += x;
		else break;
		while(!RPI.available()) {static int counter = 0; counter++; if (counter==TIMEOUT) panic("No response from Pi",18);}
	}
}

uint32_t tx (uint32_t currentTime)
{
	substate++;
	return (currentTime + wspr_tone_delay);
}

void loop()
{
	if(calibration_flag) //GPS calibration has been updated 
	{
		#if DEBUG
			PC.println(calibration_value);
		#endif
		static int error = 0;
		if(abs(200e6 - calibration_value) > 10e3)
		{
			if(++error == 3) panic("GPS calibration failed", 20);	
		}
		else
		{
			osc.resources[0] = calibration_value <<2;
			osc.resources[1] = calibration_value <<2;
		}
		calibration_value = 0;
		calibration_flag = 0;
	}
	
	switch (state) //going to attempt to implement as a state machine, sure M0IKY will have some complaints to make 
	{
		case START:
		{
			if(0); //TODO add EEPROM read function to see if valid configuration data has been found	
			else
			{
				state_clean();
				state = UNCONFIGURED;
				goto end;
			}
			
			break;
		} 
		
		case UNCONFIGURED:
		{
			if(!state_initialised)
			{
				lcd_write(0,1, "Not configured");
				lcd_write(1,1, "Use webpage or");
				lcd_write(2,2, "press \"Menu\"");
				while(menu_pressed()) delay(50); //Wait for any previous button press to clear (written here so the display updates before debouncing)
				state_initialised=1;
			}
		
			if(menu_pressed())
			{
				state_clean();
				state = IP;
				goto end;
			}
			
			break;
		}
		
		case CALLSIGN:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,4, "Callsign");
				lcd_write(1,0, callsign);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}

			if(menu_pressed())
			{
				switch(editing_flag)
				{
					case 0: //Not editing callsign so move to next menu screen
					{
						if(!warning)
						{
							state_clean();
							state = CALLSIGN_CHECK;
							goto end;
						}
						else
						{
							for (int i =0; i<3; i++)
							{
								lcd_write(2,0,blank_line);
								delay(400);
								lcd_write(2,0,"Data after blank");
								delay(400);
							}
						}
						break;
					}
					
					case 1: //Editing callsign so move cursor to next character
					{ 
						if(++substate == 10)
						{
							substate = 0;
							callsign.trim(); // Remove any spaces at the end
							lcd_write(1,0,blank_line); //Rewrite the callsign
							lcd_write(1,0,callsign);
							editing_flag = 0; // Have swept over whole callsign so assume editing is done
							lcd.noCursor();
							lcd.noBlink();
							break;
						}
						lcd.setCursor(1,substate); 
						break;
					}
				}; 
				while(menu_pressed()) delay(50);	
			}

			if(edit_pressed())
			{
				switch(editing_flag)
				{
					case 0: //Start editing
					{
						lcd.setCursor(1,0);
						lcd.cursor();	
						lcd.blink();
						editing_flag=1;
						break;
					}
					
					case 1: //Change current character (indexed by substate)
					{
						if((substate+1) > callsign.length()) //We're editing a character that doesn't exist
						{
							for (int i = callsign.length(); i < (substate + 1); i++) //add spaces between here and the character being edited
							{
								callsign += ' ';
							}
						}
						int x = letters_find(callsign[substate]); //x is the index of the current character in array "letters"
						if(x!=-1) callsign[substate] = letters[x+1]; //Overflow has been dealt with by adding the first character in letters to the end, when searched for next time, it will return the first instance of it at index 0
						else panic("Invalid character found in callsign", 9);
						lcd_write(1, 0, callsign);
						if(callsign_check())
						{
							lcd_write(2,0,"Data after blank");
							warning = 1;
						}
						else
						{
							warning = 0;
							lcd_write(2,0,blank_line);
						}
						
						lcd.setCursor(1,substate);		
						break;
					} 
				};
				while(edit_pressed())delay(50);
			}
			
			break; 
		}//end of CALLSIGN
		
		case CALLSIGN_CHECK:
		{
			String lcd_message;
			//Not actual encoding, just checks for callsign related errors
			switch (WSPR::encode(callsign, locator, power, symbols, WSPR_NORMAL))
			{
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
				case 15: lcd_message = 	"   Zero Length      Callsign        Error 15    "; break;
				case 16: lcd_message = 	"   Zero Length      Callsign        Error 16    "; break;
				case 17: lcd_message = 	"  Callsign is       too long        Error 17    "; break;
				case 18: extended_mode = 1; //Deliberate fallthrough as that is a success (same as default case)
				default: 
					state_clean();
					state=LOCATOR;
					goto end;
			};
			
			lcd_write(0,0, lcd_message);
			delay(2000);
			state_clean();
			state = CALLSIGN;
			goto end;
		}//end of CALLSIGN_CHECK
		
		case LOCATOR:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,4, "Locator");
				if(!gps_enabled)
				{
					lcd_write(1,0, locator);
					lcd_write(2,1, " GPS: Hold Edit ");
				}
				else 
				{
					lcd_write(1,0, "Set by GPS");
					lcd_write(2,0, "Manual:Hold Edit");
				}

				while(menu_pressed()) delay(50);
				state_initialised=1;
				break;
			}

			if(menu_pressed())
			{
				switch(editing_flag)
				{
					case 0: //Not editing locator so move to next menu screen
					{
						state_clean();
						state= LOCATOR_CHECK;
						goto end;
						break;
					}
					
					case 1: //Editing locator so move cursor to next character
					{ 
						if(++substate == 6)
						{
							editing_flag = 0; // Have swept over whole locator so assume editing is done
							substate = 0;
							lcd.noCursor();
							lcd.noBlink();
							break;
						}
						lcd.setCursor(1,substate); 
						break;
					}
				}; 
				while(menu_pressed()) delay(50);	
			}

			if(edit_pressed())
			{ 
				int counter =0;
				while(edit_pressed())
				{	
					if(++counter>(3*SECOND))
					{
						gps_enabled = !gps_enabled;
						counter=0;
						editing_flag=0;
						
						if(!gps_enabled)
						{
							lcd_write(1,0, blank_line);
							lcd_write(1,0, locator);
							lcd_write(2,0, " GPS: Hold Edit ");
						}
						else 
						{
							lcd_write(1,0, blank_line);
							lcd_write(1,0, "Set by GPS");
							lcd_write(2,0, "Manual:Hold Edit");
						}
						editing_flag = 0;
						lcd.noBlink();
						lcd.noCursor();
						while(edit_pressed())delay(50);
						goto end;
					}
				}
		
				if(!gps_enabled)
				{
					switch(editing_flag)
					{
						case 0: //Start editing
						{
							lcd.setCursor(1,0);
							lcd.cursor();
							lcd.blink();	
							editing_flag=1;
							break;
						}
						
						case 1: //Change current character (indexed by substate)
						{
							switch (substate)
							{
								case 0: ;//Allow fall thourgh as same behaviour if editing the first two characters
								case 1: if(++locator[substate] == 'S') locator[substate] = 'A'; break;
								case 2: ;//Fallthrough
								case 3: if(++locator[substate] == ':') locator[substate] = '0'; break; //':' is next in ASCII after '9'
								case 4: ;//Fallthrough
								case 5: if(++locator[substate] =='y') locator[substate] = 'a'; break;
							};
							lcd_write(1, 0, locator);
							lcd.setCursor(1,substate);
							break;
						} 
					};
				}
				while(edit_pressed())delay(50);
			}
			
			break; 
		}//end of LOCATOR
		
		case LOCATOR_CHECK:
		{
			if(gps_enabled) 
			{
				state_clean();
				state=POWER;
				goto end;
			}
			
			String lcd_message;
			//Not actual encoding, just checks for locator related errors
			switch (WSPR::encode(callsign, locator, power, symbols, WSPR_NORMAL))
			{
				case 1:  lcd_message = 	"Need 6 char loc in extended WSPR    Error 01    "; break;
				case 13: lcd_message = 	"Invalid Locator      Format         Error 13    "; break;
				default: 	if(!extended_mode)
							{
								state_clean();
								state=POWER;
								goto end;
							}
							break;
			};
			
			if(extended_mode)
			{
				switch (WSPR::encode(callsign, locator, power, symbols, WSPR_EXTENDED))
				{
					case 1:  lcd_message = 	"Need 6 char loc in extended WSPR    Error 01    "; break;
					case 13: lcd_message = 	"Invalid Locator      Format         Error 13    "; break;
					default: 
						state_clean();
						state=POWER;
						goto end;
				};
			}
			lcd_write(0,0, lcd_message);
			delay(2000);
			state_clean();
			state = LOCATOR;
			goto end;
		}//end of LOCATOR_CHECK
		
		
		case POWER:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,5, "Power");
				lcd_write(1,0, watt_strings[power]);
				lcd_write(1, 16-(dbm_strings[power].length()), dbm_strings[power]);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = TX_PERCENTAGE;
				goto end;	
			}
			
			if(edit_pressed())
			{
				state_clean();
				state = POWER_WARNING;
				goto end;	
			}
			
			break;
		}//end of POWER
		
		case POWER_WARNING:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,0,"Changes reported");
				lcd_write(1,1, "PWR not actual");
				lcd_write(2,0, "PWR Press \"Menu\"");
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
				state_initialised=1;
			}
		
			if (menu_pressed())
			{
				state_clean();
				state = POWER_QUESTION;
				goto end;
			}
			
			break;
		}//end of POWER_WARNING
		
		case POWER_QUESTION:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,5, "Power");
				lcd_write(1,0, watt_strings[power]);
				lcd_write(1, 16-(dbm_strings[power].length()), dbm_strings[power]);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
		
			if (menu_pressed())
			{
				state_clean();
				state= TX_PERCENTAGE;
				goto end;
			}
			
			if (edit_pressed())
			{
				if(power != dbm60) power = (power_t)((int)power+1);
				else power=dbm0;
				lcd_write(1,0,blank_line);
				lcd_write(1,0, watt_strings[power]);
				lcd_write(1, 16-(dbm_strings[power].length()), dbm_strings[power]);
				while(edit_pressed())delay(50);
			}
			
			break;
		}//end of POWER_QUESTION
		
		case TX_PERCENTAGE:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,1, "TX Percentage");
				lcd_write(1,0, ((String)tx_percentage+"%"));
				while(menu_pressed()) delay(50);
				while(edit_pressed())delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = BAND;	
				goto end;	
			}
			
			if(edit_pressed())
			{
				tx_percentage +=10;
				if(tx_percentage>100) tx_percentage=0;
				lcd_write(1,0,blank_line);
				lcd_write(1,0, ((String)tx_percentage+"%"));
				while(edit_pressed())delay(50);
			}
			
			break;
		}//end of TX_PERCENTAGE	
			
		case IP:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				String data_received;
				data_received.reserve(16);
				lcd_write(0,0, "IP and Hostname");
				request_from_PI("IP", data_received,16);
				lcd_write(1,0, data_received);
				request_from_PI("HO", data_received,16);
				lcd_write(2,0,data_received);
				while(menu_pressed()) delay(50);
				while(edit_pressed())delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = CALLSIGN;
				goto end;
			}
			
			break;
		}//end of IP
		
		case BAND:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,6, "Band");
				lcd_write(1,0, band_strings[band]);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				if(band == BAND_OTHER) state = OTHER_BAND_WARNING;
				else state = DATE_FORMAT;
				goto end;	
			}
			
			if(edit_pressed())
			{
				if(band != BAND_OTHER) band = (band_t)((int)band+1);
				else band=BAND_160;
				lcd_write(1,0,blank_line);
				lcd_write(1,0, band_strings[band]);
				while(edit_pressed())delay(50);
			}
			
			break;
		}//end of POWER	
		
		case OTHER_BAND_WARNING:
		{
			if(!state_initialised)
			{
				lcd_write(0,4, "WARNING!");
				lcd_write(1,0, "NO OUTPUT FILTER");
				lcd_write(2,2, "Press \"Menu\"");
				while(menu_pressed()) delay(50); //Wait for any previous button press to clear (written here so the display updates before debouncing)
				state_initialised=1;
			}
		
			if(menu_pressed())
			{
				state_clean();
				state = OTHER_BAND_QUESTION;
				goto end;
			}
			
			break;
		}//end of OTHER_BAND_WARNING	
		
		case OTHER_BAND_QUESTION:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,1, "Enter VFO Freq");
				lcd_write(1,0, frequency_char);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}

			if(menu_pressed())
			{
				switch(editing_flag)
				{
					case 0: //Not editing frequency so move to next menu screen
					{
						
						frequency = 0;
						for (int i = 0; i<8; i++)
						{
							frequency *= 10;
							frequency += (frequency_char[i]-'0');
						}
						Serial.print(frequency);
						if(frequency < 500e3)
						{
							for (int i =0; i<3; i++)
							{
								lcd_write(2,0,blank_line);
								delay(400);
								lcd_write(2,0,"Must be >500kHz");
								delay(400);
							}
							lcd_write(2,0,blank_line);
						}
						else
						{
							state_clean();
							state = DATE_FORMAT;
							goto end;
						}
						
						break;
					}
					
					case 1: //Editing frequency so move cursor to next character
					{ 
						if(++substate == 8)
						{
							substate = 0;
							editing_flag = 0; // Have swept over whole callsign so assume editing is done
							lcd.noCursor();
							lcd.noBlink();
							break;
						}
						lcd.setCursor(1,substate); //Callsign starts at column 3
						break;
					}
				}; 
				while(menu_pressed()) delay(50);	
			}

			if(edit_pressed())
			{
				switch(editing_flag)
				{
					case 0: //Start editing
					{
						lcd.setCursor(1,0);
						lcd.cursor();
						lcd.blink();
						editing_flag=1;
						break;
					}
					
					case 1: //Change current character (indexed by substate)
					{
						if(substate == 0)
						{
							if(++frequency_char[0] == '3') frequency_char[substate] = '0'; //Limiy maximum frequency to 29.999999MHz
						}
						else
						{
							if(++frequency_char[substate] == ':') frequency_char[substate] = '0';	
						}
						lcd_write(1,0, frequency_char);
						lcd.setCursor(1,substate);
						break;
					} 
				};
				while(edit_pressed())delay(50);
			}
			
			break; 
		}//end of OTHER_BAND_QUESTION
		
		case DATE_FORMAT:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,2, "Date Format");
				lcd_write(1,0, date_strings[date_format]);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				if(gps_enabled) state = UNLOCKED;
				else state = ENCODING;
				goto end;	
			}
			
			if(edit_pressed())
			{
				if(date_format != GLOBAL) date_format = (date_t)((int)date_format+1);
				else date_format = BRITISH;
				lcd_write(1,0, date_strings[date_format]);
				while(edit_pressed())delay(50);	
			}
			
			break;
		}//end of DATE_FORMAT	
		
		case UNLOCKED:
		{
			if(!state_initialised)
			{
				lcd_write(0,0, "Waiting for GPS");
				lcd_write(1,2, "Lock. Press");
				lcd_write(2,1, "\"Menu\" to skip");
				while(menu_pressed()) delay(50); //Wait for any previous button press to clear (written here so the display updates before debouncing)
				state_initialised=1;
			}
		
			if(digitalRead(GPS_PPS))
			{
				state_clean();
				#if SKIP_CALIBRATION
					state = ENCODING;
				#else
					state = CALIBRATING;
				#endif
				goto end;
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = LOCATOR;
				goto end;
			}
			
			break;
		}	
		
		case ENCODING:
		{
			switch (WSPR::encode(callsign, locator, power, symbols, WSPR_NORMAL))
			{
				case 0: 
				{
					state=HOME;
					state_clean();
					goto end;
				}
				case 21: 	if(WSPR::encode(callsign, locator,power,symbols, WSPR_EXTENDED) == 21)
							{
								state = HOME;
								state_clean();
								goto end;
							}
							else panic("Something went very wrong", 22); //Think this should be impossible as all errors should have already been tested for
							break;
				default: 	panic("Something went very wrong", 22); //Think this should be impossible as all errors should have already been tested for
							break;
			};
				
			
		}//end of ENCODING
		
		case CALIBRATING:
		{
			if(!state_initialised)
			{
				osc.set_freq(2,1,2500000.0);
				lcd_write(0,2, "Calibrating");
				lcd_write(1,1, "This will take");
				lcd_write(2,0, "about 160 second");
				T2CON = 0x00;
				T3CON = 0x00;
				TMR2 = 0x00;
				PR2 = 0xFFFFFFFF;
				T2CKR = PIN_A0;
				T2CON = (TIMER_ENABLED | NO_PRESCALER | MODE_32_BIT_TIMER | EXTERNAL_SOURCE);
				//attachInterrupt(GPS_PPS_INTERRUPT, check_frequency, RISING);
				state_initialised = 1;
				while(menu_pressed()) delay(50); //Wait for any previous button press to clear (written here so the display updates before debouncing)
			}
		
			static int i = 160;
			static int counter = 0;
			
			if(++counter > SECOND) //nicer way of doing it than using delay as state machines don't like blocking
			{
				if(i>0) i--;
				counter = 0;
				lcd.setCursor(2,6);
				if(i<100) lcd.print('0');
				if(i<10) lcd.print('0');
				lcd.print(i);
			}
		
			if(calibration_flag) //Calibration complete
			{
				if(substate == 0) //First reading is always wrong for some reason
				{
					calibration_value = 0;
					calibration_flag = 0;
					substate = 1;
				}
				else
				{
					static int error = 0;
					if(abs(200e6 - calibration_value) > 10e3)
					{
						if(++error == 3) panic("Invalid calbiration signal", 20);
						else
						{
							if(error == 1) lcd_write(0,0, "1st");
							else lcd_write(0,0, "2nd");
							lcd_write(0,3, " calibration ");
							lcd_write(1,0, "failed. Retry in");
							i=80;
							counter = 0;
						}
					}
					else
					{
						osc.resources[0] = calibration_value <<2;
						osc.resources[1] = calibration_value <<2;
						state_clean();
						maidenhead(gps, locator);
						state = ENCODING;
						goto end;
					}
					calibration_value = 0;
					calibration_flag = 0;
				}
			}
			
			break;
		}//end of CALIBRATING
			
		case HOME:
		{
			static String new_locator;
			static uint32_t gps_watchdog = 0;
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				lcd_write(0,0, callsign);
				lcd_write(0,10, locator);
				int seed = 0;
				for (int i = 0; i<10; i++) //not sure if something quite this complex is needed but hey!
				{
					if(callsign[i] == 0) break;
					seed += callsign[i];
				}
				randomSeed(seed);	
				
				if(band != BAND_OTHER)
				{
					lcd_write(1,0, band_strings[band]);
					tx_frequency = band_freq[band] + 1400 + random(20,180);
				}
				else
				{
					lcd_write(1,13, "!");
					lcd.setCursor(1,0);
					tx_frequency = frequency + 1400 + random(20,180);
					
					//frequency is abcdefgh Hz
					if(frequency_char[0] > '0') //frequency has 10s of MHz, show (ab)MHZ
					{
						lcd.print(frequency_char[0]);
						lcd.print(frequency_char[1]);
						lcd.print("MHz");
					}
					else if(frequency_char[1] > '0') //show (b.c)MHz
					{
						lcd.print(frequency_char[1]);
						lcd.print('.');
						lcd.print(frequency_char[2]);
						lcd.print("MHz");
					}
					else //show (cde)kHz
					{
						lcd.print(frequency_char[2]);
						lcd.print(frequency_char[3]);
						lcd.print(frequency_char[4]);
						lcd.print("kHz");
					}
				}
				
				lcd_write(1,14, "RX");
				lcd_write(1,7, watt_strings[power]);
				
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
				new_locator.reserve(6);
				uint8_t gps_symbol[7] = {14,27,17,27,14,14,4};
				lcd.createChar(0, gps_symbol);
				gps_watchdog = 0;
				state_initialised=1;
			}
			
			if(substate) //Either transmitting or in substate 165 (decided not to tx this frame)
			{
				static int old_substate = 0;
				if((gps.time.second() == 1) && (substate == 165)) //Reset substate so can try to TX next even minute
				{
					substate = 0;
					old_substate = 0;
					goto end;
				}
				
				if(substate != old_substate) //Move onto next tone
				{
					static int send_extended = 0;
					if(substate == 163) //Done
					{
						detachCoreTimerService(tx);
						osc.disable_clock(0);
						substate = 0;
						old_substate = 0;
						if(extended_mode) send_extended ^=1; //if we're using two packet messages, send the other one next time
						digitalWrite(LED,LOW);
						lcd_write(1,14,"RX");
						goto end;
					}
					else
					{
						if((extended_mode == 0) | (send_extended == 0)) osc.set_freq(0,0, tx_frequency + symbols[substate-1] * 375.0/256.0);
						else osc.set_freq(0,0, tx_frequency + symbols2[substate-1] * 375.0/256.0);
						old_substate = substate;
					}
				}
			}
			if(gps_enabled)
			{
				if(++gps_watchdog > (10*SECOND))
				{
					state_clean();
					state = UNLOCKED;
					goto end;
				}
				
				static bool gps_flag=0;
				if (digitalRead(GPS_PPS))
				{
					if(!gps_flag) 
					{
						lcd.setCursor(2,15);
						lcd.print('\0');
						gps_flag = 1;
					}
					gps_watchdog = 0;
				}
				else 
				{
					if(gps_flag)
					{
						lcd_write(2,15," ");
						gps_flag = 0;
					}
				}
				
				while(GPS.available())
				{
					gps.encode(GPS.read());
				}
				
				maidenhead(gps, new_locator);
				if(locator != new_locator)
				{
					locator = new_locator;
					lcd_write(0,10, locator);
				}
				
				static int old_time = 0;
				if(old_time != gps.time.value());
				{
					char temp[2];
					lcd.setCursor(2,9);
					sprintf(temp, "%02i", gps.time.hour());
					lcd.print(temp);
					lcd.print(':');
					sprintf(temp, "%02i", gps.time.minute());
					lcd.print(temp);
					
					if((gps.time.second() == 0) && ((gps.time.minute()%2) == 0) && (substate == 0)) //Start of new WSPR frame
					{
						int x = random(100);
						PC.print(x);
						PC.print(" ");
						PC.println(tx_percentage);
						if(x < tx_percentage) //TX next frame
						{
							digitalWrite(LED, HIGH);
							lcd_write(1,14, "TX");
							substate = 1;
							attachCoreTimerService(tx); 
							goto end;
						}
						else
						{
							substate = 165; //prevents WSPR frame decision happening more than once as this loop is much faster than once per second
						}
					}
				}
				
				static int old_date = 0;
				if(old_date != gps.date.value());
				{
					old_date = gps.date.value();
					char temp[2];
					lcd.setCursor(2,0);
					switch (date_format)
					{
						case BRITISH:
						{
							sprintf(temp, "%02i", gps.date.day());
							lcd.print(temp);
							lcd.print('/');
							sprintf(temp, "%02i", gps.date.month());
							lcd.print(temp);
							lcd.print('/');
							sprintf(temp, "%02i", gps.date.year()%100);
							lcd.print(temp);
							break;
						}
						case AMERICAN:
						{
							sprintf(temp, "%02i", gps.date.month());
							lcd.print(temp);
							lcd.print('/');
							sprintf(temp, "%02i", gps.date.day());
							lcd.print(temp);
							lcd.print('/');
							sprintf(temp, "%02i", gps.date.year()%100);
							lcd.print(temp);
							break;
						}
						case GLOBAL:
						{
							sprintf(temp, "%02i", gps.date.year()%100);
							lcd.print(temp);
							lcd.print('/');
							sprintf(temp, "%02i", gps.date.month());
							lcd.print(temp);
							lcd.print('/');
							sprintf(temp, "%02i", gps.date.day());
							lcd.print(temp);
							break;
						}
					};		
				}//end of updating date	
				
				break;
			}//end of GPS stuff
			
		}//end of HOME	
	} //end of state machine
end:;
} //end of loop





