#ifndef BOBLAUNCHER_EVENTS_H
#define BOBLAUNCHER_EVENTS_H

#include <stdatomic.h>
#include <stdint.h>

int events_ok(uint32_t event_id);
int32_t events_get();
int32_t events_increment();

#endif
