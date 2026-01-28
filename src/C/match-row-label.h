#ifndef BOB_LAUNCHER_MATCH_ROW_LABEL_H
#define BOB_LAUNCHER_MATCH_ROW_LABEL_H

#include <gtk/gtk.h>
#include "description.h"

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL (bob_launcher_match_row_label_get_type())
#define BOB_LAUNCHER_MATCH_ROW_LABEL(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL, BobLauncherMatchRowLabel))
#define BOB_LAUNCHER_IS_MATCH_ROW_LABEL(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_MATCH_ROW_LABEL))

typedef struct _BobLauncherMatchRowLabel BobLauncherMatchRowLabel;
typedef struct _BobLauncherMatchRowLabelClass BobLauncherMatchRowLabelClass;
typedef struct _BobLauncherTextImage BobLauncherTextImage;

GType bob_launcher_match_row_label_get_type(void) G_GNUC_CONST;

BobLauncherMatchRowLabel* bob_launcher_match_row_label_new(gchar **css_classes, gint css_classes_length1);

void bob_launcher_match_row_label_set_text(BobLauncherMatchRowLabel *self,
                                            const char *text,
                                            PangoAttrList *attrs);

void bob_launcher_match_row_label_set_description(BobLauncherMatchRowLabel *self,
                                                   Description *desc);

void bob_launcher_match_row_label_reset(BobLauncherMatchRowLabel *self);

gboolean bob_launcher_match_row_label_lookup_click(BobLauncherMatchRowLabel *self,
                                                    GtkWidget *widget,
                                                    ClickFunc *out_func,
                                                    void **out_target);

/* Performance diagnostics - call after a search to see timing breakdown */
void bob_launcher_match_row_label_print_perf_stats(void);
void bob_launcher_match_row_label_reset_perf_stats(void);

G_END_DECLS

#endif /* BOB_LAUNCHER_MATCH_ROW_LABEL_H */
