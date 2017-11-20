#include <DSPI.h>
#include "LC640.h"

DSPI1 spi;

//Please note, some things in this are sub-optimal (delays and clock speed) but changing them causes things to fail
// and I have better things to do than head scratching to solve something that already works. May play with this later.

LC640::LC640(int cs)
{
	this->cs = cs;
	spi.begin(); //Seems to default to 2.93kHz, if the setSpeed is used, clock goes to 25MHz regardless of what is supplied.
	pinMode(this->cs,OUTPUT);
	digitalWrite(this->cs,HIGH);	
}

void LC640::write(uint16_t address, uint8_t data)
{
  digitalWrite(this->cs,LOW);
  spi.transfer(write_enable);
  digitalWrite(this->cs,HIGH);
  delay(5); //For some reason, reducing this causes read operation to fail
  digitalWrite(this->cs,LOW);
  spi.transfer(write_command);
  spi.transfer((address>>8)&0xFF);
  spi.transfer(address&0xFF);
  spi.transfer(data);
  digitalWrite(this->cs,HIGH);
  delay(5);

}


int LC640::read(uint16_t address)
{
  digitalWrite(this->cs,LOW);
  spi.transfer(read_command);
  spi.transfer((address>>8)&0xFF);
  spi.transfer(address&0xFF);
  int x = spi.transfer(0x00); //send 0 to generate clock pulses for receiver
  digitalWrite(this->cs, HIGH);
  delay(5);
  return x;
}

