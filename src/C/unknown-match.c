#include "unknown-match.h"
#include "bob-launcher.h"

/* ============================================================================
 * Type definitions
 * ============================================================================ */

struct _BobLauncherUnknownMatchPrivate {
    gchar *query_string;
};

struct _BobLauncherUnknownMatch {
    BobLauncherMatch parent_instance;
    BobLauncherUnknownMatchPrivate *priv;
};

struct _BobLauncherUnknownMatchClass {
    BobLauncherMatchClass parent_class;
};

/* ============================================================================
 * Static variables
 * ============================================================================ */

static gint BobLauncherUnknownMatch_private_offset;
static gpointer bob_launcher_unknown_match_parent_class = NULL;

/* Interface parent pointer */
static BobLauncherITextMatchIface *unknown_match_itext_match_parent_iface = NULL;

/* ============================================================================
 * Private function declarations
 * ============================================================================ */

static inline BobLauncherUnknownMatchPrivate *
bob_launcher_unknown_match_get_instance_private(BobLauncherUnknownMatch *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherUnknownMatch_private_offset);
}

static void bob_launcher_unknown_match_class_init(BobLauncherUnknownMatchClass *klass, gpointer klass_data);
static void bob_launcher_unknown_match_instance_init(BobLauncherUnknownMatch *self, gpointer klass);
static void bob_launcher_unknown_match_itext_match_interface_init(BobLauncherITextMatchIface *iface, gpointer data);
static void bob_launcher_unknown_match_finalize(GObject *obj);

/* Match virtual methods */
static gchar *bob_launcher_unknown_match_get_title(BobLauncherMatch *base);
static gchar *bob_launcher_unknown_match_get_description(BobLauncherMatch *base);
static gchar *bob_launcher_unknown_match_get_icon_name(BobLauncherMatch *base);

/* ITextMatch interface methods */
static gchar *bob_launcher_unknown_match_itext_match_get_text(BobLauncherITextMatch *iface);

/* ============================================================================
 * Type registration
 * ============================================================================ */

static GType
bob_launcher_unknown_match_get_type_once(void)
{
    static const GTypeInfo type_info = {
        sizeof(BobLauncherUnknownMatchClass),
        NULL, NULL,
        (GClassInitFunc)bob_launcher_unknown_match_class_init,
        NULL, NULL,
        sizeof(BobLauncherUnknownMatch),
        0,
        (GInstanceInitFunc)bob_launcher_unknown_match_instance_init,
        NULL
    };

    static const GInterfaceInfo itext_match_info = {
        (GInterfaceInitFunc)bob_launcher_unknown_match_itext_match_interface_init,
        NULL, NULL
    };

    GType type_id = g_type_register_static(BOB_LAUNCHER_TYPE_MATCH,
                                           "BobLauncherUnknownMatch",
                                           &type_info, 0);

    g_type_add_interface_static(type_id, BOB_LAUNCHER_TYPE_ITEXT_MATCH, &itext_match_info);

    BobLauncherUnknownMatch_private_offset =
        g_type_add_instance_private(type_id, sizeof(BobLauncherUnknownMatchPrivate));

    return type_id;
}

GType
bob_launcher_unknown_match_get_type(void)
{
    static volatile gsize type_id_once = 0;
    if (g_once_init_enter(&type_id_once)) {
        GType type_id = bob_launcher_unknown_match_get_type_once();
        g_once_init_leave(&type_id_once, type_id);
    }
    return type_id_once;
}

/* ============================================================================
 * Class initialization
 * ============================================================================ */

static void
bob_launcher_unknown_match_class_init(BobLauncherUnknownMatchClass *klass, gpointer klass_data)
{
    bob_launcher_unknown_match_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherUnknownMatch_private_offset);

    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->finalize = bob_launcher_unknown_match_finalize;

    BobLauncherMatchClass *match_class = BOB_LAUNCHER_MATCH_CLASS(klass);
    match_class->get_title = bob_launcher_unknown_match_get_title;
    match_class->get_description = bob_launcher_unknown_match_get_description;
    match_class->get_icon_name = bob_launcher_unknown_match_get_icon_name;
}

/* ============================================================================
 * Instance initialization
 * ============================================================================ */

static void
bob_launcher_unknown_match_instance_init(BobLauncherUnknownMatch *self, gpointer klass)
{
    self->priv = bob_launcher_unknown_match_get_instance_private(self);
    self->priv->query_string = NULL;
}

/* ============================================================================
 * Interface initialization
 * ============================================================================ */

static void
bob_launcher_unknown_match_itext_match_interface_init(BobLauncherITextMatchIface *iface, gpointer data)
{
    unknown_match_itext_match_parent_iface = g_type_interface_peek_parent(iface);
    iface->get_text = bob_launcher_unknown_match_itext_match_get_text;
}

/* ============================================================================
 * Object lifecycle
 * ============================================================================ */

static void
bob_launcher_unknown_match_finalize(GObject *obj)
{
    BobLauncherUnknownMatch *self = BOB_LAUNCHER_UNKNOWN_MATCH(obj);
    BobLauncherUnknownMatchPrivate *priv = self->priv;

    g_free(priv->query_string);

    G_OBJECT_CLASS(bob_launcher_unknown_match_parent_class)->finalize(obj);
}

/* ============================================================================
 * Match virtual method implementations
 * ============================================================================ */

static gchar *
bob_launcher_unknown_match_get_title(BobLauncherMatch *base)
{
    BobLauncherUnknownMatch *self = BOB_LAUNCHER_UNKNOWN_MATCH(base);
    return g_strdup(self->priv->query_string);
}

static gchar *
bob_launcher_unknown_match_get_description(BobLauncherMatch *base)
{
    return g_strdup("");
}

static gchar *
bob_launcher_unknown_match_get_icon_name(BobLauncherMatch *base)
{
    return g_strdup("unknown");
}

/* ============================================================================
 * ITextMatch interface implementation
 * ============================================================================ */

static gchar *
bob_launcher_unknown_match_itext_match_get_text(BobLauncherITextMatch *iface)
{
    BobLauncherUnknownMatch *self = BOB_LAUNCHER_UNKNOWN_MATCH(iface);
    return g_strdup(self->priv->query_string);
}

/* ============================================================================
 * Public API
 * ============================================================================ */

BobLauncherUnknownMatch *
bob_launcher_unknown_match_new(const gchar *query_string)
{
    BobLauncherUnknownMatch *self = g_object_new(BOB_LAUNCHER_TYPE_UNKNOWN_MATCH, NULL);
    self->priv->query_string = g_strdup(query_string);
    return self;
}

gchar *
bob_launcher_unknown_match_get_mime_type(BobLauncherUnknownMatch *self)
{
    g_return_val_if_fail(BOB_LAUNCHER_IS_UNKNOWN_MATCH(self), NULL);
    return g_strdup("application/x-unknown");
}
