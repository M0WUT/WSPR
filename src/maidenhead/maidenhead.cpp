#include "maidenhead.h"
String maidenhead(TinyGPSPlus *gps)
{
	String locator = "      ";
	double latminutes = 60*(gps->location.lat()*(gps->location.rawLat().negative ? -1.0 : +1.0) + 90.0);
    double lngminutes = 60*(gps->location.lng()*(gps->location.rawLng().negative ? -1.0 : +1.0) + 180.0); 
   
    int temp = lngminutes/1200;
    locator[0]=(char)(temp+'A');
    lngminutes -= temp*1200;
    temp = latminutes/600;
    locator[1]=(char)(temp+'A');
    latminutes -= temp*600;

    temp = lngminutes/120;
    locator[2]=(char)(temp+'0');
    lngminutes -= temp*120;
    temp = latminutes/60;
    locator[3]=(char)(temp+'0');
    latminutes -= temp*60;

    temp = lngminutes/5;
    locator[4]=(char)(temp+'a');
    temp = (int)(latminutes/2.5);
    locator[5]=(char)(temp+'a');
	
	return locator;
}