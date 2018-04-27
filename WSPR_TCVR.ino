#include "WSPR_config.h"

Si5351 osc;
DogLcd lcd(21, 20, 24, 22); //I don't know why it's called that, not my library!
supervisor master;

uint8_t gps_symbol[7] = {14,27,17,27,14,14,4}; //I would have made this const but threw type errors and had better things to do than edit someone else's library

uint8_t crossed_t[7] = {31,4,4,31,4,4,4};
uint8_t crossed_x[7] = {17,17,10,31,10,17,17};

const String callsignChar = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/ A"; //the extra A means the index can be incremented from '/', next time it searches for 'A' it will returns 0 not 37 as it loops from the starts

//State variable
enum state_t{START, IP, CALLSIGN, LOCATOR, POWER, TX_PERCENTAGE, BAND, DATE_FORMAT, WAITING_FOR_LOCK, CALIBRATING, HOME} state = START;
int substate = 0;
bool stateInitialised = 0, editingFlag = 0;
const uint32_t WSPR_TONE_DELAY = (uint32_t)(256000.0 * (double)CORE_TICK_RATE/375.0);

void setup()
{
	lcd.begin(DOG_LCD_M163, LCD_CONTRAST, DOG_LCD_VCC_5V);
	lcd.noCursor();
	register_lcd_for_panic(&lcd);
	
	//RPI, PC and GPS are alternative names for the UART ports, defined in WSPR_config.h
	RPI.begin(115200);
	PC.begin(9600); 
	GPS.begin(9600);
	
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

	#ifdef OSC_ENABLED
		osc.begin(XTAL_10pF, 25000000,GPS_ENABLED);
	#endif
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
				master.sync("8.8.8.8", supervisor::IP);
				master.sync("deadbeef", supervisor::HOSTNAME); //Load stupid data into master to check when it's updated
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
			
			if(master.updated(supervisor::CALLSIGN) && master.updated(supervisor::HOSTNAME))
			{
				//We have got new connection information
				if(master.settings().ip == "")
				{
					//Pi is not connected to network
					lcd.write(1, 3, "No network");
					lcd.write(2, 3, "connection");
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
				lcd.clear_line(1);
				lcd.write(1,0, master.settings().callsign);
				master.clearUpdateFlag(supervisor::CALLSIGN);
				//overwrite any unsaved changes and stop editing
				tempCallsign = master.settings().callsign;
				editingFlag = 0;
				substate = 0;
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
						switch(WSPR::encode(tempCallsign, "AA00aa", 23, NULL, WSPR_NORMAL)) //Test callsign for compatibility with WSPR standard
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
							case 21: 	if(!WSPR::encode(tempCallsign, "AA00aa", 23, NULL, WSPR_EXTENDED))
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
				lcd.clear_line(1);
				lcd.write(1,0, (master.settings().gpsEnabled ? "Set by GPS" : master.settings().locator));
				master.clearUpdateFlag(supervisor::LOCATOR);
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
			int tempPower;
			const int powerValues[] = {0,3,7,10,13,17,20,23,27,30,33,37,40,43,47,50,53,57,60};
			const String powerStrings[] = {"1mW", "2mW", "5mW", "10mW", "20mW", "50mW", "100mW", "200mW", "500mW", "1W", "2W", "5W", "10W", "20W", "50W", "100W", "200W", "500W", "1kW"};
			if(!stateInitialised)
			{
				lcd.write(0,5, "Power");
				master.sync("Setting Power", supervisor::STATUS);		
				tempPower = master.settings().power;
				lcd.write(1,(tempPower < 10 ? 12 : 11), String(tempPower) + "dBm"); //Power in dBm right aligned
				lcd.write(1,0,powerStrings[(tempPower % 10) + (3 * (tempPower / 10))]); 
				master.clearUpdateFlag(supervisor::POWER);
				stateInitialised = 1;
				while(menu_pressed()) delay(50);
				while(edit_pressed()) delay(50);
			}
			
			if(master.updated(supervisor::POWER))
			{
				tempPower = master.settings().power;
				lcd.clear_line(1);
				lcd.write(1,0,powerStrings[(tempPower % 10) + (3 * (tempPower / 10))]); 
				lcd.write(1,(tempPower < 10 ? 12 : 11), String(tempPower) + "dBm"); //Power in dBm right aligned
				master.clearUpdateFlag(supervisor::POWER);
			}
			
			if(menu_pressed())
			{
				state_clean();
				if(tempPower != master.settings().power)
				{
					lcd.clear();
					lcd.write(0,2, "Only changes");
					lcd.write(1,1, "reported power");
					lcd.write(2,0, "Menu:OK Edit:"); //TODO
				}
				state = TX_PERCENTAGE;
				while(menu_pressed()) delay(50);
			}
			
			if(edit_pressed())
			{
				int powerIndex = ((tempPower % 10) + (3 * (tempPower / 10)) + 1) % (sizeof(powerValues) / sizeof(powerValues[0]));
				tempPower = powerValues[powerIndex]; //Increase power to next index in powerValues, also handles errors as indexOf returns -1 if not found
				lcd.clear_line(1);
				lcd.write(1,0,powerStrings[powerIndex]); 
				lcd.write(1,(tempPower < 10 ? 12 : 11), String(tempPower) + "dBm"); //Power in dBm right aligned
				while(edit_pressed()) delay(50);
			}
				
			break;
		} //case POWER
		
		
		default: panic(INVALID_STATE_ACCESSED, String(state)); break;
					


	}; //end of state machine

} //end of loop




