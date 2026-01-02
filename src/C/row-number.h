#ifndef BOB_LAUNCHER_ROW_NUMBER_H
#define BOB_LAUNCHER_ROW_NUMBER_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_ROW_NUMBER (bob_launcher_row_number_get_type())
#define BOB_LAUNCHER_ROW_NUMBER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_ROW_NUMBER, BobLauncherRowNumber))
#define BOB_LAUNCHER_IS_ROW_NUMBER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_ROW_NUMBER))

typedef struct _BobLauncherRowNumber BobLauncherRowNumber;
typedef struct _BobLauncherRowNumberClass BobLauncherRowNumberClass;

GType bob_launcher_row_number_get_type(void) G_GNUC_CONST;
BobLauncherRowNumber *bob_launcher_row_number_new(int row_num);
void bob_launcher_row_number_update_row_num(BobLauncherRowNumber *self, int new_row);

G_END_DECLS

#endif
