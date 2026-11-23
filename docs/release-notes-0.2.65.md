# Visions of Grandshelt 0.2.65 Beta

This update fixes missing People and Exits targets at town entrances and when you return to the street. It supports FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- From Felicitas’s entrance, you can find reachable NPCs and all four street-level building doors before moving.
- You can choose the inn as soon as you return from the armory, item shop, or lower house. People lists the attendants once you enter the shops or inn.
- Town navigation checks walkable paths around buildings so a target can appear when the route requires turns.

## Update

Close the game. In Amethyst, choose **Visions of Grandshelt** from **Buu’s Mods** and select **0.2.65 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch from Steam. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are upgrading manually from 0.2.52, follow the cleanup instructions for the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

Copied-game tests verified the Felicitas entrance in two story states, shop and house round trips, and 35 default town arrivals. The Rebel Hideout passed from its tunnel entrance after the copied cutscene was ended. The normal cutscene handoff still needs testing. All 33 automated test suites passed. The startup log identifies this build as `0.2.65-dev`.

If People or Exits misses a target, report the town, where you entered, what was missing, and whether moving made it appear. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
