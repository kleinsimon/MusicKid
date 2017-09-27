# MusicKid
Arduino Firmware for interactive MP3 Playback... used for a child-friendly mp3-player


This sktech is intended for a child-friendly MP3-Player, which is based on a Arduino Leonardo with the Adafruit MusicMaker Shield.
Controls are implemented via the Shields GPIO-Ports.

By now playback from a predefined Folder works, later the folder will be defined by an external code, given by other buttons.
Then all MP3s (or other files supported by VS1053) in the Folder with a given Number are played sequentially. 

If the same Folder is played again, the last played track is resumed at the last played position, stored in EEPROM.

Controls: Pause, Vol up, Vol down, Next Track, Prev Track
