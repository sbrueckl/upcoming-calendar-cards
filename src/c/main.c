#include <pebble.h>

#define SETTINGS_KEY 1

typedef struct {
  GColor  PrimaryColor;
  GColor  SecondaryColor;
  GColor  TextColor;
  uint8_t TemperatureUnit;    // 0 = Celsius, 1 = Fahrenheit
  uint8_t CountdownPosition;  // 0 = top (above event name), 1 = bottom
} ClaySettings;

static ClaySettings s_settings;

static Window    *s_window;
static Layer     *s_bg_layer;
static Layer     *s_status_layer;
static Layer     *s_card_layer;
static TextLayer *s_time_layer;
static TextLayer *s_date_layer;
static TextLayer *s_countdown_label_layer;  // "In X hours"
static TextLayer *s_event_title_layer;
static TextLayer *s_weather_layer;

static char s_event_title[33];
static int  s_event_hour;
static int  s_event_minute;
static bool s_has_event;
static bool s_show_card;  // event within 24 hours

static char s_time_buf[8];
static char s_date_buf[20];
static char s_countdown_label_buf[24];
static char s_weather_buf[24];

static int32_t s_temperature;
static char    s_conditions[16];
static bool    s_has_weather;

static uint8_t s_battery_pct;
static bool    s_bt_connected;

// Cached layout values
static int s_time_h;
static int s_date_h;
static int s_date_px;   // date left margin (≈ left edge of centered time text)
static int s_status_h;

#ifdef PBL_COLOR
static GPoint    s_diag_pts[3];
static GPathInfo s_diag_info = { .num_points = 3, .points = NULL };
static GPath    *s_diag_path = NULL;
#endif

// ---- Settings ----

static void prv_default_settings(void) {
  s_settings.PrimaryColor        = PBL_IF_COLOR_ELSE(GColorCadetBlue, GColorBlack);
  s_settings.SecondaryColor      = PBL_IF_COLOR_ELSE(GColorSunsetOrange, GColorBlack);
  s_settings.TextColor           = GColorWhite;
  s_settings.TemperatureUnit     = 0;
  s_settings.CountdownPosition   = 0;
}

static void prv_save_settings(void) {
  persist_write_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

static void prv_load_settings(void) {
  prv_default_settings();
  persist_read_data(SETTINGS_KEY, &s_settings, sizeof(s_settings));
}

// ---- Background layer ----

static void bg_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, s_settings.PrimaryColor);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 0, GCornerNone);

#ifdef PBL_COLOR
  if (s_diag_path) {
    graphics_context_set_fill_color(ctx, s_settings.SecondaryColor);
    gpath_draw_filled(ctx, s_diag_path);
  }
#endif
}

// ---- Status layer ----

static void status_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_stroke_color(ctx, s_settings.TextColor);
  graphics_context_set_fill_color(ctx, s_settings.TextColor);

  int bat_x = 4, bat_y = 4, bat_w = 20, bat_h = 9;
  graphics_draw_rect(ctx, GRect(bat_x, bat_y, bat_w, bat_h));
  graphics_fill_rect(ctx,
    GRect(bat_x + bat_w, bat_y + 3, 2, bat_h - 6), 0, GCornerNone);
  int fill_w = (bat_w - 2) * s_battery_pct / 100;
  if (fill_w > 0) {
    graphics_fill_rect(ctx,
      GRect(bat_x + 1, bat_y + 1, fill_w, bat_h - 2), 0, GCornerNone);
  }
  if (s_bt_connected) {
    graphics_fill_circle(ctx, GPoint(bat_x + bat_w + 7, bat_y + bat_h / 2), 3);
  }
}

// ---- Card layer ----

static void card_update_proc(Layer *layer, GContext *ctx) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  graphics_fill_rect(ctx, layer_get_bounds(layer), 8, GCornersAll);
}

// ---- Card interior layout (positions countdown label vs event title) ----

static void prv_apply_card_layout(void) {
  if (!s_card_layer || !s_countdown_label_layer || !s_event_title_layer) return;
  GRect cb = layer_get_bounds(s_card_layer);
  int card_h = cb.size.h;
  int card_w = cb.size.w;
  int ci_px  = 10;
  int ci_w   = card_w - 2 * ci_px;
  int lbl_h  = (card_h >= 50) ? 22 : 18;
  int margin = 10;
  int gap    = 8;

  int lbl_y, title_y, title_h;
  if (s_settings.CountdownPosition == 0) {
    lbl_y   = margin;
    title_y = lbl_y + lbl_h + gap;
    title_h = card_h - title_y - margin;
  } else {
    title_y = margin;
    lbl_y   = card_h - margin - lbl_h;
    title_h = lbl_y - title_y - gap;
  }
  if (title_h < 20) title_h = 20;

  layer_set_frame(text_layer_get_layer(s_countdown_label_layer),
    GRect(ci_px, lbl_y, ci_w, lbl_h));
  layer_set_frame(text_layer_get_layer(s_event_title_layer),
    GRect(ci_px, title_y, ci_w, title_h));
}

// ---- Dynamic layout (repositions time+date when card shown/hidden) ----

static void prv_apply_layout(bool show_card) {
  if (!s_window) return;
  GRect bounds = layer_get_bounds(window_get_root_layer(s_window));
  int w = bounds.size.w;
  int h = bounds.size.h;
  bool is_round = PBL_IF_ROUND_ELSE(true, false);
  int py = is_round ? h * 6 / 100 : 0;

  int time_y, date_y;

  if (show_card) {
    time_y = s_status_h + py + 2;
  } else {
    // Vertically center the time+date block
    int block_h = s_time_h + s_date_h;
    time_y = (h - block_h) / 2;
    if (time_y < s_status_h + py + 2) time_y = s_status_h + py + 2;
  }
  date_y = time_y + s_time_h;

  layer_set_frame(text_layer_get_layer(s_time_layer),
    GRect(0, time_y, w, s_time_h));
  layer_set_frame(text_layer_get_layer(s_date_layer),
    GRect(s_date_px, date_y, w - s_date_px, s_date_h));
}

// ---- Time / Date ----

static void prv_update_time(void) {
  time_t t = time(NULL);
  struct tm *tm = localtime(&t);
  if (clock_is_24h_style()) {
    strftime(s_time_buf, sizeof(s_time_buf), "%H:%M", tm);
  } else {
    strftime(s_time_buf, sizeof(s_time_buf), "%I:%M", tm);
  }
  strftime(s_date_buf, sizeof(s_date_buf), "%a %d %b", tm);
  text_layer_set_text(s_time_layer, s_time_buf);
  text_layer_set_text(s_date_layer, s_date_buf);
}

// ---- Event display ----

static void prv_update_event_display(void) {
  // Only show card if event is within the next 24 hours
  bool show_card = s_has_event && s_event_hour < 24;
  s_show_card = show_card;

  layer_set_hidden(s_card_layer, !show_card);
  prv_apply_layout(show_card);

  if (show_card) {
    // "In X hours" / "In X min" label
    if (s_event_hour > 0) {
      snprintf(s_countdown_label_buf, sizeof(s_countdown_label_buf),
               s_event_hour == 1 ? "In 1 hour" : "In %d hours", s_event_hour);
    } else {
      snprintf(s_countdown_label_buf, sizeof(s_countdown_label_buf),
               s_event_minute == 1 ? "In 1 min" : "In %d min", s_event_minute);
    }

    text_layer_set_text(s_countdown_label_layer, s_countdown_label_buf);
    text_layer_set_text(s_event_title_layer, s_event_title);
  }
}

static void prv_update_weather_display(void) {
  if (s_has_weather) {
    int temp = (int)s_temperature;
    const char *unit = s_settings.TemperatureUnit ? "\xc2\xb0""F" : "\xc2\xb0""C";
    snprintf(s_weather_buf, sizeof(s_weather_buf), "%s %d%s",
             s_conditions, temp, unit);
  } else {
    s_weather_buf[0] = '\0';
  }
  text_layer_set_text(s_weather_layer, s_weather_buf);
}

// ---- AppMessage ----

static void prv_request_update(void) {
  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) == APP_MSG_OK) {
    dict_write_uint8(iter, MESSAGE_KEY_REQUEST_UPDATE, 1);
    app_message_outbox_send();
  }
}

static void inbox_received_callback(DictionaryIterator *iterator, void *context) {
  bool event_changed    = false;
  bool settings_changed = false;
  Tuple *t;

  t = dict_find(iterator, MESSAGE_KEY_HAS_EVENT);
  if (t) { s_has_event = (t->value->int32 == 1); event_changed = true; }

  t = dict_find(iterator, MESSAGE_KEY_EVENT_TITLE);
  if (t) { snprintf(s_event_title, sizeof(s_event_title), "%s", t->value->cstring); }

  t = dict_find(iterator, MESSAGE_KEY_EVENT_HOUR);
  if (t) { s_event_hour = (int)t->value->int32; }

  t = dict_find(iterator, MESSAGE_KEY_EVENT_MINUTE);
  if (t) { s_event_minute = (int)t->value->int32; }

  t = dict_find(iterator, MESSAGE_KEY_TEMPERATURE);
  if (t) { s_temperature = t->value->int32; s_has_weather = true; prv_update_weather_display(); }

  t = dict_find(iterator, MESSAGE_KEY_CONDITIONS);
  if (t) { snprintf(s_conditions, sizeof(s_conditions), "%s", t->value->cstring); prv_update_weather_display(); }

  t = dict_find(iterator, MESSAGE_KEY_PrimaryColor);
  if (t) { s_settings.PrimaryColor = GColorFromHEX(t->value->int32); settings_changed = true; }

  t = dict_find(iterator, MESSAGE_KEY_SecondaryColor);
  if (t) { s_settings.SecondaryColor = GColorFromHEX(t->value->int32); settings_changed = true; }

  t = dict_find(iterator, MESSAGE_KEY_TextColor);
  if (t) { s_settings.TextColor = GColorFromHEX(t->value->int32); settings_changed = true; }

  t = dict_find(iterator, MESSAGE_KEY_TemperatureUnit);
  if (t) { s_settings.TemperatureUnit = (uint8_t)t->value->int32; settings_changed = true; prv_update_weather_display(); }

  t = dict_find(iterator, MESSAGE_KEY_CountdownPosition);
  if (t) { s_settings.CountdownPosition = (uint8_t)t->value->int32; settings_changed = true; prv_apply_card_layout(); }

  if (settings_changed) {
    prv_save_settings();
    window_set_background_color(s_window, s_settings.PrimaryColor);
    text_layer_set_text_color(s_time_layer, s_settings.TextColor);
    text_layer_set_text_color(s_date_layer, s_settings.TextColor);
    text_layer_set_text_color(s_weather_layer, s_settings.TextColor);
    layer_mark_dirty(s_bg_layer);
    layer_mark_dirty(s_status_layer);
  }

  if (event_changed) prv_update_event_display();
}

static void inbox_dropped_callback(AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Inbox dropped: %d", (int)reason);
}

static void outbox_failed_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
  APP_LOG(APP_LOG_LEVEL_ERROR, "Outbox failed: %d", (int)reason);
}

static void outbox_sent_callback(DictionaryIterator *iterator, void *context) {}

// ---- Tick handler ----

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  prv_update_time();

  if (!s_has_event) return;

  if (s_event_hour == 0 && s_event_minute == 0) {
    s_has_event = false;
    prv_update_event_display();
    prv_request_update();
  } else if (s_event_minute > 0) {
    s_event_minute--;
  } else {
    s_event_hour--;
    s_event_minute = 59;
  }
  prv_update_event_display();

  if (tick_time->tm_min % 30 == 0) {
    prv_request_update();
  }
}

// ---- Battery / BT ----

static void battery_handler(BatteryChargeState state) {
  s_battery_pct = state.charge_percent;
  if (s_status_layer) layer_mark_dirty(s_status_layer);
}

static void bt_handler(bool connected) {
  s_bt_connected = connected;
  if (s_status_layer) layer_mark_dirty(s_status_layer);
}

// ---- Window load/unload ----

static void main_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(root);
  int w = bounds.size.w;
  int h = bounds.size.h;

  bool large    = (h >= 200);
  bool is_round = PBL_IF_ROUND_ELSE(true, false);

  int py   = is_round ? h * 6 / 100 : 0;
  int px_c = is_round ? w * 6 / 100 : 6;   // card horizontal margin

  s_status_h = 16;
  s_time_h   = large ? 58 : 48;
  s_date_h   = large ? 26 : 20;
  // date left margin ≈ left edge of centered time text.
  // ROBOTO_BOLD_SUBSET_49: "HH:MM" ≈ 130px on 200px → starts at ~35px.
  // LECO_38: "HH:MM" ≈ 90px on 144px → starts at ~27px.
  s_date_px  = large ? w * 17 / 100 : w * 18 / 100;

  // ---- Weather (top-right) ----
  int weather_x = w / 2;
  int weather_y = py + 1;
  int weather_w = w - weather_x - px_c;
  int weather_h = s_status_h - 1;

  // ---- Card (positioned near bottom; final y set by prv_apply_layout) ----
  // Compute time+date block bottom when shown at top
  int time_y_top  = s_status_h + py + 2;
  int date_y_top  = time_y_top + s_time_h;
  int card_y      = date_y_top + s_date_h + (large ? 10 : 8);
  int card_w      = w - 2 * px_c;
  int card_h      = h - card_y - py - 4;
  if (card_h < 60) card_h = 60;

  // Card interior (relative to card origin)
  int ci_px    = 10;
  int ci_w     = card_w - 2 * ci_px;
  int lbl_y    = 10;
  int lbl_h    = large ? 22 : 18;       // extra height so font isn't clipped
  int title_y  = lbl_y + lbl_h + 8;
  int title_h  = card_h - title_y - 10;
  if (title_h < 20) title_h = 20;

  // ---- Diagonal background ----
#ifdef PBL_COLOR
  s_diag_info.points = s_diag_pts;
  s_diag_pts[0] = GPoint(w * 50 / 100, 0);
  s_diag_pts[1] = GPoint(w, 0);
  s_diag_pts[2] = GPoint(w, h * 50 / 100);
  s_diag_path = gpath_create(&s_diag_info);
#endif

  // ---- Create layers ----
  s_bg_layer = layer_create(bounds);
  layer_set_update_proc(s_bg_layer, bg_update_proc);

  s_status_layer = layer_create(GRect(0, py, w, s_status_h));
  layer_set_update_proc(s_status_layer, status_update_proc);

  // Time: full width, centered text
  s_time_layer = text_layer_create(GRect(0, time_y_top, w, s_time_h));
  text_layer_set_background_color(s_time_layer, GColorClear);
  text_layer_set_text_color(s_time_layer, s_settings.TextColor);
  text_layer_set_font(s_time_layer, large
    ? fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49)
    : fonts_get_system_font(FONT_KEY_LECO_38_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_time_layer, GTextAlignmentCenter);

  // Date: left-aligned, starting at s_date_px
  s_date_layer = text_layer_create(
    GRect(s_date_px, date_y_top, w - s_date_px, s_date_h));
  text_layer_set_background_color(s_date_layer, GColorClear);
  text_layer_set_text_color(s_date_layer, s_settings.TextColor);
  text_layer_set_font(s_date_layer, fonts_get_system_font(
    large ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_date_layer, GTextAlignmentLeft);

  // Weather: top-right
  s_weather_layer = text_layer_create(
    GRect(weather_x, weather_y, weather_w, weather_h));
  text_layer_set_background_color(s_weather_layer, GColorClear);
  text_layer_set_text_color(s_weather_layer, s_settings.TextColor);
  text_layer_set_font(s_weather_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_weather_layer, GTextAlignmentRight);

  // Card
  s_card_layer = layer_create(GRect(px_c, card_y, card_w, card_h));
  layer_set_update_proc(s_card_layer, card_update_proc);

  // "In X hours" label
  s_countdown_label_layer = text_layer_create(
    GRect(ci_px, lbl_y, ci_w, lbl_h));
  text_layer_set_background_color(s_countdown_label_layer, GColorClear);
  text_layer_set_text_color(s_countdown_label_layer, GColorDarkGray);
  text_layer_set_font(s_countdown_label_layer, fonts_get_system_font(
    large ? FONT_KEY_GOTHIC_18 : FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_countdown_label_layer, GTextAlignmentLeft);

  // Event title
  s_event_title_layer = text_layer_create(
    GRect(ci_px, title_y, ci_w, title_h));
  text_layer_set_background_color(s_event_title_layer, GColorClear);
  text_layer_set_text_color(s_event_title_layer, GColorBlack);
  text_layer_set_font(s_event_title_layer, fonts_get_system_font(
    large ? FONT_KEY_GOTHIC_28_BOLD : FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_alignment(s_event_title_layer, GTextAlignmentLeft);
  text_layer_set_overflow_mode(s_event_title_layer,
    GTextOverflowModeWordWrap);

  // Layer hierarchy
  layer_add_child(root, s_bg_layer);
  layer_add_child(root, s_status_layer);
  layer_add_child(root, text_layer_get_layer(s_time_layer));
  layer_add_child(root, text_layer_get_layer(s_date_layer));
  layer_add_child(root, text_layer_get_layer(s_weather_layer));
  layer_add_child(root, s_card_layer);
  layer_add_child(s_card_layer, text_layer_get_layer(s_countdown_label_layer));
  layer_add_child(s_card_layer, text_layer_get_layer(s_event_title_layer));

  s_show_card = false;
  layer_set_hidden(s_card_layer, true);
  prv_apply_card_layout();
  prv_update_time();
  prv_apply_layout(false);  // center time+date before JS responds
}

static void main_window_unload(Window *window) {
  text_layer_destroy(s_event_title_layer);
  text_layer_destroy(s_countdown_label_layer);
  layer_destroy(s_card_layer);
  text_layer_destroy(s_weather_layer);
  text_layer_destroy(s_date_layer);
  text_layer_destroy(s_time_layer);
  layer_destroy(s_status_layer);
  layer_destroy(s_bg_layer);
#ifdef PBL_COLOR
  if (s_diag_path) { gpath_destroy(s_diag_path); s_diag_path = NULL; }
#endif
}

// ---- Init / deinit ----

static void init(void) {
  prv_load_settings();

  s_event_title[0] = '\0';
  s_has_event    = false;
  s_show_card    = false;
  s_event_hour   = 0;
  s_event_minute = 0;
  s_has_weather  = false;
  s_temperature  = 0;
  s_conditions[0] = '\0';
  s_weather_buf[0] = '\0';

  BatteryChargeState bat = battery_state_service_peek();
  s_battery_pct   = bat.charge_percent;
  s_bt_connected  = connection_service_peek_pebble_app_connection();

  s_window = window_create();
  window_set_background_color(s_window, s_settings.PrimaryColor);
  WindowHandlers handlers = {
    .load   = main_window_load,
    .unload = main_window_unload
  };
  window_set_window_handlers(s_window, handlers);
  window_stack_push(s_window, true);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  battery_state_service_subscribe(battery_handler);
  connection_service_subscribe(
    (ConnectionHandlers){ .pebble_app_connection_handler = bt_handler });

  app_message_register_inbox_received(inbox_received_callback);
  app_message_register_inbox_dropped(inbox_dropped_callback);
  app_message_register_outbox_failed(outbox_failed_callback);
  app_message_register_outbox_sent(outbox_sent_callback);
  app_message_open(512, 64);

  prv_request_update();
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  battery_state_service_unsubscribe();
  connection_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
