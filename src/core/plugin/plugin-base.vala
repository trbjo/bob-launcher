namespace BobLauncher {
    public delegate void SettingsCallback(Cancellable cancellable);

    public abstract class PluginBase : Match {
        private bool working = false;
        private Mutex work_mutex;
        private Mutex state_mutex;
        private Cancellable current_cancellable;
        public double bonus { get; set; }

        construct {
            work_mutex = Mutex();
            state_mutex = Mutex();
            current_cancellable = new Cancellable();
        }

        private bool _is_enabled = false;
        public bool is_enabled() {
            state_mutex.lock();
            bool is_enabled = _is_enabled && !working;
            state_mutex.unlock();
            return is_enabled;
        }

        public void shutdown() {
            current_cancellable.cancel();
            work_mutex.lock();
            deactivate();
            work_mutex.unlock();
        }

        public virtual ulong initialize(GLib.Settings settings) {
            this.title = get_summary(settings);
            this.description = get_settings_description(settings);

            var schema_source = GLib.SettingsSchemaSource.get_default();
            var schema_id = settings.schema_id + ".settings";

            if (schema_source.lookup(schema_id, true) != null) {
                var specific_settings = settings.get_child("settings");
                specific_settings.bind("bonus", this, "bonus", SettingsBindFlags.GET);
                Threads.run(() => {
                    work();
                    handle_specific_settings(specific_settings, on_set_init_delegate);

                    bool enabled = settings.get_boolean("enabled");
                    bool activated = enabled;

                    if (enabled) {
                        activated = activate(current_cancellable);
                    }

                    if (enabled && !activated) {
                        deactivate();
                    }

                    _is_enabled = activated;
                    if (enabled != activated) {
                        settings.set_boolean("enabled", activated);
                    }

                    relax();
                });


                settings_handler_id = specific_settings.changed.connect((key) => {
                    current_cancellable.cancel();
                    current_cancellable = new Cancellable();
                    Threads.run(() => {
                        work();
                        if (!handle_base_settings(specific_settings, key)) {
                            Variant value = specific_settings.get_value(key);
                            on_set_change_delegate(key, value);
                        }
                        relax();
                    });
                });
            } else {
                work();
                bool enabled = settings.get_boolean("enabled");
                bool activated = enabled;

                if (enabled) {
                    activated = activate(current_cancellable);
                }

                if (enabled && !activated) {
                    deactivate();
                }

                _is_enabled = activated;
                if (enabled != activated) {
                    settings.set_boolean("enabled", activated);
                }

                relax();
            }



            return settings.changed.connect((key) => {
                current_cancellable.cancel();
                current_cancellable = new Cancellable();
                Threads.run(() => {
                    work();

                    bool enabled = settings.get_boolean("enabled");
                    bool activated = enabled;

                    if (enabled) {
                        activated = activate(current_cancellable);
                    }

                    if (enabled && !activated) {
                        deactivate();
                    }

                    _is_enabled = activated;
                    if (enabled != activated) {
                        settings.set_boolean("enabled", activated);
                    }

                    relax();
                });
            });
        }

        ulong settings_handler_id;

        private delegate void SetChange(string k, Variant value);


        private void on_set_init_delegate(string k, Variant value) {
            this.on_setting_initialized(k, value);
        }

        private void on_set_change_delegate(string k, Variant value) {
            var cb = this.on_setting_changed(k, value);
            if (cb != null) {
                cb(current_cancellable);
            }
        }

        private void work() {
            state_mutex.lock();
            working = true;
            state_mutex.unlock();
            work_mutex.lock();
        }

        private void relax() {
            state_mutex.lock();
            working = false;
            state_mutex.unlock();
            work_mutex.unlock();
        }

        private void handle_specific_settings(GLib.Settings specific_settings, SetChange func) {
            foreach (string key in specific_settings.settings_schema.list_keys()) {
                if (!handle_base_settings(specific_settings, key)) {
                    Variant value = specific_settings.get_value(key);
                    func(key, value);
                }
            }
        }

        protected virtual bool activate(Cancellable cancellable) {
            return true;
        }

        protected virtual void deactivate() {
            // Default implementation does nothing
        }

        public delegate void SettingsCallback(Cancellable cancellable);

        protected virtual SettingsCallback? on_setting_changed(string key, GLib.Variant value) {
            error("Handling setting: '%s' for plugin: %s, but plugin does not override `on_setting_changed`", key, this.title);
        }

        public virtual void on_setting_initialized(string key, GLib.Variant value) {
            error("Handling setting: '%s' for plugin: %s, but plugin does not override `on_setting_initialized`", key, this.title);
        }

        protected virtual bool handle_base_settings(GLib.Settings settings, string key) {
            return false;
        }

        protected string title;
        public override string get_title() {
            return this.title;
        }

        protected string description;
        public override string get_description() {
            return this.description;
        }

        protected string icon_name;
        public override string get_icon_name() {
            return icon_name;
        }

        public string get_mime_type() {
            return "application-x-executable";
        }

        private string _cached_string;

        public virtual string to_string() {
            if (_cached_string == null) {
                string name = get_type().name().replace("BobLauncher", "").replace("Plugin", "");
                StringBuilder result = new StringBuilder();

                for (int i = 0; i < name.length; i++) {
                    unichar c = name[i];
                    if (i > 0 && c.isupper()) {
                        result.append_c('-');
                    }
                    result.append_unichar(c.tolower());
                }

                _cached_string = result.str;
            }
            return _cached_string;
        }

        private static string get_summary(GLib.Settings settings) {
            var schema = settings.settings_schema;
            if (schema != null) {
                var key = schema.get_key("enabled");
                if (key != null) {
                    return key.get_summary() ?? "No Title provided";
                }
            }
            return "No Title provided";
        }

        private static string get_settings_description(GLib.Settings settings) {
            var schema = settings.settings_schema;
            if (schema != null) {
                var key = schema.get_key("enabled");
                if (key != null) {
                    return key.get_description() ?? "No description provided";
                }
            }
            return "No description provided";
        }

        public virtual void find_for_match(Match match, ActionSet rs) { }
    }

    public abstract class SearchBase : PluginBase {
        public virtual uint shard_count { get; set; default = 1; }
        public uint update_interval { get; set; }
        public bool enabled_in_default_search { get; set; }
        public uint char_threshold { get; set; }

        protected override bool handle_base_settings(GLib.Settings settings, string key) {
            switch (key) {
            case "char-threshold":
                this.char_threshold = settings.get_uint("char-threshold");
                return true;
            case "bonus":
                this.bonus = settings.get_double("bonus");
                return true;
            case "enabled-in-default":
                this.enabled_in_default_search = settings.get_boolean("enabled-in-default");
                return true;
            case "update-interval":
                uint user_value = settings.get_uint("update-interval");
                update_interval = ((user_value == 0) ? 0 : uint.max(500, user_value) * 1000);
                return true;
            default:
                return false;
            }
        }

        protected virtual void search(ResultContainer rs) {
            error("Plugin has not implemented search");
        }

        public virtual void search_shard(ResultContainer rs, uint shard_id) {
            if (shard_id > 0) {
                error("Plugin doesn't support sharding");
            }
            search(rs);
        }
    }

    public abstract class SearchAction : SearchBase {
        protected override bool handle_base_settings(GLib.Settings settings, string key) {
            // First try search settings
            if (base.handle_base_settings(settings, key)) {
                return true;
            }
            // No additional settings yet for SearchAction
            return false;
        }
    }
}
