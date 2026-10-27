fn main() {
    if let Ok(home) = std::env::var("HOME") {
        println!("cargo:rustc-link-search=native={}/.local/usr/lib64", home);
    }
    tauri_build::build()
}
