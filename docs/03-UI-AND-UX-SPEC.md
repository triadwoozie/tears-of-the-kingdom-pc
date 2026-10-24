# TotK-Recomp UI & UX Specification: Desktop Studio & Overlay

## 1. Overview & UI Design Philosophy

**`TotK-Recomp`** features a unified user interface built using **SDL3** and **Dear ImGui**, styled with a custom modern dark aesthetic characterized by:
- Deep charcoal backgrounds (`#121316`, `#1a1c23`) with subtle borders (`#2d3139`).
- Electric cyan/blue accents (`#00e5ff`, `#2979ff`) and emerald green indicators (`#00e676`).
- Rounded corners on frames, buttons, and popups (`FrameRounding = 6.0f`, `WindowRounding = 8.0f`).
- Smooth visual animations for progress bars and state transitions.
- Seamless dual-mode operation:
  1. **Pre-Launch Configuration Studio:** Displayed when launching without game arguments or when ROM setup is incomplete.
  2. **In-Game Overlay:** Summoned at any time via `F1` on keyboard or `Minus + Plus` on controllers.

---

## 2. Pre-Launch Configuration Studio

```
┌────────────────────────────────────────────────────────────────────────┐
│  TotK-Recomp Studio                                   [ - ] [ □ ] [ X ]│
├────────────────────────────────────────────────────────────────────────┤
│  [ ROM & Keys ]    [ Controller Studio ]    [ Graphics ]    [ Audio ]  │
├────────────────────────────────────────────────────────────────────────┤
│                                                                        │
│  Base Game (.nsp / .xci):                                              │
│  [/path/to/Zelda_TotK_Base.nsp                ]  [ Browse... ]  [ OK ] │
│  SHA-256: 48d9... verified (Base v1.0.0)                               │
│                                                                        │
│  Update File (v1.4.3 required):                                        │
│  [/path/to/Zelda_TotK_Update_v1.4.3.nsp       ]  [ Browse... ]  [ OK ] │
│  SHA-256: e84a... verified (v1.4.3 Fingerprint Match)                  │
│                                                                        │
│  Keys File (prod.keys):                                                │
│  [/home/user/.switch/prod.keys                ]  [ Browse... ]  [ OK ] │
│  Status: 168 Keys Validated                                            │
│                                                                        │
│  ────────────────────────────────────────────────────────────────────  │
│                                                                        │
│  Recompilation Status: Ready                                           │
│  [████████████████████████████████████████████████████████] 100%       │
│  Modules: main (142,854 fns), subsdk0 (12,410 fns), sdk (8,920 fns)   │
│  Shaders: 2,418 / 2,418 SPIR-V cached                                  │
│                                                                        │
│                             [ Recompile ]         [ Launch Game ▶ ]    │
└────────────────────────────────────────────────────────────────────────┘
```

### 2.1 Tab 1: ROM & Key Manager
- **Base Game & v1.4.3 Update Path Selectors:** Native file picker integration via `SDL_ShowOpenFileDialog`.
- **Integrity Badges:**
  - Real-time SHA-256 computation on selected files.
  - Emerald Green Badge `[ VERIFIED ]` when matching known hashes.
  - Ruby Red Badge `[ MISMATCH ]` with explanatory error text if user provides an unsupported version (e.g., v1.1.0 or v1.2.0).
- **Resumable Recompile Progress Bar:**
  - Visual breakdown of Lifting $\rightarrow$ C Generation $\rightarrow$ Clang Object Compilation $\rightarrow$ Shared Library Linking.
  - Retains completed compilation units across runs in `work/build/` to prevent re-compiling untouched functions.

---

## 3. Controller Studio

```
┌────────────────────────────────────────────────────────────────────────┐
│ Controller Studio                                                      │
├────────────────────────────────────────────────────────────────────────┤
│ Active Controller: [ DualSense Wireless Controller (SDL_Joystick 0) ▼ ]│
│ Button Prompt Theme: ( ) Nintendo Switch  ( ) Xbox  (•) PlayStation    │
│                                                                        │
│                  [ Visual Controller Mapping Diagram ]                 │
│                                                                        │
│ Left Stick Deadzone:   [----|-------] 12%                              │
│ Right Stick Deadzone:  [---|--------] 10%                              │
│                                                                        │
│ [X] Enable Gyroscope / Motion Aiming (SDL3 Sensor API)                 │
│ Gyro Sensitivity:      [-------|----] 1.4x                             │
│ Gyro Axis:             [ Yaw / Pitch ] ▼                               │
│                                                                        │
│ [X] HD Rumble Emulation                                                │
│ Rumble Intensity:      [--------|---] 85%                              │
│                                                                        │
│                                                  [ Restore Defaults ]  │
└────────────────────────────────────────────────────────────────────────┘
```

### 3.1 Key Capabilities
1. **Interactive Button Binder:** Clicking any input allows pressing the desired gamepad button or keyboard key to rebind.
2. **Deadzone & Response Sliders:** Real-time visual stick deflection indicators showing current stick position vs configured inner and outer deadzones.
3. **SDL3 Sensor Gyro Passthrough:**
   - Hooks into `SDL_OpenGamepadSensor` for `SDL_SENSOR_GYRO` and `SDL_SENSOR_ACCEL`.
   - Normalizes angular velocity to emulate the Switch Joy-Con / Pro Controller IMU at 200 Hz.
4. **Button Prompt Selector:** Switches in-game HUD button texture assets between:
   - **Nintendo Switch:** A, B, X, Y (Japanese orientation)
   - **Xbox:** A, B, X, Y (Western orientation)
   - **PlayStation:** Cross, Circle, Square, Triangle

---

## 4. Graphics Tuner

```
┌────────────────────────────────────────────────────────────────────────┐
│ Graphics Tuner                                                         │
├────────────────────────────────────────────────────────────────────────┤
│ Rendering Resolution: [ 2560 x 1440 (2K QHD - 1.77x)                 ▼]│
│ Aspect Ratio:         [ 21:9 Ultrawide (Dynamic FOV Patch)           ▼]│
│ Framerate Cap:        [ 60 FPS (Game Timing Patched)                 ▼]│
│ VSync Mode:           [ Mailbox / Fast Sync (Triple Buffer)          ▼]│
│                                                                        │
│ [X] AMD FidelityFX Super Resolution (FSR)                              │
│ FSR RCAS Sharpness:   [---------|--] 80%                               │
│                                                                        │
│ Shader Pipeline Cache:                                                 │
│ Validated SPIR-V Shaders:  2,418 modules                               │
│ Compiled Vulkan Pipelines: 4,120 graphics pipelines                    │
│ [ Purge Cache ]             [ Re-Warm All Pipelines ]                  │
│                                                                        │
│ Fullscreen Mode:      [ Borderless Windowed                          ▼]│
│ Vulkan Device:        [ NVIDIA GeForce RTX 4080 (Vulkan 1.3.280)     ▼]│
└────────────────────────────────────────────────────────────────────────┘
```

### 4.1 Resolution Scaling & Aspect Ratio Options
- **Internal Resolution Scale:** Native (1x: 720p/900p), 1080p (1.2x/1.5x), 1440p (2x), 4K UHD (3x), 8K UHD (6x).
- **Aspect Ratio:**
  - `16:9` (Standard)
  - `16:10` (Steam Deck / MacBooks)
  - `21:9` (Ultrawide)
  - `32:9` (Super Ultrawide)
  - When non-16:9 is selected, `TotK-Recomp` activates the `tools/render_scale.py` projection matrix override to expand horizontal viewport culling without stretching Link or HUD elements.
- **Framerate Options:**
  - `30 FPS`: Console native timing.
  - `60 FPS`: Activates `tools/game_timing.py` 60 FPS delta-time patch.
  - `120 FPS` & `Unlocked`: Interpolated presentation pace.

---

## 5. In-Game Overlay (`F1` / `Minus + Plus`)

When summoned during active gameplay:
- Semi-transparent backdrop (`rgba(10, 10, 12, 0.75)`).
- **Performance HUD:** Real-time graphs for Frame Time (ms), GPU execution duration, CPU sub-render tick, and audio stream buffer latency.
- **Quick Settings Bar:** Quick toggle for Resolution Scale, FPS limit, and Controller sensitivity without returning to the title screen.
- **Savestate / Quick Save Manager:** Quick load and slot inspection.
