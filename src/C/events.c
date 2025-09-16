#include "events.h"

static atomic_int _event __attribute__((aligned(64))) = 0;

int32_t events_ok(int32_t event_id) {
    return event_id == atomic_load(&_event);
}

int32_t events_get() {
    return atomic_load(&_event);
}

int32_t events_increment() {
    return atomic_fetch_add(&_event, 1) + 1;
}
