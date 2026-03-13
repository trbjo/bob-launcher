#ifndef BOB_LAUNCHER_FAST_LABEL_H
#define BOB_LAUNCHER_FAST_LABEL_H

#include <gtk/gtk.h>
#include <pango/pango.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_FAST_LABEL (bob_launcher_fast_label_get_type())
#define BOB_LAUNCHER_FAST_LABEL(obj) ((BobLauncherFastLabel*)(obj))
#define BOB_LAUNCHER_IS_FAST_LABEL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_FAST_LABEL))

typedef struct _BobLauncherFastLabel BobLauncherFastLabel;
typedef struct _BobLauncherFastLabelClass BobLauncherFastLabelClass;

GType bob_launcher_fast_label_get_type(void) G_GNUC_CONST;

BobLauncherFastLabel* bob_launcher_fast_label_new(void);

void bob_launcher_fast_label_invalidate(BobLauncherFastLabel *self);
void bob_launcher_fast_label_set_text(BobLauncherFastLabel *self,
                                       const char *text,
                                       PangoAttrList *attrs);

G_END_DECLS

#endif /* BOB_LAUNCHER_FAST_LABEL_H */
