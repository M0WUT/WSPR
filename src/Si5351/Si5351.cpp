#include "Si5351.h"


// I am aware that this code uses a lot of unhelpful single letter variables which I normally consider poor style.
// This was done so that the naming of values is consistent with the Silicon Labs AN619, detailing how to program the Si5351

unsigned int Si5351::bc_solve(double x0, uint64_t &num, uint64_t &den)
{
	//Expresses x0 as a fraction num/den, err is acceptable error for the expression
	//This is heavily based on answer from StackOverflow user PinkFloyd in this discussion: http://stackoverflow.com/questions/5124743/algorithm-for-simplifying-decimal-to-fractions
	//The routine he suggests is, itself, based on routine by Ian Richards, Continued Fractions without Tears 1981 https://www.maa.org/sites/default/files/pdf/upload_library/22/Allendoerfer/1982/0025570x.di021121.02p0002y.pdf 
	const double error= 1e-10;
	double g = abs(x0);
    uint64_t a = 0;
    uint64_t b = 1;
    uint64_t c = 1;
    uint64_t d = 0;
    uint64_t s;
    uint32_t iter= 0;
	while((abs(((double)num/(double)den)-x0)) > error)
	{
	    s = floor(g);
        num = a + s*c;
        den = b + s*d;
        a = c;
        b = d;
        c = num;
        d = den;
        g = 1.0/(g-(double)s);
		if((iter++>1e3) || (num>1048576) || (den>1048576)) panic(SI5351_DIVIDER_ERROR);
    } 
	return iter;
}

void Si5351::stopTransmission()
{
	
	switch(Wire.endTransmission())
		{
			case 0: break;
			case 2: panic(I2C_NOT_RESPONDING); break;
			default: panic(WEIRD_I2C_ERROR);
		}
	
	
}

uint8_t Si5351::I2C_read(uint8_t address)
{
	uint8_t return_value;
	Wire.beginTransmission(Si5351_ADDRESS);
	Wire.write(address);
	stopTransmission();

	Wire.requestFrom(Si5351_ADDRESS, 1);
	
	while(Wire.available())
	{
		return_value = Wire.read();
	}
	return return_value;
	
}

void Si5351::set_freq(uint8_t clock, uint8_t pll, double target_frequency)
{

	if(clock>2) panic(INVALID_CLOCK);
	//Output frequency = PLL Frequency / (a+b/c)
	//so a+b/c = VCO frequency / target frequency
	double a;
	uint64_t b,c;

	double pll_frequency;
	switch(pll)
	{
		case PLL_A: pll_frequency = this->plla_frequency; break;
		case PLL_B: pll_frequency = this->pllb_frequency; break;
		default: panic(INVALID_PLL);		
	};

	
	a = floor(pll_frequency / target_frequency);
	
	if(a<6 | a>1800 | ((a==1800) & (b>0))) panic(SI5351_DIVIDER_ERROR);
	
	double remainder = pll_frequency - (target_frequency * a);
	remainder /= target_frequency;
	
	bc_solve(remainder, b, c); // Function to convert remainder to best fit fraction
	
	//Calculate actual output frequency
	double actual_output=pll_frequency/(a+(double)b/(double)c);
	
	#ifdef DEBUG
		PC.print("Setting clock ");
		PC.print(clock);
		PC.print(" to ");
		PC.print(target_frequency,8);
		PC.print("Hz, actual frequency: ");
		PC.print(actual_output,8);
		PC.println("Hz");
	#endif
	
	uint32_t intermediate = (uint32_t)128.0*(double)b/(double)c; //To save calculating multiple times
	
	uint32_t p1, p2, p3;
	
	//How Si5351 packs bits
	p1 = 128*(uint32_t)a + intermediate - 512;
	p2 = 128*(uint32_t)b - (uint32_t)c * intermediate;
	p3=c;
	
	uint8_t tx_buffer[8];
	tx_buffer[0] = (p3>>8)&0xFF;
	tx_buffer[1] = (p3&0xFF);
	tx_buffer[2] = (p1>>16)&0x03;
	tx_buffer[3] = (p1>>8)&0xFF;
	tx_buffer[4] = (p1&0xFF);
	tx_buffer[5] = ((p3>>12)&0xF0)|((p2>>16)&0x0F);
	tx_buffer[6] = (p2>>8)&0xFF;
	tx_buffer[7] = (p2&0xFF);
	
	// 8 registers per clock starting at 42 for CLK0
	I2C_write(42+8*clock, tx_buffer, 8);
	
	//Enable Clock
	bool int_mode;
	if((((uint32_t)a%2)==0) & (b==0)) int_mode = 1;
	else int_mode = 0;
	
	tx_buffer[0] = (int_mode<<6) | (pll<<5) | 0x0F;
	
	I2C_write(16+clock, tx_buffer[0]);
	uint8_t reg = I2C_read(OUTPUT_ENABLE_REG);
	reg &= ~(1<<clock);
	I2C_write(OUTPUT_ENABLE_REG, reg);
	
}

void Si5351::I2C_write(uint8_t address, uint8_t data)
{
	Wire.beginTransmission(Si5351_ADDRESS);
	Wire.write(address);
	Wire.write(data);
	stopTransmission();
}

void Si5351::I2C_write(uint8_t address, uint8_t *data, uint8_t length)
{
	Wire.beginTransmission(Si5351_ADDRESS);
	Wire.write(address);
	Wire.write(data, length);
	stopTransmission();
}

void Si5351::I2C_write(uint8_t address, uint8_t data, uint8_t length)
{
	Wire.beginTransmission(Si5351_ADDRESS);
	Wire.write(address);
	for(int i=0; i<length; i++)
	{
		Wire.write(data);
	}
	stopTransmission();
}

void Si5351::set_PLL(uint8_t pll, uint64_t xtal_frequency, uint32_t target_pll_output)
{
	// VCO output = Xtal_freq * (a+b/c)
	if(target_pll_output>600000000 & target_pll_output<900000000);
	else panic(VCO_ERROR);
	uint64_t a,b,c;
	a=target_pll_output/xtal_frequency;
	double remainder = target_pll_output - (xtal_frequency * a);
	remainder /= xtal_frequency;
	bc_solve(remainder, b, c);
	
	if (b==0 & a%2==0) I2C_write(22+pll, 1<<6); //Integer mode allows for lower phase noise (allegedly!)
	//Calculate actual PLL output frequency
	uint32_t actual_pll_output = (uint32_t)(a*xtal_frequency+(b*xtal_frequency)/c);
	
	
	if(pll==PLL_A)
	{
		this->plla_frequency = actual_pll_output;
		#ifdef DEBUG 
			PC.print("Attempting to set PLL A to ");
		#endif
	}
	else if(pll==PLL_B)
	{
		this->pllb_frequency = actual_pll_output;
		#ifdef DEBUG
			PC.print("Attempting to set PLL B to ");
		#endif
	}
	else panic(INVALID_PLL);
	
	#ifdef DEBUG
		PC.print(target_pll_output);
		PC.print("Hz, Actual Frequency: ");
		PC.print(actual_pll_output);
		PC.println("Hz");
	#endif
	
	
	//Ensure actual frequency is within 10Hz of target (May edit this if it's unreasonable)
	if(target_pll_output-actual_pll_output>10) //Actual will always be lower as integer division truncates
		panic(SI5351_DIVIDER_ERROR); 
	
	//a+b/c must be >=15 and <=90
	if(a<15 | a>90 | (a==90 & b>0)) panic (SI5351_DIVIDER_ERROR);
	
	uint32_t p1, p2, p3;
	uint32_t intermediate = (uint32_t)(128.0*(double)b/(double)c); //value that is used more than once and is reasonably expensive to compute
	
	//These are what they are because data sheet says so, not for any sane reason conceivable to man
	p1=(uint32_t)(128*(uint32_t)a+intermediate-512);
	p2=(uint32_t)(128*(uint32_t)b-(uint32_t)c*intermediate);
	p3=c;
	
	
	//Be careful if you use the datasheet for this bit. There are SOOO many errors between register map and details
	//Have tested this so edit at your own risk
	uint8_t tx_buffer[8];
	tx_buffer[0] = (p3>>8)&0xFF;
	tx_buffer[1] = (p3&0xFF);
	tx_buffer[2] = (p1>>16)&0x03;
	tx_buffer[3] = (p1>>8)&0xFF;
	tx_buffer[4] = (p1&0xFF);
	tx_buffer[5] = (((p3>>12)&0xF0)|((p2>>16)&0x0F));
	tx_buffer[6] = (p2>>8)&0xFF;
	tx_buffer[7] = (p2&0xFF);
	
	//8 registers for PLL A starting at address 26, followed by 8 for PLL B (so this will start at 34)
	I2C_write(26+pll*8, tx_buffer, 8);

}

void Si5351::begin(si5351_capacitance xtal_cap, uint32_t xtal_freq, int32_t correction)
{ 
	
	Wire.begin();
	
	/////////////////////
	//Reset all outputs//
	/////////////////////
	
	//Disable all outputs
	I2C_write(OUTPUT_ENABLE_REG, ALL_CLOCKS_DISABLED);
	
	//Full Si5351 have 8 clocks, control registers for which are in 16-23
	//Power Down all clocks
	I2C_write(16, CLOCK_POWER_DOWN, 8); 
	
	////////////////////////////////////////////
	//Set Load capacitance for XTAL Oscillator//
	////////////////////////////////////////////
	if((xtal_cap!=0) && (xtal_cap<4)) I2C_write(XTAL_REG, ((xtal_cap<<6)| 0b010010));
	else panic(INCORRECT_CAPACITANCE); // Incorrect capacitance specified
	
	////////////////////////////////////////////////////////////////////////////////////////
	//Set input clock divider, clock must be 10-40MHz, recommended 25MHz/27MHz fundamental//
	////////////////////////////////////////////////////////////////////////////////////////
	
	if((10000000<xtal_freq) && (xtal_freq<40000000));
	else panic(INCORRECT_XTAL_FREQ);
	
	if(correction != GPS_ENABLED) xtal_freq=xtal_freq-(xtal_freq*correction)/1000000;
	
	////////////////////////////////////////////////////////////////////////
	//Hard code both PLLs to be 800Mhz, if you want to edit this feel free//
	////////////////////////////////////////////////////////////////////////
	set_PLL(PLL_A, xtal_freq, 800000000);
	set_PLL(PLL_B, xtal_freq, 800000000);
	I2C_write(177, 0xAC); //Soft reset both PLL
	
} 
void Si5351::disable_clock(uint8_t clock)
{
	if(clock>7) panic(INVALID_CLOCK);
	uint8_t x = I2C_read(OUTPUT_ENABLE_REG);
	x |= (1<<clock);
	I2C_write(OUTPUT_ENABLE_REG, x);
}