#pragma once

#include <stdbool.h>
#include <hashset.h>

typedef struct _BobLauncherSearchBase BobLauncherSearchBase;

void data_sink_sources_execute_search(
    const char* query,
    BobLauncherSearchBase* selected_plg,
    const int event_id,
    const bool reset_index);
