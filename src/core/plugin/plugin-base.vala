namespace BobLauncher {
    public abstract class PluginBase : Match {
        internal MatchFactory make_match() {
            return () => this;
        }

        public int16 bonus { get; set; }

        private int _is_enabled;

        private ulong worker_id = 0;

        public bool is_enabled() {
            return Atomics.load(ref _is_enabled) == 1 && AtomicUlong.load(ref worker_id) == 0;
        }

        private void wait() {
            while (AtomicUlong.load(ref worker_id) != 0) {
                Threads.pause();
            }
        }

        public void shutdown() {
            if (!is_enabled()) return;
            wait();
            deactivate();
        }

        public virtual ulong initialize(GLib.Settings settings) {
            this.title = get_summary(settings);
            this.description = get_settings_description(settings);

            var schema_source = GLib.SettingsSchemaSource.get_default();
            var schema_id = settings.schema_id + ".settings";

            if (schema_source.lookup(schema_id, true) != null) {
                var specific_settings = settings.get_child("settings");
                specific_settings.bind("bonus", this, "bonus", SettingsBindFlags.GET);
                wait();
                ulong _worker_id = Threads.spawn_joinable(() => {
                    handle_specific_settings(specific_settings);

                    bool enabled = settings.get_boolean("enabled");
                    bool activated = enabled;

                    if (enabled) {
                        activated = activate();
                    }

                    if (enabled && !activated) {
                        deactivate();
                    }

                    Atomics.store(ref _is_enabled, (int)activated);

                    if (enabled != activated) {
                        settings.set_boolean("enabled", activated);
                    }

                    AtomicUlong.store(ref worker_id, 0);
                });
                AtomicUlong.store(ref worker_id, _worker_id);

                specific_settings.changed.connect((key) => {
                    wait();
                    ulong new_worker = Threads.spawn_joinable(() => {
                        if (!handle_base_settings(specific_settings, key)) {
                            Variant value = specific_settings.get_value(key);
                            on_setting_changed(key, value);
                        }
                        AtomicUlong.store(ref worker_id, 0);
                    });
                    AtomicUlong.store(ref worker_id, new_worker);
                });
            } else {
                wait();

                bool enabled = settings.get_boolean("enabled");
                bool activated = enabled;

                if (enabled) {
                    activated = activate();
                }

                if (enabled && !activated) {
                    deactivate();
                }

                Atomics.store(ref _is_enabled, (int)activated);

                if (enabled != activated) {
                    settings.set_boolean("enabled", activated);
                }
            }

            return settings.changed.connect((key) => {
                wait();
                ulong _worker_id = Threads.spawn_joinable(() => {
                    bool enabled = settings.get_boolean("enabled");
                    bool activated = enabled;

                    if (enabled) {
                        activated = activate();
                    }

                    if (enabled && !activated) {
                        deactivate();
                    }

                    Atomics.store(ref _is_enabled, (int)activated);
                    if (enabled != activated) {
                        settings.set_boolean("enabled", activated);
                    }

                    AtomicUlong.store(ref worker_id, 0);
                });
                AtomicUlong.store(ref worker_id, _worker_id);
            });
        }

        private void handle_specific_settings(GLib.Settings specific_settings) {
            foreach (string key in specific_settings.settings_schema.list_keys()) {
                if (!handle_base_settings(specific_settings, key)) {
                    Variant value = specific_settings.get_value(key);
                    on_setting_changed(key, value);
                }
            }
        }

        protected virtual bool activate() {
            return true;
        }

        protected virtual void deactivate() {
            // Default implementation does nothing
        }

        public virtual void on_setting_changed(string key, GLib.Variant value) {
            error("Handling setting: '%s' for plugin: %s, but plugin does not override `on_setting_changed`", key, this.title);
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
}
