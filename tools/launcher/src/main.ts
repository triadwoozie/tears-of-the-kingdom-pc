import { invoke } from "@tauri-apps/api/core";
import { listen } from "@tauri-apps/api/event";
import { open, save } from "@tauri-apps/plugin-dialog";

export interface DisplayConfig {
  resolution: string;
  aspect_ratio: string;
  vsync_mode: string;
  docked_mode: bool;
  fullscreen: bool;
  framerate_mode: string;
  target_hz: number;
}

export interface GraphicsConfig {
  enable_hud: bool;
  async_replay: bool;
  headless: bool;
  vulkan_device_index: number;
}

export interface ControllerConfig {
  active_guid: string;
  profile: string;
  left_deadzone: number;
  right_deadzone: number;
  trigger_threshold: number;
  gyro_enabled: bool;
  gyro_sensitivity: number;
  swap_ab_xy: bool;
  rumble_enabled: bool;
  rumble_intensity: number;
  hd_rumble_lra_sim: bool;
}

export interface MouseConfig {
  mouse_cam_enabled: bool;
  mouse_sens_x: number;
  mouse_sens_y: number;
  invert_y: bool;
  invert_x: bool;
  lock_cursor: bool;
  keybinds: Record<string, string>;
}

export interface ShaderPackConfig {
  use_shaders_pack: bool;
  shaders_pack_path: string;
}

export interface PatchConfig {
  id: string;
  name: string;
  enabled: boolean;
  description: string;
}

export interface LauncherConfig {
  theme: string;
  romfs_path: string;
  engine_binary_path: string;
  display: DisplayConfig;
  graphics: GraphicsConfig;
  controller: ControllerConfig;
  mouse: MouseConfig;
  shader_pack: ShaderPackConfig;
  patches: PatchConfig[];
  custom_env: Record<string, string>;
}

export interface GamepadInfo {
  id: string;
  name: string;
  is_connected: boolean;
  has_rumble: boolean;
  power_info: string;
}

export interface StickPreview {
  lx: number;
  ly: number;
  rx: number;
  ry: number;
  l2: number;
  r2: number;
}

export interface RomFsStatus {
  status: string;
  total_files: number;
  total_bytes: number;
  path: string;
  has_version: boolean;
  has_pack: boolean;
  has_shader: boolean;
  has_shader_pack: boolean;
  shader_pack_bytes: number;
  loose_shader_count: number;
  shader_pack_path: string;
}

export interface EngineState {
  is_running: boolean;
  pid: number | null;
}

export interface EngineLogEntry {
  channel: string;
  text: string;
  timestamp: number;
}

type bool = boolean;

// Default Keybinding Definitions matching keyboard_bindings.def
interface KeybindDef {
  id: string;
  group: string;
  switchBtn: string;
  name: string;
  defaultKey: string;
  defaultCode: string;
}

const DEFAULT_KEYBINDS: KeybindDef[] = [
  { id: "MOVE_UP", group: "Movement", switchBtn: "Left Stick Up", name: "Move Forward", defaultKey: "W", defaultCode: "26,0" },
  { id: "MOVE_LEFT", group: "Movement", switchBtn: "Left Stick Left", name: "Move Left", defaultKey: "A", defaultCode: "4,0" },
  { id: "MOVE_DOWN", group: "Movement", switchBtn: "Left Stick Down", name: "Move Backward", defaultKey: "S", defaultCode: "22,0" },
  { id: "MOVE_RIGHT", group: "Movement", switchBtn: "Left Stick Right", name: "Move Right", defaultKey: "D", defaultCode: "7,0" },
  { id: "X", group: "Actions", switchBtn: "X", name: "Jump / Paraglider", defaultKey: "Space", defaultCode: "44,0" },
  { id: "B", group: "Actions", switchBtn: "B", name: "Sprint (Hold) / Cancel", defaultKey: "Left Shift, Esc", defaultCode: "225,41" },
  { id: "Y", group: "Actions", switchBtn: "Y", name: "Melee Attack / Action", defaultKey: "Left Mouse (LMB)", defaultCode: "512,9" },
  { id: "ZR", group: "Actions", switchBtn: "ZR", name: "Draw Bow / Fire", defaultKey: "Right Mouse (RMB)", defaultCode: "513,21" },
  { id: "ZL", group: "Actions", switchBtn: "ZL", name: "Focus / Lock-On Shield", defaultKey: "Middle Mouse (MMB)", defaultCode: "514,20" },
  { id: "A", group: "Actions", switchBtn: "A", name: "Interact / Confirm", defaultKey: "E", defaultCode: "8,0" },
  { id: "L", group: "Actions", switchBtn: "L", name: "Ability Wheel (Hold)", defaultKey: "C", defaultCode: "6,0" },
  { id: "R", group: "Actions", switchBtn: "R", name: "Throw Weapon Aim", defaultKey: "T", defaultCode: "23,0" },
  { id: "PLUS", group: "Menus", switchBtn: "+", name: "Open Pouch / Inventory", defaultKey: "Tab", defaultCode: "43,0" },
  { id: "MINUS", group: "Menus", switchBtn: "-", name: "Open Purah Pad / Map", defaultKey: "Grave (~)", defaultCode: "53,16" },
  { id: "STICK_L", group: "Actions", switchBtn: "Left Stick Press", name: "Crouch / Stand", defaultKey: "Left Ctrl", defaultCode: "224,0" },
  { id: "STICK_R", group: "Actions", switchBtn: "Right Stick Press", name: "Purah Scope (Tap)", defaultKey: "V", defaultCode: "226,0" },
];

// State management
let currentConfig: LauncherConfig;
let logHistory: EngineLogEntry[] = [];
let controllerPollTimer: number | null = null;
let currentControllerId: string = "";

// DOM Elements
const docRoot = document.documentElement;
const themeToggle = document.getElementById("theme-toggle") as HTMLInputElement;
const navTabs = document.getElementById("nav-tabs") as HTMLElement;
const statusBadge = document.getElementById("status-badge") as HTMLElement;
const btnLaunch = document.getElementById("btn-launch") as HTMLButtonElement;
const btnStop = document.getElementById("btn-stop") as HTMLButtonElement;
const quickPidLabel = document.getElementById("quick-pid-label") as HTMLElement;

// Footer Indicators
const statRomfsStatus = document.getElementById("stat-romfs-status") as HTMLElement;
const statRomfsDetail = document.getElementById("stat-romfs-detail") as HTMLElement;
const statActiveController = document.getElementById("stat-active-controller") as HTMLElement;
const statControllerProfile = document.getElementById("stat-controller-profile") as HTMLElement;
const statTargetProfile = document.getElementById("stat-target-profile") as HTMLElement;

// Dashboard Controls
const dashDockedToggle = document.getElementById("dash-docked-toggle") as HTMLInputElement;
const dashMouseCamToggle = document.getElementById("dash-mouse-cam-toggle") as HTMLInputElement;
const dashHudToggle = document.getElementById("dash-hud-toggle") as HTMLInputElement;
const dashHeadlessToggle = document.getElementById("dash-headless-toggle") as HTMLInputElement;
const dashResolutionSelect = document.getElementById("dash-resolution-select") as HTMLSelectElement;
const dashVsyncSelect = document.getElementById("dash-vsync-select") as HTMLSelectElement;
const dashFramerateSelect = document.getElementById("dash-framerate-select") as HTMLSelectElement;
const dashCustomHzContainer = document.getElementById("dash-custom-hz-container") as HTMLElement;
const sliderCustomHz = document.getElementById("slider-custom-hz") as HTMLInputElement;
const labelCustomHz = document.getElementById("label-custom-hz") as HTMLElement;

// Dedicated Mouse & Keyboard Elements
const mouseCamMasterToggle = document.getElementById("mouse-cam-master-toggle") as HTMLInputElement;
const sliderMouseSensX = document.getElementById("slider-mouse-sens-x") as HTMLInputElement;
const sliderMouseSensY = document.getElementById("slider-mouse-sens-y") as HTMLInputElement;
const labelMouseSensX = document.getElementById("label-mouse-sens-x") as HTMLElement;
const labelMouseSensY = document.getElementById("label-mouse-sens-y") as HTMLElement;
const mouseInvertY = document.getElementById("mouse-invert-y") as HTMLInputElement;
const mouseInvertX = document.getElementById("mouse-invert-x") as HTMLInputElement;
const mouseLockCursor = document.getElementById("mouse-lock-cursor") as HTMLInputElement;
const keybindsTableBody = document.getElementById("keybinds-table-body") as HTMLElement;
const btnResetKeybinds = document.getElementById("btn-reset-keybinds") as HTMLButtonElement;

// Gamepad Elements
const ctrlDeviceSelect = document.getElementById("ctrl-device-select") as HTMLSelectElement;
const btnRefreshControllers = document.getElementById("btn-refresh-controllers") as HTMLButtonElement;
const ctrlProfileSelect = document.getElementById("ctrl-profile-select") as HTMLSelectElement;
const sliderDeadzoneL = document.getElementById("slider-deadzone-l") as HTMLInputElement;
const sliderDeadzoneR = document.getElementById("slider-deadzone-r") as HTMLInputElement;
const sliderTriggerThresh = document.getElementById("slider-trigger-thresh") as HTMLInputElement;
const labelDeadzoneL = document.getElementById("label-deadzone-l") as HTMLElement;
const labelDeadzoneR = document.getElementById("label-deadzone-r") as HTMLElement;
const labelTriggerThresh = document.getElementById("label-trigger-thresh") as HTMLElement;
const stickLDot = document.getElementById("stick-l-dot") as HTMLElement;
const stickRDot = document.getElementById("stick-r-dot") as HTMLElement;
const stickLDeadzoneVisual = document.getElementById("stick-l-deadzone-visual") as HTMLElement;
const stickRDeadzoneVisual = document.getElementById("stick-r-deadzone-visual") as HTMLElement;
const stickLCoords = document.getElementById("stick-l-coords") as HTMLElement;
const stickRCoords = document.getElementById("stick-r-coords") as HTMLElement;
const ctrlRumbleToggle = document.getElementById("ctrl-rumble-toggle") as HTMLInputElement;
const sliderRumbleIntensity = document.getElementById("slider-rumble-intensity") as HTMLInputElement;
const labelRumbleIntensity = document.getElementById("label-rumble-intensity") as HTMLElement;
const ctrlHdRumbleToggle = document.getElementById("ctrl-hd-rumble-toggle") as HTMLInputElement;
const btnTestRumble = document.getElementById("btn-test-rumble") as HTMLButtonElement;
const ctrlGyroToggle = document.getElementById("ctrl-gyro-toggle") as HTMLInputElement;
const sliderGyroSens = document.getElementById("slider-gyro-sens") as HTMLInputElement;
const labelGyroSens = document.getElementById("label-gyro-sens") as HTMLElement;

// Assets Elements
const assetsRomfsInput = document.getElementById("assets-romfs-input") as HTMLInputElement;
const btnBrowseRomfs = document.getElementById("btn-browse-romfs") as HTMLButtonElement;
const btnVerifyRomfs = document.getElementById("btn-verify-romfs") as HTMLButtonElement;
const iconCheckVersion = document.getElementById("icon-check-version") as HTMLElement;
const badgeCheckVersion = document.getElementById("badge-check-version") as HTMLElement;
const iconCheckPack = document.getElementById("icon-check-pack") as HTMLElement;
const badgeCheckPack = document.getElementById("badge-check-pack") as HTMLElement;
const iconCheckShader = document.getElementById("icon-check-shader") as HTMLElement;
const badgeCheckShader = document.getElementById("badge-check-shader") as HTMLElement;
const iconCheckShaderPack = document.getElementById("icon-check-shader-pack") as HTMLElement;
const badgeCheckShaderPack = document.getElementById("badge-check-shader-pack") as HTMLElement;

const btnPackRomfs = document.getElementById("btn-pack-romfs") as HTMLButtonElement;
const packProgressContainer = document.getElementById("pack-progress-container") as HTMLElement;
const packProgressBar = document.getElementById("pack-progress-bar") as HTMLProgressElement;
const packProgressStatus = document.getElementById("pack-progress-status") as HTMLElement;
const packProgressPct = document.getElementById("pack-progress-pct") as HTMLElement;

const btnPackShaders = document.getElementById("btn-pack-shaders") as HTMLButtonElement;
const shaderPackProgressContainer = document.getElementById("shader-pack-progress-container") as HTMLElement;
const shaderPackProgressBar = document.getElementById("shader-pack-progress-bar") as HTMLProgressElement;
const shaderPackProgressStatus = document.getElementById("shader-pack-progress-status") as HTMLElement;
const shaderPackProgressPct = document.getElementById("shader-pack-progress-pct") as HTMLElement;

// Graphics & Patches Elements
const gfxAspectSelect = document.getElementById("gfx-aspect-select") as HTMLSelectElement;
const gfxDeviceIdx = document.getElementById("gfx-device-idx") as HTMLInputElement;
const gfxAsyncReplayToggle = document.getElementById("gfx-async-replay-toggle") as HTMLInputElement;
const gfxFullscreenToggle = document.getElementById("gfx-fullscreen-toggle") as HTMLInputElement;
const patchesTableBody = document.getElementById("patches-table-body") as HTMLElement;
const btnSavePatches = document.getElementById("btn-save-patches") as HTMLButtonElement;

// Console Elements
const logFilterChannel = document.getElementById("log-filter-channel") as HTMLSelectElement;
const logSearchInput = document.getElementById("log-search-input") as HTMLInputElement;
const logAutoscrollToggle = document.getElementById("log-autoscroll-toggle") as HTMLInputElement;
const btnClearLogs = document.getElementById("btn-clear-logs") as HTMLButtonElement;
const btnExportLogs = document.getElementById("btn-export-logs") as HTMLButtonElement;
const logOutputContainer = document.getElementById("log-output-container") as HTMLElement;
const logLineCounter = document.getElementById("log-line-counter") as HTMLElement;
const consoleTerminal = document.getElementById("console-terminal") as HTMLElement;

// Helper: Format Bytes
function formatBytes(bytes: number): string {
  if (bytes === 0) return "0 Bytes";
  const k = 1024;
  const sizes = ["Bytes", "KiB", "MiB", "GiB", "TiB"];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return parseFloat((bytes / Math.pow(k, i)).toFixed(2)) + " " + sizes[i];
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

// 1. Tab Switching
function initTabs() {
  const tabs = navTabs.querySelectorAll<HTMLAnchorElement>(".tab");
  tabs.forEach((tab) => {
    tab.addEventListener("click", () => {
      tabs.forEach((t) => {
        t.classList.remove("tab-active");
        t.classList.add("text-base-content/70");
      });
      tab.classList.add("tab-active");
      tab.classList.remove("text-base-content/70");

      const targetId = tab.getAttribute("data-tab");
      document.querySelectorAll(".tab-content-panel").forEach((panel) => {
        panel.classList.add("hidden");
      });
      if (targetId) {
        document.getElementById(targetId)?.classList.remove("hidden");
      }
    });
  });
}

// 2. Theme Switching
function applyTheme(themeName: string) {
  const finalTheme = themeName === "sky-island-light" ? "sky-island-light" : "sky-island-dark";
  docRoot.setAttribute("data-theme", finalTheme);
  themeToggle.checked = finalTheme === "sky-island-dark";
  if (currentConfig) {
    currentConfig.theme = finalTheme;
    invoke("save_config", { config: currentConfig }).catch(console.error);
  }
}

themeToggle?.addEventListener("change", () => {
  const nextTheme = themeToggle.checked ? "sky-island-dark" : "sky-island-light";
  applyTheme(nextTheme);
});

// 3. Config Hydration
async function loadConfig() {
  try {
    currentConfig = await invoke<LauncherConfig>("get_config");
    applyTheme(currentConfig.theme === "sky-island-light" ? "sky-island-light" : "sky-island-dark");

    // Ensure display framerate defaults
    if (!currentConfig.display.framerate_mode) {
      currentConfig.display.framerate_mode = "60";
    }
    if (!currentConfig.display.target_hz) {
      currentConfig.display.target_hz = 144;
    }

    // Ensure mouse and shader_pack defaults
    if (!currentConfig.mouse) {
      currentConfig.mouse = {
        mouse_cam_enabled: true,
        mouse_sens_x: 1.0,
        mouse_sens_y: 1.0,
        invert_y: false,
        invert_x: false,
        lock_cursor: true,
        keybinds: {},
      };
    }
    if (!currentConfig.shader_pack) {
      currentConfig.shader_pack = {
        use_shaders_pack: true,
        shaders_pack_path: "data/shaders/ryujinx/shaders.pack",
      };
    }

    // Hydrate Dashboard & Quick settings
    dashDockedToggle.checked = currentConfig.display.docked_mode;
    dashMouseCamToggle.checked = currentConfig.mouse.mouse_cam_enabled;
    dashHudToggle.checked = currentConfig.graphics.enable_hud;
    dashHeadlessToggle.checked = currentConfig.graphics.headless;
    dashResolutionSelect.value = currentConfig.display.resolution;
    dashVsyncSelect.value = currentConfig.display.vsync_mode;

    // Hydrate Framerate Controls
    if (dashFramerateSelect) {
      dashFramerateSelect.value = currentConfig.display.framerate_mode;
      if (currentConfig.display.framerate_mode === "custom") {
        dashCustomHzContainer?.classList.remove("hidden");
      } else {
        dashCustomHzContainer?.classList.add("hidden");
      }
    }
    if (sliderCustomHz) {
      sliderCustomHz.value = String(currentConfig.display.target_hz);
      if (labelCustomHz) labelCustomHz.innerText = `${currentConfig.display.target_hz} Hz`;
    }

    // Hydrate Mouse & Keyboard
    mouseCamMasterToggle.checked = currentConfig.mouse.mouse_cam_enabled;
    const sensXVal = Math.round(currentConfig.mouse.mouse_sens_x * 10);
    const sensYVal = Math.round(currentConfig.mouse.mouse_sens_y * 10);
    sliderMouseSensX.value = String(sensXVal);
    sliderMouseSensY.value = String(sensYVal);
    labelMouseSensX.innerText = `${(sensXVal / 10).toFixed(2)}x`;
    labelMouseSensY.innerText = `${(sensYVal / 10).toFixed(2)}x`;
    mouseInvertY.checked = currentConfig.mouse.invert_y;
    mouseInvertX.checked = currentConfig.mouse.invert_x;
    mouseLockCursor.checked = currentConfig.mouse.lock_cursor;

    // Hydrate Graphics
    gfxAspectSelect.value = currentConfig.display.aspect_ratio;
    gfxDeviceIdx.value = String(currentConfig.graphics.vulkan_device_index);
    gfxAsyncReplayToggle.checked = currentConfig.graphics.async_replay;
    gfxFullscreenToggle.checked = currentConfig.display.fullscreen;

    // Hydrate Controller Settings
    sliderDeadzoneL.value = String(Math.round(currentConfig.controller.left_deadzone * 100));
    labelDeadzoneL.innerText = `${sliderDeadzoneL.value}%`;
    stickLDeadzoneVisual.style.width = `${sliderDeadzoneL.value}%`;
    stickLDeadzoneVisual.style.height = `${sliderDeadzoneL.value}%`;

    sliderDeadzoneR.value = String(Math.round(currentConfig.controller.right_deadzone * 100));
    labelDeadzoneR.innerText = `${sliderDeadzoneR.value}%`;
    stickRDeadzoneVisual.style.width = `${sliderDeadzoneR.value}%`;
    stickRDeadzoneVisual.style.height = `${sliderDeadzoneR.value}%`;

    sliderTriggerThresh.value = String(Math.round(currentConfig.controller.trigger_threshold * 100));
    labelTriggerThresh.innerText = `${sliderTriggerThresh.value}%`;

    ctrlProfileSelect.value = currentConfig.controller.profile;
    ctrlRumbleToggle.checked = currentConfig.controller.rumble_enabled;
    sliderRumbleIntensity.value = String(Math.round(currentConfig.controller.rumble_intensity * 100));
    labelRumbleIntensity.innerText = `${sliderRumbleIntensity.value}%`;
    ctrlHdRumbleToggle.checked = currentConfig.controller.hd_rumble_lra_sim;

    ctrlGyroToggle.checked = currentConfig.controller.gyro_enabled;
    sliderGyroSens.value = String(Math.round(currentConfig.controller.gyro_sensitivity * 10));
    labelGyroSens.innerText = `${(Number(sliderGyroSens.value) / 10).toFixed(1)}x`;

    // Hydrate Assets
    assetsRomfsInput.value = currentConfig.romfs_path || "";
    await checkRomfs(currentConfig.romfs_path || "");

    renderKeybindsTable();
    renderPatchesTable();
    updateTargetProfileStat();
  } catch (err) {
    console.error("Failed to load config:", err);
  }
}

function updateTargetProfileStat() {
  if (!currentConfig) return;
  const dock = currentConfig.display.docked_mode ? "Docked" : "Handheld";
  statTargetProfile.innerText = `${currentConfig.display.resolution} ${dock}`;
}

async function saveCurrentConfig() {
  if (!currentConfig) return;
  try {
    await invoke("save_config", { config: currentConfig });
  } catch (err) {
    console.error("Failed to save config:", err);
  }
}

// 4. Keyboard Bindings Matrix Rendering
function renderKeybindsTable() {
  if (!keybindsTableBody || !currentConfig) return;
  keybindsTableBody.innerHTML = "";

  DEFAULT_KEYBINDS.forEach((def) => {
    const tr = document.createElement("tr");
    tr.className = "hover border-b border-base-300/40";

    const customCode = currentConfig.mouse?.keybinds?.[def.id] || def.defaultCode;
    const isCustom = customCode !== def.defaultCode;

    tr.innerHTML = `
      <td>
        <span class="font-medium text-base-content">${escapeHtml(def.name)}</span>
        <div class="text-[10px] text-base-content/60">${escapeHtml(def.group)}</div>
      </td>
      <td>
        <span class="badge badge-sm badge-ghost font-mono text-[10px] text-primary border border-base-300">${escapeHtml(def.switchBtn)}</span>
      </td>
      <td>
        <button class="btn btn-xs ${isCustom ? "btn-warning" : "btn-outline border-base-300"} font-mono text-[11px] gap-1 rebind-btn" data-key-id="${def.id}">
          <span>${escapeHtml(def.defaultKey)}</span>
          ${isCustom ? "<span class='badge badge-xs badge-neutral'>Custom</span>" : ""}
        </button>
      </td>
      <td class="text-right font-mono text-[10px] text-base-content/60">
        <code>${escapeHtml(customCode)}</code>
      </td>
    `;

    const btn = tr.querySelector(".rebind-btn") as HTMLButtonElement;
    btn?.addEventListener("click", () => {
      const input = prompt(`Enter custom SDL physical scancode for ${def.name} (primary,secondary, e.g. '26,0'):`, customCode);
      if (input !== null && input.trim()) {
        if (!currentConfig.mouse.keybinds) currentConfig.mouse.keybinds = {};
        currentConfig.mouse.keybinds[def.id] = input.trim();
        saveCurrentConfig();
        renderKeybindsTable();
      }
    });

    keybindsTableBody.appendChild(tr);
  });
}

btnResetKeybinds?.addEventListener("click", () => {
  if (confirm("Reset all physical keyboard bindings to default layout?")) {
    if (currentConfig?.mouse) {
      currentConfig.mouse.keybinds = {};
      saveCurrentConfig();
      renderKeybindsTable();
    }
  }
});

// 5. Patches Table
function renderPatchesTable() {
  if (!currentConfig || !patchesTableBody) return;
  patchesTableBody.innerHTML = "";

  const patchHooks: Record<string, string> = {
    patch_zero_heap: "f_80006a920 + 0x8008ac138",
    patch_jump_table_fallback: "Lifter Branch Resolver",
    patch_shaders_pack: "Vulkan Shader Cache",
    patch_mouse_camera: "Direct Input / Mouse Look",
    patch_loose_romfs: "RomFS Asset Loader",
    patch_60fps_lock: "Monotonic Timestep Delta",
    patch_disable_drs: "Render Target Fixed Scale",
    patch_ultrawide: "Viewport Aspect Matrix",
  };

  currentConfig.patches.forEach((patch, idx) => {
    const tr = document.createElement("tr");
    tr.className = "hover border-b border-base-300";

    const tdCheck = document.createElement("td");
    tdCheck.className = "text-center";
    const chk = document.createElement("input");
    chk.type = "checkbox";
    chk.className = "checkbox checkbox-primary checkbox-sm";
    chk.checked = patch.enabled;
    chk.addEventListener("change", () => {
      currentConfig.patches[idx].enabled = chk.checked;
      if (patch.id === "patch_mouse_camera") {
        currentConfig.mouse.mouse_cam_enabled = chk.checked;
        mouseCamMasterToggle.checked = chk.checked;
        dashMouseCamToggle.checked = chk.checked;
      }
      if (patch.id === "patch_shaders_pack") {
        currentConfig.shader_pack.use_shaders_pack = chk.checked;
      }
      if (patch.id === "patch_60fps_lock") {
        if (chk.checked) {
          if (currentConfig.display.framerate_mode === "30") {
            currentConfig.display.framerate_mode = "60";
            if (dashFramerateSelect) dashFramerateSelect.value = "60";
          }
        } else {
          currentConfig.display.framerate_mode = "30";
          if (dashFramerateSelect) dashFramerateSelect.value = "30";
          dashCustomHzContainer?.classList.add("hidden");
        }
      }
      saveCurrentConfig();
    });
    tdCheck.appendChild(chk);

    const tdName = document.createElement("td");
    const hookSymbol = patchHooks[patch.id] || "src/runtime/";
    tdName.innerHTML = `
      <div class="flex flex-col gap-0.5">
        <span class="font-semibold text-xs text-base-content">${escapeHtml(patch.name)}</span>
        <span class="badge badge-ghost font-mono text-[10px] text-accent border border-base-300/80 w-fit">${escapeHtml(hookSymbol)}</span>
      </div>
    `;

    const tdDesc = document.createElement("td");
    tdDesc.className = "text-xs text-base-content/80";
    tdDesc.innerText = patch.description;

    tr.appendChild(tdCheck);
    tr.appendChild(tdName);
    tr.appendChild(tdDesc);
    patchesTableBody.appendChild(tr);
  });
}

btnSavePatches?.addEventListener("click", async () => {
  await saveCurrentConfig();
  const originalText = btnSavePatches.innerText;
  btnSavePatches.innerText = "Saved ✓";
  setTimeout(() => {
    btnSavePatches.innerText = originalText;
  }, 1500);
});

// 6. Framerate Selector Listeners
dashFramerateSelect?.addEventListener("change", () => {
  if (!currentConfig) return;
  const val = dashFramerateSelect.value;
  currentConfig.display.framerate_mode = val;
  if (val === "custom") {
    dashCustomHzContainer?.classList.remove("hidden");
  } else {
    dashCustomHzContainer?.classList.add("hidden");
  }

  // Synchronize 60fps patch flag
  const patch60 = currentConfig.patches.find((p) => p.id === "patch_60fps_lock");
  if (patch60) {
    patch60.enabled = (val === "60" || val === "120" || val === "custom");
    renderPatchesTable();
  }
  saveCurrentConfig();
});

sliderCustomHz?.addEventListener("input", () => {
  if (!currentConfig) return;
  const hz = parseInt(sliderCustomHz.value, 10) || 120;
  if (labelCustomHz) labelCustomHz.innerText = `${hz} Hz`;
  currentConfig.display.target_hz = hz;
  saveCurrentConfig();
});

// 7. Mouse Controls Listeners
function syncMouseCam(enabled: boolean) {
  if (!currentConfig) return;
  currentConfig.mouse.mouse_cam_enabled = enabled;
  mouseCamMasterToggle.checked = enabled;
  dashMouseCamToggle.checked = enabled;

  const patch = currentConfig.patches.find((p) => p.id === "patch_mouse_camera");
  if (patch) patch.enabled = enabled;
  renderPatchesTable();
  saveCurrentConfig();
}

mouseCamMasterToggle?.addEventListener("change", () => syncMouseCam(mouseCamMasterToggle.checked));
dashMouseCamToggle?.addEventListener("change", () => syncMouseCam(dashMouseCamToggle.checked));

sliderMouseSensX?.addEventListener("input", () => {
  const val = Number(sliderMouseSensX.value) / 10.0;
  labelMouseSensX.innerText = `${val.toFixed(2)}x`;
  if (currentConfig?.mouse) {
    currentConfig.mouse.mouse_sens_x = val;
    saveCurrentConfig();
  }
});

sliderMouseSensY?.addEventListener("input", () => {
  const val = Number(sliderMouseSensY.value) / 10.0;
  labelMouseSensY.innerText = `${val.toFixed(2)}x`;
  if (currentConfig?.mouse) {
    currentConfig.mouse.mouse_sens_y = val;
    saveCurrentConfig();
  }
});

mouseInvertY?.addEventListener("change", () => {
  if (currentConfig?.mouse) {
    currentConfig.mouse.invert_y = mouseInvertY.checked;
    saveCurrentConfig();
  }
});

mouseInvertX?.addEventListener("change", () => {
  if (currentConfig?.mouse) {
    currentConfig.mouse.invert_x = mouseInvertX.checked;
    saveCurrentConfig();
  }
});

mouseLockCursor?.addEventListener("change", () => {
  if (currentConfig?.mouse) {
    currentConfig.mouse.lock_cursor = mouseLockCursor.checked;
    saveCurrentConfig();
  }
});

// 8. Controller Enumeration & Live Calibration
async function refreshControllers() {
  try {
    const controllers = await invoke<GamepadInfo[]>("list_controllers");
    ctrlDeviceSelect.innerHTML = "";

    if (controllers.length === 0) {
      const opt = document.createElement("option");
      opt.value = "";
      opt.innerText = "No gamepads detected (Keyboard & Mouse active)";
      ctrlDeviceSelect.appendChild(opt);
      statActiveController.innerText = "Mouse / Keyboard";
      stopPollingStick();
      return;
    }

    controllers.forEach((pad) => {
      const opt = document.createElement("option");
      opt.value = pad.id;
      const ffTag = pad.has_rumble ? "[Haptics ✓]" : "[No FF]";
      opt.innerText = `${pad.name} • ${pad.power_info} ${ffTag}`;
      ctrlDeviceSelect.appendChild(opt);
    });

    currentControllerId = controllers[0].id;
    ctrlDeviceSelect.value = currentControllerId;
    statActiveController.innerText = controllers[0].name.split("(")[0].trim();
    startPollingStick(currentControllerId);
  } catch (err) {
    console.error("Failed to list controllers:", err);
  }
}

btnRefreshControllers?.addEventListener("click", refreshControllers);
ctrlDeviceSelect?.addEventListener("change", () => {
  currentControllerId = ctrlDeviceSelect.value;
  if (currentControllerId) {
    startPollingStick(currentControllerId);
    const selectedOpt = ctrlDeviceSelect.options[ctrlDeviceSelect.selectedIndex];
    statActiveController.innerText = selectedOpt.text.split("•")[0].trim();
  } else {
    stopPollingStick();
    statActiveController.innerText = "Mouse / Keyboard";
  }
});

function startPollingStick(id: string) {
  stopPollingStick();
  controllerPollTimer = window.setInterval(async () => {
    try {
      const stick = await invoke<StickPreview>("poll_controller_preview", { id });
      const maxR = 52;
      const lxPx = stick.lx * maxR;
      const lyPx = -stick.ly * maxR;
      const rxPx = stick.rx * maxR;
      const ryPx = -stick.ry * maxR;

      stickLDot.style.transform = `translate(${lxPx}px, ${lyPx}px)`;
      stickRDot.style.transform = `translate(${rxPx}px, ${ryPx}px)`;
      stickLCoords.innerText = `X: ${stick.lx.toFixed(2)} | Y: ${stick.ly.toFixed(2)}`;
      stickRCoords.innerText = `X: ${stick.rx.toFixed(2)} | Y: ${stick.ry.toFixed(2)}`;
    } catch (_) {}
  }, 40);
}

function stopPollingStick() {
  if (controllerPollTimer !== null) {
    clearInterval(controllerPollTimer);
    controllerPollTimer = null;
  }
}

// Sliders Events
sliderDeadzoneL?.addEventListener("input", () => {
  const val = Number(sliderDeadzoneL.value);
  labelDeadzoneL.innerText = `${val}%`;
  stickLDeadzoneVisual.style.width = `${val * 2}%`;
  stickLDeadzoneVisual.style.height = `${val * 2}%`;
  if (currentConfig) {
    currentConfig.controller.left_deadzone = val / 100.0;
    saveCurrentConfig();
  }
});

sliderDeadzoneR?.addEventListener("input", () => {
  const val = Number(sliderDeadzoneR.value);
  labelDeadzoneR.innerText = `${val}%`;
  stickRDeadzoneVisual.style.width = `${val * 2}%`;
  stickRDeadzoneVisual.style.height = `${val * 2}%`;
  if (currentConfig) {
    currentConfig.controller.right_deadzone = val / 100.0;
    saveCurrentConfig();
  }
});

sliderTriggerThresh?.addEventListener("input", () => {
  const val = Number(sliderTriggerThresh.value);
  labelTriggerThresh.innerText = `${val}%`;
  if (currentConfig) {
    currentConfig.controller.trigger_threshold = val / 100.0;
    saveCurrentConfig();
  }
});

sliderRumbleIntensity?.addEventListener("input", () => {
  const val = Number(sliderRumbleIntensity.value);
  labelRumbleIntensity.innerText = `${val}%`;
  if (currentConfig) {
    currentConfig.controller.rumble_intensity = val / 100.0;
    saveCurrentConfig();
  }
});

ctrlRumbleToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.controller.rumble_enabled = ctrlRumbleToggle.checked;
    saveCurrentConfig();
  }
});

ctrlHdRumbleToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.controller.hd_rumble_lra_sim = ctrlHdRumbleToggle.checked;
    saveCurrentConfig();
  }
});

sliderGyroSens?.addEventListener("input", () => {
  const val = Number(sliderGyroSens.value) / 10.0;
  labelGyroSens.innerText = `${val.toFixed(1)}x`;
  if (currentConfig) {
    currentConfig.controller.gyro_sensitivity = val;
    saveCurrentConfig();
  }
});

ctrlGyroToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.controller.gyro_enabled = ctrlGyroToggle.checked;
    saveCurrentConfig();
  }
});

ctrlProfileSelect?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.controller.profile = ctrlProfileSelect.value;
    statControllerProfile.innerText = ctrlProfileSelect.value;
    currentConfig.controller.swap_ab_xy = ctrlProfileSelect.value.includes("Xbox");
    saveCurrentConfig();
  }
});

btnTestRumble?.addEventListener("click", async () => {
  if (!currentControllerId) return;
  const intensity = currentConfig?.controller.rumble_intensity || 1.0;
  try {
    await invoke("test_rumble", {
      id: currentControllerId,
      lowFreq: intensity,
      highFreq: intensity,
      durationMs: 250,
    });
  } catch (err) {
    console.error("Test rumble error:", err);
  }
});

// 9. Assets & RomFS Verification
async function checkRomfs(path: string) {
  try {
    const status = await invoke<RomFsStatus>("validate_romfs", { path });
    updateRomfsUI(status);
  } catch (err) {
    console.error("Validate romfs error:", err);
  }
}

function updateRomfsUI(status: RomFsStatus) {
  // Footer status
  if (status.status === "PackedVerified") {
    statRomfsStatus.innerText = "Packed ✓";
    statRomfsStatus.className = "badge badge-sm badge-success text-[11px] font-mono";
    statRomfsDetail.innerText = `romfs.bin (${formatBytes(status.total_bytes)})`;
  } else if (status.status === "LooseVerified") {
    statRomfsStatus.innerText = "Loose Ready";
    statRomfsStatus.className = "badge badge-sm badge-primary text-[11px] font-mono";
    statRomfsDetail.innerText = `${status.total_files} files (${formatBytes(status.total_bytes)})`;
  } else {
    statRomfsStatus.innerText = "Missing";
    statRomfsStatus.className = "badge badge-sm badge-error text-[11px] font-mono";
    statRomfsDetail.innerText = "Path not set";
  }

  // Row 1: System/Version/ett.byml
  if (iconCheckVersion && badgeCheckVersion) {
    if (status.has_version || status.status === "PackedVerified") {
      iconCheckVersion.innerText = "✓";
      badgeCheckVersion.className = "badge badge-sm badge-success font-mono text-[10px]";
      badgeCheckVersion.innerText = status.status === "PackedVerified" ? "In romfs.bin" : "Found";
    } else {
      iconCheckVersion.innerText = "⚠️";
      badgeCheckVersion.className = "badge badge-sm badge-error font-mono text-[10px]";
      badgeCheckVersion.innerText = "Missing";
    }
  }

  // Row 2: data/romfs/romfs.bin
  if (iconCheckPack && badgeCheckPack) {
    if (status.status === "PackedVerified") {
      iconCheckPack.innerText = "✓";
      badgeCheckPack.className = "badge badge-sm badge-success font-mono text-[10px]";
      badgeCheckPack.innerText = `Verified (${formatBytes(status.total_bytes)})`;
    } else {
      iconCheckPack.innerText = "⚠️";
      badgeCheckPack.className = "badge badge-sm badge-ghost font-mono text-[10px] text-base-content/60";
      badgeCheckPack.innerText = "Unpacked";
    }
  }

  // Row 3: Loose Shaders
  if (iconCheckShader && badgeCheckShader) {
    if (status.loose_shader_count > 0 || status.has_shader) {
      iconCheckShader.innerText = "✓";
      badgeCheckShader.className = "badge badge-sm badge-primary font-mono text-[10px]";
      badgeCheckShader.innerText = `${status.loose_shader_count || "57k+"} loose .spv`;
    } else {
      iconCheckShader.innerText = "⚠️";
      badgeCheckShader.className = "badge badge-sm badge-ghost font-mono text-[10px] text-base-content/60";
      badgeCheckShader.innerText = "0 files found";
    }
  }

  // Row 4: shaders.pack
  if (iconCheckShaderPack && badgeCheckShaderPack) {
    if (status.has_shader_pack) {
      iconCheckShaderPack.innerText = "✓";
      badgeCheckShaderPack.className = "badge badge-sm badge-success font-mono text-[10px]";
      badgeCheckShaderPack.innerText = `Packed Ready (${formatBytes(status.shader_pack_bytes)})`;
    } else {
      iconCheckShaderPack.innerText = "⚠️";
      badgeCheckShaderPack.className = "badge badge-sm badge-warning badge-outline font-mono text-[10px]";
      badgeCheckShaderPack.innerText = "Unpacked (~57k files)";
    }
  }
}

btnBrowseRomfs?.addEventListener("click", async () => {
  try {
    const selected = await open({
      directory: true,
      multiple: false,
      title: "Select Tears of the Kingdom RomFS Folder",
    });
    if (selected && typeof selected === "string") {
      assetsRomfsInput.value = selected;
      if (currentConfig) {
        currentConfig.romfs_path = selected;
        await saveCurrentConfig();
      }
      await checkRomfs(selected);
    }
  } catch (err) {
    console.error("Browse path error:", err);
  }
});

btnVerifyRomfs?.addEventListener("click", async () => {
  const p = assetsRomfsInput.value.trim();
  if (currentConfig) {
    currentConfig.romfs_path = p;
    await saveCurrentConfig();
  }
  await checkRomfs(p);
});

assetsRomfsInput?.addEventListener("change", async () => {
  const p = assetsRomfsInput.value.trim();
  if (currentConfig) {
    currentConfig.romfs_path = p;
    await saveCurrentConfig();
  }
  await checkRomfs(p);
});

// RomFS Packer Action
btnPackRomfs?.addEventListener("click", async () => {
  const source = assetsRomfsInput.value.trim();
  if (!source) {
    alert("Please select or paste a valid RomFS source path first.");
    return;
  }
  packProgressContainer.classList.remove("hidden");
  packProgressBar.value = 5;
  packProgressStatus.innerText = "Initializing RomFS compression via tools/setup.py...";
  packProgressPct.innerText = "5%";

  try {
    await invoke("pack_romfs", {
      engineDir: ".",
      sourcePath: source,
      outputBin: "data/romfs/romfs.bin",
    });
  } catch (err: any) {
    packProgressStatus.innerText = `Pack error: ${err}`;
  }
});

listen("romfs-pack-progress", (event: any) => {
  const data = event.payload;
  packProgressBar.value = Math.round(data.percent);
  packProgressPct.innerText = `${Math.round(data.percent)}%`;
  packProgressStatus.innerText = data.message;
  if (data.is_finished) {
    setTimeout(() => {
      if (data.success && currentConfig) {
        currentConfig.romfs_path = "data/romfs/romfs.bin";
        assetsRomfsInput.value = currentConfig.romfs_path;
        checkRomfs(currentConfig.romfs_path);
      }
    }, 1000);
  }
});

// Shader Packer Action
btnPackShaders?.addEventListener("click", async () => {
  shaderPackProgressContainer.classList.remove("hidden");
  shaderPackProgressBar.value = 5;
  shaderPackProgressStatus.innerText = "Optimizing shader cache archive...";
  shaderPackProgressPct.innerText = "5%";

  try {
    await invoke("pack_shaders", {
      sourceDir: "data/shaders/ryujinx",
      outputPack: "data/shaders/ryujinx/shaders.pack",
    });
  } catch (err: any) {
    shaderPackProgressStatus.innerText = `Shader pack error: ${err}`;
  }
});

listen("shader-pack-progress", (event: any) => {
  const data = event.payload;
  shaderPackProgressBar.value = Math.round(data.percent);
  shaderPackProgressPct.innerText = `${Math.round(data.percent)}%`;
  shaderPackProgressStatus.innerText = data.message;
  if (data.is_finished && data.success) {
    setTimeout(async () => {
      await checkRomfs(assetsRomfsInput.value.trim());
    }, 1000);
  }
});

// 10. Quick Controls & Settings
dashDockedToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.display.docked_mode = dashDockedToggle.checked;
    updateTargetProfileStat();
    saveCurrentConfig();
  }
});

dashHudToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.graphics.enable_hud = dashHudToggle.checked;
    saveCurrentConfig();
  }
});

dashHeadlessToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.graphics.headless = dashHeadlessToggle.checked;
    saveCurrentConfig();
  }
});

dashResolutionSelect?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.display.resolution = dashResolutionSelect.value;
    updateTargetProfileStat();
    saveCurrentConfig();
  }
});

dashVsyncSelect?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.display.vsync_mode = dashVsyncSelect.value;
    saveCurrentConfig();
  }
});

gfxAspectSelect?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.display.aspect_ratio = gfxAspectSelect.value;
    saveCurrentConfig();
  }
});

gfxDeviceIdx?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.graphics.vulkan_device_index = Number(gfxDeviceIdx.value);
    saveCurrentConfig();
  }
});

gfxAsyncReplayToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.graphics.async_replay = gfxAsyncReplayToggle.checked;
    saveCurrentConfig();
  }
});

gfxFullscreenToggle?.addEventListener("change", () => {
  if (currentConfig) {
    currentConfig.display.fullscreen = gfxFullscreenToggle.checked;
    saveCurrentConfig();
  }
});

// 11. Engine Execution & Status
async function checkEngineStatus() {
  try {
    const st = await invoke<EngineState>("get_engine_status");
    updateEngineStateUI(st);
  } catch (err) {
    console.error("Check status error:", err);
  }
}

function updateEngineStateUI(st: EngineState) {
  if (st.is_running) {
    statusBadge.className = "badge badge-success gap-2 font-mono text-xs py-2 px-3 animate-pulse border border-success whitespace-nowrap shrink-0";
    statusBadge.innerHTML = `<span class="inline-block w-2 h-2 rounded-full bg-white"></span><span>Running PID ${st.pid}</span>`;
    btnLaunch.classList.add("hidden");
    btnStop.classList.remove("hidden");
    quickPidLabel.innerText = `PID: ${st.pid}`;
  } else {
    statusBadge.className = "badge badge-ghost gap-2 font-mono text-xs py-2 px-3 border border-base-300 whitespace-nowrap shrink-0";
    statusBadge.innerHTML = `<span class="inline-block w-2 h-2 rounded-full bg-base-content/40"></span><span>Engine Idle</span>`;
    btnLaunch.classList.remove("hidden");
    btnStop.classList.add("hidden");
    quickPidLabel.innerText = "PID: --";
  }
}

btnLaunch?.addEventListener("click", async () => {
  if (!currentConfig) return;
  try {
    const pid = await invoke<number>("launch_engine", { config: currentConfig });
    updateEngineStateUI({ is_running: true, pid });
    appendLog({
      channel: "info",
      text: `[Launcher] Spawned ultratears engine with PID ${pid} (Framerate: ${currentConfig.display.framerate_mode === "custom" ? currentConfig.display.target_hz + "Hz" : currentConfig.display.framerate_mode + " FPS"})`,
      timestamp: Date.now(),
    });
  } catch (err: any) {
    alert(`Launch failed: ${err}`);
    appendLog({
      channel: "error",
      text: `[Launcher Error] ${err}`,
      timestamp: Date.now(),
    });
  }
});

btnStop?.addEventListener("click", async () => {
  try {
    await invoke("stop_engine");
    updateEngineStateUI({ is_running: false, pid: null });
  } catch (err) {
    console.error("Stop engine error:", err);
  }
});

listen("engine-status", (event: any) => {
  updateEngineStateUI(event.payload);
});

// 12. Console & Log Streaming
listen("engine-log", (event: any) => {
  const entry: EngineLogEntry = event.payload;
  logHistory.push(entry);
  if (logHistory.length > 2000) {
    logHistory.shift();
  }
  appendLog(entry);
});

function appendLog(entry: EngineLogEntry) {
  const filter = logFilterChannel.value;
  const search = logSearchInput.value.toLowerCase().trim();

  const matchFilter = filter === "all" || entry.channel === filter;
  const matchSearch = !search || entry.text.toLowerCase().includes(search);

  logLineCounter.innerText = `${logHistory.length} lines`;

  if (!matchFilter || !matchSearch) return;

  const pre = document.createElement("pre");
  let colorClass = "text-base-content/80";
  if (entry.channel === "gfx" || entry.text.includes("[totk-gfx]") || entry.text.includes("[NVN]") || entry.text.includes("shader")) {
    colorClass = "text-accent font-semibold";
  } else if (entry.channel === "thread" || entry.text.includes("[totk-thread]")) {
    colorClass = "text-warning font-semibold";
  } else if (entry.channel === "error" || entry.text.includes("signal") || entry.text.includes("SIGSEGV") || entry.text.includes("fault") || entry.text.includes("error") || entry.text.includes("Error")) {
    colorClass = "text-error font-bold";
  } else if (entry.channel === "info" || entry.text.includes("[totk]")) {
    colorClass = "text-base-content";
  }

  pre.className = colorClass;
  pre.innerHTML = `<code>${escapeHtml(entry.text)}</code>`;
  logOutputContainer.appendChild(pre);

  if (logAutoscrollToggle.checked) {
    consoleTerminal.scrollTop = consoleTerminal.scrollHeight;
  }
}

function refilterLogs() {
  logOutputContainer.innerHTML = "";
  const filter = logFilterChannel.value;
  const search = logSearchInput.value.toLowerCase().trim();

  logHistory.forEach((entry) => {
    const matchFilter = filter === "all" || entry.channel === filter;
    const matchSearch = !search || entry.text.toLowerCase().includes(search);
    if (matchFilter && matchSearch) {
      appendLog(entry);
    }
  });
}

logFilterChannel?.addEventListener("change", refilterLogs);
logSearchInput?.addEventListener("input", refilterLogs);

btnClearLogs?.addEventListener("click", () => {
  logHistory = [];
  logOutputContainer.innerHTML = "";
  logLineCounter.innerText = "0 lines";
});

btnExportLogs?.addEventListener("click", async () => {
  const text = logHistory.map((l) => `[${new Date(l.timestamp).toISOString()}] [${l.channel}] ${l.text}`).join("\n");
  try {
    const filePath = await save({
      defaultPath: "ultratears-session.log",
      filters: [{ name: "Log Files", extensions: ["log", "txt"] }],
    });
    if (filePath) {
      const blob = new Blob([text], { type: "text/plain" });
      const a = document.createElement("a");
      a.href = URL.createObjectURL(blob);
      a.download = filePath;
      a.click();
    }
  } catch (err) {
    console.error("Export log error:", err);
  }
});

// Initialization
window.addEventListener("DOMContentLoaded", async () => {
  initTabs();
  await loadConfig();
  await refreshControllers();
  await checkEngineStatus();
});
