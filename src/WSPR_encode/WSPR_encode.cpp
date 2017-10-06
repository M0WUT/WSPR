#include "WSPR_encode.h"
//Function for encoding Callsign, Locator and Power information for WSPR Transmission
//Expects callsign  and locator as null-terminated arrays and returns results in wspr_symbols 


/*Limitations: 	maximum 6 character callsign, NO '/' operators (i.e. TF/M0WUT) even if under 6 chars
				Callsign must have a number as the second or third character i.e. 3XY1T will fail
				These constraints are imposed by basic WSPR protocol
*/

static int mix(uint32_t x, uint32_t k)
{
	return(((x)<<(k)) | ((x)>>(32-(k))));
}

static int WSPR_hash(char *callsign, uint8_t callsign_length)
{
	uint32_t a,b,c;
	a = 0xdeadbeef + (uint32_t)callsign_length + 146; //At this point, you should know better than to ask why
	b=a;
	c=a;
	switch(callsign_length)
    {
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
	return (parity&1);;
}

static int ischar(char x)
{
	if (x>='A' && x<='Z') return 1;
	else return 0;
}

static int is_loc_char(char x) //Locator characters constrained to A-R
{
	if (x>='A' && x<='R') return 1;
	else return 0;
}

static int is_sub_loc_char(char x) //Subsquare Locator characters constrained to A-X
{
	if (x>='a' && x<='x') return 1;
	else return 0;
}

int isnum(char x)
{
	if (x>='0' && x<='9') return 1;
	else return 0;
}

int WSPR::encode(String callsign, String locator, int power, char *wspr_symbols, WSPR_mode encoding_mode)
{

	/////////////////////////////////
	//Prepare callsign for encoding//
	/////////////////////////////////

	char prepped_callsign[6];
	uint8_t callsign_length, stored_callsign_length, locator_length, slash_location=0;
	enum mode_type {NORMAL, SINGLE_SUFFIX, DOUBLE_SUFFIX, PREFIX, HASH};
	mode_type mode = NORMAL;
	int pre_pad=0; //Number of padding spaces before callsign
	uint32_t encoded_callsign=0;
	char callsign_modifier[3], trimmed_callsign[6], stored_callsign[12];
	
	/* If the callsign does not fit into a standard WSPR message (6 character callsign, 4 digit locator, power)
	a second set of characters is generated which is the locator converted to comply with the callsign rules by moving the first letter to the end
	i.e. using AB12CD would become a callsign of B12CDA, the locator is replaced with a 15 bit hash of the callsign and the power is left as is. Yuck! */
	
	if (encoding_mode == WSPR_EXTENDED)
	{ 
		if(!is_loc_char(locator[4])) return 1;
		for (int i =0; i<11; i++)
		{
			if(callsign[i] == '\0')
			{	
				stored_callsign_length = i;
				break;
			}
			stored_callsign[i]=callsign[i]; //Copy callsign to stored_callsign
			if (i==10) return 2;
		}
		
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
	
	//Check callsign is less than or equal to 11 characters (Can be at most 3 prefix characters, a '/', 6 character callsign and NULL termination)
	for(int i =0; i<11; i++)
	{
		if(callsign[i]=='\0') //Have reached end
		{
			callsign_length=i;
			if (i==0) return 16;
			break;
		}
		if(callsign[i]=='/') //Have to use extended protocol
		{
			if (slash_location > 0) return 3; 
			slash_location=i;
		}
		if((i==10) || (slash_location==0 && i==6)) return 4;
	}
	
	if(slash_location == 0)
	{
		if (encoding_mode == WSPR_EXTENDED) mode = HASH;
		else mode = NORMAL; //Normal WSPR message
		
		for(int i=0; i<callsign_length; i++)
		{
			trimmed_callsign[i]=callsign[i]; //Copy the main body of the callsign (in this case, all of it) to trimmed_callsign
		}
	}
	else if(slash_location == callsign_length-2) //Single character suffix (letter or number)
	{
		callsign_modifier[0] = callsign[callsign_length-1];
		mode = SINGLE_SUFFIX;
		callsign_length -= 2; //Remove length due to suffix from callsign_length
		if(isnum(callsign_modifier[0]) || ischar(callsign_modifier[0]));
		else return 5;
		for(int i = 0; i<slash_location; i++) //Copy the main body of the callsign to trimmed_callsign
		{
			trimmed_callsign[i]=callsign[i];
		}
	}
	else if(slash_location == callsign_length-3) //Double number suffix (10-99)
	{
		callsign_modifier[0] = callsign[callsign_length-2];
		callsign_modifier[1] = callsign[callsign_length-1];
		mode = DOUBLE_SUFFIX;
		callsign_length -= 3; //Remove length due to suffix from callsign_length
		if(isnum(callsign_modifier[0]) && isnum(callsign_modifier[1])); //Check both are numbers
		else return 6;
		for(int i = 0; i<slash_location; i++) //Copy the main body of the callsign to trimmed_callsign
		{
			trimmed_callsign[i]=callsign[i];
		}
	}
	else if(slash_location<4)//1-3 letter prefix
	{
		mode = PREFIX;
		for(int i=0; i<3-slash_location; i++) //If prefix, it requires padding with spaces before the prefix to make it 3 characters
		{
			callsign_modifier[i]=' ';
		}
		for (int i=0; i<slash_location; i++)
		{
			char x = callsign[i];
			if(ischar(x) || isnum(x))
			{
				callsign_modifier[i+3-slash_location]=callsign[i];
			}
			else return 7;
		}
		for (int i = slash_location + 1; i<callsign_length; i++)
		{
			trimmed_callsign[i-(slash_location+1)] = callsign[i]; //Copy the main body of the callsign to trimmed_callsign
		}
		callsign_length -= slash_location;
		callsign_length -= 1;
	}
	else return 8;
	
	
	//Check all characters in main body of the callsign are number or capital letters
	for(int i=0; i<callsign_length; i++)
	{
		if(isnum(trimmed_callsign[i]) || ischar(trimmed_callsign[i]))
		{
			;//Valid char, do nothing
		}
		else
		{
			return 9;
		}
	}
	
	
	if(isnum(trimmed_callsign[2]))
	{
		pre_pad = 0; //No padding needed
	}
	else
	{
		if(isnum(trimmed_callsign[1]))
		{
			pre_pad=1; //Pad with a single preceding space
		}
		else
		{
			return 10;
			return 0;
		}
	}
	
	if(pre_pad==1) prepped_callsign[0]=' ';
	for(int i = 0; i<callsign_length; i++)
	{
		prepped_callsign[i+pre_pad]=trimmed_callsign[i];
	}
	for(int i=pre_pad+callsign_length; i<6;i++)
	{
		prepped_callsign[i]=' ';
	}
	
	for(int i =3; i<6; i++)
	{
		if(isnum(prepped_callsign[i])) return 11; //Enforced by the WSPR spec
	}
	
	///////////////////
	//Encode callsign//
	///////////////////
	for(int i =0; i<6; i++)
	{
		prepped_callsign[i]=char_encode(prepped_callsign[i]);
	}
	
	encoded_callsign=prepped_callsign[0]*36+prepped_callsign[1];
	encoded_callsign=encoded_callsign*10+prepped_callsign[2];
	
	for(int i =0; i<3; i++)
	{
		encoded_callsign=encoded_callsign*27+(prepped_callsign[3+i]-10);
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
	for(int i =0; i<7; i++)
	{
		if(locator[i]=='\0')
		{
			locator_length=i;
			break;
		}
	}
	
	if((is_loc_char(locator[0]) && is_loc_char(locator[1]) && isnum(locator[2]) && isnum(locator[3]) && ((locator_length==4) || ((locator_length==6) && is_sub_loc_char(locator[4]) && is_sub_loc_char(locator[5]))))==0)
	{
		return 13;
	}
	

	/////////////////////////////////////////
	//Encode the second part of the message//
	/////////////////////////////////////////
	
	/*For a normal WSPR message, this is an encoded form of the 4 digit locator
	  For a WSPR message with a suffix or a prefix, it is an encoded form of that (called callsign_modifier in the code)
	*/
	
	uint32_t second_part = 0;
	switch (mode)
	{
		case NORMAL: 		locator[0]-='A'; //Convert locator from string to values
							locator[1]-='A';
							locator[2]-='0';
							locator[3]-='0';
							second_part = (179 - 10 * locator[0] - locator[2] ) * 180 + 10 * locator[1] + locator[3]; 
							second_part = 128 * second_part + (uint32_t)power + 64; //because that is what it is
							break; 
								
		case SINGLE_SUFFIX: if(ischar(callsign_modifier[0]))
							{
								second_part = 60000 - 32768 + callsign_modifier[0] - 65 + 10; 
							}
							else
							{
								second_part = 60000 - 32768 + callsign_modifier[0] - 48; 
							}
							power += 2; //As the power can only end in 0,3,7 adding 2 indicates that it is a non-standard message
							second_part = 128 * second_part + (uint32_t)power + 64;
							break;
							
		case DOUBLE_SUFFIX: callsign_modifier[0] -= '0';
							callsign_modifier[1] -= '0';
							second_part = 60000 + 26 + 10*callsign_modifier[0] + callsign_modifier[1];
							power +=2;
							second_part = 128 * second_part + (uint32_t)power + 64;
							break;
							
		case PREFIX:		for (int i=0; i<3; i++)
							{
								second_part=37*second_part + char_encode(callsign_modifier[i]);
							}
							if(second_part>=32768)
							{
								second_part -= 32768;
								power += 1;
							}
							power+=1;
							second_part = 128 * second_part + (uint32_t)power + 64;
							break;
							
		case HASH:			second_part = WSPR_hash(stored_callsign, stored_callsign_length);
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
		
		if(j<162) 	// If reversed i <162, move the current bit to the jth bit of wspr_symbols. This will happen 162 times exactly as i counts			
		//from 0-255 as the reverse of 0-162 must all be 8 bits values 
		{
			wspr_symbols[j]=unshuffled[current_bit];
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
									
	//Finally, ith 'bit' of wspr_symbols = ith bit of unsynced wspr_symbols *2 + ith bit of sync vector so will be 0-3
	for(int i =0; i<162; i++)
	{
		wspr_symbols[i]=(wspr_symbols[i]<<1) | sync_vector[i];
	}
	
	#if TALKATIVE
		Serial.println("WSPR Encoding successful");
	#endif
	if (mode == NORMAL) return 0;
	else return 21;
}


