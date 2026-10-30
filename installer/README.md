# installer

WiX per-user (HKCU) MSI. Not implemented yet.

The installer is the only component that registers the thumbnail handler. It records any
existing `.ply` thumbnail registration on install and restores it on uninstall.
