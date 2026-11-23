# RenHDrum

[RenHDrum](https://github.com/trzz1214/RenHDrum) by Three-Dola turns the game's HD rumble values into the audio a DualSense's
voice coils play (over its cable) and the report they travel in (over Bluetooth). SuperWonders loads `renhdrum.dll` from beside
the program at run time (`runtime/src/dualsense_haptics.cpp`) and uses its DualSense functions only; its ABI is 0.5.x.

- `renhdrum.dll`: the project's own **v0.5.0** release asset, unmodified.
  SHA-256 `c6859c17ad0f9f689ffa00028ab0066454d199c25a98e3fb68f136d1d0049669`
  (<https://github.com/trzz1214/RenHDrum/releases/tag/v0.5.0>, `SHA256SUMS.txt`).
- `LICENSE-MIT`, `LICENSE-APACHE`, `NOTICE`: the project's, unmodified. Licensed MIT OR Apache-2.0.

To update it, replace `renhdrum.dll` with the DLL of a newer 0.5.x release (the program refuses another minor version and plays
the plain rumble instead) and change the SHA-256 above. The build copies it next to `wonder.exe`.
