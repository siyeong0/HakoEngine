# HakoEngine

**Game engine for real-time rendering and graphics research.**

HakoEngine is a game/graphics engine focused on fast iteration for rendering research.
* **Modular architecture** — foundational static libraries and decoupled dynamic modules.
* **Modern C++ core** — C++20 baseline with clean RAII and constexpr.
* **Ray-tracing** — DXR based real-time ray tracing support.

## Build & Run

> Windows is the primary target today. Linux builds are planned (Vulkan path).

### Prerequisites (Windows)

* **Visual Studio 2022** (MSVC, C++20)
* **Windows 10/11**, **DX12 + DXR‑capable GPU** (for ray tracing samples)
* **vcpkg** (optional) for Assimp / stb_image / imgui / etc.

## References
* https://github.com/megayuchi/D3D12Lecture
* https://github.com/megayuchi/DXRLecture
* https://learn.microsoft.com/en-us/samples/microsoft/directx-graphics-samples/d3d12-raytracing-samples-win32/
