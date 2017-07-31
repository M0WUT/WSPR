#include <DSPI.h>
#include "LC640.h"

DSPI1 spi;

LC640::LC640(int cs)
{
	this->cs = cs;
	spi.begin();
	pinMode(this->cs,OUTPUT);
	digitalWrite(this->cs,HIGH);
}

void LC640::write(uint16_t address, uint8_t data)
{
  digitalWrite(this->cs,LOW);
  spi.transfer(write_enable);
  digitalWrite(this->cs,HIGH);
  delay(1);
  digitalWrite(this->cs,LOW);
  spi.transfer(write_command);
  spi.transfer((address>>8)&0xFF);
  spi.transfer(address&0xFF);
  spi.transfer(data);
  digitalWrite(this->cs,HIGH);
}


int LC640::read(uint16_t address)
{
  digitalWrite(this->cs,LOW);
  spi.transfer(read_command);
  spi.transfer((address>>8)&0xFF);
  spi.transfer(address&0xFF);
  int x = spi.transfer(0x00); //send 0 to generate clock pulses for receiver
  digitalWrite(this->cs, HIGH);
  return x;
}
