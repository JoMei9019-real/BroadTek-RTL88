![BroadTek: Broadcom features on Realtek Bluetooth](docs/banner.png)

<p align="center">
  <img alt="macOS Tahoe" src="https://img.shields.io/badge/macOS-Tahoe%2026.7-0B0D12?logo=apple&logoColor=white">
  <img alt="Bluetooth LE" src="https://img.shields.io/badge/Bluetooth-LE%20HCI-1F6FEB?logo=bluetooth&logoColor=white">
  <img alt="Lilu plugin" src="https://img.shields.io/badge/Lilu-plugin-7C5CFF">
  <img alt="License MIT" src="https://img.shields.io/badge/license-MIT-blue">
</p>

# BroadTek

**Broadcom features on Realtek and Intel Bluetooth.** A Lilu plugin that makes a
Hackintosh's Bluetooth behave the way macOS expects of a Mac's own Broadcom
chip, so that an iPhone can find the Mac for **AirDrop** and **Handoff**, and,
from 0.3.0, so that an **Apple Watch can unlock it**.

Apple's `bluetoothd` treats any Bluetooth controller it does not know as a
"third-party dongle" and quietly leaves things out for it. Two of those
decide whether an iPhone ever sees the Mac:

1. **It does not advertise at all.** bluetoothd refuses every Continuity
   advert with *"Stack is not ready"*, because the check it makes is on the
   chip's identity, not on what the chip can do. BroadTek patches that one
   check in bluetoothd's memory as it loads, the way BlueToolFixup does for
   other checks.
2. **The adverts carry no address.** bluetoothd sets each extended advertising
   set to use a random address but only ever sends the old command that sets
   the address for *legacy* advertising (`0x2005`), never the one for advertising
   sets (`0x2035`). The sets go out with no private address the iPhone could
   recognise, so it ignores them. BroadTek sits on the HCI stream between
   bluetoothd and the controller and sends `0x2035` with the current address
   just before each set is switched on.

With both, the iPhone lists the Mac in AirDrop, files go both ways, and
Handoff works.

BroadTek ships with [AirPort_RTW89](https://github.com/FranciumOS-org/Airport_RTW89)
0.7.0 and later, which brings the Wi-Fi half (AWDL) for Realtek cards.

## What works

Measured on an ASUS TUF A15 with macOS Tahoe 26.7.3, an iPhone on iOS 26 and an
Apple Watch on watchOS 26, with two cards: the RTL8852BE (Bluetooth on USB
`13D3:3571`, RealtekBluetoothFirmware and BlueToolFixup) and an Intel AX210
(IntelBluetoothFirmware and BlueToolFixup):

| | Realtek RTL8852BE | Intel AX210 |
|---|---|---|
| Bluetooth LE advertising | Works | Works |
| AirDrop, both ways | Works | Works |
| Handoff | Works | Works |
| Apple Watch unlock (prompts) | Not working | **Works** |
| Apple Watch unlock (lock screen) | Not working | Untested |
| AirPods, keyboards, mice | Unchanged | Unchanged |

It works on the HCI stream, not on one chip, so other USB controllers without
Apple's firmware should behave much the same, but none has been tried yet.

### RTL8821CE test on Tahoe

The RTL8821C Bluetooth controller in the RTL8821CE uses Bluetooth 4.2.
On macOS Tahoe 26.7.x, add `-broadtek8821c` to test the single-set Legacy
Advertising path. It matches the Bluetooth **USB** IDs `0BDA:B00A`,
`13D3:3529`, `13D3:3533`, and `13D3:3558`. The controller can broadcast
one advertising set with at most 31 bytes of data. This translation still
needs a hardware test on RTL8821CE.

Keep `Lilu.kext`, `RealtekBluetoothFirmware.kext`, and `BlueToolFixup.kext`
for Bluetooth, then add this build of `BroadTek.kext` after Lilu. For AirDrop,
keep [AirPortRTW 2.0.0 Beta 20](https://github.com/JoMei9019-real/AirPort-RTW/blob/beta-2.0.0/BETA_2_0_0_20_TEST.md)
for Wi-Fi and AWDL. The Apple Watch helper is separate from this test.

Check your USB ID and the Bluetooth firmware loader first. The loader needs
the matching USB ID in its own `Info.plist`, even when BroadTek recognizes it.

```sh
system_profiler SPUSBDataType
log show --last boot --predicate 'eventMessage CONTAINS "RealtekFirmware"'
ioreg -l -w0 | grep -Ei 'broadtek-(rtl8821c|btd-pages-patched|adv-status)'
ifconfig awdl0
```

On a working setup, `broadtek-rtl8821c-command-hook` and
`broadtek-rtl8821c-pipe-hook` show `Yes`. The
`broadtek-rtl8821c-event-pipe` appears when BroadTek binds the controller's
event endpoint. `broadtek-rtl8821c-pipe-lookup-failures` counts attempts to
bind that endpoint that did not succeed. The counter
`broadtek-rtl8821c-supported-commands` increments when the HCI reply lists
the advertising commands translated by BroadTek.

Compare the BroadTek reply counter before and after opening AirDrop on the
Mac. Then check iPhone discovery and transfer a small file in each direction
with both devices set to Everyone. A successful HCI reply confirms controller
acceptance. Discovery and file transfers are separate hardware checks.

### Apple Watch unlock

On the AX210 the Watch approves the "use your Apple Watch to unlock" prompts
macOS shows for passwords, settings and apps, including the first one after a
restart. It needs three things on top of the plain install:

- the `-broadtekrw` boot-arg (BroadTek's full stage),
- the key helper from the release zip, installed with `install.sh` (below),
- AMFI off (`amfi_get_out_of_my_way=0x1`). The helper reads the Watch's key
  from your keychain, where iCloud Keychain has already put it, and macOS gives
  that key only to Apple's own programs unless AMFI is off.

macOS decides when it asks the Watch, and that is the same as on a real Mac:
never for the first login after a restart or a log out, and after **Lock
Screen** (Ctrl-Cmd-Q) only once the display has gone to sleep and been woken.

On Realtek the Watch link stalls the same way it first did on Intel; the fix
that made Intel work (BroadTek standing in for the PHY Update and Data Length
Change reports bluetoothd waits for) is applied only to Intel controllers so far.

## Not working

- **Instant Hotspot.** The Mac has to connect to the iPhone at its private
  address, which needs the iPhone's key (IRK), and that key is not in the
  Mac's keychain the way the Watch's is.
- **iPhone Mirroring.** BroadTek carries the link all the way through pairing,
  then macOS asks the Secure Enclave for a device certificate, and a Hackintosh
  has none. Not fixable in software.
- **macOS versions other than Tahoe 26.7.x.** The bluetoothd patch matches
  that build's code. On another version it does not apply, nothing breaks, and
  iPhones do not list the Mac.

## Requirements

- A Hackintosh booting with **OpenCore**, with **Lilu**.
- Working Bluetooth: for Realtek combo cards, RealtekBluetoothFirmware and
  BlueToolFixup.
- macOS Tahoe 26.7.x for the advertising fix.
- For AirDrop: a Wi-Fi driver with AWDL, such as AirPort_RTW89 0.7.0.
- For Apple Watch unlock: an Intel controller, `-broadtekrw`, AMFI off, and the
  Watch on the same Apple ID with iCloud Keychain on.

## Install

**With AirPort_RTW89:** nothing to do. `BroadTek.kext` is in its release zip
and its Kext Installer adds it.

**On its own:** download `BroadTek.kext` from [Releases](../../releases), or
build it (below), then

```sh
sudo efi/install.sh      # copies the kext next to Lilu and adds it after Lilu
```

It finds the EFI with OpenCore on it (or asks which, if there are several),
keeps the old config as `EFI/OC/config.plist.pre-broadtek`, and changes
nothing if the volume is short of room. `sudo efi/uninstall.sh` switches it
off again. Restart after either.

**By hand:** copy `BroadTek.kext` to `EFI/OC/Kexts` and add it to
`Kernel → Add` right after Lilu.

On Intel, `efi/install.sh` switches BTLEAdvFixup off if it finds it (the two
patch the same thing) and `efi/uninstall.sh` switches it back on.

**For Apple Watch unlock**, once the kext is in the EFI, `-broadtekrw` is in
the boot-args and you have restarted, run from the unzipped release folder:

```sh
sudo ./install.sh        # adds the Watch key helper to macOS
```

It checks that BroadTek is running, installs a small LaunchDaemon that hands
the Watch's key to BroadTek after every login, and waits for the first one.
Then turn on System Settings → Touch ID & Password → Use Apple Watch to unlock.
The key is never printed or saved; `/var/log/broadtek-irk.log` holds addresses
and statuses only. `sudo ./uninstall.sh` removes the helper.

## Boot-args

| | |
|---|---|
| `-broadtekoff` | BroadTek does nothing |
| `-broadtek8821c` | test single-set Legacy Advertising on a matched RTL8821C USB controller |
| `-broadtekadvaddroff` | leave the adverts' addresses alone |
| `-broadtekadvoff` | do not patch bluetoothd's advertising check |
| `-broadtekrw` | the full stage: the software resolving list, needed for Watch unlock |
| `-broadtekintel` / `-broadtekinteloff` | force the Intel fixes on, or off (they turn on by themselves for USB vendor `8087`) |
| `-broadtekreadyoff` | Intel: do not stand in for the link-ready reports |
| `-broadtekdbg` | Lilu's debug output (in a debug build) |

`ioreg -l | grep -i broadtek` shows what it did: `broadtek-adv-addr`,
`broadtek-adv-injected` (adverts given their address) and the counters of
what it saw on the HCI stream. No sudo needed.

## Building

On macOS with the Command Line Tools. Lilu's headers and MacKernelSDK are in
`third_party/`.

```sh
make test              # the HCI and crypto core in userspace, with ASan/UBSan
make kext              # build/out/BroadTek.kext, after the test
make kext DEBUG=0      # the quiet build that releases use
make release DEBUG=0   # build/out/BroadTek-<version>.zip: kext, helper, install.sh
```

## How it works

- `src/core/`: plain C with no kernel dependencies, so it is tested in
  userspace: HCI parsing, the resolving list, AES-128 and Bluetooth's `ah()`
  for private addresses, and the SMP pieces.
- `src/plugin/`: the Lilu plugin. It routes `IOUSBHostPipe`'s control and I/O
  calls to see bluetoothd's commands and the controller's events, sends the
  `0x2035` commands, and patches bluetoothd's pages through
  `_cs_validate_page` as they are loaded.
- `efi/`: the install and uninstall scripts and the `config.plist` editor.
- `tools/`: probes used while working this out (keychain, nearby info, the
  advert decoder). Nothing in them is needed to use BroadTek.

The long version, including the routes that did not work and why, is in
[docs/LE_ADVERTISING.md](docs/LE_ADVERTISING.md), [docs/DESIGN.md](docs/DESIGN.md)
and [docs/STATE.md](docs/STATE.md). [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)
has the notes for testing a build on hardware.

## Credits

- [Lilu](https://github.com/acidanthera/Lilu) and
  [MacKernelSDK](https://github.com/acidanthera/MacKernelSDK) by Acidanthera.
- [BlueToolFixup](https://github.com/acidanthera/BrcmPatchRAM), whose
  `_cs_validate_page` route the bluetoothd patch follows.
- [ForbiddenItlwm](https://github.com/retropineaple/ForbiddenItlwm), whose
  write-up first pointed at Bluetooth LE advertising as the reason iPhones do
  not list a Hackintosh.

## License

MIT, see [LICENSE](LICENSE). Lilu is BSD-3-Clause; MacKernelSDK carries
Apple's APSL headers.
