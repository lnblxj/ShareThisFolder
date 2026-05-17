# ShareThisFolder

A lightweight LAN file sharing tool for Windows. Share any folder to your phone or other devices via QR code scanning.

## Features

- **One-click sharing** — run the executable and instantly share the current folder over LAN
- **QR code scanning** — automatically generates a QR code image for quick phone access
- **Bilingual UI** — auto-detects system language (Chinese / English)
- **Zero dependencies** — single `.exe`, no installation needed
- **Mobile-friendly** — clean web interface for browsing and downloading files

## Quick Start

### Download

Grab the latest build from [Releases](https://github.com/lnblxj/ShareThisFolder/releases).

### Build from Source

Requires Visual Studio 2022 with C++ desktop development workload.

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The executable will be at `build/Release/ShareThisFolder.exe`.

### Usage

1. Place `ShareThisFolder.exe` in the folder you want to share
2. Double-click to run (or run from terminal)
3. A QR code image will open automatically — scan it with your phone
4. Browse and download files from your phone's browser
5. Press `Ctrl+C` in the terminal to stop

```
  ShareThisFolder - LAN File Sharing
  ------------------------------------------------

  Sharing: D:\MyFiles

  Or open in browser: http://192.168.1.100:8080/

  Scan QR code with your phone:

  QR code image opened - scan with your phone

  Press Ctrl+C to stop
```

## Requirements

- Windows 10 / 11
- Network connectivity (LAN) between your computer and phone
- Internet connection (only needed for QR code generation on first launch)

## License

MIT
