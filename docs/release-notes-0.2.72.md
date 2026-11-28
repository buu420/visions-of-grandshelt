# Visions of Grandshelt 0.2.72 Beta

This beta supports FINAL FANTASY RESONANCE on Windows x64, Steam build 24250053 (game version 1.0.1.0).

- **Story in Mitra** follows your progress through Rain’s house. It leads you to the front door, the basement door where you use the key, and the next investigation. If you visit the inn first, Story directs you back to the street.
- In **Grandshelt Castle**, Story finds the next route after you leave Rain’s room. In the burning castle, auto-walk can guide you from the ladder landing to the king’s chamber doors.
- The **world-map route to Sanctum of Light** for Onion Knight’s Vision checks a walkable approach around the steep ground near Grandshelt. Use the game’s Interact control when you reach the entrance.

## Update

Close the game. In Amethyst, refresh **Buu’s Mods**, find **Visions of Grandshelt**, and select **0.2.72 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager) · [Buu’s Mods catalog](https://github.com/buu420/buu-s-mods)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch from Steam. Back up an existing `FFRS/Binaries/Win64/dwmapi.dll` if you use a separate loader. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are updating manually from 0.2.52, follow the instructions for cleaning up the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

Try Story from the Mitra inn and from inside Rain’s house while you have the basement key. In Grandshelt Castle, cross from Rain’s room into the hall and start the next route. In the burning castle, try the king’s chamber route from the top of the ladder.

After Fina joins, select **Sanctum of Light** under **Destinations** on the world map. **Delete** starts the beacon, and **Right Shift+Delete** starts auto-walk. On controller, hold **RB/R1** and click the left stick for the beacon or the right stick for auto-walk. Try walking with the beacon, including after a battle, and use Interact at the entrance.

All 33 automated tests passed. Copied-game replays covered the basement key and investigation scene, the castle hall and king’s chamber doors, and arrival inside the Sanctum. The full Sanctum replay used scripted controller input and disabled random encounters in the copy. Live controller and audible beacon feedback are welcome. The reader logs this beta as `0.2.72-dev`.

If a route fails, report where you started, the category and target you selected, and the last announcement. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
