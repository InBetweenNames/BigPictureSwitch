# Yet Another Big Picture Audio Switcher

Based off the excellent work of [Big Picture Audio Switch](https://github.com/cinterre/BigPictureAudioSwitch/).

# Motivation

I noticed "high CPU usage" when running the original BPAS.  There's something going on with the C# AudioSwitcher library used by that.
After trying to debug it a bit, I realized it would be easier to just use the COM interfaces directly in C++.

# Differences

* This is native code in C++, not C#, which is a bit easier to work with because audio switching in Windows needs to be done through COM anyway
	* And there is no native C# library to do this other than AudioSwitcher, which depends on .NET Framework, etc...
* Zero dependencies other than Windows 7 and above
* Event based rather than polling based
	* Receives events for window creation and destruction and then just matches that against Steam to detect Big Picture mode
* Efficiency mode support

# Installation

Place somewhere you won't delete it, and run it.  You can mark it to start with Windows.

# Usage

* Clicking the system tray icon will allow you to select your "Big Picture" audio device.

