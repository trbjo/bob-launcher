namespace PluginLoader {
    private const string PLUGIN_INTERFACE_VERSION = "1.0";

    [CCode (has_target = false)]
    private delegate Type PluginInitFunc(TypeModule type_module);

    private static string[] plugin_dirs;
    private static GenericArray<PluginModule> type_modules;
    private static GLib.HashTable<ulong, string> handler_ids;
    private static BobLauncher.AppSettings.Plugins settings;

    internal static GenericArray<BobLauncher.SearchBase> search_providers;
    internal static GenericArray<BobLauncher.SearchBase> default_search_providers;
    internal static GenericArray<BobLauncher.PluginBase> loaded_plugins;
    internal static GenericArray<unowned BobLauncher.PluginBase> enabled_plugins;

    private class PluginModule : TypeModule {
        public string path { get; construct; }
        private Module? module = null;
        public Type plugin_type = Type.INVALID;

        internal PluginModule(string path) {
            Object(path: path);
            module = Module.open(path, ModuleFlags.LAZY | ModuleFlags.LOCAL);

            void* function;
            if (!module.symbol("plugin_init", out function)) {
                warning(@"Plugin '$path' doesn't have plugin_init symbol: $(Module.error())");
                return;
            }

            unowned PluginInitFunc init_func = (PluginInitFunc) function;

            plugin_type = init_func(this);
            if (plugin_type == Type.INVALID) {
                warning(@"Plugin '$path' returned invalid type");
            }
        }

        internal override bool load() {
            return plugin_type != Type.INVALID;
        }

        internal override void unload() {
            module = null;
        }
    }

    internal static void initialize() {
        settings = BobLauncher.AppSettings.get_default().plugins;

        unowned string[] system_data_dirs = Environment.get_system_data_dirs();
        string user_data_dir = Environment.get_user_data_dir();

        plugin_dirs = new string[system_data_dirs.length + 1];

        // Add user-specific path first
        string user_path = user_data_dir.replace("share", "lib");
        plugin_dirs[0] = Path.build_filename(user_path, BobLauncher.BOB_LAUNCHER_APP_ID);

        for (int i = 0; i < system_data_dirs.length; i++) {
            string lib_path = system_data_dirs[i].replace("share", "lib");
            plugin_dirs[i + 1] = Path.build_filename(lib_path, BobLauncher.BOB_LAUNCHER_APP_ID);
        }

        type_modules = new GenericArray<PluginModule>();
        search_providers = new GenericArray<BobLauncher.SearchBase>();
        default_search_providers = new GenericArray<BobLauncher.SearchBase>();
        loaded_plugins = new GenericArray<BobLauncher.PluginBase>();
        enabled_plugins = new GenericArray<unowned BobLauncher.PluginBase>();
        handler_ids = new GLib.HashTable<ulong, string>(direct_hash, direct_equal);

        if (!Module.supported()) {
            warning("Module loading is not supported on this system!");
            return;
        }

        foreach (string plugin_dir in plugin_dirs) {
            try {
                if (!FileUtils.test(plugin_dir, FileTest.IS_DIR)) {
                    continue;
                }

                Dir dir = Dir.open(plugin_dir);

                string? name = null;
                while ((name = dir.read_name()) != null) {
                    if (!name.has_suffix(".so")) continue;

                    string path = Path.build_filename(plugin_dir, name);
                    debug(@"Attempting to load plugin: $path");

                    var plugin_module = new PluginModule(path);
                    if (!plugin_module.load()) {
                        warning(@"Failed to load plugin '$name'");
                        continue;
                    }

                    type_modules.add(plugin_module);

                    var plugin = (BobLauncher.PluginBase)Object.new(plugin_module.plugin_type);
                    if (plugin == null) {
                        warning(@"Failed to create plugin instance");
                        continue;
                    }
                    loaded_plugins.add(plugin);
                }


            } catch (Error e) {
                warning(@"Failed to load plugins from $plugin_dir: $(e.message)");
                continue;
            }
        }

        foreach (unowned var plugin in loaded_plugins) {
            string plugin_name = plugin.to_string();
            debug(@"Plugin name resolved: $plugin_name");
            ulong handler_id = initialize_plugin(plugin, settings.plugins.get(plugin_name));
            handler_ids.set(handler_id, plugin_name);
        }

        foreach (unowned var plugin in loaded_plugins) {
            plugin.notify["enabled"].connect(on_plugin_enabled_changed_wrapper);
            plugin.notify["search-providers"].connect(on_plugin_search_providers_changed_wrapper);
        }

        foreach (unowned var loaded_plugin in loaded_plugins) {
            on_plugin_enabled_changed(loaded_plugin);
        }

        search_providers.sort(shard_comp);
        debug(@"Finished loading plugins. Loaded $(loaded_plugins.length) plugins total");
    }

    private static string get_plugin_summary(GLib.Settings settings) {
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

    public static ulong initialize_plugin(BobLauncher.PluginBase plg, GLib.Settings settings) {
        plg.title = get_plugin_summary(settings);
        plg.description = get_settings_description(settings);

        var schema_source = GLib.SettingsSchemaSource.get_default();
        var schema_id = settings.schema_id + ".custom-settings";

        settings.bind("bonus", plg, "bonus", SettingsBindFlags.GET);

        if (schema_source.lookup(schema_id, true) != null) {
            var custom_settings = settings.get_child("custom-settings");

            foreach (string key in custom_settings.settings_schema.list_keys()) {
                debug("key: %s, %s", key, plg.to_string());
                Variant value = custom_settings.get_value(key);
                plg.on_setting_changed(key, value);
            }

            custom_settings.changed.connect((_setting, key) => {
                Variant value = _setting.get_value(key);
                plg.on_setting_changed(key, value);
            });
        }

        handle_enabled(plg, settings);
        return settings.changed.connect((key) => settings_changed_handler(plg, settings, key));
    }

    private static void settings_changed_handler(BobLauncher.PluginBase plg, GLib.Settings settings, string key) {
        if (key == "enabled") {
            handle_enabled(plg, settings);
        } else {
            var value = settings.get_value(key);
            foreach (unowned var sp in plg.search_providers) {
                sp.handle_base_settings(key, value);
            }
        }
    }

    private static void handle_enabled(BobLauncher.PluginBase plg, GLib.Settings settings) {
        bool should_enable = settings.get_boolean("enabled");
        if (plg.enabled == should_enable) return;

        bool activated = should_enable && plg.activate();

        if (!activated) {
            plg.deactivate();
        }

        if (plg.enabled != activated) {
            plg.enabled = activated;
        }

        if (should_enable != activated) {
            settings.set_boolean("enabled", activated);
        }
    }

    private static void add_providers(BobLauncher.PluginBase plugin) {
        unowned GenericArray<BobLauncher.SearchBase> providers = plugin.search_providers;
        foreach (unowned var provider in providers) {
            search_providers.add(provider);
            provider.notify["shard-count"].connect(on_shard_count_changed);
            provider.notify["enabled-in-default-search"].connect(on_provider_default_search_changed);

            if (provider.enabled_in_default_search) {
                default_search_providers.add(provider);
            }

            debug(@"Added search provider: $(provider.get_title())");
        }
        search_providers.sort(shard_comp);

        string settings_id = BobLauncher.BOB_LAUNCHER_APP_ID + ".plugins." + plugin.to_string();
        var settings = new Settings(settings_id);
        foreach (string key in settings.settings_schema.list_keys()) {
            foreach (unowned var sp in plugin.search_providers) {
                sp.handle_base_settings(key, settings.get_value(key));
            }
        }
    }

    private static void remove_providers(GenericArray<BobLauncher.SearchBase> providers) {
        foreach (unowned var provider in providers) {
            default_search_providers.remove(provider);
            provider.notify["shard-count"].disconnect(on_shard_count_changed);
            provider.notify["enabled-in-default-search"].disconnect(on_provider_default_search_changed);

            search_providers.remove(provider);
            debug(@"Removed search provider: $(provider.get_title())");
        }
        var indices = new GenericArray<uint>();
        for (uint i = 0; i < search_providers.length; i++) {
            BobLauncher.SearchBase? sp = search_providers.get(i);
            if (sp == null) {
                indices.add(i);
                debug("plugin is null!");
            }
        }
        // Iterate backwards through indices to maintain correct positions
        for (int i = (int)indices.length - 1; i >= 0; i--) {
            uint index = indices.get(i);
            search_providers.remove_index(index);
        }
    }

    private static void on_provider_default_search_changed(GLib.Object obj, GLib.ParamSpec param) {
        unowned var provider = (BobLauncher.SearchBase)obj;
        if (provider.enabled_in_default_search) {
            if (!default_search_providers.find(provider)) {
                default_search_providers.add(provider);
                debug(@"Provider '$(provider.get_title())' enabled in default search");
            }
        } else {
            default_search_providers.remove(provider);
            debug(@"Provider '$(provider.get_title())' disabled from default search");
        }
    }

    private static void on_plugin_enabled_changed_wrapper(GLib.Object obj, GLib.ParamSpec param) {
        on_plugin_enabled_changed((BobLauncher.PluginBase)obj);
    }

    private static void on_plugin_enabled_changed(BobLauncher.PluginBase plugin) {
        if (plugin.enabled) {
            if (!enabled_plugins.find(plugin)) {
                enabled_plugins.add(plugin);
                debug(@"Plugin '$(plugin.get_title())' has been enabled");
                add_providers(plugin);
            }
        } else {
            enabled_plugins.remove(plugin);
            debug(@"Plugin '$(plugin.get_title())' has been disabled");
            remove_providers(plugin.search_providers);
        }
    }

    private static void on_plugin_search_providers_changed_wrapper(GLib.Object obj, GLib.ParamSpec param) {
        on_plugin_search_providers_changed(((BobLauncher.PluginBase)obj));
    }

    private static void on_plugin_search_providers_changed(BobLauncher.PluginBase plugin) {
        // Remove all existing providers from this plugin
        foreach (unowned var provider in search_providers) {
            bool found = false;
            foreach (unowned var p in plugin.search_providers) {
                if (p == provider) {
                    found = true;
                    break;
                }
            }
            if (found) {
                search_providers.remove(provider);
                default_search_providers.remove(provider);
                provider.notify["shard-count"].disconnect(on_shard_count_changed);
                provider.notify["enabled-in-default-search"].disconnect(on_provider_default_search_changed);
            }
        }

        if (!plugin.enabled) return;
        add_providers(plugin);

    }

    private static void on_shard_count_changed() {
        search_providers.sort(shard_comp);
    }

    public static int alpha_comp(BobLauncher.PluginBase a, BobLauncher.PluginBase b) {
        return strcmp(a.get_title(), b.get_title());
    }

    public static int shard_comp(BobLauncher.SearchBase? a, BobLauncher.SearchBase? b) {
        if (a == null && b == null) return 0;
        if (a == null) return -1;
        if (b == null) return 1;
        if (a.shard_count > b.shard_count) return 1;
        if (b.shard_count > a.shard_count) return -1;
        return 0;
    }

    internal static void shutdown() {
        debug("Shutting down plugin loader");
        handler_ids.foreach((key, value) => settings.plugins.get(value).disconnect(key));

        default_search_providers.remove_range(0, default_search_providers.length);
        enabled_plugins.remove_range(0, enabled_plugins.length);

        foreach (unowned var plugin in loaded_plugins) {
            plugin.notify["enabled"].disconnect(on_plugin_enabled_changed_wrapper);
            plugin.notify["search-providers"].disconnect(on_plugin_search_providers_changed_wrapper);
        }

        search_providers.remove_range(0, search_providers.length);

        foreach (unowned var plugin in loaded_plugins) {
            if (plugin.enabled) {
                plugin.deactivate();
            }
            plugin.dispose();
            plugin = null;
        }

        foreach (unowned var module in type_modules) {
            module.unload();
        }
    }
}
