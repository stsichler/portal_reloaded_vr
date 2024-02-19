<div align="center">
  <p>
    <a align="center">
      <img width="auto" src="https://raw.githubusercontent.com/stsichler/portal_reloaded_vr/reloaded/imgs/logo.png"></a>
  </p>
</div>

# ![Portal 2 icon](imgs/icon.jpg "Portal 2 icon") Portal Reloaded VR
### ~~Use this mod at your own risk of getting VAC banned. Use the -insecure launch option to help protect yourself.~~
### Apparently Portal Reloaded doesn't have VAC, but just to be safe you should still run the game with the `insecure` flag.
This game contains flashing lights and fast motion sequences.

## Things that work
* 6DoF VR view
* Motion controls for portal gun and grabbable objects
* 3rd portal (use left trigger)

## Things that need fixing
* Use the game's own haptic feedback
* In-game UI and pause menu are broken
* 6DoF and Roomscale needs to be reimplemented
* CPU is underutilized

## How to use
1. Download [PortalReloadedVR.zip](https://github.com/stsichler/portal_reloaded_vr/releases) and extract the files to your Portal Reloaded directory (steamapps\common\Portal Reloaded)
2. Connect your headset, then launch Portal Reloaded with these launch options:
   
   ``` -insecure -window -novid +mat_motion_blur_percent_of_screen_max 0 +mat_queue_mode 0 +mat_vsync 0 +mat_antialias 0 +mat_grain_scale_override 0 -width 1280 -height 720 ```

3. To recenter the camera height, press down on the left stick. To see the HUD, aim the controller up or down.

## Troubleshooting
If you have no audio:
* Go to ```steamapps\common\Portal Reloaded\portal2_dlc3``` and execute ```UpdateSoundCache.cmd```

If the game isn't loading in VR:
* Try opening SteamVR before the game
* Disable SteamVR theater in [Steam settings](https://external-preview.redd.it/1WdLExouo_YKhTGT6C5GGrOjeWO7qNdIdDRvIRBhw-0.png?auto=webp&s=0d4447a9d954e1ec15b2c010cf50eeabd51f4197)

If the game is stuttering, try: 
* Steam Settings -> Shader Pre-Caching -> Allow background processing of Vulkan shaders

If the game is crashing, try:
* Lowering video settings
* Disabling all add-ons then verifying integrity of game files
* Re-installing the game

## Build instructions
1. Clone Repository
2. Open l4d2vr.sln
3. Set to x86 Release
4. Build -> Build Solution

Note: After building, it will attempt to copy the new d3d9.dll to your Portal Reloaded/bin directory.

## Based on
* Brilliant Portal 2 VR mod from [Gistix](https://github.com/Gistix/portal2vr)
* Portal Reloaded adaptions from [Scriptor25](https://github.com/Scriptor25)
* [l4d2vr](https://github.com/sd805/l4d2vr)
  
## Utilizes code from
* [VirtualFortress2](https://github.com/PinkMilkProductions/VirtualFortress2)
* [gmcl_openvr](https://github.com/Planimeter/gmcl_openvr/)
* [dxvk](https://github.com/TheIronWolfModding/dxvk/tree/vr-dx9-rel)
* [source-sdk-2013](https://github.com/ValveSoftware/source-sdk-2013/)

## Support the original author
<a href="https://www.paypal.com/donate/?business=YL7TGWKPCC9H8&no_recurring=0&currency_code=USD"><img src="https://pics.paypal.com/00/s/MDAwNDljNmUtZWZiZS00ZTI1LWFiMTMtZTdhZmQ5NmU5ZDUx/file.PNG" alt="Donate Button" style="width:auto;height:100px;"></a>

