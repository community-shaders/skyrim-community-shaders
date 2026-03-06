Reflex Build Notes
==================

- Active build uses `CMakeLists.txt` from DEV.
- Reflex runtime-staging CMake logic is parked in `CMakeLists.txt.REFLEX` so it is not used by default. 
- You have to decide yourself how implement this best for your repo. There are several options, this I how I did it.

To ensure that Reflex is detected
----------------------------------
- Local self-built Streamline plugins are unsigned.
- In non-Develop runtime mode, the interposer enforces signature/security checks and Reflex/PCL may not load.
- Building Streamline runtime in Develop mode (`-develop`, artifacts in `Develop_x64`) allows local unsigned runtime modules to load.

Comnpiled runtime DLLs to keep version-aligned (DEV has 2.10.0, I updated submodules and have 2.10.3)
------------------------------------
- `sl.common.dll`
- `sl.interposer.dll`
- `sl.dlss.dll`
- `sl.reflex.dll`
- `sl.pcl.dll`

Submodule Requirement
---------------------
- Reflex/PCL support depends on the updated `extern/Streamline-DX12` revision used by this branch.
- I Kept the extern update when testing/building Reflex.

Manual Build Flow for Reflex Runtime
------------------------------------
From `extern/Streamline-DX12`:
1. `setup.bat vs2022`
2. `build.bat -develop`
3. Copy from:
   - `_artifacts/sl.common/Develop_x64/sl.common.dll`
   - `_artifacts/sl.interposer/Develop_x64/sl.interposer.dll`
   - `_artifacts/sl.dlss/Develop_x64/sl.dlss.dll`
   - `_artifacts/sl.reflex/Develop_x64/sl.reflex.dll`
   - `_artifacts/sl.pcl/Develop_x64/sl.pcl.dll`
   into `features/Upscaling/Shaders/Upscaling/Streamline`.
4. Keep these five runtime DLLs from the same Streamline build output so versions stay in sync.

How to Re-enable Automated CMake Staging
----------------------------------------
- Use the content in `CMakeLists.txt.REFLEX` as the staging template.

Runtime Verification
--------------------
Check logs for:
- `[Streamline] Reflex ... available`
- `[Streamline] PCL ... available`
- no errors for missing `sl.common.dll`, `sl.interposer.dll`, `sl.dlss.dll`, `sl.reflex.dll`, or `sl.pcl.dll`

In-game:
- Open Upscaling settings and verify the `NVIDIA Reflex` section is enabled.
