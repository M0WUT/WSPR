#include "supervisor.h"
//TODO: add filter settings

const String VERSION = "0.6";
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
		#ifdef DEBUG
			PC.println("Loading from EEPROM");
		#endif
		//Load callsign
		tempData = "";
		for(int i = 0; i < 10; i++)
		{
			char x = eeprom.read(EEPROM_CALLSIGN_BASE_ADDRESS + i);
			if(x == 0) break;
			else tempData += x;
		}
		sync(tempData, CALLSIGN);
		
		
		//DEBUG - only support GPS locator so no point saving in EEPROM
		
		//Load locator
		/*
		tempData = "";
		for(int i = 0; i < 6; i++)
		{
			char x = eeprom.read(EEPROM_LOCATOR_BASE_ADDRESS + i);
			if(x == 0) break;
			else tempData += x;
		}
		tempData.trim();
		sync(tempData, LOCATOR);
		
		
		//Load GPS Enable
		sync(eeprom.read(EEPROM_GPS_ENABLED_ADDRESS), GPS_ENABLE);
		*/
		
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
			tempDisableArray[i] = eeprom.read(EEPROM_TX_DISABLE_BASE_ADDRESS+i);
		}
		sync(tempDisableArray, TX_DISABLE);
		
		//Load Band Array
		int tempBandArray[24];
		for(int i = 0; i<24; i++)
		{
			tempBandArray[i] = eeprom.read(EEPROM_BAND_BASE_ADDRESS+i);
		}
		sync(tempBandArray, BAND);
		
	}
	else
	{
		#ifdef DEBUG
			PC.println("Loading defaults");
		#endif
		//EEPROM doesn't contain valid data, use defaults
		sync("M0WUT", CALLSIGN);
		sync(1, GPS_ENABLE);
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
	
	sync("", LOCATOR); //LOCATOR always set to blank on startup as only currently support GPS locator
	sync(1, GPS_ENABLE); //Only support GPS mode atm
	
	sync("Data loaded", STATUS); 
	
	
	//Make it look like we've synced with Pi to prevent server timeout on startup, want GPS to not be synced to prevent thinking GPS data is valid
	piSyncTime = millis();
	gpsSyncTime = (millis() - 2 * GPS_TIMEOUT);
	
	
}

bool supervisor::updated(supervisor::data_t type){return (updatedFlags >> type) & 0x01;}
void supervisor::clearUpdateFlag(supervisor::data_t type){updatedFlags &= ~(1<<type);}

struct supervisor::settings_t supervisor::settings() {return setting;}

void supervisor::background_tasks()
{
	setting.gpsActive = (setting.gpsEnabled && (millis() - gpsSyncTime < GPS_TIMEOUT));
	setting.piActive = (millis() - piSyncTime < PI_TIMEOUT);
	
	if(heartbeat)
	{
		#ifdef DEBUG
			PC.println(".");
		#endif
		piUart->print("A;\n"); //If PI has toggled GPIO to indicate activity acknowledge
		heartbeat = 0;
	}
	
	if(timeRequested && setting.gpsActive) //Only responds to time requests if we have valid GPS data
	{
		#ifdef DEBUG
			PC.println("Sending requested time: " + linuxTimeString);
		#endif
		piUart->print(linuxTimeString);
		timeRequested = 0;
	}
	
	if(locatorRequested && setting.gpsActive && setting.locator != "")
	{
		#ifdef DEBUG
			PC.println("Sending requested GPS locator: " + setting.locator);
		#endif
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
	if(setting.gpsActive && gpsUart->available()) //If first expression is false, second expression is not evaluated
	{
		while(gpsUart->available())
			gps.encode(gpsUart->read());
		if(gps.location.isValid())
		{
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
		

		
		if(rxString.substring(rxString.length()-1) != ";") panic(INCORRECT_UART_TERMINATION, rxString.substring(rxString.length()-1));
		
		String data = rxString.substring(1, rxString.length() - 1); //Trim control characters
			
		if(data == "") //no packet means Pi is requesting info
		{
			switch(rxString[0]) //switch on control character
			{
				case 'I':	sync(data, IP, 0); break; //actually setting data, for when pi not connected to network so has no IP address
				case 'H':   sync(data, HOSTNAME, 0); break; //as above with hostname
				case 'C':	piUart->print("C" + setting.callsign + ";\n"); break;
				case 'P':	piUart->print("P" + String(setting.power) + ";\n"); break;
				case 'B':	piUart->print("B");
							for (int i=0; i<23; i++)
							{
								piUart->print(bandArray[i], HEX);
								piUart->print(",");
							}
							piUart->print(bandArray[23], HEX);
							piUart->print(";\n");
							break;
				case 'X':	piUart->print("X" + String(setting.txPercentage) + ";\n"); break;
				case 'S':	piUart->print("S" + setting.status + ";\n"); break;
				case 'T':	timeRequested = 1; break;
				case 'V':	piUart->print("V" + VERSION + ";\n"); break;
				case 'U':	//Deliberate fallthough as procedure is same for software and firmware updated
				case 'F':	piUart->print(String(rxString[0]) + ";\n");
							while(1)
							{
								//Upgrade is happening so just stop, need to keep responding to heartbeat though
								if(heartbeat)
								{
									piUart->print("A;\n");
									heartbeat = 0;
								}
							}
							break;
				case 'D':	piUart->print("D");
							for (int i=0; i<11; i++)
							{
								piUart->print(txDisableArray[i]);
								piUart->print(",");
							}
							piUart->print(txDisableArray[11]);
							piUart->print(";\n");
							break;
				case 'M':	piUart->print("M" + String(setting.gpsEnabled) + ";\n"); break;
				case 'G':	locatorRequested = 1; break;
				case 'L':	break; //DEBUG
				case 'A': 	//These should never be sent to the PIC
							//put in as acknowledgement I haven't forgotten to deal with them
				default: 	panic(PI_UNKNOWN_CHARACTER, rxString[0] + data); break;
			};
		}
		else //we are setting data
		{
			switch(rxString[0]) //switch on control character
			{
				case 'I':	sync(data, IP, 0); break;
				case 'H':	sync(data, HOSTNAME, 0); break;
				case 'C':	sync(data, CALLSIGN, 0); break;
				case 'P':	sync(atoi(data.c_str()), POWER, 0); break; //Convert String to int
				case 'B':	int temp[24];
							for(int i = 0; i<24; i++)
								temp[i] = data[i*2] > '9' ? data[i*2] - ('A' - 10) : data[i*2] - '0'; //Supplied as ascii hex, convert to int
							sync(temp, BAND, 0);
							break;
				case 'X': 	sync(atoi(data.c_str()), TX_PERCENTAGE, 0); break; //Convert String to int
				case 'D':	int dTemp[12];
							for(int i = 0; i<12; i++)
								dTemp[i] = data[i*2] - '0';	
							sync(dTemp, TX_DISABLE, 0);
							break;
				case 'L':	//These all fallthrough deliberately
				case 'G':	//These should not be sent with extra data
				case 'M':	//They are put here as acknowledgement that
				case 'S': 	//I haven't forgotten to deal with them
				case 'V':	
				case 'U': 	
				case 'F':	
				case 'A':
				case 'T':
				default:	panic(PI_UNKNOWN_CHARACTER, rxString[0] + data); break;
			};		
		}
	}
}

void supervisor::sync(supervisor::dateFormat_t data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	if(data != setting.dateFormat)
	{
		setting.dateFormat = data;
		char temp[8]; //needed as sprintf needs a char*, not a string
		#ifdef DEBUG
			PC.print("Date format: ");
		#endif
		switch (setting.dateFormat)
		{
			case BRITISH: sprintf(temp, "%02i/%02i/%02i", setting.time.day,setting.time.month,setting.time.year%100); PC.print("DD/MM/YY"); break;					
			case AMERICAN: sprintf(temp, "%02i/%02i/%02i", setting.time.month,setting.time.day,setting.time.year%100); PC.print("MM/DD/YY"); break;
			case GLOBAL: sprintf(temp, "%02i/%02i/%02i", setting.time.year%100,setting.time.month,setting.time.day); PC.print("YY/MM/DD"); break;	
		};
		PC.println("");
		eeprom.write(EEPROM_DATE_FORMAT_ADDRESS, setting.dateFormat);
		setting.dateString = String(temp);
		updatedFlags |= (1<<DATE);
		
	}	
}

void supervisor::sync(supervisor::settings_t::time_t newTime, data_t type, const bool updatePi/*=1*/)
{
	if(type != TIME) panic(TIME_SYNC_FAILED);
	if(	setting.time.hour != newTime.hour ||
		setting.time.minute != newTime.minute ||
		setting.time.second != newTime.second)
	{
		char temp[5];
		sprintf(temp, "%02i:%02i", newTime.hour, newTime.minute);
		setting.timeString = String(temp);
		
		updatedFlags |= 1<<TIME;
	}		
	
	if(	setting.time.day != newTime.day ||
		setting.time.month != newTime.month ||
		setting.time.year != newTime.year)
	{
		char temp[8]; //needed as sprintf needs a char*, not a string
		switch (setting.dateFormat)
		{
			case BRITISH: sprintf(temp, "%02i/%02i/%02i", newTime.day,newTime.month,newTime.year%100); break;					
			case AMERICAN: sprintf(temp, "%02i/%02i/%02i", newTime.month,newTime.day,newTime.year%100); break;
			case GLOBAL: sprintf(temp, "%02i/%02i/%02i", newTime.year%100,newTime.month,newTime.day); break;	
		};
		
		setting.dateString = String(temp);
		updatedFlags |= (1<<DATE);
	}		
	setting.time = newTime;
	
	char temp[25];
	sprintf(temp, "T%04i-%02i-%02i %02i:%02i:%02i;\n", setting.time.year, setting.time.month, setting.time.day, setting.time.hour, setting.time.minute, setting.time.second);
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

void supervisor::sync(const char *ptr, supervisor::data_t type, const bool updatePi/*=1*/)
{
	String data = String(ptr); //Deal with stupid Arduino having it's own String class which does not handle String literals :S
	sync(data, type, updatePi);
}

void supervisor::sync(String data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	switch(type)
	{
		case LOCATOR: 	if(data != setting.locator)
						{
							setting.locator = data;
							updatedFlags |= (1<<LOCATOR);
							if(updatePi)
								piUart->print("L" + setting.locator + ";\n");
							#ifdef DEBUG
								PC.println("Locator: " + setting.locator);
							#endif
							//Save to EEPROM, zero padding up to 6 characters
								for(int i = 0; i< 6; i++)
									eeprom.write(EEPROM_LOCATOR_BASE_ADDRESS + i, (i < data.length() ? data[i] : 0));
						}
						break;
						
						
		case IP:		if(data != setting.ip)
						{
							setting.ip = data;
							updatedFlags |= (1<<IP);
							//Don't need to check whether this came from Pi, nothing else knows its IP address
							#ifdef DEBUG
								PC.println("IP: " + setting.ip);
							#endif
						}
						break;
						
		case HOSTNAME:	if(data != setting.hostname)
						{
							setting.hostname = data;
							updatedFlags |= (1<<HOSTNAME);
							//Don't need to check whether this came from Pi, nothing else knows its hostname
							#ifdef DEBUG
								PC.println("Hostname: " + setting.hostname);
							#endif
						}
						break;
						
		case CALLSIGN:	if(data != setting.callsign)
						{
							if(!WSPR_encode(data, "IO93fp", 23, NULL, WSPR_NORMAL) || (!WSPR_encode(data, "IO93fp", 23, NULL, WSPR_EXTENDED)))
							{
								setting.callsign = data;
								updatedFlags |= (1<<CALLSIGN);
								if(updatePi)
									piUart->print("C" + setting.callsign + ";\n");
								#ifdef DEBUG
									PC.println("Callsign: " + setting.callsign);
								#endif
								
								//Save to EEPROM, zero padding up to 10 characters
								for(int i = 0; i< 10; i++)
									eeprom.write(EEPROM_CALLSIGN_BASE_ADDRESS + i, (i < data.length() ? data[i] : 0));
							}
							else
								warn("WARNING: Ignoring invalid callsign: " + data);	
						}
						break;
						
		case STATUS:	if(data != setting.status)
						{
							setting.status = data;
							updatedFlags |= (1<<STATUS);
							if(updatePi)
								piUart->print("S" + setting.status + ";\n");
							#ifdef DEBUG
								PC.println("Status: " + setting.status);
							#endif
						}
						break;
			
		default:		panic(INVALID_SYNC_PARAMETERS, String(type) + ", " + data); break;	
	};
}

void supervisor::sync(int data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	switch(type)
	{
		case POWER:		if(data != setting.power)
						{
							if(data < 61 && ((data % 10 == 0) || (data % 10 == 3) || (data % 10 == 7)))
							{
								setting.power = data;
								updatedFlags |= (1<<POWER);
								eeprom.write(EEPROM_POWER_ADDRESS, setting.power);
								#ifdef DEBUG
									PC.println("Power: " + String(setting.power) + "dBm");
								#endif
								if(updatePi)
									piUart->print("P" + String(setting.power) + ";\n");
							}
							else
								warn("WARNING: Ignoring invalid power: " + String(data));
						}
						break;
						
		case TX_PERCENTAGE:
						if(data != setting.txPercentage)
						{
							if(data < 101 && data % 10 == 0)
							{
								setting.txPercentage = data;
								updatedFlags |= (1<<TX_PERCENTAGE);
								eeprom.write(EEPROM_TX_PERCENTAGE_ADDRESS, setting.txPercentage);
								#ifdef DEBUG
									PC.println("Tx Percentage: " + String(setting.txPercentage));
								#endif
								if(updatePi)
									piUart->print("X" + String(setting.txPercentage) + ";\n");
							}
							else
								warn("WARNING: Ignoring invalid TX Percentage: " + String(data));
						}
						break;
		case GPS_ENABLE:
						if(data != setting.gpsEnabled)
						{
							setting.gpsEnabled = data;
							updatedFlags |= (1<<GPS_ENABLE);
							eeprom.write(EEPROM_GPS_ENABLED_ADDRESS, data & 0x01);
							#ifdef DEBUG
								PC.println("GPS: " + setting.gpsEnabled ? "Enabled" : "Disabled");
							#endif
							if(updatePi)
								piUart->print("M" + String(setting.gpsEnabled) + ";\n");
						}
						break;
		case CALIBRATION: break; //TODO
		default:		panic(INVALID_SYNC_PARAMETERS, String(type) + ", " + String(data)); break;	
	};
}
void supervisor::sync(int *data, supervisor::data_t type, const bool updatePi/*=1*/)
{
	int targetArraySize;
	int *destination;
	int eepromBaseAddress;
	String controlChar;
	switch(type)
	{
		case BAND:	targetArraySize = 24;
					destination = bandArray;
					controlChar = "B";
					eepromBaseAddress = EEPROM_BAND_BASE_ADDRESS;
					break;
		case TX_DISABLE:
					targetArraySize = 12;
					destination = txDisableArray;
					controlChar = "D";
					eepromBaseAddress = EEPROM_TX_DISABLE_BASE_ADDRESS;
					break;
		default:	panic(INVALID_SYNC_PARAMETERS, String(type) + ", " + String(data[0])); break;	
	};
	
	
	bool changedFlag = 0;
	bool invalidFlag = 0;
	for(int i = 0; i<targetArraySize; i++)
	{
		if(data[i] != destination[i])
		{
				changedFlag = 1;
				destination[i] = data[i];
		}
	}
	
	if(changedFlag)
	{
		#ifdef DEBUG
			PC.print(controlChar == "B" ? "Band: " : "Tx Disable: ");
		#endif
		
		setting.bandhop = 0;
		
		//Update EEPROM
		for(int i = 0; i<targetArraySize; i++)
		{
			#ifdef DEBUG
				if(i != 0) PC.print(", ");
				PC.print(String(destination[i]));
			#endif
			eeprom.write(eepromBaseAddress+i, destination[i]);
			if(destination[i] != destination[0]) setting.bandhop = 1;	
		}
		
		#ifdef DEBUG
			PC.println(""); //line feed after band array
			if(type == BAND) PC.println(String("Bandhopping: ") + ((setting.bandhop == 1) ? "Yes" : "No"));
		#endif
		
		//Set updated flag
		updatedFlags |= (1<<type);
		//Update Pi
		if(updatePi)
		{
			piUart->print(controlChar);
			for(int i = 0; i<targetArraySize; i++)
			{	
				if(i!=0) piUart->print(",");
				piUart->print(destination[i], HEX);
			}
			piUart->print(";\n");
		}
	}
}
