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
#define RED RGB(184, 62, 81)
#define GREEN RGB(80, 210, 173)
#define WHITE 0xffff
#define SETTINGS_ADDR 0x1ff000u
#define RX_SIZE 512u
#define NOTES 96u
#define DIRTY 32u
#define BAND 2

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
static uint8_t flags, sleeping, resetting, micro,
    mask = 63, page, selected, settings_page, show_hz, width_index = 2;
static const uint8_t widths[3] = {25, 50, 100};
static uint32_t position, duration, now, last_state, last_paint, midi_now,
    last_midi_draw;
static uint8_t midi_dirty;
static uint8_t have_state;
static char title[49] = "Нет композиции", notice[48];
static uint32_t notice_until;
static MidiNote notes[NOTES];
static unsigned note_next;
static const uint16_t voice_colors[6] = {
    RGB(106, 201, 255), RGB(189, 157, 255), RGB(118, 223, 187),
    RGB(255, 206, 115), RGB(249, 148, 194), RGB(143, 169, 255)};
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
static uint8_t settings_dirty;
static uint32_t settings_at;
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
    g = glyph(utf8(&s));
    for (yy = 0; yy < 16; yy++)
      for (xx = 0; xx < 9; xx++)
        if (font_rows[g][yy] & (1u << xx))
          fill(x + xx * scale, y + yy * scale, scale, scale, color);
    x += 9 * scale;
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
static int bar_height(const Motor *m) {
  uint32_t f = m->mhz;
  unsigned octave = 0, frac;
  if (!(m->flags & 2) || f < 20000)
    return 0;
  if (f >= 4000000)
    return 92;
  f = f * 256 / 20000;
  while (f >= 512) {
    f >>= 1;
    octave++;
  }
  frac = (unsigned)f - 256;
  return (int)((octave * 256 + frac) * 92 / 1957);
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
  for (k = 0; k < 4; k++)
    fill(x + 4 + (k % 2) * (size - 10), y + 4 + (k / 2) * (size - 10), 3, 3,
         MUTED);
  for (dy = -r; dy <= r; dy++)
    for (dx = -r; dx <= r; dx++) {
      int d = dx * dx + dy * dy;
      if ((d <= r * r && d >= (r - 1) * (r - 1)) ||
          (d <= (r - 4) * (r - 4) && d >= (r - 5) * (r - 5)))
        fill(x + center + dx, y + center + dy, 1, 1, c);
      if (d <= 6)
        fill(x + center + dx, y + center + dy, 1, 1, TEXT);
    }
}
static void notification(const char *s) {
  strncpy(notice, s, sizeof(notice) - 1);
  notice[sizeof(notice) - 1] = 0;
  notice_until = now + 2000;
  invalidate(0, 28, 480, 26);
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
  char s[32], a[16], b[16];
  for (i = 0; i < 6; i++) {
    int x = 8 + (int)i * 78, h = bar_height(&motors[i]),
        w = 75 * widths[width_index] / 100;
    fill(x + (75 - w) / 2, 128 - h, w, h, BLUE);
    if (show_hz)
      snprintf(s, sizeof(s), "%lu Гц",
               (unsigned long)((motors[i].flags & 2)
                                   ? (motors[i].mhz + 500) / 1000
                                   : 0));
    else
      note_text(s, (motors[i].flags & 2) ? motors[i].note : 255);
    text(x + (75 - textlen(s) * 9) / 2, 130, s, TEXT, 1);
    fill(x, 150, 75, 48, PANEL);
    fill(x, 150, 75, 1, MUTED);
    fill(x, 197, 75, 1, MUTED);
    fill(x, 150, 1, 48, MUTED);
    fill(x + 74, 150, 1, 48, MUTED);
    motor_icon(x + 6, 156, 36, (motors[i].flags & 2) ? BLUE : MUTED);
    snprintf(s, sizeof(s), "M%u", i + 1);
    text(x + 47, 166, s, BLUE, 1);
  }
  fill(8, 206, 340, 50, PANEL);
  text(16, 211, title, TEXT, 1);
  time_text(a, position);
  time_text(b, duration);
  snprintf(s, sizeof(s), "%s / %s", a, b);
  text(16, 229, s, MUTED, 1);
  fill(16, 249, 324, 3, BTN);
  if (duration)
    fill(16, 249, (int)((uint64_t)324 * position / duration), 3, BLUE);
  button(356, 206, 116, 50, (flags & 2) ? "Пауза" : "Играть",
         RGB(70, 142, 232));
}
static void draw_motor(void) {
  char s[32];
  Motor *m = &motors[selected];
  button(8, 34, 56, 48, "<", BTN);
  snprintf(s, sizeof(s), "МОТОР %u / 6", selected + 1);
  text(170, 50, s, BLUE, 1);
  button(416, 34, 56, 48, ">", BTN);
  fill(8, 90, 160, 104, PANEL);
  motor_icon(18, 121, 40, BLUE);
  note_text(s, m->note);
  text(69, 112, s, TEXT, 2);
  text(17, 169,
       (m->flags & 2)   ? "STEP работает"
       : (m->flags & 1) ? "Удержание"
                        : "Отключён",
       MUTED, 1);
  button(8, 202, 160, 48, (m->flags & 1) ? "EN ВКЛ" : "EN ВЫКЛ",
         (m->flags & 1) ? RGB(36, 76, 72) : BTN);
  button(176, 90, 50, 48, "-", BTN);
  snprintf(s, sizeof(s), "%lu.%02lu Гц", (unsigned long)(m->mhz / 1000),
           (unsigned long)((m->mhz % 1000) / 10));
  button(232, 90, 184, 48, s, BTN);
  button(422, 90, 50, 48, "+", BTN);
  button(176, 146, 296, 48,
         (m->flags & 4) ? "Направление DIR 1" : "Направление DIR 0", BTN);
  button(176, 202, 296, 48, (m->flags & 2) ? "Остановить" : "Пуск мотора",
         (m->flags & 2) ? RED : RGB(70, 142, 232));
}
static void draw_song(void) {
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
}
static void draw_settings(void) {
  unsigned i;
  char s[16];
  button(8, 34, 228, 48, "Экран", settings_page == 0 ? RGB(36, 76, 72) : BTN);
  button(244, 34, 228, 48, "Драйверы", settings_page ? RGB(36, 76, 72) : BTN);
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
  }
}
static void draw_midi(void) {
  unsigned i;
  char s[16];
  uint32_t start = midi_now > 8000 ? midi_now - 8000 : 0;
  int x, y, w;
  const int left = 32, top = 86, height = 168;
  text(8, 34, (flags & 8) ? "MIDI подключён" : "Нет подключения", MUTED, 1);
  for (i = 0; i < 6; i++) {
    snprintf(s, sizeof(s), "M%u", i + 1);
    text(12 + (int)i * 78, 56, s, voice_colors[i], 1);
  }
  fill(left, top, 440, height, PANEL);
  for (i = 0; i < 5; i++) {
    time_text(s, start + i * 2000);
    text(i == 4 ? 436 : left + (int)i * 105, 70, s, MUTED, 1);
    fill(left + (int)i * 105, top, 1, height, BTN);
  }
  text(1, 95, "C6", MUTED, 1);
  text(1, 143, "C5", MUTED, 1);
  text(1, 191, "C4", MUTED, 1);
  text(1, 239, "C3", MUTED, 1);
  for (i = 0; i < NOTES; i++) {
    MidiNote *n = &notes[i];
    uint32_t end = n->open ? midi_now : n->end, a;
    if (!n->used || (int32_t)(end - start) < 0 ||
        (int32_t)(midi_now - n->start) < 0)
      continue;
    a = (int32_t)(n->start - start) < 0 ? start : n->start;
    if (end > start + 8000)
      end = start + 8000;
    x = left + (int)((a - start) * 438 / 8000);
    w = (int)((end - a) * 438 / 8000);
    if (w < 2)
      w = 2;
    y = top + (84 - (int)n->pitch) * 4;
    if (y < top)
      y = top;
    if (y > top + height - 3)
      y = top + height - 3;
    fill(x, y, w, 3, voice_colors[n->motor]);
  }
  x = left + (int)((midi_now - start) * 438 / 8000);
  fill(x, top, 1, height, WHITE);
}
static void draw_numeric(void) {
  unsigned i;
  static const char *keys[] = {"1", "2", "3", "4", "5", "6",
                               "7", "8", "9", ".", "0", "<"};
  fill(0, 28, 480, 236, BG);
  text(8, 40, "Частота, Гц", MUTED, 1);
  button(130, 32, 48, 48, "X", BTN);
  fill(8, 97, 170, 52, PANEL);
  text(14, 115, digits, TEXT, 1);
  button(8, 204, 170, 48, "Применить", RGB(70, 142, 232));
  for (i = 0; i < 12; i++)
    button(190 + (int)(i % 3) * 94, 32 + (int)(i / 3) * 56, 88, 50, keys[i],
           BTN);
}
static void scene(void) {
  unsigned i;
  static const char *tabs[] = {"Обзор", "MIDI", "Мелодия", "Настройки"};
  fill(0, 0, 480, 320, BG);
  fill(0, 0, 480, 28, PANEL);
  text(10, 6, "MUSIC MOTOR", TEXT, 1);
  text(345, 6, (flags & 1) ? "6 МОТОРОВ" : "Нет связи",
       (flags & 1) ? GREEN : MUTED, 1);
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
static void touch_numeric(int x, int y) {
  unsigned col, row, k;
  size_t n;
  if (inside(x, y, 130, 32, 48, 48)) {
    numeric = 0;
    invalidate(0, 28, 480, 236);
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
  Motor *m = &motors[selected];
  if (inside(x, y, 374, 268, 102, 48)) {
    send_action(0, 0, 0);
    if (numeric)
      invalidate(0, 28, 480, 236);
    numeric = 0;
    return;
  }
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
    } else if (inside(x, y, 8, 206, 340, 50))
      seek(x);
    else if (inside(x, y, 356, 206, 116, 50))
      send_action(1, 0, 0);
  } else if (page == 4) {
    if (inside(x, y, 8, 34, 56, 48))
      selected = (selected + 5) % 6;
    else if (inside(x, y, 416, 34, 56, 48))
      selected = (selected + 1) % 6;
    else if (inside(x, y, 232, 90, 184, 48)) {
      snprintf(digits, sizeof(digits), "%lu", (unsigned long)(m->mhz / 1000));
      numeric = 1;
      fresh_digits = 1;
    } else if (inside(x, y, 176, 90, 50, 48))
      send_action(3, selected, m->mhz >= 30000 ? m->mhz - 10000 : 20000);
    else if (inside(x, y, 422, 90, 50, 48))
      send_action(3, selected, m->mhz <= 3990000 ? m->mhz + 10000 : 4000000);
    else if (inside(x, y, 8, 202, 160, 48))
      send_action(4, selected, !(m->flags & 1));
    else if (inside(x, y, 176, 146, 296, 48))
      send_action(6, selected, !(m->flags & 4));
    else if (inside(x, y, 176, 202, 296, 48))
      send_action(5, selected, !(m->flags & 2));
    invalidate(0, 28, 480, 236);
  } else if (page == 2) {
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
    Motor old[6];
    memcpy(old, motors, sizeof(old));
    flags = b[1] & 15;
    sleeping = b[2];
    resetting = b[3];
    micro = b[4];
    mask = b[5] & 63;
    position = get32(b + 6);
    duration = get32(b + 10);
    if (position > duration)
      position = duration;
    for (i = 0; i < 6; i++) {
      motors[i].flags = b[14 + i * 6] & 7;
      motors[i].note = b[15 + i * 6];
      motors[i].mhz = get32(b + 16 + i * 6);
      if (motors[i].mhz > 4000000)
        motors[i].mhz = 4000000;
      if (!(flags & 1) || !(mask & (1 << i)))
        motors[i].flags = 0;
    }
    last_state = now;
    have_state = 1;
    if (old_flags != flags)
      invalidate(0, 0, 480, 28);
    if (page == 0) {
      for (i = 0; i < 6; i++) {
        int a = bar_height(&old[i]), v = bar_height(&motors[i]),
            w = 75 * widths[width_index] / 100, x = 8 + (int)i * 78;
        if (a != v)
          invalidate(x + (75 - w) / 2, 128 - (a > v ? a : v), w,
                     a > v ? a - v : v - a);
        if ((show_hz ? old[i].mhz != motors[i].mhz
                     : old[i].note != motors[i].note) ||
            old[i].flags != motors[i].flags)
          invalidate(x, 130, 75, 18);
        if (old[i].flags != motors[i].flags)
          invalidate(x, 150, 75, 48);
      }
      if (oldpos != position || oldduration != duration || old_flags != flags)
        invalidate(8, 206, 464, 50);
    } else if (page == 4 && !numeric) {
      if (memcmp(&old[selected], &motors[selected], sizeof(Motor)))
        invalidate(8, 90, 464, 160);
    } else if (page == 2) {
      if (oldpos != position || oldduration != duration || old_flags != flags)
        invalidate(0, 28, 480, 236);
    } else if (page == 3 && settings_page &&
               (oldsleep != sleeping || oldreset != resetting ||
                oldmicro != micro))
      invalidate(0, 84, 480, 180);
    else if (page == 1 && old_flags != flags)
      invalidate(0, 28, 480, 26);
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
    uint32_t at = get32(b);
    midi_dirty = 1;
    if ((int32_t)(at - midi_now) >= 0)
      midi_now = at;
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
    uint32_t at = get32(b);
    if ((int32_t)(at - midi_now) < 0) {
      memset(notes, 0, sizeof(notes));
      note_next = 0;
    }
    midi_now = at;
    midi_dirty = 1;
  } else if (packet[4] == 0x51 && len == 1 && pending &&
             packet[3] == action_packet[3]) {
    pending = 0;
    if (b[0])
      notification(b[0] == 1 ? "Команда недоступна" : "Ошибка команды");
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
  note_next = 0;
  midi_now = 0;
  midi_dirty = 0;
  notice_until = 0;
  settings_dirty = 0;
  last_paint = last_midi_draw = now;
  memset(notes, 0, sizeof(notes));
  strcpy(title, "Нет композиции");
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
  if (have_state && now - last_state > 1500) {
    unsigned i;
    flags = 0;
    have_state = 0;
    for (i = 0; i < 6; i++)
      motors[i].flags = 0;
    pending = 0;
    full();
  }
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
  if (page == 1 && midi_dirty && now - last_midi_draw >= 200) {
    midi_dirty = 0;
    last_midi_draw = now;
    invalidate(0, 54, 480, 210);
  }
  if (settings_dirty && !touch_down && !(flags & 2) && !dirty_count &&
      now - settings_at >= 5000)
    save_settings();
  if (p && p->write_rect) {
    flush_one();
  }
  last_paint = now;
}
