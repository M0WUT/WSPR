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
#include "WSPR_config.h"

enum wsprMode {WSPR_NORMAL=0, WSPR_EXTENDED=1};
namespace WSPR
{
	int encode(String callsign, String locator, int power, char *wsprSymbols, wsprMode encoding_mode);
	/*
	callsign: string containing callsign for TX/RX with letters in UPPERCASE and numbers as ASCII
	locator: string containing 4 or 6 character Maidenhead locater with letters in UPPERCASE and numbers as ASCII
	power: in dBm
	wsprSymbols: 162 long char array for the results to be stored in 
	wsprMode: use WSPR_NORMAL for most encoding, if need to generate a 2nd set of symbols for encoded WSPR, set to WSPR_EXTENDED
	*/
}
#endif



