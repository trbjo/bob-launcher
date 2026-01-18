#ifndef BOB_LAUNCHER_DESCRIPTION_H
#define BOB_LAUNCHER_DESCRIPTION_H

#include <stdint.h>
#include <pango/pango.h>
#include <glib.h>

typedef uint8_t DescType;
enum {
    DESC_TEXT,
    DESC_IMAGE,
    DESC_CONTAINER
};

typedef void (*ClickFunc)(void *target, GError **error);

typedef struct {
    char *text;
    char *css_class;
    PangoAttrList *attrs;
    ClickFunc click_func;
    void *click_target;
} TextDesc;

typedef struct {
    char *icon_name;
    char *css_class;
    ClickFunc click_func;
    void *click_target;
} ImageDesc;

typedef struct Description Description;

struct Description {
    char *css_class;
    ClickFunc click_func;
    void *click_target;
    DescType *types;
    void **members;
    int count;
    int capacity;
};

Description* description_new_container(const char *css_class, ClickFunc func, void *target, void* nop);
TextDesc* description_new_text(const char *text, const char *css_class, PangoAttrList *attrs, ClickFunc func, void *target, void* nop);

ImageDesc* description_new_image(const char *icon_name, const char *css_class, ClickFunc func, void *target, void* nop);

void description_add_text(Description *self, TextDesc *child);
void description_add_image(Description *self, ImageDesc *child);
void description_add_container(Description *self, Description *child);

void description_free(Description *self);

#endif /* BOB_LAUNCHER_DESCRIPTION_H */
