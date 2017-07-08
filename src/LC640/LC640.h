#ifndef LC640H
#define LC640H 
#include <DSPI.h>

#define write_enable 6
#define write_command 2
#define read_command 3
class LC640
{
	public:
		void write(uint16_t address, uint8_t data, int cs = 1);
		int read(uint16_t address, int cs = 1);
		LC640(int cs = 1);
};
#endif