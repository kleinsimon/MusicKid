/*
 Name:		KidPlay.ino
 Created:	09.09.2017 22:19:30
 Author:	Simon Klein (simon.klein@outlook.com)
*/
#include <require_cpp11.h>
//#include <MFRC522Hack.h>
//#include <MFRC522Extended.h>
//#include <MFRC522Debug.h>
#include <MFRC522.h>
#include <deprecated.h>
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

// RFID 522 Pins:
#define RFID_RST_PIN -1
#define RFID_SS_PIN 10

// Pins for Buttons: VolUp, VolDown, Pause, Prev, Next
#define nBtnPins 5
uint8_t btnPins[nBtnPins] = {12,13,11,8,9};

// assignment of buttons in array
#define btnVolUp 0
#define btnVolDown 1
#define btnPause 2
#define btnPrev 3
#define btnNext 4

// Eeprom Adresses
#define memVolLocked 10	//one byte, Volume locked
#define memAdrVol 11	//one byte, last volume
#define memLastDir 12	//one byte, last Dir 
#define memLastTrack 13	//13 bytes, last Track Name
#define memLastPos 26	//8 bytes, last Position
#define memLastID 34 //10 bytes, rfid card ID
#define memLastIDLen 45 //10 bytes, rfid card ID

// Amount of DB for volume change, max and min volume (lower value is higher volume)
#define volChange 5
#define maxVol 20
#define minVol 60

// cycle sleep
#define loopSleep 10

// Buffers for File search
char curFile[13];
char nearest[13];
char tmpFile[13];

// Stacks for Button state and Event state
bool btnState[nBtnPins];
bool btnTemp[nBtnPins];
bool btnConsumed[nBtnPins];

// State of the player
bool stopped = false;
bool paused = false;

// Runtime vars
uint8_t volume = 40;
char lastTrackName[13];
char curFolder[20];
byte curCardID[10] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
int curCardIDlen = 4;

// Interface objects for shields
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(SHIELD_RESET, SHIELD_CS, SHIELD_DCS, DREQ, CARDCS);
MFRC522 rfid = MFRC522(RFID_SS_PIN, RFID_RST_PIN);

void setup() {
	Serial.begin(9600);
	Serial.println("Initializing musicplayer...");
	SPI.begin();
	rfid.PCD_Init();
	rfid.PCD_DumpVersionToSerial();

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
	EEPROM_readAnything(memLastID, curCardID);
	EEPROM_readAnything(memLastIDLen, curCardIDlen);
	EEPROM_readAnything(memLastTrack, lastTrackName);

	Serial.print("Last Card ID Length: ");
	Serial.println(curCardIDlen);
	Serial.print("Last Card ID: ");
	printHex(curCardID, curCardIDlen);
	Serial.println("");
	initDir();

	// Restore volume
	setVolume(EEPROM.read(memAdrVol));

	// If same folder as last time, restore track and position
	Serial.print("Last played: ");
	Serial.println(lastTrackName);
	strcpy(curFile, lastTrackName);
	if (playFile()) {
		long lastPos = 0;
		EEPROM_readAnything(memLastPos, lastPos);
		Serial.print("Resuming at : ");
		Serial.println(lastPos);
		musicPlayer.pausePlaying(true);
		musicPlayer.currentTrack.seek(lastPos);
		musicPlayer.pausePlaying(false);
	}
}

// Main loop
void loop() {
	
	// Check action buttons
	readButtons();
	if (checkEvent(btnPause)) {
		togglePause();
	}
	else if (checkEvent(btnNext)) {
		musicPlayer.stopPlaying();
		findNextFile();
		playFile();
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

	// Check for new ID card
	if (rfid.PICC_IsNewCardPresent()) {
		if (readRfid()) {
			Serial.println("RFID read...");
			musicPlayer.stopPlaying();
			initDir();
			stopped = false;
		}
	}

	// play next track if nothing plays
	if (!musicPlayer.playingMusic && !stopped && !paused) {
		findNextFile();
		playFile();
	}
	else {
		EEPROM_writeAnything(memLastPos, musicPlayer.currentTrack.position());
	}

	delay(loopSleep);
}

// Read the rfid card id and store in curCardID. if new ID, return true
bool readRfid() {
	if (!rfid.PICC_ReadCardSerial()) return;

	MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);

	// Check is the PICC of Classic MIFARE type
	if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
		piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
		piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
		Serial.println(F("Your tag is not of type MIFARE Classic."));
		return false;
	}

	if (byteCmp(curCardID, rfid.uid.uidByte, rfid.uid.size)) return false;

	Serial.print(F("PICC type: "));
	Serial.println(rfid.PICC_GetTypeName(piccType));

	Serial.println(F("A new card has been detected."));

	// Store NUID into nuidPICC array
	byteCopy(rfid.uid.uidByte, curCardID, rfid.uid.size);
	curCardIDlen = rfid.uid.size;

	Serial.println(F("The NUID tag is:"));
	Serial.print(F("In hex: "));
	printHex(curCardID, curCardIDlen);
	Serial.println();
	Serial.print(F("In dec: "));
	printDec(curCardID, curCardIDlen);
	Serial.println();

	rfid.PICC_HaltA();
	rfid.PCD_StopCrypto1();


	return true;
}

// Compare first n bytes of arrays b1 and b2
bool byteCmp(byte b1[], byte b2[], int n) {
	bool r = true;

	for (int i = 0; i < n; i++)
		r &= b1[i] == b2[i];

	return r;
}

// Copy n bytes of array b1 to b2
void byteCopy(byte b1[], byte *b2, int n) {
	for (int i = 0; i < n; i++)
		b2[i] = b1[i];
}

void initDir() {
	strcpy(curFile, "");

	// Read Folder ID 
	cardIdtoHex();

	//printDirectory(SD.open(curFolder),0);

	Serial.print("Current Folder: ");
	Serial.println(curFolder);
	
	// Store current folder number in eeprom
	EEPROM_writeAnything(memLastID, curCardID);
	EEPROM_writeAnything(memLastIDLen, curCardIDlen);
}

// write current card ID to curFolder
static void cardIdtoHex()
{
	size_t i = 0;
	char *tmp = new char[3];
	for (i = 0; i < 10; ++i) {
		if (i < curCardIDlen) {
			sprintf(tmp, "%02X", curCardID[i]);
			curFolder[i * 2] = tmp[0];
			curFolder[i * 2 + 1] = tmp[1];
		}
		else {
			curFolder[i * 2] = '\0';
			break;
		}
	}
}

//Check for an event and consume it
bool checkEvent(uint8_t pin) {
	bool r = btnState[pin] && !btnConsumed[pin];
	if (r)	btnConsumed[pin] = true;
	return r;
}

//Play the current file in the current folder. Store filename in eeprom
bool playFile() {
	char f[35];

	sprintf(f, "%s/%s", curFolder, curFile);

	if (SD.exists(f)) {
		musicPlayer.startPlayingFile(f);

		Serial.print("Playing '");
		Serial.print(f);
		Serial.println("'");

		EEPROM_writeAnything(memLastTrack, curFile);
		return true;
	}
	else {
		return false;
	}
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

// Initialize all Buttons on arduino
void initButtons() {
	for (uint8_t i = 0; i < nBtnPins; i++)
		pinMode(btnPins[i], INPUT_PULLUP);
}

// Read the shield buttons state and reset event state
void readButtons() {
	for (uint8_t i = 0; i < nBtnPins; i++) {
		btnTemp[i] = btnState[i];
		btnState[i] = !digitalRead(btnPins[i]);
		if (btnTemp[i] && !btnState[i] && btnConsumed[i]) btnConsumed[i] = false; //Reset if now not pressed
	}
}

// Print the FID-State to Serial
void plotBtnState() {
	Serial.print("Buttons: [");
	for (uint8_t i = 0; i < nBtnPins; i++)
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

/**
* Helper routine to dump a byte array as hex values to Serial.
*/
void printHex(byte *buffer, byte bufferSize) {
	for (byte i = 0; i < bufferSize; i++) {
		Serial.print(buffer[i] < 0x10 ? " 0" : " ");
		Serial.print(buffer[i], HEX);
	}
}

/**
* Helper routine to dump a byte array as dec values to Serial.
*/
void printDec(byte *buffer, byte bufferSize) {
	for (byte i = 0; i < bufferSize; i++) {
		Serial.print(buffer[i] < 0x10 ? " 0" : " ");
		Serial.print(buffer[i], DEC);
	}
}