#ifndef BOB_LAUNCHER_WIDTH_RESIZE_HANDLE_H
#define BOB_LAUNCHER_WIDTH_RESIZE_HANDLE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_WIDTH_RESIZE_HANDLE (bob_launcher_width_resize_handle_get_type())
#define BOB_LAUNCHER_WIDTH_RESIZE_HANDLE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_WIDTH_RESIZE_HANDLE, BobLauncherWidthResizeHandle))
#define BOB_LAUNCHER_IS_WIDTH_RESIZE_HANDLE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_WIDTH_RESIZE_HANDLE))

typedef struct _BobLauncherWidthResizeHandle BobLauncherWidthResizeHandle;
typedef struct _BobLauncherWidthResizeHandleClass BobLauncherWidthResizeHandleClass;

GType bob_launcher_width_resize_handle_get_type(void) G_GNUC_CONST;
BobLauncherWidthResizeHandle *bob_launcher_width_resize_handle_new(void);

G_END_DECLS

#endif
