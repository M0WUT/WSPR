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

void panic(String message, uint8_t error_code);
void panic(String message);
void panic(int message);
void register_lcd_for_panic(DogLcd *new_lcd);

#endif