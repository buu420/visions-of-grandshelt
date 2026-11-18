# Visions of Grandshelt 0.2.58 beta

This update supports FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- Enemy targeting reads the visible stagger gauge and affinity icons. It names revealed elements and calls unrevealed elements unknown. Exact HP is read when Libra reveals it.
- Press F9 while targeting an enemy to hear who is acting and the focused enemy's details. Turn changes announce the acting character's name. Press F9 outside enemy targeting for the battle overview.

## Update

Close the game. In [Amethyst Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager), choose **Visions of Grandshelt** from **Buu's Mods** and select **0.2.58 Beta**. For a manual update, extract the Windows ZIP into the Steam game folder. Install the [Microsoft .NET 9 Desktop Runtime for Windows x64](https://dotnet.microsoft.com/en-us/download/dotnet/9.0), start your screen reader, then launch the game from Steam.

For a manual upgrade from 0.2.52, see the [instructions for removing the retired recordings](https://github.com/buu420/visions-of-grandshelt/releases/tag/v0.2.54).

## Beta testing

A copied-save test verified the Wolf A and Damia B targeting announcements and the shorter F9 review through NVDA. The startup log identifies this reader as `0.2.58-dev`.

Try switching between an enemy you've analyzed with Libra and one you've yet to analyze. Press F9 while a target is selected. If an announcement is missing or incorrect, include the game text language, enemy, whether you used Libra, and the last announcement in [GitHub Issues](https://github.com/buu420/visions-of-grandshelt/issues).
