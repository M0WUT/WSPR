#include "WSPR_config.h"

Si5351 osc;
DogLcd lcd(21, 20, 24, 22); //I don't know why it's called that, not my library!
supervisor master;

uint8_t gps_symbol[7] = {14,27,17,27,14,14,4}; //I would have made this const but threw type errors and had better things to do than edit someone else's library

uint8_t crossed_t[7] = {31,4,4,31,4,4,4};
uint8_t crossed_x[7] = {17,17,10,31,10,17,17};

const String callsignChar = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789/ A"; //the extra A means the index can be incremented from '/', next time it searches for 'A' it will returns 0 not 37 as it loops from the starts

//State variable
enum state_t{START, UNCONFIGURED, UNLOCKED, HOME, PANIC, CALLSIGN, CALLSIGN_CHECK, EXTENDED_CHECK, LOCATOR, LOCATOR_CHECK, POWER, POWER_WARNING, POWER_QUESTION, TX_PERCENTAGE, IP, BAND, OTHER_BAND_WARNING, ENCODING, DATE_FORMAT, CALIBRATING, TX_DISABLED_QUESTION} state = START;
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
				if(++dotNum >16)
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
				while(menu_pressed()) delay(50);
				while(edit_pressed())delay(50);
				stateInitialised=1;
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
				while(menu_pressed())delay(50);
				while(edit_pressed())delay(50);
				master.sync("Setting Callsign", supervisor::STATUS);		
				lcd.write(1,0, master.settings().callsign);
				master.clearUpdateFlag(supervisor::CALLSIGN);
				tempCallsign = master.settings().callsign;
				stateInitialised = 1;
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
						switch(WSPR::encode(tempCallsign, "aa00aa", 23, NULL, WSPR_NORMAL))
						{
							//TODO add checking to callsign
							default: break;
						};
					}
				}
				while(menu_pressed())delay(50);
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
					while(tempCallsign.length() < substate) tempCallsign += "A"; //If editing char after callsign end, Pad with 'A's
					if(tempCallsign.length() == substate) tempCallsign += " "; //If padding, make last character a space so when incremented, becomes an A
					//Already editing
					tempCallsign[substate] = callsignChar[callsignChar.indexOf(tempCallsign[substate])+1];	
					//Looks a bit nasty, just replaces current char with next value in callsignChar which contains all letters, numbers, space and /
					//Also nicely handles errors as indexOf(char_not_in_string) = -1 so this will replace with callsignChar[0] = 'A'
					tempCallsign.trim(); //Remove any white space at the end
					lcd.clear_line(1);
					lcd.write(1,0, tempCallsign);
					lcd.setCursor(1, substate);
				}
				while(edit_pressed())delay(50);
			}
			break; 
		} //case CALLSIGN
		default: panic(INVALID_STATE_ACCESSED, state); break;
					


	}; //end of state machine

} //end of loop




