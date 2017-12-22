////////////////////////////
//Libraries - DO NOT TOUCH//
////////////////////////////

#include "src/WSPR_encode/WSPR_encode.h"
#include "src/Si5351/Si5351.h"
#include "src/TinyGPS/TinyGPS.h"
#include "src/panic/panic.h"
#include "src/DogLcd/DogLcd.h"
#include "src/LC640/LC640.h"
#include "src/maidenhead/maidenhead.h"

//////////////////////////////
//Definitions - DO NOT TOUCH//
//////////////////////////////

#define GPS Serial1
#define PC Serial
#define RPI Serial0
#define SECOND 6e5 //Rough number of loops per second, a counter up to this value will take (VERY) roughly 1 second
#define LED 1
#define TIMEOUT 2*SECOND
#define TIMER_ENABLED 1<<15
#define NO_PRESCALER 0
#define MODE_32_BIT_TIMER 1<<3
#define EXTERNAL_SOURCE 2
#define GPS_PPS 0
#define GPS_PPS_INTERRUPT 2
#define PIN_A0 0
#define MENU_BTN 16
#define EDIT_BTN 19
#define EEPROM_CS 28
#define PI_WATCHDOG 3


//////////////////////
//Enabled debug mode//
//////////////////////

#define DEBUG 1 //set to 0 for normal mode
#define SKIP_CALIBRATION 1

//////////////////////////////
//Memory Addresses in EEPROM//
//////////////////////////////
#define EEPROM_CALLSIGN_BASE_ADDRESS 0
#define EEPROM_LOCATOR_BASE_ADDRESS 10
#define EEPROM_POWER_ADDRESS 16
#define EEPROM_TX_PERCENTAGE_ADDRESS 17
#define EEPROM_DATE_FORMAT_ADDRESS 18
#define EEPROM_BAND_BASE_ADDRESS 19
#define EEPROM_TX_DISABLE_BASE_ADDRESS 44

///////////////////////
//Configuration stuff//
///////////////////////
#define LCD_CONTRAST 35 //0-63




