module.exports = [
  {
    "type": "heading",
    "defaultValue": "Upcoming Calendar Cards"
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Colors"
      },
      {
        "type": "color",
        "messageKey": "PrimaryColor",
        "label": "Primary (background)",
        "defaultValue": "0x00AAAA"
      },
      {
        "type": "color",
        "messageKey": "SecondaryColor",
        "label": "Secondary (accent triangle)",
        "defaultValue": "0xFF5500"
      },
      {
        "type": "color",
        "messageKey": "TextColor",
        "label": "Text color",
        "defaultValue": "0xFFFFFF"
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Weather"
      },
      {
        "type": "toggle",
        "messageKey": "TemperatureUnit",
        "label": "Fahrenheit (off = Celsius)",
        "defaultValue": false
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Calendar Card"
      },
      {
        "type": "select",
        "messageKey": "CountdownPosition",
        "label": "Countdown position",
        "defaultValue": 0,
        "options": [
          { "label": "Top (above event name)", "value": 0 },
          { "label": "Bottom (below event name)", "value": 1 }
        ]
      }
    ]
  },
  {
    "type": "section",
    "items": [
      {
        "type": "heading",
        "defaultValue": "Google Calendar"
      },
      {
        "type": "text",
        "defaultValue": "Paste your Google Calendar private iCal URL below. To find it: open Google Calendar on desktop → Settings (gear icon) → click your calendar name → scroll to 'Integrate calendar' → copy 'Secret address in iCal format'."
      },
      {
        "type": "input",
        "messageKey": "CalendarUrl",
        "label": "iCal URL",
        "defaultValue": "",
        "attributes": {
          "placeholder": "https://calendar.google.com/calendar/ical/..."
        }
      }
    ]
  },
  {
    "type": "submit",
    "defaultValue": "Save Settings"
  }
];
