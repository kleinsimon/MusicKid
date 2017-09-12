/*
 Name:		KidPlay.ino
 Created:	09.09.2017 22:19:30
 Author:	Simon
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

// Pins for Buttons
#define btnPause 1
#define btnVolUp 2
#define btnVolDown 3
#define btnNext 4
#define btnPrev 5

// Amount of DB for volume change
#define volChange 5

// Eeprom Adresses
#define memAdrVol 11	//one byte, last volume
#define memLastDir 12	//one byte, last Dir 
#define memLastTrack 13	//13 bytes, last Track Name
#define memLastPos 26	//8 bytes, last Position

Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);
uint8_t volume = 20;
const uint8_t maxVol = 10;
const uint8_t minVol = 60;

int curFolderNumber = 2;
char * curFolder = "";

//Stacks for File search
char curFile[13];
char nearest[13];
char tmpFile[13];

//Stacks for Button state and Event state
bool btnState[8];
bool btnTemp[8];
bool btnConsumed[8];

// State of the player
bool stopped = false;
bool paused = false;

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

	musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);

	
	const uint8_t lastFolderNumber = EEPROM.read(memLastDir);
	char lastTrackName[13];
	EEPROM_readAnything(memLastTrack, lastTrackName);

	Serial.print("Last Folder Number: ");
	Serial.println(lastFolderNumber);

	initButtons();
	//printDirectory(SD.open(curFolder),0);

	sprintf(curFolder, "%d", curFolderNumber);

	Serial.print("Current Folder Number: ");
	Serial.println(curFolderNumber);
	Serial.print("Current Folder: ");
	Serial.println(curFolder);
	EEPROM.write(memLastDir, curFolderNumber);

	setVolume(EEPROM.read(memAdrVol));

	if (lastFolderNumber == curFolderNumber) {
		Serial.print("Last played: ");
		Serial.println(lastTrackName);
		strcpy(curFile, lastTrackName);
		playFile();
		long lastPos = 0;
		EEPROM_readAnything(memLastPos, lastPos);
		Serial.print("Resuming at : ");
		Serial.println(lastPos);
		musicPlayer.currentTrack.seek(lastPos);
	}
}

// the loop function runs over and over again until power down or reset
void loop() {
	readButtons();

	if (checkEvent(btnPause)) {
		Serial.println("Pause");
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

	// play next track if nothing plays
	if (!musicPlayer.playingMusic && !stopped && !paused) {
		findNextFile();
		playFile();
	}
	else {
		EEPROM_writeAnything(memLastPos, musicPlayer.currentTrack.position());
	}

	delay(10);
}

bool checkEvent(uint8_t pin) {
	bool r = btnState[pin] && !btnConsumed[pin];
	if (r)	btnConsumed[pin] = true;
	return r;
}

void playFile() {

	char f[20];

	sprintf(f, "%s/%s", curFolder, curFile);

	musicPlayer.startPlayingFile(f);

	Serial.print("Playing '");
	Serial.print(f);
	Serial.println("'");

	safePlayingFile();
}

void safePlayingFile() {
	//writeCharEeprom(curFile, memLastTrack);
	EEPROM_writeAnything(memLastTrack, curFile);
}

void togglePause() {
	paused = !paused;
	musicPlayer.pausePlaying(paused);
}

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

void raiseVolume(uint8_t dif) {
	setVolume(volume + dif);
}

void initButtons() {
	for (uint8_t i = 0; i < 8; i++)
		musicPlayer.GPIO_pinMode(i, INPUT);
}

void readButtons() {
	for (uint8_t i = 0; i < 8; i++) {
		btnTemp[i] = btnState[i];
		btnState[i] = musicPlayer.GPIO_digitalRead(i);
		if (btnTemp[i] && !btnState[i] && btnConsumed[i]) btnConsumed[i] = false; //Reset if now not pressed
	}
}

void plotBtnState() {
	Serial.print("Buttons: [");
	for (uint8_t i = 0; i < 8; i++)
		Serial.print(btnState[i]);
	Serial.println("]");
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
