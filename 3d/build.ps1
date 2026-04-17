# Build script for 3D Radiance Cascades
# Run from the 3d directory

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Building 3D Radiance Cascades" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Check if we're in the right directory
if (-not (Test-Path "CMakeLists.txt")) {
    Write-Host "ERROR: Please run this script from the 3d/ directory" -ForegroundColor Red
    exit 1
}

# Create build directory
if (-not (Test-Path "build")) {
    Write-Host "Creating build directory..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Force -Path "build" | Out-Null
}

# Navigate to build directory
Set-Location build

# Run CMake
Write-Host "Running CMake..." -ForegroundColor Yellow
cmake ..

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: CMake configuration failed" -ForegroundColor Red
    exit 1
}

# Build
Write-Host ""
Write-Host "Building project..." -ForegroundColor Yellow
cmake --build . --config Release

if ($LASTEXITCODE -ne 0) {
    Write-Host "ERROR: Build failed" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Green
Write-Host "Build successful!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Green
Write-Host ""
Write-Host "Executable location: build/RadianceCascades3D.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "To run:" -ForegroundColor Yellow
Write-Host "  cd build" -ForegroundColor Gray
Write-Host "  ./RadianceCascades3D.exe" -ForegroundColor Gray
Write-Host ""
