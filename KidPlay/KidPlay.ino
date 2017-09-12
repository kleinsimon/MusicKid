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

const char * curFolder = "2";
char curFile[13];
char nearest[13];
char tmpFile[13];

bool stopped = false;

// the setup function runs once when you press reset or power the board
void setup() {
	Serial.begin(9600);
	Serial.println("Initializing musicplayer...");

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

	//printDirectory(SD.open(curFolder),0);
	strcpy(curFile, "26-CIR~1.MP3");
}

// the loop function runs over and over again until power down or reset
void loop() {
	// play next track if nothing plays

	if (!musicPlayer.playingMusic && !stopped) {
		//findNextFile();
		findPrevFile();

		//Serial.println(curFile);
		//playFile(curFile);
	}

	delay(10);
}

void playFile(const char * fn) {

	char f[20];

	sprintf(f, "%s/%s", curFolder, fn);

	musicPlayer.startPlayingFile(f);

	Serial.print("Playing '");
	Serial.print(f);
	Serial.println("'");
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

void findNextFile() {
	strcpy(nearest, "");
	File root = SD.open(curFolder);

	Serial.print("Finding File in '");
	Serial.print(root.name());
	Serial.print("' following: ");
	Serial.println(curFile);
	root.rewindDirectory();
	bool set = false;

	while (true) {
		File entry = root.openNextFile();
		if (!entry) {
			Serial.println("No more files...");
			break;
		}
		if (entry.isDirectory()) {
			Serial.println(" ... skipping Dir");
			entry.close();
			continue;
		}
		
		strcpy(tmpFile, entry.name());
		entry.close();

		if (!isFnMusic(tmpFile)) {
			Serial.print(" ... non music File: ");
			Serial.println(tmpFile);
			continue;
		}

		if (strcmp(tmpFile, curFile) <= 0) {
			continue;
		}

		if (!set || strcmp(tmpFile, nearest) < 0) {
			set = true;
			strcpy(nearest, tmpFile);
		}
	}
	if (!set) {
		stopped = true;
		Serial.println("No more Tracks... stopping");
	}
	else {
		Serial.print("Next Track: ");
		Serial.println(nearest);
		strcpy(curFile, nearest);
	}
	root.close();
}

void findPrevFile() {
	strcpy(nearest, "");
	File root = SD.open(curFolder);

	Serial.print("Finding File in '");
	Serial.print(root.name());
	Serial.print("' before: ");
	Serial.println(curFile);
	root.rewindDirectory();
	bool set = false;

	while (true) {
		File entry = root.openNextFile();
		if (!entry) {
			Serial.println("No more files...");
			break;
		}
		if (entry.isDirectory()) {
			Serial.println(" ... skipping Dir");
			entry.close();
			continue;
		}

		strcpy(tmpFile, entry.name());
		entry.close();

		if (!isFnMusic(tmpFile)) {
			Serial.print(" ... non music File: ");
			Serial.println(tmpFile);
			continue;
		}

		if (strcmp(tmpFile, curFile) >= 0) {
			continue;
		}

		if (!set || strcmp(tmpFile, nearest) > 0) {
			set = true;
			strcpy(nearest, tmpFile);
		}
	}
	if (!set) {
		stopped = true;
		Serial.println("No more Tracks... stopping");
	}
	else {
		Serial.print("Next Track: ");
		Serial.println(nearest);
		strcpy(curFile, nearest);
	}
	root.close();
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
	if (strstr(filename + (len - 4), ".MP3")
		// and anything else you want
		) {
		result = true;
	}
	else {
		result = false;
	}
	return result;
}
