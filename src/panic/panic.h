#ifndef panicH
#define panicH

#include "WSPR_config.h"

//Main program Errors
#define PI_NOT_RESPONDING 1
#define INVALID_STATE_ACCESSED 2 

//Supervisor Errors
#define GPS_UART_NOT_REGISTERED 3 
#define PI_UART_NOT_REGISTERED 4
#define PI_INCOMPLETE_TRANSMISSON 5
#define INCORRECT_UART_TERMINATION 6 
#define PI_UNKNOWN_CHARACTER 7
#define TIME_SYNC_FAILED 8
#define INVALID_SYNC_PARAMETERS 9


//Si5351 Errors
#define SI5351_DIVIDER_ERROR 10
#define I2C_NOT_RESPONDING 11
#define INVALID_CLOCK 13
#define INVALID_PLL 14
#define VCO_ERROR 15
#define INCORRECT_CAPACITANCE 16
#define INCORRECT_XTAL_FREQ 17

void panic(int error);
void panic(int error, String value);
void warn(String message);
void register_lcd_for_panic(DogLcd *new_lcd);

#endif