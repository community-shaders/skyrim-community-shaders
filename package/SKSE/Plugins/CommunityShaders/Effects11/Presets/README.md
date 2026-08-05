# Effects 11 Presets

Place each ENB-compatible preset in its own subfolder:

```
Data/SKSE/Plugins/CommunityShaders/Effects11/Presets/<PresetName>/
  enbseries.ini
  enbseries/
    enbeffect.fx   (required)
    ...other FX, textures, weather files...
```

Open this folder from the Effects 11 menu (**Open Folder**). Switch presets in-game without touching a classic root/Data `enbseries` install (Legacy).

Classic Legacy installs (`enbseries` next to the game exe, or under `Data`) still work and appear as **Legacy** in the preset list.

**Defaults:** Legacy is used when a valid root/Data install is present; otherwise the first valid folder preset (alphabetical) is selected automatically. Changing the preset in the UI saves the Community Shaders selection immediately (no separate Save Settings click).
