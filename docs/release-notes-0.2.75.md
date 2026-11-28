# Visions of Grandshelt 0.2.75 Beta

This beta supports FINAL FANTASY RESONANCE on Windows x64, Steam build 24250053 (game version 1.0.1.0).

- **Mobliz Shipyard** does less work while checking walking routes. At the entrance, opening Objects returned the same route after 21 checks. The slowest check dropped from 22.4 ms to 12.7 ms, and the longest frame interval dropped from 75.4 ms to 38.7 ms in the paired test.
- **The manual beacon** updates sooner when you run toward a turn. It keeps pointing along the route as you pass the turn and recalculates if you detour.
- **Village of Kol** finds reachable people, objects, and exits from the pond paths and the paths around buildings. Story guidance follows the next objective at the inn or the Chocobo Stablehand, including after you return to the street.

## Update

Close the game. In Amethyst, refresh **Buu’s Mods**, find **Visions of Grandshelt**, and select **0.2.75 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager) · [Buu’s Mods catalog](https://github.com/buu420/buu-s-mods)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch from Steam. Back up an existing `FFRS/Binaries/Win64/dwmapi.dll` if you use a separate loader. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are updating manually from 0.2.52, follow the instructions for cleaning up the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

In Mobliz Shipyard, open Objects near the entrance and try the route to the Tablet Shard. Browse Exits for the factory passage and the world-map exit. During Borin’s escape, Story guides you from the rooftop lift through the lever puzzle and on to the cargo elevator. **F9** reads the visible countdown.

In Kol, try **People**, **Objects**, and **Exits** after you pass the boy by the pond and again after you come out of a building. Start the beacon and run through a turn, then step off the route. **Delete** starts the beacon. On controller, hold **RB/R1** and click the left stick.

All 33 automated tests passed. The copied-game replay collected the Tablet Shard, opened the rooftop-landing chest, verified both entrance routes, and completed the timed escape. The escape replay used scripted controls, disabled random encounters, and applied a damage boost during the scripted cargo battle. The game’s 15-minute countdown ran normally. The reader logs this beta as `0.2.75-dev`.

If you encounter a stall or a route that stops, include your starting area, category, target, and last announcement in your report. Attach `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
