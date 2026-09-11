/* Music box UI, display API v2. No HAL, dynamic allocation or framebuffer. */
#include "display_api.h"
#include "font.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RGB(r, g, b)                                                           \
  (uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))
#define BG RGB(17, 27, 42)
#define PANEL RGB(28, 42, 61)
#define BTN RGB(36, 53, 74)
#define BLUE RGB(105, 186, 255)
#define TEXT RGB(232, 239, 249)
#define MUTED RGB(155, 176, 201)
#define MOTOR_OFF RGB(80, 80, 80)
#define RED RGB(184, 62, 81)
#define GREEN RGB(80, 210, 173)
#define WHITE 0xffff
#define SETTINGS_ADDR 0x0df000u
#define SAVED_SONG_COUNT 10u
#define RX_SIZE 512u
#define NOTES 96u
#define DIRTY 32u
#ifndef MUSIC_BOX_BAND
#define MUSIC_BOX_BAND 2
#endif
#ifndef MUSIC_BOX_MIDI_INTERVAL_MS
#define MUSIC_BOX_MIDI_INTERVAL_MS 200
#endif
#ifndef MUSIC_BOX_LIVE
#define MUSIC_BOX_LIVE 0
#endif
#ifndef MUSIC_BOX_CONTROLLER
#define MUSIC_BOX_CONTROLLER 0
#endif
#ifndef MUSIC_BOX_PIXEL_MIDI
#define MUSIC_BOX_PIXEL_MIDI 0
#endif
#define BAND MUSIC_BOX_BAND

typedef struct {
  uint32_t mhz;
  uint8_t flags, note;
} Motor;
typedef struct {
  int16_t x, y, w, h;
} Rect;
typedef struct {
  uint32_t start, end;
  uint8_t motor, pitch, open, used;
} MidiNote;
static const DisplayPlatform *p;
static Motor motors[6];
/* Overview presentation only. Raw motor state and MIDI history stay immediate. */
static Motor overview[6];
static uint32_t overview_started[6], overview_off[6];
static uint8_t overview_was_active[6];
#define OVERVIEW_MIN_MS 1000u
#define OVERVIEW_GAP_MS 1000u
static uint8_t flags, sleeping, resetting, micro,
    mask = 63, page, selected, settings_page, show_hz, width_index = 2;
static const uint8_t widths[3] = {25, 50, 100};
static uint32_t position, duration, now, last_state, last_paint, midi_now,
    last_midi_draw;
static uint8_t midi_dirty;
static uint32_t midi_source_time;
static uint32_t midi_tail_ms;
static uint8_t midi_source_valid, midi_stop_latched, midi_input_running;
static uint8_t have_state;
static char title[49] = "Нет композиции", notice[48];
static uint32_t notice_until;
static MidiNote notes[NOTES];
static unsigned note_next;
static const uint16_t voice_colors[6] = {
    RGB(106, 201, 255), RGB(189, 157, 255), RGB(118, 223, 187),
    RGB(255, 206, 115), RGB(249, 148, 194), RGB(143, 169, 255)};
#if MUSIC_BOX_PIXEL_MIDI
// Exact previous MIDI image: 4 bits/pixel for its 12 RGB565 colours, no hashes.
// 15 means unknown. Other pages invalidate this cache before entering MIDI again.
static uint8_t midi_image[480 * 210 / 2];
static uint8_t midi_image_valid;
static uint8_t midi_changed[(480*MUSIC_BOX_BAND+7)/8];
static int midi_changed_at(unsigned i){return (midi_changed[i/8]>>(i%8))&1;}
static void midi_mark(unsigned i){midi_changed[i/8]|=(uint8_t)(1u<<(i%8));}
static void midi_unmark(unsigned i){midi_changed[i/8]&=(uint8_t)~(1u<<(i%8));}
static unsigned midi_pixel_code(uint16_t color) {
  static const uint16_t colors[] = {BG,PANEL,BTN,MUTED,WHITE,TEXT,
    RGB(106,201,255),RGB(189,157,255),RGB(118,223,187),
    RGB(255,206,115),RGB(249,148,194),RGB(143,169,255)};
  for(unsigned i=0;i<sizeof(colors)/sizeof(colors[0]);++i)
    if(colors[i]==color)return i;
  return 15;
}
#endif
static Rect dirty[DIRTY], clip;
static unsigned dirty_count;
static uint16_t pixels[480 * BAND], rotated[480 * BAND];
static volatile uint16_t rx_in, rx_out;
static volatile uint8_t rx_bad;
static volatile uint8_t rx[RX_SIZE];
static uint8_t packet[247];
static unsigned packet_n, packet_total;
static uint32_t rx_time;
static uint8_t seq, pending, pending_tries, action_packet[13];
static uint32_t pending_at;
static uint8_t touch_down, drag_seek;
static int16_t down_x, down_y;
static uint8_t numeric;
static char digits[9];
static uint8_t fresh_digits;
static uint8_t input_note = 69;
static const uint8_t natural_pitches[7] = {0,2,4,5,7,9,11};
static const char *natural_names[7] = {"C","D","E","F","G","A","B"};
static uint8_t settings_dirty;
static uint32_t settings_at;
#if MUSIC_BOX_CONTROLLER
static unsigned controller_source;
static unsigned connection_status;
static char saved_titles[SAVED_SONG_COUNT][32];
static char playback_error[128];
static uint8_t initial_render_done;
static uint32_t saved_durations[SAVED_SONG_COUNT],saved_offset;
static uint8_t saved_present[SAVED_SONG_COUNT],saved_selected,saved_playing,save_stage,save_percent;
static uint8_t pc_range[2]={255,255},saved_range[2]={255,255};
static unsigned pc_song_visible(void) {return (flags&16)&&duration&&!saved_playing;}
#endif
const DisplayModule display_module = {DISPLAY_API_VERSION, 0, 0, 0, 0};

static uint32_t get32(const uint8_t *b) {
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
         ((uint32_t)b[3] << 24);
}
static void put32(uint8_t *b, uint32_t n) {
  unsigned i;
  for (i = 0; i < 4; i++)
    b[i] = (uint8_t)(n >> (8 * i));
}
static uint16_t crc16(const uint8_t *b, unsigned n) {
  uint16_t crc = 0xffff;
  unsigned i;
  while (n--) {
    crc ^= (uint16_t)*b++ << 8;
    for (i = 0; i < 8; i++)
      crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
  }
  return crc;
}
static int inside(int x, int y, int a, int b, int w, int h) {
  return x >= a && y >= b && x < a + w && y < b + h;
}
static void invalidate(int x, int y, int w, int h) {
  unsigned i;
  if (x < 0) {
    w += x;
    x = 0;
  }
  if (y < 0) {
    h += y;
    y = 0;
  }
  if (x + w > 480)
    w = 480 - x;
  if (y + h > 320)
    h = 320 - y;
  if (w <= 0 || h <= 0)
    return;
  for (i = 0; i < dirty_count; i++) {
    Rect *r = &dirty[i];
    if (x >= r->x && y >= r->y && x + w <= r->x + r->w && y + h <= r->y + r->h)
      return;
    // Merge waiting updates without rewinding the strip currently being painted.
    if (i && x < r->x + r->w && x + w > r->x && y < r->y + r->h && y + h > r->y) {
      int x0 = x < r->x ? x : r->x, y0 = y < r->y ? y : r->y;
      int x1 = x + w > r->x + r->w ? x + w : r->x + r->w;
      int y1 = y + h > r->y + r->h ? y + h : r->y + r->h;
      *r = (Rect){x0,y0,x1-x0,y1-y0};
      return;
    }
  }
  if (dirty_count == DIRTY) {
    dirty_count = 1;
    dirty[0] = (Rect){0, 0, 480, 320};
    return;
  }
  dirty[dirty_count++] = (Rect){(int16_t)x, (int16_t)y, (int16_t)w, (int16_t)h};
}
static void full(void) {
  dirty_count = 0;
  invalidate(0, 0, 480, 320);
}
static void fill(int x, int y, int w, int h, uint16_t c) {
  int xx, yy, x1 = x + w, y1 = y + h;
  if (x < clip.x)
    x = clip.x;
  if (y < clip.y)
    y = clip.y;
  if (x1 > clip.x + clip.w)
    x1 = clip.x + clip.w;
  if (y1 > clip.y + clip.h)
    y1 = clip.y + clip.h;
  for (yy = y; yy < y1; yy++)
    for (xx = x; xx < x1; xx++)
      pixels[(yy - clip.y) * clip.w + xx - clip.x] = c;
}
static uint32_t utf8(const char **s) {
  uint32_t c = (uint8_t)*(*s)++;
  if (c < 128)
    return c;
  if ((c & 0xe0) == 0xc0 && **s) {
    c = ((c & 31) << 6) | ((uint8_t)*(*s)++ & 63);
    return c;
  }
  if ((c & 0xf0) == 0xe0 && **s) {
    c = ((c & 15) << 6) | ((uint8_t)*(*s)++ & 63);
    if (**s)
      c = (c << 6) | ((uint8_t)*(*s)++ & 63);
    return c;
  }
  return '?';
}
static unsigned glyph(uint32_t c) {
  if (c >= 32 && c < 127)
    return (unsigned)c - 32;
  if (c >= 0x410 && c < 0x450)
    return 95 + (unsigned)c - 0x410;
  if (c == 0x401)
    return 159;
  if (c == 0x451)
    return 160;
  return '?' - 32;
}
static void text(int x, int y, const char *s, uint16_t color, int scale) {
  int xx, yy;
  unsigned g;
  if (y >= clip.y + clip.h || y + 16 * scale <= clip.y)
    return;
  while (*s && x < 480) {
    uint32_t code = utf8(&s);
    if (code == 0x2022) {
      fill(x + 3 * scale, y + 6 * scale, 3 * scale, 3 * scale, color);
      x += 9 * scale;
      continue;
    }
    g = glyph(code);
    for (yy = 0; yy < 16; yy++)
      for (xx = 0; xx < 9; xx++)
        if (font_rows[g][yy] & (1u << xx))
          fill(x + xx * scale, y + yy * scale, scale, scale, color);
    x += 9 * scale;
  }
}
static void motor_label(int x, int y, const char *s) {
  while (*s) {
    unsigned g = glyph(utf8(&s));
    for (int yy=0; yy<22; ++yy)
      for (int xx=0; xx<12; ++xx)
        if (font_rows[g][yy*16/22] & (1u << (xx*9/12)))
          fill(x+xx,y+yy,1,1,BLUE);
    x+=12;
  }
}
static int textlen(const char *s) {
  int n = 0;
  while (*s) {
    utf8(&s);
    n++;
  }
  return n;
}
void music_box_boot_error(const DisplayPlatform *platform) {
  /* Reuse the Cyrillic font and strip buffer without initializing the app. */
  const char *first = "Ошибка внутреннего обмена.";
  const char *second = "Попробуйте перезапустить плату";
  p = platform;
  dirty_count = 0;
  for (int y = 0; y < 320; y += BAND) {
    int height = y + BAND > 320 ? 320 - y : BAND;
    clip = (Rect){0, (int16_t)y, 480, (int16_t)height};
    fill(0, y, 480, height, BG);
    text((480 - textlen(first) * 9) / 2, 136, first, TEXT, 1);
    text((480 - textlen(second) * 9) / 2, 168, second, TEXT, 1);
    p->write_rect(0, (uint16_t)y, 480, (uint16_t)height, pixels, 480, 0);
  }
}
static void button(int x, int y, int w, int h, const char *s, uint16_t c) {
  fill(x, y, w, h, c);
  fill(x, y, w, 1, MUTED);
  fill(x, y + h - 1, w, 1, MUTED);
  fill(x, y, 1, h, MUTED);
  fill(x + w - 1, y, 1, h, MUTED);
  text(x + (w - textlen(s) * 9) / 2, y + (h - 16) / 2, s, TEXT, 1);
}
static void time_text(char *s, uint32_t ms) {
  unsigned v = (unsigned)(ms / 1000);
  snprintf(s, 16, "%u:%02u", v / 60, v % 60);
}
static void note_text(char *s, uint8_t n) {
  static const char *names[] = {"C",  "C#", "D",  "D#", "E",  "F",
                                "F#", "G",  "G#", "A",  "A#", "B"};
  if (n > 127)
    strcpy(s, "--");
  else
    snprintf(s, 16, "%s%d", names[n % 12], (int)n / 12 - 1);
}
static uint32_t note_frequency(unsigned note) {
  static const uint32_t base[12] = {261626,277183,293665,311127,329628,349228,
                                   369994,391995,415305,440000,466164,493883};
  int octave = (int)note / 12 - 5;
  return octave >= 0 ? base[note % 12] << octave : base[note % 12] >> -octave;
}
static unsigned motor_note(const Motor *m) {
  unsigned best = 16;
  uint32_t distance = UINT32_MAX;
  for (unsigned n = 16; n <= 107; ++n) {
    uint32_t f = note_frequency(n);
    uint32_t d = f > m->mhz ? f - m->mhz : m->mhz - f;
    if (d < distance) { best = n; distance = d; }
  }
  return best;
}
static int bar_height(const Motor *m) {
  uint32_t f = m->mhz;
  unsigned octave = 0, frac;
  if (!(m->flags & 2) && (m->flags & 24) != 24) return 0;
#if MUSIC_BOX_CONTROLLER
  if (save_stage == 5 || saved_playing || ((flags & 32) && pc_song_visible())) {
    /* Same test chord as STM32 engine_boot_test, limited to installed motors. */
    static const uint8_t test_notes[6] = {48,52,55,60,64,67};
    uint8_t test_range[2] = {127,0};
    for (unsigned i=0; i<6; ++i) if (mask & (1u<<i)) {
      if (test_notes[i]<test_range[0]) test_range[0]=test_notes[i];
      if (test_notes[i]>test_range[1]) test_range[1]=test_notes[i];
    }
    const uint8_t *range = save_stage == 5 ? test_range : saved_playing ? saved_range : pc_range;
    if (range[0] <= range[1] && range[1] < 128 && m->note < 128) {
      int span = range[1] - range[0], note = (int)m->note - range[0];
      if (!span) return 92;
      if (note < 0) note = 0;
      if (note > span) note = span;
      return (92 * (span + 9 * note) + 5 * span) / (10 * span);
    }
  }
#endif
  if (f < 20000)
    return 0;
  if (f >= 1200000)
    return 92;
  f = f * 256 / 20000;
  while (f >= 512) {
    f >>= 1;
    octave++;
  }
  frac = (unsigned)f - 256;
  return (int)((octave * 256 + frac) * 92 / 1512);
}
static void motor_icon(int x, int y, int size, uint16_t c) {
  int k, dx, dy, r = size * 30 / 100, center = size / 2;
  if (x >= clip.x + clip.w || x + size <= clip.x || y >= clip.y + clip.h ||
      y + size <= clip.y)
    return;
  fill(x + 4, y, size - 8, 2, c);
  fill(x + 4, y + size - 2, size - 8, 2, c);
  fill(x, y + 4, 2, size - 8, c);
  fill(x + size - 2, y + 4, 2, size - 8, c);
  // Join the four sides with chamfers, keeping the two-pixel outline continuous.
  for (k = 0; k < 4; ++k) {
    fill(x + 4 - k, y + k, 2, 1, c);
    fill(x + size - 6 + k, y + k, 2, 1, c);
    fill(x + 4 - k, y + size - 1 - k, 2, 1, c);
    fill(x + size - 6 + k, y + size - 1 - k, 2, 1, c);
  }
  for (k = 0; k < 4; k++)
    for (dy = -1; dy <= 1; ++dy)
      for (dx = -1; dx <= 1; ++dx)
        if (dx * dx + dy * dy <= 1)
          fill(x + 5 + (k % 2) * (size - 10) + dx,
               y + 5 + (k / 2) * (size - 10) + dy, 1, 1, c == MOTOR_OFF ? MOTOR_OFF : MUTED);
  for (dy = -r; dy <= r; dy++)
    for (dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if ((d <= r * r && d >= (r - 1) * (r - 1)) ||
          (d <= (r - 4) * (r - 4) && d >= (r - 5) * (r - 5)))
        fill(x + center + dx, y + center + dy, 1, 1, c);
      if (d <= 6)
        fill(x + center + dx, y + center + dy, 1, 1, c == MOTOR_OFF ? MOTOR_OFF : TEXT);
    }
}
static void notification(const char *s) {
  strncpy(notice, s, sizeof(notice) - 1);
  notice[sizeof(notice) - 1] = 0;
  notice_until = now + 2000;
  invalidate(0, 28, 480, 26);
}
static int midi_timeline_running(void) {
  if (midi_stop_latched) return 0;
#if MUSIC_BOX_CONTROLLER
  if (midi_input_running) return 1;
#else
  if ((flags & 8) && !(flags & 4)) return 1;
#endif
  return have_state && (flags & 1) && !(flags & 4) && !sleeping && !resetting && (flags & (2|64));
}
static void midi_close_notes(void) {
  for (unsigned i=0;i<NOTES;++i) if(notes[i].used&&notes[i].open) {
    notes[i].end=midi_now;notes[i].open=0;midi_dirty=1;
  }
}
static void midi_clock_update(uint32_t at, int running) {
  if (midi_stop_latched || (!running && (flags & 4))) midi_tail_ms=0;
  if (midi_source_valid) {
    int32_t delta=(int32_t)(at-midi_source_time);
    if (delta<0) {
      memset(notes,0,sizeof(notes));note_next=0;midi_dirty=1;midi_tail_ms=0;
    } else if (running && delta) { midi_now+=(uint32_t)delta;midi_dirty=1; }
    else if (delta && midi_tail_ms) {
      uint32_t advance=(uint32_t)delta<midi_tail_ms?(uint32_t)delta:midi_tail_ms;
      midi_now+=advance;midi_tail_ms-=advance;midi_dirty=1;
    }
  }
  if(running)midi_tail_ms=1000;
  midi_source_time=at;midi_source_valid=1;
}
static void send_action(uint8_t action, uint8_t motor, uint32_t value) {
  uint16_t crc;
  if (pending && action != 0) {
    notification("Подождите ответа");
    return;
  }
  if (!(flags & 1) && action != 0) {
    notification("Нет связи с устройством");
    return;
  }
  action_packet[0] = 0xa5;
  if(action==0) {midi_stop_latched=1;midi_tail_ms=0;midi_close_notes();}
  else if((action==5&&value)||action==20||(action==1&&!(flags&2)))midi_stop_latched=0;
  action_packet[1] = 0x5a;
  action_packet[2] = 6;
  action_packet[3] = ++seq;
  action_packet[4] = 0x50;
  action_packet[5] = action;
  action_packet[6] = motor;
  put32(action_packet + 7, value);
  crc = crc16(action_packet + 2, 9);
  action_packet[11] = (uint8_t)crc;
  action_packet[12] = (uint8_t)(crc >> 8);
  pending = 1;
  pending_tries = 1;
  pending_at = now;
  if (p->send)
    p->send(action_packet, 13);
}
static void seek(int x) {
#if MUSIC_BOX_CONTROLLER
  if((!pc_song_visible()&&!saved_playing)||save_stage)return;
#endif
  uint32_t at;
  if (!duration)
    return;
  if (x < 8)
    x = 8;
  if (x > 348)
    x = 348;
  at = (uint32_t)((uint64_t)(x - 8) * duration / 340);
  if (at >= duration)
    at = duration - 1;
  send_action(2, 0, at);
}
static void draw_overview(void) {
  unsigned i;
  char s[40];
#if !MUSIC_BOX_LIVE
  char a[16], b[16];
#endif
  for (i = 0; i < 6; i++) {
    int x = 8 + (int)i * 78, h = bar_height(&overview[i]),
        w = 75 * widths[width_index] / 100;
    int visible_note = overview[i].flags & 2;
    fill(x + (75 - w) / 2, 128 - h, w, h, BLUE);
    if (show_hz)
      snprintf(s, sizeof(s), "%lu Гц",
               (unsigned long)(visible_note
                                   ? (overview[i].mhz + 500) / 1000
                                   : 0));
    else
      note_text(s, visible_note ? overview[i].note : 255);
    text(x + (75 - textlen(s) * 9) / 2, 130, s, TEXT, 1);
    fill(x, 150, 75, 48, PANEL);
    fill(x, 150, 75, 1, MUTED);
    fill(x, 197, 75, 1, MUTED);
    fill(x, 150, 1, 48, MUTED);
    fill(x + 74, 150, 1, 48, MUTED);
    motor_icon(x + 6, 156, 36, visible_note ? BLUE : MOTOR_OFF);
    snprintf(s, sizeof(s), MUSIC_BOX_LIVE ? "V%u" : "M%u", i + 1);
    motor_label(x + 46, 163, s);
  }
  fill(8, 206, 340, 50, PANEL);
#if MUSIC_BOX_CONTROLLER
  text(16, 211, pc_song_visible()?title:saved_present[saved_selected]?saved_titles[saved_selected]:"Выберите мелодию", TEXT, 1);
#else
  text(16, 211, title, TEXT, 1);
#endif
#if MUSIC_BOX_LIVE
  text(16, 233, "Ноты на выходе ESP32", MUTED, 1);
#else
  time_text(a, position);
#if MUSIC_BOX_CONTROLLER
  time_text(b, pc_song_visible()?duration:saved_durations[saved_selected]);
#else
  time_text(b, duration);
#endif
  snprintf(s, sizeof(s), "%s / %s", a, b);
  text(16, 229, s, MUTED, 1);
  fill(16, 249, 324, 3, BTN);
  if (duration)
    fill(16, 249, (int)((uint64_t)324 * position / duration), 3, BLUE);
#endif
#if MUSIC_BOX_CONTROLLER
  button(356,206,116,50,pc_song_visible()?((flags&2)?"Пауза":"Пуск"):(saved_playing?"Стоп":"Пуск"),saved_playing?RED:BLUE);
#else
  button(356, 206, 116, 50, (flags & 2) ? "Пауза" : "Играть", RGB(70,142,232));
#endif
}
static void draw_motor(void) {
  char s[32];
  Motor *m = &motors[selected];
  button(8, 34, 56, 48, "<", BTN);
  snprintf(s, sizeof(s), MUSIC_BOX_LIVE ? "ГОЛОС %u / 6" : "МОТОР %u / 6", selected + 1);
  text(170, 50, s, BLUE, 1);
  button(416, 34, 56, 48, ">", BTN);
  fill(8, 90, 160, 104, PANEL);
  motor_icon(18, 121, 40, ((m->flags & 2) || (m->flags & 24) == 24) ? BLUE : MOTOR_OFF);
  text(69, 94, "Нота", MUTED, 1);
  note_text(s, m->note < 128 ? m->note : motor_note(m));
  text(69, 112, s, TEXT, 2);
#if MUSIC_BOX_LIVE
  text(17, 169, (m->flags & 2) ? "Нота отправлена" : "Нет ноты", MUTED, 1);
  snprintf(s, sizeof(s), "%lu.%02lu Гц", (unsigned long)(m->mhz / 1000),
           (unsigned long)((m->mhz % 1000) / 10));
  text(188, 110, s, BLUE, 1);
  text(188, 150, "Частота задаётся MIDI", MUTED, 1);
  text(188, 178, "Голос на выходе ESP32", MUTED, 1);
  text(12, 225, "Без обратной связи от STM32", MUTED, 1);
#else
  text(17, 169,
       (m->flags & 2)   ? "STEP работает"
       : (m->flags & 1) ? "Удержание"
                        : "Отключён",
       MUTED, 1);
  button(8, 202, 160, 48, (m->flags & 1) ? "EN ВКЛ" : "EN ВЫКЛ",
         (m->flags & 1) ? RGB(36, 76, 72) : BTN);
  button(176, 90, 50, 48, "-", BTN);
  if (show_hz)
    snprintf(s, sizeof(s), "%lu.%02lu Гц", (unsigned long)(m->mhz / 1000),
             (unsigned long)((m->mhz % 1000) / 10));
  else note_text(s, motor_note(m));
  button(232, 90, 184, 48, "", BTN);
  text(240, 94, show_hz ? "Частота" : "Нота", MUTED, 1);
  text(240, 114, s, TEXT, 1);
  button(422, 90, 50, 48, "+", BTN);
  button(176, 146, 296, 48,
         (m->flags & 4) ? "Направление DIR 1" : "Направление DIR 0", BTN);
  button(176, 202, 296, 48, (m->flags & 2) ? "Остановить" : "Пуск мотора",
         (m->flags & 2) ? RED : RGB(70, 142, 232));
#endif
}
static void draw_song(void) {
#if MUSIC_BOX_CONTROLLER
  char s[48],time[16];
  snprintf(s,sizeof(s),"Мелодия %u / %u",saved_selected+1,SAVED_SONG_COUNT);
  text(170,42,s,MUTED,1);
  button(8,70,56,60,"<",BTN);button(416,70,56,60,">",BTN);
  text(76,82,saved_present[saved_selected]?saved_titles[saved_selected]:"Пустое место",TEXT,1);
  time_text(time,saved_durations[saved_selected]);text(76,112,time,MUTED,1);
  text(16,155,"Сохранить через MotorMusic Studio",MUTED,1);
  text(16,214,"Запуск: Обзор - Пуск",MUTED,1);
#else
#if MUSIC_BOX_LIVE
  text(16, 40, "ЖИВОЙ USB-MIDI", BLUE, 1);
  text(16, 80, (flags & 8) ? "Инструмент подключён" : "Подключите USB-MIDI", TEXT, 1);
  text(16, 112, "До шести голосов одновременно", MUTED, 1);
  text(16, 144, "После паузы нажмите ноты заново", MUTED, 1);
  text(16, 176, "СТОП ВСЁ отключает выход нот", MUTED, 1);
  button(16, 208, 448, 48, (flags & 2) ? "Пауза MIDI" : "Включить MIDI", BLUE);
#else
  char s[32], b[16];
  fill(8, 36, 264, 218, PANEL);
  text(20, 48, "КОМПОЗИЦИЯ", MUTED, 1);
  text(20, 82, title, TEXT, 1);
  text(20, 115, "6 голосов MIDI", MUTED, 1);
  time_text(s, position);
  time_text(b, duration);
  text(20, 180, s, BLUE, 2);
  text(20, 226, b, MUTED, 1);
  button(280, 36, 192, 48, (flags & 2) ? "Пауза" : "Играть", RGB(70, 142, 232));
  button(280, 92, 92, 48, "-10 сек", BTN);
  button(380, 92, 92, 48, "+10 сек", BTN);
  button(280, 148, 192, 48, "В начало", BTN);
  text(288, 217,
       (flags & 2)   ? "Воспроизведение"
       : (flags & 4) ? "Пауза"
                     : "Остановлено",
       MUTED, 1);
#endif
#endif
}
static void draw_settings(void) {
  unsigned i;
  char s[16];
  button(8, 34, 228, 48, "Экран", settings_page == 0 ? RGB(36, 76, 72) : BTN);
  button(244, 34, 228, 48, MUSIC_BOX_LIVE ? "MIDI" : "Драйверы", settings_page ? RGB(36, 76, 72) : BTN);
  if (!settings_page) {
    text(8, 90, "Под шкалами", MUTED, 1);
    button(150, 88, 155, 48, "Нота", !show_hz ? RGB(36, 76, 72) : BTN);
    button(313, 88, 159, 48, "Частота", show_hz ? RGB(36, 76, 72) : BTN);
    text(8, 150, "Ширина шкалы", MUTED, 1);
    for (i = 0; i < 3; i++) {
      snprintf(s, sizeof(s), "%u%%", widths[i]);
      button(8 + (int)i * 156, 174, 152, 48, s,
             width_index == i ? RGB(36, 76, 72) : BTN);
    }
  } else {
#if MUSIC_BOX_LIVE
    text(8, 96, "Живой MIDI: до шести голосов", TEXT, 1);
    text(8, 132, "Драйверы настроены на STM32", MUTED, 1);
    text(8, 168, "На экране показан выход ESP32", MUTED, 1);
    text(8, 204, "Остановка: кнопка СТОП ВСЁ", MUTED, 1);
#else
    static const uint8_t raw[] = {0, 1, 2, 3, 7};
    static const char *labels[] = {"Полный", "1/2", "1/4", "1/8", "1/16"};
    text(8, 86, "Микрошаг для всех моторов", MUTED, 1);
    for (i = 0; i < 5; i++)
      button(8 + (int)i * 94, 108, 88, 48, labels[i],
             micro == raw[i] ? RGB(36, 76, 72) : BTN);
    button(8, 174, 150, 48, sleeping ? "SLEEP ВКЛ" : "SLEEP ВЫКЛ",
           sleeping ? RGB(36, 76, 72) : BTN);
    button(166, 174, 150, 48, "RESET", RED);
    button(324, 174, 148, 48, "ACTIVATE", RGB(70, 142, 232));
    text(8, 235,
         resetting  ? "Драйверы в сбросе"
         : sleeping ? "Драйверы спят"
                    : "Драйверы готовы",
         MUTED, 1);
#endif
  }
}
static void draw_midi(void) {
  unsigned i;
  char s[16];
  uint32_t start = midi_now > 8000 ? midi_now - 8000 : 0;
  int x, y, w;
  const int left = 48, top = 86, height = 168;
  text(8, 34, (flags & 8) ? "MIDI подключён" : "Нет подключения", MUTED, 1);
  for (i = 0; i < 6; i++) {
    snprintf(s, sizeof(s), MUSIC_BOX_LIVE ? "V%u" : "M%u", i + 1);
    text(12 + (int)i * 78, 56, s, voice_colors[i], 1);
  }
  fill(left, top, 424, height, PANEL);
  for (i = 0; i < 5; i++) {
    time_text(s, start + i * 2000);
    text(i == 4 ? 436 : left + (int)i * 100, 70, s, MUTED, 1);
    fill(left + (int)i * 100, top, 1, height, BTN);
  }
  text(1, 70, "Ноты", MUTED, 1);
  text(1, 81, "D6", MUTED, 1);
  text(1, 112, "C5", MUTED, 1);
  text(1, 140, "C4", MUTED, 1);
  text(1, 168, "C3", MUTED, 1);
  text(1, 196, "C2", MUTED, 1);
  text(1, 224, "C1", MUTED, 1);
  text(1, 242, "E0", MUTED, 1);
  for (i = 0; i < NOTES; i++) {
    MidiNote *n = &notes[i];
    uint32_t end = n->open ? midi_now : n->end, a;
    if (!n->used || (int32_t)(end - start) < 0 ||
        (int32_t)(midi_now - n->start) < 0)
      continue;
    a = (int32_t)(n->start - start) < 0 ? start : n->start;
    if (end > start + 8000)
      end = start + 8000;
    x = left + (int)((a - start) * 422 / 8000);
    w = (int)((end - a) * 422 / 8000);
    if (w < 2)
      w = 2;
    // log-frequency axis: MIDI 15.487 = 20 Hz, MIDI 86.370 = 1200 Hz.
    y = top + (86370 - (int)n->pitch * 1000) * (height - 3) / 70883;
    if (y < top)
      y = top;
    if (y > top + height - 3)
      y = top + height - 3;
    fill(x, y, w, 3, voice_colors[n->motor]);
  }
  x = left + (int)((midi_now - start) * 422 / 8000);
  fill(x, top, 1, height, WHITE);
}
static void draw_numeric(void) {
  unsigned i;
  static const char *keys[] = {"1", "2", "3", "4", "5", "6",
                               "7", "8", "9", ".", "0", "<"};
  fill(0, 28, 480, 236, BG);
  text(8, 40, show_hz ? "Частота, Гц" : "Нота", MUTED, 1);
  button(130, 32, 48, 48, "X", BTN);
  fill(8, 97, 170, 52, PANEL);
  button(8, 204, 170, 48, "Применить", RGB(70, 142, 232));
  if (!show_hz) {
    char label[16];
    unsigned pitch=input_note%12;
    note_text(label,input_note);text(20,106,label,TEXT,2);
    text(8,165,"Диапазон E0..B7",MUTED,1);
    for(i=0;i<7;++i) {
      unsigned base=natural_pitches[i];
      int chosen=pitch==base || (base!=4 && base!=11 && pitch==base+1);
      button(190+(int)(i%3)*94,32+(int)(i/3)*56,88,50,natural_names[i],chosen?BLUE:BTN);
    }
    int sharp=pitch==1||pitch==3||pitch==6||pitch==8||pitch==10;
    if(pitch==4||pitch==11) {
      fill(284,144,88,50,RGB(40,40,40));
      text(323,161,"#",RGB(96,96,96),1);
    } else button(284,144,88,50,"#",sharp?BLUE:BTN);
    button(190,200,88,50,"-",BTN);
    snprintf(label,sizeof(label),"Окт. %d",(int)input_note/12-1);
    text(292,217,label,TEXT,1);
    button(378,200,88,50,"+",BTN);
    return;
  }
  text(14, 115, digits, TEXT, 1);
  for (i = 0; i < 12; i++)
    button(190 + (int)(i % 3) * 94, 32 + (int)(i / 3) * 56, 88, 50, keys[i],
           BTN);
}
static void scene(void) {
  unsigned i;
  static const char *tabs[] = {"Обзор", "MIDI", MUSIC_BOX_LIVE ? "Вход" : "Мелодия", "Настройки"};
  fill(0, 0, 480, 320, BG);
  fill(0, 0, 480, 28, PANEL);
  text(10, 6, "MUSIC MOTOR", TEXT, 1);
#if MUSIC_BOX_CONTROLLER
  const char *status = (connection_status & 1) ? "Готов к подкл." : "Ошибка внутр. обмена.";
  int status_right = 470;
  if (connection_status & 4) {
    status_right -= 9 * textlen("MIDI");
    text(status_right, 6, "MIDI", GREEN, 1);
    status_right -= 12;
  }
  if (connection_status & 2) {
    status_right -= 9 * textlen("USB");
    text(status_right, 6, "USB", GREEN, 1);
    status_right -= 12;
  }
  if (!(connection_status & 1) || !(connection_status & 6))
    text(status_right - 9 * textlen(status), 6, status,
         (connection_status & 1) ? MUTED : RED, 1);
#else
  const char *status = MUSIC_BOX_LIVE ? ((flags & 8) ? "USB-MIDI" : "Нет USB") :
       ((flags & 1) ? "6 МОТОРОВ" : "Нет связи");
  text(470 - 9 * textlen(status), 6, status,
       (flags & (MUSIC_BOX_LIVE ? 8 : 1)) ? GREEN : MUTED, 1);
#endif
  switch (page) {
  case 0:
    draw_overview();
    break;
  case 1:
    draw_midi();
    break;
  case 2:
    draw_song();
    break;
  case 3:
    draw_settings();
    break;
  case 4:
    draw_motor();
    break;
  }
  fill(0, 264, 480, 56, PANEL);
  for (i = 0; i < 4; i++)
    button(4 + (int)i * 92, 268, 86, 48, tabs[i],
           (page == i || (page == 4 && i == 0)) ? RGB(42, 65, 92) : PANEL);
  button(374, 268, 102, 48, "СТОП ВСЁ", RED);
  if (numeric)
    draw_numeric();
#if MUSIC_BOX_CONTROLLER
  if(save_stage) {
    static const char *stages[]={"","Подготовка памяти","Приём мелодии","Проверка и сохранение","Мелодия сохранена","Проверка моторов","Ошибка сохранения","Ошибка воспроизведения"};
    char progress[12];
    if(save_stage==5) {
      fill(8,204,464,54,PANEL);
      text(16,210,"Проверка моторов",TEXT,1);
      snprintf(progress,sizeof(progress),"%u%%",save_percent);text(416,210,progress,TEXT,1);
      fill(16,242,448,8,BTN);fill(16,242,448*save_percent/100,8,GREEN);
    } else if(save_stage==7) {
      fill(8,204,464,54,PANEL);
      text(16,210,"Не удалось запустить",RED,1);
      text(16,234,playback_error,TEXT,1);
    } else {
    fill(20,82,440,152,PANEL);
    text(36,104,stages[save_stage<=7?save_stage:6],save_stage>=6?RED:TEXT,1);
    snprintf(progress,sizeof(progress),"%u%%",save_percent);text(212,144,progress,TEXT,1);
    fill(36,188,408,16,BTN);fill(36,188,408*save_percent/100,16,save_stage>=6?RED:GREEN);
    }
  }
#endif
  if (notice_until) {
    fill(0, 28, 480, 26, RED);
    text(8, 33, notice, TEXT, 1);
  }
}
static void flush_one(void) {
  int xx, yy;
  Rect *r;
  unsigned h;
  if (!dirty_count)
    return;
  r = &dirty[0];
#if MUSIC_BOX_PIXEL_MIDI
  if(page == 1 && !numeric && p->width == 480 && p->height == 320) {
    if(!midi_image_valid){memset(midi_image,255,sizeof(midi_image));midi_image_valid=1;}
    // Vertical slices bound CPU/SPI work between touch polls. Scan left to right.
    int columns=(480*BAND)/r->h;
    if(columns>32)columns=32;
    if(columns>r->w)columns=r->w;
    clip=(Rect){r->x,r->y,(int16_t)columns,r->h};
    scene();
    memset(midi_changed,0,(columns*r->h+7)/8);
    for(yy=0;yy<r->h;++yy)for(xx=0;xx<columns;++xx) {
      int y=r->y+yy,changed=1;
      if(y>=54 && y<264) {
        unsigned index=(unsigned)(y-54)*480+r->x+xx,shift=(index&1)*4;
        unsigned code=midi_pixel_code(pixels[yy*columns+xx]);
        unsigned old=(midi_image[index/2]>>shift)&15;
        changed=code==15 || old!=code;
        midi_image[index/2]=(uint8_t)((midi_image[index/2]&~(15u<<shift))|(code<<shift));
      }
      if(changed)midi_mark(yy*columns+xx);
    }
    // Merge adjacent changed runs into rectangles without including a single unchanged pixel.
    for(xx=0;xx<columns;++xx)for(yy=0;yy<r->h;) {
      if(!midi_changed_at(yy*columns+xx)){++yy;continue;}
      int end=yy+1,right=xx+1;
      while(end<r->h && midi_changed_at(end*columns+xx))++end;
      while(right<columns) {
        int row;
        for(row=yy;row<end && midi_changed_at(row*columns+right);++row){}
        if(row!=end)break;
        ++right;
      }
      p->write_rect(r->x+xx,r->y+yy,right-xx,end-yy,pixels+yy*columns+xx,columns,0);
      for(int col=xx;col<right;++col)for(int row=yy;row<end;++row)midi_unmark(row*columns+col);
      yy=end;
    }
    r->x+=(int16_t)columns;r->w-=(int16_t)columns;
    if(!r->w){--dirty_count;memmove(dirty,dirty+1,dirty_count*sizeof(Rect));}
    return;
  }
  midi_image_valid=0;
#endif
  h = (unsigned)r->h;
  if (h > BAND)
    h = BAND;
  clip = (Rect){r->x, r->y, r->w, (int16_t)h};
  scene();
  if (p->width == 320 && p->height == 480) {
    for (yy = 0; yy < (int)h; yy++)
      for (xx = 0; xx < r->w; xx++)
        rotated[xx * h + h - 1 - yy] = pixels[yy * r->w + xx];
    p->write_rect((uint16_t)(320 - r->y - h), (uint16_t)r->x, (uint16_t)h,
                  (uint16_t)r->w, rotated, (uint16_t)h, 0);
  } else
    p->write_rect((uint16_t)r->x, (uint16_t)r->y, (uint16_t)r->w, (uint16_t)h,
                  pixels, (uint16_t)r->w, 0);
  r->y += (int16_t)h;
  r->h -= (int16_t)h;
  if (!r->h) {
    dirty_count--;
    memmove(dirty, dirty + 1, dirty_count * sizeof(Rect));
  }
}
static void save_settings(void) {
  uint8_t b[8] = {'M', 'B', 1, 0, 0, 0, 0, 0};
  uint16_t c;
  b[3] = show_hz;
  b[4] = width_index;
  c = crc16(b, 6);
  b[6] = (uint8_t)c;
  b[7] = (uint8_t)(c >> 8);
  if (p->flash_erase && p->flash_write && p->flash_erase(SETTINGS_ADDR) &&
      p->flash_write(SETTINGS_ADDR, b, 8))
    settings_dirty = 0;
  else {
    settings_dirty = 0;
    notification("Настройки не сохранены");
  }
}
static void setting_changed(void) {
  settings_dirty = 1;
  settings_at = now;
  invalidate(0, 28, 480, 236);
}
#if MUSIC_BOX_CONTROLLER
static void select_saved_song(int direction) {
  for (unsigned step = 1; step <= SAVED_SONG_COUNT; ++step) {
    unsigned candidate = (saved_selected + SAVED_SONG_COUNT + direction * (int)step) % SAVED_SONG_COUNT;
    if (saved_present[candidate]) { saved_selected = candidate; break; }
  }
  full();
}
#endif
static void touch_numeric(int x, int y) {
  unsigned col, row, k;
  size_t n;
  if (inside(x, y, 130, 32, 48, 48)) {
    numeric = 0;
    invalidate(0, 28, 480, 236);
    return;
  }
  if (!show_hz) {
    if (inside(x,y,8,204,170,48)) {
      if(input_note<16||input_note>107){notification("Диапазон нот: E0..B7");return;}
      send_action(3,selected,note_frequency(input_note));
      numeric=0;
    } else {
      if(!inside(x,y,190,32,282,224))return;
      col=(unsigned)(x-190)/94;row=(unsigned)(y-32)/56;
      if((x-190)%94>=88||(y-32)%56>=50)return;
      k=row*3+col;
      unsigned pitch=input_note%12,octave=input_note/12;
      int sharp=pitch==1||pitch==3||pitch==6||pitch==8||pitch==10;
      if(k<7) {
        unsigned base=natural_pitches[k];
        input_note=(uint8_t)(octave*12+base+(sharp&&base!=4&&base!=11));
      } else if(k==7) {
        if(pitch==4||pitch==11)return;
        if(sharp)--input_note;
        else ++input_note;
      } else if(k==9&&octave>1)input_note-=12;
      else if(k==11&&octave<8)input_note+=12;
    }
    invalidate(0,28,480,236);
    return;
  }
  if (inside(x, y, 8, 204, 170, 48)) {
    uint32_t hz = 0, frac = 0, mult = 100;
    const char *s = digits;
    while (*s >= '0' && *s <= '9')
      hz = hz * 10 + (unsigned)(*s++ - '0');
    if (*s == '.') {
      s++;
      while (*s >= '0' && *s <= '9' && mult) {
        frac += (unsigned)(*s++ - '0') * mult;
        mult /= 10;
      }
    }
    if (hz > 4000) {
      notification("Частота: 20..4000 Гц");
      return;
    }
    hz = hz * 1000 + frac;
    if (*s || hz < 20000 || hz > 4000000) {
      notification("Частота: 20..4000 Гц");
      return;
    }
    send_action(3, selected, hz);
    numeric = 0;
    invalidate(0, 28, 480, 236);
    return;
  }
  if (!inside(x, y, 190, 32, 282, 224)) {
    return;
  }
  col = (unsigned)(x - 190) / 94;
  row = (unsigned)(y - 32) / 56;
  if ((x - 190) % 94 >= 88 || (y - 32) % 56 >= 50)
    return;
  k = row * 3 + col;
  n = strlen(digits);
  if (k == 11) {
    if (n)
      digits[n - 1] = 0;
  } else {
    char c = k == 9 ? '.' : k == 10 ? '0' : (char)('1' + k);
    if (fresh_digits) {
      digits[0] = 0;
      n = 0;
    }
    if (n < 7 && (c != '.' || !strchr(digits, '.'))) {
      digits[n] = c;
      digits[n + 1] = 0;
    }
  }
  fresh_digits = 0;
  invalidate(8, 97, 170, 52);
}
static void tap(int x, int y) {
  unsigned i;
#if !MUSIC_BOX_LIVE
  Motor *m = &motors[selected];
#endif
  if (inside(x, y, 374, 268, 102, 48)) {
    send_action(0, 0, 0);
    if (numeric)
      invalidate(0, 28, 480, 236);
    numeric = 0;
    return;
  }
#if MUSIC_BOX_CONTROLLER
  if(save_stage) {
    if(save_stage==4||save_stage>=6){save_stage=0;full();}
    return;
  }
#endif
  if (y >= 264) {
    if (x >= 368)
      return;
    page = (uint8_t)(x / 92);
    if (page > 3)
      page = 3;
    numeric = 0;
    invalidate(0, 28, 480, 292);
    return;
  }
  if (numeric) {
    touch_numeric(x, y);
    return;
  }
  if (page == 0) {
    if (inside(x, y, 8, 34, 464, 164)) {
      selected = (uint8_t)((x - 8) / 78);
      if (selected < 6) {
        page = 4;
        invalidate(0, 28, 480, 236);
      }
    }
#if MUSIC_BOX_CONTROLLER
    else if((pc_song_visible()||saved_playing)&&inside(x,y,8,206,340,50))seek(x);
    else if(inside(x,y,356,206,116,50)) {
      if(pc_song_visible())send_action(1,0,0);
      else if(saved_present[saved_selected])send_action(20,saved_selected,0);
      else notification("Выберите мелодию во вкладке Мелодия");
    }
#else
    else if (inside(x, y, 8, 206, 340, 50)) seek(x);
    else if (inside(x, y, 356, 206, 116, 50)) send_action(1, 0, 0);
#endif
  } else if (page == 4) {
    if (inside(x, y, 8, 34, 56, 48))
      selected = (selected + 5) % 6;
    else if (inside(x, y, 416, 34, 56, 48))
      selected = (selected + 1) % 6;
#if !MUSIC_BOX_LIVE
    else if (inside(x, y, 232, 90, 184, 48)) {
      if(show_hz)snprintf(digits, sizeof(digits), "%lu", (unsigned long)(m->mhz / 1000));
      else input_note=(uint8_t)motor_note(m);
      numeric = 1;
      fresh_digits = 1;
    } else if (inside(x, y, 176, 90, 50, 48))
      send_action(3, selected, show_hz ? (m->mhz >= 30000 ? m->mhz - 10000 : 20000) :
                  note_frequency(motor_note(m) > 16 ? motor_note(m) - 1 : 16));
    else if (inside(x, y, 422, 90, 50, 48))
      send_action(3, selected, show_hz ? (m->mhz <= 3990000 ? m->mhz + 10000 : 4000000) :
                  note_frequency(motor_note(m) < 107 ? motor_note(m) + 1 : 107));
    else if (inside(x, y, 8, 202, 160, 48))
      send_action(4, selected, !(m->flags & 1));
    else if (inside(x, y, 176, 146, 296, 48))
      send_action(6, selected, !(m->flags & 4));
    else if (inside(x, y, 176, 202, 296, 48))
      send_action(5, selected, !(m->flags & 2));
#endif
    invalidate(0, 28, 480, 236);
  } else if (page == 2) {
#if MUSIC_BOX_CONTROLLER
    if(!saved_playing&&inside(x,y,8,70,56,60))select_saved_song(-1);
    else if(!saved_playing&&inside(x,y,416,70,56,60))select_saved_song(1);
#else
#if MUSIC_BOX_LIVE
    if (inside(x, y, 16, 208, 448, 48)) send_action(1, 0, 0);
#else
    if (inside(x, y, 280, 36, 192, 48))
      send_action(1, 0, 0);
    else if (inside(x, y, 280, 92, 92, 48))
      send_action(2, 0, position > 10000 ? position - 10000 : 0);
    else if (inside(x, y, 380, 92, 92, 48))
      send_action(2, 0,
                  position + 10000 < duration ? position + 10000
                  : duration                  ? duration - 1
                                              : 0);
    else if (inside(x, y, 280, 148, 192, 48))
      send_action(2, 0, 0);
#endif
#endif
  } else if (page == 3) {
    if (inside(x, y, 8, 34, 228, 48)) {
      settings_page = 0;
      invalidate(0, 28, 480, 236);
    } else if (inside(x, y, 244, 34, 228, 48)) {
      settings_page = 1;
      invalidate(0, 28, 480, 236);
    } else if (!settings_page) {
      if (inside(x, y, 150, 88, 155, 48)) {
        show_hz = 0;
        setting_changed();
      } else if (inside(x, y, 313, 88, 159, 48)) {
        show_hz = 1;
        setting_changed();
      } else
        for (i = 0; i < 3; i++)
          if (inside(x, y, 8 + (int)i * 156, 174, 152, 48)) {
            width_index = (uint8_t)i;
            setting_changed();
          }
    } else {
#if !MUSIC_BOX_LIVE
      static const uint8_t raw[] = {0, 1, 2, 3, 7};
      for (i = 0; i < 5; i++)
        if (inside(x, y, 8 + (int)i * 94, 108, 88, 48))
          send_action(7, 0, raw[i]);
      if (inside(x, y, 8, 174, 150, 48))
        send_action(8, 0, !sleeping);
      else if (inside(x, y, 166, 174, 150, 48))
        send_action(9, 0, 1);
      else if (inside(x, y, 324, 174, 148, 48))
        send_action(10, 0, 0);
#endif
    }
  }
}
static void update_overview(const uint8_t *previous_heights) {
  for (unsigned i = 0; i < 6; ++i) {
    Motor old = overview[i];
    int active = !!(motors[i].flags & 2) || ((flags & 64) && (motors[i].flags & 8));
    int clear = !have_state || !(flags & 1) || sleeping || resetting ||
                !(mask & (1u << i)) || (flags & 4);
    if (!(flags & (32 | 64))) { /* File/queued playback; live and manual stay immediate. */
      overview[i] = motors[i];
      overview[i].flags &= 7;
      if ((flags & 64) && (motors[i].flags & 8)) overview[i].flags |= 2;
      if (clear) overview[i].flags = 0;
      overview_was_active[i] = 0;
    } else if (clear) {
      overview[i] = motors[i];
      overview[i].flags = 0;
      overview_was_active[i] = 0;
    } else if (active) {
      if (!overview_was_active[i] || overview[i].note != motors[i].note)
        overview_started[i] = now;
      overview[i] = motors[i];
      overview[i].flags = (overview[i].flags & 7) | 2; /* Actual note or confirmed queue hold. */
      overview_was_active[i] = 1;
    } else {
      if (overview_was_active[i]) {
        overview_off[i] = now;
        if ((int32_t)(now - overview_started[i]) < (int32_t)OVERVIEW_MIN_MS)
          overview_off[i] = overview_started[i] + OVERVIEW_MIN_MS;
      }
      overview_was_active[i] = 0;
      if ((int32_t)(now - overview_started[i]) >= (int32_t)OVERVIEW_MIN_MS &&
          (int32_t)(now - overview_off[i]) >= (int32_t)OVERVIEW_GAP_MS)
        overview[i].flags &= (uint8_t)~2u;
    }
    if (page == 0) {
      int a = previous_heights ? previous_heights[i] : bar_height(&old);
      int v = bar_height(&overview[i]);
      int w = 75 * widths[width_index] / 100, x = 8 + (int)i * 78;
      if (a != v)
        invalidate(x + (75 - w) / 2, 128 - (a > v ? a : v), w,
                   a > v ? a - v : v - a);
      if ((show_hz ? old.mhz != overview[i].mhz : old.note != overview[i].note) ||
          ((old.flags ^ overview[i].flags) & 2))
        invalidate(x, 130, 75, 18);
      if ((old.flags ^ overview[i].flags) & 2)
        invalidate(x, 150, 75, 48);
    }
  }
}

static void process_packet(void) {
  uint8_t *b = packet + 5;
  unsigned len = packet[2], i;
  uint8_t old_flags = flags;
  if (packet[4] == 0x40 && len == 50 && b[0] == 1) {
    uint32_t oldpos = position, oldduration = duration;
    uint8_t oldsleep = sleeping, oldreset = resetting, oldmicro = micro;
    uint8_t previous_heights[6];
    Motor old[6];
    memcpy(old, motors, sizeof(old));
    /* Measure the previous bars before flags/duration/mask change their scale. */
    for (i = 0; i < 6; ++i) previous_heights[i] = (uint8_t)bar_height(&overview[i]);
    flags = b[1] & 127;
    sleeping = b[2];
    resetting = b[3];
    micro = b[4];
    mask = b[5] & 63;
    position = get32(b + 6);
    duration = get32(b + 10);
#if MUSIC_BOX_CONTROLLER
    if(saved_playing){duration=saved_durations[saved_selected];position+=saved_offset;}
    if(save_stage==7 && (flags&16) && duration) {save_stage=0;full();}
#endif
    if (position > duration)
      position = duration;
    for (i = 0; i < 6; i++) {
      motors[i].flags = b[14 + i * 6] & 15;
      motors[i].note = b[15 + i * 6];
      motors[i].mhz = get32(b + 16 + i * 6);
      if (motors[i].mhz > 4000000)
        motors[i].mhz = 4000000;
      if (!(flags & 1) || !(mask & (1 << i)))
        motors[i].flags = 0;
    }
    last_state = now;
    have_state = 1;
    if (!(old_flags & (2|64)) && (flags & (2|64)) && !(flags & 4)) midi_stop_latched=0;
    if (!midi_timeline_running()) midi_close_notes();
    update_overview(previous_heights);
    if (old_flags != flags)
      invalidate(0, 0, 480, 28);
    if (page == 0) {
      if (oldduration != duration || old_flags != flags)
        invalidate(8, 206, 464, 50);
      else if (oldpos != position) {
        if (oldpos / 1000 != position / 1000) invalidate(16, 229, 220, 16);
        if (duration && (uint64_t)oldpos * 324 / duration != (uint64_t)position * 324 / duration)
          invalidate(16, 249, 324, 3);
      }
    } else if (page == 4 && !numeric) {
      if (memcmp(&old[selected], &motors[selected], sizeof(Motor)))
        invalidate(8, 90, 464, 160);
    } else if (page == 2) {
      if (oldduration != duration || old_flags != flags)
        invalidate(0, 28, 480, 236);
      else if (oldpos / 1000 != position / 1000) invalidate(20, 180, 250, 32);
    } else if (page == 3 && settings_page &&
               (oldsleep != sleeping || oldreset != resetting ||
                oldmicro != micro))
      invalidate(0, 84, 480, 180);
    else if (page == 1 && old_flags != flags)
      invalidate(0, 28, 480, 26);
  } else if (packet[4] == 0x44 && len == 2) {
#if MUSIC_BOX_CONTROLLER
    if (pc_range[0] != b[0] || pc_range[1] != b[1]) {
      memcpy(pc_range,b,2);
      if (page == 0) full();
    }
#endif
  } else if (packet[4] == 0x41 && len <= 48) {
    if (strlen(title) != len || memcmp(title, b, len)) {
      memcpy(title, b, len);
      title[len] = 0;
      if (page == 0)
        invalidate(8, 206, 340, 50);
      if (page == 2)
        invalidate(8, 36, 264, 218);
    }
  } else if (packet[4] == 0x42 && len == 7 && b[4] < 6 && b[5] < 128) {
    if(b[6]&&midi_stop_latched) {
      if(flags&(2|64))return; /* Ignore already queued Note On after STOP ALL. */
      midi_stop_latched=0; /* A newly started motor after the stopped state. */
    }
    midi_clock_update(get32(b),midi_timeline_running()||!!b[6]);
    uint32_t at = midi_now;
    midi_dirty = 1;
    for (i = 0; i < NOTES; i++)
      if (notes[i].used && notes[i].open && notes[i].motor == b[4] &&
          (b[6] || notes[i].pitch == b[5])) {
        notes[i].end = at;
        notes[i].open = 0;
      }
    if (b[6]) {
      MidiNote *n = &notes[note_next++ % NOTES];
      *n = (MidiNote){at, at, b[4], b[5], 1, 1};
    }
  } else if (packet[4] == 0x43 && len == 4) {
    midi_clock_update(get32(b),midi_timeline_running());
  } else if (packet[4] == 0x51 && len == 1 && pending &&
             packet[3] == action_packet[3]) {
    pending = 0;
    if (b[0]) {
      static const char *reasons[] = {"", "Контроллер отклонил команду",
        "Неверный параметр команды", "Драйверы в сбросе", "Драйверы спят",
        "Мотор отключён в настройках", "Управление с компьютера",
        "Идёт поток MIDI", "Нет права управления", "Сначала остановите моторы",
        "Запустите мелодию на ПК", "Драйверы ещё не готовы", "Нет связи с моторами"};
      notification(b[0] < sizeof(reasons)/sizeof(reasons[0]) ? reasons[b[0]] : "Неизвестная ошибка контроллера");
    }
  }
}
static void consume(uint8_t b) {
  if (packet_n == 0) {
    if (b == 0xa5)
      packet[packet_n++] = b;
    return;
  }
  if (packet_n == 1) {
    if (b == 0x5a)
      packet[packet_n++] = b;
    else
      packet_n = (b == 0xa5) ? 1 : 0;
    return;
  }
  packet[packet_n++] = b;
  if (packet_n == 3) {
    if (b > 240) {
      packet_n = 0;
      return;
    }
    packet_total = b + 7;
  }
  if (packet_n == packet_total) {
    uint16_t got = (uint16_t)(packet[packet_n - 2] | packet[packet_n - 1] << 8);
    if (crc16(packet + 2, packet_n - 4) == got)
      process_packet();
    packet_n = 0;
  }
}
void display_init(const DisplayPlatform *platform) {
#if MUSIC_BOX_CONTROLLER
  initial_render_done=0;
#endif
  uint8_t b[8];
  unsigned i;
  p = platform;
  now = platform->now_ms ? platform->now_ms() : 0;
  flags = sleeping = resetting = micro = page = selected = settings_page =
      show_hz = 0;
  width_index = 2;
  position = duration = 0;
  have_state = 0;
  numeric = touch_down = drag_seek = 0;
  pending = 0;
  seq = 0;
  rx_in = rx_out = 0;
  rx_bad = 0;
  packet_n = 0;
  dirty_count = 0;
#if MUSIC_BOX_PIXEL_MIDI
  midi_image_valid = 0;
#endif
  note_next = 0;
  midi_now = 0;
  midi_source_valid=midi_stop_latched=midi_input_running=0;
  midi_tail_ms=0;
  midi_dirty = 0;
  notice_until = 0;
  settings_dirty = 0;
  last_paint = last_midi_draw = now;
  memset(notes, 0, sizeof(notes));
  memset(overview, 0, sizeof(overview));
  memset(overview_was_active, 0, sizeof(overview_was_active));
  memset(overview_started, 0, sizeof(overview_started));
  memset(overview_off, 0, sizeof(overview_off));
  strcpy(title, MUSIC_BOX_LIVE ? "Живой USB-MIDI" : "Нет композиции");
  for (i = 0; i < 6; i++) {
    motors[i].flags = 0;
    motors[i].note = 69;
    motors[i].mhz = 440000;
  }
  if (p->flash_read && p->flash_read(SETTINGS_ADDR, b, 8) && b[0] == 'M' &&
      b[1] == 'B' && b[2] == 1 && b[3] < 2 && b[4] < 3 &&
      crc16(b, 6) == (uint16_t)(b[6] | b[7] << 8)) {
    show_hz = b[3];
    width_index = b[4];
  }
  if (p->version == DISPLAY_API_VERSION && p->write_rect &&
      ((p->width == 320 && p->height == 480) ||
       (p->width == 480 && p->height == 320)))
    full();
}
void display_event(const DisplayEvent *e) {
  int x = e->x, y = e->y;
  if (e->type == DISPLAY_TOUCH_CANCEL) {
    touch_down = drag_seek = 0;
    return; // Invalid input is not a release: never execute a tap or seek.
  }
  if (e->type == DISPLAY_RX_BYTE) {
    uint16_t next = (uint16_t)(rx_in + 1);
    if ((uint16_t)(next - rx_out) > RX_SIZE)
      rx_bad = 1;
    else {
      rx[rx_in & (RX_SIZE - 1)] = e->byte;
      rx_in = next;
    }
    return;
  }
  if (e->type == DISPLAY_RX_ERROR) {
    rx_bad = 1;
    return;
  }
  if (e->type != DISPLAY_TOUCH || !p)
    return;
  now = e->now_ms;
  if (p->width == 320) {
    x = e->y;
    y = 319 - e->x;
  }
  if (x < 0 || x >= 480 || y < 0 || y >= 320)
    return;
  if (e->down && !touch_down) {
    touch_down = 1;
    down_x = (int16_t)x;
    down_y = (int16_t)y;
    drag_seek =
        (uint8_t)(page == 0 && !numeric && inside(x, y, 8, 206, 340, 50));
    if (inside(x, y, 374, 268, 102, 48)) {
      tap(x, y);
      down_y = -100;
    }
  } else if (!e->down && touch_down) {
    touch_down = 0;
    if (drag_seek) {
      seek(x);
      drag_seek = 0;
    } else if (x - down_x < 16 && down_x - x < 16 && y - down_y < 16 &&
               down_y - y < 16)
      tap(x, y);
  }
}
void display_step(uint32_t ms) {
  unsigned budget = 512;
  now = ms;
  if (rx_bad) {
    rx_out = rx_in;
    packet_n = 0;
    rx_bad = 0;
    notification("Ошибка UART");
  }
  if (packet_n && now - rx_time > 100)
    packet_n = 0;
  while (rx_out != rx_in && budget--) {
    uint8_t b = rx[rx_out & (RX_SIZE - 1)];
    rx_out++;
    rx_time = now;
    consume(b);
  }
  if (have_state && (int32_t)(now - last_state) > 1500) {
    unsigned i;
    flags = 0;
    have_state = 0;
    for (i = 0; i < 6; i++)
      motors[i].flags = 0;
    pending = 0;
    midi_close_notes();
    full();
  }
  update_overview(NULL);
  if (pending && now - pending_at >= 300) {
    if (pending_tries >= 3) {
      pending = 0;
      notification("Нет ответа на команду");
    } else {
      pending_tries++;
      pending_at = now;
      if (p->send)
        p->send(action_packet, 13);
    }
  }
  if (notice_until && (int32_t)(now - notice_until) >= 0) {
    notice_until = 0;
    invalidate(0, 28, 480, 26);
  }
  if (page == 1 && midi_dirty && now - last_midi_draw >= MUSIC_BOX_MIDI_INTERVAL_MS &&
      (!(MUSIC_BOX_LIVE || MUSIC_BOX_CONTROLLER) || !dirty_count)) {
    midi_dirty = 0;
    last_midi_draw = now;
    invalidate(0, 54, 480, 210);
  }
  if (settings_dirty && !touch_down && !(flags & 2) && !dirty_count &&
      (!(MUSIC_BOX_LIVE || MUSIC_BOX_CONTROLLER) || !pending) &&
      now - settings_at >= 5000)
    save_settings();
  if (p && p->write_rect) {
    flush_one();
#if MUSIC_BOX_CONTROLLER
    if(!dirty_count)initial_render_done=1;
#endif
  }
  last_paint = now;
}

#if MUSIC_BOX_LIVE
#include "music_box_live.h"
void music_box_live_state(uint32_t at, uint8_t connected, uint8_t enabled,
                          const uint8_t pitches[6], const uint32_t frequencies[6]) {
  unsigned i;
  uint8_t *b = packet + 5;
  now = p && p->now_ms ? p->now_ms() : at;
  for (i = 0; i < 6; ++i) {
    const uint8_t old = (motors[i].flags & 2) ? motors[i].note : 255;
    if (old == pitches[i]) continue;
    packet[2] = 7; packet[4] = 0x42;
    put32(b, at); b[4] = (uint8_t)i;
    if (old != 255) { b[5] = old; b[6] = 0; process_packet(); }
    if (pitches[i] < 128) { b[5] = pitches[i]; b[6] = 100; process_packet(); }
  }
  packet[2] = 50; packet[4] = 0x40;
  memset(b, 0, 50); b[0] = 1;
  b[1] = (uint8_t)(1 | (enabled ? 2 : 4) | (connected ? 8 : 0)); b[5] = 63;
  for (i = 0; i < 6; ++i) {
    b[14 + 6 * i] = pitches[i] < 128 ? 3 : 0;
    b[15 + 6 * i] = pitches[i]; put32(b + 16 + 6 * i, frequencies[i]);
  }
  process_packet();
  packet[2] = 4; packet[4] = 0x43; put32(b, at); process_packet();
}
void music_box_live_ack(uint8_t sequence, uint8_t result) {
  packet[2] = 1; packet[3] = sequence; packet[4] = 0x51; packet[5] = result;
  process_packet();
}
void music_box_live_history_reset(uint32_t at) {
  memset(notes, 0, sizeof(notes)); note_next = 0; midi_now = at; midi_dirty = 1;
  midi_source_valid=0;midi_tail_ms=0;
  for (unsigned i = 0; i < 6; ++i) { motors[i].flags = 0; motors[i].note = 255; }
  full();
}
#endif

#if MUSIC_BOX_CONTROLLER
#include "music_box_control.h"
void music_box_control_frame(const uint8_t *frame, unsigned length) {
  if (!frame || length < 7 || length > sizeof(packet) || frame[0] != 0xa5 ||
      frame[1] != 0x5a || frame[2] + 7u != length ||
      crc16(frame + 2, length - 4) != (uint16_t)(frame[length - 2] | frame[length - 1] << 8)) return;
  now = p && p->now_ms ? p->now_ms() : now;
  memcpy(packet, frame, length);
  process_packet();
}
void music_box_control_source(unsigned source) {
  if (controller_source == source) return;
  controller_source = source;
  // Never carry a pending command, notes or playback position to another controller.
  pending = packet_n = 0; have_state = flags = 0;
  memset(overview, 0, sizeof(overview));
  memset(overview_was_active, 0, sizeof(overview_was_active));
  memset(notes, 0, sizeof(notes)); note_next = 0; midi_now = 0;
  midi_source_valid=0;midi_tail_ms=0;
  for (unsigned i = 0; i < 6; ++i) motors[i].flags = 0;
  position = duration = 0;
    strcpy(title, source == 1 ? "Контроллер UART" : source == 2 ? "USB-симулятор" : source == 3 ? "USB MIDI: выход ESP" : "Нет контроллера");
  full();
}
void music_box_control_connections(unsigned status) {
  if (connection_status == status) return;
  connection_status = status;
  full();
}
void music_box_midi_input(unsigned running) {
  if(running&&!midi_input_running)midi_stop_latched=0;
  midi_input_running=!!running;
  if(!midi_timeline_running())midi_close_notes();
}
void music_box_saved_song(unsigned slot,const char *name,unsigned present,uint32_t duration_ms) {
  if(slot>=SAVED_SONG_COUNT)return;
  strncpy(saved_titles[slot],name,31);saved_titles[slot][31]=0;saved_present[slot]=!!present;saved_durations[slot]=duration_ms;
  if (!saved_playing && !saved_present[saved_selected]) select_saved_song(1);
  full();
}
void music_box_save_progress(unsigned stage,unsigned percent) {
  if(percent>100)percent=100;
  if(save_stage==stage&&save_percent==percent)return;
  unsigned old_stage=save_stage;
  save_stage=stage;save_percent=percent;
  if (old_stage != stage && (old_stage == 5 || stage == 5)) invalidate(0,34,480,170);
  if((old_stage==0||old_stage==5)&&(stage==0||stage==5))invalidate(8,204,464,54);
  else full();
}
unsigned music_box_screen_ready(void) {return initial_render_done;}
void music_box_playback_error(const char *reason) {
  strncpy(playback_error,reason,sizeof(playback_error)-1);playback_error[sizeof(playback_error)-1]=0;
  save_stage=7;full();
}
void music_box_saved_offset(uint32_t offset){saved_offset=offset;position=offset;full();}
void music_box_saved_range(uint8_t low,uint8_t high){saved_range[0]=low;saved_range[1]=high;full();}
void music_box_saved_playing(unsigned playing) {saved_playing=!!playing;full();}
#endif
