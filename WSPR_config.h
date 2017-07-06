////////////////////////////
//Libraries - DO NOT TOUCH//
////////////////////////////

#include "WSPR_encode.h"
#include "Si5351.h"
#include "TinyGPS++.h"
#include "panic.h"
#include "DogLcd.h"
#include "maidenhead.h"

//////////////////////////////
//Definitions - DO NOT TOUCH//
//////////////////////////////

#define GPS Serial1
#define PC Serial
#define RPI Serial0
#define SECOND 4e5
#define LED 1
#define TIMEOUT 1*SECOND
#define TIMER_ENABLED 1""15
#define NO_PRESCALER 0
#define MODE_32_BIT_TIMER 1""3
#define EXTERNAL_SOURCE 2
#define GPS_PPS 0
#define GPS_PPS_INTERRUPT 2
#define PIN_2 0
#define RPI_UART_PROD 7
#define MENU_BTN 16
#define EDIT_BTN 13

//////////////////////////////





