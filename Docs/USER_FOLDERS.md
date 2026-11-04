# Where Vortex64 keeps your files

| What | Folder |
|---|---|
| Settings, shortcuts, ROM list caches, settings of the plugins (GLideN64.ini...), your RDB copy | `$XDG_CONFIG_HOME/vortex64/` (`~/.config/vortex64/`) |
| Saves, save states, screenshots, your cheats and enhancements, scripts, textures | `$XDG_DATA_HOME/vortex64/` (`~/.local/share/vortex64/`) |
| Library state of the frontend (favourites, history) | `~/.config/vortex64/vortex.json` |

The file names inside keep their technical names (`Project64.cfg`, `Project64.sc3`, `*.rdb`): they are formats of the emulator core and
other tools read them.

## Coming from Project64 (the earlier folders)

Before it had its own name the same files were in `.../project64/`. Nothing there is changed, moved or deleted:

* at the first start Vortex64 **copies** what is in `~/.config/project64/` and `~/.local/share/project64/` to the folders above (settings,
  shortcuts, saves, states, screenshots, cheats, scripts, the plugin settings). It never replaces a file that already exists in the new
  folder, and each file is copied under a temporary name and renamed when complete.
* **Logs** are not copied. **Textures** (they can be gigabytes) are not copied: `vortex64/Textures` becomes a link to the old folder when the
  new one has none.
* It is done **once**: a `.migrated-from-project64` file in each new folder says so. If a copy fails (a file that cannot be read, no space)
  the mark is not written and the next start tries again; what was already copied is skipped.
* A new installation (no old folder) just gets its own folders; an old folder that appears later is not imported.
* Directories that you chose in the settings with an absolute path keep pointing where they pointed.
* The saves of both applications are separate after the copy: if you keep using the old one, play in one of them.

Tested by `migration-test` (temporary directories only; never your real ones).
