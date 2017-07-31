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



void panic(String message, uint8_t error_code);
void panic(String message);
void register_lcd_for_panic(DogLcd *new_lcd);

#endif