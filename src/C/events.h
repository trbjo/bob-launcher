#pragma once

#include <stdatomic.h>
#include <stdint.h>

int32_t events_ok(int32_t event_id);
int32_t events_get();
int32_t events_increment();
