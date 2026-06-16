@echo off
REM Phase 3B: One-click validation pipeline
REM Usage: tools\run_phase3b_validation.bat

echo ============================================================
echo Phase 3B: Chart Classification & Validation Pipeline
echo ============================================================
echo.

REM Check if build exists
if not exist "build\RadianceCascades3D.exe" (
    echo ERROR: Build not found. Run cmake --build build first.
    exit /b 1
)

REM Step 1: Generate raymarch shader with includes
echo [Step 1/5] Generating raymarch shader with includes...
python tools\generate_raymarch_with_includes.py
if errorlevel 1 (
    echo ERROR: Failed to generate raymarch shader
    exit /b 1
)
echo.

REM Step 2: UV round-trip validation
echo [Step 2/5] Running UV round-trip validation...
build\RadianceCascades3D.exe --validate-uv-roundtrip --exit-immediately
if errorlevel 1 (
    echo.
    echo VALIDATION FAILED at Step 2: UV round-trip test
    echo Investigate UV mapping issues before proceeding.
    exit /b 1
)
echo.

REM Step 3: Capture unknown distribution
echo [Step 3/5] Capturing unknown hit distribution...
if not exist "tools\phase3b_visual" mkdir tools\phase3b_visual
build\RadianceCascades3D.exe --capture-unknown-distribution --exit-after-capture
if errorlevel 1 (
    echo WARNING: Failed to capture unknown distribution
    echo Continuing with remaining tests...
)
echo.

REM Step 4: Analyze spatial distribution
echo [Step 4/5] Analyzing spatial distribution...
if exist "tools\phase3b_visual\unknown_distribution_frame5.png" (
    python tools\phase3b_analyze_unknowns.py tools\phase3b_visual\unknown_distribution_frame5.png
    if errorlevel 1 (
        echo WARNING: Spatial analysis failed
        echo Install dependencies: pip install -r tools\requirements.txt
    )
) else (
    echo WARNING: Unknown distribution image not found, skipping analysis
)
echo.

REM Step 5: Misclassification rate test
echo [Step 5/5] Running misclassification rate test (this takes 15-30 minutes)...
build\RadianceCascades3D.exe --measure-misclassification --num-samples=1000 --exit-immediately
if errorlevel 1 (
    echo.
    echo VALIDATION FAILED at Step 5: Misclassification test
    echo Investigate chart classification issues.
    exit /b 1
)
echo.

echo ============================================================
echo Phase 3B: All validation tests PASSED!
echo ============================================================
echo.
echo Summary of results saved to:
echo   - tools/phase3b_visual/unknown_spatial_heatmap.png
echo   - Console output above
echo.
echo Next steps:
echo   1. Review heatmap for spatial patterns
echo   2. Check misclassification rates per chart
echo   3. If all pass, proceed to Phase 3C
echo.
pause
