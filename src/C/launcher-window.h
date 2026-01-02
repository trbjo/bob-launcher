#ifndef BOB_LAUNCHER_LAUNCHER_WINDOW_H
#define BOB_LAUNCHER_LAUNCHER_WINDOW_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_LAUNCHER_WINDOW (bob_launcher_launcher_window_get_type())
#define BOB_LAUNCHER_LAUNCHER_WINDOW(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_LAUNCHER_WINDOW, BobLauncherLauncherWindow))
#define BOB_LAUNCHER_IS_LAUNCHER_WINDOW(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_LAUNCHER_WINDOW))

typedef struct _BobLauncherLauncherWindow BobLauncherLauncherWindow;
typedef struct _BobLauncherLauncherWindowClass BobLauncherLauncherWindowClass;
typedef struct _BobLauncherLauncherWindowPrivate BobLauncherLauncherWindowPrivate;

/* Forward declarations for handle types */
typedef struct _BobLauncherUpDownResizeHandle BobLauncherUpDownResizeHandle;
typedef struct _BobLauncherWidthResizeHandle BobLauncherWidthResizeHandle;

struct _BobLauncherLauncherWindow {
    GtkWindow parent_instance;
    BobLauncherLauncherWindowPrivate *priv;
};

struct _BobLauncherLauncherWindowClass {
    GtkWindowClass parent_class;
};

/* Public static handles - accessed by other C files */
extern BobLauncherUpDownResizeHandle *bob_launcher_launcher_window_up_down_handle;
extern BobLauncherWidthResizeHandle *bob_launcher_launcher_window_width_handle;

GType bob_launcher_launcher_window_get_type(void) G_GNUC_CONST;
BobLauncherLauncherWindow *bob_launcher_launcher_window_new(void);
void bob_launcher_launcher_window_ensure_surface(BobLauncherLauncherWindow *self);

/* InputRegion functions */
void input_region_initialize(void);
void input_region_reset(void);

G_END_DECLS

#endif /* BOB_LAUNCHER_LAUNCHER_WINDOW_H */
