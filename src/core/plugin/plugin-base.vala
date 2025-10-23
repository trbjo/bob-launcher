namespace BobLauncher {
    public abstract class PluginBase : Match {
        public int16 bonus { get; set; }

        private int _is_enabled = 0;

        public bool enabled {
            get {
                return Atomics.load(ref _is_enabled) == 1;
            }
            set {
                Atomics.store(ref _is_enabled, (int)value);
            }
        }

        private GenericArray<SearchBase> _search_providers;

        public GenericArray<SearchBase> search_providers {
            public get {
                if (_search_providers == null) {
                    _search_providers = new GenericArray<SearchBase>();
                    if (this is SearchBase) {
                        _search_providers.add((SearchBase)this);
                    }
                }
                return _search_providers;
            }
            protected set {
                _search_providers = value;
            }
        }

        public virtual bool activate() {
            return true;
        }

        public virtual void deactivate() {
            // Default implementation does nothing
        }

        public virtual void on_setting_changed(string key, GLib.Variant value) {
            error("Handling setting: '%s' for plugin: %s, but plugin does not override `on_setting_changed`", key, this.title);
        }

        internal string title;
        public override string get_title() {
            return this.title;
        }

        internal string description;
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

        public virtual void find_for_match(Match match, ActionSet rs) { }
    }
}
