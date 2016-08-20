This is a 3DS homebrew application intended for running under a NCCH(which can be installed via .cia), for booting the *hax payloads. https://smealum.github.io/3ds/  

This app was mostly finished in October 2015, this was finally released on November 25, 2015, due to the custom logo being finished.

This will first attempt to load the payload from SD, if that isn't successful it will then automatically download the payload for your system with HTTP. SD payload loading can be skipped if you hold down the X button. If you don't hold down the Y button, this will write the downloaded payload from HTTP to SD, if it actually downloaded it via HTTP.  

The exact filepath used for the SD payload depends on your system. Since this app can handle writing the payload here itself, writing the payload here manually isn't really needed. Example SD filepath with New3DS 10.1.0-27U: "/hblauncherloader_otherapp_payload_NEW-10-1-0-27-USA.bin". The Old3DS filepath for the same system-version and region as that example is the same, except that "OLD" is used instead of "NEW".

If you want to manually build this, you'll need this: https://github.com/Steveice10/bannertool
If you pass "VERBOSE=1" to make while building, printfs will be enabled which are normally not printed.

Credits:
* 3DSGuy for originally converting the CWAV used by this app's banner, years ago(which seems to be originally from the Wii HBC banner audio?).
* @Substance12 For the icon/banner(#4).
* The ICON_FLAGS used for parental-controls is from this: https://github.com/Steveice10/FBI/commit/585bc289635a1675734d46f03b69f7bd25ed0be2

