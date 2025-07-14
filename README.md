# Big Picture Switch

Ever want to use your main desktop as a game console using Steam?  Look no further!
Big Picture Switch manages your audio and video devices to make the experience as seamless as possible.

Based off the excellent work of [Big Picture Audio Switch](https://github.com/cinterre/BigPictureAudioSwitch/).

# Usage

![A screenshot of Big Picture Switch](screenshot.png)

-   Clicking the system tray icon will allow you to configure your "Big Picture" devices.
-   Selecting an audio or video device will trigger them to be switched to when Big Picture Mode turns on
-   Clicking a selected item will deselect the device (if no device is selected, no action is taken)
    -   e.g., if you prefer to let Steam manage your displays, set your preferred display and then leave the display unchecked
-   If "Exclude selected display from desktop" is enabled, then the selected device will NOT appear in your desktop configuration
    -   This is ideal for a TV that you don't use unless you're playing games on it.
    -   A best effort is made to do this.
-   No display changes are permanent (i.e. saved to the Windows display database).
    If something weird happens, just close the program and hit Win+P to get back your original configuration.

# Planned features

-   [libcec](https://github.com/Pulse-Eight/libcec) integration to automatically power on/off the display and change its inputs
    -   This one is waiting on me getting my USB HDMI-CEC dongle in the mail

# Motivation

The original [Big Picture Audio Switch](https://github.com/cinterre/BigPictureAudioSwitch/) project that I was using worked
fine if you were willing to extend your desktop onto your TV and then set your preferred display in Steam to it.
However, I found this kind of annoying -- when my TV is off, Windows still sees it as a display and I can "move" my cursor
onto it.  On a technical level, sure, it's a display connected to my system, but on a practical level, I don't use it like
I use my main displays on my deskop.  Also, my windows would get kind of screwed up too -- I'd randomly have windows
open on the TV, and if it was off, it wouldn't be obvious except when I went to go looking for them.  So I wanted a solution
that would manage both the audio _and_ display devices on the system.

Originally I was going to fork BPAS to do this, when I noticed "high CPU usage" when running it.  There's something going on with the C# AudioSwitcher library used by that.
After trying to debug it a bit, I realized it would be easier to just use the COM and Win32 interfaces directly in C++ and use event-based monitoring.
C++ was the most straightforward choice to do this with minimal overhead and bloat.

So... that's how I got to developing Big Picture Switch.

# Differences compared to BPAS

-   This is native code in C++, not C#, which is a bit easier to work with because audio switching in Windows needs to be done through COM anyway
    -   And there is no native C# library to do this other than AudioSwitcher, which depends on .NET Framework, etc...
-   Zero dependencies other than Windows 7 and above (in principle?  Haven't actually tested it.  I use Windows 11 so YMMV).
-   Event based rather than polling based
    -   Receives events for window creation and destruction and then just matches that against Steam to detect Big Picture mode
-   Efficiency mode support (not that it really matters, as the application is idle except when it receives events, but why not?)
-   Manages display configurations _and_ audio configurations

# Installation

Place somewhere you won't delete it, and run it.  You can mark it to start with Windows.
