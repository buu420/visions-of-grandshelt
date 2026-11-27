# Visions of Grandshelt 0.2.71 Beta

This beta supports FINAL FANTASY RESONANCE on Windows x64, Steam build 24250053 (game version 1.0.1.0).

- **Mobliz Shipyard** Story guides you to the elevators and switches, through Borin’s rescue, and back through the 15-minute escape. Use the game’s Interact control when prompted. **F9** reads the countdown when the game displays it.
- **Shipyard Objects** lists reachable treasure, pickups, and the pot as you explore the walkways. **Exits, doors and ladders** includes the reachable ladder.
- **Dwarven Forge** guidance from Mt. Thunderclap’s far exit was rechecked through the Forge’s south gate.
- **Manual beacon** pauses during battle and recalculates the route to the selected destination when field control returns.

## Update

Close the game. In Amethyst, refresh **Buu’s Mods**, find **Visions of Grandshelt**, and select **0.2.71 Beta**. [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager) · [Buu’s Mods catalog](https://github.com/buu420/buu-s-mods)

For a manual update, extract the Windows ZIP into the Steam game folder. Install the Microsoft .NET 9 Desktop Runtime for Windows x64, start your screen reader, then launch the game from Steam. Back up any existing `FFRS/Binaries/Win64/dwmapi.dll` if you use a separate loader. [Microsoft .NET 9 Desktop Runtime](https://dotnet.microsoft.com/en-us/download/dotnet/9.0)

If you are updating manually from 0.2.52, follow the instructions for cleaning up the retired recordings. [0.2.54 upgrade notes](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54)

## Beta testing

Try Story from the Shipyard entrance, then use Objects from each level. The ladder, treasure lift, and east walkway pickups need more live coverage. On the world map, select Dwarven Forge from Thunderclap’s far exit. **F8** reads directions, **Delete** starts the beacon, and **Right Shift+Delete** starts auto-walk. Try keeping the manual beacon active through a victory and dismissing the results.

All 33 automated tests passed on the released reader. Development builds completed the escape on the native timer and reached the Forge’s south gate, two Shipyard chests, the pot, and the entrance shard. Tests verified the manual beacon after Escape and auto-walk after victory. The escape replay disabled random encounters and boosted damage for the scripted fight in the copied game. The reader logs this beta as `0.2.71-dev`.

If a route fails, report where you started, the category and target you selected, and the last announcement. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
