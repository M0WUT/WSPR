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

#include <WSPR_config.h>

void panic(DogLcd lcd, String message, uint8_t error_code);
void panic(String message);

#endif