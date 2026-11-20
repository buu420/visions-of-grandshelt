# Visions of Grandshelt 0.2.61 beta

This update supports FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- In the Village of Kol, Story guides you to the inn door and innkeeper. After you rest, it guides you to the Chocobo Stablehand. Use the game's interaction control when prompted.
- On a chocobo, browse targets, read directions with **F8**, or use the beacon and auto-walk. The beacon replans when you steer off the route. Moving the left stick takes over from auto-walk.
- At the Mobliz Shipyard service elevator, select **Elevator** under **Exits, doors and ladders**. Follow the route to the panel and use the game's interaction control to return to the main shipyard.

The new Kol Story lines use English.

## Update

Close the game. In [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager), choose **Visions of Grandshelt** from **Buu's Mods** and select **0.2.61 Beta**. For a manual update, extract the Windows ZIP into the Steam game folder. Install the [Microsoft .NET 9 Desktop Runtime for Windows x64](https://dotnet.microsoft.com/en-us/download/dotnet/9.0), start your screen reader, then launch the game from Steam.

For a manual upgrade from 0.2.52, see the [instructions for removing the retired recordings](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54).

## Beta testing

All 33 automated test suites passed. Copied-game replays verified Kol's inn and rental, mounted guidance and auto-walk, and the Mobliz elevator return. The area survey and follow-up replays checked all 142 playable map IDs. From the sampled entries and story states, 139 maps returned a route, one started inside its target's activation area, and two were scenes without navigation targets. The startup log identifies the reader as `0.2.61-dev`.

If a route fails, include the area, current Story objective, selected target and last announcement in [GitHub Issues](https://github.com/buu420/visions-of-grandshelt/issues). For a mounted route, include whether you were using the beacon or auto-walk.
