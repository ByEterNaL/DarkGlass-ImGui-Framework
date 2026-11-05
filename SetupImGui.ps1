$repoUrl = "https://raw.githubusercontent.com/ocornut/imgui/master"
$files = @(
    "imgui.cpp", "imgui.h", "imgui_demo.cpp", "imgui_draw.cpp", "imgui_internal.h",
    "imgui_tables.cpp", "imgui_widgets.cpp", "imstb_rectpack.h", "imstb_textedit.h", "imstb_truetype.h",
    "backends/imgui_impl_win32.cpp", "backends/imgui_impl_win32.h",
    "backends/imgui_impl_dx11.cpp", "backends/imgui_impl_dx11.h"
)

$targetDir = Join-Path $PSScriptRoot "imgui"
New-Item -ItemType Directory -Force -Path $targetDir

[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

foreach ($file in $files) {
    $url = "$repoUrl/$file"
    $filename = Split-Path $file -Leaf
    $dest = Join-Path $targetDir $filename
    Write-Host "Downloading $filename..."
    Invoke-WebRequest -Uri $url -OutFile $dest
}
Write-Host "ImGui download complete."
