# BlackPillSprints

STM32F401 (aka "Black Pill") Arduino code for dual pulse counting with hall effect/TMR sensors.
Uses hardware timers for counting, which in theory will reliably count beyond the rated switching speed of most sensors.
Tested at up to 80kph (~15 ms/rotation).

Meant to replace the aging [OpenSprints hardware](https://www.opensprints.com/howto_upgrade_opensprints_hub_arduino.php).
Software compatibility with [OpenSprints firmware](https://github.com/opensprints/opensprints-comm/tree/master/arduino/racemonitor) and [SilverSprint](https://github.com/cwhitney/SilverSprint) desktop application is WIP.

STL for a pair of enclosures that will fit in the OpenSprints mounts can be found [on MakerWorld](https://makerworld.com/en/models/1363938-blackpillsprints-hardware-enclosure).
Use any 3 conductor wire to connect them; I used 3.5mm TRS jacks and a stereo audio cable.
