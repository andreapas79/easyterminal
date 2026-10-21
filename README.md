# EasyTerminal

A fork of [GNOME Terminal](https://gitlab.gnome.org/GNOME/gnome-terminal) 3.44.0
(GTK 3, VTE 0.68) that adds:

- **Tab renaming** – right-click a tab → *Rename Tab…*. The name stays even
  when the running program changes the title.
- **Tab groups** – right-click a tab → *Move to New Group…*. Each group has a
  coloured, named pill before its tabs: click it to collapse or expand the
  group, right-click it to rename or ungroup.

EasyTerminal installs next to GNOME Terminal without touching it: its own app
ID (`io.github.andreapas79.easyterminal`), binaries, settings and D-Bus
service. Built and tested on Pop!_OS 22.04 / Ubuntu 22.04.

## Install

Download the `.deb` from the releases page and run:

```sh
sudo apt install ./easyterminal_3.44.0-1easy1_amd64.deb
```

To open it with Super+T instead of GNOME Terminal:

```sh
gsettings set org.gnome.desktop.default-applications.terminal exec 'easyterminal'
```

## Build

```sh
sudo apt install debhelper meson ninja-build pkg-config gettext \
  libglib2.0-dev libgtk-3-dev libvte-2.91-dev libdconf-dev \
  gsettings-desktop-schemas-dev libpcre2-dev uuid-dev libx11-dev \
  xsltproc docbook-xsl docbook-xml libxml2-utils desktop-file-utils

./build.sh        # build in ./build
./run-dev.sh      # run from the build tree (settings kept in memory only)
./build.sh deb    # build the .deb (written to the parent directory)
```

## Changes from GNOME Terminal

EasyTerminal is a modified version of GNOME Terminal 3.44.0, changed in 2026
by andreapas79. It is not affiliated with or endorsed by the GNOME project;
please report problems here, not to GNOME.

Changes, all visible in the git history after the first commit (the unmodified
upstream source):

- renamed app ID, binaries, settings schemas, D-Bus service and icon;
- tab renaming and tab groups (new `src/terminal-tab-group.*` and
  `src/terminal-name-dialog.*`, hooks in the notebook, window, screen and tab
  label code);
- Ubuntu / Pop!_OS default profile carried over to the new schemas;
- Italian translations for the new strings;
- Debian packaging (`debian/`) and the `build.sh` / `run-dev.sh` scripts.

## License

GPL-3.0-or-later, like GNOME Terminal. The original GNOME Terminal README is
in [README.gnome-terminal.md](README.gnome-terminal.md).
