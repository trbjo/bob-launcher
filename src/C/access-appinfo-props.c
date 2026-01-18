#include <glib-object.h>
#include <glib.h>
#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>

// Forward declare the private struct based on the source code you showed
struct _GDesktopAppInfo
{
  GObject parent_instance;

  char *desktop_id;
  char *filename;
  char *app_id;

  GKeyFile *keyfile;

  // We don't need the rest of the fields for our purpose
};

char *
g_desktop_app_info_get_string_from_group (GDesktopAppInfo *info,
                                          const char      *group_name,
                                          const char      *key)
{
  g_return_val_if_fail (G_IS_DESKTOP_APP_INFO (info), NULL);
  g_return_val_if_fail (group_name != NULL, NULL);
  g_return_val_if_fail (key != NULL, NULL);

  // Access the keyfile field directly from the struct
  if (info->keyfile == NULL)
    return NULL;

  return g_key_file_get_string (info->keyfile, group_name, key, NULL);
}
