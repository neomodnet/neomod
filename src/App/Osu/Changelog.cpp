// Copyright (c) 2017, PG & 2023-2025, kiwec, All rights reserved.
#include "Changelog.h"

#include "CBaseUIButton.h"
#include "CBaseUIContainer.h"
#include "CBaseUILabel.h"
#include "CBaseUIScrollView.h"
#include "Engine.h"
#include "HUD.h"
#include "Logging.h"
#include "MainMenu.h"
#include "NotificationOverlay.h"
#include "UIBackButton.h"
#include "Osu.h"
#include "SoundEngine.h"
#include "Graphics.h"
#include "Environment.h"
#include "MakeDelegateWrapper.h"
#include "UI.h"

#include "build_timestamp.h"

Changelog::Changelog() : ScreenBackable() {
    this->setPos(-1, -1);
    this->scrollView = new CBaseUIScrollView(-1, -1, 0, 0, "");
    this->scrollView->setVerticalScrolling(true);
    this->scrollView->setHorizontalScrolling(true);
    this->scrollView->setDrawBackground(false);
    this->scrollView->setDrawFrame(false);
    this->scrollView->setScrollResistance(0);
    this->addBaseUIElement(this->scrollView);

    std::vector<CHANGELOG> changelogs;

    CHANGELOG v43_00;
    v43_00.title = "43.00 (" CHANGELOG_TIMESTAMP ")";
    v43_00.changes = {
        R"(- Added initial support for pre-calculating star rating for various mod/rate combinations)",
        R"(  - Currently included: 9 rates between 0.75x and 1.5x, and for each rate, 6 unique mod combinations (valid combos of nomod/HR/HD/EZ))",
        R"(- Added an SDL_gpu-based renderer (Vulkan or D3D12))",
        R"(  - Currently requires "-sdlgpu" or "-gpu" to be passed as a launch option; use "-sdlgpu vulkan" to prefer Vulkan over D3D12 on Windows)",
        R"(- Added WASM releases (https://neosu.net/online/))",
        R"(- Improved OAuth login flow)",
        R"(- Improved user avatar/direct screen thumbnail loading responsiveness)",
        R"(- Made "Click on the orange cursor to continue play!" actually accurate)",
        R"(- Fixed notification toasts getting drawn in the wrong order)",
        R"(- Fixed selected collection jumping around unexpectedly)",
        R"(- Updated BASS to fix some offset issues (e.g. https://osu.ppy.sh/beatmapsets/246733 ))",
    };
    changelogs.push_back(v43_00);

    CHANGELOG v42_01;
    v42_01.title = "42.01 (2026-02-03)";
    v42_01.changes = {
        R"(- Added loudness normalization for SoLoud)",
        R"(- Added -headless launch option to start without a visible window)",
        R"(- Fixed missing 'x' on combo indicator)",
        R"(- Fixed multiple crashes)",
        R"(- Fixed some skin elements failing to load)",
        R"(- Reduced overall memory usage)",
        R"(- Tweaked SoLoud audio offset to match lazer)",
    };
    changelogs.push_back(v42_01);

    CHANGELOG v42_00;
    v42_00.title = "42.00 (2026-01-30)";
    v42_00.changes = {
        R"(- Added Song Browser right click->Export Collection and Export Beatmapset)",
        R"(  - Note that all of the difficulties from the beatmapset in a collection will be archived, not just the difficulty in the collection itself.)",
        R"(- Added button to import McOsu collections/scores/settings (from Steam or by manual folder selection))",
        R"(- Added Linux ARM64 releases)",
        R"(- Visual updates:)",
        R"(  - Fixed carousel buttons' menu-button-background image being incorrectly scaled)",
        R"(  - Updated song browser group/sort label appearance and improved button positioning at various UI scales)",
        R"(  - Improved font rendering performance and fixed characters being cut off around the edges at higher UI scales)",
        R"(  - Added thousands separators for score/combo text everywhere)",
        R"(  - Changed tooltip backgrounds to be semi-transparent)",
        R"(  - Improved fullscreening behavior (probably))",
        R"(- Audio updates:)",
        R"(  - Added asio_freq convar to allow custom sample rates on BASSASIO)",
        R"(  - Added universal_offset_norate convar and options menu option to set a constant, unscaled music offset)",
        R"(  - Improved SoLoud audio offset behavior to better match BASS (fixed desync when changing rates))",
        R"(  - Optimized SoLoud rate changing to help with audio crackling on lower-end setups or small buffer sizes)",
        R"(- Misc. bug fixes:)",
        R"(  - Fixed crashing when trying to enter song browser with an empty osu! folder)",
        R"(  - Fixed crashing when deleting entire collections)",
        R"(  - Fixed map downloads not starting until all map thumbnails are loaded)",
        R"(  - Fixed issues related to handling scores with the exact same submission timestamp)",
        R"(  - Fixed loading of skin elements with mismatched combo/score/hitcircle prefix (from skin.ini) casing on Linux)",
        R"(- Added CPU/RAM usage tracking to VProf (Shift+F11->CTRL+Tab))",
    };
    changelogs.push_back(v42_00);

    CHANGELOG v41_14;
    v41_14.title = "41.14 (2026-01-10)";
    v41_14.changes = {
        R"(- Song browser:)",
        R"(  - Fixed carousel buttons being cut off when using skins with a transparent top/bottom bar)",
        R"(  - Updated carousel to scroll to the currently selected beatmap when changing collection/grouping mode)",
        R"(  - Improved performance further, especially with "Prefer metadata in original language" enabled)",
        R"(- Database/beatmap importing:)",
        R"(  - Added detection for corrupt osu!.db beatmap databases and fixed some other database save/load issues)",
        R"(  - Fixed newly-imported beatmaps not being saved if the database is reloaded (with F5))",
        R"(  - Fixed duplicate neosu_maps.db entries being saved)",
        R"(  - Fixed a crash that occurred when only a single beatmap is installed)",
        R"(  - Fixed an issue causing "Alphanumeric group wasn't initialized!" logs to show up when auto-importing .osz files)",
        R"(- Fixed issues causing fullscreen to not work properly in some cases)",
        R"(- Improved more incorrect/unexpected UI behavior in general (e.g. right click context menu focus for song buttons))",
        R"(- Fixed tablet motion being ignored with raw input enabled)",
        R"(- Fixed mod selector reset/close buttons no longer working)",
    };
    changelogs.push_back(v41_14);

    CHANGELOG v41_13;
    v41_13.title = "41.13 (2026-01-02)";
    v41_13.changes = {
        R"(- Song browser:)",
        R"( - Improved scrolling behavior and performance)",
        R"( - Updated top-left artist/title font for sharper text)",
        R"( - Merged some nice features from McOsu (from @McKay))",
        R"(  - Added Options > Songbrowser > "Song Buttons Velocity Animation)",
        R"(  - Added Options > Songbrowser > "Song Buttons Curved Layout)",
        R"(  - Updated song buttons to have the velocity animation disabled by default)",
        R"(  - Updated "Sort by Artist" to secondarily sort by title)",
        R"(- Score results screen:)",
        R"(  - Better match stable skin behavior for 300/100/50 hiresult images (use non-animated variants if possible))",
        R"(  - Fix incorrect AR/CS/HP/OD being shown for most mod combinations)",
        R"(- In-game scoreboard can now also be sorted by pp, misscount, accuracy or combo))",
        R"(- Multiplayer matches now always have the "No Pausing" mod enabled)",
        R"(- Online Beatmaps screen:)",
        R"(  - Added a checkbox to only see ranked beatmaps)",
        R"(  - Added a hoverable icon for each beatmapset difficulty)",
        R"(  - Added background thumbnails)",
        R"(  - Fixed results after map 100 not getting fetched correctly)",
        R"(  - Improved performance)",
        R"(- Replay viewer now auto-skips sections that the player has skipped)",
        R"(- Fixed issues with mouse scroll events being applied to layered UI elements simultaneously)",
        R"(- Fixed icons (options menu categories, etc.) being offset when changing DPI)",
    };
    changelogs.push_back(v41_13);

    CHANGELOG v41_12;
    v41_12.title = "41.12 (2025-12-27)";
    v41_12.changes = {
        R"(- Added loading spinner for online beatmap search queries)",
        R"(- Fixed map backgrounds being drawn in places they shouldn't be)",
        R"(- Fixed online beatmaps screen not always detecting installed beatmaps)",
        R"(- Fixed some 2B drawing issues (e.g. : https://osu.ppy.sh/beatmapsets/613791#osu/1294898 ))",
        R"(- Fixed some edge case map loading issues (e.g. : https://osu.ppy.sh/beatmapsets/1397110/#osu/2883509 ))",
        R"(- Fixed beatmaps being stuck on old PPv2 versions (bumped database version))",
        R"(- Fixed slightly incorrect PP sorting in the user stats screen)",
        R"(- Improved songbrowser performance when song grouping is active)",
        R"(- Improved background star/PPv2 calc performance, and lowered memory usage)",
    };
    changelogs.push_back(v41_12);

    CHANGELOG v41_11;
    v41_11.title = "41.11 (2025-12-24)";
    v41_11.changes = {
        R"(- Merged updated PPv2 ver. 20251007 from McOsu (thanks @Givikap120))",
        R"(  - See https://osu.ppy.sh/home/news/2025-10-29-performance-points-star-rating-updates)",
        R"(- Added "Boost hitsound volume" option to make hitsounds slightly louder than usual)",
        R"(- Added online beatmap browsing/downloading screen)",
        R"(- Re-added DT/NC/HT labels when speed override is exactly 1.5x/0.75x)",
        R"(- Fixed inaccurate beatmap BPM values)",
        R"(- Fixed song browser up/down arrow key navigation)",
    };
    changelogs.push_back(v41_11);

    CHANGELOG v41_10;
    v41_10.title = "41.10 (2025-12-05)";
    v41_10.changes = {
        R"(- Added -console launch option to open a separate console window on Windows)",
        R"(- Added -info launch option to print OpenGL information)",
        R"(- Added support for media keyboard buttons (play/pause/prev/next))",
        R"(- Fixed being unable to chat during gameplay)",
        R"(- Fixed colors being wrong in fullscreen on Windows on AMD cards)",
        R"(- Fixed mod selection animations)",
        R"(- Fixed restarting a map not keeping nightcore pitch)",
        R"(- Fixed multiple SoLoud specific issues:)",
        R"(  - Clicking/popping when looping a song)",
        R"(  - Rare deadlock/freeze when switching devices)",
        R"(  - Selected audio device not always getting saved)",
        R"(  - Volume sometimes getting set to 100% when switching devices)",
        R"(- Improved login error messages)",
        R"(- Made window resizing smoother)",
        R"(- Updated osu! version to b20251128.1)",
    };
    changelogs.push_back(v41_10);

    CHANGELOG v41_09;
    v41_09.title = "41.09 (2025-11-28)";
    v41_09.changes = {
        R"(- Added "snd_rate_transpose_algorithm" ConVar to adjust rate changer quality)",
        R"(- Added option to draw map backgrounds in discord rich presence)",
        R"(- Added environment variables "SOLOUD_MINIAUDIO_DRIVER" and "SOLOUD_SDL_DRIVER" to control OS-level output backend)",
        R"(  - Mainly relevant to non-Windows users: https://github.com/whrvt/neoloud/commit/5dd9074f159cc1462f3f2146f67b33bd56d54911)",
        R"(- Various performance and audio latency improvements)",
        R"(- Removed combobreak sound when pressing wrong key with Alternate mod)",
        R"(- Renamed Full Alternate to Alternate)",
        R"(- Improved the reliability of avatar images showing up when they're supposed to)",
        R"(- Improved gameplay animation smoothness (position interpolator fixes))",
        R"(- Fixed incorrect beatmap hitsound volumes and samplesets being used)",
        R"(- Fixed some replay playback bugs)",
        R"(- Fixed multiplayer matches failing to start)",
        R"(- Fixed multiplayer room screen missing ready button and freemods checkbox)",
        R"(- Fixed nightcore/daycore not speeding up/slowing down the map with SoLoud)",
        R"(- Fixed multiple songbrowser and collection management issues:)",
        R"(  - Beatmap difficulties are now properly grouped by mapset when searching in "No Grouping" mode)",
        R"(  - All collections are now sorted alphabetically)",
        R"(  - Deleting/renaming collections now works properly)",
        R"(- Fixed audio clicks/pops with certain looped sounds when using SoLoud (e.g. some spinner-spin sounds))",
        R"(- Fixed some options menu/back button layout issues)",
        R"(- Fixed crashing when importing beatmaps during gameplay)",
        R"(- Fixed some cases of memory corruption from certain database import combinations)",
    };
    changelogs.push_back(v41_09);

    CHANGELOG v41_08;
    v41_08.title = "41.08 (2025-11-14)";
    v41_08.changes = {
        R"(- .osk/.osz files in the maps/ and skins/ directories are now automatically imported)",
        R"(- Added option to enable raw keyboard input on Windows)",
        R"(- Added ConVar to select resampler used for SoLoud (snd_soloud_resampler: "point", "linear", "catmull-rom"))",
        R"(- Added "No pausing" mod)",
        R"(- Background thumbnails will no longer show in Discord Rich Presence if they are disabled in-game)",
        R"(- Fixed command convars like "help" and "find" not working)",
        R"(- Fixed options menu focus in multiplayer rooms)",
        R"(- Fixed UI scaling bugs)",
        R"(- Updated SDL to fix Windows 32-bit builds)",
        R"(- Added "fps_limiter_nobusywait" ConVar (don't use it))",
    };
    changelogs.push_back(v41_08);

    CHANGELOG v41_07;
    v41_07.title = "41.07 (2025-11-09)";
    v41_07.changes = {
        R"(- Added crash dump generation support (send us the .dmp file next to neosu.exe if the game crashes))",
        R"(- Added option to use BASSWASAPI event callbacks (similar to https://github.com/ppy/osu-framework/pull/6651 ))",
        R"(- Added the experimental DirectX11 renderer to the regular build configuration (run with the -dx11 launch option to test))",
        R"(- Center cursor when starting a map with FPoSu)",
        R"(- Improved resampling algorithm for SoLoud (now Catmull-Rom vs linear))",
        R"(- Improved performance with raw input enabled on Windows)",
        R"(- Allowed UI scales under 100% (if set from console/config))",
        R"(- Fixed spinner transparency and uncentered spinner approach circles)",
        R"(- Fixed sliderslide sounds never playing)",
        R"(- Fixed font size/scaling not updating when DPI scale changed)",
        R"(- Fixed options menu not being transparent enough)",
        R"(- Fixed large skin back buttons covering up the options menu)",
        R"(- Fixed auto-updater for the third time)",
        R"(- Fixed cursor visibility when pausing autoplay)",
        R"(- Fixed pp strain graphs not updating)",
        R"(- Fixed windowed resolution not getting saved)",
        R"(- Updated FPS counter to use colors based on monitor refresh rate)",
        R"(- Updated osu! version to b20251102.1)",
    };
    changelogs.push_back(v41_07);

    CHANGELOG v41_06;
    v41_06.title = "41.06 (2025-10-30)";
    v41_06.changes = {
        R"(- Added timestamped inputs (aka. "Click Between Frames"))",
        R"(- Added WASAPI exclusive support to SoLoud when using MiniAudio)",
        R"(- Added "Team" leaderboard filter)",
        R"(- Fixed "CursorExpand: 1" skins)",
        R"(- Fixed a crash when switching skins)",
        R"(- Fixed a rare case where music would desync on SoLoud)",
        R"(- Fixed background brightness flashbang)",
        R"(- Fixed NoFail staying on after seeking)",
        R"(- Fixed glitchy seeking/restarting on BASS)",
        R"(- Fixed keyboard not registering after quickly exiting a map before it starts)",
        R"(- Fixed slider hitreg)",
        R"(- Fixed unpause confirmation circle being clickable on the same frame as the continue button (configurable as "unpause_continue_delay")",
        R"(- Improved ingame statistics font rendering at small sizes)",
        R"(- Improved skin support for spinners (still lots more to fix))",
        R"(- Improved window minimize/refocus behavior)",
        R"(- Screenshots taken in letterboxed mode will now be cropped by default)",
        R"(- Updated FPS counter to support frametimes above 10,000 fps)",
    };
    changelogs.push_back(v41_06);

    CHANGELOG v41_05;
    v41_05.title = "41.05 (2025-10-22)";
    v41_05.changes = {
        R"(- Fixed cursor being visible at screen edges)",
        R"(- Fixed random crashes while online)",
        R"(- Fixed some skins not loading properly)",
    };
    changelogs.push_back(v41_05);

    CHANGELOG v41_04;
    v41_04.title = "41.04 (2025-10-21)";
    v41_04.changes = {
        R"(- Added "Singletap" and "No keylock" mods (for 1 key and 3/4 key players))",
        R"(- Merged updated PPv2 ver. 20250306 from McOsu (current))",
        R"(- Added an option/ConVar to disable HP drain entirely (disables online score submission))",
        R"(- Added an experimental DirectX11 renderer backend (for Windows, and Linux through dxvk-native))",
        R"(  - If the build has both OpenGL and DirectX11, OpenGL will be used by default, the -dx11 launch argument can choose DX11)",
        R"(  - Not currently enabled for release builds, due to extra dependencies (to be ironed out in the future!))",
        R"(- Fixed ingame total stars/total pp statistics not updating)",
        R"(- Added file logging (in logs/ subdirectory); improved logging performance)",
        R"(- Improved text rendering performance (e.g. statistics, song browser info))",
    };
    changelogs.push_back(v41_04);

    CHANGELOG v41_03;
    v41_03.title = "41.03 (2025-09-22)";
    v41_03.changes = {
        R"(- Added Escape confirmation for leaving multiplayer room)",
        R"(- Changed some default settings to be less annoying)",
        R"(- Fixed accuracy-restricting mods not working)",
        R"(- Fixed beatmap downloads not working)",
        R"(- Fixed chat not switching to new channels automatically)",
        R"(- Fixed incorrect number of search matches)",
        R"(- Fixed main menu backrgound showing missing texture)",
        R"(- Fixed own messages being shown in chat ticker)",
        R"(- Fixed some HiDPI issues)",
        R"(- Moved settings to more intuitive locations)",
        R"(- Multiplayer matches will now show [no map] instead of a red username)",
        R"(- Reduced status change notification spam)",
        R"(- Scaled setting icons to not get hidden behind the back button in some skins)",
    };
    changelogs.push_back(v41_03);

    CHANGELOG v41_02;
    v41_02.title = "41.02 (2025-09-20)";
    v41_02.changes = {
        R"(- Added "Open Beatmap Folder" context menu option)",
        R"(- Added transition animation for main menu background)",
        R"(- Fixed beatmap combo color setting not getting imported)",
        R"(- Fixed pressing F8/F9 while offline not opening settings menu)",
        R"(- Fixed score submission warning showing more than once while seeking)",
        R"(- Moved "Online" section to the top of the settings menu)",
        R"(- Score filtering method is now saved across restarts)",
        R"(- Shift-hovering a score now shows which client created it)",
        R"(- More rendering performance optimizations)",
    };
    changelogs.push_back(v41_02);

    CHANGELOG v41_01;
    v41_01.title = "41.01 (2025-09-17)";
    v41_01.changes = {
        R"(- Fixed multiple easy-to-trigger crashes while logged in)",
        R"(- Fixed online leaderboards from ez-pp.farm)",
        R"(- Fixed some song select performance issues with new user profiles)",
        R"(- Fixed some audio-related issues)",
        R"(  - Users on very low end PCs with audio dropouts can try the "-sound soloud-nt" (or "-sound bass") executable launch argument)",
        R"(- Added a new "pen_input" ConVar as a way to potentially work around tablet issues on Wayland)",
        R"(  - On by default, you probably don't need to touch this)",
    };
    changelogs.push_back(v41_01);

    CHANGELOG v41_00;
    v41_00.title = "41.00 (2025-09-17)";
    v41_00.changes = {
        R"(- Added leaderboard filters)",
        R"(- Added support for OAuth login servers)",
        R"(- Added ability for servers to override game settings)",
        R"(- Added ability for skins to override game settings)",
        R"(- Added support for tablet/pen input without OpenTabletDriver (e.g. native tablet drivers))",
        R"(- Changed default server to neosu.net)",
        R"(- Changed default sound engine to SoLoud)",
        R"(  - BASS can still be used from the neosu-BASS shortcut, or running with "-sound bass" appended to launch options)",
        R"(- Re-added integration for file/url associations on Windows)",
        R"(- Added support for Discord integration on Linux (thanks @NelloKudo))",
        R"(- Fixed crashes when loading song folders directly without an osu!.db)",
        R"(- Fixed crash when reloading songs list)",
        R"(- Fixed leaderboards pp not using correct AR/OD values)",
        R"(- Fixed multiplayer mod selection menu)",
        R"(- Fixed user presence/stats getting requested too often)",
        R"(- More rendering performance optimizations)",
    };
    changelogs.push_back(v41_00);

    CHANGELOG v40_03;
    v40_03.title = "40.03 (2025-08-27)";
    v40_03.changes = {
        R"(- Fixed certain maps failing to load correctly (hitsound parsing problems))",
        R"(- Fixed HP drain and geki/katu hit results (new combo always started at 2))",
        R"(- Small audio improvements and fixes (for BASS and SoLoud))",
    };
    changelogs.push_back(v40_03);

    CHANGELOG v40_02;
    v40_02.title = "40.02 (2025-08-25)";
    v40_02.changes = {
        R"(- Added setting to display map titles in their original language)",
        R"(- Added support for per-hitobject hitsounds, sample sets and volume)",
        R"(- Fixed pause loop playing after quitting a map)",
        R"(- Fixed Shift+F2 not working in song browser)",
        R"(- Fixed seeking or restarting sometimes resulting in bugged hitobjects)",
        R"(- Fixed speed mods not always getting applied)",
        R"(- Fixed strain graphs in seeking overlay)",
        R"(- Improved performance)",
        R"(- Reduced input lag by 1 frame)",
        R"(- Seeking during gameplay will now enable NoFail)",
    };
    changelogs.push_back(v40_02);

    CHANGELOG v40_01;
    v40_01.title = "40.01 (2025-08-16)";
    v40_01.changes = {
        R"(- Added cursor smoke)",
        R"(- Added a new command line launch option to enable global MSAA)",
        R"(  - Run neosu with "-aa 2" (in the "Target" field for the shortcut on Windows) for 2x MSAA; up to 16x is supported)",
        R"(- Fixed chat toasts always being visible during gameplay)",
        R"(- Fixed beatmapset difficulties not always being sorted by difficulty)",
        R"(- Fixed tablet/mouse breakage)",
        R"(- Improved the quality of the main menu cube)",
        R"(- Updated osu! version to b20250815)",
    };
    changelogs.push_back(v40_01);

    CHANGELOG v40_00;
    v40_00.title = "40.00 (2025-08-09)";
    v40_00.changes = {
        R"(- Added chat ticker)",
        R"(- Added chat/screenshot/status notifications)",
        R"(- Re-added local user stats)",
        R"(- Re-added FPoSu 3D skybox support)",
        R"(- Fixed avatar downloads getting stuck)",
        R"(- Fixed crash when skin is missing spinner sounds)",
        R"(- Fixed circles not being clickable while spinner is active)",
        R"(- Fixed extended chat console flickering)",
        R"(- Switched to SDL3 platform backend)",
    };
    changelogs.push_back(v40_00);

    CHANGELOG v39_02;
    v39_02.title = "39.02 (2025-08-02)";
    v39_02.changes = {
        R"(- Fixed crash when saving score (and others))",
        R"(- Fixed auto-updater failing to update)",
    };
    changelogs.push_back(v39_02);

    CHANGELOG v39_01;
    v39_01.title = "39.01 (2025-08-02)";
    v39_01.changes = {
        R"(- Added "bleedingedge" release stream)",
        R"(- Added a "keep me signed in" checkbox next to the login button)",
        R"(- Added ability to import McOsu/neosu/stable collections and scores from database files)",
        R"(- Adjusted hit windows to match stable)",
        R"(- Added fps_max_menu setting, to limit framerate differently in menus and gameplay)",
        R"(- Added customizable chat word ignore list)",
        R"(- Added setting to keep chat visible during gameplay)",
        R"(- Added setting to hide spectator list)",
        R"(- Added setting to automatically send currently played map to spectators)",
        R"(- Fixed collections not displaying all available maps)",
        R"(- Fixed keys being counted before first hitobject)",
        R"(- Fixed "RawInputBuffer" option on Windows, should work much more reliably)",
        R"(- Fixed search text being hidden under some skin elements)",
    };
    changelogs.push_back(v39_01);

    CHANGELOG v39_00;
    v39_00.title = "39.00 (2025-07-27)";
    v39_00.changes = {
        R"(- Added the SoLoud audio backend, as an alternative to BASS)",
        R"(  - Currently selectable by passing "-sound soloud" to the launch arguments, Windows builds have shortcuts provided to do so)",
        R"(  - Latency should be much lower than regular BASS)",
        R"(  - BASS will remain as default for the foreseeable future, but testing is highly encouraged)",
        R"(  - No exclusive mode support or loudness normalization yet)",
        R"(- Added support for parsing McOsu "scores.db" databases for local scores, just put the scores.db in the same folder as neosu)",
        R"(- Added initial support for loading beatmaps without an osu!.db (raw loading))",
        R"(  - Used either when an osu!.db doesn't exist in the selected osu! folder, or when the "osu_database_enabled" ConVar is set to 0)",
        R"(  - The osu folder should be set somewhere that contains "Skins/" and "Songs/" as subfolders)",
        R"(- Added "Max Queued Frames" slider to options menu (and associated renderer logic), for lower input lag)",
        R"(- Crop screenshots to the internal/letterboxed osu! resolution, instead of keeping the black borders)",
        R"(- Added "Sort Skins Alphabetically" option, for sorting skins in a different order (ignoring prefixes))",
        R"(- Added support for filtering by "creator=" in the song browser)",
        R"(- Re-added support for FPoSu in multiplayer matches)",
        R"(- Implemented support for canceling login attempts by right clicking loading button)",
        R"(  - Login request timeouts should usually be detected and automatically canceled, but this may not always be possible)",
        R"(- Stop storing passwords as plaintext)",
        R"(- Optimized song browser initial load time)",
        R"(- Optimized ingame HUD rendering performance)",
        R"(- Fixed networking-related crashes)",
        R"(- Fixed many memory leaks and reduced memory usage)",
    };
    changelogs.push_back(v39_00);

    CHANGELOG v38_00;
    v38_00.title = "38.00 (2025-07-13)";
    v38_00.changes = {
        R"_(- Added user list interface (F9, aka. "Extended Chat Console"))_",
        R"(- Added support for older osu!.db versions (~2014))",
        R"(- Fixed beatmap downloads sometimes not getting saved correctly)",
        R"(- Fixed LOGOUT packet triggering errors on some servers)",
        R"(- Greatly optimized skin dropdown list performance, for users with many skins)",
        R"(- Improved clarity of beatmap background thumbnails (enabled mipmapping to reduce aliasing))",
        R"(- Added CJK/Unicode font support (skin dropdowns, chat, other text))",
        R"(  - Missing font fallback list is currently hardcoded, will be expanded and improved in the future)",
        R"(- Added support for raw input/sensitivity on Linux)",
        R"_(- Added option to disable cursor confinement during gameplay - "Confine Cursor (NEVER)")_",
        R"(- Allowed left/right CTRL/ALT/SHIFT keys to be mapped independently)",
        R"(- Fixed letterboxed mode cutting off GUI/HUD elements strangely)",
        R"(- Fixed crashing on partially corrupt scores databases)",
        R"(- Fixed rate change slider having a much smaller range than it should)",
        R"(- Fixed leaderboard scores showing the wrong AR/OD for non-HT/DT rate-changed plays)",
        R"(- Updated osu! version to b20250702.1)",
        // Spectating is not included in this release (feature cvar: cv::enable_spectating)
    };
    changelogs.push_back(v38_00);

    CHANGELOG v37_03;
    v37_03.title = "37.03 (2025-03-09)";
    v37_03.changes = {
        R"(- Added missing mode-osu.png)",
        R"(- Fixed search bar not being visible on some resolutions)",
        R"(- Updated osu! version to b20250309.2)",
    };
    changelogs.push_back(v37_03);

    CHANGELOG v37_02;
    v37_02.title = "37.02 (2025-01-28)";
    v37_02.changes = {
        R"(- Added support for mode-osu skin element)",
        R"(- Fixed "Draw Background Thumbnails in SongBrowser" setting)",
        R"(- Fixed UR bar scaling setting being imported incorrectly)",
        R"(- Improved rendering of scores list)",
        R"(- Removed back button height limit)",
    };
    changelogs.push_back(v37_02);

    CHANGELOG v37_01;
    v37_01.title = "37.01 (2025-01-27)";
    v37_01.changes = {
        R"(- Added support for back button skin hacks)",
        R"(- Added windowed_resolution convar (window size will now be saved))",
        R"(- Changed user card to be compatible with more skins)",
        R"(- Improved rendering of grade/ranking skin elements)",
        R"(- Improved rendering of song info text/background)",
    };
    changelogs.push_back(v37_01);

    CHANGELOG v37_00;
    v37_00.title = "37.00 (2025-01-26)";
    v37_00.changes = {
        R"(- Added support for song browser skin elements)",
        R"(- Added file handlers for .osk, .osr, and .osz formats)",
        R"(- Fixed interface animations not playing at the correct speed)",
        R"(- Fixed crash when starting beatmap with live pp/stars enabled)",
        R"(- Fixed dropdowns in the options menu (such as skin select))",
        R"(- Updated osu! folder detection)",
        R"(- Updated osu! version to b20250122.1)",
    };
    changelogs.push_back(v37_00);

    CHANGELOG v36_06;
    v36_06.title = "36.06 (2025-01-08)";
    v36_06.changes = {
        R"(- Added support for new osu!.db format)",
    };
    changelogs.push_back(v36_06);

    CHANGELOG v36_05;
    v36_05.title = "36.05 (2024-12-26)";
    v36_05.changes = {
        R"(- Fixed crash when loading old scores)",
        R"(- Fixed crash when alt-tabbing)",
        R"(- Improved Discord integration)",
        R"(- Linux: Fixed osu!stable settings import)",
    };
    changelogs.push_back(v36_05);

    CHANGELOG v36_04;
    v36_04.title = "36.04 (2024-12-11)";
    v36_04.changes = {
        R"(- Settings will now auto-import from osu!stable on first launch)",
        R"(- You can also re-import them manually from the settings menu)",
        R"(- Fixed Discord integration being retarded)",
        R"(- Linux: Fixed crash when osu!folder wasn't set)",
    };
    changelogs.push_back(v36_04);

    CHANGELOG v36_03;
    v36_03.title = "36.03 (2024-11-24)";
    v36_03.changes = {
        R"(- Added checkbox to disable auto-updater)",
        R"(- Added Discord Rich Presence integration)",
        R"(- Fixed live pp calculations)",
        R"(- Updated neosu icon)",
    };
    changelogs.push_back(v36_03);

    CHANGELOG v36_02;
    v36_02.title = "36.02 (2024-11-16)";
    v36_02.changes = {
        R"(- Added toast notifications)",
        R"(- Updated star/pp algorithms (thanks @Khangaroo and @McKay))",
        R"(- Updated osu! version to b20241030)",
    };
    changelogs.push_back(v36_02);

    CHANGELOG v36_01;
    v36_01.title = "36.01 (2024-09-25)";
    v36_01.changes = {
        R"(- Fixed crash when opening song browser)",
    };
    changelogs.push_back(v36_01);

    CHANGELOG v36_00;
    v36_00.title = "36.00 (2024-09-24)";
    v36_00.changes = {
        R"(- Added "Actual Flashlight" mod)",
        R"(- Added "Rank Achieved" sort option)",
        R"(- Added BPM song grouping)",
        R"(- Added keybind to open skin selection menu)",
        R"(- Added option to change pitch based on hit accuracy)",
        R"(- Added retry/watch buttons on score screen)",
        R"(- Added slider instafade setting)",
        R"(- Fixed "Personal Best" score button not being clickable)",
        R"(- Fixed local scores not saving avatar)",
        R"(- Fixed song buttons not always displaying best grade)",
        R"(- Fixed map settings (like local offset) not getting saved)",
        R"(- Fixed Nightcore getting auto-selected instead of Double Time in some cases)",
        R"(- Improved loudness normalization)",
        R"(- Merged pp calculation optimizations from McOsu)",
        R"(- Optimized database loading)",
        R"(- Removed custom HP drain (use nofail instead))",
        R"(- Removed mandala mod, random mod)",
        R"(- Removed rosu-pp)",
        R"(- Updated osu! version to b20240820.1)",
        R"(- Linux: Fixed "Skin.ini" failing to load)",
        R"(- Windows: Fixed collections/maps/scores not getting saved)",
    };
    changelogs.push_back(v36_00);

    CHANGELOG v35_09;
    v35_09.title = "35.09 (2024-07-03)";
    v35_09.changes = {
        R"(- Added keybind to toggle current map background)",
        R"(- Fixed beatmaps not getting selected properly in some cases)",
        R"(- Fixed crash when osu! folder couldn't be found)",
        R"(- Fixed mod selection not being restored properly)",
        R"(- Fixed skin selection menu being drawn behind back button)",
    };
    changelogs.push_back(v35_09);

    CHANGELOG v35_08;
    v35_08.title = "35.08 (2024-06-28)";
    v35_08.changes = {
        R"(- Added ability to import .osk and .osz files (drop them onto the neosu window))",
        R"(- Added persistent map database (downloaded or imported maps stay after restarting the game))",
        R"(- Added skin folder)",
        R"(- Now publishing 32-bit releases (for PCs running Windows 7))",
        R"(- Fixed songs failing to restart)",
    };
    changelogs.push_back(v35_08);

    CHANGELOG v35_07;
    v35_07.title = "35.07 (2024-06-27)";
    v35_07.changes = {
        R"(- Added sort_skins_by_name convar)",
        R"(- Added setting to prevent servers from replacing the main menu logo)",
        R"(- Chat: added missing chat commands)",
        R"(- Chat: added missing keyboard shortcuts)",
        R"(- Chat: added support for user links)",
        R"(- Chat: improved map link support)",
        R"(- Fixed freeze when switching between songs in song browser)",
        R"(- Lowered audio latency for default (not ASIO/WASAPI) output)",
    };
    changelogs.push_back(v35_07);

    CHANGELOG v35_06;
    v35_06.title = "35.06 (2024-06-17)";
    v35_06.changes = {
        R"(- Added cursor trail customization settings)",
        R"(- Added instafade checkbox)",
        R"(- Added more UI sounds)",
        R"(- Added submit_after_pause convar)",
        R"(- Chat: added support for /me command)",
        R"(- Chat: added support for links)",
        R"(- Chat: added support for map links (auto-downloads))",
        R"(- Chat: added support for multiplayer invite links)",
        R"(- FPS counter will now display worst frametime instead of current frametime)",
        R"(- Improved song browser performance)",
        R"(- Skins are now sorted alphabetically, ignoring meme characters)",
        R"(- Unlocked osu_drain_kill convar)",
    };
    changelogs.push_back(v35_06);

    CHANGELOG v35_05;
    v35_05.title = "35.05 (2024-06-13)";
    v35_05.changes = {
        R"(- Fixed Artist/Creator/Title sorting to be in A-Z order)",
        R"(- Improved sound engine reliability)",
        R"(- Removed herobrine)",
    };
    changelogs.push_back(v35_05);

    CHANGELOG v35_04;
    v35_04.title = "35.04 (2024-06-11)";
    v35_04.changes = {
        R"(- Changed "Open Skins folder" button to open the currently selected skin's folder)",
        R"(- Fixed master volume control not working on exclusive WASAPI)",
        R"(- Fixed screenshots failing to save)",
        R"(- Fixed skins with non-ANSI folder names failing to open on Windows)",
        R"(- Fixed sliderslide and spinnerspin sounds not looping)",
        R"(- Improved sound engine reliability)",
        R"(- Re-added strain graphs)",
        R"(- Removed sliderhead fadeout animation (set osu_slider_sliderhead_fadeout to 1 for old behavior))",
    };
    changelogs.push_back(v35_04);

    CHANGELOG v35_03;
    v35_03.title = "35.03 (2024-06-10)";
    v35_03.changes = {
        R"(- Added SoundEngine auto-restart settings)",
        R"(- Disabled FPoSu noclip by default)",
        R"(- Fixed auto mod staying on after Ctrl+clicking a map)",
        R"(- Fixed downloads sometimes failing on Windows)",
        R"(- Fixed recent score times not being visible in leaderboards)",
        R"(- Fixed restarting map while watching a replay)",
        R"(- Improved sound engine reliability)",
        R"(- Re-added win_snd_wasapi_exclusive convar)",
        R"(- User mods will no longer change when watching a replay or joining a multiplayer room)",
    };
    changelogs.push_back(v35_03);

    CHANGELOG v35_02;
    v35_02.title = "35.02 (2024-06-08)";
    v35_02.changes = {
        R"(- Fixed online leaderboards displaying incorrect values)",
    };
    changelogs.push_back(v35_02);

    CHANGELOG v35_01;
    v35_01.title = "35.01 (2024-06-08)";
    v35_01.changes = {
        R"(- Added ability to get spectated)",
        R"(- Added use_https convar (to support plain HTTP servers))",
        R"(- Added restart_sound_engine_before_playing convar ("fixes" sound engine lagging after a while))",
        R"(- Fixed chat channels being unread after joining)",
        R"(- Fixed flashlight mod)",
        R"(- Fixed FPoSu mode)",
        R"(- Fixed playfield borders not being visible)",
        R"(- Fixed sound engine not being restartable during gameplay or while paused)",
        R"(- Fixed missing window icon)",
        R"(- Hid password cvar from console command list)",
        R"(- Now making 64-bit MSVC builds)",
        R"(- Now using rosu-pp for some pp calculations)",
        R"(- Removed DirectX, Software, Vulkan renderers)",
        R"(- Removed OpenCL support)",
        R"(- Removed user stats screen)",
    };
    changelogs.push_back(v35_01);

    CHANGELOG v35_00;
    v35_00.title = "35.00 (2024-05-05)";
    v35_00.changes = {
        R"(- Renamed 'McOsu Multiplayer' to 'neosu')",
        R"(- Added option to normalize loudness across songs)",
        R"(- Added server logo to main menu button)",
        R"(- Added instant_replay_duration convar)",
        R"(- Added ability to remove beatmaps from osu!stable collections (only affects neosu))",
        R"(- Allowed singleplayer cheats when the server doesn't accept score submissions)",
        R"(- Changed scoreboard name color to red for friends)",
        R"(- Changed default instant replay key to F2 to avoid conflicts with mod selector)",
        R"(- Disabled score submission when mods are toggled mid-game)",
        R"(- Fixed ALT key not working on linux)",
        R"(- Fixed chat layout updating while chat was hidden)",
        R"(- Fixed experimental mods not getting set while watching replays)",
        R"(- Fixed FPoSu camera not following cursor while watching replays)",
        R"(- Fixed FPoSu mod not being included in score data)",
        R"(- Fixed level bar always being at 0%)",
        R"(- Fixed music pausing on first song database load)",
        R"(- Fixed not being able to adjust volume while song database was loading)",
        R"(- Fixed pause button not working after cancelling database load)",
        R"(- Fixed replay playback starting too fast)",
        R"(- Fixed restarting SoundEngine not kicking the player out of play mode)",
        R"(- Improved audio engine)",
        R"(- Improved overall stability)",
        R"(- Optimized song database loading speed (a lot))",
        R"(- Optimized collection processing speed)",
        R"(- Removed support for the Nintendo Switch)",
        R"(- Updated protocol version)",
    };
    changelogs.push_back(v35_00);

    CHANGELOG v34_10;
    v34_10.title = "34.10 (2024-04-14)";
    v34_10.changes = {
        R"(- Fixed replays not saving/submitting correctly)",
        R"(- Fixed scores, collections and stars/pp cache not saving correctly)",
    };
    changelogs.push_back(v34_10);

    CHANGELOG v34_09;
    v34_09.title = "34.09 (2024-04-13)";
    v34_09.changes = {
        R"(- Added replay viewer)",
        R"(- Added instant replay (press F1 while paused or after failing))",
        R"(- Added option to disable in-game scoreboard animations)",
        R"(- Added start_first_main_menu_song_at_preview_point convar (it does what it says))",
        R"(- Added extra slot to in-game scoreboard)",
        R"(- Fixed hitobjects being hittable after failing)",
        R"(- Fixed login packet sending incorrect adapters list)",
        R"(- Removed VR support)",
        R"(- Updated protocol and database version to b20240411.1)",
    };
    changelogs.push_back(v34_09);

    CHANGELOG v34_08;
    v34_08.title = "34.08 (2024-03-30)";
    v34_08.changes = {
        R"(- Added animations for the in-game scoreboard)",
        R"(- Added option to always pick Nightcore mod first)",
        R"(- Added osu_animation_speed_override cheat convar (code by Givikap120))",
        R"(- Added flashlight_always_hard convar to lock flashlight radius to the 200+ combo size)",
        R"(- Allowed scores to submit when using mirror mods)",
        R"(- Now playing a random song on game launch)",
        R"(- Small UI improvements)",
        R"(- Updated protocol and database version to b20240330.2)",
    };
    changelogs.push_back(v34_08);

    CHANGELOG v34_07;
    v34_07.title = "34.07 (2024-03-28)";
    v34_07.changes = {
        R"(- Added Flashlight mod)",
        R"(- Fixed a few bugs)",
    };
    changelogs.push_back(v34_07);

    CHANGELOG v34_06;
    v34_06.title = "34.06 (2024-03-12)";
    v34_06.changes = {
        R"(- Fixed pausing not working correctly)",
    };
    changelogs.push_back(v34_06);

    CHANGELOG v34_05;
    v34_05.title = "34.05 (2024-03-12)";
    v34_05.changes = {
        R"(- Added support for ASIO output)",
        R"(- Disabled ability to fail when using Relax (online))",
        R"(- Enabled non-vanilla mods (disables score submission))",
        R"(- Fixed speed modifications not getting applied to song previews when switching songs)",
        R"(- Improved Nightcore/Daycore audio quality)",
        R"(- Improved behavior of speed modifier mod selection)",
        R"(- Improved WASAPI output latency)",
    };
    changelogs.push_back(v34_05);

    CHANGELOG v34_04;
    v34_04.title = "34.04 (2024-03-03)";
    v34_04.changes = {
        R"(- Fixed replays having incorrect tickrate when using speed modifying mods (again))",
        R"(- Fixed auto-updater not working)",
        R"(- Fixed scores getting submitted for 0-score plays)",
        R"(- Replay frames will now be written for slider ticks/ends)",
        R"(- When using Relax, replay frames will now also be written for every hitcircle)",
        R"(- Improved beatmap database loading performance)",
        R"(- Improved build process)",
    };
    changelogs.push_back(v34_04);

    CHANGELOG v34_03;
    v34_03.title = "34.03 (2024-02-29)";
    v34_03.changes = {
        R"(- Fixed replays having incorrect tickrate when using speed modifying mods)",
    };
    changelogs.push_back(v34_03);

    CHANGELOG v34_02;
    v34_02.title = "34.02 (2024-02-27)";
    v34_02.changes = {
        R"(- Added score submission (for servers that allow it via the x-mcosu-features header))",
        R"(- Added [quit] indicator next to users who quit a match)",
        R"(- Made main menu shuffle through songs instead of looping over the same one)",
        R"(- Fixed "No records set!" banner display)",
        R"(- Fixed "Server has restarted" loop after a login error)",
        R"(- Fixed chat being force-hid during breaks and before map start)",
        R"(- Fixed chat not supporting expected keyboard navigation)",
        R"(- Fixed text selection for password field)",
        R"(- Fixed version header not being sent on login)",
    };
    changelogs.push_back(v34_02);

    CHANGELOG v34_01;
    v34_01.title = "34.01 (2024-02-18)";
    v34_01.changes = {
        R"(- Added ability to close chat channels with /close)",
        R"(- Added "Force Start" button to avoid host accidentally starting the match)",
        R"(- Disabled force start when there are only two players in the room)",
        R"(- Made tab button switch between endpoint, username and password fields in options menu)",
        R"(- Fixed not being visible on peppy client when starting a match as host)",
        R"(- Fixed Daycore, Nightcore and Perfect mod selection)",
        R"(- Fixed mod selection sound playing twice)",
        R"(- Fixed client not realizing when it gets kicked from a room)",
        R"(- Fixed room settings not updating immediately)",
        R"(- Fixed login failing with "No account by the username '?' exists.")",
        R"(- Removed Steam Workshop button)",
    };
    changelogs.push_back(v34_01);

    CHANGELOG v34_00;
    v34_00.title = "34.00 (2024-02-16)";
    v34_00.changes = {
        R"(- Added ability to connect to servers using Bancho protocol)",
        R"(- Added ability to see, join and play in multiplayer rooms)",
        R"(- Added online leaderboards)",
        R"(- Added chat)",
        R"(- Added automatic map downloads for multiplayer rooms)",
        R"()",
        R"()",
        R"()",
        R"()",
        R"()",
    };
    changelogs.push_back(v34_00);

    this->addAllChangelogs(std::move(changelogs));
}

Changelog::~Changelog() { this->changelogs.clear(); }

namespace {
class ChangelogLabel final : public CBaseUIButton {
    NOCOPY_NOMOVE(ChangelogLabel)
   public:
    ChangelogLabel(UString text) : CBaseUIButton(0, 0, 0, 0, "", std::move(text)) {}
    ~ChangelogLabel() override = default;

    void draw() override {
        if(this->bVisible && this->isMouseInside()) {
            g->setColor(0x3fffffff);

            const int margin = 0;
            const int marginX = margin + 10;
            g->fillRect(this->getPos().x - marginX, this->getPos().y - margin, this->getSize().x + marginX * 2,
                        this->getSize().y + margin * 2);
        }

        if(!this->bVisible) return;

        g->setColor(this->textColor);
        g->pushTransform();
        {
            g->translate((int)(this->getPos().x + this->getSize().x / 2.0f - this->fStringWidth / 2.0f),
                         (int)(this->getPos().y + this->getSize().y / 2.0f + this->fStringHeight / 2.0f));
            g->drawString(this->font, this->getText());
        }
        g->popTransform();
    }

    bool isMouseInside() override {
        return CBaseUIButton::isMouseInside() && !ui->getChangelog()->backButton->isMouseInside();
    }
};
}  // namespace

void Changelog::addAllChangelogs(std::vector<CHANGELOG> &&logtexts) {
    auto changelogs = std::move(logtexts);
    for(int i = 0; i < changelogs.size(); i++) {
        CHANGELOG_UI changelog;

        // title label
        changelog.title = new CBaseUILabel(0, 0, 0, 0, "", changelogs[i].title);
        if(i == 0)
            changelog.title->setTextColor(0xff00ff00);
        else
            changelog.title->setTextColor(0xff888888);

        changelog.title->setSizeToContent();
        changelog.title->setDrawBackground(false);
        changelog.title->setDrawFrame(false);

        this->scrollView->container.addBaseUIElement(changelog.title);

        // changes
        for(auto &&changeText : changelogs[i].changes) {
            CBaseUIButton *change = new ChangelogLabel(std::move(changeText));
            change->setClickCallback(SA::MakeDelegate<&Changelog::onChangeClicked>(this));

            if(i > 0) change->setTextColor(0xff888888);

            change->setSizeToContent();
            change->setDrawBackground(false);
            change->setDrawFrame(false);

            changelog.changes.push_back(change);

            this->scrollView->container.addBaseUIElement(change);
        }

        this->changelogs.push_back(changelog);
    }
}

CBaseUIContainer *Changelog::setVisible(bool visible) {
    ScreenBackable::setVisible(visible);

    if(this->bVisible) this->updateLayout();

    return this;
}

void Changelog::updateLayout() {
    ScreenBackable::updateLayout();

    const float dpiScale = Osu::getUIScale();

    this->setSize(osu->getVirtScreenSize() + vec2(2, 2));
    this->scrollView->setSize(osu->getVirtScreenSize() + vec2(2, 2));

    float yCounter = 0;
    for(const CHANGELOG_UI &changelog : this->changelogs) {
        changelog.title->onResized();  // HACKHACK: framework, setSizeToContent() does not update string metrics
        changelog.title->setSizeToContent();

        yCounter += changelog.title->getSize().y;
        changelog.title->setRelPos(15 * dpiScale, yCounter);
        /// yCounter += 10 * dpiScale;

        for(CBaseUIButton *change : changelog.changes) {
            change->onResized();  // HACKHACK: framework, setSizeToContent() does not update string metrics
            change->setSizeToContent();
            change->setSizeY(change->getSize().y * 2.0f);
            yCounter += change->getSize().y /* + 13 * dpiScale*/;
            change->setRelPos(35 * dpiScale, yCounter);
        }

        // gap to previous version
        yCounter += 65 * dpiScale;
    }

    this->scrollView->setScrollSizeToContent(15 * dpiScale);
}

void Changelog::onBack() { ui->setScreen(ui->getMainMenu()); }

void Changelog::onChangeClicked(CBaseUIButton *button) {
    const UString &changeTextMaybeContainingClickableURL = button->getText();

    const int maybeURLBeginIndex = changeTextMaybeContainingClickableURL.find("http");
    if(maybeURLBeginIndex != -1 && changeTextMaybeContainingClickableURL.find("://") != -1) {
        UString url = changeTextMaybeContainingClickableURL.substr(maybeURLBeginIndex);
        if(url.length() > 0 && url[url.length() - 1] == L')') url = url.substr(0, url.length() - 1);

        debugLog("url = {:s}", url.toUtf8());

        ui->getNotificationOverlay()->addNotification("Opening browser, please wait ...", 0xffffffff, false, 0.75f);
        env->openURLInDefaultBrowser(url.toUtf8());
    }
}
