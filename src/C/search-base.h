#ifndef BOB_LAUNCHER_SEARCH_BASE_H
#define BOB_LAUNCHER_SEARCH_BASE_H

#include <glib-object.h>
#include "plugin-base.h"
#include "result-container.h"

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_SEARCH_BASE (bob_launcher_search_base_get_type())
#define BOB_LAUNCHER_SEARCH_BASE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_SEARCH_BASE, BobLauncherSearchBase))
#define BOB_LAUNCHER_SEARCH_BASE_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), BOB_LAUNCHER_TYPE_SEARCH_BASE, BobLauncherSearchBaseClass))
#define BOB_LAUNCHER_IS_SEARCH_BASE(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_SEARCH_BASE))
#define BOB_LAUNCHER_SEARCH_BASE_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), BOB_LAUNCHER_TYPE_SEARCH_BASE, BobLauncherSearchBaseClass))

typedef struct _BobLauncherSearchBase BobLauncherSearchBase;
typedef struct _BobLauncherSearchBaseClass BobLauncherSearchBaseClass;
typedef struct _BobLauncherSearchBasePrivate BobLauncherSearchBasePrivate;

struct _BobLauncherSearchBase {
    BobLauncherPluginBase parent;
    BobLauncherSearchBasePrivate *priv;
};

struct _BobLauncherSearchBaseClass {
    BobLauncherPluginBaseClass parent_class;
    guint (*get_shard_count)(BobLauncherSearchBase *self);
    void (*set_shard_count)(BobLauncherSearchBase *self, guint value);
    void (*search)(BobLauncherSearchBase *self, ResultContainer *rs);
    void (*search_shard)(BobLauncherSearchBase *self, ResultContainer *rs, guint shard_id);
};

GType bob_launcher_search_base_get_type(void) G_GNUC_CONST;

void bob_launcher_search_base_search(BobLauncherSearchBase *self, ResultContainer *rs);
void bob_launcher_search_base_search_shard(BobLauncherSearchBase *self, ResultContainer *rs, guint shard_id);
void bob_launcher_search_base_handle_base_settings(BobLauncherSearchBase *self, const char *key, GVariant *value);
BobLauncherSearchBase *bob_launcher_search_base_construct(GType object_type);

guint bob_launcher_search_base_get_shard_count(BobLauncherSearchBase *self);
void bob_launcher_search_base_set_shard_count(BobLauncherSearchBase *self, guint value);
guint bob_launcher_search_base_get_update_interval(BobLauncherSearchBase *self);
void bob_launcher_search_base_set_update_interval(BobLauncherSearchBase *self, guint value);
gboolean bob_launcher_search_base_get_enabled_in_default_search(BobLauncherSearchBase *self);
void bob_launcher_search_base_set_enabled_in_default_search(BobLauncherSearchBase *self, gboolean value);
const char *bob_launcher_search_base_get_regex_match(BobLauncherSearchBase *self);
void bob_launcher_search_base_set_regex_match(BobLauncherSearchBase *self, const char *value);
GRegex *bob_launcher_search_base_get_compiled_regex(BobLauncherSearchBase *self);

G_END_DECLS

#endif
