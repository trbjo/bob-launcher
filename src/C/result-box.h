#ifndef BOB_LAUNCHER_RESULT_BOX_H
#define BOB_LAUNCHER_RESULT_BOX_H

#include <gtk/gtk.h>
#include <hashset.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_RESULT_BOX (bob_launcher_result_box_get_type())
#define BOB_LAUNCHER_RESULT_BOX(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_RESULT_BOX, BobLauncherResultBox))
#define BOB_LAUNCHER_IS_RESULT_BOX(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_RESULT_BOX))

typedef struct _BobLauncherResultBox BobLauncherResultBox;
typedef struct _BobLauncherResultBoxClass BobLauncherResultBoxClass;
typedef struct _BobLauncherMatchRow BobLauncherMatchRow;

GType bob_launcher_result_box_get_type(void) G_GNUC_CONST;
BobLauncherResultBox *bob_launcher_result_box_new(void);
void bob_launcher_result_box_update_layout(BobLauncherResultBox *self, HashSet *provider, gint selected_index);

extern gint bob_launcher_result_box_box_size;
extern gint bob_launcher_result_box_visible_size;
extern BobLauncherMatchRow **bob_launcher_result_box_row_pool;
extern gint bob_launcher_result_box_row_pool_length1;

G_END_DECLS

#endif
