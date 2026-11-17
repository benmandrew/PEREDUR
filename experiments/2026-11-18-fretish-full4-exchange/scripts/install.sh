#!/bin/sh
# Install the full4-exchange timer on this host: sh install.sh av2|av3.
# Starts nothing now; the timer starts the driver at 06:00 UTC on 2026-11-18.
set -eu
host=$1
case "$host" in av2|av3) ;; *) echo "usage: install.sh av2|av3" >&2; exit 2 ;; esac
units=$HOME/.config/systemd/user
mkdir -p "$units"
printf 'FULL4X_HOST=%s\n' "$host" > "$HOME/.config/full4x.env"
cat > "$units/full4x.service" <<'UNIT'
[Unit]
Description=full4-exchange driver (experiments/2026-11-18-fretish-full4-exchange)

[Service]
Type=simple
WorkingDirectory=%h/projects/peredur-full4-exchange
EnvironmentFile=%h/.config/full4x.env
ExecStart=/usr/bin/python3 experiments/2026-11-18-fretish-full4-exchange/scripts/driver.py --host ${FULL4X_HOST}
MemoryMax=8G
MemorySwapMax=0
# Backstop for the driver's own 11:59:30 stop: started at 06:00:00, this
# kills the whole unit before 12:00.
RuntimeMaxSec=21580
KillMode=control-group
UNIT
cat > "$units/full4x.timer" <<'UNIT'
[Unit]
Description=Start the full4-exchange driver at 06:00 UTC on 2026-11-18

[Timer]
OnCalendar=2026-11-18 06:00:00 UTC
AccuracySec=1s
Persistent=false
Unit=full4x.service

[Install]
WantedBy=timers.target
UNIT
systemctl --user daemon-reload
systemctl --user enable --now full4x.timer
systemctl --user list-timers full4x.timer --no-pager
