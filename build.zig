const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const exe = b.addExecutable(.{
        .name = "totk_recomp",
        .target = target,
        .optimize = optimize,
    });

    exe.addIncludePath(b.path("src"));
    exe.addIncludePath(b.path("third_party/volk"));
    exe.addIncludePath(b.path("third_party/vulkan-headers/include"));
    exe.addIncludePath(b.path("third_party/vma/include"));
    exe.addIncludePath(b.path("third_party/simde"));
    exe.addIncludePath(b.path("third_party/imgui"));
    exe.addIncludePath(b.path("third_party/imgui/backends"));
    exe.addIncludePath(b.path("third_party/sdl3/include"));

    exe.addCSourceFiles(.{
        .files = &.{
            "third_party/volk/volk.c",
            "third_party/imgui/imgui.cpp",
            "third_party/imgui/imgui_draw.cpp",
            "third_party/imgui/imgui_tables.cpp",
            "third_party/imgui/imgui_widgets.cpp",
            "third_party/imgui/backends/imgui_impl_sdl3.cpp",
            "third_party/imgui/backends/imgui_impl_vulkan.cpp",
            "src/graphics/nvn_vk.cpp",
            "src/graphics/vk_pipeline_cache.cpp",
            "src/ui/imgui_style.cpp",
            "src/ui/studio.cpp",
            "src/ui/overlay.cpp",
            "src/runtime/paths.c",
            "src/runtime/dispatch.c",
            "src/runtime/platform/linux.c",
            "src/main.cpp",
        },
        .flags = &.{
            "-std=c++20",
            "-DVK_NO_PROTOTYPES",
            "-mavx2",
            "-mfma",
            "-O2",
        },
    });

    exe.linkLibC();
    exe.linkLibCpp();
    b.installArtifact(exe);
}
