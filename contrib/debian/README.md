
Debian
====================
This directory contains files used to package biblepayd/biblepay-qt
for Debian-based Linux systems. If you compile biblepayd/biblepay-qt yourself, there are some useful files here.

## Biblepay: URI support ##


Biblepay-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install Biblepay-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your Biblepay-qt binary to `/usr/bin`
and the `../../share/pixmaps/Biblepay128.png` to `/usr/share/pixmaps`

Biblepay-qt.protocol (KDE)

