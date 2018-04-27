#include "WSPR_encode.h"
//Function for encoding Callsign, Locator and Power information for WSPR Transmission
//Expects callsign  and locator as null-terminated arrays and returns results in wsprSymbols 


/*Limitations: 	maximum 6 character in the main callsign, 
				Callsign must have a number as the second or third character i.e. 3XY1T will fail
				These constraints are imposed by basic WSPR protocol
*/

static int mix(uint32_t x, uint32_t k)
{
	return((x<<k) | (x>>(32-k)));
}

static int WSPR_hash(String callsign)
{
	uint32_t a,b,c;
	a = 0xdeadbeef + (uint32_t)callsign.length() + 146; //At this point, you should know better than to ask why
	b=a;
	c=a;
	switch(callsign.length())
    {
		//Yes, all this monstrous fallthrough is deliberate
		case 12: 	c+=((uint32_t)callsign[11])<<24;
		case 11: 	c+=((uint32_t)callsign[10])<<16;
		case 10: 	c+=((uint32_t)callsign[9])<<8;
		case 9 : 	c+=callsign[8];
		case 8 : 	b+=((uint32_t)callsign[7])<<24;
		case 7 : 	b+=((uint32_t)callsign[6])<<16;
		case 6 : 	b+=((uint32_t)callsign[5])<<8;
		case 5 : 	b+=callsign[4];
		case 4 : 	a+=((uint32_t)callsign[3])<<24;
		case 3 : 	a+=((uint32_t)callsign[2])<<16;
		case 2 : 	a+=((uint32_t)callsign[1])<<8;
		case 1 : 	a+=callsign[0]; break;
		case 0 : 	return 15;
		default: 	return 17;
    }
	c ^= b; c -= mix(b,14); 
	a ^= c; a -= mix(c,11); 
	b ^= a; b -= mix(a,25); 
	c ^= b; c -= mix(b,16); 
	a ^= c; a -= mix(c,4);  
	b ^= a; b -= mix(a,14); 
	c ^= b; c -= mix(b,24); 
	return (c & 32767);
}

static int char_encode(char x)
{
	//Performs mapping from callsign characters to values used for generating values for coding
	if(x==' ') return 36; //Space encoded as 36
	if((x>='0') && (x<='9')) return (x-'0'); //Numbers encoded as value
	return (x-'A'+10); //else letter which is mapped from 10-35, don't need to worry about out of bound as already checked in function
}

static int parity_bit(uint32_t x)
{
	int parity = 0;
	for(int i = 0; i < 32; i++)
	{
		parity = parity ^ (x & 0x01);
		x = x >> 1;
	}
	return (parity & 1);
}

static int is_char(char x) {return (x>='A' && x<='Z');}

static int is_loc_char(char x) {return (x>='A' && x<='R');} //Locator characters constrained to A-R

static int is_sub_loc_char(char x) {return (x>='a' && x<='x');}//Subsquare Locator characters constrained to A-X

static int is_num(char x) {return (x>='0' && x<='9');}

int WSPR::encode(String callsign, String locator, int power, char *wsprSymbols, wsprMode encodingMode)
{

	/////////////////////////////////
	//Prepare callsign for encoding//
	/////////////////////////////////

	enum mode_type {NORMAL, SINGLE_SUFFIX, DOUBLE_SUFFIX, PREFIX, HASH};
	mode_type mode = NORMAL;
	uint32_t encoded_callsign=0;
	String callsignModifier, mainCallsign;
	
	/* If the callsign does not fit into a standard WSPR message (6 character callsign, 4 digit locator, power)
	a second set of characters is generated which is the locator converted to comply with the callsign rules by moving the first letter to the end
	i.e. using AB12CD would become a callsign of B12CDA, the locator is replaced with a 15 bit hash of the callsign and the power is left as is. Yuck! */
	
	if (encodingMode == WSPR_EXTENDED)
	{ 
		if(locator.length() != 6) return 1;

		//Replace callsign with modified locator as discussed above
		locator[4] -=32; //Convert to capital in ASCII
		locator[5] -=32;
		
		for (int i=0; i<5; i++)
		{
			callsign[i]=locator[i+1];
		}
		callsign[5] = locator[0];
		callsign[6] = '\0';
	}
	
	
	//Check callsign is less than or equal to 10 characters (Can be at most 3 prefix characters, a '/' and a 6 character callsign)
	if(callsign.length() > 10) return 4;
	
	int slashCount = 0;
	for(int i = 0; i < locator.length(); i++)
		if(locator[i] == '/')
				slashCount++;
	if(slashCount > 1) return 3;
	
	int slashLocation = locator.indexOf('/');
	
	if(slashLocation == -1) //No slash found
	{
		mode = (encodingMode == WSPR_EXTENDED ? HASH : NORMAL);
		if(callsign.length() > 6) return 4;
		mainCallsign = callsign;
	}
	
	else if(slashLocation == callsign.length()-2) //Single character suffix (letter or number)
	{
		callsignModifier = callsign[slashLocation + 1];
		mode = SINGLE_SUFFIX;
		if(!is_num(callsignModifier[0]) && !is_char(callsignModifier[0])) return 5;
		mainCallsign = callsign.substring(0, callsign.length() - 2); //Remove '/' and suffix
	}
	
	else if(slashLocation == callsign.length()-3) //Double number suffix (10-99)
	{
		callsignModifier = callsign.substring(slashLocation + 1);
		mode = DOUBLE_SUFFIX;
		if(is_num(callsignModifier[0]) && (callsignModifier[0] != '0') && is_num(callsignModifier[1])) return 6; //Check in range 10 - 99
		mainCallsign = callsign.substring(0, callsign.length() - 3); //Remove '/' and 2 char suffix
	}
	
	else if(slashLocation < 4) //1-3 letter prefix
	{
		mode = PREFIX;
		callsignModifier = callsign.substring(0, slashLocation);
		for (int i=0; i<callsignModifier.length(); i++)
		{
			char x = callsignModifier[i];
			if(!is_num(x) || is_char(x))
				return 7;
		}
		mainCallsign = callsign.substring(slashLocation + 1);
	}
	
	else return 8;
	
	
	//Check all characters in main body of the callsign are number or capital letters
	for(int i=0; i< mainCallsign.length(); i++)
		if(!is_num(mainCallsign[i]) || is_char(mainCallsign[i]))
			return 9;
	
	//Callsign must have a number as the 3rd character, can be padded with a space at start if needed
	if (!is_num(mainCallsign[2]))
	{		
		//Not got a number in 3rd char, check if 2nd char is a number, if so space-pad else error
		if(is_num(mainCallsign[1]))
			mainCallsign = " " + mainCallsign;
		else
			return 10;
	}
	
	//mainCallsign must be 6 characters long, pad with spaces
	while(mainCallsign.length() < 6)
		mainCallsign += ' ';
	
	
	for(int i =3; i<6; i++)
	{
		if(is_num(mainCallsign[i])) return 11; //Last 3 char must not be numbers - WSPR spec
	}
	
	///////////////////
	//Encode callsign//
	///////////////////
	for(int i =0; i<6; i++)
	{
		mainCallsign[i]=char_encode(mainCallsign[i]);
	}
	
	encoded_callsign=mainCallsign[0]*36+mainCallsign[1];
	encoded_callsign=encoded_callsign*10+mainCallsign[2];
	
	for(int i =0; i<3; i++)
	{
		encoded_callsign=encoded_callsign*27+(mainCallsign[3+i]-10);
	}
	
	//////////////////////////////
	//Prepare power for encoding//
	//////////////////////////////
	
	//Check power figure is valid (in range 0-60 and ends in 0, 3, 7)
	if((power<=60) && ((power%10==0) || (power%10==3) || (power%10==7)))
	{
		;//Good
	}
	else
	{
		return 12;
	}
	////////////////////////////////
	//Prepare locator for encoding//
	////////////////////////////////
	if((is_loc_char(locator[0]) && is_loc_char(locator[1]) && is_num(locator[2]) && is_num(locator[3]) && ((locator.length()==4) || ((locator.length()==6) && is_sub_loc_char(locator[4]) && is_sub_loc_char(locator[5]))))==0)
	{
		return 13;
	}
	

	/////////////////////////////////////////
	//Encode the second part of the message//
	/////////////////////////////////////////
	
	/*For a normal WSPR message, this is an encoded form of the 4 digit locator
	  For a WSPR message with a suffix or a prefix, it is an encoded form of that (called callsignModifier in the code)
	*/
	
	uint32_t second_part = 0; //WSPR message is callsign and a 2nd part containg locator / power
	switch (mode)
	{
		case NORMAL: 		locator[0]-='A'; //Convert locator from string to values
							locator[1]-='A';
							locator[2]-='0';
							locator[3]-='0';
							second_part = (179 - 10 * locator[0] - locator[2] ) * 180 + 10 * locator[1] + locator[3]; 
							second_part = 128 * second_part + (uint32_t)power + 64; //because that is what it is
							break; 
								
		case SINGLE_SUFFIX: if(is_char(callsignModifier[0]))
							{
								second_part = 60000 - 32768 + callsignModifier[0] - 65 + 10; 
							}
							else
							{
								second_part = 60000 - 32768 + callsignModifier[0] - 48; 
							}
							power += 2; //As the power can only end in 0,3,7 adding 2 indicates that it is a non-standard message
							second_part = 128 * second_part + (uint32_t)power + 64;
							break;
							
		case DOUBLE_SUFFIX: callsignModifier[0] -= '0';
							callsignModifier[1] -= '0';
							second_part = 60000 + 26 + 10*callsignModifier[0] + callsignModifier[1];
							power +=2;
							second_part = 128 * second_part + (uint32_t)power + 64;
							break;
							
		case PREFIX:		for (int i=0; i<3; i++)
							{
								second_part=37*second_part + char_encode(callsignModifier[i]);
							}
							if(second_part>=32768)
							{
								second_part -= 32768;
								power += 1;
							}
							power+=1;
							second_part = 128 * second_part + (uint32_t)power + 64;
							break;
							
		case HASH:			second_part = WSPR_hash(callsign);
							second_part = 128 * second_part - (uint32_t)(power+1) + 64;
							break;
							
		default:			return 14;
	}
	
	///////////////////
	//Pack into bytes//
	///////////////////
	int encoded_data[11];
	
	encoded_data[3] = ((encoded_callsign & 0x0F) << 4);
	encoded_callsign = encoded_callsign >> 4;
	encoded_data[2] = (encoded_callsign & 0xFF);
	encoded_callsign = encoded_callsign >> 8;
	encoded_data[1] = (encoded_callsign & 0xFF);
	encoded_callsign = encoded_callsign >> 8;
	encoded_data[0] = (encoded_callsign & 0xFF);
	
	encoded_data[6] = ((second_part & 0x03) << 6);
	second_part = second_part >> 2;
	encoded_data[5] = (second_part & 0xFF);
	second_part = second_part >> 8;
	encoded_data[4] = (second_part & 0xFF);
	second_part = second_part >> 8;
	encoded_data[3] |= (second_part & 0x0F);
	encoded_data[7] = 0;
	encoded_data[8] = 0;
	encoded_data[9] = 0;
	encoded_data[10] = 0;
	
	#if TALKATIVE
	for(int i =0; i<7; i++)
	{
		Serial.print(encoded_data[i],HEX);
		Serial.print(" ");
	}
	#endif
	
	///////////////////////////////////
	//Convolve encoded data with taps//
	///////////////////////////////////
	
	uint32_t reg0=0;
	int unshuffled[162];
	int data=0;
	uint32_t tapped0, tapped1;
	for(int i=0; i<81; i++)
	{
		int byte_number=i/8;
		int bit_number=i%8;
		
		data=(encoded_data[byte_number]>>(7-bit_number))&1;
		
		reg0=(reg0<<1)+data; //Append data to LSB of register
	
		
		tapped0 = reg0 & 0xF2D05351; //Mask the register two different ways, the two masks are just what is defined in WSPR spec
		tapped1 = reg0 & 0xE4613C47;	
		
		
		unshuffled[2*i]=parity_bit(tapped0); //Append XOR parity bit of tapped0 to unshuffled	
		unshuffled[(2*i)+1]=parity_bit(tapped1); //Append XOR parity bit of tapped1 to unshuffled
			
	}
	
	//////////////////////////////////////
	//Horrendous bit shuffling technique//
	//////////////////////////////////////
	uint8_t current_bit=0; //current bit being shuffled
	for(uint8_t i=0; i<255; i++)
	{
		//Reverse the bits in i
		//Yes, using this technique is nasty (but reasonably efficient)
		uint8_t j = (i & 0xF0) >> 4 | (i & 0x0F) << 4;
		j = (j & 0xCC) >> 2 | (j & 0x33) << 2;
		j = (j & 0xAA) >> 1 | (j & 0x55) << 1;
		
		if(j<162) 	// If reversed i <162, move the current bit to the jth bit of wsprSymbols. This will happen 162 times exactly as i counts			
		//from 0-255 as the reverse of 0-162 must all be 8 bits values 
		{
			wsprSymbols[j]=unshuffled[current_bit];
			current_bit++;
		}
	}
									
	//////////////////////////////////////////
	//Combine shuffled bits with sync vector//
	//////////////////////////////////////////
	
	const char sync_vector[162]=	{1,1,0,0,0,0,0,0,1,0,0,0,1,1,1,0,0,0,1,0,0,1,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,1,0,1,0,0,
									0,0,0,0,1,0,1,1,0,0,1,1,0,1,0,0,0,1,1,0,1,0,0,0,0,1,1,0,1,0,1,0,1,0,1,0,0,1,0,0,1,0,
									1,1,0,0,0,1,1,0,1,0,1,0,0,0,1,0,0,0,0,0,1,0,0,1,0,0,1,1,1,0,1,1,0,0,1,1,0,1,0,0,0,1,
									1,1,0,0,0,0,0,1,0,1,0,0,1,1,0,0,0,0,0,0,0,1,1,0,1,0,1,1,0,0,0,1,1,0,0,0};
									
	//Finally, ith 'bit' of wsprSymbols = ith bit of unsynced wsprSymbols *2 + ith bit of sync vector so will be 0-3
	for(int i =0; i<162; i++)
	{
		wsprSymbols[i]=(wsprSymbols[i]<<1) | sync_vector[i];
	}
	
	#if TALKATIVE
		Serial.println("WSPR Encoding successful");
	#endif
	if (mode == NORMAL) return 0;
	else return 21;
}


