/*
 Name:		KidPlay.ino
 Created:	09.09.2017 22:19:30
 Author:	Simon
*/
// include SPI, MP3 and SD libraries
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>

// These are the pins used for the music maker shield
#define SHIELD_RESET  -1      // VS1053 reset pin (unused!)
#define SHIELD_CS     7      // VS1053 chip select pin (output)
#define SHIELD_DCS    6      // VS1053 Data/command select pin (output)

// These are common pins between breakout and shield
#define CARDCS 4     // Card chip select pin
// DREQ should be an Int pin, see http://arduino.cc/en/Reference/attachInterrupt
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin

// Eprom Adresses
const int adrVol = 11;

Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);
uint8_t volume = 20;
const uint8_t maxVol = 10;
const uint8_t minVol = 40;

char * fileName[99];
char tempString[13];
byte numberElementsInArray = 0;

// the setup function runs once when you press reset or power the board
void setup() {
	Serial.begin(9600);
	Serial.println("Initializing musicplayer... sine test");

	if (!musicPlayer.begin()) { // initialise the music player
		Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
		while (1);
	}

	if (!SD.begin(CARDCS)) {
		Serial.println(F("SD failed, or not present"));
		while (1);  // don't do anything more
	}

	setVolume(EEPROM.read(adrVol));
	musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

	//printDirectory(SD.open("/"), 0);

	const char * fon = "1";
	readFiles(SD.open(fon));
	sortFileArray();

	char pBuff[255];
	sprintf(pBuff, "%s/%s", fon, fileName[0]);

	Serial.print("Playing '");
	Serial.print(pBuff);
	Serial.println("'");

	musicPlayer.startPlayingFile(pBuff);
	Serial.println("Done");
}

// the loop function runs over and over again until power down or reset
void loop() {
	
}

char * joinPath(char * folder, char * file) {
	char path[255];
	sprintf(path, "/%s/%s", folder, file);
	return path;
}

void togglePause() {
	musicPlayer.pausePlaying(!musicPlayer.paused());
}

void setVolume(uint8_t v) {
	if (v < maxVol)
		v = maxVol;
	else if (v > minVol)
		v = minVol;

	Serial.print("Set Volume to ");
	Serial.println(v);
	musicPlayer.setVolume(volume, volume);
	volume = v;
	EEPROM.write(adrVol, volume);
}

void raiseVolume(uint8_t dif) {
	setVolume(volume + dif);
}

void readFiles(File root) {

	freeMessageMemory();  // Start by freeing previously allocated malloc pointers

	File entry;
	while ((entry = root.openNextFile()) != false) {
		sprintf(tempString, "%s", entry.name());
		numberElementsInArray++;
		fileName[numberElementsInArray - 1] = (char *)malloc(13);
		//checkMemory();
		sprintf(fileName[numberElementsInArray - 1], "%s", tempString);
	}
}

void printDirectory(File dir, int numTabs) {
	while (true) {
		File entry = dir.openNextFile();
		if (!entry) {
			// no more files
			break;
		}
		for (uint8_t i = 0; i < numTabs; i++) {
			Serial.print('\t');
		}
		Serial.print(entry.name());
		if (entry.isDirectory()) {
			Serial.println("/");
			printDirectory(entry, numTabs + 1);
		}
		else {
			// files have sizes, directories do not
			Serial.print("\t\t");
			Serial.println(entry.size(), DEC);
		}
		entry.close();
	}
}

bool isFnMusic(char* filename) {
	int8_t len = strlen(filename);
	bool result;
	if (strstr(strlwr(filename + (len - 4)), ".mp3")
		|| strstr(strlwr(filename + (len - 4)), ".aac")
		|| strstr(strlwr(filename + (len - 4)), ".wma")
		|| strstr(strlwr(filename + (len - 4)), ".wav")
		|| strstr(strlwr(filename + (len - 4)), ".fla")
		|| strstr(strlwr(filename + (len - 4)), ".mid")
		// and anything else you want
		) {
		result = true;
	}
	else {
		result = false;
	}
	return result;
}


// -----------------------------------------------------------------------------------------------
// switchArray - This function takes the element in the array that we are dealing with, and 
// switches the pointers between this one and the previous one.
// -----------------------------------------------------------------------------------------------
void switchArray(byte value)
{
	// switch pointers i and i-1, using a temp pointer. 
	char *tempPointer;

	tempPointer = fileName[value - 1];
	fileName[value - 1] = fileName[value];
	fileName[value] = tempPointer;
}


// -------------------------------------------------------------------------
// This is a real neat function. It decides whether the first string is "less than"
// or "greater than" the previous string in the array. Input 2 pointers to chars;
// if the function returns 1 then you should switch the pointers.
// -------------------------------------------------------------------------
//
// Check 2 character arrays; return FALSE if #2 > 1; 
// return TRUE if #2 > #1 for the switch. Return 1 = TRUE, 0 = FALSE
byte arrayLessThan(char *ptr_1, char *ptr_2)
{
	char check1;
	char check2;

	int i = 0;
	while (i < strlen(ptr_1))		// For each character in string 1, starting with the first:
	{
		check1 = (char)ptr_1[i];	// get the same char from string 1 and string 2

									//Serial.print("Check 1 is "); Serial.print(check1);

		if (strlen(ptr_2) < i)    // If string 2 is shorter, then switch them
		{
			return 1;
		}
		else
		{
			check2 = (char)ptr_2[i];
			//   Serial.print("Check 2 is "); Serial.println(check2);

			if (check2 > check1)
			{
				return 1;				// String 2 is greater; so switch them
			}
			if (check2 < check1)
			{
				return 0;				// String 2 is LESS; so DONT switch them
			}
			// OTHERWISE they're equal so far; check the next char !!
			i++;
		}
	}

	return 0;
}

// -----------------------------------------------------------------------
// This is the guts of the sort function. It's also neat.
// It compares the current element with each previous element, and switches
// it to it's final place.
// -----------------------------------------------------------------------
void sortFileArray()
{

	int innerLoop;
	int mainLoop;

	for (mainLoop = 1; mainLoop < numberElementsInArray; mainLoop++)
	{
		innerLoop = mainLoop;
		while (innerLoop >= 1)
		{
			if (arrayLessThan(fileName[innerLoop], fileName[innerLoop - 1]) == 1)
			{
				// Serial.print("Switching ");
				// Serial.print(fileName[innerLoop]);
				// Serial.print(" and ");
				// Serial.println(fileName[innerLoop-1]);

				switchArray(innerLoop);
			}
			innerLoop--;
		}
	}
}

// -----------------------------------------------------------------------
// You remember we have to free the malloc's ? Well, it's a simple function.
// The pointer points to nothing, and the memory that was used is 
// -----------------------------------------------------------------------
void freeMessageMemory()
{
	// If we have previous messages, then free the memory
	for (byte i = 1; i <= numberElementsInArray; i++)
	{
		free(fileName[i - 1]);
	}

	numberElementsInArray = 0;

}

// ---------------------------------------------------------------------------
// This is the part you care least about; how to populate the char array in the first place.
// I have taken from this code from an MP3 project where I read the fies in a directory into
// that array of pointers.
// ---------------------------------------------------------------------------
//