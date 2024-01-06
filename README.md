![](media/cas256x256.png)

# cas

cas is a simple utility for setting CPU affinity on Windows.

You can download the latest build as a zip archive here: [cas.zip](https://raw.githubusercontent.com/wiki/nukoseer/cas/cas.zip)

_Please note that it is recommended to run cas.exe as an administrator, as some processes may require administrator rights to set affinity._

# Usage

cas takes process names and affinity mask pairs as input from the user. It periodically queries the specified processes in the background to update the affinity masks.

## User Dialog

- Processes (List of process names to query - each process must have a matching affinity mask)
- Affinity Masks (List of affinity masks to set for the corresponding processes - affinity mask should be given in hex format)
- Done (Indicator to see if desired affinity mask is set)
- Settings (Program options)
  - Period: Query period in seconds [1-99]
  - Menu Shortcut: Set a shortcut to open/close the cas menu
  - Silent-start: Start querying automatically the next time you run cas
  - Auto-start: Run cas automatically at startup (administrator rights needed)
- Convert (Affinity mask converter between bit and hex representation)
  - Value Type: Value type you want to convert from (Bit or Hex)
  - Value: Value you want to convert
  - Result: Result of the conversion
- Start (Start querying)
- Stop (Stop querying)
- Cancel/X (Change to tray mode)
- Tray Menu (Right-click on tray icon)
  - cas: Redirects to this page.
  - Exit: Quit cas.

# Media

![](media/cas_dialog.png)

