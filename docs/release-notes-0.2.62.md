# Visions of Grandshelt 0.2.62 beta

This update supports FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- Select **Story** in Kolobos Ruins during **Look for Emma’s mom**. Story guides you to the next passage and recalculates from your position when you return from the side room.
- Auto-walk resumes after the bridge fight. When Story says **Hold Up to climb**, use the game’s movement control. Auto-walk continues when you reach the upper passage.
- When **Find a ship in Grandport** begins, Story guides you from the Felicitas landing to the world-map exit. Use the game’s interaction control at the gate.

The new Story guidance uses English.

## Update

Close the game. In [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager), choose **Visions of Grandshelt** from **Buu’s Mods** and select **0.2.62 Beta**. For a manual update, extract the Windows ZIP into the Steam game folder. Install the [Microsoft .NET 9 Desktop Runtime for Windows x64](https://dotnet.microsoft.com/en-us/download/dotnet/9.0), start your screen reader, then launch the game from Steam.

For a manual upgrade from 0.2.52, see the [instructions for removing the retired recordings](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54).

## Beta testing

All 33 automated test suites passed. A copied-save replay with scripted controller input and battle assists completed the ruins objective, including the side-room detour. A separate replay on the packaged build verified the Felicitas exit from the saved landing. The startup log identifies the reader as `0.2.62-dev`.

If Story gets stuck, report the room you were in, the current objective, the last announcement, and whether you were steering or using auto-walk. Include `ResonanceAccess.log` if available. [Report a bug](https://github.com/buu420/visions-of-grandshelt/issues)
