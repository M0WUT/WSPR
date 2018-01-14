#include "supervisor.h"

int supervisor::sync(String data, supervisor::data_t type){}
int supervisor::sync(int data, supervisor::data_t type){}
int supervisor::sync(int data[], supervisor::data_t type){}
supervisor::supervisor() : eeprom(EEPROM_CS){}
int supervisor::eeprom_load(){}

void supervisor::uart_handler(String data){}


struct supervisor::settings_t supervisor::settings() {return this->setting;}

void supervisor::background_tasks()
{
	this->setting.gpsActive = this->setting.gpsEnabled && (millis() - this->gpsSyncTime < GPS_TIMEOUT);
	this->setting.piActive = (millis() - this->piSyncTime < PI_TIMEOUT);
	
	if(this->heartbeat)
		RPI.print("A;\n"); //If PI has toggled GPIO to indicate activity acknowledge
	
	if(this->timeRequested && this->setting.gpsActive) //Only responds to time requests if we have valid GPS data
	{
		RPI.print(this->linuxTimeString);
		this->timeRequested = 0;
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
				";\n", TIME_STRING);
	this->sync(this->bandArray[this->setting.time.hour], BAND);
	this->sync(maidenhead(gps), LOCATOR);
	this->sync(txDisable[this->setting.time.hour], TX_DISABLE);
}