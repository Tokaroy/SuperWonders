# Experimental ultrawide support

The tested configuration is Wonder 1.0.0 at 3840x1080 (32:9). It widens the
world view, includes the experimental terrain, in-game HUD, talking flower,
and world-map player adjustments, and retains the existing separate launcher.
Some menus and loading screens may still use a centered 16:9 layout. Other
aspect ratios and game versions have not been validated with all these fixes.

For a build of this branch, launch the game directly with these environment
variables in the same PowerShell session:

```powershell
$env:NX_ULTRAWIDE = '3840x1080'
$env:NX_TERRAIN_MOD_ASPECT_TEST = '1'
$env:NX_UW_POWERUP_TEST = '1'
$env:NX_UW_FLOWER_TEST = '1'
$env:NX_UW_MAP_ALIGN_TEST = '1'
$env:NX_UW_MAP_COMPOSITE_TEST = '1'
Start-Process -FilePath '.\wonder.exe' -ArgumentList '--play'
```

The terrain option changes a verified Wonder 1.0.0 constant in memory before
constructors run. The 32:9 value follows the emulator patch by StevensND and
KeatonTheBot: https://github.com/StevensND/switch-port-mods/tree/main/Super%20Mario%20Bros.%20Wonder/%5B010015100B514000%5D/Aspect%20Ratio/32.9
It is a fixed 32:9 value; do not enable it for a different ratio. The other
options activate the fixes tested alongside it. Restart the game after changing
aspect ratio or these options.

## World-map HUD layout

The tested 32:9 map HUD also uses a layout generated from the player's own
extracted game data. The generated files are not distributed in this repository.
With the game's Python dependencies installed, run the following from the
repository root, substituting the paths for the extracted RomFS, the existing
`recomp-menu` mod's resource-size table, and the installation's mods folder:

```powershell
python tools/wonder_uw_map_hud.py `
  --romfs 'PATH_TO_DATA\extract\romfs' `
  --rstb 'PATH_TO_DATA\mods\recomp-menu\romfs\System\Resource\ResourceSizeTable.Product.100.rsizetable.zs' `
  --out 'PATH_TO_DATA\mods\00-uw-map-hud-32-9' `
  --aspect 3.5555555556
```

The generated resource-size table is based on the specified table. If the menu
mod is regenerated or other RomFS mods also replace this table, their resource
size changes must be combined; otherwise mod precedence can discard entries.
The HUD layout generated above is specific to the requested aspect ratio.

Diagnostic `*_TEST`, snapshot, and tracing switches remain available for the
investigation; the map proxy and map depth test switches are diagnostics, not
part of the working configuration above.
