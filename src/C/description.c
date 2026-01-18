#include "description.h"
#include <stdlib.h>
#include <string.h>

static void
ensure_capacity(Description *self)
{
    if (self->count >= self->capacity) {
        int new_cap = self->capacity == 0 ? 4 : self->capacity * 2;
        self->types = realloc(self->types, new_cap * sizeof(DescType));
        self->members = realloc(self->members, new_cap * sizeof(void *));
        self->capacity = new_cap;
    }
}

Description*
description_new_container(const char *css_class, ClickFunc func, void *target, void* nop)
{
    Description *self = calloc(1, sizeof(Description));
    self->css_class = css_class ? strdup(css_class) : NULL;
    self->click_func = func;
    self->click_target = target;
    self->types = NULL;
    self->members = NULL;
    self->count = 0;
    self->capacity = 0;
    return self;
}

TextDesc*
description_new_text(const char *text, const char *css_class, PangoAttrList *attrs, ClickFunc func, void *target, void* nop)
{
    TextDesc *self = calloc(1, sizeof(TextDesc));
    self->text = text ? strdup(text) : NULL;
    self->css_class = css_class ? strdup(css_class) : NULL;
    self->attrs = attrs ? pango_attr_list_ref(attrs) : NULL;
    self->click_func = func;
    self->click_target = target;
    return self;
}

ImageDesc*
description_new_image(const char *icon_name, const char *css_class, ClickFunc func, void *target, void* nop)
{
    ImageDesc *self = calloc(1, sizeof(ImageDesc));
    self->icon_name = icon_name ? strdup(icon_name) : NULL;
    self->css_class = css_class ? strdup(css_class) : NULL;
    self->click_func = func;
    self->click_target = target;
    return self;
}

void
description_add_text(Description *self, TextDesc *child)
{
    ensure_capacity(self);
    self->types[self->count] = DESC_TEXT;
    self->members[self->count] = child;
    self->count++;
}

void
description_add_image(Description *self, ImageDesc *child)
{
    ensure_capacity(self);
    self->types[self->count] = DESC_IMAGE;
    self->members[self->count] = child;
    self->count++;
}

void
description_add_container(Description *self, Description *child)
{
    ensure_capacity(self);
    self->types[self->count] = DESC_CONTAINER;
    self->members[self->count] = child;
    self->count++;
}

static void
text_desc_free(TextDesc *self)
{
    if (self == NULL) return;
    free(self->text);
    free(self->css_class);
    if (self->attrs) pango_attr_list_unref(self->attrs);
    free(self);
}

static void
image_desc_free(ImageDesc *self)
{
    if (self == NULL) return;
    free(self->icon_name);
    free(self->css_class);
    free(self);
}

void
description_free(Description *self)
{
    if (self == NULL) return;

    for (int i = 0; i < self->count; i++) {
        switch (self->types[i]) {
        case DESC_TEXT:
            text_desc_free(self->members[i]);
            break;
        case DESC_IMAGE:
            image_desc_free(self->members[i]);
            break;
        case DESC_CONTAINER:
            description_free(self->members[i]);
            break;
        }
    }

    free(self->css_class);
    free(self->types);
    free(self->members);
    free(self);
}
