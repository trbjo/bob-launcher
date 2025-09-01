[CCode (cheader_filename = "dbus-scope-monitor.h", lower_case_cprefix = "dbus_scope_")]
namespace DBusScope {
    [CCode (cname = "DBusScopeEventType", cprefix = "DBUS_SCOPE_EVENT_", has_type_id = false)]
    public enum EventType {
        CONNECTED,
        DISCONNECTED
    }

    [CCode (cname = "DBusScopeCallback", has_target = true, delegate_target = true, has_type_id=false)]
    public delegate void Callback (EventType event_type, string dbus_name, string? object_path);

    [CCode (cname = "DBusScopeMonitor", free_function = "dbus_scope_monitor_free")]
    [Compact]
    public class Monitor {
        [CCode (cname = "dbus_scope_monitor_new", has_type_id=false)]
        public Monitor (Callback callback);

        [CCode (cname = "dbus_scope_monitor_start")]
        public int start ();

        [CCode (cname = "dbus_scope_monitor_stop")]
        public void stop ();
    }
}
