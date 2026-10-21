# Code Monkey AMOLED for Roblox Studio

A [Roblox ModLoader](https://github.com/revolutionxk/rml-launcher) mod that makes all of Roblox Studio pure black. The script editor uses the Dark+ syntax colors from the VS Code theme. It also colors the Windows title bar `#111111` like VS Code's.

The mod is the `studio_theme` example from Roblox ModLoader, extended to reach the parts of Studio that the stock example couldn't:

| Part of Studio | How it's themed |
| --- | --- |
| Menus, docks, script editor, classic panels | The mod recolors Studio's theme file as it loads. |
| Stylesheets Studio sets later (such as the menu bar's) | Hooks `QWidget::setStyleSheet` and recolors each stylesheet as it's set. |
| Ribbon, Explorer, Properties, Start Page | Recolors each panel's finished frame on the GPU, just before it's shown. The 3D viewport is skipped, so game colors stay as they are. |
| Title bar | Sets the title bar colors through DWM for every Studio window. |

Plugin popups drawn inside the viewport (Rojo's toasts, for example) keep their own colors.

## Install

You need Windows 11 and the [Roblox ModLoader launcher](https://github.com/revolutionxk/rml-launcher), with the mod loader installed for your Studio version.

1. Download `studio_theme.zip` from this repository's [releases](https://github.com/dooory/code-monkey-amoled-rml/releases).
2. Close Studio.
3. In the launcher's **Mods** section, choose **Add from .zip** and pick `studio_theme.zip`.
4. Open Studio. It starts on Code Monkey AMOLED. To switch themes or go back to stock, use **Mods > Studio Theme**.

Each release is built against one version of Roblox ModLoader, named in the release notes. If a newer mod loader won't load the mod, build it yourself (below) or wait for a new release.

Studio installs each update into a new version folder, so import the zip again after Studio updates.

## Font

Install **Inconsolata Nerd Font Mono** from the [Nerd Fonts Inconsolata release](https://github.com/ryanoasis/nerd-fonts/releases/download/v3.4.0/Inconsolata.zip), then restart Studio. Open **File > Studio Settings > Script Editor > Font** and select that family. Keep your preferred editor size.

The mod customizes colors rather than fonts. This setting changes the script editor; the ribbon, Explorer, Properties and other panels keep their native fonts. The font includes Nerd Font symbols, but does not replace Studio's existing icons.

## Build

The mod builds as part of the Roblox ModLoader source tree. You need Visual Studio 2022 Build Tools with the C++ workload, and CMake.

1. Clone the [Roblox ModLoader source](https://github.com/revolutionxk/roblox-modloader) and copy this folder's `studio_theme` over `examples/studio_theme`.
2. Configure, then build the mod:

   ```powershell
   cmake -B build
   cmake --build build --target studio_theme --config Release
   ```
3. Package it for the launcher:

   ```powershell
   .\package.ps1 -ModLoader C:\path\to\roblox-modloader
   ```

   This writes `studio_theme.zip` next to the script, ready for **Add from .zip**.

## Releasing

Push a version tag and the Release workflow builds the mod, packages it and publishes `studio_theme.zip` as a GitHub release:

```powershell
git tag v1.0.0
git push origin v1.0.0
```

The workflow builds against the Roblox ModLoader commit in `MODLOADER_REF` at the top of [`.github/workflows/release.yml`](.github/workflows/release.yml). The release notes name that commit. To target a newer mod loader, change it there.

## Changing colors

The theme lives in `studio_theme/themes/presets/code-monkey-amoled.json`:

| Key | Default | Code Monkey role |
| --- | --- | --- |
| `background` | `#000000` | base |
| `surface` | `#111111` | tertiary (headers, menu bar, title bar) |
| `border` | `#303030` | border |
| `muted` | `#858585` | muted text |
| `text` | `#DCDCDC` | text |
| `accent` | `#007ACC` | highlight 2 |

The `syntax` section holds the script editor colors.

## Debugging

**Mods > Studio Theme > Dump widgets** writes every visible Qt widget's class, size, palette and stylesheet to the ModLoader log in `RobloxModLoader\logs\`.
