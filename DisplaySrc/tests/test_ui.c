/* Native behavioural tests exercise the actual firmware module. */
#include "../src/music_box.c"
#include <assert.h>
#include <stdio.h>
static uint16_t screen[320][480];
static uint8_t saved[8], sent[64];
static unsigned writes, pixel_count, sends, erases;
static uint32_t tick;
static int portrait;
static void out(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                const uint16_t *data, uint16_t stride, void *user) {
  unsigned a, b;
  (void)user;
  assert(w && h && stride >= w);
  assert(w * h <= 960);
  assert(x + w <= (portrait ? 320 : 480));
  assert(y + h <= (portrait ? 480 : 320));
  for (b = 0; b < h; b++)
    for (a = 0; a < w; a++) {
      int xx = portrait ? y + b : x + a, yy = portrait ? 319 - (x + a) : y + b;
      screen[yy][xx] = data[b * stride + a];
    }
  writes++;
  pixel_count += w * h;
}
static int read_flash(uint32_t at, void *data, uint32_t n) {
  assert(at == SETTINGS_ADDR && n == 8);
  memcpy(data, saved, 8);
  return 1;
}
static int erase_flash(uint32_t at) {
  assert(at == SETTINGS_ADDR);
  memset(saved, 255, 8);
  erases++;
  return 1;
}
static int write_flash(uint32_t at, const void *data, uint32_t n) {
  assert(at == SETTINGS_ADDR && n == 8);
  memcpy(saved, data, 8);
  return 1;
}
static void send_bytes(const uint8_t *b, uint16_t n) {
  assert(n <= 64);
  memcpy(sent, b, n);
  sends++;
}
static uint32_t clock_ms(void) { return tick; }
static DisplayPlatform platform = {
    2,           480,         320,      out,        0, read_flash,
    write_flash, erase_flash, clock_ms, send_bytes, 0, 0};
static void drain(void) {
  unsigned limit = 3000;
  while (dirty_count && limit--)
    display_step(tick);
  assert(!dirty_count);
}
static void feed(uint8_t cmd, uint8_t id, const uint8_t *b, unsigned n,
                 int corrupt) {
  unsigned i;
  uint8_t frame[247];
  uint16_t c;
  frame[0] = 0xa5;
  frame[1] = 0x5a;
  frame[2] = (uint8_t)n;
  frame[3] = id;
  frame[4] = cmd;
  memcpy(frame + 5, b, n);
  c = crc16(frame + 2, n + 3);
  frame[n + 5] = (uint8_t)c;
  frame[n + 6] = (uint8_t)(c >> 8);
  if (corrupt)
    frame[n + 5] ^= 1;
  for (i = 0; i < n + 7; i++) {
    DisplayEvent e = {DISPLAY_RX_BYTE, tick, 0, 0, 0, frame[i]};
    display_event(&e);
  }
  display_step(tick);
}
static void state(uint32_t mhz) {
  uint8_t b[50] = {1, 1, 0, 0, 0, 63};
  unsigned i;
  put32(b + 6, 2000);
  put32(b + 10, 93000);
  for (i = 0; i < 6; i++) {
    b[14 + i * 6] = 3;
    b[15 + i * 6] = 69;
    put32(b + 16 + i * 6, mhz);
  }
  feed(0x40, 0, b, 50, 0);
}
static void contact(int x, int y, int down) {
  DisplayEvent e = {DISPLAY_TOUCH,
                    tick,
                    (int16_t)(portrait ? 319 - y : x),
                    (int16_t)(portrait ? x : y),
                    (uint8_t)down,
                    0};
  display_event(&e);
}
static void press(int x, int y) {
  contact(x, y, 1);
  contact(x, y, 0);
  drain();
}
static void ack(void) {
  uint8_t b = 0;
  feed(0x51, sent[3], &b, 1, 0);
  assert(!pending);
}
int main(void) {
  unsigned i, wide, narrow;
  uint16_t reference[320][480];
  uint8_t b[50] = {1, 1, 0, 0, 0, 63};
  memset(saved, 255, 8);
  tick = 10;
  display_init(&platform);
  drain();
  assert(sends == 0);
  assert(screen[0][0] == PANEL);
  state(440000);
  drain();
  memcpy(reference, screen, sizeof(screen));
  portrait = 1;
  platform.width = 320;
  platform.height = 480;
  display_init(&platform);
  state(440000);
  drain();
  assert(!memcmp(reference, screen, sizeof(screen)));
  press(200, 70);
  assert(page == 4 && selected == 2);
  press(310, 112);
  assert(numeric);
  press(220, 52);
  press(315, 52);
  press(410, 52);
  assert(!strcmp(digits, "123"));
  press(70, 225);
  assert(sent[5] == 3 && sent[6] == 2 && get32(sent + 7) == 123000);
  ack();
  press(40, 290);
  assert(page == 0);
  contact(80, 220, 1);
  contact(178, 220, 1);
  contact(178, 220, 0);
  assert(sent[5] == 2 && get32(sent + 7) == 46500);
  ack();
  contact(400, 290, 1);
  assert(sent[5] == 0);
  i = sends;
  contact(400, 290, 1);
  contact(400, 290, 0);
  assert(sends == i);
  ack();
  press(295, 290);
  assert(page == 3);
  press(30, 192);
  assert(width_index == 0);
  press(390, 110);
  assert(show_hz == 1);
  tick = 6010;
  drain();
  display_step(tick);
  drain();
  display_step(tick);
  assert(erases == 1);
  display_init(&platform);
  assert(show_hz == 1 && width_index == 0);
  state(440000);
  drain();
  width_index = 2;
  full();
  drain();
  pixel_count = 0;
  state(880000);
  drain();
  wide = pixel_count;
  state(440000);
  drain();
  width_index = 0;
  full();
  drain();
  pixel_count = 0;
  state(880000);
  drain();
  narrow = pixel_count;
  assert(narrow < wide);
  feed(0x40, 1, b, 50, 1);
  assert(motors[0].mhz == 880000);
  for (i = 0; i < 600; i++) {
    DisplayEvent e = {DISPLAY_RX_BYTE, tick, 0, 0, 0, 0xff};
    display_event(&e);
  }
  display_step(tick);
  state(330000);
  assert(motors[0].mhz == 330000);
  tick += 1600;
  display_step(tick);
  drain();
  assert(!have_state && !flags && !(motors[0].flags & 2));
  assert(screen[100][45] == BG);
  state(440000);
  drain();
  press(40, 160);
  assert(page == 4 && selected == 0);
  press(320, 225);
  assert(pending);
  i = sends;
  tick += 300;
  display_step(tick);
  assert(sends == i + 1);
  tick += 300;
  display_step(tick);
  tick += 300;
  display_step(tick);
  assert(!pending && sends == i + 2);
  {
    uint8_t n[7] = {0};
    put32(n, 1000);
    n[4] = 1;
    n[5] = 64;
    n[6] = 100;
    feed(0x42, 0, n, 7, 0);
    assert(notes[0].used && notes[0].open);
    put32(n, 1500);
    n[6] = 0;
    feed(0x42, 0, n, 7, 0);
    assert(!notes[0].open && notes[0].end == 1500);
  }
  for (i = 0; i < 5; i++) {
    page = (uint8_t)i;
    numeric = 0;
    full();
    drain();
  }
  numeric = 1;
  full();
  drain();
  tick = 0xfffffff0u;
  state(440000);
  tick += 100;
  display_step(tick);
  assert(have_state);
  tick += 1501;
  display_step(tick);
  assert(!have_state);
  printf("PASS: rotation/touch, commands, drag seek, stop, CRC/overflow, "
         "timeout/retry, flash, MIDI, all pages.\nBar update pixels: full=%u "
         "narrow=%u\n",
         wide, narrow);
  return 0;
}
