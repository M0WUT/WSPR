#include "supervisor.h"

supervisor::supervisor() : eeprom(EEPROM_CS){}

void supervisor::setup()
{
	//Setup Band Control pins
	pinMode(BAND0, OUTPUT);
	digitalWrite(BAND0, LOW);
	pinMode(BAND1, OUTPUT);
	digitalWrite(BAND1, LOW);
	pinMode(BAND2, OUTPUT);
	digitalWrite(BAND2, LOW);
	
	//Attempt to load data from EEPROM, if no valid data, load defaults and save to EEPROM
	
	if( eeprom.read(EEPROM_CHECKSUM_BASE_ADDRESS) == 'L' && 
		eeprom.read(EEPROM_CHECKSUM_BASE_ADDRESS+1) == 'I' && 
		eeprom.read(EEPROM_CHECKSUM_BASE_ADDRESS+2) == 'D')
	{
		///////////////////////////////
		//Load everything from eeprom//
		///////////////////////////////
		String tempData;

		//Load callsign
		tempData = "";
		for(int i = 0; i < 10; i++)
		{
			char x = eeprom.read(EEPROM_CALLSIGN_BASE_ADDRESS + i);
			if(x == 0) break;
			else tempData += x;
		}
		sync(tempData, CALLSIGN);
		
		//Load locator
		tempData = "";
		for(int i = 0; i < 6; i++)
		{
			char x = eeprom.read(EEPROM_LOCATOR_BASE_ADDRESS + i);
			if(x == 0) break;
			else tempData += x;
		}
		sync(tempData, LOCATOR);
		
		//Load power
		sync(eeprom.read(EEPROM_POWER_ADDRESS), POWER);
		
		//Load tx percentage
		sync(eeprom.read(EEPROM_TX_PERCENTAGE_ADDRESS), TX_PERCENTAGE);
		
		//Load date format
		sync((dateFormat_t)eeprom.read(EEPROM_DATE_FORMAT_ADDRESS), DATE_FORMAT);
		
		//Load txDisable Array
		int tempDisableArray[12];
		for(int i = 0; i<12; i++)
		{
			tempDisableArray[i] = (eeprom.read(EEPROM_TX_DISABLE_BASE_ADDRESS+i) == '1' ? 1 : 0);
		}
		sync(tempDisableArray, TX_DISABLE);
		
		//Load Band Array
		int tempBandArray[24];
		for(int i = 0; i<24; i++)
		{
			tempBandArray[i] = eeprom.read(EEPROM_TX_DISABLE_BASE_ADDRESS+i) - '0';
		}
		sync(tempBandArray, BAND);
		
	}
	else
	{
		//EEPROM doesn't contain valid data, use defaults
		sync("M0WUT", CALLSIGN);
		sync("GPS", LOCATOR);
		sync(23, POWER);
		sync(20, TX_PERCENTAGE);
		sync(BRITISH, DATE_FORMAT);
		int defaultBand[24] = {7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7}; //All 20m
		sync(defaultBand, BAND);
		int defaultDisable[12] = {1,1,1,1,1,1,1,1,1,1,1,1}; //TX disabled for all bands
		sync(defaultDisable, TX_DISABLE);
		eeprom.write(EEPROM_CHECKSUM_BASE_ADDRESS, 'L');
		eeprom.write(EEPROM_CHECKSUM_BASE_ADDRESS+1, 'I');
		eeprom.write(EEPROM_CHECKSUM_BASE_ADDRESS+2, 'D');
	}
}


int supervisor::sync(supervisor::dateFormat_t data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	if(dateFormat != data)
	{
		dateFormat = data;
		char temp[8]; //needed as sprintf needs a char*, not a string
		switch (dateFormat)
		{
			case BRITISH: sprintf(temp, "%02i/%02i/%02i", setting.time.day,setting.time.month,setting.time.year%100); break;					
			case AMERICAN: sprintf(temp, "%02i/%02i/%02i", setting.time.month,setting.time.day,setting.time.year%100); break;
			case GLOBAL: sprintf(temp, "%02i/%02i/%02i", setting.time.year%100,setting.time.month,setting.time.day); break;	
		};
		
		setting.dateString = String(temp);
		updatedFlags |= (1<<DATE);
		
	}	
}

bool supervisor::updated(supervisor::data_t type){return (updatedFlags >> type) & 0x01;}

struct supervisor::settings_t supervisor::settings() {return setting;}

void supervisor::background_tasks()
{
	setting.gpsActive = setting.gpsEnabled && (millis() - gpsSyncTime < GPS_TIMEOUT);
	setting.piActive = (millis() - piSyncTime < PI_TIMEOUT);
	
	if(heartbeat)
		piUart->print("A;\n"); //If PI has toggled GPIO to indicate activity acknowledge
		heartbeat = 0;
	
	if(timeRequested && setting.gpsActive) //Only responds to time requests if we have valid GPS data
	{
		piUart->print(linuxTimeString);
		timeRequested = 0;
	}
	
	if(locatorRequested && setting.gpsActive)
	{
		piUart->print("G" + setting.locator + ";\n");
		locatorRequested = 0;
	}
	
	if(bandArray[setting.time.hour] != setting.band)
	{
		setting.band = bandArray[setting.time.hour];
		updatedFlags |= (1<<BAND);
	}
	
	if(txDisableArray[setting.band] != setting.txDisable)
	{
		setting.txDisable = txDisableArray[setting.band];
		updatedFlags |= (1<<TX_DISABLE);
	}

	digitalWrite(BAND0, filter[setting.band] & 1);
	digitalWrite(BAND1, filter[setting.band] & 2);
	digitalWrite(BAND2, filter[setting.band] & 4);	
}

void supervisor::gps_handler()
{
	if (gpsUart == NULL) panic(GPS_UART_NOT_REGISTERED);
	if(setting.gpsEnabled)
	{
		if(gpsUart->available()) //Has to be nested as this if can't be evaluated if a gps object hasn't been initialised
		{
			while(gpsUart->available())
				gps.encode(gpsUart->read());
			sync(supervisor::settings_t::time_t{gps.date.day(), gps.date.month(), gps.date.year(), gps.time.hour(), gps.time.minute(), gps.time.second()}, TIME);
			sync(maidenhead(&gps), LOCATOR);
		}
	}
}

void supervisor::pi_handler()
{
	if (piUart == NULL) panic(PI_UART_NOT_REGISTERED);
	if(piUart->available())
	{
		//Temporary string to store data in
		String rxString;
		rxString.reserve(50);
		
		//Read all available data until newline found or timeout
		char x = piUart->read();
			while(x != '\n')
			{
				rxString += x;
				int start_time = millis();
				//Need timeout as PIC is much faster than Pi so some tranmissions weren't done before uart->available() == 0
				while(!piUart->available())
				{
					if((millis() - start_time > 2000) || rxString.length() > 50) 
					{
						panic(PI_INCOMPLETE_TRANSMISSON);
					}
				}
				x = piUart->read();	
			}
		

		
		if(rxString.substring(rxString.length()-1) != ";") panic(INCORRECT_UART_TERMINATION);
		
		String data = rxString.substring(1, rxString.length() - 1); //Trim control characters
		
		//Note that very little sanity checking is done on the data
		//If implementing in your own system, might be worth adding
		
		if(data == "") //no packet means Pi is requesting info
		{
			switch(rxString[0]) //switch on control character
			{
				case 'I':	sync(data, IP, 0); break; //actually setting data, for when pi not connected to network so has no IP address
				case 'H':   sync(data, HOSTNAME, 0); break; //as above with hostname
				case 'C':	piUart->print("C" + setting.callsign + ";\n"); break;
				case 'L':	piUart->print("L" + (setting.gpsEnabled ? "GPS" : setting.locator) + ";\n"); break;
				case 'P':	piUart->print("P" + String(setting.power) + ";\n"); break;
				case 'B':	piUart->print("B");
							for (int i=0; i<23; i++)
							{
								piUart->print(bandArray[i]);
								piUart->print(",");
							}
							piUart->print(bandArray[23]);
							piUart->print(";\n");
							break;
				case 'X':	piUart->print("X" + String(setting.txPercentage) + ";\n"); break;
				case 'S':	piUart->print("S" + setting.status + ";\n"); break;
				case 'T':	timeRequested = 1; break;
				case 'V':	piUart->print("V" + String(VERSION) + ";\n"); break;
				case 'U':	//Deliberate fallthough as procedure is same for software and firmware updated
				case 'F':	setting.upgradeChar = rxString[0]; break;
				case 'D':	piUart->print("D");
							for (int i=0; i<11; i++)
							{
								piUart->print(txDisableArray[i]);
								piUart->print(",");
							}
							piUart->print(txDisableArray[11]);
							piUart->print(";\n");
							break;
				case 'G':	locatorRequested = 1; break;
							
							
				case 'A': 	//These should never be sent to the PIC
							//put in as acknowledgement I haven't forgotten to deal with them
				default: 	panic(rxString[0] + data, PI_UNKNOWN_CHARACTER); break;
				
				
			};
		}
		else //we are setting data
		{
			switch(rxString[0]) //switch on control character
			{
				case 'I':	sync(data, IP, 0); break;
				case 'H':	sync(data, HOSTNAME, 0); break;
				case 'C':	sync(data, CALLSIGN, 0); break;
				case 'L': 	sync(data, LOCATOR, 0); break;
				case 'P':	sync(atoi(data.c_str()), POWER, 0); break; //Convert String to int
				case 'B':	int temp[24];
							for(int i = 0; i<24; i++)
								temp[i] = data[i*2] - '0';
							sync(temp, BAND, 0);
							break;
				case 'X': 	sync(atoi(data.c_str()), TX_PERCENTAGE, 0); break; //Convert String to int
				case 'D':	int dTemp[12];
							for(int i = 0; i<12; i++)
								dTemp[i] = data[i*2] - '0';	
							sync(dTemp, TX_DISABLE, 0);
							break;
				case 'S': 	//These all fallthrough deliberately
				case 'V':	//These should not be sent with extra data
				case 'U': 	//They are put here as acknowledgement that
				case 'F':	//I haven't forgotten to deal with them
				case 'A':
				case 'G':
				case 'T':
				default:	panic(rxString[0] + data, PI_UNKNOWN_CHARACTER); break;
			};		
		}
	}
}

int supervisor::sync(supervisor::settings_t::time_t newTime, data_t type, const bool updatePi/*=1*/)
{
	if(type != TIME) panic(TIME_SYNC_FAILED);
	if(	setting.time.hour != newTime.hour ||
		setting.time.minute != newTime.minute ||
		setting.time.second != newTime.second)
	{
		char temp[5];
		sprintf(temp, "%02i:%02i", setting.time.hour, setting.time.minute);
		setting.timeString = String(temp);
		updatedFlags |= 1<<TIME;
	}		
	
	if(	setting.time.day != newTime.day ||
		setting.time.month != newTime.month ||
		setting.time.year != newTime.year)
	{

		char temp[8]; //needed as sprintf needs a char*, not a string
		switch (dateFormat)
		{
			case BRITISH: sprintf(temp, "%02i/%02i/%02i", setting.time.day,setting.time.month,setting.time.year%100); break;					
			case AMERICAN: sprintf(temp, "%02i/%02i/%02i", setting.time.month,setting.time.day,setting.time.year%100); break;
			case GLOBAL: sprintf(temp, "%02i/%02i/%02i", setting.time.year%100,setting.time.month,setting.time.day); break;	
		};
		
		setting.dateString = String(temp);
		updatedFlags |= (1<<DATE);
	}		
	
	setting.time = newTime;
	char temp[20];
	sprintf(temp, "T%02i/%02i/%02i %02i:%02i:%02i;\n", setting.time.day, setting.time.month, setting.time.year, setting.time.hour, setting.time.minute, setting.time.second);
	linuxTimeString = String(temp);
}

void supervisor::register_pi_uart(HardwareSerial *uart)
{
	piUart = uart;
	piUart->begin(115200);
}

void supervisor::register_gps_uart(HardwareSerial *uart)
{
	gpsUart = uart;
	gpsUart->begin(9600);
}

int supervisor::sync(String data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	switch(type)
	{
		case LOCATOR: 	//If we are here the data is a bit dumb, "GPS" means use onboard GPS, anything else is the locator
						if(data == "GPS" && !setting.gpsEnabled)
						{
							setting.gpsEnabled = 1;
							updatedFlags |= (1<<GPS);
							if(updatePi)
							{
								piUart->print("LGPS;\n");
								locatorRequested = 1;
							}
						}
						else
						{
							if(setting.gpsEnabled)
							{
								setting.gpsEnabled = 0;
								updatedFlags |= (1<<GPS);
							}
							
							if(setting.locator != data)
							{
								setting.locator = data;
								updatedFlags |= (1<<LOCATOR);
								if(updatePi)
									piUart->print("L" + setting.locator + ";\n");
							}
						}		
						break;
						
						
		case IP:		if(data != setting.ip)
						{
							setting.ip = data;
							updatedFlags |= (1<<IP);
							//Don't need to check whether this came from Pi, nothing else knows its IP address
							#if DEBUG
								PC.println("IP: " + setting.ip);
							#endif
						}
						break;
						
		case HOSTNAME:	if(data != setting.hostname)
						{
							setting.hostname = data;
							updatedFlags |= (1<<HOSTNAME);
							//Don't need to check whether this came from Pi, nothing else knows its hostname
							#if DEBUG
								PC.println("Hostname: " + setting.hostname);
							#endif
						}
						break;
						
		case CALLSIGN:	if(data != setting.callsign)
						{
							setting.callsign = data;
							updatedFlags |= (1<<CALLSIGN);
							if(updatePi)
								piUart->print("C" + setting.callsign + ";\n");
							#if DEBUG
								PC.println("Callsign: " + setting.callsign);
							#endif
						}
						break;
						
		case STATUS:	if(data != setting.status)
						{
							setting.status = data;
							updatedFlags |= (1<<STATUS);
							if(updatePi)
								piUart->print("S" + setting.status + ";\n");
							#if DEBUG
								PC.println("Status: " + setting.status);
							#endif
						}
						break;
			
		default:		panic(INVALID_SYNC_PARAMETERS); break;	
	};
}

int supervisor::sync(int data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	switch(type)
	{
		case POWER:		if(data != setting.power)
						{
							setting.power = data;
							updatedFlags |= (1<<POWER);
							if(updatePi)
								piUart->print("P" + String(setting.power) + ";\n");
						}
						break;
						
		case TX_PERCENTAGE:
						if(data != setting.txPercentage)
						{
							setting.txPercentage = data;
							updatedFlags |= (1<<TX_PERCENTAGE);
							if(updatePi)
								piUart->print("T" + String(setting.txPercentage) + ";\n");
						}
						break;
		default:		panic(INVALID_SYNC_PARAMETERS); break;	
	};
}
int supervisor::sync(int *data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	int arraySize = sizeof(data)/sizeof(data[0]);
	int targetArraySize;
	int *destination;
	String controlChar;
	switch(type)
	{
		case BAND:	targetArraySize = 24;
					destination = bandArray;
					controlChar = "B";
					break;
		case TX_DISABLE:
					targetArraySize = 12;
					destination = txDisableArray;
					controlChar = "D";
					break;
		default:	panic(INVALID_SYNC_PARAMETERS); break;	
	};
	
	if(arraySize != targetArraySize) panic(INVALID_SYNC_PARAMETERS);
	
	bool changedFlag = 0;
	for(int i = 0; i<targetArraySize; i++)
	{
		if(data[i] != destination[i])
		{
				changedFlag = 1;
				destination[i] = data[i];
		}
	}
	
	if(updatePi && changedFlag)
	{
		piUart->print(controlChar + String(destination[0]));
		for(int i = 1; i<targetArraySize; i++)
			piUart->print("," + String(destination[i]));
		piUart->print(";\n");
	}
}
