Emulator I
==========

This project is a software emulation of the E-MU Emulator I for research and analysis purposes. It can boot the latest OS code and give insights into how the OS code works, without requiring access to a real Emulator I.

**WARNING**: This is **not** a usable "virtual instrument" or anything comparable to such a thing. This project is purely a software emulator to analyze the behavior of the original Emulator I firmware.

Features
--------

- A reasonably accurate software emulation of the digital logic of the E-MU Emulator I
- Tracing capabilities to perform offline analysis of the firmware
- Logging of various system events to quickly see what is happening
- Injection of key presses with optional keyboard matrix encoding


Compiling
---------

You need a Linux system with gcc and make. To compile the project, run `make` in the root directory.


Usage
-----

```
./emulator [OPTIONS] <floppy-image>
```

The following options are supported:
- `-w`: use wildcard ROM
- `-t<tracefile>`: record machine readable execution trace to file
- `-k<key-id>`: press key with raw ID after system boot
- `-m<midi-key>`: press MIDI key but encode it to the keyboard matrix
- `-e`: automatically exit on idle
- `-s`: patch serial number from EPROM into floppy
- `-o<os-file>`: load OS from file and replace OS section on the floppy
- `-r<rom-file>`: use EPROM instead of default retail/wildcard EPROMs

You need an unmodified floppy dump of a bootable Emulator I floppy. The floppy dump must only contain the track data, low level dumps are not supported.


Usage Examples
--------------

Get the sample playback rates for all keys:
```
for i in `seq 0 48`; do result=$(./emulator -m$i -e fdd/\#17\ Male\ Voices\ -\ Mixed\ Choir.emufd | grep CH.CSH | tail -n 1); printf "%02d => %s\n" $i "$result"; done
```

Get the keyboard matrix mapping:
```
for i in `seq 0 71`; do result=$(timeout 5 ./emulator -k$i -e fdd/\#17\ Male\ Voices\ -\ Mixed\ Choir.emufd | grep 'alloc_voice\|LEDs:' | tail -n 1); echo "$i => $result"; done
```


Floppy Format
-------------

Look at the FLOPPY.md file for a detailed description of the floppy format.
