#ifndef supervisorH
#define supervisorH
#include "../../WSPR_config.h"

class supervisor
{
	public:
		//Constructor loads data from EEPROM (if available) or loads default values
		supervisor();
		
		enum data_t {CALLSIGN, LOCATOR, POWER, TX_DISABLE, BAND_ARRAY, DATE_FORMAT, TX_PERCENTAGE, STATUS, HOUR, MINUTE, UNIX_TIME, TIME, BAND, IP, HOSTNAME};
		struct settings_t
		{
			String callsign;
			String locator;
			String ip;
			String hostname;
			int band;
			int power;
			struct time_t
			{
				int day;
				int month;
				int year;
				int hour;
				int minute;
				int second;
			} time;

			int txPercentage;
			bool txDisable;
			String date;
			bool gpsEnabled;
			bool gpsActive;
			bool piActive;
			bool upgradeFlag;
			String statusString;
		};
		
	
		//Function used to return settings to make them effectively read only, except via sync function
		struct settings_t settings();
		
		//Synchronisation functions
		int sync(String data, data_t type);
		int sync(struct supervisor::settings_t::time_t time, data_t type);
		int sync(int data, data_t type);
		int sync(int *data, data_t type);
		
		//Used to indicate to main program that something has changed
		bool updated(supervisor::data_t type);
		
		//Functions to ingest data
		void uart_handler();
		void gps_handler(TinyGPSPlus gps);
		
		//Deals with any data requests / new data input
		void background_tasks();
		
		//Time last synched to bits of hardware;
		volatile uint32_t piSyncTime;
		volatile uint32_t gpsSyncTime;
		bool heartbeat;
		
	private:
		LC640 eeprom;
		bool locatorRequested;
		bool timeRequested;
		struct settings_t setting;
		String linuxTimeString;
		int bandArray[24];
		int txDisable[12];
		int filter[12];		
		uint32_t updatedFlags = 0; //Used as a 32 bit array of updated bits, indexed by data_t
};
#endif