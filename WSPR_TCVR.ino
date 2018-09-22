#include "WSPR_config.h"

Si5351 osc;
DogLcd lcd(21, 20, 24, 22); //I don't know why it's called that, not my library!
supervisor master; //God object

uint8_t gps_symbol[7] = {14,27,17,27,14,14,4}; //I would have made this const but threw type errors and had better things to do than edit someone else's library

uint8_t crossed_t[7] = {31,4,4,31,4,4,4};
uint8_t crossed_x[7] = {17,17,10,31,10,17,17};

const String callsignChar = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/ A"; //the extra A means the index can be incremented from '/', next time it searches for 'A' it will returns 0 not 37 as it loops from the starts

const int powerValues[] = {0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60};
const String powerStrings[] = {"1mW", "2mW", "5mW", "10mW", "20mW", "50mW", "100mW", "200mW", "500mW", "1W", "2W", "5W", "10W", "20W", "50W", "100W", "200W", "500W", "1kW"};
const String bandStrings[]  = {"2200m", "630m", "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"};

//State variable
enum state_t{START, IP, CALLSIGN, LOCATOR, POWER, POWER_WARNING, TX_PERCENTAGE, BAND, TX_DISABLED_QUESTION, DATE_FORMAT, WAITING_FOR_LOCK, CALIBRATING, HOME} state = START;
int substate = 0;
bool stateInitialised = 0, editingFlag = 0;
const uint32_t WSPR_TONE_DELAY = (uint32_t)(256000.0 * (double)CORE_TICK_RATE/375.0);

void setup()
{
	lcd.begin(DOG_LCD_M163, LCD_CONTRAST, DOG_LCD_VCC_5V);
	lcd.noCursor();
	register_lcd_for_panic(&lcd);
	
	//PC is an alias for the onboard USB COM port, defined in WSPR_config.h
	PC.begin(9600); 
	
	master.register_pi_uart(&RPI);
	master.register_gps_uart(&GPS);
	
	pinMode(GPS_PPS, INPUT);
	pinMode(MENU_BTN, INPUT);
	pinMode(EDIT_BTN, INPUT);
	pinMode(PI_WATCHDOG, INPUT);
	
	pinMode(LED, OUTPUT);
	digitalWrite(LED, LOW);
	pinMode(TX, OUTPUT);
	digitalWrite(TX, LOW);

	T2CON = 0x00;
	T3CON = 0x00;
	TMR2 = 0x00;
	PR2 = 0xFFFFFFFF;
	T2CKR = PIN_A0;
	T2CON = (TIMER_ENABLED | NO_PRESCALER | MODE_32_BIT_TIMER | EXTERNAL_SOURCE);
	attachInterrupt(GPS_PPS_INTERRUPT, pps_handler, RISING);
}

void state_clean() //clears anything that needs to be cleaned up before changing state
{
	lcd.clear();
	lcd.noBlink();
	lcd.noCursor();
	stateInitialised=0;
	editingFlag=0;
	substate=0;
}

bool menu_pressed() {return !digitalRead(MENU_BTN);}

bool edit_pressed() {return !digitalRead(EDIT_BTN);}

//Interrrupts
void heartbeat()
{
	master.heartbeat = 1;
	master.piSyncTime = millis();
}

uint32_t tx(uint32_t currentTime)
{
	//TODO: something here
	return currentTime + WSPR_TONE_DELAY;
}

void pps_handler()
{
	static int counter = 0;
	if(++counter == 80)
	{
		//master.sync(TMR2, supervisor::CALIBRATION); //DEBUG
		TMR2 = 0;
		counter = 0;
	}
	master.gpsSyncTime = millis();
}

//Main code
void loop()
{
	master.gps_handler();
	master.pi_handler();
	master.background_tasks();
	
	if (state != START && !master.settings().piActive)
	{
		PC.println("Server dead :(");
		if(state == HOME && substate > 0) //we are transmitting
		{
			detachCoreTimerService(tx); //stop transmitting
			osc.disable_clock(0);
		}
		state_clean();
		state = START;
	}
	
	static int tempPower; //Needs to be here as used in multiples states
	switch (state) 
	{
		case START: //Note this first state isn't quite the same as the others as it is blocking. No point doing anything until server started
		{
			static bool old_watchdog;
			if(!stateInitialised)
			{
				old_watchdog = digitalRead(PI_WATCHDOG);
				stateInitialised = 1;
			}
			lcd.write(0,2,"Waiting for");
			lcd.write(1,0, "server to start");
			lcd.setCursor(2,0);
			
			while(old_watchdog == digitalRead(PI_WATCHDOG)) //Wait until server starts i.e. watchdog pin changes
			{ 
				static int dotNum = 0;
				lcd.print(".");
				#ifdef DEBUG
					PC.println("Waiting for server to start");
				#endif
				digitalWrite(LED, dotNum&1);
				delay(1000);
				if(++dotNum > 15)
				{
					lcd.clear_line(2);
					lcd.setCursor(2,0);
					dotNum = 0;
				}
			}
			master.setup();
			#ifdef OSC_ENABLED
				osc.begin(XTAL_10pF, 25000000, osc.GPS_ENABLED);
			#endif 
			attachInterrupt(1, heartbeat, RISING); //INT1 is on RB14, will reset the watchdog timeout everytime the pin goes high.
			state_clean();
			digitalWrite(LED,LOW);
			state = IP;
			break;
		} //case START
					
		case IP:
		{
			static uint32_t ipRequestedTime;
			if(!stateInitialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				ipRequestedTime = millis();
				//Load stupid data to check when it's been updated
				//Can't wait for a signal as need to keep going around the loop so the uart handler is called
				master.sync("8.8.8.8", supervisor::IP);
				master.sync("deadbeef", supervisor::HOSTNAME); 
				master.clearUpdateFlag(supervisor::IP);
				master.clearUpdateFlag(supervisor::HOSTNAME);
				RPI.print("I;\n");
				RPI.print("H;\n");
				lcd.write(0,0, "IP and Hostname");
				master.sync("Displaying Connection Info", supervisor::STATUS);		
				stateInitialised=1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
			
			if((master.settings().ip == "8.8.8.8") && (millis() - ipRequestedTime > PI_TIMEOUT)) panic(PI_NOT_RESPONDING);
			
			if(master.updated(supervisor::IP) && master.updated(supervisor::HOSTNAME))
			{
				//We have got new connection information
				if(master.settings().ip == "")
				{
					//Pi is not connected to network
					lcd.write(1, 1, "No connection");
					lcd.write(2, 1, "Edit: Refresh");
				}
				else
				{
					lcd.write(1, 0, master.settings().ip);
					lcd.write(2, 0, master.settings().hostname);
				}
				master.clearUpdateFlag(supervisor::IP);
				master.clearUpdateFlag(supervisor::HOSTNAME);
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = CALLSIGN;
			}
			
			if(edit_pressed()) state_clean(); //Re-query IP Address
			break; 
		} //case IP

		case CALLSIGN:
		{
			static String tempCallsign = "";
			tempCallsign.reserve(10);
			
			if(!stateInitialised)
			{
				lcd.write(0,4, "Callsign");
				master.sync("Setting Callsign", supervisor::STATUS);		
				lcd.write(1,0, master.settings().callsign);
				master.clearUpdateFlag(supervisor::CALLSIGN);
				tempCallsign = master.settings().callsign;
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
			
			if(master.updated(supervisor::CALLSIGN)) //callsign has been updated from external source
			{
				master.clearUpdateFlag(supervisor::CALLSIGN);
				state_clean();
			}
			
			
			if(menu_pressed())
			{
				if(editingFlag)
				{
					//move onto editing next character
					if(++substate == 10) //max of 10 char is WSPR callsign
					{
						//Have finished editing
						editingFlag = 0;
						substate = 0;
						lcd.noBlink();
					}
					else
						lcd.setCursor(1, substate);
				}
				else
				{
					//Move onto next state
					if(tempCallsign.indexOf(' ') != -1)
					{
						lcd.write(2,0,"Space in call");
						delay(1000);
						lcd.clear_line(2);
						delay(1000);
						break;
					}
					else
					{
						String lcd_message;
						lcd_message.reserve(16);
						switch(WSPR_encode(tempCallsign, "AA00aa", 23, NULL, WSPR_NORMAL)) //Test callsign for compatibility with WSPR standard
						{
							case 0:	lcd_message = "";
									master.sync(tempCallsign, supervisor::CALLSIGN); 
									state = LOCATOR;
									break;
							case 1:  lcd_message = 	"Need 6 char loc in extended WSPR    Error 17    "; break;
							// case 2 got removed in rewrite
							case 3:  lcd_message = 	"Only 1 / allowed  in callsign.      Error 19    "; break;
							case 4:  lcd_message = 	"  Callsign is       too long        Error 20    "; break;
							case 5:  lcd_message = 	"  Invalid char     in suffix        Error 21    "; break;
							case 6:  lcd_message = 	"2 char suffixes  must be 10-99      Error 22    "; break;
							case 7:  lcd_message = 	"  Invalid char     in prefix        Error 23    "; break;
							case 8:  lcd_message = 	" Use of / char   is unsupported     Error 24    "; break;
							case 9:  lcd_message = 	"  Invalid char    in callsign       Error 25    "; break;
							case 10: lcd_message = 	"Invalid CallsignNeed num in 2/3     Error 26    "; break;
							case 11: lcd_message = 	"  Invalid Main      Callsign        Error 27    "; break;
							case 21: 	if(!WSPR_encode(tempCallsign, "AA00aa", 23, NULL, WSPR_EXTENDED))
										{
											master.sync(tempCallsign, supervisor::CALLSIGN); 
											state = LOCATOR;
											break;
										}
										//Deliberate fallthrough if encode returns an error
							default: lcd_message = 	"    No Idea!                        Error 28    "; break;
						
						};
						
						if(lcd_message != "") //there is something wrong with the callsign
						{
							lcd.clear();
							lcd.write(0,0, lcd_message);
							delay(3000);
							lcd.clear();
							lcd.write(0,4, "Callsign");
							lcd.write(1,0, tempCallsign);
							editingFlag = 0;
							substate = 0;
						}
					}
				}
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				if(!editingFlag)
				{
					//Start editing
					editingFlag = 1;
					substate = 0;
					lcd.setCursor(1,0);
					lcd.blink();
				}
				else
				{
					//Already editing
					while(tempCallsign.length() < substate) tempCallsign += "A"; //If editing char after callsign end, Pad with 'A's
					if(tempCallsign.length() == substate) tempCallsign += " "; //If padding, make last character a space so when incremented, becomes an A
					tempCallsign[substate] = callsignChar[callsignChar.indexOf(tempCallsign[substate])+1];	
					//Looks a bit nasty, just replaces current char with next value in callsignChar which contains all letters, numbers, space and /
					//Also nicely handles errors as indexOf(char_not_in_string) = -1 so this will replace with callsignChar[0] = 'A'
					tempCallsign.trim(); //Remove any white space at the end
					lcd.clear_line(1);
					lcd.write(1,0, tempCallsign);
					lcd.setCursor(1, substate);
				}
				while(edit_pressed()) delay(50);
			}
			break; 
		} //case CALLSIGN
		
		case LOCATOR:
		{
			if(!stateInitialised)
			{
				lcd.write(0,4, "Locator");
				master.sync("Setting Locator", supervisor::STATUS);		
				lcd.write(1,0, (master.settings().gpsEnabled ? "Set by GPS" : master.settings().locator));
				master.clearUpdateFlag(supervisor::LOCATOR);
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
			
			if(master.updated(supervisor::LOCATOR))
			{
				master.clearUpdateFlag(supervisor::LOCATOR);
				state_clean();
			}
			
			if(menu_pressed())
			{
				state_clean();
				state = POWER;
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				lcd.write(2,0, "No other options");
				delay(1000);
				lcd.clear_line(2);
				while(edit_pressed()) delay(50);
			}
			break;
		} //case LOCATOR
		
		case POWER:
		{
			if(!stateInitialised)
			{
				lcd.write(0,5, "Power");
				master.sync("Setting Power", supervisor::STATUS);		
				tempPower = master.settings().power;
				lcd.write(1,(tempPower < 10 ? 12 : 11), String(tempPower) + "dBm"); //Power in dBm right aligned
				lcd.write(1,0,powerStrings[((tempPower % 10)/3) + (3 * (tempPower / 10))]); //Little bit nasty using the /3. Due to integer divison converts 0 -> 0, 3->1, 7->2 which gives the index
				master.clearUpdateFlag(supervisor::POWER);
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
			
			if(master.updated(supervisor::POWER))
			{
				master.clearUpdateFlag(supervisor::POWER);
				state_clean();
			}
			
			if(menu_pressed())
			{
				state_clean();
				if(tempPower != master.settings().power)
					state = POWER_WARNING;
				else
					state = TX_PERCENTAGE;
				
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				int powerIndex = ((tempPower % 10)/3) + (3 * (tempPower / 10));
				powerIndex++;
				powerIndex %= 19; //19 possible power values
				tempPower = powerValues[powerIndex]; //Increase power to next index in powerValues
				lcd.clear_line(1);
				lcd.write(1,0,powerStrings[powerIndex]); 
				lcd.write(1,(tempPower < 10 ? 12 : 11), String(tempPower) + "dBm"); //Power in dBm right aligned
				while(edit_pressed()) delay(50);
			}
			break;
		} //case POWER
		
		case POWER_WARNING:
		{
			if(!stateInitialised)
			{
				lcd.write(0,0, "Changes reportedpower only. OK? Menu:Yes Edit:No");
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}

			if(menu_pressed())
			{
				state_clean();
				master.sync(tempPower, supervisor::POWER);
				state = TX_PERCENTAGE;
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				state_clean();
				state = POWER;
				while(edit_pressed()) delay(50);
			}
			
			break;
		} //case POWER_WARNING
		
		case TX_PERCENTAGE:
		{

			static int tempPercentage;
			if(!stateInitialised)
			{
				lcd.write(0, 1, "TX Percentage");
				master.sync("Setting TX Percentage", supervisor::STATUS);
				tempPercentage = master.settings().txPercentage;
				lcd.write(1,0, String(tempPercentage) +"%");
				stateInitialised = 1;

				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50); 
			}
			
			if(master.updated(supervisor::TX_PERCENTAGE))
			{
				master.clearUpdateFlag(supervisor::TX_PERCENTAGE);
				state_clean();
			}
			
			if(menu_pressed())
			{
				master.sync(tempPercentage, supervisor::TX_PERCENTAGE);
				state_clean();
				state = BAND;
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				tempPercentage += 10;
				tempPercentage %= 110; //Has to be 110 to allow 100% as an option
				lcd.clear_line(1);
				lcd.write(1,0, String(tempPercentage) + "%");
				while(edit_pressed()) delay(50);
			}
			break;
		} //case TX_PERCENTAGE
		
		case BAND:
		{
			static int tempBand;
			if(!stateInitialised)
			{
				master.sync("Setting Band", supervisor::STATUS);
				tempBand = master.settings().band;
				lcd.write(0,6, "Band");
				lcd.write(1,0, (master.settings().bandhop ? "Bandhop" : bandStrings[tempBand]));
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50); 
			}
			
			if(master.updated(supervisor::BAND))
			{
				master.clearUpdateFlag(supervisor::BAND);
				state_clean();
			}
			
			if(menu_pressed())
			{
				if(editingFlag) //Only sync if we have changed bands using buttons i.e. band doesn't change with time
				{
					int tempBandArray[24];
					for (int i = 0; i<24; i++)
						tempBandArray[i] = tempBand;
					master.sync(tempBandArray, supervisor::BAND);
					state_clean();
					state = TX_DISABLED_QUESTION;
				}	
				else
				{
					state_clean();
					state = DATE_FORMAT;
				}
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				editingFlag = 1;
				tempBand++;
				tempBand %= 12;
				lcd.clear_line(1);
				lcd.write(1,0, bandStrings[tempBand]);
				while(edit_pressed()) delay(50);	
			}
			
			break;
		} //case BAND
		
		case TX_DISABLED_QUESTION:
		{
			if(!stateInitialised)
			{
				lcd.write(0,2, "Enable TX on    chosen band?  Menu:Yes Edit:No");
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}

			if(menu_pressed())
			{
				state_clean();
				int txDisableArray[12] = {0,0,0,0,0,0,0,0,0,0,0,0}; //Enable TX on all bands
				master.sync(txDisableArray, supervisor::TX_DISABLE);
				state = DATE_FORMAT;
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{	
				state_clean();
				int txDisableArray[12] = {1,1,1,1,1,1,1,1,1,1,1,1}; //Disable TX on all bands
				master.sync(txDisableArray, supervisor::TX_DISABLE);
				state = DATE_FORMAT;
				while(edit_pressed()) delay(50);
			}
			
			break;
		} //case TX_DISABLED_QUESTION
		
		case DATE_FORMAT:
		{
			static supervisor::dateFormat_t tempDateFormat;
			const String dateFormatStrings[] = {"DD/MM/YYYY", "MM/DD/YYYY", "YYYY/MM/DD"};
			if(!stateInitialised)
			{
				tempDateFormat = master.settings().dateFormat;
				master.sync("Setting Date Format", supervisor::STATUS);
				lcd.write(0,2, "Date Format");
				lcd.write(1,0, dateFormatStrings[tempDateFormat]);
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
			
			if(master.updated(supervisor::DATE_FORMAT))
			{
				master.clearUpdateFlag(supervisor::DATE_FORMAT);
				state_clean();
			}
			
			if(menu_pressed())
			{
				master.sync(tempDateFormat, supervisor::DATE_FORMAT);
				state_clean();
				state = WAITING_FOR_LOCK;
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				tempDateFormat = (supervisor::dateFormat_t)((((int) tempDateFormat) + 1) % 3);
				lcd.clear_line(1);
				lcd.write(1,0, dateFormatStrings[tempDateFormat]);
				while(edit_pressed()) delay(50);
			}
			
			break;
		} //case DATE_FORMAT
		
		case WAITING_FOR_LOCK:
		{
			if(!stateInitialised)
			{
				lcd.write(0,3, "Waiting for      GPS Lock       Edit: Back");
				master.sync("Waiting for GPS Lock", supervisor::STATUS);
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
		
			if(edit_pressed())
			{	
				state_clean();
				state = LOCATOR;
				while(edit_pressed()) delay(50);
			}
			
			if(digitalRead(GPS_PPS))
			{
				state_clean();
				state = CALIBRATING;
			}
			
			break;
		} //case WAITING_FOR_LOCK
		
		case CALIBRATING:
		{
			static int i = 160;
			if(!stateInitialised)
			{
				i=160;
				master.sync("Calibrating - roughly 160 seconds remaining", supervisor::STATUS);
				osc.set_freq(2,1,2500000.0);
				lcd.write(0,2, "Calibrating");
				lcd.write(1,1, "This will take");
				lcd.write(2,1, "about 160 sec");
				master.clearUpdateFlag(supervisor::TIME);
				stateInitialised = 1;
				while(menu_pressed()) delay(50); 
				while(edit_pressed()) delay(50);
			}
			
			if(master.updated(supervisor::TIME))
			{
				master.clearUpdateFlag(supervisor::TIME);
				i--;
				lcd.clear_line(2);
				lcd.write(2, 1, "about " + String(i) + " sec");
				master.sync("Calibrating - roughly " + String(i) + " seconds remaining", supervisor::STATUS);
			}
			
			
			
			break;
		} //case CALIBRATING
		default: panic(INVALID_STATE_ACCESSED, String(state)); break;
					
	}; //end of state machine

} //end of loop




