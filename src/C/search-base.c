#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include "search-base.h"

struct _BobLauncherSearchBasePrivate {
    guint shard_count;
    guint update_interval;
    gboolean enabled_in_default_search;
    char *regex_match;
    GRegex *compiled_regex;
};

static int BobLauncherSearchBase_private_offset;
static gpointer parent_class = NULL;

enum {
    PROP_0,
    PROP_SHARD_COUNT,
    PROP_UPDATE_INTERVAL,
    PROP_ENABLED_IN_DEFAULT_SEARCH,
    PROP_REGEX_MATCH,
    PROP_COMPILED_REGEX,
    N_PROPERTIES
};

static GParamSpec *properties[N_PROPERTIES];

static inline BobLauncherSearchBasePrivate *get_priv(BobLauncherSearchBase *self) {
    return G_STRUCT_MEMBER_P(self, BobLauncherSearchBase_private_offset);
}

static gboolean update_compiled_regex(BobLauncherSearchBase *self, const char *new_value) {
    BobLauncherSearchBasePrivate *priv = get_priv(self);
    GError *error = NULL;

    GRegex *regex = g_regex_new(new_value, G_REGEX_OPTIMIZE, 0, &error);
    if (error) {
        g_warning("Failed to compile regex '%s', reusing existing: %s: %s", new_value, priv->regex_match, error->message);
        g_error_free(error);
        return FALSE;
    }

    if (priv->compiled_regex) g_regex_unref(priv->compiled_regex);
    priv->compiled_regex = regex;
    return TRUE;
}

static guint real_get_shard_count(BobLauncherSearchBase *self) {
    return get_priv(self)->shard_count;
}

static void real_set_shard_count(BobLauncherSearchBase *self, guint value) {
    BobLauncherSearchBasePrivate *priv = get_priv(self);
    if (priv->shard_count != value) {
        priv->shard_count = value;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_SHARD_COUNT]);
    }
}

static void real_search(BobLauncherSearchBase *self, ResultContainer *rs) {
    (void)self; (void)rs;
    g_error("Plugin has not implemented search");
}

static void real_search_shard(BobLauncherSearchBase *self, ResultContainer *rs, guint shard_id) {
    if (shard_id > 0) {
        g_error("Plugin doesn't support sharding");
    }
    BOB_LAUNCHER_SEARCH_BASE_GET_CLASS(self)->search(self, rs);
}

void bob_launcher_search_base_search(BobLauncherSearchBase *self, ResultContainer *rs) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));
    BOB_LAUNCHER_SEARCH_BASE_GET_CLASS(self)->search(self, rs);
}

void bob_launcher_search_base_search_shard(BobLauncherSearchBase *self, ResultContainer *rs, guint shard_id) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));
    BOB_LAUNCHER_SEARCH_BASE_GET_CLASS(self)->search_shard(self, rs, shard_id);
}

void bob_launcher_search_base_handle_base_settings(BobLauncherSearchBase *self, const char *key, GVariant *value) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));

    if (strcmp(key, "regex-match") == 0) {
        bob_launcher_search_base_set_regex_match(self, g_variant_get_string(value, NULL));
    } else if (strcmp(key, "enabled-in-default") == 0) {
        bob_launcher_search_base_set_enabled_in_default_search(self, g_variant_get_boolean(value));
    } else if (strcmp(key, "update-interval") == 0) {
        guint user_value = g_variant_get_uint32(value);
        guint interval = (user_value == 0) ? 0 : MAX(500, user_value) * 1000;
        bob_launcher_search_base_set_update_interval(self, interval);
    }
}

guint bob_launcher_search_base_get_shard_count(BobLauncherSearchBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self), 0);
    return BOB_LAUNCHER_SEARCH_BASE_GET_CLASS(self)->get_shard_count(self);
}

void bob_launcher_search_base_set_shard_count(BobLauncherSearchBase *self, guint value) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));
    BOB_LAUNCHER_SEARCH_BASE_GET_CLASS(self)->set_shard_count(self, value);
}

guint bob_launcher_search_base_get_update_interval(BobLauncherSearchBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self), 0);
    return get_priv(self)->update_interval;
}

void bob_launcher_search_base_set_update_interval(BobLauncherSearchBase *self, guint value) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));
    BobLauncherSearchBasePrivate *priv = get_priv(self);
    if (priv->update_interval != value) {
        priv->update_interval = value;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_UPDATE_INTERVAL]);
    }
}

gboolean bob_launcher_search_base_get_enabled_in_default_search(BobLauncherSearchBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self), FALSE);
    return get_priv(self)->enabled_in_default_search;
}

void bob_launcher_search_base_set_enabled_in_default_search(BobLauncherSearchBase *self, gboolean value) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));
    BobLauncherSearchBasePrivate *priv = get_priv(self);
    if (priv->enabled_in_default_search != value) {
        priv->enabled_in_default_search = value;
        g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_ENABLED_IN_DEFAULT_SEARCH]);
    }
}

const char *bob_launcher_search_base_get_regex_match(BobLauncherSearchBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self), NULL);
    return get_priv(self)->regex_match;
}

void bob_launcher_search_base_set_regex_match(BobLauncherSearchBase *self, const char *value) {
    g_return_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self));
    BobLauncherSearchBasePrivate *priv = get_priv(self);

    if (g_strcmp0(priv->regex_match, value) != 0 && update_compiled_regex(self, value)) {
        free(priv->regex_match);
        priv->regex_match = strdup(value);
    }
    g_object_notify_by_pspec(G_OBJECT(self), properties[PROP_REGEX_MATCH]);
}

GRegex *bob_launcher_search_base_get_compiled_regex(BobLauncherSearchBase *self) {
    g_return_val_if_fail(BOB_LAUNCHER_IS_SEARCH_BASE(self), NULL);
    return get_priv(self)->compiled_regex;
}

static void get_property(GObject *obj, guint prop_id, GValue *value, GParamSpec *pspec) {
    BobLauncherSearchBase *self = BOB_LAUNCHER_SEARCH_BASE(obj);
    switch (prop_id) {
        case PROP_SHARD_COUNT: g_value_set_uint(value, bob_launcher_search_base_get_shard_count(self)); break;
        case PROP_UPDATE_INTERVAL: g_value_set_uint(value, bob_launcher_search_base_get_update_interval(self)); break;
        case PROP_ENABLED_IN_DEFAULT_SEARCH: g_value_set_boolean(value, bob_launcher_search_base_get_enabled_in_default_search(self)); break;
        case PROP_REGEX_MATCH: g_value_set_string(value, bob_launcher_search_base_get_regex_match(self)); break;
        case PROP_COMPILED_REGEX: g_value_set_boxed(value, bob_launcher_search_base_get_compiled_regex(self)); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
    }
}

static void set_property(GObject *obj, guint prop_id, const GValue *value, GParamSpec *pspec) {
    BobLauncherSearchBase *self = BOB_LAUNCHER_SEARCH_BASE(obj);
    switch (prop_id) {
        case PROP_SHARD_COUNT: bob_launcher_search_base_set_shard_count(self, g_value_get_uint(value)); break;
        case PROP_UPDATE_INTERVAL: bob_launcher_search_base_set_update_interval(self, g_value_get_uint(value)); break;
        case PROP_ENABLED_IN_DEFAULT_SEARCH: bob_launcher_search_base_set_enabled_in_default_search(self, g_value_get_boolean(value)); break;
        case PROP_REGEX_MATCH: bob_launcher_search_base_set_regex_match(self, g_value_get_string(value)); break;
        default: G_OBJECT_WARN_INVALID_PROPERTY_ID(obj, prop_id, pspec);
    }
}

static void finalize(GObject *obj) {
    BobLauncherSearchBase *self = BOB_LAUNCHER_SEARCH_BASE(obj);
    BobLauncherSearchBasePrivate *priv = get_priv(self);

    free(priv->regex_match);
    if (priv->compiled_regex) g_regex_unref(priv->compiled_regex);

    G_OBJECT_CLASS(parent_class)->finalize(obj);
}

static void class_init(BobLauncherSearchBaseClass *klass, gpointer data) {
    (void)data;
    parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherSearchBase_private_offset);

    klass->get_shard_count = real_get_shard_count;
    klass->set_shard_count = real_set_shard_count;
    klass->search = real_search;
    klass->search_shard = real_search_shard;

    GObjectClass *obj_class = G_OBJECT_CLASS(klass);
    obj_class->get_property = get_property;
    obj_class->set_property = set_property;
    obj_class->finalize = finalize;

    properties[PROP_SHARD_COUNT] = g_param_spec_uint("shard-count", "shard-count", "shard-count", 0, G_MAXUINT, 1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_UPDATE_INTERVAL] = g_param_spec_uint("update-interval", "update-interval", "update-interval", 0, G_MAXUINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_ENABLED_IN_DEFAULT_SEARCH] = g_param_spec_boolean("enabled-in-default-search", "enabled-in-default-search", "enabled-in-default-search", FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_REGEX_MATCH] = g_param_spec_string("regex-match", "regex-match", "regex-match", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
    properties[PROP_COMPILED_REGEX] = g_param_spec_boxed("compiled-regex", "compiled-regex", "compiled-regex", G_TYPE_REGEX, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

    g_object_class_install_properties(obj_class, N_PROPERTIES, properties);
}

static void instance_init(BobLauncherSearchBase *self, gpointer klass) {
    (void)klass;
    self->priv = get_priv(self);
    self->priv->shard_count = 1;
    self->priv->regex_match = strdup("^$");
}

BobLauncherSearchBase *bob_launcher_search_base_construct(GType object_type) {
    BobLauncherSearchBase *self = NULL;
    self = (BobLauncherSearchBase *)bob_launcher_plugin_base_construct(object_type);
    return self;
}

GType bob_launcher_search_base_get_type(void) {
    static volatile gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(BobLauncherSearchBaseClass), NULL, NULL,
            (GClassInitFunc)class_init, NULL, NULL,
            sizeof(BobLauncherSearchBase), 0,
            (GInstanceInitFunc)instance_init, NULL
        };
        GType id = g_type_register_static(BOB_LAUNCHER_TYPE_PLUGIN_BASE, "BobLauncherSearchBase", &info, G_TYPE_FLAG_ABSTRACT);
        BobLauncherSearchBase_private_offset = g_type_add_instance_private(id, sizeof(BobLauncherSearchBasePrivate));
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}
