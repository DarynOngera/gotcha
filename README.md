# Build & Install

A small D-Bus listener that watches for `org.freedesktop.Notifications.Notify`
calls, filters by app name + sender, and appends matches to a plain-text log
file in a human-readable `[timestamp] Sender: body` format. Runs as a
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
./notify_logger --app whatsapp-linux-app --sender Name --out ./matches.log
```
Send yourself a test notification, or wait for a real one, and confirm a line
appears in `matches.log` and on stdout, e.g.:
```
[2026-07-08 14:32:10] Name : hey, are you around?
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

To change the target app/sender/output path, edit the `ExecStart=` line in
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

**`summary` doesn't match the sender name you expect:**
Not all apps put the contact name in `summary`. If matching by `summary`
alone is unreliable for your WhatsApp client build, run `dbus-monitor` while
receiving a message from the target contact and inspect the actual argument
order/values — some clients put the sender in `body` prefix text instead, or
expose a more stable identifier in the `hints` dict (6th positional arg),
which this program currently doesn't parse.
