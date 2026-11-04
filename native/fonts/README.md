# Fonts
B612 and B612 Mono (https://github.com/polarsys/b612, tag 1.008), by Airbus and Intactile Design for cockpit
displays. Licensed under the SIL Open Font License 1.1 (`OFL.txt`); the release zip ships that file as
`FXAnnounce/licenses/B612-OFL.txt`. "B612" is a trademark of the Eclipse Foundation.

The `.inc` files are the TTFs compressed with Dear ImGui's `misc/fonts/binary_to_compressed_c -u8` and are
compiled into the plugin (`src/ui/fonts.cpp`). To regenerate one:
```
c++ -O2 -o bin2c <imgui>/misc/fonts/binary_to_compressed_c.cpp
./bin2c -u8 B612-Regular.ttf b612_regular > b612_regular.inc   # then restore the three header lines
```
