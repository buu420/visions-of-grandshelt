# Visions of Grandshelt 0.2.54 beta

This update supports FINAL FANTASY RESONANCE on Windows Steam build 24250053.

- Story resumes after the credits and guides to Ultima Weapon once the route is available. Crystal Tower routes and optional pickups also received fixes.
- The route beacon plays slightly lower when the next turn is behind you, including behind-left and behind-right.
- Press **F4** or click the **right stick** to interrupt the current attack's announcements. Moving through battle menus also cuts them off. Enemy target speech includes visible weakness, stagger and health indicators.
- Victory waits for the reward counters. Use **Up/Down** or **D-pad Up/Down** to browse the results. **F5** repeats the selected row.
- English descriptions use recordings. The other seven game text languages use screen-reader speech for descriptions.

## Install or update

Close the game and select **Beta** for Visions of Grandshelt under Buu's Mods in [Accessibility Mod Manager](https://github.com/RealAmethyst/AccessibilityModManager). For manual installation, extract the Windows ZIP into the Steam game folder. Both packages include the game-local loader, Prism, and description assets. The loader requires the [Microsoft .NET 9 Desktop Runtime for Windows x64](https://dotnet.microsoft.com/en-us/download/dotnet/9.0).

If you manually extracted 0.2.52, its retired recordings are in the `audio` folders under `de`, `es-ES`, `fr`, `ja`, `ko`, `zh-Hans` and `zh-Hant` inside `ResonanceAccess/Reloaded-II/Mods/Resonance.Access/Descriptions`. You can remove those seven folders when updating. The English recordings are in `Descriptions/audio`.

## Beta testing

Copied-save tests covered the finale, clear-save reload, Ultima Weapon routing, and victory review. Localized descriptions and the new enemy indicators also have automated checks. Feedback on how they sound during play is welcome. Include the game text language, scene or enemy, and the last announcement in [GitHub Issues](https://github.com/buu420/visions-of-grandshelt/issues).
