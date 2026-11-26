# Visions of Grandshelt 0.2.70 Beta

This beta fixes two routes through Mt. Thunderclap. It supports FINAL FANTASY RESONANCE on Windows x64, Steam build 24250053 (game version 1.0.1.0).

- **Onion Knight’s Sanctum of Light** is reachable through Thunderclap’s side trail. Guidance continues to the lower exit, then around the wall at the Sanctum entrance.
- **Ramuh’s Esper marker** retains the selected route after a detour and continues through the high-trail entrance to the encounter. Choose **Esper** in **Map Markers** when the game shows the rumor.

## Update

Close the game. In Amethyst, refresh **Buu’s Mods**, find **Visions of Grandshelt**, and select **0.2.70 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager) · [Buu’s Mods catalog](https://github.com/buu420/buu-s-mods)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch the game from Steam. If you use a separate loader, back up any existing `FFRS/Binaries/Win64/dwmapi.dll` before replacing it. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are updating manually from 0.2.52, follow the instructions for cleaning up the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

Try **F8** for directions, **Delete** for the beacon, or **Right Shift+Delete** for auto-walk. Use the game’s interaction control at entrances when prompted. For the Onion Knight route, the reproduced report started outside the side-trail entrance after collecting Noctis. For Ramuh, try a detour with the Esper beacon active and check that guidance resumes inside the high trail.

During the **Mobliz Shipyard** escape, **F9** reads the visible countdown. The copied 0.2.69 replay reached the train on the normal 15-minute timer. At the rooftop elevator, pressing **Interact** worked after auto-walk reported a block.

All 33 automated test suites passed. The copied-game checks reached the Sanctum and started Ramuh’s encounter. Random encounters were disabled in the copy, and the Mobliz cargo battle used a test damage boost. The reader logs this beta as `0.2.70-dev`.

If a route fails, report where you started, the category and target you selected, and the last announcement. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
