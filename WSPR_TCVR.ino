#include "WSPR_config.h"

Si5351 osc;
TinyGPSPlus gps;
DogLcd lcd(21, 20, 24, 22); //I don't know why it's called that, not my library!
supervisor master;
 
uint32_t calibration_value; //Contains the number of pulses from 2.5MHz ouput of Si5351 in 80 seconds (should be 200e6) 
int substate=0;

uint8_t gps_symbol[7] = {14,27,17,27,14,14,4}; //I would have made this const but threw type errors and had better things to do than edit someone else's library

uint8_t crossed_t[7] = {31,4,4,31,4,4,4};
uint8_t crossed_x[7] = {17,17,10,31,10,17,17};

//State variable
enum state_t{START, UNCONFIGURED, UNLOCKED, HOME, PANIC, CALLSIGN, CALLSIGN_CHECK, EXTENDED_CHECK, LOCATOR, LOCATOR_CHECK, POWER, POWER_WARNING, POWER_QUESTION, TX_PERCENTAGE, IP, BAND, OTHER_BAND_WARNING, ENCODING, DATE_FORMAT, CALIBRATING, TX_DISABLED_QUESTION} state = START;
bool stateInitialised = 0, editingFlag = 0;
const uint32_t wspr_tone_delay = (uint32_t)(256000.0 * (double)CORE_TICK_RATE/375.0);

void setup()
{
	lcd.begin(DOG_LCD_M163, LCD_CONTRAST, DOG_LCD_VCC_5V);
	lcd.noCursor();
	register_lcd_for_panic(&lcd);
	
	RPI.begin(115200);
	PC.begin(9600); //Used for debugging and stuff
	GPS.begin(9600);
	master.register_pi_uart(&RPI);
	master.register_gps_uart(&GPS);
	
	pinMode(GPS_PPS, INPUT);
	pinMode(MENU_BTN, INPUT);
	pinMode(EDIT_BTN, INPUT);
	pinMode(LED, OUTPUT);
	pinMode(PI_WATCHDOG, INPUT);
	digitalWrite(LED, LOW);
	
	pinMode(TX, OUTPUT);
	digitalWrite(TX, LOW);

	#if OSC_ENABLED
		osc.begin(XTAL_10pF, 25000000,GPS_ENABLED);
	#endif
}



void lcd_write(int row, int col, String data)
{
	lcd.setCursor(row, col);
	lcd.print(data);
}
 
 void lcd_clear_line(int row)
 {
	 lcd.setCursor(row,0);
	 lcd.print("                ");
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


bool menu_pressed()
{
	return !digitalRead(MENU_BTN); //done in function to save me remembering active high / low
}

bool edit_pressed()
{
	return !digitalRead(EDIT_BTN);
}

void heartbeat()
{
	master.heartbeat = 1;
	master.piSyncTime = millis();
}

uint32_t tx (uint32_t currentTime)
{
	substate++;
	return (currentTime + wspr_tone_delay);
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
	
	switch (state) //going to attempt to implement as a state machine, sure M0IKY will have some complaints to make 
	{
		case START: //Note this first state isn't quite the same as the others as it is blocking. This is becuase the UART doesn't need checking if the server is not running
		{
			lcd_write(0,2,"Waiting for");
			lcd_write(1,0, "server to start");
			bool old_watchdog = digitalRead(PI_WATCHDOG);
			
			master.setup();
			
			while(old_watchdog == digitalRead(PI_WATCHDOG)) //Wait until server starts i.e. watchdog pin changes
			{ 
				static int dot_num = 0;
				lcd.setCursor(2,0);
				for (int i =0; i< 16; i++)
				{
					lcd.print(i< dot_num ? "." : " ");
				}
				dot_num++;
				dot_num %= 17;
				PC.println(dot_num);
				digitalWrite(LED, dot_num%2); //Flash LED
				delay(1000);
				lcd_clear_line(2);
			}
			attachInterrupt(1, heartbeat, RISING); //INT1 is on RB14, will reset the watchdog timeout everytime the pin goes high.
			state_clean();
			digitalWrite(LED,LOW);
			state = IP;
			break;
		}
					
		case IP:
		{
			static uint32_t ipRequestedTime;
			if(!stateInitialised) //This is the first time in this state so draw on the LCD and wait for debounce
			{
				ipRequestedTime = millis();
				master.sync("1.2.3.4", supervisor::IP);
				master.sync("deadbeef", supervisor::HOSTNAME); //Load stupid data into master to check when it's updated
				RPI.print("I;\n");
				RPI.print("H;\n");
				lcd_write(0,0, "IP and Hostname");
				master.sync("Displaying Connection Info", supervisor::STATUS);		
				while(menu_pressed()) delay(50);
				while(edit_pressed())delay(50);
				stateInitialised=1;
			}
			
			if(master.settings().ip != "1.2.3.4" && master.settings().hostname!="deadbeef")
			{
				//We have got new connection information
				if(master.settings().ip == "")
				{
					//Pi is not connected to network
					lcd_write(1, 3, "No network");
					lcd_write(2, 3, "connection");
				}
				else
				{
					lcd_write(1, 0, master.settings().ip);
					lcd_write(2, 0, master.settings().hostname);
				}
			}
			break; //end of IP
		}

		default: while(1) {PC.println("Attempted to enter state" + String(state)); delay(1000);} break;
					


	}; //end of state machine

} //end of loop




