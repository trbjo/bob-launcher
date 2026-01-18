#ifndef BOB_LAUNCHER_RESIZE_HANDLE_H
#define BOB_LAUNCHER_RESIZE_HANDLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_UP_DOWN_RESIZE_HANDLE (bob_launcher_up_down_resize_handle_get_type())
#define BOB_LAUNCHER_UP_DOWN_RESIZE_HANDLE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_UP_DOWN_RESIZE_HANDLE, BobLauncherUpDownResizeHandle))
#define BOB_LAUNCHER_IS_UP_DOWN_RESIZE_HANDLE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_UP_DOWN_RESIZE_HANDLE))

typedef struct _BobLauncherUpDownResizeHandle BobLauncherUpDownResizeHandle;
typedef struct _BobLauncherUpDownResizeHandleClass BobLauncherUpDownResizeHandleClass;

GType bob_launcher_up_down_resize_handle_get_type(void) G_GNUC_CONST;
BobLauncherUpDownResizeHandle *bob_launcher_up_down_resize_handle_new(void);

G_END_DECLS

#endif
