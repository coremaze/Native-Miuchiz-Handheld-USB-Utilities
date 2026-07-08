# Miuchiz Handheld USB Utilities

## Compiling
Dependencies:
* `git` (if you choose to clone this repo)
* `cmake`
* `make`
* `gcc` (or the appropriate compiler for your target system)
  
To build:
```
mkdir build
cd build
cmake ..
make
```
To subsequently install:
```
sudo make install
```

## Connecting a Miuchiz to a computer

  A Miuchiz can be connected to the computer in two ways:
  
  1) Hold LEFT and MENU while powering on the device. If the device has batteries, only pressing the RESET button will truly reboot the device. The device will display "Please Connect to PC" and connect over USB. This is recommended when doing complete flash reads or writes.
  
  2) Navigate to the computer room on the handheld and use the computer. The Miuchiz can connect over USB.

## Connecting an emulated Miuchiz (emiu2)

  Running [emiu2](https://github.com/coremaze/emiu2) emulators are discovered automatically, alongside physical handhelds, and appear as `emu:` devices; every action works on them unchanged. The emulator's USB cable is plugged and unplugged with the U key (or `--usb-plugged` at startup), and like a real device, the handheld only answers USB in its "Please Connect to PC" mode - start the emulator with `--connect-mode` to boot straight into it with the cable plugged.

  Emulators are found through endpoint files in emiu2's runtime directory under the shared [Miuchiz Reborn path policy](https://github.com/coremaze/Miuchiz-Reborn-Paths) (`$XDG_RUNTIME_DIR/miuchiz-reborn/emiu2` on Linux, `%TMP%\Miuchiz Reborn\emiu2` on Windows). `MIUCHIZ_REBORN_HOME` reroots the whole policy; if the tools and the emulator run under different environments (e.g. `sudo`), point both at the same directory with either that or the narrower `EMIU2_USB_DIR` override.

## Usage

### Dump flash

```
Usage: miuchiz dump-flash <output file>
Example: miuchiz dump-flash dump.dat
Example: miuchiz dump-flash -d/dev/sdb dump.dat
Example: miuchiz dump-flash -d\\.\E: dump.dat
```

Dumps the entire flash of a Miuchiz device to a file. 

`-d` or `--device` may be specified with an argument to distinguish between multiple connected Miuchiz devices.

`-c` or `--checksum` may be specified in order to perform a checksum on the result. The checksum is performed in the same manner the device's test program performs it: the sum of every byte from offset 0x1F000 to the end of the flash. The first 0x1F000 bytes are excluded. The device's test program displays only the low 16 bits of this sum.

### Dump OTP

```
Usage: miuchiz dump-otp <output file>
Example: miuchiz dump-otp otp.dat
Example: miuchiz dump-otp -d/dev/sdb otp.dat
Example: miuchiz dump-otp -d\\.\E: otp.dat
```

Dumps the **O**ne **T**ime **P**rogrammable (OTP) ROM from the device.

`-d` or `--device` may be specified with an argument to distinguish between multiple connected Miuchiz devices.

`-c` or `--checksum` may be specified in order to perform a checksum on the result. The checksum is performed in the same manner the device's test program performs it.


## Eject

```
Usage: miuchiz eject
Example: miuchiz eject
Example: miuchiz eject -d/dev/sdb
Example: miuchiz eject -d\\.\E:
```

Causes the Miuchiz device to disconnect from the computer. 

`-d` or `--device` may be specified with an argument to distinguish between multiple connected Miuchiz devices.

## Load flash

```
Usage: miuchiz load-flash <input file>
Example: miuchiz load-flash flash.dat
Example: miuchiz load-flash -d/dev/sdb flash.dat
Example: miuchiz load-flash -d\\.\E: flash.dat
```

Writes a flash dump from a file to a Miuchiz device.

`-d` or `--device` may be specified with an argument to distinguish between multiple connected Miuchiz devices.

`-c` or `--check-changes` may be specified in order to verify that pages on the device are different than the pages in the file before writing to the device. This will usually improve speed.

`-m` or `--mirror` may be specified with an argument in order to supply a file which will be treated as a cached copy of the handheld. This will maintain a local copy of the firmware in order to identify which pages need updated. This is the fastest option for those developing firmware to run on the Miuchiz device.

## Read creditz

```
Usage: miuchiz read-creditz
Example: miuchiz read-creditz
Example: miuchiz read-creditz -d/dev/sdb
Example: miuchiz read-creditz -d\\.\E:
```

Reads the number of creditz on a Miuchiz device.

`-d` or `--device` may be specified with an argument to distinguish between multiple connected Miuchiz devices.

## Set creditz

```
Usage: miuchiz set-creditz <output file>
Example: miuchiz set-creditz 123456
Example: miuchiz set-creditz -d/dev/sdb 123456
Example: miuchiz set-creditz -d\\.\E: 123456
```

Sets the number of creditz on a Miuchiz device.

`-d` or `--device` may be specified with an argument to distinguish between multiple connected Miuchiz devices.

## Status

```
Usage: miuchiz status
Example: miuchiz status
```

Displays the device path, major version, and character type for each of the Miuchiz devices connected to the computer.