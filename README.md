# Visions of Grandshelt

Visions of Grandshelt is an accessibility mod for FINAL FANTASY RESONANCE on Windows. It adds screen reader access to menus, equipment, tutorials, dialogue and combat. Story navigation, auto-walk and a positional audio beacon help you explore. Audio descriptions cover selected movies and scripted scenes.

## Install

This beta supports the 64-bit Steam release, game version 1.0.1.0 and Steam build 24250053. Install the [Microsoft .NET 9 Desktop Runtime for Windows x64](https://dotnet.microsoft.com/en-us/download/dotnet/9.0).

In [Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager), choose **Visions of Grandshelt** for **FINAL FANTASY RESONANCE** from [Buu's Mods](https://github.com/buu420/buu-s-mods) and select the beta. Close the game before installing or updating. Start your screen reader and launch the game from Steam.

For manual installation, open the game's local files from Steam. Extract the Windows ZIP into that folder so `FFRS` and `ResonanceAccess` are beside `FFRS.exe`. The package places its loader at `FFRS/Binaries/Win64/dwmapi.dll`. Back up an existing loader at that path before replacing it.

The Windows and Amethyst downloads both include the game-local Reloaded loader, Prism, localization and description assets.

## Start playing

- **F5** reviews the current selection or dialogue. **F9** reviews the screen or the presented battle status. **F10** reads controls and availability.
- In the field, **Home/End** change category and **Page Up/Page Down** choose a target. **Delete** starts the directional beacon. **Right Shift+Delete** starts auto-walk.
- On a controller, hold **RB/R1** and use the **D-pad** to browse categories and targets. Click the **left stick** for guidance or the **right stick** for auto-walk. In battle, click the left stick for the party status panel.
- Open **System → Accessibility Options** to adjust beacon, wall, NPC and object cue volumes.

Use the game's interaction button when Story guides you to a door, switch or person. See the [full control guide](docs/controls.md) for combat review, automatic route continuation and how to steer on the supported airship and boat legs.

## Languages and beta testing

The mod follows the game's Text Language setting. French, German, Spanish (Spain), Japanese, Korean, Simplified Chinese and Traditional Chinese use the screen reader for displayed subtitles and audio descriptions. English audio descriptions use recordings.

In 0.2.60, Story guides you up the west staircase at Grandshelt Castle, and auto-walk warns and pauses before the Antlion quicksand. See the [0.2.60 beta notes](docs/release-notes-0.2.60.md).

In 0.2.59, exit browsing refreshes when you return from a shop, treasure chests require a verified approach, and guidance recalculates after detours and when field controls return. See the [0.2.59 beta notes](docs/release-notes-0.2.59.md).

In 0.2.58, enemy targeting reads the visible stagger gauge and elemental affinities. While targeting an enemy, F9 reads the acting character and the focused enemy's details. See the [0.2.58 beta notes](docs/release-notes-0.2.58.md).

In 0.2.57, copied-save controller tests reached the Earth Shrine entrance from the merchant and from the far side of the approach, and reached the nearby save point. In-game logs verified French and Japanese caption delivery to NVDA. See the [0.2.57 beta notes](docs/release-notes-0.2.57.md) for release details.

If a route fails, report the area, Story objective, selected target and last announcement in [GitHub Issues](https://github.com/buu420/visions-of-grandshelt/issues). The mod log is at `ResonanceAccess/Reloaded-II/Mods/Resonance.Access/ResonanceAccess.log`.

## Source and credits

Created by buu420 with development assistance from Codex and Claude Teammate. See [build instructions](docs/BUILD.md), the [software license](LICENSE) and [third-party notices](docs/THIRD-PARTY.md).
