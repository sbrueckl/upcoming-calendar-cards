# Upcoming Calendar Cards

A Pebble watchface combining Google Calendar events and live weather in a clean card-based layout that adapts dynamically to what's coming up next.

## Features

- **Event Card** - A white rounded card slides into view when an event is within 24 hours, showing the title and an "In X hours" label
- **Live Weather** - Current temperature and conditions fetched via Open-Meteo (no API key required)
- **Dynamic Layout** - Time and date center on screen when no card is shown; shift up to make room when a card appears
- **Date Display** - Abbreviated day and date shown left-aligned below the time
- **Battery & Bluetooth Status** - Top-left status bar with a graphical battery indicator and Bluetooth connection dot
- **Diagonal Color Accent** - A configurable triangular color accent fills the top-right corner on color displays
- **Customizable Colors** - Primary (background), secondary (accent triangle), and text colors via Clay settings
- **Multi-Platform Support** - Works on Basalt, Chalk, Diorite, Emery, Flint, and Gabbro

## Screenshots

### Color Display (Basalt/Emery)
Cadet blue background with a sunset orange diagonal accent. White event card appears in the lower portion when an event is imminent.

### Round Display (Chalk/Gabbro)
Corner-safe padding and centered layout adapted for circular screens; diagonal accent omitted on B&W round platforms.

### Large Display (Emery)
Larger fonts and taller card area take advantage of the 200×228 high-resolution screen.

## Installation

### From PBW File
1. Transfer `upcoming-calendar-cards.pbw` to your phone
2. Open with the Pebble app
3. Install to your watch

### From Source
```bash
cd upcoming-calendar-cards
pebble build
pebble install --emulator basalt  # or --phone for real device
```

### Setup
After installing, open the watchface settings in the Pebble app and paste your Google Calendar private iCal URL. Find it in Google Calendar → Settings → your calendar → "Secret address in iCal format". Location access is required for weather.

## Technical Details

- **Calendar Refresh**: Every 30 minutes, plus on-demand when the countdown reaches zero
- **Weather Provider**: Open-Meteo (free, no API key, uses device GPS location)
- **Card Threshold**: Event card shown only when event is within 24 hours
- **Supported Platforms**: Basalt, Chalk, Diorite, Emery, Flint, Gabbro
- **Capabilities**: Configurable (Clay settings UI), Location (for weather)

## Credits

Created with Claude Code

## License

MIT License
