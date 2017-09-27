/*
 Name:		KidPlay.ino
 Created:	09.09.2017 22:19:30
 Author:	Simon Klein (simon.klein@outlook.com)
*/
#include <EEPROM.h>
#include <SPI.h>
#include <Adafruit_VS1053.h>
#include <SD.h>
#include <Arduino.h>
#include "EEPROMAnything.h"

// These are the pins used for the music maker shield
#define SHIELD_RESET  -1      // VS1053 reset pin (unused!)
#define SHIELD_CS     7      // VS1053 chip select pin (output)
#define SHIELD_DCS    6      // VS1053 Data/command select pin (output)
#define CARDCS 4     // Card chip select pin
#define DREQ 3       // VS1053 Data request, ideally an Interrupt pin
#define KEEPALIVE 5 //Pin for keep-alive-resistor (~ 150 OHM)

// Pins for the Folder ID Switches. Order is lowest bit to highest bit. The more pins, the more folders can be addressed.
const uint8_t fidBtnPins[6] = {8,9,10,11,12,13};

// Pins for Buttons
#define btnPause 3
#define btnVolUp 1
#define btnVolDown 2
#define btnNext 5
#define btnPrev 4

// Eeprom Adresses
#define memAdrVol 11	//one byte, last volume
#define memLastDir 12	//one byte, last Dir 
#define memLastTrack 13	//13 bytes, last Track Name
#define memLastPos 26	//8 bytes, last Position

// Amount of DB for volume change
#define volChange 5
#define maxVol 10
#define minVol 60

// cycle sleep
#define loopSleep 10

// Delay before switching to new folder in loops
#define switchDelay 100

// Keep-Alive durations
#define loopKeepAlive 200
#define keepAliveDuration 50

Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);

// Stacks for File search
char curFile[13];
char nearest[13];
char tmpFile[13];

// Stacks for Button state and Event state
bool btnState[8];
bool btnTemp[8];
bool btnConsumed[8];

// State of the player
bool stopped = false;
bool paused = false;

int delayTimer = 0;

#if KEEPALIVE > 0
	int keepAliveTimer = 0;
	int keepAliveCounter = 0;
#endif

uint8_t volume = 40;
uint8_t curFolderNumber = 0;
uint8_t lastFolderNumber = 0;
char lastTrackName[13];
char curFolder[3];

void setup() {
	Serial.begin(9600);
	Serial.println("Initializing musicplayer...");

	if (!musicPlayer.begin()) { // initialise the music player
		Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
		while (1); // don't do anything more
	}

	if (!SD.begin(CARDCS)) {
		Serial.println(F("SD failed, or not present"));
		while (1); // don't do anything more
	}

	// Initialization
	initButtons();
	musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);
	
	// Read last state from eeprom
	lastFolderNumber = EEPROM.read(memLastDir);

	EEPROM_readAnything(memLastTrack, lastTrackName);

	Serial.print("Last Folder Number: ");
	Serial.println(lastFolderNumber);

	// Restore volume
	setVolume(EEPROM.read(memAdrVol));

	initDir();

	// If same folder as last time, restore track and position
	if (lastFolderNumber == curFolderNumber) {
		Serial.print("Last played: ");
		Serial.println(lastTrackName);
		strcpy(curFile, lastTrackName);
		long lastPos = 0;
		EEPROM_readAnything(memLastPos, lastPos);
		Serial.print("Resuming at : ");
		Serial.println(lastPos);
		playFile();
		musicPlayer.pausePlaying(true);
		musicPlayer.currentTrack.seek(lastPos);
		musicPlayer.pausePlaying(false);
	}

#if KEEPALIVE > 0
	pinMode(KEEPALIVE, OUTPUT);
	toggleKeepAlive(true);
#endif
}

// Main loop
void loop() {
	readButtons();

	if (checkEvent(btnPause)) {
		togglePause();
	}
	else if (checkEvent(btnNext)) {
		musicPlayer.stopPlaying();
	}
	else if (checkEvent(btnPrev)) {
		musicPlayer.stopPlaying();
		findPrevFile();
		playFile();
	}
	else if (checkEvent(btnVolUp)) {
		raiseVolume(-volChange);
	}
	else if (checkEvent(btnVolDown)) {
		raiseVolume(volChange);
	}

	int fidState = readFidState();
	if (fidState != curFolderNumber) {
		if (delayTimer > switchDelay) {
			musicPlayer.stopPlaying();
			curFolderNumber = fidState;
			initDir();
			stopped = false;
			delayTimer = 0;
		} 
		else {
			delayTimer++;
		}
	}
	else {
		delayTimer = 0;
	}

	// play next track if nothing plays
	if (!musicPlayer.playingMusic && !stopped && !paused) {
		findNextFile();
		playFile();
	}
	else {
		EEPROM_writeAnything(memLastPos, musicPlayer.currentTrack.position());
	}

#if KEEPALIVE < 0
	if (keepAliveTimer > loopKeepAlive) {
		if (keepAliveCounter == 0) {
			toggleKeepAlive(true);
		}
		keepAliveCounter++;
		if (keepAliveCounter > keepAliveDuration) {
			keepAliveCounter = 0;
			keepAliveTimer = 0;
			toggleKeepAlive(false);
		}
	}
	keepAliveTimer++;
#endif

	delay(loopSleep);
}

void initDir() {

	strcpy(curFile, "");

	// Read Folder ID from GPIOs
	curFolderNumber = readFidState();
	sprintf(curFolder, "%d", curFolderNumber);

	//printDirectory(SD.open(curFolder),0);

	Serial.print("Current Folder Number: ");
	Serial.println(curFolderNumber);
	Serial.print("Current Folder: ");
	Serial.println(curFolder);

	// Store current folder number in eeprom
	EEPROM.write(memLastDir, curFolderNumber);
}

#if KEEPALIVE > 0
void toggleKeepAlive(bool kaState) {
	digitalWrite(KEEPALIVE, kaState);
	Serial.print("Keepalive: ");
	Serial.println(kaState);
}
#endif

//Check for an event and consume it
bool checkEvent(uint8_t pin) {
	bool r = btnState[pin] && !btnConsumed[pin];
	if (r)	btnConsumed[pin] = true;
	return r;
}

//Play the current file in the current folder. Store filename in eeprom
void playFile() {
	char f[20];

	sprintf(f, "%s/%s", curFolder, curFile);

	musicPlayer.startPlayingFile(f);

	Serial.print("Playing '");
	Serial.print(f);
	Serial.println("'");

	EEPROM_writeAnything(memLastTrack, curFile);
}

// Pause / unpause music
void togglePause() {
	paused = !paused;
	Serial.print("Paused: ");
	Serial.println(paused);
	musicPlayer.pausePlaying(paused);
}

// Sets volume to the given value but keeps the limits given by maxVol and minVol (lower is louder)
void setVolume(uint8_t v) {
	if (v < maxVol)
		v = maxVol;
	else if (v > minVol)
		v = minVol;

	Serial.print("Set Volume to ");
	Serial.println(v);
	musicPlayer.setVolume(v, v);
	volume = v;
	EEPROM.write(memAdrVol, volume);
}

// Raise the volume by dif (negative for louder)
void raiseVolume(uint8_t dif) {
	setVolume(volume + dif);
}

// Initialize all Buttons on the shield and the FID-Buttons on arduino
void initButtons() {
	for (uint8_t i = 0; i < 8; i++)
		musicPlayer.GPIO_pinMode(i, INPUT);
	for (uint8_t i = 0; i < sizeof(fidBtnPins); i++)
		pinMode(fidBtnPins[i], INPUT_PULLUP);
}

// Read the shield buttons state and reset event state
void readButtons() {
	for (uint8_t i = 0; i < 8; i++) {
		btnTemp[i] = btnState[i];
		btnState[i] = musicPlayer.GPIO_digitalRead(i);
		if (btnTemp[i] && !btnState[i] && btnConsumed[i]) btnConsumed[i] = false; //Reset if now not pressed
	}
}

// Read the fid state to a number. Low means pressed (internal pullup)
int readFidState() {
	bool fidTmp[6];
	int res = 0;
	//Serial.print("FID State: ");
	for (uint8_t i = 0; i < sizeof(fidBtnPins); i++) {
		bitWrite(res, i, digitalRead(fidBtnPins[i]));
		//Serial.print(!digitalRead(fidBtnPins[i]));

	}
	//Serial.println(" ");
	return res;
}

// Print the FID-State to Serial
void plotBtnState() {
	Serial.print("Buttons: [");
	for (uint8_t i = 0; i < 8; i++)
		Serial.print(btnState[i]);
	Serial.println("]");
}

// Find the alphabetically next file after the current one
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

		if (!isValidExt(tmpFile)) {
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

// Find the alphabetically previous file before the current one
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

		if (!isValidExt(tmpFile)) {
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

// Check if the file is a valid music file
bool isValidExt(char* filename) {
	int8_t len = strlen(filename);

	return strstr(filename + (len - 4), ".MP3")
		|| strstr(filename + (len - 4), ".AAC")
		|| strstr(filename + (len - 4), ".WMA")
		|| strstr(filename + (len - 4), ".WAV")
		|| strstr(filename + (len - 4), ".FLA")
		|| strstr(filename + (len - 4), ".MID");
}

/// File listing helper
void printDirectory(File dir, int numTabs) {
	while (true) {

		File entry = dir.openNextFile();
		if (!entry) {
			// no more files
			//Serial.println("**nomorefiles**");
			break;
		}
		for (uint8_t i = 0; i<numTabs; i++) {
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