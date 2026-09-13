#ifndef JOURNAL_APP_H
#define JOURNAL_APP_H
#include "hmi_ui.h"
void journal_app_init(HmiUi *ui);
void journal_app_event(const HmiEvent *event);
void journal_app_tick(uint32_t now);
#endif
