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

#[tauri::command]
pub fn validate_romfs(path: String) -> Result<RomFsStatus, String> {
    if path.trim().is_empty() {
        return Ok(RomFsStatus {
            status: "Missing".to_string(),
            total_files: 0,
            total_bytes: 0,
            path: "".to_string(),
            has_version: false,
            has_pack: false,
            has_shader: false,
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

    let script_path = engine_path.join("tools/setup.py");
    if !script_path.exists() {
        let _ = app.emit(
            "romfs-pack-progress",
            PackProgressEvent {
                message: format!("Notice: {} not found; creating direct index", script_path.display()),
                percent: 100.0,
                is_finished: true,
                success: false,
            },
        );
        return Err(format!("tools/setup.py not found in {}", engine_path.display()));
    }

    let mut cmd = Command::new("python3");
    cmd.arg(&script_path)
        .arg("--only")
        .arg("romfs")
        .arg("--romfs")
        .arg(&source_path)
        .arg("--output")
        .arg(&output_bin)
        .current_dir(&engine_path)
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped());

    let mut child = cmd.spawn().map_err(|e| format!("Failed to spawn python: {}", e))?;
    let stdout = child.stdout.take().ok_or_else(|| "Failed to capture stdout".to_string())?;
    let mut reader = BufReader::new(stdout).lines();

    let mut lines_count = 0;
    while let Ok(Some(line)) = reader.next_line().await {
        lines_count += 1;
        let pct = (lines_count as f32 * 4.0).min(90.0);
        let _ = app.emit(
            "romfs-pack-progress",
            PackProgressEvent {
                message: line,
                percent: pct,
                is_finished: false,
                success: true,
            },
        );
    }

    let status = child.wait().await.map_err(|e| format!("Wait child error: {}", e))?;
    let success = status.success();
    let _ = app.emit(
        "romfs-pack-progress",
        PackProgressEvent {
            message: if success {
                "RomFS packing completed successfully!".to_string()
            } else {
                "RomFS packing exited with errors".to_string()
            },
            percent: 100.0,
            is_finished: true,
            success,
        },
    );

    if success {
        Ok(())
    } else {
        Err(format!("Packer failed with exit code {:?}", status.code()))
    }
}
