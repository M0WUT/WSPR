#ifndef supervisorH
#define supervisorH
#include "../../WSPR_config.h"

class supervisor
{
	public:
		supervisor();
		
		//loads data from EEPROM (if available) or loads default values
		void setup();
		
		enum data_t {CALLSIGN, GPS_ENABLE, LOCATOR, POWER, TX_DISABLE, DATE_FORMAT, TX_PERCENTAGE, STATUS, TIME, DATE, BAND, IP, HOSTNAME, CALIBRATION};
		enum dateFormat_t {AMERICAN, BRITISH, GLOBAL};
		
		struct settings_t
		{
			String callsign;
			String locator;
			String ip;
			String hostname;
			int band; 
			bool bandhop;
			int power; //in dBm
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
			dateFormat_t dateFormat;	
			String dateString;
			String timeString;
			bool gpsEnabled = 0;
			bool gpsActive;
			bool piActive;
			String status;
		};
		
	
		//Function used to return settings to make them effectively read only, except via sync function
		struct settings_t settings();
		
		//Synchronisation functions
		void sync(String data, data_t type, const bool updatePi = 1);
		void sync(struct supervisor::settings_t::time_t newTime, data_t type, const bool updatePi = 1);
		void sync(int data, data_t type, const bool updatePi = 1);
		void sync(const char *data, supervisor::data_t type, const bool updatePi = 1);
		void sync(supervisor::dateFormat_t data, supervisor::data_t type, const bool updatePi = 1);
		void sync(int *data, data_t type, const bool updatePi = 1);
		
		//Used to indicate to main program that something has changed
		bool updated(supervisor::data_t type);
		void clearUpdateFlag(supervisor::data_t type);
		
		//Functions to ingest data
		void pi_handler();
		void gps_handler();
		
		//Deals with any data requests / new data input
		void background_tasks();
		
		//Allows for registering of UART buses
		void register_pi_uart(HardwareSerial *uart);
		void register_gps_uart(HardwareSerial *uart);
		
		//Time last synched to bits of hardware;
		volatile uint32_t piSyncTime;
		volatile uint32_t gpsSyncTime;
		volatile bool heartbeat;
		
	
		
	private:
		LC640 eeprom;
		bool locatorRequested;
		bool timeRequested;
		struct settings_t setting;
		String linuxTimeString;
		int bandArray[24];
		int txDisableArray[12];
		int filter[12]; 
		uint32_t updatedFlags = 0; //Used as a 32 bit array of updated bits, indexed by data_t
		TinyGPSPlus gps;
		HardwareSerial *piUart = NULL;
		HardwareSerial *gpsUart = NULL;
		
		//EEPROM addresses
		static const int EEPROM_CALLSIGN_BASE_ADDRESS = 0;
		static const int EEPROM_LOCATOR_BASE_ADDRESS = 10;
		static const int EEPROM_POWER_ADDRESS = 16;
		static const int EEPROM_TX_PERCENTAGE_ADDRESS = 17;
		static const int EEPROM_DATE_FORMAT_ADDRESS = 18;
		static const int EEPROM_BAND_BASE_ADDRESS = 19;
		static const int EEPROM_TX_DISABLE_BASE_ADDRESS = 44;
		static const int EEPROM_CHECKSUM_BASE_ADDRESS = 56;
		static const int EEPROM_GPS_ENABLED_ADDRESS = 59;
};
#endif