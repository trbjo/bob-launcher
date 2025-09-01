#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#include <sys/syscall.h>
#include <linux/futex.h>
#include <unistd.h>
#include <glib-object.h>

#define IN_PROGRESS 1
#define EMPTY 0

gboolean g_once_init_enter(volatile void *location) {
    volatile atomic_size_t *slot_ptr = (volatile atomic_size_t*)location;

    if (atomic_load_explicit(slot_ptr, memory_order_acquire) != 0) {
        return FALSE;
    }

    size_t expected = EMPTY;
    if (atomic_compare_exchange_strong_explicit(
            slot_ptr,
            &expected,
            IN_PROGRESS,
            memory_order_acq_rel,
            memory_order_acquire)) {
        return TRUE;
    }

    while (atomic_load_explicit(slot_ptr, memory_order_acquire) == IN_PROGRESS) {
        syscall(SYS_futex, (int*)slot_ptr, FUTEX_WAIT_PRIVATE, IN_PROGRESS, NULL, NULL, 0);
    }
    return FALSE;
}

void g_once_init_leave(volatile void *location, gsize result) {
    volatile atomic_size_t *slot_ptr = (volatile atomic_size_t*)location;
    atomic_store_explicit(slot_ptr, (size_t)result, memory_order_release);
    syscall(SYS_futex, (int*)slot_ptr, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
}
