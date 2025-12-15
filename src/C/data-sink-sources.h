#ifndef bob_launcher_DATA_SINK_SOURCES_H
#define bob_launcher_DATA_SINK_SOURCES_H

#include <stdbool.h>
#include <hashset.h>

typedef struct _BobLauncherSearchBase BobLauncherSearchBase;

void data_sink_sources_execute_search(
    const char* q,
    BobLauncherSearchBase* selected_plg,
    int event_id,
    bool reset_index);

#endif /* bob_launcher_DATA_SINK_SOURCES_H */
