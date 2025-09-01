#ifndef NO_THREAD_INIT_H
#define NO_THREAD_INIT_H

/* Block the unwanted headers BEFORE including glib */
#define __G_THREAD_H__
#define __G_ATOMIC_H__
#define __G_ASYNCQUEUE_H__
#define __GI_SCANNER__
#define __G_DEPRECATED_THREAD_H__
#define __G_DEPRECATED_MAIN_H__

#include <stdatomic.h>

typedef void* (*GThreadFunc) (void* data);

// upstream comment:
/* This macro is defeat a false -Wnonnull warning in GCC.
 * Without it, it thinks strlen and memcmp may be getting passed NULL
 * despite the explicit check for NULL right above the calls.
 */
#define _G_STR_NONNULL(x) ((x) + !(x))

typedef union  _GMutex          GMutex;
typedef struct _GCond           GCond;

#include <glib.h>

/* Override g_once_init with simple versions */
#undef g_cond_clear
#define g_cond_clear(cond) ((void)0)

GLIB_AVAILABLE_IN_ALL
gboolean        g_once_init_enter               (volatile void  *location);
GLIB_AVAILABLE_IN_ALL
void            g_once_init_leave               (volatile void  *location,
                                                 gsize           result);
/* Map GLib atomics to C11 atomics */
#undef g_atomic_int_inc
#undef g_atomic_int_dec_and_test

#define g_atomic_int_inc(atomic) atomic_fetch_add((atomic), 1)
#define g_atomic_int_dec_and_test(atomic) (atomic_fetch_sub((atomic), 1) == 1)

#endif
