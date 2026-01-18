#ifndef BOB_LAUNCHER_TEXT_IMAGE_H
#define BOB_LAUNCHER_TEXT_IMAGE_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_TEXT_IMAGE (bob_launcher_text_image_get_type())
#define BOB_LAUNCHER_TEXT_IMAGE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_TEXT_IMAGE, BobLauncherTextImage))
#define BOB_LAUNCHER_IS_TEXT_IMAGE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_TEXT_IMAGE))

typedef struct _BobLauncherTextImage BobLauncherTextImage;
typedef struct _BobLauncherTextImageClass BobLauncherTextImageClass;

GType bob_launcher_text_image_get_type(void) G_GNUC_CONST;
BobLauncherTextImage *bob_launcher_text_image_new(void);
void bob_launcher_text_image_update_icon_name(BobLauncherTextImage *self, const gchar *new_icon_name);

G_END_DECLS

#endif
