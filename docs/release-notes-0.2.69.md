# Visions of Grandshelt 0.2.69 Beta

This beta fixes navigation from the east exit of Mt. Thunderclap and to the mountain’s collectibles. It supports FINAL FANTASY RESONANCE on Windows x64, Steam build 24250053 (game version 1.0.1.0).

- **Dwarven Forge** now has a direct route from the east exit. Story and Destinations guide you to the south entrance.
- Select **Esper** in **Map Markers** to reach Thunderclap’s high-trail entrance. Use the game’s interaction control at the gate.
- **Objects** lists all four chests and all three medal pickups from the trails that reach them. **Exits** names the different entrances so you can choose the trail you want.

## Update

Close the game. In Amethyst, choose **Visions of Grandshelt** from **Buu’s Mods** and select **0.2.69 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager) · [Buu’s Mods catalog](https://github.com/buu420/buu-s-mods)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch the game from Steam. If you use a separate loader, back up any existing `FFRS/Binaries/Win64/dwmapi.dll` before replacing it. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are updating manually from 0.2.52, follow the instructions for cleaning up the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

From the east exit, try **F8** for directions, **Delete** for the beacon, or **Right Shift+Delete** for auto-walk. Try a detour with the beacon on and check that it picks up the route from your new position.

All 33 automated test suites passed. In a copied game with random encounters disabled, manual guidance recalculated after detours, auto-walk reached both entrances, and all seven collectibles could be opened or picked up. The reader logs this build as `0.2.69-dev`.

If a route fails, report where you started, the category and target you selected, and the last announcement. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
