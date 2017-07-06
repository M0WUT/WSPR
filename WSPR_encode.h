#ifndef WSPR_encodeH
#define WSPR_encodeH

//////////////////////////////
//Functions for external use//
//////////////////////////////
#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
#endif
#define TALKATIVE 1

#include <panic.h>
#include <DogLcd.h>

enum WSPR_mode {WSPR_NORMAL=0, WSPR_EXTENDED=1};
namespace WSPR{
	int encode(char *callsign, char *locator, int power, char *wspr_symbols, WSPR_mode encoding_mode, DogLcd lcd);
	/*
	callsign: pointer to array/string containing callsign for TX/RX with letters in UPPERCASE and numbers as ASCII
	locator: pointer to 4 or 6 character Maidenhead locater with letters in UPPERCASE and numbers as ASCII
	Both Callsign and locator must be null terminated
	wspr_symbols: 162 long char array for the results to be stored in 
	*/
}
#endif



