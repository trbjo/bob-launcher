#include "events.h"

static atomic_int _event __attribute__((aligned(64)));

int events_ok(uint32_t event_id) {
    return event_id == atomic_load(&_event);
}

int32_t events_get() {
    return atomic_load(&_event);
}

int32_t events_increment() {
    int32_t val = atomic_fetch_add(&_event, 1);
    return val + 1;
}
