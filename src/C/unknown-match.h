#ifndef BOB_LAUNCHER_UNKNOWN_MATCH_H
#define BOB_LAUNCHER_UNKNOWN_MATCH_H

#include <glib-object.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_UNKNOWN_MATCH (bob_launcher_unknown_match_get_type())
#define BOB_LAUNCHER_UNKNOWN_MATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_UNKNOWN_MATCH, BobLauncherUnknownMatch))
#define BOB_LAUNCHER_UNKNOWN_MATCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST((klass), BOB_LAUNCHER_TYPE_UNKNOWN_MATCH, BobLauncherUnknownMatchClass))
#define BOB_LAUNCHER_IS_UNKNOWN_MATCH(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_UNKNOWN_MATCH))
#define BOB_LAUNCHER_IS_UNKNOWN_MATCH_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), BOB_LAUNCHER_TYPE_UNKNOWN_MATCH))
#define BOB_LAUNCHER_UNKNOWN_MATCH_GET_CLASS(obj) \
    (G_TYPE_INSTANCE_GET_CLASS((obj), BOB_LAUNCHER_TYPE_UNKNOWN_MATCH, BobLauncherUnknownMatchClass))

typedef struct _BobLauncherUnknownMatch BobLauncherUnknownMatch;
typedef struct _BobLauncherUnknownMatchClass BobLauncherUnknownMatchClass;
typedef struct _BobLauncherUnknownMatchPrivate BobLauncherUnknownMatchPrivate;

GType bob_launcher_unknown_match_get_type(void) G_GNUC_CONST;

/* Constructor */
BobLauncherUnknownMatch *bob_launcher_unknown_match_new(const gchar *query_string);

/* Public method */
gchar *bob_launcher_unknown_match_get_mime_type(BobLauncherUnknownMatch *self);

G_END_DECLS

#endif /* BOB_LAUNCHER_UNKNOWN_MATCH_H */
