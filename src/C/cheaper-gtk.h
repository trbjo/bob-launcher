#pragma once

#include <gtk/gtk.h>
#undef GTK_WIDGET
#define GTK_WIDGET(obj) ((GtkWidget*)obj)
