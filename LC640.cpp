#include <DSPI.h>
#include <LC640.h>

DSPI1 spi;

LC640::LC640(int cs)
{
	spi.begin();
	pinMode(cs,OUTPUT);
	digitalWrite(cs,HIGH);
}

void LC640::write(uint16_t address, uint8_t data, int cs)
{
  digitalWrite(cs,LOW);
  spi.transfer(write_enable);
  digitalWrite(cs,HIGH);
  delay(1);
  digitalWrite(cs,LOW);
  spi.transfer(write_command);
  spi.transfer((address>>8)&0xFF);
  spi.transfer(address&0xFF);
  spi.transfer(data);
  digitalWrite(cs,HIGH);
}


int LC640::read(uint16_t address, int cs)
{
  digitalWrite(cs,LOW);
  spi.transfer(read_command);
  spi.transfer((address>>8)&0xFF);
  spi.transfer(address&0xFF);
  int x = spi.transfer(0x00); //send 0 to generate clock pulses for receiver
  digitalWrite(cs, HIGH);
  return x;
}
