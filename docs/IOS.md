# iOS: the port on an iPhone, played with a game controller

The same port, built for iPhone: the game code (`src/game/*`), the input
model, the Metal renderer and the movie player are the macOS build's, unchanged;
only the platform layer differs. It runs the **Original** profile (the 4:3 GS
frame, nearest-neighbour, black bars at the sides) and is played **with a game
controller only** (an MFi / extended gamepad such as the Backbone Pro). There
are no touch controls.

> **The built app contains your own disc data.** The build copies your local
> `assets/` and `data/` exports (made from your own disc, docs/STARTUP.md)
> into the app bundle. Never share, upload, AirDrop, commit or publish the
> `.app`, an `.ipa` made from it, or the build folders. Install it only on your
> own devices. `tools/ios/build.sh clean` deletes every copy on the Mac.

## Requirements

- A Mac with Xcode (built and tested with Xcode 27.0, iOS SDK 27.0; the app
  targets iOS 17 and later, arm64).
- An Apple ID signed in to Xcode (Settings > Accounts). A **free personal
  team** works; see "Signing and the 7-day limit".
- An iPhone with Developer Mode on (Settings > Privacy & Security > Developer
  Mode), paired with the Mac (`xcrun devicectl list devices` shows it
  `available (paired)`), unlocked when installing and launching.
- The exported assets in `assets/` (docs/STARTUP.md), as for the macOS build.
- A controller that iOS reports as an extended gamepad (the Backbone Pro, a
  DualShock 4 / DualSense, an Xbox controller, other MFi pads).

## Build, install, launch

All on the Mac, from the repository root:

```sh
tools/ios/build.sh device     # compile, bundle, sign (Apple Development)
tools/ios/build.sh install    # install on the first paired iPhone
tools/ios/build.sh launch     # start it
tools/ios/build.sh run        # all three
```

- `EM_IOS_DEVICE=<UDID or name>` picks the phone (default: the first
  available paired iPhone in `xcrun devicectl list devices`).
- `EM_IOS_TEAM=<team ID>` picks the signing team (default: the team of the
  first "Apple Development" certificate in the keychain).
- `EM_IOS_BUNDLE_ID` (default `com.exterminationport.game`). Change it if
  Apple reports the identifier as unavailable for your team.
- `launch` passes `VAR=value` arguments to the app as environment variables,
  and `--console` keeps the terminal attached to the app's output:
  `tools/ios/build.sh launch --console EM_INPUT_TEST=1 EM_HEADLESS=0`.
- `tools/ios/build.sh fetch extermination.log` copies a file from the app's
  Documents folder on the phone to `build/ios/device-files/`.

The first build compiles the whole game (about 10 s with all cores), copies
about 600 MB of assets and converts the two movies (about 15 s, cached). The
install copies about 680 MB to the phone (about 40 s).

Simulator: `tools/ios/build.sh sim` builds for the iOS Simulator and
`tools/ios/build.sh sim-run` boots `EM_IOS_SIM` (default "iPhone 17 Pro"),
installs and launches it. See "Simulator" for what it can and cannot show.

The Xcode project (`tools/ios/Extermination.xcodeproj`) can also be opened
in Xcode: pick your team under Signing & Capabilities and press Run.

## Signing and the 7-day limit

The build signs with your Apple Development certificate and lets Xcode create
the provisioning profile automatically (`-allowProvisioningUpdates`).

With a **free personal team**:

- **The app stops launching 7 days after it was signed** (the profile's
  lifetime). Run `tools/ios/build.sh run` again to re-sign and reinstall;
  the app's Documents folder (saves, logs) is kept.
- The first time an app from your certificate is launched, iOS refuses it
  until you trust the developer, on the phone: **Settings > General > VPN &
  Device Management > (Developer App) Apple Development: <your Apple ID> >
  Trust "Apple Development: <your Apple ID>" > Trust**. The phone needs an
  internet connection for this check. Deleting the app when it is the only
  one signed by your certificate also removes this trust: after the next
  install, trust again. `build.sh install` and `run` update the app in place
  (Documents is kept) and keep the trust.
- Free teams are limited to 3 installed apps signed this way per device and
  10 new App IDs per 7 days.

A paid Apple Developer Program team removes the 7-day limit (profiles last a
year) and the app-count limits; nothing else changes.

## Controller mapping

The iOS build uses the macOS controller backend unchanged
(`src/platform/mac/em_gamepad_mac.m`, GameController.framework). Buttons are
named by position, so on the Backbone Pro (Xbox-style labels):

| Controller (Backbone Pro label) | PS2 pad |
|---|---|
| A (bottom face button) | Cross |
| B (right face button) | Circle |
| X (left face button) | Square |
| Y (top face button) | Triangle |
| LB / RB (shoulders) | L1 / R1 |
| LT / RT (triggers) | L2 / R2 |
| D-pad | D-pad |
| Menu (right centre button, three lines) | START |
| Options / View (left centre button) | SELECT |
| Left stick click / right stick click | L3 / R3 |
| Left stick / right stick | left / right analog stick |

The sticks' round gate is mapped onto the DualShock's square range
(`em_input_stick_from_round_gate`, radial dead zone 0.12), so a full diagonal
runs as on the PS2. The Backbone / Home button and the screenshot button
belong to iOS and are not used. Triggers and face buttons are digital (the
DualShock 2's pressure values are not reproduced, as on macOS). Rumble is not
sent to the controller (the game's requests are recorded only, as on macOS).

With no extended gamepad connected the screen shows "Connect a controller";
the game keeps running underneath and the message disappears when a
controller connects. Controller connections are logged
(`controller: connected "<name>" (extended gamepad)`).

To check what the game receives, launch with the input log:

```sh
tools/ios/build.sh launch --console EM_INPUT_TEST=1 EM_HEADLESS=0
```

Every change of the pad state prints one line (`pad 0x.... [CROSS START]
L(+0.0,+0.0) R(+0.0,+0.0)`). `EM_HEADLESS=0` is needed because any `EM_*TEST` switch makes a run
headless, and headless runs ignore a physical controller (so a connected pad
cannot disturb the scripted test fixtures).

## How it works

- **Platform layer** (`src/platform/ios/em_platform_ios.m`): UIKit with the
  scene life cycle; one landscape-only view controller (status bar and home
  indicator hidden) whose view is backed by a `CAMetalLayer` at the screen's
  native scale. The idle timer is disabled while the app is active.
- **Threads.** `UIApplicationMain` owns the main thread, so the engine runs on
  its own game thread: `main.c`'s bring-up, compiled for iOS as
  `em_port_main` (Makefile `-Dmain=em_port_main`), then `em_frame_run`. The
  frame loop's own pacing keeps the game's 59.94 Hz tick exactly as on macOS;
  the display's refresh rate (60 or 120 Hz) never drives the logic. The
  layer's device, format and drawable size are set on the main thread; the
  renderer takes drawables and presents from the game thread.
- **Refresh rate.** iOS keeps a presented frame queued for a refresh or two
  before it reaches the screen. On a 60 Hz panel all three drawables were
  then usually in use and the renderer waited up to a whole refresh for the
  next one (measured: about 15 ms of every step on the title screen, and 29
  of 1663 steps overran the 16.68 ms tick). An idle `CADisplayLink` asks a
  ProMotion panel for 120 Hz (`CADisableMinimumFrameDurationOnPhone` in
  Info.plist); its callback does nothing. With it the wait is gone (title
  steps: 0.4 ms wall = 0.4 ms CPU) and 3 of 1545 steps overran (the first
  step, the movie start, and one 7 s step with 0.4 ms of CPU: the thread was
  waiting, most likely parked while the app was inactive; parking is now
  logged as `ios: app inactive ...`). A 59.94 Hz frame shows for two 120 Hz
  refreshes, with one extra refresh about every 17 s, as on a 60 Hz screen.
  Low Power Mode caps the panel at 60 Hz.
- **Background.** When the app stops being active (home, app switcher, a
  call, Control Center) the game thread parks at the top of its next frame
  (`em_window_poll`): no game ticks and no GPU work in the background, which
  iOS forbids. Audio stops with it and resumes on return; a movie that was
  playing resumes where it stopped.
- **Files.** The game reads `assets/...` and writes `data/memcard` (the
  host memory cards, docs/OPTIONS.md) relative to
  the working directory, and the bundle is read-only, so the working
  directory is the app's **Documents** folder: `assets` there is a symlink
  to the bundle's `assets/` (made again at every launch) and `data/` is a real
  folder seeded from the bundle's `data/`. Everything the game writes lands in
  Documents. Launched from the home screen, stdout/stderr go to
  `Documents/extermination.log`; with `--console` they stay on the console.
- **Audio** (`src/audio/ios/em_audio_ios.m`): a RemoteIO AudioUnit with the
  same pull callback contract as macOS (`em_audio.h`: float stereo at the
  game's 48000 Hz, converted by the unit), and an AVAudioSession of category
  Playback, so the game is heard with the ring/silent switch on silent.
- **Renderer**: the macOS Metal backend. On iOS the window handle is the
  `CAMetalLayer` and the drawable size comes from the platform layer
  (`TARGET_OS_IPHONE` in `em_gfx_metal.m`); every buffer and texture already
  uses Shared or Private storage, and the GS frame stage's framebuffer fetch
  is native on every iPhone GPU.
- **Movies.** iOS has no MPEG-2 decoder (the player plays the sound and shows
  no picture). The build converts each `assets/**/*.mov` on the Mac with
  `tools/ios/movie_transcode.m` (AVFoundation): the video track's decoded YUV
  samples re-encoded as HEVC at 1.5x the source bitrate with every sample's
  timestamp, the time scale and the colour tags kept, the PCM audio copied
  sample for sample. The bundle gets the converted file under the same name,
  so the game's movie table is unchanged; the cache is `build/ios/movies/`.
  Measured with `movie_transcode --compare` against the macOS decode of the
  original: E001 2305 of 2305 pictures with identical timestamps, mean error
  0.49 of 255 per channel, PSNR 46.8 dB.
- **Build** (`tools/ios/build.sh`, `tools/ios/Extermination.xcodeproj`):
  `make ios-lib IOS_SDK=iphoneos|iphonesimulator` compiles everything into
  `build/ios/<sdk>/libextermination.a` with the macOS flags (zero
  warnings); the Xcode project only links it (one stub source), copies the
  assets and signs. The app bundles are built in
  `~/Library/Developer/Xcode/DerivedData/<checkout>-ios/`, outside the
  repository, because codesign refuses any bundle inside a file-provider
  folder such as an iCloud-synced `~/Documents` (it tags every `.app` with
  attributes that cannot be removed).

## Simulator

The simulator build launches, plays the converted intro movie (with sound)
and reaches the title screen, and a controller connected to the Mac reaches
it as "Gamepad". It cannot show the game world: the simulator's Metal has no
framebuffer fetch, so the GS frame stage's pipelines fail to build
(`reading from a rendertarget is not supported`) and the first world frame
stops the game. Use the phone for anything past the title.

## Verified (2026-10-04, iPhone Air, iOS 27, Xcode 27.0)

- **macOS** is unchanged: `make all` with zero warnings and
  `EM_STARTUP_TEST=newgame-control` PASS (displacement 9.599849).
- **Simulator** (iPhone 17 Pro): `EM_STARTUP_TEST=skip` PASS (the logos, the
  converted intro movie delivering pictures, the title and its menu), and the
  title captured at 2622x1206 with the 4:3 frame between black bars.
- **iPhone Air**, free personal team: installs, launches from the home screen
  and from `build.sh launch`; the Backbone Pro is reported as
  `controller: connected "Backbone Pro" (extended gamepad)`.
  - `EM_STARTUP_TEST=skip` PASS (62 movie pictures shown before the skip),
    title captured at 2736x1260.
  - `EM_STARTUP_TEST=newgame-control` PASS twice: New Game, the whole AREA11
    opening (1301 locked ticks) and 30 ticks of control, displacement
    9.599849 and the final position (245.475342, 229.891891, 216.987808),
    the same digits as macOS: the game code computes the same on the phone.
  - The controller reaches the game: a visible run of that fixture
    (`EM_HEADLESS=0`) with the Backbone Pro connected failed exactly as on
    a Mac with a pad attached, because the pad's state replaced the
    fixture's scripted input. Run the fixtures headless (the default for
    `EM_*TEST`), which ignores the pad.
  - Not checked by a person yet: each button against the table above (the
    mapping code is the macOS one) and a hand-played New Game. Use the input
    log above while pressing every button, then play: at the title the
    D-pad moves the cursor and Menu (START) or A (CROSS) chooses NEW GAME.

## Frame time

`EM_FRAME_TIMING=<file>` writes one line per step (wall and game-thread CPU
time, the pacing sleep excluded). `EM_STARTUP_TEST=newgame-control`,
headless (offscreen at the panel's size), same build of the game:

| | all steps (3003 / 2999): mean / p50 / p95 / p99 | world steps after 1400: p50 / p95 / p99 | steps over 16.68 ms |
|---|---|---|---|
| iPhone Air (A19 Pro) | 3.82 / 2.43 / 8.76 / 8.97 ms | 6.99 / 8.87 / 9.04 ms | 5 (loads; longest 390 ms) |
| Mac, M1 Pro | 4.01 / 0.94 / 11.34 / 11.68 ms | 7.52 / 11.47 / 11.85 ms | 4 (loads; longest 177 ms) |

The VU1 kernels run on the CPU in both. The phone has about 7.6 ms of
headroom per tick in the world at p99.

## Known limits

- Movies are a high-quality re-encode (above), not the original MPEG-2
  stream; the sound is the original's.
- Controller only: no touch controls, no on-screen pad, no keyboard.
- Rumble, pressure-sensitive buttons and saves are not available (the same
  as macOS today).
- iPhone only (the project's device family); an iPad runs it in iPhone
  compatibility mode.
- The game pauses whenever the app is not active; there is no background
  play.
- A free-team build expires after 7 days (rebuild and reinstall).
