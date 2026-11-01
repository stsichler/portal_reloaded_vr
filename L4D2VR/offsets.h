#pragma once
#include "sigscanner.h"
#include "game.h"
#include <iostream>


struct Offset
{
    std::string moduleName;
    int offset;
    int address = 0;
    std::string signature;
    int sigOffset;

    Offset(std::string moduleName, int currentOffset, std::string signature, int sigOffset = 0)
    {
        this->moduleName = moduleName;
        this->offset = currentOffset;
        this->signature = signature;
        this->sigOffset = sigOffset;

        int newOffset = SigScanner::VerifyOffset(moduleName, currentOffset, signature, sigOffset);

#ifdef L4D2VR_DIAGNOSTICS
        // Diagnostics: was the hard-coded offset still valid, and is the signature unique?
        std::cout << "[Offset] " << moduleName << " 0x" << std::hex << currentOffset << ": ";
        if (newOffset == 0)
            std::cout << "offset verified";
        else if (newOffset > 0)
            std::cout << "NOT verified, scan found 0x" << newOffset;
        else if (newOffset == -2)
            std::cout << "NOT verified, signature NOT UNIQUE";
        else
            std::cout << "NOT FOUND";
        std::cout << std::dec << ", " << SigScanner::CountMatches(moduleName, signature) << " match(es)"
            << " [" << signature.substr(0, 40) << (signature.size() > 40 ? "..." : "") << "]\n";
#endif

        if (newOffset > 0)
        {
            this->offset = newOffset;
        }
            
        if (newOffset == -1)
        {
            Game::errorMsg(("Signature not found: " + signature).c_str());
            return;
        }

        // Taking an arbitrary one of several matches would hook the wrong code.
        if (newOffset == -2)
        {
            Game::errorMsg(("Signature not unique: " + signature).c_str());
            return;
        }

        this->address = (uintptr_t)GetModuleHandle(moduleName.c_str()) + this->offset;
    }
};

class Offsets
{
public:
    Offset GetFullScreenTexture =        { "client.dll", 0x1A5610, "A1 ? ? ? ? 85 C0 75 53 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 6A 00 6A 01 68 ? ? ? ? 68 ? ? ? ? FF D2 50 B9 ? ? ? ? E8 ? ? ? ? 80 3D ? ? ? ? ? 75 1C 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 68 ? ? ? ? C6 05 ? ? ? ? ? FF D2 A1 ? ? ? ? C3" };
    Offset RenderView =                  { "client.dll", 0x1EF110, "55 8B EC 83 EC 2C 53 56 8B F1 6A 00 8D 8E ? ? ? ? E8 ? ? ? ?" };
    // GetClientMode(): returns g_pClientMode[active split screen slot]; the offset points at the array address
    Offset g_pClientMode =               { "client.dll", 0x286AC3, "8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 8B 04 85 ? ? ? ? C3 CC CC CC CC CC CC CC CC B8 ? ? ? ?", 19 };
    Offset CalcViewModelView =           { "client.dll", 0x279E40, "55 8B EC 83 EC 34 53 8B D9 80 BB" };
    Offset CreateMove =                  { "client.dll", 0x276AC0, "55 8B EC A1 ? ? ? ? 83 EC 0C 83 78 30 00 56 8B 75 0C 57 8B F9 74 43" };

    //Offset WriteUsercmdDeltaToBuffer =   { "client.dll", 0x134790, "55 8B EC 83 EC 60 0F 57 C0 8B 55 0C" }; //
    Offset WriteUsercmd =                { "client.dll", 0x1BF360, "55 8B EC A1 ? ? ? ? 83 78 30 00 53 8B 5D 0C 56 57" };
    Offset g_pppInput =                  { "client.dll", 0xD0B12, "8B 0D ? ? ? ? 8B 01 8B 50 68 FF E2", 2 };
    /*Offset AdjustEngineViewport =        { "client.dll", 0x41AD10, "55 8B EC 8B 0D ? ? ? ? 85 C9 74 17" };
    Offset IsSplitScreen =               { "client.dll", 0x1B2A60, "33 C0 83 3D ? ? ? ? ? 0F 9D C0" };*/
    Offset PrePushRenderTarget =         { "client.dll", 0xD0220, "55 8B EC 8B C1 56 8B 75 08 8B 0E 89 08 8B 56 04 89" };

    Offset ReadUserCmd =                 { "server.dll", 0x279450, "55 8B EC 53 8B 5D 10 56 57 8B 7D 0C 53" };
    Offset ProcessUsercmds =             { "server.dll", 0x16E960, "55 8B EC B8 ? ? ? ? E8 ? ? ? ? 0F 57 C0 53 56 57 B9 ? ? ? ? 8D 85 ? ? ? ? 33 DB" }; //?
    Offset CBaseEntity_entindex =        { "server.dll", 0x101F40, "8B 41 1C 85 C0 75 01 C3 8B 0D ? ? ? ? 2B 41 58 C1 F8 04 C3 CC"};
    Offset EyePosition =                 { "server.dll", 0xF32E0, "55 8B EC 56 8B F1 8B 86 ? ? ? ? C1 E8 0B A8 01 74 05 E8 ? ? ? ? 8B 45 08 F3 0F 10 86 D8 02 00 00" };

    /*Offset GetRenderTarget =             { "materialsystem.dll", 0x2CD30, "83 79 4C 00" };
    Offset Viewport =                    { "materialsystem.dll", 0x2E010, "55 8B EC 8B 45 0C 53 8B 5D" };
    Offset GetViewport =                 { "materialsystem.dll", 0x2CAF0, "55 8B EC 8B 41 4C 8B 49 40 8D 04 C0 83 7C 81 ? ?" };*/
    Offset PushRenderTargetAndViewport = { "materialsystem.dll", 0x2CED0, "55 8B EC 83 EC 24 8B 45 08 8B 55 10 89" };
    Offset PopRenderTargetAndViewport =  { "materialsystem.dll", 0x2C740, "56 8B F1 83 7E 4C 00" };

    //Offset TraceFirePortalClient =       { "client.dll", 0x3E0980, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A" };
    // Firing Portals
    Offset TraceFirePortalServer =       { "server.dll", 0x3FB0A0, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A" };
    Offset CWeaponPortalgun_FirePortal = { "server.dll", 0x3FB6C0, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 89 7D EC E8 ? ? ? ?" };

    //53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F1 6A 00 56 8D 4D C0 89 75 F8 E8
    //Offset DrawModelExecute =            { "engine.dll", 0xE05E0, "55 8B EC 81 EC ? ? ? ? A1 ? ? ? ? 33 C5 89 45 FC 8B 45 10 56 8B 75 08 57 8B" }; //
    Offset VGui_Paint =                  { "engine.dll", 0x126D70, "55 8B EC E8 ? ? ? ? 8B 10 8B C8 8B 52 38" };

    Offset PlayerPortalled = { "client.dll", 0x2790C0, "55 8B EC 83 EC 78 53 56 8B D9 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? 57 33 FF 57 FF D2" };

    // Ingame UI
    Offset DrawSelf = { "client.dll", 0x12AD00, "55 8B EC 56 8B F1 80 BE ? ? ? ? ? 0F 84 ? ? ? ? 8B 0D" };
    Offset ClipTransform = { "client.dll", 0x1DA100, "55 8B EC 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 8B 4D" };
    /*Offset VGui_GetHudBounds = { "client.dll", 0x1CC550, "55 8B EC 51 56 8B 75 08 8B CE" };
    Offset VGui_GetPanelBounds = { "client.dll", 0x1CC350, "55 8B EC 8B 45 08 8B C8 83 E1 1F BA ? ? ? ?" };

    Offset VGUI_UpdateScreenSpaceBounds = { "client.dll", 0x1CC8C0, "55 8B EC 83 EC 14 8B 45 0C 8B 4D 10 53 8B 5D 18 56 A3 ? ? ? ? 33 C0" };
    Offset VGui_GetTrueScreenSize = { "client.dll", 0x1CBCF0, "55 8B EC 8B 45 08 8B 0D ? ? ? ? 8B 55 0C 89 08 A1 ? ? ? ? 89 02 5D C3" };*/

    Offset VGui_GetClientDLLRootPanel = { "client.dll", 0x26B580, "8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 8B 04 85 ? ? ? ? 8B 48 04" };
    Offset g_pFullscreenRootPanel = { "client.dll", 0x26B5B2, "A1 ? ? ? ? C3 CC CC CC CC CC CC CC CC CC CC A1 ? ? ? ? 8B 48 04", 2 };

    // Pointer laser
    Offset CreatePingPointer = { "client.dll", 0x27CB80, "55 8B EC 83 EC 14 53 56 8B F1 8B 8E ? ? ? ? 57 85 C9 74 30" };
    //Offset ClientThink = { "client.dll", 0x27EA30, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ?" };
    Offset GetPortalPlayer = { "client.dll", 0x8DB90, "55 8B EC 8B 45 08 83 F8 FF 75 10 8B 0D ? ? ? ? 8B 01 8B 90 ? ? ? ? FF D2 8B 04 85 ? ? ? ?" };
    // Four string table precache helpers are identical except for the table; ParticleEffectNames is the second
    Offset PrecacheParticleSystem = { "server.dll", 0x16C5A0, "55 8B EC 8B 0D ? ? ? ? 8B 55 08 8B 01 8B 40 20 6A 00 6A FF 52 6A 01 FF D0 5D C3 CC CC CC CC 55 8B EC 56 8B 75 08 85 F6 74 24 8B 0D ? ? ? ? 8B 01 8B 50 30 56 FF D2 3D FF FF 00 00 75 11 56 68 ? ? ? ? FF 15 ? ? ? ? 83 C4 08 33 C0 5E 5D C3 CC CC CC CC CC CC CC CC CC CC CC CC 55 8B EC 8B 0D ? ? ? ? 8B 55 08 8B 01 8B 40 20 6A 00 6A FF 52 6A 01 FF D0 5D C3 CC CC CC CC 55" };
    // CPortal_Player::Precache (BaseClass::Precache, then 10 sound precaches)
    Offset Precache = { "server.dll", 0x356B30, "E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ?" };
    //Offset GetActivePortalWeapon = { "client.dll", 0x2A8910, "8B 89 ? ? ? ? 83 F9 FF 74 1F 8B 15 ? ? ? ?" };

    Offset SetControlPoint = { "client.dll", 0x1791E0, "55 8B EC 53 56 8B 75 0C 57 8B F9 BB ? ? ? ? 84 9F ? ? ? ?" };
    Offset SetDrawOnlyForSplitScreenUser = { "client.dll", 0x178E90, "55 8B EC 8B 45 08 53 8B D9 3B 83 ? ? ? ? 74 55" };
    Offset StopEmission = { "client.dll", 0x178B50, "55 8B EC 53 8B 5D 08 57 8B F9 F6 87 ? ? ? ? ? 74 7F" };

    // Aim related
    Offset CHudCrosshair_ShouldDraw = { "client.dll", 0x13F4A0, "57 8B F9 80 BF ? ? ? ? ? 74 04 32 C0 5F C3" };

    // VR Eyes
    Offset UTIL_Portal_FirstAlongRay = { "server.dll", 0x373080, "55 8B EC 8B 0D ? ? ? ? 85 C9 74 19 A1 ? ? ? ?" };
    Offset UTIL_IntersectRayWithPortal = { "server.dll", 0x372550, "55 8B EC 83 EC 48 56 8B 75 0C 85 F6 0F 84 ? ? ? ?" };
    Offset UTIL_Portal_AngleTransform = { "server.dll", 0x371AC0, "55 8B EC 8B 45 08 8B 4D 0C 83 EC 0C 50 51 8D 55 F4" };

    /*Offset GetScreenSize = { "vguimatsurface.dll", 0xB8C0, "55 8B EC 83 EC 08 80 B9 ? ? ? ? ? 74 1C" };
    Offset GetHudSize = { "client.dll", 0x1CBCD0, "55 8B EC 8B 55 0C 8B 0D ? ? ? ? 8B 01 8B 80 ? ? ? ? 52 8B 55 08 52 FF D0 5D C3" };

    Offset SetSizeC = { "client.dll", 0x63FB70, "55 8B EC 8B 41 04 8B 50 04 8B 45 0C 56 8B 35 ? ? ? ?" };
    Offset SetSizeE = { "engine.dll", 0x298620, "55 8B EC 8B 41 04 8B 50 04 8B 45 0C 56 8B 35 ? ? ? ?" };
    Offset SetSizeV = { "vguimatsurface.dll", 0x4B6D0, "55 8B EC 8B 41 04 8B 50 04 8B 45 0C 56 8B 35 ? ? ? ?" };

    //Offset SetBoundsC = { "client.dll", 0x63FBF0, "55 8B EC 8B 55 0C 53 56 8B F1 8B 46 04 8B 48 04 8B 45 08 57 8B 3D ? ? ? ?" };
    Offset SetBoundsE = { "engine.dll", 0x2986A0, "55 8B EC 8B 55 0C 53 56 8B F1 8B 46 04 8B 48 04 8B 45 08 57 8B 3D ? ? ? ? 8B 1F 8D 4C 31 04 52 8B 11 50 8B 02 FF D0 8B 53 08 50 8B CF FF D2" };*/
   

    /*Offset Push2DView = { "engine.dll", 0xDF980, "55 8B EC 51 53 8B D9 8B 83 ? ? ? ? 56 8D B3 ? ? ? ? 57 89 5D FC 3B 46 04 7C 09" };
    Offset Render = { "client.dll", 0x1D6800, "55 8B EC 81 EC ? ? ? ? 53 56 57 8B F9 8B 0D ? ? ? ? 89 7D F4 FF 15 ? ? ? ?" };
    Offset GetClipRect = { "vguimatsurface.dll", 0x4C700, "55 8B EC 8B 81 ? ? ? ? 8B 50 04 8B 45 14 56 8B 35 ? ? ? ? 57 8B 3E 8D 8C 0A ? ? ? ? 8B 55 10 50" };
    //Offset GetWeaponCrosshairScale = {}
    Offset GetModeHeight = { "engine.dll", 0x1F9F10, "8B 81 ? ? ? ? C3" };*/

    //Grababbles
    //Offset Weapon_ShootPosition =        { "client.dll", 0x2A8A60, "55 8B EC 8B 01 8B 90 ? ? ? ? 56 8B 75 08 56 FF D2 8B C6 5E 5D C2 04 00" };
    // return EyePosition(); EyePosition is vtable offset 0x200
    Offset Weapon_ShootPosition = { "server.dll", 0x363F80, "55 8B EC 8B 01 8B 90 00 02 00 00 56 8B 75 08 56 FF D2 8B C6 5E 5D C2 04 00" };
    Offset ComputeError = { "server.dll", 0x3C2C70, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 8B F1 8B 86 ? ? ? ? 57 83 F8 FF 74 2A" };
    Offset UpdateObject = { "server.dll", 0x3C4B40, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 8B 87 ? ? ? ? 89 BD" };
    Offset UpdateObjectVM = { "server.dll", 0x3C6610, "53 8B DC 83 EC 08 83 E4 F0 83 C4 04 55 8B 6B 04 89 6C 24 04 8B EC 81 EC ? ? ? ? 56 57 8B F9 8B 87 ? ? ? ? 83 F8" };
    Offset RotateObject = { "server.dll", 0x3C23C0, "55 8B EC 0F 57 C0 F3 0F 10 4D ? 81 EC ? ? ? ? 0F 2E C8 9F 57 8B F9 F6 C4 44 7A 12" };
    Offset EyeAngles = { "server.dll", 0x102C20, "55 8B EC 8B 81 ? ? ? ? 83 EC 60 56 57 8B 3D ? ? ? ? 83 F8 FF 74 1D" };

    // For Portal gun VFX (do we really need all three??)
    Offset MatrixBuildPerspectiveX = { "engine.dll", 0x271DB0, "55 8B EC 83 EC 08 F2 0F 10 45 ? F2 0F 59 05 ? ? ? ? F2 0F 11 45 F8 F2 0F 10 45 0C F2 0F 59 05 ? ? ? ? F2 0F 59 05 ? ? ? ? 66 0F 5A C0 E8 ? ? ? ? F2 0F 10 4D F8" };
    Offset GetFOV = { "client.dll", 0x273AD0, "55 8B EC 51 56 8B F1 E8 ? ? ? ? D9 5D FC 8B 06 8B 90 ? ? ? ? 8B CE FF D2" };
    Offset GetDefaultFOV = { "client.dll", 0x2756C0, "A1 ? ? ? ? F3 0F 2C 40 ? C3" };
    // ClientModePortalNormal::GetViewModelFOV (cl_viewmodelfov); ClientModeShared's version reads viewmodel_fov
    Offset GetViewModelFOV = { "client.dll", 0x287050, "A1 ? ? ? ? D9 40 2C C3 CC CC CC CC CC CC CC 56" };

    // Multiplayer
    Offset GetOwner = { "server.dll", 0xD6590, "8B 81 ? ? ? ? 83 F8 FF 74 23 8B 15 ? ? ? ?" };
    //Offset GetActiveWeapon = { "server.dll", 0xD3FD0, "8B 89 ? ? ? ? 83 F9 FF 74 1F 8B 15 ? ? ? ?" };
};