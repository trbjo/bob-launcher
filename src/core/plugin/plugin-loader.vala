namespace PluginLoader {
    private const string PLUGIN_INTERFACE_VERSION = "1.0";

    [CCode (has_target = false)]
    private delegate Type PluginInitFunc(TypeModule type_module);

    private static string[] plugin_dirs;
    private static GenericArray<unowned Module> loaded_modules;
    private static GenericArray<TypeModule> type_modules;
    private static GLib.HashTable<ulong, string> handler_ids;
    private static BobLauncher.AppSettings.Plugins settings;

    internal static GenericArray<BobLauncher.SearchBase> search_providers;
    internal static GenericArray<BobLauncher.PluginBase> loaded_plugins;

    private class PluginModule : TypeModule {
        public string path { get; construct; }
        private Module module;

        internal PluginModule(string path) {
            Object(path: path);
        }

        internal override bool load() {
            module = Module.open(path, ModuleFlags.LAZY | ModuleFlags.LOCAL);
            if (module == null) {
                warning("Failed to load module: %s", Module.error());
                return false;
            }
            return true;
        }

        internal override void unload() {
            module = null;
        }

        internal unowned Module? get_module() {
            return module;
        }
    }

    internal static void initialize() {
        settings = BobLauncher.AppSettings.get_default().plugins;

        // Get XDG data directories
        unowned string[] system_data_dirs = Environment.get_system_data_dirs();
        string user_data_dir = Environment.get_user_data_dir();

        // Initialize array with space for all paths
        plugin_dirs = new string[system_data_dirs.length + 1];

        // Add user-specific path first
        string user_path = user_data_dir.replace("share", "lib");
        plugin_dirs[0] = Path.build_filename(user_path, BobLauncher.BOB_LAUNCHER_APP_ID);
        debug(@"User plugin path: $(plugin_dirs[0])");

        // Add system paths
        for (int i = 0; i < system_data_dirs.length; i++) {
            string lib_path = system_data_dirs[i].replace("share", "lib");
            plugin_dirs[i + 1] = Path.build_filename(lib_path, BobLauncher.BOB_LAUNCHER_APP_ID);
            debug(@"System plugin path: $(plugin_dirs[i + 1])");
        }

        loaded_modules = new GenericArray<unowned Module>();
        type_modules = new GenericArray<TypeModule>();
        search_providers = new GenericArray<BobLauncher.SearchBase>();
        loaded_plugins = new GenericArray<BobLauncher.PluginBase>();
        handler_ids = new GLib.HashTable<ulong, string>(direct_hash, direct_equal);

        debug("Starting plugin loading process");

        if (!Module.supported()) {
            warning("Module loading is not supported on this system!");
            return;
        }

        foreach (string plugin_dir in plugin_dirs) {
            try {
                if (!FileUtils.test(plugin_dir, FileTest.IS_DIR)) {
                    debug(@"Skipping non-existent directory: $plugin_dir");
                    continue;
                }

                Dir dir = Dir.open(plugin_dir);
                debug(@"Scanning directory: $plugin_dir");

                string? name = null;
                while ((name = dir.read_name()) != null) {
                    if (!name.has_suffix(".so")) continue;

                    string path = Path.build_filename(plugin_dir, name);
                    debug(@"Attempting to load plugin: $path");

                    // Create a TypeModule for this plugin
                    var plugin_module = new PluginModule(path);
                    if (!plugin_module.load()) {
                        warning(@"Failed to load plugin '$name'");
                        continue;
                    }

                    type_modules.add(plugin_module);

                    unowned Module module = plugin_module.get_module();
                    if (module == null) {
                        warning(@"Failed to get module for plugin '$name'");
                        continue;
                    }

                    void* function;
                    if (!module.symbol("plugin_init", out function)) {
                        warning(@"Plugin '$name' doesn't have plugin_init symbol: $(Module.error())");
                        continue;
                    }

                    debug(@"Found plugin_init symbol in $name");

                    unowned PluginInitFunc init_func = (PluginInitFunc) function;

                    Type plugin_type = init_func(plugin_module);
                    if (plugin_type == Type.INVALID) {
                        warning(@"Plugin '$name' returned invalid type");
                        continue;
                    }

                    debug(@"Registered type: $(plugin_type.name())");

                    var plugin = (BobLauncher.PluginBase)Object.new(plugin_type);
                    if (plugin == null) {
                        warning(@"Failed to create plugin instance");
                        continue;
                    }

                    string plugin_name = plugin.to_string();
                    debug(@"Plugin name resolved: $plugin_name");

                    ulong handler_id = plugin.initialize(settings.plugins.get(plugin_name));
                    handler_ids.set(handler_id, plugin_name);
                    debug(@"Plugin '$plugin_name' initialized with settings");

                    if (plugin is BobLauncher.SearchBase) {
                        var search_provider = (BobLauncher.SearchBase)plugin;
                        search_providers.add(search_provider);
                        debug(@"Added search provider: $((search_provider).get_title())");
                    }

                    loaded_modules.add(module);
                    loaded_plugins.add(plugin);
                    debug(@"Successfully loaded plugin: $plugin_name");
                }
                search_providers.sort(alpha_comp);
            } catch (Error e) {
                warning(@"Failed to load plugins from $plugin_dir: $(e.message)");
                continue;
            }
        }
        debug(@"Finished loading plugins. Loaded $(loaded_plugins.length) plugins total");
    }

    public static int alpha_comp(BobLauncher.PluginBase a, BobLauncher.PluginBase b) {
        return strcmp(a.get_title(), b.get_title());
    }

    internal static void shutdown() {
        debug("Shutting down plugin loader");
        handler_ids.foreach((key, value) => settings.plugins.get(value).disconnect(key));

        search_providers.remove_range(0, search_providers.length);
        search_providers = null;

        foreach (var plugin in loaded_plugins) {
            plugin.shutdown();
            plugin.dispose();
        }

        loaded_plugins.remove_range(0, loaded_plugins.length);
        loaded_plugins = null;

        foreach (var module in type_modules) {
            module.unload();
        }
    }
}
