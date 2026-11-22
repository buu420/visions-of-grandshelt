# Visions of Grandshelt 0.2.64 beta

This update improves town navigation in FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- In **Grandport**, reach the armor clerk and follow the route back through the harbor to the world-map exit after leaving the shop.
- In **Felicitas**, find the inn and shops from the fast-travel arrival. Get directions to attendants, house doors and the upstairs chest, then follow the upper walkway to the ladder back to the street.
- **Objects** lists usable bulletin boards. **Exits, doors and ladders** lists reachable Zoldaad streetcar timetables and Mysidia portals when the game makes them available.
- Moving NPCs no longer hold up the rest of the target list. At the upper ladder in Grandshelt Castle, guidance tells you when to use the game's interaction control before climbing down.

## Update

Close the game. In Amethyst, choose **Visions of Grandshelt** from **Buu’s Mods** and select **0.2.64 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch from Steam. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are upgrading manually from 0.2.52, follow the cleanup instructions for the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

Copied-save in-game tests completed reachability scans on 48 of 49 audited town and interior maps. They also verified the Grandport shop exit, Felicitas house and shop routes, and selected boards and transit stops. All 33 automated test suites passed. The optional Chamber of Arms and some quest-dependent NPC and object states still need in-game verification. The startup log identifies this build as `0.2.64-dev`.

If a route fails, report the town and room, how you entered, the target you picked, and the last announcement. Mention whether you were using auto-walk and include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
