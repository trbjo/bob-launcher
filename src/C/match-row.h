#ifndef BOB_LAUNCHER_MATCH_ROW_H
#define BOB_LAUNCHER_MATCH_ROW_H

#include <gtk/gtk.h>
#include "match.h"

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_MATCH_ROW (bob_launcher_match_row_get_type())
#define BOB_LAUNCHER_MATCH_ROW(obj) ((BobLauncherMatchRow*)obj)
#define BOB_LAUNCHER_IS_MATCH_ROW(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_MATCH_ROW))

typedef struct _BobLauncherMatchRow BobLauncherMatchRow;
typedef struct _BobLauncherMatchRowClass BobLauncherMatchRowClass;
typedef struct _BobLauncherMatchRowPrivate BobLauncherMatchRowPrivate;

struct _BobLauncherMatchRow {
    GtkWidget parent_instance;
    BobLauncherMatchRowPrivate *priv;
    gint abs_index;
    gint event_id;
};

struct _BobLauncherMatchRowClass {
    GtkWidgetClass parent_class;
};

GType bob_launcher_match_row_get_type(void) G_GNUC_CONST;
BobLauncherMatchRow *bob_launcher_match_row_new(gint abs_index);
void bob_launcher_match_row_update_match(BobLauncherMatchRow *self, needle_info *si);
void bob_launcher_match_row_update(BobLauncherMatchRow *self,
                                   needle_info *si,
                                   gint new_row,
                                   gint new_abs_index,
                                   gboolean row_selected,
                                   gint new_event);

G_END_DECLS

#endif /* BOB_LAUNCHER_MATCH_ROW_H */
