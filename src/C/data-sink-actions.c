#include "data-sink-actions.h"
#include "constants.h"
#include <glib-object.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <glib.h>
#include <hashset.h>
#include <match.h>
#include <result-container.h>
#include <float.h>
#include <math.h>

#include "constants.h"

typedef int16_t Score;
typedef struct _BobLauncherMatch BobLauncherMatch;
typedef struct _BobLauncherAction BobLauncherAction;
typedef struct _BobLauncherPluginBase BobLauncherPluginBase;

typedef struct {
    HashSet* hsh;
    BobLauncherMatch* m;
    bool query_empty;
    needle_info* si;
    ResultContainer* rc;
} ActionSetInternal;

extern Score bob_launcher_action_get_relevancy(BobLauncherAction* action, BobLauncherMatch* match);
extern char* bob_launcher_match_get_title(BobLauncherMatch* match);
extern char* bob_launcher_match_get_description(BobLauncherMatch* match);
extern GPtrArray* plugin_loader_loaded_plugins;
extern gboolean bob_launcher_plugin_base_get_enabled (BobLauncherPluginBase* self);
extern void bob_launcher_plugin_base_find_for_match (BobLauncherPluginBase* self,
                                          BobLauncherMatch* match,
                                          ActionSetInternal* rs);


ActionSetInternal* action_set_new(BobLauncherMatch* m, const char* query, int current_event) {
    ActionSetInternal* self = malloc(sizeof(ActionSetInternal));
    if (!self) return NULL;

    self->m = m;
    self->query_empty = g_utf8_strlen(query, -1) == 0;
    self->si = prepare_needle(query);
    self->hsh = hashset_create(current_event);
    self->rc = hashset_create_default_handle(self->hsh, query);

    return self;
}

/* Clean function to free an ActionSetInternal */
void action_set_free(ActionSetInternal* self) {
    if (!self) return;

    free_string_info(self->si);
    /* ResultContainer is freed by HashSet */
    /* BobLauncherMatch is owned elsewhere */
    free(self);
}

HashSet* action_set_to_hashset(ActionSetInternal* self) {
    if (!self) return NULL;

    hashset_merge(self->hsh, self->rc);
    hashset_prepare(self->hsh);
    return self->hsh;
}

typedef BobLauncherMatch* (*MatchFactory)(void* user_data);

void action_set_add_action(ActionSet* _self, BobLauncherAction* action) {
    ActionSetInternal* self = (ActionSetInternal*)_self;

    if (!self || !action) return;

    Score score = bob_launcher_action_get_relevancy(action, self->m);
    if (score < SCORE_THRESHOLD) return;

    if (self->query_empty) {
        result_container_add_lazy_unique(self->rc, score,
                                         (MatchFactory)g_object_ref,
                                         g_object_ref(action), g_object_unref);
    } else if ((score = match_score(self->si, bob_launcher_match_get_title((BobLauncherMatch*)action))) >= SCORE_THRESHOLD) {
        result_container_add_lazy_unique(self->rc, score,
                                         (MatchFactory)g_object_ref,
                                         g_object_ref(action), g_object_unref);
    } else if ((score = match_score(self->si, bob_launcher_match_get_description((BobLauncherMatch*)action))) >= SCORE_THRESHOLD) {
        score = score >> 1;
        result_container_add_lazy_unique(self->rc, score,
                                         (MatchFactory)g_object_ref,
                                         g_object_ref(action), g_object_unref);
    }
}

HashSet* data_sink_search_for_actions(const char* query, BobLauncherMatch* m, int event_id) {
    if (!query || !m) return NULL;

    ActionSetInternal* rs = action_set_new(m, query, event_id);
    if (!rs) return NULL;

    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        BobLauncherPluginBase* plg = g_ptr_array_index(plugin_loader_loaded_plugins, i);
        if (bob_launcher_plugin_base_get_enabled(plg)) {
            bob_launcher_plugin_base_find_for_match(plg, m, (ActionSetInternal*)rs);
        }
    }

    HashSet* result = action_set_to_hashset(rs);
    action_set_free(rs);
    return result;
}
