#include "supervisor.h"

int supervisor::sync(String data, supervisor::data_t type){}
int supervisor::sync(int data, supervisor::data_t type){}
int supervisor::sync(int *data, supervisor::data_t type){}
supervisor::supervisor() : eeprom(EEPROM_CS){}
int supervisor::eeprom_load(){}
bool supervisor::updated(supervisor::data_t type){return (this->updatedFlags >> type) & 0x01;}

struct supervisor::settings_t supervisor::settings() {return this->setting;}

void supervisor::background_tasks()
{
	this->setting.gpsActive = this->setting.gpsEnabled && (millis() - this->gpsSyncTime < GPS_TIMEOUT);
	this->setting.piActive = (millis() - this->piSyncTime < PI_TIMEOUT);
	
	if(this->heartbeat)
		RPI.print("A;\n"); //If PI has toggled GPIO to indicate activity acknowledge
		this->heartbeat = 0;
	
	if(this->timeRequested && this->setting.gpsActive) //Only responds to time requests if we have valid GPS data
	{
		RPI.print(this->linuxTimeString);
		this->timeRequested = 0;
	}
	
	if(this->locatorRequested && this->setting.gpsActive)
	{
		RPI.print("G" + this->setting.locator + ";\n");
		this->locatorRequested = 0;
	}
	
	digitalWrite(BAND0, this->filter[this->setting.band] & 1);
	digitalWrite(BAND1, this->filter[this->setting.band] & 2);
	digitalWrite(BAND2, this->filter[this->setting.band] & 4);	
}


void supervisor::gps_handler(TinyGPSPlus gps)
{
	this->sync(gps.time.hour(), HOUR);
	this->sync(gps.time.hour(), MINUTE);
	this->sync(	"T" +
				String(gps.date.day()) +
				"/" +
				String(gps.date.month()) +
				"/" +
				String(gps.date.year()) +
				" " +
				String(gps.time.hour()) +
				":" +
				String(gps.time.minute()) +
				":" +
				String(gps.time.second()) +
				";\n", UNIX_TIME);
	this->sync(this->bandArray[this->setting.time.hour], BAND);
	this->sync(maidenhead(gps), LOCATOR);
	this->sync(txDisable[this->setting.time.hour], TX_DISABLE);
}

void supervisor::uart_handler()
{
	//Temporary string to store data in
	String rxString;
	rxString.reserve(50);
	
	//Read all available data until newline found or timeout
	char x = RPI.read();
		while(x != '\n')
		{
			rxString += x;
			int start_time = millis();
			//Need timeout as PIC is much faster than Pi so some tranmissions weren't done before RPI.available() == 0
			while(!RPI.available())
				if((millis() - start_time > 2000) || rxString.length() > 50) 
				{
					panic(PI_INCOMPLETE_TRANSMISSON);
				}
			x = RPI.read();	
		}
	

	
	if(rxString.substring(rxString.length()-1) != ";") panic(INCORRECT_UART_TERMINATION);
	
	String data = rxString.substring(1, rxString.length() - 1); //Trim control characters
	
	//Note that very little sanity checking is done on the data
	//If implementing in your own system, might be worth adding
	
	if(data == "") //no packet means Pi is requesting info
	{
		switch(rxString[0]) //switch on control character
		{
			case 'C':	RPI.print("C" + this->setting.callsign + ";\n"); break;
			
			
		};
	}
	else //we are setting data
	{
		switch(rxString[0]) //switch on control character
		{
			case 'I':	this->sync(data, IP); break;
			case 'H':	this->sync(data, HOSTNAME); break;
			case 'C':	this->sync(data, CALLSIGN); break;
			case 'L': 	this->sync(data, LOCATOR); break;
			case 'P':	this->sync(atoi(data.c_str()), POWER); break; //Convert String to int
			case 'B':	int temp[24];
						for(int i = 0; i<24; i++)
							temp[i] = data[i*2] - '0';
						this->sync(temp, BAND_ARRAY);
						break;
			case 'X': 	this->sync(atoi(data.c_str()), TX_PERCENTAGE); break; //Convert String to int
			case 'T':	this->timeRequested = 1; break;
			case 'D':	int dTemp[12];
						for(int i = 0; i<12; i++)
							dTemp[i] = data[i*2] - '0';
							
						this->sync(dTemp, TX_DISABLE);
						break;
			case 'S': 	//These all fallthrough deliberately
			case 'V':	//These should not be sent with extra data
			case 'U': 	//They are put here as acknowledgement that
			case 'F':	//I haven't forgotten to deal with them
			case 'A':	
			default:	panic(rxString[0] + data, PI_UNKNOWN_CHARACTER);
		};
		
	}
	
	
	
	
	
	
	
	
}