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

typedef int16_t Score;
typedef struct _BobLauncherMatch BobLauncherMatch;
typedef struct _BobLauncherAction BobLauncherAction;
typedef struct _BobLauncherPluginBase BobLauncherPluginBase;

typedef struct {
    HashSet* hsh;
    BobLauncherMatch* m;
    bool query_empty;
    Score score_threshold;
    needle_info* si;
    ResultContainer* rc;
} ActionSetInternal;

extern Score bob_launcher_action_get_relevancy(BobLauncherAction* action, BobLauncherMatch* match);
extern char* bob_launcher_match_get_title(BobLauncherMatch* match);
extern char* bob_launcher_match_get_description(BobLauncherMatch* match);
extern GPtrArray* plugin_loader_loaded_plugins;
extern gboolean bob_launcher_plugin_base_is_enabled (BobLauncherPluginBase* self);
extern void bob_launcher_plugin_base_find_for_match (BobLauncherPluginBase* self,
                                          BobLauncherMatch* match,
                                          ActionSetInternal* rs);


ActionSetInternal* action_set_new(BobLauncherMatch* m, const char* query, int current_event, Score score_threshold) {
    ActionSetInternal* self = malloc(sizeof(ActionSetInternal));
    if (!self) return NULL;

    self->m = m;
    self->query_empty = g_utf8_strlen(query, -1) == 0;
    self->si = prepare_needle(query);
    self->score_threshold = score_threshold;
    self->hsh = hashset_create(query, current_event);
    self->rc = hashset_create_handle(self->hsh);

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

static BobLauncherMatch* create_match_from_action(BobLauncherAction* action) {
    return g_object_ref((BobLauncherMatch*)action); /* Actions are also Matches in this API */
}

void action_set_add_action(ActionSet* _self, BobLauncherAction* action) {
    ActionSetInternal* self = (ActionSetInternal*)_self;

    if (!self || !action) return;

    Score basic_relevancy = bob_launcher_action_get_relevancy(action, self->m);

    if (basic_relevancy < self->score_threshold) {
        return;
    }

    uint64_t identity = (uint64_t)(uintptr_t)action;

    if (self->query_empty) {
        result_container_add_lazy_unique(self->rc, identity, basic_relevancy,
                                         (MatchFactory)create_match_from_action,
                                         action, NULL);
    } else {
        char* title = bob_launcher_match_get_title((BobLauncherMatch*)action);
        if (title && query_has_match(self->si, title)) {
            basic_relevancy = match_score(self->si, title);
            result_container_add_lazy_unique(self->rc, identity, basic_relevancy,
                                             (MatchFactory)create_match_from_action,
                                             action, NULL);
        } else {
            char* desc = bob_launcher_match_get_description((BobLauncherMatch*)action);
            if (desc && query_has_match(self->si, desc)) {
                basic_relevancy = match_score(self->si, desc);
                result_container_add_lazy_unique(self->rc, identity, basic_relevancy,
                                                 (MatchFactory)create_match_from_action,
                                                 action, NULL);
            }
        }
    }
}

HashSet* data_sink_search_for_actions(const char* query, BobLauncherMatch* m, int event_id) {
    if (!query || !m) return NULL;

    ActionSetInternal* rs = action_set_new(m, query, event_id, MATCH_SCORE_THRESHOLD);
    if (!rs) return NULL;

    /* Process all plugins */
    for (guint i = 0; i < plugin_loader_loaded_plugins->len; i++) {
        BobLauncherPluginBase* plg = g_ptr_array_index(plugin_loader_loaded_plugins, i);
        if (bob_launcher_plugin_base_is_enabled(plg)) {
            bob_launcher_plugin_base_find_for_match(plg, m, (ActionSetInternal*)rs);
        }
    }

    HashSet* result = action_set_to_hashset(rs);
    action_set_free(rs);
    return result;
}
