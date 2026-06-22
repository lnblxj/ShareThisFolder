# ShareThisFolder

ShareThisFolder is a lightweight Windows folder sharing tool that quickly shares the current folder with phones and other devices.

## New Features

- **UPnP port mapping** - when public access is requested, the app first tries to create a router UPnP port mapping.
- **STUN tunnel fallback** - adds STUN public endpoint discovery and local forwarding for compatible NAT environments.
- **Windows Explorer address-bar quick launch** - after enabling quick launch in Settings, type `stf` in the Windows Explorer address bar to share the current folder.

## Features

- **One-click LAN sharing** - run the executable in the folder you want to share, and the folder can be accessed over the local network.
- **Mobile-friendly browser page** - provides a clean mobile directory browser and file download page.
- **Multilingual UI** - automatically detects the system language and supports multiple languages.
- **Single executable** - the sharing host only needs to download and run the executable.
- **Quick launch** - install the system-level `stf` command from the Settings menu.
- **Public access** - supports intranet traversal as much as possible, including NAT3 network environments.

## Quick Start

### Download

Download the latest version from [Releases](https://github.com/lnblxj/ShareThisFolder/releases).

### Build from Source

Requires Visual Studio 2022 with the C++ desktop development workload.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable will be generated at `build/Release/ShareThisFolder.exe`.

## Usage

### Share the Current Folder

1. Put `ShareThisFolder.exe` into the folder you want to share.
2. Double-click it, or run it from a terminal.
3. Open the LAN address shown in the menu from your phone or another device.
4. Press an address number to copy the corresponding URL.
5. Press `s` to choose an address and show its QR code.
6. Press `q` or `Ctrl+C` to stop sharing.

### Enable Public Access

Press `w` in the main menu. The app will try to open a NAT tunnel. If hole punching succeeds, a `WAN` address is added to the menu.

Public access availability depends on the router, ISP, firewall, and NAT type. Enabling UPnP on the router is recommended to improve the success rate.

### Set Up Windows Explorer Quick Launch

1. Press `e` to open Settings.
2. Choose `Set quick launch`.
3. Approve the Windows administrator permission prompt.
4. The app copies itself to `C:\Program Files\ShareThisFolder\stf.exe` and adds that directory to the system `Path`.
5. Open any folder in Windows Explorer, type `stf` in the address bar, and press Enter to share the current folder.

To remove it, choose `Remove quick launch` from the Settings menu.

## License

MIT
