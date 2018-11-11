# MusicKid
Arduino Firmware for interactive MP3 Playback... used for a child-friendly mp3-player where the folder is selected via RFID TAG ID.

This sktech is intended for a child-friendly MP3-Player, which is based on a Arduino Leonardo with the Adafruit MusicMaker Shield and a RC-522 RFID module.
Controls are implemented via the Shields GPIO-Ports. 

When a RFID tag is found, the player searches for a folder that begins with its hexadecimal ID. It then plays the mp3s there in their alphabetic order. Tested with mifare classic and ultralight, may propably work with others.

Controls: Pause, Vol up, Vol down, Next Track, Prev Track

Limitations:
- By now, only the first 6-8 digits are taken in account, which sometimes leads to ambigous folder selection. This is due to limitations of the Fat-library used and may be fixed in the future.
- non-ASCII-chars in the files prevents playback
- since the whole folder is buble-sorted when searching the next file, playback in big folders may delay