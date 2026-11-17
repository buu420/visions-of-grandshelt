# Visions of Grandshelt controls

The game must have focus. The mod reads selected state and visible text. F5 reviews the current ready selection or dialogue and otherwise repeats the last capture, F9 reads the current Options value or captured visible screen text, and F10 reads help and availability status. The game's own movement, menu, dialogue and Confirm controls remain usable.

Item and skill tips and gear stats follow the highlighted entry when the game displays them. F5 rereads the selection and its tip. F9 also reads the displayed shortcut prompts with their current bindings, including Quick Heal in the Main Menu. Recovery speaks the selected party member's name. In Quests, use Up and Down to select a quest. Use Page Up and Page Down or move the right stick vertically to scroll the summary and hear the passage in view. F5 rereads the current passage.

During battle, speech distinguishes the acting ally or enemy, current target and selected command. Displayed damage and recovery results are read for each hit. Press **F4** or click the **right stick** to interrupt the announcements for the current attack. Navigating the battle menus also interrupts them. F9 reviews the presented party and enemy sides, with current ally HP/MP and visible target details. The focused enemy includes its on-screen affinity prediction, stagger status and weakened health indicators when shown.

On the victory screen, use **Up/Down** on the keyboard or **D-pad Up/Down** to review the displayed rows. **F5** repeats the current row. **F9** reads the page.

Moving through a menu prioritizes the current selection. If you move quickly through several rows, speech can skip rows you already passed. In Equipment & Abilities, the readout pairs the selected slot type with its equipped item or ability.

## Battle status

The review starts on the acting ally when their party row is visible. You can change the review selection independently of the combat cursor.

| Keyboard | Readout |
| --- | --- |
| Home and End | Previous or next displayed ally |
| H | HP |
| M | MP |
| L | Limit gauge |
| F2 | Buffs |
| F3 | Debuffs and harmful conditions |

Click the left stick to open controller status. D-pad Left and Right choose a displayed ally, Up reads HP and MP, Down reads buffs and debuffs, and Y reads the Limit gauge. Press B or click the left stick again to close. Review uses information presented by the battle HUD. F9 also reviews the currently displayed Libra details.

## Accessibility Options

Open the Main Menu and choose System, then choose Accessibility Options directly below Options. You can also find it on the separate System screen opened by the System Menu shortcut. Move up and down to choose a setting. Left and right adjust volume in ten-percent steps or turn a cue off and on. Confirm also toggles a switch. Back returns to the System menu.

The beacon, wall tones, NPC sounds, and object sounds have independent volumes. Wall tones, NPC sounds, and object sounds also have separate switches. Settings apply immediately and are saved in `AudioSettings.ini` next to the mod DLL. A beacon volume of zero uses spoken turn directions for manual guidance.

## Dialogue, tutorials, and field controls

Automatic dialogue speech follows the game's Text Language setting. With English text, lines with verified native voice playback stay quiet and unvoiced or unverified lines read automatically. With any other supported text language, displayed subtitles read even when the game voices the line. F5 reviews the current ready selection or dialogue and falls back to the last capture. F9 reviews the screen. Opening English recordings describe the eight colored crystals.

Some sequence-audio-only and battle-sequence captions can still be read twice. Unknown, pre-reflect or inactive native voice state preserves readable text when no successful voice result was captured for that line. A proven voiced line stays quiet across a recorded-description delay. This update does not claim complete voiced-caption detection.

Tutorial speech includes the current page and the game's displayed page/close hints with current bindings. Closing is available on the final page once its animation finishes. In the checked keyboard setup, Q/E changes pages and C, Backspace or Escape closes the final page; Enter advances earlier pages and does not close the final page. The mod never presses these controls. Field HUD hints such as Mini-Map/System Menu stay available through F9 without blocking navigation as a synthetic menu.

| Field key | Action |
| --- | --- |
| Home | Previous category, wrapping at the beginning |
| End | Next category, wrapping at the end |
| Page Up | Previous target in the current category |
| Page Down | Next target in the current category |
| Delete | Toggle manual guidance to the selected target |
| Right Shift + Delete | Toggle automatic walking to the selected target |
| F6 / F7 | Previous / next target aliases |
| F8 | Read a route once |

Hold **RB/R1** during ordinary field play for the familiar controller layout:

| While holding RB/R1 | Action |
| --- | --- |
| D-pad left / right | Previous / next category |
| D-pad up / down | Previous / next target |
| Left stick click (L3) | Start manual guidance |
| Right stick click (R3) | Start automatic walking |
| B / Circle | Stop navigation |
| Y / Triangle | Read controller help and availability |

RB's direct Quests shortcut is reserved while field navigation is available. Open Quests from the Main Menu. LT keeps its native Track Quest and other actions, including while the replacement modifier is held. Pressing LT takes priority over controller navigation. Menus, battle and unsupported traversal keep native controller behavior. Release a held field RB press before using RB in a menu.

Release RB and the right-stick click to begin automatic walking. The mod waits for both controls to be up, even when you release one before the other. Release any held movement controls before starting. Use a movement control or RB+B to stop the walk. Captured closing controls remain withheld until released. Returning to the field, restoring focus, reconnecting or changing bindings never starts navigation from a held chord. One active XInput controller is supported. F10 explains controller availability.

The categories are All, People, Objects, Exits/doors/ladders, Save points, Story, and Destinations. The field lists show active targets with a walking route from your current location, including offscreen targets on the same reachable ground. People, objects and exits in another interior stay out of your list until you enter it. Offscreen moving actors update their positions live. Visible inn and shop icons distinguish those attendants. Each selection is remembered for its category and area, and navigation checks the target again when you start.

In a busy area, you may hear “Checking reachable targets” while walking routes are checked. You can keep using the controls during the check. The reader announces a verified target as soon as it can answer your selection and includes the count once the list is ready.

Use Exits to travel between rooms, areas, and towns. Choose a named entrance or exit, start guidance or auto-walk, and follow the game's interaction prompt if the door or gate requires it. The category carries into the new area so you can choose the next exit. If there is only one reachable exit, you can start guidance or auto-walk directly.

Use **Destinations** to pick a town or area once. Select it with Page Up/Down or RB and D-pad Up/Down. Start the beacon with Delete or RB and the left-stick click. Start automatic walking with Shift+Delete or RB and the right-stick click. The selected mode continues after you enter the expected room or area. Automatic walking also resumes after a battle when field controls return. Press the game's Interact button when a door or gate asks for it. RB+B stops the trip.

Story reads the current main objective and the game's available quest destinations. In the opening Battlefield, active continuations appear as Story events. Select the exit to the next Battlefield section when that is the active step. Control-relative direction and straight-line distance distinguish the available points before you request a route.

Use Page Up/Down or RB and D-pad Up/Down to choose a Story target. Start auto-walk with Shift+Delete or RB and the right-stick click. After the expected exit or a battle, auto-walk picks up when field control returns and Story can verify the next walking target. Press Interact at doors, gates, and controls when prompted. If several new routes are available, choose the next target and start again. RB+B or moving takes over from auto-walk.

When Story sends you to the helm of the Enterprise, follow the deck door and the helm route. Once the airship is flying, press Delete or hold RB and click the left stick to turn on the beacon for the tornado over Pharm. Steer with the game's movement controls. Story offers the entrance to Sian when field control returns after the encounter.

After Umbra, follow Story to the helm again. Once the Enterprise is airborne, press Delete or hold RB and click the left stick. Steer toward the Crystal Tower with the beacon. In the tower, Story announces the next passage or interaction as the game opens it. Use Exits to find the side chamber, the Middle Reach and Upper Reach landings, and the summit rest stop.

In Mitra, select Story after equipping a Vision to follow the route to Lasswell when his scene is available. After that conversation, Story guides you toward the next door or town exit for the Grandshelt Castle objective. The Exits category names Mitra's doors and the two world-map gates. Objects lists the mailbox by the inn while the game offers an unclaimed delivery.

During the Mobliz Shipyard escape, select Story to follow the return elevator, stairs and passages as the game opens them. The reader speaks the painted numbers as you pass them. At the elevator puzzle, use the Story target list for the memo and the numbered levers. F9 reviews the visible countdown and nearby floor marking. In the Wind Shrine, Story tells you when to call a lift and when it is ready to board. Use the game's interaction control at each stop. Start the beacon or auto-walk again when Story announces the next leg.

Story stays selected as an active objective carries into a new area. In Olderion it identifies the locals and the Warden of the Waters. In the Cave of Waters, interact with the pedestal and wait for Story to announce that you can board the boat.

On a Story raft crossing in the Cave of Waters or the Water Shrine, press Delete or hold RB and click the left stick to start the beacon. Steer with the game's movement controls. F8 reads the route and F9 repeats the current Story instruction. When the beacon announces the landing, use the game's interaction control to disembark. Start guidance again after the next crossing or passage loads.

At a ladder in Waterfall Way or the Water Shrine, follow the movement cue and start the next Story route after reaching the landing. In the Shrine crate room, use Story to reach the push side of the loose crate. Push it with the game's movement controls, then return to the pedestal when Story says the row is aligned. With Story selected, F9 includes its current instruction in screen review.

Use Right Shift with Delete. Left Shift is the game's Dash binding. Either Shift works only when the binding checks permit it; Dash is not remapped. Other modifiers, native binding conflicts and unreadable control state can make automatic walking unavailable. In a recognized field, Home/End, Page Up/Down and Delete explain unavailable field controls. They add no navigation messages in menus, maps, text entry or key assignment screens. F6/F7/F8 remain explicit commands that can explain unavailability; F6/F7 also remain separate aliases when a Page binding conflicts.

Walking is continuous on a 3D surface. F8 speaks the native horizontal walking-control directions and the game's world distances, for example “left 2 metres, then up 4 metres.” Up means pushing the up control. F8 describes route elevation separately. The native control heading can come from control rotation, the view target or the character camera. Guidance requires a fresh, complete route.

Both route modes play a two-note HRTF beacon. Use stereo headphones to hear which way to move for the next turn. The beacon plays at a lower pitch when the turn is behind you, including behind and to either side. The pulse interval follows the remaining route distance. If you leave the route during manual guidance, the mod recalculates from your current position. Arrival and route problems are spoken. F8 speaks directions at any time while a route is available. If the audio device is unavailable or a leg has no horizontal direction, guidance uses speech.

During field play, nearby NPCs and interactable objects repeat their distinct positional sounds from Echos of Gaia. Each sound follows its target relative to your movement controls and repeats faster as you get closer. You can hear multiple nearby targets with route guidance off. Selecting an entity reads its name, visible role or object type, along with its direction and straight-line distance when available.

Nearby solid walls produce continuous tones while you are on foot, even when you stand still. Each movement direction has its own pitch, and each sound comes from the direction of the wall. The tones get louder as you approach and fade as you move away. Use stereo headphones to hear where the sounds come from.

A second collision check distinguishes a doorway edge from a wall across the opening. The forward tone clears at an open entrance. Nearby walls and door frames can still sound to either side.

Delete turns manual guidance on or off. Shift+Delete turns automatic walking on or off. Selecting another target or category stops the current mode and requires starting again. Arrival stops the beacon and announces the target. Use the game's interaction or Confirm button at the destination.

Exit and event-trigger routes use the game's walking surface at the edge of the activation area. The mod checks that the player's capsule can reach the trigger there. The game handles the activation when you enter it.

When leaving Grandshelt Castle, Story offers the door or passage for the current leg. If the route ends at the hall door, walk through it with the game's controls, then start the next route. The save point and other targets with a complete route from the hall can also use this doorway as their first leg.

Automatic walking uses the game's normal movement animation and footsteps. Release RB and right-stick click to start moving. Use the game's Dash control with RB released to change pace according to your Dash setting. The mod slows for turns, limited clearance, and arrival.

Automatic walking yields to left-stick movement or movement through the game's current bindings, including the D-pad when it is bound to movement. You can also stop it with Shift+Delete or RB+B. You can use Interact, the camera stick, and other field buttons while walking. If an action opens a menu or takes field control, the walk stops. Focus loss, menus, dialogue, control/mode changes, missing speech, area/character changes, invalid or stale paths, and blocked progress also stop it. Going off camera does not stop an active target; native unavailability revokes it. An active Destinations trip can resume at an expected exit or when field control returns from a battle.

Automatic walking has no fixed total-distance cutoff. It retains the full native route and checks a forward portion of ordinary ground, renewing that check as you progress. The checked portion is tied to that exact route; its end is never treated as arrival. Walking speed, current velocity, braking and nearby route geometry determine how much must be checked. Walkable slopes use the current character and selected mesh limits. Terrain breaks omitted by the native path's straight segments receive a surface-following check; horizontal progress and conservative stopping distances keep that check tied to the actual ground ahead. Steps, drops, steep or unreadable ground, ladders, jumps and special traversal may require manual movement when approached. A distant special section does not prevent starting on the currently supported ground. A visible ladder can be listed without being automatically traversable. Use the game's controls for unsupported movement.

On startup and after returning focus, release any held mod keys, then press a fresh command. A held key is synchronized without treating it as a new press. Returning focus or continuing to hold Shift+Delete never activates automatic walking.

Recorded descriptions play automatically only for reviewed cues in the package. Timed scenes follow playback; scenes that wait for the player follow captured scene events. Waiting for Confirm does not create an estimated scene clock. The game's voice/dialogue readiness gates the start of a description. Actual pause or loss of focus pauses audio; choices and menus cancel descriptions and read normally. Deferred dialogue waits for the current recording to finish. F10 reports availability. No description command advances dialogue or selects New Game.

The English description pack has 282 recordings in the newer engaged, conversational Echos delivery, with 188 timed cues and 94 scripted-scene cues. It includes selected cutscenes across Chapters 3–8 alongside the prologue and first two chapters. Selected Options speech, NVDA output, and the navigation beacon have been user-confirmed.
