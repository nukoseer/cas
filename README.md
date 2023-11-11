![](media/cas256x256.png)

# cas

Simple utility for setting CPU affinity for Windows.

You can get the latest build as a zip archive here: [cas.zip](https://raw.githubusercontent.com/wiki/nukoseer/cas/cas.zip)

_Note: It is recommended to run cas.exe as an administrator, some processes may require administrator rights for setting affinity._

# Usage

cas takes process name and affinity mask pairs as input from the user. It periodically queries the given processes in the background to change the affinity masks.

## User Dialog

- Processes (List of process names to query - every process needs matching affinity mask)
- Affinity Masks (List of affinity masks to set for given process - affinity mask should be given in hex format)
- Done (Indicator to see if desired affinity mask is set)
- Settings (Program options)
  - Period: Query period in seconds [1-99]
  - Silent-start: Start querying automatically next time when you run cas.exe
  - Auto-start: Run cas.exe automatically at startup (administrator rights needed)
- Convert (Affinity mask converter between bit and hex representation)
  - Value Type: Value type that you want to convert from (Bit or Hex)
  - Value: Value that you want to convert
  - Result: Result of the conversion
- Start (Start querying)
- Stop (Stop querying)
- Cancel (Change to tray mode)
- Tray Menu (Right-click to tray icon)
  - cas: Redirects to this page.
  - Exit: Quit cas.

# Media

![](media/cas_dialog.png)

