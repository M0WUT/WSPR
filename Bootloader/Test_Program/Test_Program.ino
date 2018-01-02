#include "config.h"

#define DOT_DELAY 250 // Time that 1 dot lasts in ms

void dit()
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(DOT_DELAY);
  digitalWrite(LED_BUILTIN, LOW); 
  delay(DOT_DELAY);
}

void dah()
{
  digitalWrite(LED_BUILTIN, HIGH);
  delay(3*DOT_DELAY);
  digitalWrite(LED_BUILTIN, LOW);
  delay(DOT_DELAY); 
}

void space()
{
  delay(4*DOT_DELAY); //Space is normally 3 dot lengths, went for 5 to make it easier to guess where you are in the sequence. 1 delay has already been provided by the inter-character space at the end of the previous character
}


void setup()
{
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop()
{
  //Yes this is horrendous but not worth wasting time doing nicely.
  dah();
  dah();
  
  space();
  
  dah();
  dah();
  dah();
  dah();
  dah();
  
  space();
  
  dit();
  dah();
  dah();
  
  space();
  
  dit();
  dit();
  dah();
  
  space();
  
  dah();
  
  space();
}

