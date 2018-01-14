#ifndef supervisorH
#define supervisorH
#include "../../WSPR_config.h"

class supervisor
{
	public:
		supervisor();
		
		enum data_t {CALLSIGN, LOCATOR, POWER, TX_DISABLE, BAND_ARRAY, DATE_FORMAT, TX_PERCENTAGE, STATUS, HOUR, MINUTE, TIME_STRING, BAND};
		struct settings_t
		{
			String callsign;
			String locator;
			String ip;
			String hostname;
			int band;
			struct time_t
			{
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
			String statusString;
		};
		
		
		
		//Function used to return settings to make them effectively read only, except via sync function
		struct settings_t settings();
		
		//Synchronisation functions
		int sync(String data, data_t type);
		int sync(int data, data_t type);
		int sync(int data[], data_t type);
		
		//Functions to ingest data
		int eeprom_load();
		void uart_handler(String data);
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
};
#endif