#ifndef Si5351H
#define Si5351H

#if ARDUINO >= 100
 #include "Arduino.h"
#else
 #include "WProgram.h"
#endif

#include <Wire2.h>

#define Si5351_ADDRESS 0b1100000

#define OUTPUT_ENABLE_REG 3
#define ALL_CLOCKS_DISABLED 0xFF
#define PLL_REG 15
#define INPUT_DIV_1 0
#define INPUT_DIV_2 64
#define INPUT_DIV_4 128

#define CLOCK_POWER_DOWN 0x80
#define GPS_ENABLED 0xDEADBEEF
#define XTAL_REG 183
enum si5351_capacitance {NA, XTAL_6pF, XTAL_8pF, XTAL_10pF};


#define PLL_A 0
#define PLL_B 1

#define TALKATIVE 0


class Si5351
{
	public:
		void begin(si5351_capacitance xtal_cap, uint32_t xtal_freq, int32_t correction);
		void disable_clock(uint8_t clock);
		void set_freq(uint8_t clock, uint8_t pll, double target_frequency);
		uint32_t resources[2];
		
	private:
		unsigned int bc_solve(double x0, uint64_t &num, uint64_t &den);
		void check_status();
		void I2C_write(uint8_t address, uint8_t data);
		void I2C_write(uint8_t address, uint8_t *data, uint8_t length);
		void I2C_write(uint8_t address, uint8_t data, uint8_t length);
		void stopTransmission();
		uint8_t I2C_read(uint8_t address);
		void set_PLL(uint8_t pll, uint64_t xtal_frequency, uint32_t vco_frequency);
};






#endif