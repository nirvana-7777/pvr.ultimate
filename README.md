# Ultimate PVR Client for Kodi

A Kodi PVR addon for streaming live TV from multiple providers including Joyn (AT, CH, DE) and RTL+.

## Features

- Multi-provider support (Joyn Austria, Switzerland, Germany, RTL+)
- DRM support via inputstream.adaptive (Widevine)
- Channel groups per provider
- Enable/disable providers individually
- No EPG support yet (coming in future version)

## Requirements

- Kodi 21 (Omega) or later
- inputstream.adaptive addon
- Backend server running at http://localhost:7777 (configurable)

## Directory Structure

```
pvr.ultimate/
├── CMakeLists.txt
├── addon.xml.in
├── pvr.ultimate/
│   ├── changelog.txt
│   └── resources/
│       ├── icon.png
│       ├── fanart.jpg
│       ├── language/
│       │   └── resource.language.en_gb/
│       │       └── strings.po
│       └── settings.xml
└── src/
    ├── PVRUltimate.h
    └── PVRUltimate.cpp
```

## Building

### Prerequisites

Install Kodi development files and dependencies:

```bash
# Ubuntu/Debian
sudo apt-get install kodi-addon-dev libjsoncpp-dev

# Fedora
sudo dnf install kodi-devel jsoncpp-devel

# macOS
brew install kodi jsoncpp
```

### Build Steps

```bash
# Clone the repository
git clone https://github.com/yourusername/pvr.ultimate.git
cd pvr.ultimate

# Create build directory
mkdir build
cd build

# Configure with CMake
cmake ..

# Build
make

# Install (optional)
sudo make install
```

### Cross-compilation for Kodi

If building as part of Kodi's binary addon build system:

```bash
cd /path/to/kodi
mkdir build
cd build
make -C tools/depends/target/binary-addons PREFIX=/path/to/prefix ADDONS="pvr.ultimate"
```

## Configuration

1. Install the addon in Kodi
2. Go to Settings → Add-ons → My add-ons → PVR clients → Ultimate PVR Client
3. Configure the backend URL and port (default: localhost:7777)
4. Enable/disable specific providers as needed
5. Enable the addon and restart Kodi

## Backend API

The addon expects the following endpoints on your backend:

- `GET /api/providers` - List all providers
- `GET /api/providers/<provider>/channels` - Get channels for a provider
- `GET /api/providers/<provider>/channels/<id>/manifest` - Get stream manifest
- `GET /api/providers/<provider>/channels/<id>/drm` - Get DRM configuration

## Troubleshooting

### Enable debug logging

1. Settings → System → Logging
2. Enable "Enable debug logging"
3. Check kodi.log for "Ultimate PVR" entries

### Common issues

**No channels appearing:**
- Check backend is running and accessible
- Verify backend URL/port in settings
- Check if providers are enabled

**Playback fails:**
- Ensure inputstream.adaptive is installed
- Check DRM license server is accessible
- Verify manifest URL is correct

**DRM errors:**
- Widevine CDM must be installed (usually automatic)
- Check license server returns valid response
- Verify certificate URL is accessible

## Development

### Adding new providers

Providers are automatically discovered from the backend. No code changes needed in the addon.

### DRM Configuration

The addon supports:
- Widevine DRM (com.widevine.alpha)
- Custom license headers
- Server certificates
- Request data formatting

## License

GPL-2.0-or-later

## Credits

Created for use with streaming provider backends.

## Disclaimer

This is an unofficial addon. Use at your own risk. Ensure you have the right to access the content provided by your backend.