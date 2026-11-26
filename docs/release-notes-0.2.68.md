# Visions of Grandshelt 0.2.68 Beta

This beta restores navigation on Mt. Thunderclap and in Oblivion Palace. It supports FINAL FANTASY RESONANCE on Windows x64, Steam build 24250053 (game version 1.0.1.0).

- On **Mt. Thunderclap**, Story leads to the far-side passage from the main trail and its save point. If you detour onto a higher trail, Story guides you to the rope or the climbable end of the ladder. Objects can route to the medal pickup from the trail.
- In **Oblivion Palace**, People lists the palace resident. Exits names the reward doors and guides you through them after you open them. Objects lists the reachable safes in your current chamber. You can return to the hall and take the world-map exit.
- **Map Markers** and **Side Quests** appear for supported visible map icons and the quest you track in the game’s Journal. They work with F8, the beacon, and auto-walk.
- Targets with a nearby interaction appear first while the other routes are checked quietly. The beacon updates sooner after sharp turns. The route to the second chest in the burning castle uses a verified approach.

## Update

Close the game. In Amethyst, choose **Visions of Grandshelt** from **Buu’s Mods** and select **0.2.68 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch from Steam. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are updating manually from 0.2.52, follow the instructions for cleaning up the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

All 33 automated test suites passed. Tests in a copied game verified the Thunderclap route up to the next story scene, the palace’s ten-shard handover, the 25 safe listings across all four chambers, and a round trip through the second chamber and back to the world map. Later reward unlocks and the following Thunderclap battle were outside those playtests. The reader logs this build as `0.2.68-dev`.

If a route fails, report your area, Story objective, selected target, and last announcement. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
