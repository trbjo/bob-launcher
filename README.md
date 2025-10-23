# BobLauncher

Alfred for Linux. But faster.

https://github.com/user-attachments/assets/f157d529-9b19-46ac-add1-992e1e476a67

Searches everything by default. No keywords needed, just type.


## Featured Plugins

- **Snippets** — text and images
- **Clipboard Manager** — full history, all MIME types
- **File Search** — live tracking, respects gitignore
- **Calendar** — agenda view, create/delete events
- **IWD WiFi Manager** — list and (dis)connect to networks
- **Systemd Service Manager** — start/stop services
- **Process Monitor** — like htop
- **Recent Files**
- **SSH Hosts**
- **Password Manager** — integrates with `pass`

And of course the basics such as calculator and applications.

## Usage

One binary does everything:

```bash
bob-launcher                                  # Launch or toggle visibility
bob-launcher --select-plugin <plugin> [query] # Invoke specific plugin
bob-launcher <file|uri>                       # Open files/URIs (xdg-open alternative)
```

Symlink to `bob-launcher.service` and it becomes a systemd service. Everything launched gets its own scope for process isolation. Styling is just CSS.

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

## Contributing

Still in beta. Contributions welcome.
