# Build & Install

A small D-Bus listener that watches for `org.freedesktop.Notifications.Notify`
calls, filters by app name + body, and appends matches to a plain-text log
file in a human-readable `[timestamp] summary: body` format. Runs as a
persistent systemd user service so it survives logouts/reboots.

## 1. Dependencies

Debian/Ubuntu:
```bash
sudo apt install build-essential pkg-config libdbus-1-dev
```

Fedora:
```bash
sudo dnf install gcc pkgconf-pkg-config dbus-devel
```

Arch:
```bash
sudo pacman -S base-devel dbus
```

## 2. Build

```bash
make
```

Or manually:
```bash
gcc -O2 -Wall -Wextra -o build/notify_logger logger.c $(pkg-config --cflags --libs dbus-1)
```

Test it manually first (Ctrl+C to stop):
```bash
./notify_logger --app whatsapp-linux-app --body "hey, are you around?" --out ./matches.log
```
Send yourself a test notification, or wait for a real one, and confirm a line
appears in `matches.log` and on stdout, e.g.:
```
[2026-07-08 14:32:10] Name: hey, are you around?
```

If nothing shows up, see **Troubleshooting** below (`eavesdrop` policy).

## 3. Install as a persistent user service (systemd)

```bash
make install
mkdir -p ~/notify_logger ~/.config/systemd/user
cp logger.service ~/.config/systemd/user/notify-logger.service

systemctl --user daemon-reload
systemctl --user enable --now notify-logger.service
```

This makes it:
- **Start automatically** on login (`WantedBy=default.target`)
- **Restart automatically** if it crashes (`Restart=on-failure`)
- **Log matches** to `~/notify_logger/matches.log`

Check status / logs:
```bash
systemctl --user status notify-logger.service
journalctl --user -u notify-logger.service -f
```

To change the target app/body/output path, edit the `ExecStart=` line in
`~/.config/systemd/user/notify-logger.service`, then:
```bash
systemctl --user daemon-reload
systemctl --user restart notify-logger.service
```

### Optional: survive reboot even when not logged in

By default, user systemd units only run once you're logged in (or after a
graphical session starts, per `After=graphical-session.target` in the unit).
If you want it running even before login (e.g. on headless boot), enable
lingering:
```bash
loginctl enable-linger $USER
```

## 4. Uninstall

```bash
systemctl --user disable --now notify-logger.service
rm ~/.config/systemd/user/notify-logger.service
rm ~/.local/bin/notify_logger
systemctl --user daemon-reload
```

## Troubleshooting

**No matches ever appear, even for real notifications:**
Modern `dbus-broker`/`dbus-daemon` policies often block a non-owning process
from eavesdropping on someone else's method calls unless explicitly allowed.
This build already requests `eavesdrop='true'` in the match rule, which
works on most default session-bus configs. If it still doesn't work, check:
```bash
dbus-monitor "interface='org.freedesktop.Notifications'"
```
If `dbus-monitor` also sees nothing, your session bus config is restricting
eavesdropping — check `/usr/share/dbus-1/session.conf` and any drop-ins under
`/etc/dbus-1/session.d/` for `<policy>` rules limiting `eavesdrop`.

**Body text doesn't match what you expect:**
The `--body` filter is an exact, case-sensitive match against the D-Bus
notification `body` field. If matches are missing, run `dbus-monitor` while
receiving a message and inspect the actual `summary` and `body` argument
values for your client build.
