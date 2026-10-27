use serde::{Deserialize, Serialize};
use std::fs;
use std::path::{Path, PathBuf};
use tauri::{AppHandle, Emitter};
use tokio::io::{AsyncBufReadExt, BufReader};
use tokio::process::Command;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RomFsStatus {
    pub status: String,
    pub total_files: usize,
    pub total_bytes: u64,
    pub path: String,
    pub has_version: bool,
    pub has_pack: bool,
    pub has_shader: bool,
    pub has_shader_pack: bool,
    pub shader_pack_bytes: u64,
    pub loose_shader_count: usize,
    pub shader_pack_path: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PackProgressEvent {
    pub message: String,
    pub percent: f32,
    pub is_finished: bool,
    pub success: bool,
}

fn scan_dir_recursive(dir: &Path, count: &mut usize, bytes: &mut u64, depth: usize) {
    if depth > 8 {
        return;
    }
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            if let Ok(meta) = entry.metadata() {
                if meta.is_file() {
                    *count += 1;
                    *bytes += meta.len();
                } else if meta.is_dir() {
                    scan_dir_recursive(&entry.path(), count, bytes, depth + 1);
                }
            }
        }
    }
}

fn count_spv_files(dir: &Path) -> usize {
    let mut count = 0;
    if let Ok(entries) = fs::read_dir(dir) {
        for entry in entries.flatten() {
            if let Ok(meta) = entry.metadata() {
                if meta.is_file() {
                    if let Some(ext) = entry.path().extension() {
                        if ext == "spv" {
                            count += 1;
                        }
                    }
                }
            }
        }
    }
    count
}

#[tauri::command]
pub fn validate_romfs(path: String) -> Result<RomFsStatus, String> {
    let mut has_shader_pack = false;
    let mut shader_pack_bytes = 0u64;
    let mut loose_shader_count = 0usize;
    let mut found_pack_path = String::new();

    // Check default / known shader pack locations
    let shader_pack_candidates = [
        PathBuf::from("data/shaders/ryujinx/shaders.pack"),
        PathBuf::from("data/shaders/shaders.pack"),
        PathBuf::from("refs/ultratears/data/shaders/ryujinx/shaders.pack"),
        PathBuf::from(&path).join("shaders.pack"),
        PathBuf::from(&path).join("data/shaders/ryujinx/shaders.pack"),
        PathBuf::from(&path).join("Shader/shaders.pack"),
    ];

    for c in &shader_pack_candidates {
        if c.is_file() {
            if let Ok(meta) = c.metadata() {
                has_shader_pack = true;
                shader_pack_bytes = meta.len();
                found_pack_path = c.to_string_lossy().to_string();
                break;
            }
        }
    }

    // Check loose shaders
    let loose_dirs = [
        PathBuf::from("data/shaders/ryujinx"),
        PathBuf::from("refs/ultratears/data/shaders/ryujinx"),
        PathBuf::from(&path).join("Shader"),
        PathBuf::from(&path).join("data/shaders/ryujinx"),
    ];
    for d in &loose_dirs {
        if d.is_dir() {
            let cnt = count_spv_files(d);
            if cnt > loose_shader_count {
                loose_shader_count = cnt;
            }
        }
    }

    if path.trim().is_empty() {
        return Ok(RomFsStatus {
            status: "Missing".to_string(),
            total_files: 0,
            total_bytes: 0,
            path: "".to_string(),
            has_version: false,
            has_pack: false,
            has_shader: false,
            has_shader_pack,
            shader_pack_bytes,
            loose_shader_count,
            shader_pack_path: found_pack_path,
        });
    }

    let p = PathBuf::from(&path);
    if !p.exists() {
        return Ok(RomFsStatus {
            status: "Missing".to_string(),
            total_files: 0,
            total_bytes: 0,
            path,
            has_version: false,
            has_pack: false,
            has_shader: false,
            has_shader_pack,
            shader_pack_bytes,
            loose_shader_count,
            shader_pack_path: found_pack_path,
        });
    }

    if p.is_file() {
        let meta = p.metadata().map_err(|e| e.to_string())?;
        return Ok(RomFsStatus {
            status: "PackedVerified".to_string(),
            total_files: 1,
            total_bytes: meta.len(),
            path,
            has_version: true,
            has_pack: true,
            has_shader: true,
            has_shader_pack,
            shader_pack_bytes,
            loose_shader_count,
            shader_pack_path: found_pack_path,
        });
    }

    let romfs_bin = p.join("romfs.bin");
    let romfs_bin_data = p.join("data/romfs/romfs.bin");
    if romfs_bin.is_file() {
        let meta = romfs_bin.metadata().map_err(|e| e.to_string())?;
        return Ok(RomFsStatus {
            status: "PackedVerified".to_string(),
            total_files: 1,
            total_bytes: meta.len(),
            path,
            has_version: true,
            has_pack: true,
            has_shader: true,
            has_shader_pack,
            shader_pack_bytes,
            loose_shader_count,
            shader_pack_path: found_pack_path,
        });
    }
    if romfs_bin_data.is_file() {
        let meta = romfs_bin_data.metadata().map_err(|e| e.to_string())?;
        return Ok(RomFsStatus {
            status: "PackedVerified".to_string(),
            total_files: 1,
            total_bytes: meta.len(),
            path,
            has_version: true,
            has_pack: true,
            has_shader: true,
            has_shader_pack,
            shader_pack_bytes,
            loose_shader_count,
            shader_pack_path: found_pack_path,
        });
    }

    let has_version = p.join("System/Version/ett.byml").exists() || p.join("System").is_dir();
    let has_pack = p.join("Pack").is_dir();
    let has_shader = p.join("Shader").is_dir();

    let mut total_files = 0;
    let mut total_bytes = 0;
    scan_dir_recursive(&p, &mut total_files, &mut total_bytes, 0);

    let status = if has_version || has_pack || total_files > 10 {
        "LooseVerified".to_string()
    } else {
        "Missing".to_string()
    };

    Ok(RomFsStatus {
        status,
        total_files,
        total_bytes,
        path,
        has_version,
        has_pack,
        has_shader,
        has_shader_pack,
        shader_pack_bytes,
        loose_shader_count,
        shader_pack_path: found_pack_path,
    })
}

#[tauri::command]
pub async fn pack_romfs(
    app: AppHandle,
    engine_dir: String,
    source_path: String,
    output_bin: String,
) -> Result<(), String> {
    let engine_path = if engine_dir.is_empty() {
        PathBuf::from(".")
    } else {
        PathBuf::from(engine_dir)
    };

    let script_candidates = [
        engine_path.join("tools/setup.py"),
        PathBuf::from("refs/ultratears/tools/setup.py"),
        PathBuf::from("tools/setup.py"),
    ];

    let script_path = script_candidates
        .iter()
        .find(|p| p.exists())
        .cloned()
        .unwrap_or_else(|| engine_path.join("tools/setup.py"));

    if !script_path.exists() {
        let _ = app.emit(
            "romfs-pack-progress",
            PackProgressEvent {
                message: format!("Notice: {} not found; direct loose verification active", script_path.display()),
                percent: 100.0,
                is_finished: true,
                success: false,
            },
        );
        return Err(format!("Script {} does not exist", script_path.display()));
    }

    let mut cmd = Command::new("python3");
    cmd.arg(&script_path)
        .arg("--only")
        .arg("romfs")
        .arg("--romfs")
        .arg(&source_path)
        .arg("--out")
        .arg(&output_bin);

    cmd.stdout(std::process::Stdio::piped());
    cmd.stderr(std::process::Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn setup.py: {}", e))?;
    let stdout = child.stdout.take();
    let app_clone = app.clone();

    if let Some(out) = stdout {
        tokio::spawn(async move {
            let mut reader = BufReader::new(out).lines();
            let mut pct = 10.0f32;
            while let Ok(Some(line)) = reader.next_line().await {
                pct = (pct + 5.0).min(95.0);
                let _ = app_clone.emit(
                    "romfs-pack-progress",
                    PackProgressEvent {
                        message: line,
                        percent: pct,
                        is_finished: false,
                        success: true,
                    },
                );
            }
        });
    }

    let status = child.wait().await.map_err(|e| e.to_string())?;
    let success = status.success();
    let _ = app.emit(
        "romfs-pack-progress",
        PackProgressEvent {
            message: if success {
                "RomFS indexing complete (romfs.bin ready)".to_string()
            } else {
                "RomFS packing failed; check console".to_string()
            },
            percent: 100.0,
            is_finished: true,
            success,
        },
    );

    if success {
        Ok(())
    } else {
        Err("Pack process exited with error status".to_string())
    }
}

#[tauri::command]
pub async fn pack_shaders(
    app: AppHandle,
    source_dir: String,
    output_pack: String,
) -> Result<(), String> {
    let script_candidates = [
        PathBuf::from("tools/pack_shaders.py"),
        PathBuf::from("refs/ultratears/tools/pack_shaders.py"),
        PathBuf::from("../../tools/pack_shaders.py"),
    ];

    let script_path = script_candidates
        .iter()
        .find(|p| p.exists())
        .cloned()
        .unwrap_or_else(|| PathBuf::from("tools/pack_shaders.py"));

    let target_dir = if source_dir.trim().is_empty() {
        "data/shaders/ryujinx".to_string()
    } else {
        source_dir
    };

    let _ = app.emit(
        "shader-pack-progress",
        PackProgressEvent {
            message: format!("Starting shader packer on '{}'...", target_dir),
            percent: 5.0,
            is_finished: false,
            success: true,
        },
    );

    let mut cmd = Command::new("python3");
    cmd.arg(&script_path).arg(&target_dir);
    if !output_pack.trim().is_empty() {
        cmd.arg(&output_pack);
    }

    cmd.stdout(std::process::Stdio::piped());
    cmd.stderr(std::process::Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn pack_shaders.py: {}", e))?;
    let stdout = child.stdout.take();
    let app_clone = app.clone();

    if let Some(out) = stdout {
        tokio::spawn(async move {
            let mut reader = BufReader::new(out).lines();
            while let Ok(Some(line)) = reader.next_line().await {
                let mut pct = 50.0f32;
                if let Some(pos) = line.find('(') {
                    if let Some(end) = line[pos..].find("%)") {
                        let pct_str = &line[pos + 1..pos + end];
                        if let Ok(val) = pct_str.parse::<f32>() {
                            pct = val;
                        }
                    }
                }
                let _ = app_clone.emit(
                    "shader-pack-progress",
                    PackProgressEvent {
                        message: line,
                        percent: pct,
                        is_finished: false,
                        success: true,
                    },
                );
            }
        });
    }

    let status = child.wait().await.map_err(|e| e.to_string())?;
    let success = status.success();
    let _ = app.emit(
        "shader-pack-progress",
        PackProgressEvent {
            message: if success {
                "Shader archive packing complete (shaders.pack ready)".to_string()
            } else {
                "Shader packing failed; check logs".to_string()
            },
            percent: 100.0,
            is_finished: true,
            success,
        },
    );

    if success {
        Ok(())
    } else {
        Err("Shader pack process exited with error status".to_string())
    }
}
