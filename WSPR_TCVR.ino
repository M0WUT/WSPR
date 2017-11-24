#include "WSPR_config.h"

const String VERSION = "0.1"; //Please don't touch this, everything will be fine but you might make the auto-updater unhappy

String callsign="M0WUT";
String locator="AA00aa";

int tx_percentage = 20;

char symbols[162]; //Used to store encoded WSPR symbols
char symbols2[162]; //Used to store more WSPR symbols in case of extended mode

Si5351 osc;
TinyGPSPlus gps;
DogLcd lcd(21, 20, 24, 22); //I don't know why it's called that, not my library!
LC640 eeprom(EEPROM_CS);
bool calibration_flag, pi_rx_flag, state_initialised=0, editing_flag=0, warning = 0, gps_enabled=1, extended_mode = 0, valid_ip = 0; //flags used to indicate to the main loop that an interrupt driven event has completed
uint32_t calibration_value; //Contains the number of pulses from 2.5MHz ouput of Si5351 in 80 seconds (should be 200e6) 
int substate=0;
uint32_t watchdog_counter = 0; //Used to detect server timeout (i.e. server crash detection!)
uint8_t gps_symbol[7] = {14,27,17,27,14,14,4}; //I would have made this const but threw type errors and had better things to do than edit someone else's library

//State variable
enum menu_state{START, UNCONFIGURED, UNLOCKED, HOME, PANIC, CALLSIGN, CALLSIGN_CHECK, EXTENDED_CHECK, LOCATOR, LOCATOR_CHECK, POWER, POWER_WARNING, POWER_QUESTION, TX_PERCENTAGE, IP, BAND, OTHER_BAND_WARNING, ENCODING, DATE_FORMAT, CALIBRATING};

//Power related stuff
enum power_t{dbm0, dbm3, dbm7, dbm10, dbm13, dbm17, dbm20, dbm23, dbm27, dbm30, dbm33, dbm37, dbm40, dbm43, dbm47, dbm50, dbm53, dbm57, dbm60};
const String dbm_strings[] = {"0", "3", "7", "10", "13", "17", "20", "23", "27", "30", "33", "37", "40", "43", "47", "50", "53", "57", "60"};
const String watt_strings[] = {"1mW", "2mW", "5mW", "10mW", "20mW", "50mW", "100mW", "200mW", "500mW", "1W", "2W", "5W", "10W", "20W", "50W", "100W", "200W", "500W", "1kW"};

//Band related stuff
enum band_t{BAND_2200, BAND_630, BAND_160, BAND_80, BAND_60, BAND_40, BAND_30, BAND_20, BAND_17, BAND_15, BAND_12, BAND_10, BAND_HOP};
const String band_strings[] = {"2200m", "630m", "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"}; 
const double band_freq[] = {136000.0, 474200.0, 1836600.0, 3592600.0, 5287200.0, 7038600.0, 10138700.0, 14095600.0, 18104600.0, 21094600.0, 24924600.0, 28124600.0};

//Date related stuff
enum date_t {BRITISH, AMERICAN, GLOBAL};								
const String date_strings[] = {"DD/MM/YY", "MM/DD/YY", "YY/MM/DD"};

const uint32_t wspr_tone_delay = (uint32_t)(256000.0 * (double)CORE_TICK_RATE/375.0);

const char letters[] = {'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
						'0','1','2','3','4','5','6','7','8','9','/',' ','A'}; //the extra A means the index can be incremented from '/', next time it searches for 'A' it will returns 0 not 37 as it loops from the starts
const String blank_line = "                "; //Very important to allow partial clearing of the LCD
String old_callsign = "";
String old_locator = "";
menu_state state = START;
String ip_address = "0.0.0.0";
String hostname = "deadbeef";
power_t power = dbm23;
power_t old_power;
band_t band_array[24];
band_t old_band_array[24];
band_t band = BAND_20;
date_t date_format = BRITISH;
date_t old_date_format;
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
	pinMode(PI_WATCHDOG, INPUT);
	digitalWrite(LED, LOW);

	//osc.begin(XTAL_10pF, 25000000,GPS_ENABLED);
	
	callsign.reserve(11);
	old_callsign.reserve(11);
	locator.reserve(7);
	old_locator.reserve(7);

}

void lcd_write(int row, int col, String data)
{
	lcd.setCursor(row, col);
	lcd.print(data);
}

bool constant_band_check()
{
	//Checks if all elements in band array are the same (i.e. single band operation)
	for(int i=0; i<23; i++)
		if(band_array [i] != band_array[i+1])
			return 0;
	return 1;
}

bool band_array_changed()
{
	//Checks if any elements in band_array have been changed
	for(int i=0; i<24; i++)
		if(band_array [i] != old_band_array[i])
			return 1;
	return 0;
}

bool unfiltered_band()
{
	//Checks if any elements in band_array do not have a BPF
	for(int i=0; i<24; i++)
		if(band_array [i]  == BAND_2200 || band_array[i] == BAND_630)
			return 1;
	return 0;
}


int letters_find(char x) //returns the index of char x in the array "letters"
{
	for (int i=0; i<39; i++)
		if(letters[i]==x) return i;
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
	return !digitalRead(MENU_BTN); //done in function to save me remembering active high / low
}

bool edit_pressed()
{
	return !digitalRead(EDIT_BTN);
}

void clear_watchdog()
{
	watchdog_counter = 0;
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
			osc.plla_frequency = calibration_value <<2;
			osc.pllb_frequency = calibration_value <<2;
		}
		calibration_value = 0;
		calibration_flag = 0;
	}
	
	if (state != START)
	{
		if(++watchdog_counter > TIMEOUT) //Server has died
		{
			PC.println("Server dead :(");
			if(state == HOME && substate > 0) //we are transmitting
			{
				detachCoreTimerService(tx); //stop transmitting
				osc.disable_clock(0);
			}
			state_clean();
			state = START;
			goto end;
		}
		
	}
	switch (state) //going to attempt to implement as a state machine, sure M0IKY will have some complaints to make 
	{
		case START: //Note this first state isn't quite the same as the others as it is blocking. This is becuase the UART doesn't need checking if the server is not running
		{
			lcd_write(0,2,"Waiting for");
			lcd_write(1,0, "server to start");
			bool old_watchdog = digitalRead(PI_WATCHDOG);
			eeprom.read(EEPROM_CALLSIGN_BASE_ADDRESS);
			if(eeprom.read(EEPROM_CALLSIGN_BASE_ADDRESS) == 0) state = UNCONFIGURED; //No valid data in EEPROM
			else
			{	
				//Load everything from EEPROM
				
				//Load callsign
				callsign = "";
				for(int i = 0; i < 10; i++)
				{
					char x = eeprom.read(EEPROM_CALLSIGN_BASE_ADDRESS + i);
					if(x == 0) break;
					else callsign += x;
				}
				
				//Load locator
				locator = "";
				for(int i = 0; i < 6; i++)
				{
					char x = eeprom.read(EEPROM_LOCATOR_BASE_ADDRESS + i);
					if(x == 0) break;
					else locator += x;
				}
				if(locator == "GPS")
				{
					gps_enabled = 1;
					locator = "AA00aa";
				}
				else gps_enabled = 0;
				
				power = (power_t)eeprom.read(EEPROM_POWER_ADDRESS);
				
				tx_percentage = 10 * eeprom.read(EEPROM_TX_PERCENTAGE_ADDRESS);
				
				date_format = (date_t) eeprom.read(EEPROM_DATE_FORMAT_ADDRESS);
				
				for(int i = 0; i<24; i++)
					band_array[i] = (band_t) eeprom.read(EEPROM_BAND_BASE_ADDRESS + i);
				
				state = CALLSIGN;
			}
			
			while(old_watchdog == digitalRead(PI_WATCHDOG)) //Wait until server starts i.e. watchdog pin changes
			{ 
				static int dot_num = 0;
				lcd.setCursor(2,0);
				for (int i =0; i< dot_num; i++)
				{
					lcd.print(".");
				}
				dot_num++;
				dot_num %= 17;
				PC.println(dot_num);
				digitalWrite(LED, dot_num%2); //Flash LED
				delay(1000);
				lcd_write(2,0,blank_line);
			}
			attachInterrupt(1, clear_watchdog, RISING); //INT1 is on RB14, will reset the watchdog timeout everytime the pin goes high.
			state_clean();
			digitalWrite(LED,LOW);
			goto end;
			
			
			break;
		} 
		
		case UNCONFIGURED:
		{
			if(!state_initialised)
			{
				RPI.print("SUnconfigured;\n");
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
				old_callsign = callsign; //Used to only send update to Pi if callsign is changed
				RPI.print("SInputting Callsign;\n");
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
					if(old_callsign != callsign)
					{
						//Update server
						RPI.print("C"+callsign+";\n");
						//and save to EEPROM
						for (int i = 0; i< 10; i++)
						{
							eeprom.write(EEPROM_CALLSIGN_BASE_ADDRESS + i, (i<callsign.length() ? callsign[i] : 0));
						}
					}
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
				old_locator = gps_enabled ? "GPS" : locator;
				RPI.print("SInputting Locator;\n");
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
					if(++counter>(2*SECOND))
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
				if(old_locator != "GPS")
				{
					//Update server
					RPI.print("LGPS;\n");
					//and save to EEPROM
					for (int i = 0; i< 6; i++)
					{
						eeprom.write(EEPROM_LOCATOR_BASE_ADDRESS + i, (i<locator.length() ? locator[i] : 0));
					}
				}
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
			if(old_locator != locator)
			{
				RPI.print("L"+locator+";\n");
				for (int i = 0; i< 6; i++)
				{
					eeprom.write(EEPROM_LOCATOR_BASE_ADDRESS + i, (i<locator.length() ? locator[i] : 0));
				}
			}
			state = LOCATOR;
			goto end;
		}//end of LOCATOR_CHECK
		
		case POWER:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				old_power = power;
				RPI.print("SInputting Power;\n");
				lcd_write(0,5, "Power");
				lcd_write(1,0, watt_strings[power]);
				lcd_write(1, 13-(dbm_strings[power].length()), dbm_strings[power]+"dBm");
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
				lcd_write(1, 13-(dbm_strings[power].length()), dbm_strings[power]+"dBm");
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
		
			if (menu_pressed())
			{
				state_clean();
				if(old_power != power)
				{
					RPI.print("P"+dbm_strings[power]+";\n");
					eeprom.write(EEPROM_POWER_ADDRESS, power);	
				}
				state= TX_PERCENTAGE;
				goto end;
			}
			
			if (edit_pressed())
			{
				if(power != dbm60) power = (power_t)((int)power+1);
				else power=dbm0;
				lcd_write(1,0,blank_line);
				lcd_write(1,0, watt_strings[power]);
				lcd_write(1, 13-(dbm_strings[power].length()), dbm_strings[power]+"dBm");
				while(edit_pressed())delay(50);
			}
			
			break;
		}//end of POWER_QUESTION
		
		case TX_PERCENTAGE:
		{
			static int old_tx_percentage;
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				old_tx_percentage = tx_percentage;
				RPI.print("SSetting TX Percentage;\n");
				lcd_write(0,1, "TX Percentage");
				lcd_write(1,0, ((String)tx_percentage+"%"));
				while(menu_pressed()) delay(50);
				while(edit_pressed())delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				if(old_tx_percentage != tx_percentage)
				{
					RPI.print("X");
					RPI.print(tx_percentage);
					RPI.print(";\n");
					eeprom.write(EEPROM_TX_PERCENTAGE_ADDRESS, tx_percentage / 10); //Can only be multiple of 10
				}
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
				valid_ip = 0;
				ip_address = "0.0.0.0";
				hostname = "deadbeef";
				RPI.print("I;\n");
				RPI.print("H;\n");
				//Ensure that this screen always shows live data by clearing IP and hostname and re-requesting them
				lcd_write(0,0, "IP and Hostname");
				RPI.print("SDisplaying connection info;\n");
				
				while(menu_pressed()) delay(50);
				while(edit_pressed())delay(50);
				state_initialised=1;
			}
			
			if(ip_address == "0.0.0.0" || hostname == "deadbeef") //Wait for defaults to be overwritten
			{
				for(int counter =0; !RPI.available(); counter++)
				{
					if(counter > TIMEOUT) panic("Pi not responding", 19);
				}
				goto end;
			}
			if(!valid_ip)
			{
				lcd_write(1,0, ip_address);
				lcd_write(2,0, hostname);
				valid_ip = 1;
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
			static band_t temp_band = BAND_2200;
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				for(int i = 0; i<24; i++) //Copy band array to old band array
					old_band_array[i] = band_array[i];
				
				RPI.print("SSetting band;\n");
				lcd_write(0,6, "Band");
				if(constant_band_check())
				{
					lcd_write(1,0, band_strings[band_array[0]]);
					temp_band = band_array[0];
				}
				else
				{
					lcd_write(1,0,"Bandhop");
					temp_band = BAND_HOP;
				}
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = OTHER_BAND_WARNING;
				if(temp_band != BAND_HOP)
					for (int i =0; i<24; i++) band_array[i] = temp_band;
				if(band_array_changed())
				{	//update server
					RPI.print('B');
					for(int i =0; i<23; i++)
					{
						RPI.print(band_array[i]);
						RPI.print(',');
						eeprom.write(EEPROM_BAND_BASE_ADDRESS + i, band_array[i]);
					}
					RPI.print(band_array[23]);
					RPI.print(";\n");
				}
				goto end;	
			}
			
			if(edit_pressed())
			{
				switch (temp_band)
				{
					BAND_HOP: 	//deliberate fallthrough
					BAND_10:	temp_band = BAND_2200; break;
					default:	temp_band = (band_t)((int)temp_band+1);
				};
				
				lcd_write(1,0,blank_line);
				lcd_write(1,0, band_strings[temp_band]);
				while(edit_pressed())delay(50);
			}
			
			break;
		}//end of BAND
		
		case OTHER_BAND_WARNING:
		{
			if(!unfiltered_band())
			{
				state_clean();
				state = DATE_FORMAT;
				goto end;
			}

			if(!state_initialised)
			{
				lcd_write(0,1, "No filters for");
				lcd_write(1,1, "a chosen band!");	
				lcd_write(2,2, "Press \"Menu\"");	
				state_initialised = 1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = DATE_FORMAT;
				goto end;
			}
			
		}//end of OTHER_BAND_WARNING
		
		case DATE_FORMAT:
		{
			if(!state_initialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				old_date_format = date_format;
				RPI.print("SSetting date format;\n");
				lcd_write(0,2, "Date Format");
				lcd_write(1,0, date_strings[date_format]);
				while(menu_pressed()) delay(50);
				state_initialised=1;
			}
			
			if(menu_pressed())
			{
				state_clean();
				if(old_date_format != date_format)
					eeprom.write(EEPROM_DATE_FORMAT_ADDRESS, date_format);
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
				RPI.print("SWaiting for GPS lock;\n");
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
				case 21: 	if(WSPR::encode(callsign, locator,power,symbols, WSPR_EXTENDED) == 0)
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
				RPI.print("SCalibrating - roughly 160 seconds remaining;\n");
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
				RPI.print("SCalibrating - roughly ");
				RPI.print(i);
				RPI.print(" seconds remaining;\n");
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
						if(++error == 3) panic("Invalid calibration signal", 20);
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
						osc.plla_frequency = calibration_value <<2;
						osc.pllb_frequency = calibration_value <<2;
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
			String freq_string = "";
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
				lcd_write(1,0, band_strings[band]);
				tx_frequency = band_freq[band] + 1400 + random(20,180);
				
				RPI.print("SRX - " + band_strings[band] +";\n");
				lcd_write(1,14, "RX");
				lcd_write(1,7, watt_strings[power]);
				
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
				new_locator.reserve(6);
				
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
						RPI.print("SRX - " + freq_string +";\n");
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
							RPI.print("STX - " + freq_string +";\n");
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
end:
	//Deals with Pi communications
	//Not a huge issue that this is blocking as successful messages will still be very quick relative to human
	//any timing critical stuff (like tx tone timing) is interrupt driven and the timeout will be triggered if messages are not complete
	
	if(RPI.available())
	{
		if(state == HOME && substate != 0 && substate != 165) //We are currently transmitting
		{
			detachCoreTimerService(tx); //Stop the transmission, who cares if we stop mid transmission as we are probably changing settings
			osc.disable_clock(0);
			substate = 0;
		}
		String rx_string = "";
		rx_string.reserve(20);
		char x = RPI.read();
		while(x != '\n')
		{
			rx_string += x;
			for(int counter = 0; !RPI.available(); ++counter)
				if(counter > TIMEOUT) panic("Pi not responding", 18);
			x = RPI.read();	
		}
		
		int rx_string_length = rx_string.length();
		int terminator_index = rx_string.indexOf(';');
		if(rx_string_length != (terminator_index +1)) panic("Command not ; terminated"); //This also handles no ; present as indexOf returns -1 if not found and length != 0
		
		PC.print(rx_string);
		if(rx_string_length == 2) //We are being requested data from
		{
			switch(rx_string[0])
			{
				case 'C': 	RPI.print("C"+callsign+";\n"); break; 
				case 'L': 	if(gps_enabled) RPI.print("LGPS;\n"); 
							else RPI.print("L"+locator+";\n"); 
							break;
				case 'P': 	RPI.print("P"+dbm_strings[power]+";\n"); break;
				case 'B':	RPI.print('B');
							for(int i =0; i<23; i++)
							{
								RPI.print(band_array[i]);
								RPI.print(',');
							}
							RPI.print(band_array[23]);
							RPI.print(";\n");
							break;	
							
				case 'X':  	RPI.print("X");
							RPI.print(tx_percentage);
							RPI.print(";\n");
							break;
				case 'T':	if(!gps_enabled) panic("PI requesting time and GPS is disabled");
							RPI.print("T");
							RPI.print(gps.date.day());
							RPI.print("/");
							RPI.print(gps.date.month());
							RPI.print("/");
							RPI.print(gps.date.year());
							RPI.print(" ");
							RPI.print(gps.time.hour());
							RPI.print(":");
							RPI.print(gps.time.minute());
							RPI.print(":");
							RPI.print(gps.time.second());
							RPI.print(";\n");
							break;
				case 'S':	RPI.print("SHello world :);\n"); break; //TODO, this needs to be handled better
				case 'V': 	RPI.print("V"+VERSION+";\n");
				default: panic("Received unknown character from Pi" + rx_string, 19);
			};
		}
		else //We are setting data
		{
			String data = rx_string.substring(1, terminator_index); //strip off command stuff
			switch (rx_string[0])
			{
				case 'C': 	callsign = data;
							for(int i = 0; i < 10; i++)
								eeprom.write(EEPROM_CALLSIGN_BASE_ADDRESS + i, (i<data.length() ? data[i] : 0));
							break;
				case 'I': 	ip_address = data; break;
				case 'H': 	hostname = data; break;
				case 'L': 	if(data == "GPS") gps_enabled = 1;
							else
							{
								gps_enabled = 0;
								locator = data; 
							}
							for(int i = 0; i < 6; i++)
							{
								eeprom.write(EEPROM_LOCATOR_BASE_ADDRESS + i, (i<data.length() ? data[i] : 0));
							}
							break;
				case 'P': 	for (int i =0; i< 19; i++)
							{
								if(dbm_strings[i] == data)
								{
									power = (power_t)i;
									eeprom.write(EEPROM_POWER_ADDRESS, i);
									goto actual_end;
								}
							}
							panic("Invalid power specified");
							break;
				case 'B': 	for (int i = 0; i<24; i++)
							{
								int x = data[2*i] - '0'; //2*i as comma seperated, then converted to int
								if (x>=0 and x <12)
								{
									band_array[i] = (band_t)x;
									eeprom.write(EEPROM_BAND_BASE_ADDRESS + i, x);
								}
								else panic("Invalid band supplied");
							}
							break;
				case 'X':	tx_percentage = atoi(data.c_str());
							eeprom.write(EEPROM_TX_PERCENTAGE_ADDRESS, tx_percentage / 10);
							break;
				default: 	panic("Unexpected char received from Pi", 19);
			};	
			if(state != IP) state_initialised = 0; //Re-initialise the state in case information has changed
		}
	}
actual_end:;
} //end of loop





