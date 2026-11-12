# Device test plan (user-run)

এই পরিকল্পনা শুধু পরীক্ষার ধাপ ও pass criteria বর্ণনা করে। এখান থেকে কোনো ফলাফল ধরে নেওয়া যাবে না। এগুলো agent-এর host-এ চালানো হয়নি।

## 1. Hardware GPU correctness (Vulkan 1.1)
- Build: `cmake -S modules/color_engine -B build_dev -DCOLOR_BUILD_VULKAN=ON -DCOLOR_GLSLANG=<glslang> -DCOLOR_VULKAN_HEADERS=<dir> -DCOLOR_REQUIRE_SHADER_VALIDATION=ON`
- Run `color_gpu_parity`, `color_vulkan_test`. Record device name, driver, and OS version.
- Pass: every assertion passes at 1e-5 (operators) and 2e-4 (`GpuGradeEngine` DAG). If a device fails, record which operator and the max error.

## 2. Mobile performance (phone)
- Run `color_bench` with 1920x1080 and 3840x2160 frames, 20+ runs. Record device model, SoC, thermal state, battery, and power mode before and after.
- Report min/median/max. CPU reference numbers are not a GPU or phone performance result.

## 3. Thermal
- Run 10+ minutes of continuous 4K graded render. Log frame time every second and temperature. Note throttle onset.

## 4. 4K playback
- Decode, grade, and display a 4K clip for 5 minutes. Count dropped frames and measure the sustained frame rate.

## 5. Codec / export
- Export graded output with the target codec and verify with an independent decoder. Compare frames against the CPU reference within the stated tolerance.

## 6. Metal (Apple)
- Run `modules/color_engine/tests/validate_metal.sh` on macOS with Xcode. The script is UNRUN until this is done.

## Not in scope of this plan
RAW camera decoder, AI/object tracking, DCTL, film emulation, restoration, full auto-grade, Dolby/HDR10+ metadata.
