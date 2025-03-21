# BobLauncher

This is a Wayland launcher inspired by the venerable [Synapse Launcher](https://launchpad.net/synapse-project).

A GPU-accelerated launcher using Gtk4 and Vala with performance critical parts written in pure C. BobLauncher aims to provide a useful, user friendly, beautiful, and fast launcher.

## Highlights

https://github.com/user-attachments/assets/f157d529-9b19-46ac-add1-992e1e476a67

- Beautiful design out of the box -- or hand roll your own CSS!
- Simple controls: there is only `bob-launcher`. Bind it to your preferred keybindings, launch it in background as a service with `bob-launcher --gapplication-service`, or launch plugins directly using the syntax `bob-launcher --select-plugin <plugin> [query]`.
- High quality plugins. Plugins have been specifically crafted for BobLauncher, many of which utilize multiprocessing with the sharded search.
- Amazing matching that just works™: It uses an adapted Levenshtein distance algorithm from [fzy](https://github.com/jhawthorn/fzy/blob/master/ALGORITHM.md). We aim to always provide the most relevant match.
- Optional layer shell integration

## Installation

### Building from Source

```bash
meson setup build
meson compile -C build
meson install -C build
```

### Dependencies

- GTK4
- GLib
- Wayland
- GTK4 Layer Shell

## Usage

BobLauncher is designed to be simple:

```bash
bob-launcher                                  # Invoke BobLauncher
bob-launcher --gapplication-service           # Start the app without launching the GUI
bob-launcher --select-plugin <plugin> [query] # Launch a specific plugin
```

## Plugins

You will need plugins to be able to do anything useful. Check out the available plugins at [GitHub](https://github.com/trbjo/bob-launcher-plugins)

## Performance

BobLauncher is built with performance as a primary goal. Performance-critical parts are written in C, so you should be able to query to your heart's content without stutter.

## Contributing

Contributions are welcome! We are still in beta, so there will be bugs.
