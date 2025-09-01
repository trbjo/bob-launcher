#ifndef ACCESS_APPINFO_PROPS_H
#define ACCESS_APPINFO_PROPS_H

#include <gio/gio.h>

char *g_desktop_app_info_get_string_from_group (GDesktopAppInfo *info,
                                                const char      *group_name,
                                                const char      *key);

#endif
