# Vala to C Conversion Guide for GTK4 Widgets (BobLauncher)

## File Naming
- Use hyphens: `result-box.c`, `result-box.h`, `result-box.vapi`
- VAPI goes in `vapi/` directory
- C files go in `src/C/`
- Add to `meson.build`: C files in `c_sources`, VAPI via `--pkg` in `base_vala_args`

## Header File Structure (.h)

```c
#ifndef BOB_LAUNCHER_WIDGET_NAME_H
#define BOB_LAUNCHER_WIDGET_NAME_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define BOB_LAUNCHER_TYPE_WIDGET_NAME (bob_launcher_widget_name_get_type())
#define BOB_LAUNCHER_WIDGET_NAME(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), BOB_LAUNCHER_TYPE_WIDGET_NAME, BobLauncherWidgetName))
#define BOB_LAUNCHER_IS_WIDGET_NAME(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), BOB_LAUNCHER_TYPE_WIDGET_NAME))

typedef struct _BobLauncherWidgetName BobLauncherWidgetName;
typedef struct _BobLauncherWidgetNameClass BobLauncherWidgetNameClass;
typedef struct _BobLauncherOtherType BobLauncherOtherType;  // forward declarations

GType bob_launcher_widget_name_get_type(void) G_GNUC_CONST;
BobLauncherWidgetName *bob_launcher_widget_name_new(void);
void bob_launcher_widget_name_public_method(BobLauncherWidgetName *self, gint arg);

extern gint bob_launcher_widget_name_public_var;  // only if needed externally

G_END_DECLS

#endif
```

**Header rules:**
- Only expose what other files need
- Forward declare types you reference but don't define
- Keep internal classes (like Separator) out of the header entirely

## VAPI File Structure (.vapi)

```vala
namespace BobLauncher {
    [CCode (cheader_filename = "widget-name.h", cname = "BobLauncherWidgetName", type_id = "bob_launcher_widget_name_get_type()")]
    public class WidgetName : Gtk.Widget {
        [CCode (cname = "bob_launcher_widget_name_new")]
        public WidgetName();

        [CCode (cname = "bob_launcher_widget_name_public_method")]
        public void public_method(int arg);

        [CCode (cname = "bob_launcher_widget_name_public_var")]
        public static int public_var;
    }
}
```

**Critical VAPI rules:**
- `cheader_filename` MUST be on EACH class, not the namespace (this is the #1 cause of "unknown type" errors)
- Use hyphenated filename matching actual file: `"widget-name.h"`
- Nested Vala classes become separate sibling classes or file-private in C
- Don't expose internal classes in VAPI if not needed by Vala code

## C Implementation File Structure (.c)

### 1. Includes and External Type Definitions

```c
#include "bob-launcher.h"
#include "widget-name.h"
#include <state.h>
#include <match.h>

// Full struct definition for types whose fields you access
typedef struct _BobLauncherOtherTypePrivate BobLauncherOtherTypePrivate;
struct _BobLauncherOtherType {
    GtkWidget parent_instance;
    BobLauncherOtherTypePrivate *priv;
    gint field_you_need;  // fields accessed directly
    gint another_field;
};

// Forward declaration for types you only use as pointers
typedef struct _BobLauncherSomeHandle BobLauncherSomeHandle;
```

### 2. Type Definitions

```c
typedef struct _BobLauncherWidgetNamePrivate BobLauncherWidgetNamePrivate;

struct _BobLauncherWidgetName {
    GtkWidget parent_instance;
    BobLauncherWidgetNamePrivate *priv;
};

struct _BobLauncherWidgetNameClass {
    GtkWidgetClass parent_class;
};

struct _BobLauncherWidgetNamePrivate {
    GSettings *settings;
    gint some_field;
};
```

### 3. Static Variables

```c
static gint BobLauncherWidgetName_private_offset;
static gpointer bob_launcher_widget_name_parent_class = NULL;
static gint *internal_array;

// Exported globals (match header externs)
gint bob_launcher_widget_name_public_var = 0;
```

### 4. External Declarations

```c
extern BobLauncherOtherType *bob_launcher_other_type_new(gint arg);
extern void bob_launcher_other_func(void);
extern BobLauncherSomeHandle *bob_launcher_some_global;
```

### 5. Forward Declarations

```c
static void bob_launcher_widget_name_class_init(BobLauncherWidgetNameClass *klass, gpointer klass_data);
static void bob_launcher_widget_name_instance_init(BobLauncherWidgetName *self, gpointer klass);
static void bob_launcher_widget_name_finalize(GObject *obj);
```

### 6. Private Instance Accessor

```c
static inline gpointer
bob_launcher_widget_name_get_instance_private(BobLauncherWidgetName *self)
{
    return G_STRUCT_MEMBER_P(self, BobLauncherWidgetName_private_offset);
}
```

### 7. Internal Helper Class (if needed)

```c
typedef struct _BobLauncherWidgetNameHelper BobLauncherWidgetNameHelper;
typedef struct _BobLauncherWidgetNameHelperClass BobLauncherWidgetNameHelperClass;

struct _BobLauncherWidgetNameHelper {
    GtkWidget parent_instance;
};

struct _BobLauncherWidgetNameHelperClass {
    GtkWidgetClass parent_class;
};

static gpointer bob_launcher_widget_name_helper_parent_class = NULL;

static void
bob_launcher_widget_name_helper_class_init(BobLauncherWidgetNameHelperClass *klass, gpointer klass_data)
{
    bob_launcher_widget_name_helper_parent_class = g_type_class_peek_parent(klass);
    gtk_widget_class_set_css_name(GTK_WIDGET_CLASS(klass), "css-name");
}

static GType
bob_launcher_widget_name_helper_get_type(void)
{
    static gsize type_id = 0;
    if (g_once_init_enter(&type_id)) {
        static const GTypeInfo info = {
            sizeof(BobLauncherWidgetNameHelperClass), NULL, NULL,
            (GClassInitFunc) bob_launcher_widget_name_helper_class_init,
            NULL, NULL, sizeof(BobLauncherWidgetNameHelper), 0, NULL, NULL
        };
        GType id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherWidgetNameHelper", &info, 0);
        g_once_init_leave(&type_id, id);
    }
    return type_id;
}

static BobLauncherWidgetNameHelper *
bob_launcher_widget_name_helper_new(void)
{
    return (BobLauncherWidgetNameHelper *)g_object_new(bob_launcher_widget_name_helper_get_type(), NULL);
}
```

### 8. Type Registration (Main Class)

```c
static GType
bob_launcher_widget_name_get_type_once(void)
{
    static const GTypeInfo info = {
        sizeof(BobLauncherWidgetNameClass), NULL, NULL,
        (GClassInitFunc) bob_launcher_widget_name_class_init, NULL, NULL,
        sizeof(BobLauncherWidgetName), 0,
        (GInstanceInitFunc) bob_launcher_widget_name_instance_init, NULL
    };
    GType type_id = g_type_register_static(GTK_TYPE_WIDGET, "BobLauncherWidgetName", &info, 0);
    BobLauncherWidgetName_private_offset = g_type_add_instance_private(type_id, sizeof(BobLauncherWidgetNamePrivate));
    return type_id;
}

GType
bob_launcher_widget_name_get_type(void)
{
    static volatile gsize type_id__once = 0;
    if (g_once_init_enter(&type_id__once)) {
        GType type_id = bob_launcher_widget_name_get_type_once();
        g_once_init_leave(&type_id__once, type_id);
    }
    return type_id__once;
}
```

### 9. Implementation

```c
static void
bob_launcher_widget_name_class_init(BobLauncherWidgetNameClass *klass, gpointer klass_data)
{
    bob_launcher_widget_name_parent_class = g_type_class_peek_parent(klass);
    g_type_class_adjust_private_offset(klass, &BobLauncherWidgetName_private_offset);

    G_OBJECT_CLASS(klass)->finalize = bob_launcher_widget_name_finalize;
    GTK_WIDGET_CLASS(klass)->get_request_mode = bob_launcher_widget_name_get_request_mode;
    GTK_WIDGET_CLASS(klass)->measure = bob_launcher_widget_name_measure;
    GTK_WIDGET_CLASS(klass)->size_allocate = bob_launcher_widget_name_size_allocate;
    GTK_WIDGET_CLASS(klass)->snapshot = bob_launcher_widget_name_snapshot;
}

static void
bob_launcher_widget_name_instance_init(BobLauncherWidgetName *self, gpointer klass)
{
    self->priv = bob_launcher_widget_name_get_instance_private(self);
    gtk_widget_set_name(GTK_WIDGET(self), "widget-name");
    // initialization code
}

static void
bob_launcher_widget_name_finalize(GObject *object)
{
    BobLauncherWidgetName *self = BOB_LAUNCHER_WIDGET_NAME(object);

    GtkWidget *child;
    while ((child = gtk_widget_get_first_child(GTK_WIDGET(self))) != NULL)
        gtk_widget_unparent(child);

    g_clear_object(&self->priv->settings);
    G_OBJECT_CLASS(bob_launcher_widget_name_parent_class)->finalize(object);
}

BobLauncherWidgetName *
bob_launcher_widget_name_new(void)
{
    return (BobLauncherWidgetName *)g_object_new(bob_launcher_widget_name_get_type(), NULL);
}
```

## GTK4 Widget Virtual Functions

```c
static GtkSizeRequestMode
bob_launcher_widget_name_get_request_mode(GtkWidget *widget)
{
    return GTK_SIZE_REQUEST_CONSTANT_SIZE;
}

static void
bob_launcher_widget_name_measure(GtkWidget *widget, GtkOrientation orientation, gint for_size,
                                  gint *minimum, gint *natural, gint *minimum_baseline, gint *natural_baseline)
{
    *minimum_baseline = *natural_baseline = -1;
    *minimum = *natural = 0;
    // measurement logic
}

static void
bob_launcher_widget_name_size_allocate(GtkWidget *widget, gint width, gint height, gint baseline)
{
    GskTransform *transform = NULL;
    graphene_point_t offset = GRAPHENE_POINT_INIT(0, 0);

    // For each child:
    gtk_widget_allocate(child, width, child_height, baseline,
                        transform ? gsk_transform_ref(transform) : NULL);
    offset.y = child_height;
    transform = gsk_transform_translate(transform, &offset);

    gsk_transform_unref(transform);
}

static void
bob_launcher_widget_name_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    // For each child:
    gtk_widget_snapshot_child(widget, child, snapshot);
}
```

## Key Optimizations Over Valac Output

1. **No ref-counting in sorts** - Just swap pointers, don't ref/unref
2. **Stack-allocated graphene_point_t** - Use `GRAPHENE_POINT_INIT(0, 0)`, not heap allocation
3. **No temporary variables** - Access arrays directly
4. **Early exits** - Check for zero-size cases at function start
5. **Local variable caching** - Cache frequently accessed globals in local const variables

## Common Pitfalls

1. **"unknown type name" errors** - Missing `cheader_filename` on class in VAPI
2. **"incomplete type" errors** - Need full struct definition to access fields, not just typedef
3. **Missing parent_class** - Always store via `g_type_class_peek_parent(klass)`
4. **Transform leaks** - Always `gsk_transform_unref(transform)` at end of size_allocate
5. **GSettings in instance_init** - This works, but signal connection needs care with lifecycle

## Checklist

- [ ] Header has all public types, functions, externs
- [ ] VAPI has `cheader_filename` on each class
- [ ] C file defines full structs for types whose fields are accessed
- [ ] C file forward-declares pointer-only types
- [ ] Type registration uses `g_once_init_enter`/`g_once_init_leave` pattern
- [ ] Private offset handled with `g_type_class_adjust_private_offset`
- [ ] Finalize unparents all children
- [ ] Internal helper classes are file-static (not in header)
- [ ] No unnecessary ref-counting in hot paths
