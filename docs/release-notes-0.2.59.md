# Visions of Grandshelt 0.2.59 beta

This update supports FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- The exit scanner refreshes after you return from a shop and finishes checking routes before cycling through targets.
- Treasure chests appear when the game offers an interaction or the mod verifies a walking route to the interaction point.
- Guidance recalculates from your current position as you detour. It pauses while field controls are unavailable and checks a fresh route when they return. Crossing a selected exit completes that route.

## Update

Close the game. In [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager), choose **Visions of Grandshelt** from **Buu's Mods** and select **0.2.59 Beta**. For a manual update, extract the Windows ZIP into the Steam game folder. Install the [Microsoft .NET 9 Desktop Runtime for Windows x64](https://dotnet.microsoft.com/en-us/download/dotnet/9.0), start your screen reader, then launch the game from Steam.

For a manual upgrade from 0.2.52, see the [instructions for removing the retired recordings](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54).

## Beta testing

A copied Mitra save verified that the town exit was listed after leaving the weapon shop, guidance resumed after the System menu, and the route updated during an 11-metre detour. The inn chest appeared from inside the inn and opened successfully. The startup log identifies this reader as `0.2.59-dev`.

Try browsing **Exits, doors and ladders** after leaving a shop. Start guidance to an exit, walk off the route, and listen for the updated directions. If a route or target is missing or incorrect, include the area, selected target, and last announcement in [GitHub Issues](https://github.com/buu420/visions-of-grandshelt/issues).
