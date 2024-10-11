<div align="center">
  <p>
    <a align="center">
      <img width="auto" src="https://raw.githubusercontent.com/stsichler/portal_reloaded_vr/revolution/imgs/logo.png"></a>
  </p>
</div>

# ![Portal 2 icon](imgs/icon.jpg "Portal 2 icon") Portal Revolution VR Mod - **CURRENTLY NOT WORKING!**

# HELP NEEDED! HELP NEEDED!

This Repo is intended as a **starting point** for implementation of a VR Mod for Portal Revolution. Feel free to contribute!


## Things that work
* Injection of [DXVK](https://github.com/doitsujin/dxvk)-based D3D11 DLL -> game now runs on Vulkan (on Flatscreen, **NO VR!**)
* Ready to re-insert necessary hooks into game engine and graphics

## Things that need fixing
* Update Source-SDK related files
* Re-establish hooks into game engine
* Port D3D-related code from D3D9 to D3D11

## Build instructions
1. Clone Repository
2. Run ```git submodule update --init --recursive```
3. Run ```dxvk-apply-patches.bat```
4. Open l4d2vr.sln
5. Set to x64 Debug or Release
6. Build -> Build Solution

Note: After building, it will attempt to copy the new d3d11.dll and dxgi.dll to your Portal Revolution/bin/win64 directory.

## Based on
* Brilliant Portal 2 VR mod from [Gistix](https://github.com/Gistix/portal2vr)
* Portal Reloaded adaptions from [Scriptor25](https://github.com/Scriptor25)
* [l4d2vr](https://github.com/sd805/l4d2vr)
  
## Utilizes code from
* [VirtualFortress2](https://github.com/PinkMilkProductions/VirtualFortress2)
* [gmcl_openvr](https://github.com/Planimeter/gmcl_openvr/)
* [dxvk](https://github.com/TheIronWolfModding/dxvk/tree/vr-dx9-rel)
* [source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013/)
