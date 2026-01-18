#ifndef BOB_LAUNCHER_FILE_MATCH_H
#define BOB_LAUNCHER_FILE_MATCH_H

#include <gtk/gtk.h>
#include <gio/gio.h>
#include "match.h"
#include "description.h"
#include "highlight.h"

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_FILE_MATCH (bob_launcher_file_match_get_type())
#define BOB_LAUNCHER_FILE_MATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_FILE_MATCH, BobLauncherFileMatch))
#define BOB_LAUNCHER_FILE_MATCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), BOB_LAUNCHER_TYPE_FILE_MATCH, BobLauncherFileMatchClass))
#define BOB_LAUNCHER_IS_FILE_MATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_FILE_MATCH))
#define BOB_LAUNCHER_IS_FILE_MATCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), BOB_LAUNCHER_TYPE_FILE_MATCH))
#define BOB_LAUNCHER_FILE_MATCH_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), BOB_LAUNCHER_TYPE_FILE_MATCH, BobLauncherFileMatchClass))

typedef struct _BobLauncherFileMatch BobLauncherFileMatch;
typedef struct _BobLauncherFileMatchClass BobLauncherFileMatchClass;
typedef struct _BobLauncherFileMatchPrivate BobLauncherFileMatchPrivate;

extern const gchar *BOB_LAUNCHER_FILE_MATCH_SEARCH_FILE_ATTRIBUTES;

GType bob_launcher_file_match_get_type(void) G_GNUC_CONST;

/* Constructors */
BobLauncherFileMatch *bob_launcher_file_match_new_from_path(const gchar *filename);
BobLauncherFileMatch *bob_launcher_file_match_new_from_uri(const gchar *uri);

/* Property accessors */
const gchar *bob_launcher_file_match_get_filename(BobLauncherFileMatch *self);
GDateTime *bob_launcher_file_match_get_timestamp(BobLauncherFileMatch *self);
void bob_launcher_file_match_set_timestamp(BobLauncherFileMatch *self, GDateTime *value);

/* Public methods */
void bob_launcher_file_match_rehighlight_matches(BobLauncherFileMatch *self);
GFileInfo *bob_launcher_file_match_get_file_info(BobLauncherFileMatch *self);

/* Static utility function */
Description *bob_launcher_file_match_generate_description_for_file(
    needle_info *si,
    const gchar *file_path,
    GDateTime *timestamp);

GPtrArray *bob_launcher_file_match_split_path_with_separators(const gchar *path);

G_END_DECLS

#endif /* BOB_LAUNCHER_FILE_MATCH_H */
