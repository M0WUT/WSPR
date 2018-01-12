#include "supervisor.h"

int supervisor::sync(String data, supervisor::data_t type){}
int supervisor::sync(int data, supervisor::data_t type){}
int supervisor::sync(int data[], supervisor::data_t type){}
supervisor::supervisor() : eeprom(EEPROM_CS){}
int supervisor::eeprom_load(){}
void supervisor::gps_handler(TinyGPSPlus gps){}
void supervisor::uart_handler(String data){}
struct supervisor::settings_t supervisor::settings() {return this->setting;}

void supervisor::background_tasks()
{
	this->setting.gpsActive = (millis() - this->gpsSyncTime < GPS_TIMEOUT);
	this->setting.piActive = (millis() - this->piSyncTime < PI_TIMEOUT);
	if(this->heartbeat)
		RPI.print("A;\n"); //If PI has toggled GPIO to indicate activity acknowledge
	if(this->timeRequested && this->setting.gpsActive) //Only responds to time requests if we have valid GPS data
	{
		RPI.print(this->linuxTimeString);
		this->timeRequested = 0;
	}
		
}