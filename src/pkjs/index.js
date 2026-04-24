var Clay = require('@rebble/clay');
var clayConfig = require('./config');
var clay = new Clay(clayConfig);

// --- iCal parsing (RFC 5545) ---

function parseTzOffsets(lines) {
  var tzMap = {};
  var inVtz = false, tzid = null, stdMins = 0, dstMins = 0, inDst = false;
  for (var i = 0; i < lines.length; i++) {
    var l = lines[i];
    if (l === 'BEGIN:VTIMEZONE') {
      inVtz = true; tzid = null; stdMins = 0; dstMins = 0; inDst = false;
    } else if (l === 'END:VTIMEZONE') {
      if (tzid) tzMap[tzid] = { std: stdMins, dst: dstMins || stdMins };
      inVtz = false;
    } else if (inVtz) {
      if (l.indexOf('TZID:') === 0) {
        tzid = l.slice(5).trim();
      } else if (l === 'BEGIN:DAYLIGHT') {
        inDst = true;
      } else if (l === 'END:DAYLIGHT' || l === 'END:STANDARD') {
        inDst = false;
      } else if (l.indexOf('TZOFFSETTO:') === 0) {
        var s = l.slice(11).trim();
        var sign = s.charAt(0) === '-' ? -1 : 1;
        var mins = sign * (parseInt(s.slice(1, 3), 10) * 60 + (parseInt(s.slice(3, 5), 10) || 0));
        if (inDst) { dstMins = mins; } else { stdMins = mins; }
      }
    }
  }
  return tzMap;
}

function parseIcalDate(line, tzMap) {
  var tzid = null;
  var tzidM = line.match(/;TZID=([^:;]+)/);
  if (tzidM) tzid = tzidM[1];

  var colonIdx = line.lastIndexOf(':');
  var val = line.slice(colonIdx + 1).trim();

  // All-day event (DATE only): treat as midnight local time
  if (val.length === 8) {
    var y = parseInt(val.slice(0, 4), 10);
    var m = parseInt(val.slice(4, 6), 10) - 1;
    var d = parseInt(val.slice(6, 8), 10);
    return isNaN(y) ? null : new Date(y, m, d, 0, 0, 0);
  }

  var isUtc = val.charAt(val.length - 1) === 'Z';
  var s = val.replace('Z', '');

  var year  = parseInt(s.slice(0, 4), 10);
  var month = parseInt(s.slice(4, 6), 10) - 1;
  var day   = parseInt(s.slice(6, 8), 10);
  var hour  = parseInt(s.slice(9, 11), 10);
  var min   = parseInt(s.slice(11, 13), 10);
  var sec   = parseInt(s.slice(13, 15), 10) || 0;

  if (isNaN(year) || isNaN(hour)) return null;

  if (isUtc) {
    return new Date(Date.UTC(year, month, day, hour, min, sec));
  }

  // TZID-aware: convert using parsed VTIMEZONE offsets
  if (tzid && tzMap && tzMap[tzid] !== undefined) {
    var monthNum = month + 1;
    var offsetMins = (monthNum >= 4 && monthNum <= 10)
      ? tzMap[tzid].dst : tzMap[tzid].std;
    return new Date(Date.UTC(year, month, day, hour, min, sec) - offsetMins * 60000);
  }

  // Fallback: local phone time (correct when phone TZ == calendar TZ)
  return new Date(year, month, day, hour, min, sec);
}

function computeRruleNext(dtstart, rruleStr) {
  var now = new Date();
  if (dtstart > now) return dtstart;

  var freq = (rruleStr.match(/FREQ=(\w+)/) || [])[1];
  if (!freq) return null;

  var interval = parseInt(((rruleStr.match(/INTERVAL=(\d+)/) || [])[1]) || '1', 10);

  var untilM = rruleStr.match(/UNTIL=(\d{8}(?:T\d{6}Z?)?)/);
  if (untilM) {
    var until = parseIcalDate('X:' + untilM[1]);
    if (until && until < now) return null;
  }

  // COUNT: series ends after N total occurrences (dtstart = #1)
  var countM = rruleStr.match(/COUNT=(\d+)/);
  var maxCount = countM ? parseInt(countM[1], 10) : Infinity;

  var dayMap = { SU: 0, MO: 1, TU: 2, WE: 3, TH: 4, FR: 5, SA: 6 };
  var byDayM = rruleStr.match(/BYDAY=([\w,]+)/);
  var byDays = byDayM
    ? byDayM[1].split(',').map(function(s) { return dayMap[s.replace(/[^A-Z]/g, '')]; })
                          .filter(function(n) { return n !== undefined; })
    : null;

  var MS_DAY  = 24 * 3600 * 1000;
  var MS_WEEK = 7 * MS_DAY;
  var d = new Date(dtstart.getTime());
  var skipped = 0;

  // Fast-forward d to near 'now' to avoid hitting the iteration limit for old dtstart.
  // Without this, DAILY events with dtstart 4+ years ago exhaust the loop before today.
  if (d < now) {
    if (freq === 'DAILY') {
      var n = Math.floor((now.getTime() - d.getTime()) / (interval * MS_DAY));
      d = new Date(d.getTime() + n * interval * MS_DAY);
      skipped = n;
    } else if (freq === 'WEEKLY') {
      // For BYDAY leave a 2-interval margin so no day-of-week match is skipped over
      var margin = byDays ? 2 * interval : 0;
      var n = Math.max(0, Math.floor((now.getTime() - d.getTime()) / (interval * MS_WEEK)) - margin);
      d = new Date(d.getTime() + n * interval * MS_WEEK);
      skipped = byDays ? n * byDays.length : n;
    }
  }

  for (var i = 0; i < 1500; i++) {
    if (freq === 'DAILY') {
      d = new Date(d.getTime() + interval * MS_DAY);
    } else if (freq === 'WEEKLY') {
      if (byDays && byDays.length > 0) {
        d = new Date(d.getTime() + MS_DAY);
        var daysFromStart = Math.round((d.getTime() - dtstart.getTime()) / MS_DAY);
        var weekFromStart = Math.floor(daysFromStart / 7);
        if (weekFromStart % interval !== 0) continue;
        if (byDays.indexOf(d.getDay()) < 0) continue;
      } else {
        d = new Date(d.getTime() + interval * MS_WEEK);
      }
    } else if (freq === 'MONTHLY') {
      var mn = new Date(d); mn.setMonth(mn.getMonth() + interval); d = mn;
    } else if (freq === 'YEARLY') {
      var yn = new Date(d); yn.setFullYear(yn.getFullYear() + interval); d = yn;
    } else {
      return null;
    }

    // Loop i=0 is the 2nd occurrence; dtstart itself is #1
    if (i + 2 + skipped > maxCount) return null;

    if (untilM) {
      var u = parseIcalDate('X:' + untilM[1]);
      if (u && d > u) return null;
    }
    if (d > now) return d;
  }
  return null;
}

function parseIcal(text) {
  var unfolded = text.replace(/\r\n[ \t]/g, '').replace(/\n[ \t]/g, '');
  var lines = unfolded.split(/\r\n|\n/);

  var tzMap = parseTzOffsets(lines);

  var events = [];
  var evt = null;

  for (var i = 0; i < lines.length; i++) {
    var line = lines[i];
    if (line === 'BEGIN:VEVENT') {
      evt = { title: null, start: null, end: null, rrule: null };
    } else if (line === 'END:VEVENT') {
      if (evt && evt.title && evt.start) events.push(evt);
      evt = null;
    } else if (evt) {
      if (line.indexOf('SUMMARY:') === 0) {
        evt.title = line.slice(8);
      } else if (line.indexOf('DTSTART') === 0) {
        var d = parseIcalDate(line, tzMap);
        if (d) evt.start = d;
      } else if (line.indexOf('DTEND') === 0) {
        var endDate = parseIcalDate(line, tzMap);
        if (endDate) evt.end = endDate;
      } else if (line.indexOf('RRULE:') === 0) {
        evt.rrule = line.slice(6);
      }
    }
  }
  return events;
}

function findNextEvent(events) {
  var now = new Date();
  var candidates = [];

  events.forEach(function(e) {
    if (e.start > now) {
      candidates.push({ title: e.title, start: e.start });
    } else if (e.rrule) {
      var next = computeRruleNext(e.start, e.rrule);
      if (next) candidates.push({ title: e.title, start: next });
    }
  });

  if (candidates.length === 0) return null;
  candidates.sort(function(a, b) { return a.start - b.start; });
  return candidates[0];
}

// --- Settings store ---

function getCalendarUrl() {
  // 1. Our own key — written on every webviewclosed
  try {
    var own = String(localStorage.getItem('ucc-ical-url') || '').trim();
    if (own.length >= 20) return own;
  } catch (e) {}

  // 2. Clay getSettings()
  try {
    var s = clay.getSettings();
    var raw = s['CalendarUrl'];
    var url = (raw && typeof raw === 'object' ? raw.value : raw) || '';
    url = String(url).trim();
    if (url.length >= 20) return url;
  } catch (e) {}

  // 3. Clay's internal localStorage key
  try {
    var stored = JSON.parse(localStorage.getItem('clay-settings') || '{}');
    var url2 = String(stored['CalendarUrl'] || '').trim();
    if (url2.length >= 20) return url2;
  } catch (e) {}

  return '';
}

// --- Event fetch ---

function sendEventData(hasEvent, title, hours, minutes) {
  var dict = {
    'HAS_EVENT':    hasEvent ? 1 : 0,
    'EVENT_TITLE':  (title || '').slice(0, 32),
    'EVENT_HOUR':   hours || 0,
    'EVENT_MINUTE': minutes || 0
  };
  Pebble.sendAppMessage(dict,
    function() { console.log('Event sent OK'); },
    function(e) { console.log('Event send failed: ' + JSON.stringify(e)); }
  );
}

function fetchNextEvent() {
  var calUrl = getCalendarUrl();
  console.log('CalendarUrl length: ' + calUrl.length);

  if (calUrl.length < 20) {
    console.log('No calendar URL configured');
    sendEventData(false, '', 0, 0);
    return;
  }

  console.log('Fetching iCal: ' + calUrl.slice(0, 60) + '...');

  var xhr = new XMLHttpRequest();
  xhr.onload = function() {
    console.log('iCal HTTP ' + this.status + ', bytes: ' + this.responseText.length);
    try {
      var events = parseIcal(this.responseText);
      console.log('Events parsed: ' + events.length);
      var next = findNextEvent(events);
      if (next) {
        var now = new Date();
        var totalMins = Math.floor((next.start - now) / 60000);
        if (totalMins <= 0) {
          console.log('Next event already started, no upcoming');
          sendEventData(false, '', 0, 0);
        } else {
          var hours = Math.floor(totalMins / 60);
          var minutes = totalMins % 60;
          console.log('Next: "' + next.title + '" in ' + hours + 'h ' + minutes + 'm');
          sendEventData(true, next.title, hours, minutes);
        }
      } else {
        console.log('No upcoming events');
        sendEventData(false, '', 0, 0);
      }
    } catch (err) {
      console.log('Parse error: ' + err.message);
      sendEventData(false, '', 0, 0);
    }
  };
  xhr.onerror = function() {
    console.log('XHR error fetching calendar');
    sendEventData(false, '', 0, 0);
  };
  xhr.open('GET', calUrl);
  xhr.send();
}

// --- Weather (Open-Meteo, no API key) ---

var WMO_MAP = [
  [0,  'Clear'],
  [3,  'Cloudy'],
  [9,  'Partly Cloudy'],
  [49, 'Foggy'],
  [57, 'Drizzle'],
  [67, 'Rain'],
  [77, 'Snow'],
  [82, 'Showers'],
  [99, 'Storm']
];

function wmoDescription(code) {
  var label = 'Cloudy';
  for (var i = 0; i < WMO_MAP.length; i++) {
    if (code <= WMO_MAP[i][0]) { label = WMO_MAP[i][1]; break; }
  }
  return label;
}

function sendWeatherData(temp, conditions) {
  Pebble.sendAppMessage({
    'TEMPERATURE': Math.round(temp),
    'CONDITIONS':  conditions.slice(0, 15)
  },
    function() { console.log('Weather sent OK'); },
    function(e) { console.log('Weather send failed: ' + JSON.stringify(e)); }
  );
}

function getTempUnit() {
  try {
    var raw = localStorage.getItem('ucc-temp-unit');
    return raw === '1' ? 'fahrenheit' : 'celsius';
  } catch (e) { return 'celsius'; }
}

function fetchWeather() {
  navigator.geolocation.getCurrentPosition(
    function(pos) {
      var lat = pos.coords.latitude.toFixed(4);
      var lon = pos.coords.longitude.toFixed(4);
      var url = 'https://api.open-meteo.com/v1/forecast' +
        '?latitude=' + lat + '&longitude=' + lon +
        '&current=temperature_2m,weather_code&temperature_unit=' + getTempUnit();

      var xhr = new XMLHttpRequest();
      xhr.onload = function() {
        try {
          var json = JSON.parse(this.responseText);
          var temp = json.current.temperature_2m;
          var code = json.current.weather_code;
          sendWeatherData(temp, wmoDescription(code));
        } catch (e) {
          console.log('Weather parse error: ' + e.message);
        }
      };
      xhr.onerror = function() { console.log('Weather XHR error'); };
      xhr.open('GET', url);
      xhr.send();
    },
    function(err) { console.log('Geolocation error: ' + err.message); },
    { timeout: 15000, maximumAge: 300000 }
  );
}

// --- Pebble events ---

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  fetchWeather();
  fetchNextEvent();
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload['REQUEST_UPDATE']) {
    console.log('Watch requested update');
    fetchWeather();
    fetchNextEvent();
  }
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (e && e.response) {
    try {
      var data = JSON.parse(decodeURIComponent(e.response));
      var raw = data['CalendarUrl'];
      var url = String((raw && typeof raw === 'object' ? raw.value : raw) || '').trim();
      if (url.length >= 20) {
        localStorage.setItem('ucc-ical-url', url);
        console.log('CalendarUrl saved, length: ' + url.length);
      }
      var tuRaw = data['TemperatureUnit'];
      var tuVal = tuRaw && typeof tuRaw === 'object' ? tuRaw.value : tuRaw;
      localStorage.setItem('ucc-temp-unit', tuVal ? '1' : '0');
      console.log('TemperatureUnit saved: ' + (tuVal ? 'fahrenheit' : 'celsius'));
    } catch (ex) {
      console.log('webviewclosed parse error: ' + ex.message);
    }
    console.log('Config saved, refreshing');
    fetchWeather();
    fetchNextEvent();
  }
});
