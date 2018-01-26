#ifndef panicH
#define panicH

///////////////////////////////////////////
//Allow use with Arduino or Chipkit PIC32//
///////////////////////////////////////////
#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
#endif

#include "../DogLcd/DogLcd.h"
#include "WSPR_config.h"

#define INCORRECT_UART_TERMINATION 7 //DEBUG
#define PI_INCOMPLETE_TRANSMISSON 7 //DEBUG
#define PI_UNKNOWN_CHARACTER 7 //DEBUG
#define TIME_SYNC_FAILED 7 //DEBUG
#define INVALID_SYNC_PARAMETERS 7 //DEBUG
#define PI_UART_NOT_REGISTERED 7 //DEBUG
#define GPS_UART_NOT_REGISTERED 7 //DEBUG
#define PI_NOT_RESPONDING 7 //DEBUG
#define INVALID_STATE_ACCESSED 7 //DEBUG

void panic(String message, uint8_t error_code);
void panic(String message);
void panic(int message);
void panic(int message, int value);
void register_lcd_for_panic(DogLcd *new_lcd);

#endif