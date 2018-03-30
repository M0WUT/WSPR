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
#include "src/supervisor/supervisor.h"

#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
#endif

//////////////////////////////
//Definitions - DO NOT TOUCH//
//////////////////////////////

#define GPS Serial1
#define PC Serial
#define RPI Serial0
#define TIMEOUT 2000 //Timeout for Pi traffic in ms
#define LED 1
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
#define BAND0 13
#define BAND1 12
#define BAND2 11
#define TX 6


/////////////////
//Debug Options//
/////////////////

//These check whether variable is defined rather than value so comment out to disable
#define DEBUG 1 
//#define OSC_ENABLED 1
#define SKIP_CALIBRATION 1


///////////////////////
//Configuration stuff//
///////////////////////
#define LCD_CONTRAST 35 //0-63
#define GPS_TIMEOUT 2000 //Time with no GPS activity before we assume GPS lock has been lost in ms
#define PI_TIMEOUT 5000 //Pi Timeout in ms



