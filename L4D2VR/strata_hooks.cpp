#include "d3d11_hooks.h"
#include "strata_hooks.h"
#include <Windows.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "MinHook.h"
#include "game.h"
#include "sdk.h"
#include "vr.h"

namespace
{
    // CViewRender (client.dll): [5] Render(vrect_t *), [6] RenderView(view, hudView, nClearFlags,
    // whatToDraw); checked in client.dll ("CViewRender::Render", which calls [6] with both views).
    constexpr int kViewRender_RenderView = 6;
    constexpr int RENDERVIEW_DRAWHUD = 1 << 1; // Render() sets it when there is a local player

    using tRenderView = void (*)(void *thisptr, const CViewSetup &view, const CViewSetup &hudView, int nClearFlags, int whatToDraw);
    tRenderView oRenderView = nullptr;

    // ClientModeShared (client.dll): [24] CreateMove(flInputSampleTime, cmd), called by
    // CInput::CreateMove; if it returns true, the engine takes over cmd->viewangles as its view
    // angles.
    constexpr int kClientMode_CreateMove = 24;

    using tCreateMove = bool (*)(void *thisptr, float flInputSampleTime, CUserCmd *cmd);
    tCreateMove oCreateMove = nullptr;

    // C_BaseViewModel (client.dll): [254] CalcViewModelView(owner, eyePosition, eyeAngles), adds
    // lag [253], bob [255] and shake, then sets the viewmodel's position. C_PredictedViewModel's
    // [253] only calls this one.
    constexpr int kViewModel_CalcViewModelLag = 253;
    constexpr int kViewModel_CalcViewModelView = 254;

    using tCalcViewModelLag = void (*)(void *thisptr, Vector &origin, QAngle &angles, QAngle &originalAngles);
    tCalcViewModelLag oCalcViewModelLag = nullptr;
    using tCalcViewModelView = void (*)(void *thisptr, void *owner, const Vector &eyePosition, const QAngle &eyeAngles);
    tCalcViewModelView oCalcViewModelView = nullptr;

    // C_WeaponPortalgun [332] AddViewmodelBob(viewModel, origin, angles): moves and turns the gun
    // with the walking speed, called by CalcViewModelView.
    constexpr int kPortalgun_AddViewmodelBob = 332;

    using tAddViewmodelBob = void (*)(void *thisptr, void *viewModel, Vector &origin, QAngle &angles);
    tAddViewmodelBob oAddViewmodelBob = nullptr;

    // OnRenderStart: after the view is set up, ParticleMgr()->Simulate(frameTime) moves the particles
    // to their control points, called this far after loading the name for the profiler. They are
    // drawn from there, so a later change of a control point shows one frame late.
    constexpr const char *kParticleSimulate_Name = "ParticleMgr()->Simulate";
    constexpr int kParticleSimulate_CallOffset = 0x21;

    using tParticleSimulate = void (*)(void *thisptr, float frameTime);
    tParticleSimulate oParticleSimulate = nullptr;

    // Reads the muzzle as C_WeaponPortalgun does: player [281] GetViewModel(index), then the
    // IClientRenderable part of the viewmodel, [32] LookupAttachment(name), [34] GetAttachment(number,
    // origin, angles).
    constexpr int kPlayer_GetViewModel = 281;
    constexpr int kEntity_RenderableOffset = 8;
    constexpr int kRenderable_LookupAttachment = 32;
    constexpr int kRenderable_GetAttachment = 34;

    // Effect "PortalBlast" (the shot), callback(const CEffectData &): m_vOrigin at 0 is where it
    // starts, at 0x58 who fired it. The game computes that start from the eye position.
    constexpr int kPortalBlast_PlacedByOffset = 0x58;
    constexpr int PORTAL_PLACED_BY_PLAYER = 2;

    using tPortalBlast = void (*)(void *effectData);
    tPortalBlast oPortalBlast = nullptr;

    // TraceFirePortal(start, direction, portal2, placedBy, result, test), in client.dll and
    // server.dll, finds where the portal lands; called only by FirePortal, at these distances from
    // where FirePortal loads "PortalBlast" with `lea rcx`. Both start with these bytes.
    constexpr int kTraceFirePortal_CallOffsetClient = -0x1E5;
    constexpr int kTraceFirePortal_CallOffsetServer = -0x1C5;
    constexpr uint8_t kTraceFirePortal_Start[] = { 0x48, 0x8B, 0xC4, 0x44, 0x88, 0x48, 0x20, 0x55 };

    using tTraceFirePortal = bool (*)(void *thisptr, const Vector &start, const Vector &direction, bool portal2, int placedBy, void *result, bool test);
    tTraceFirePortal oTraceFirePortalClient = nullptr;
    tTraceFirePortal oTraceFirePortalServer = nullptr;

    // Attack of C_WeaponPortalgun: DispatchParticleEffect("portalgun_muzzleflash", ...) on the
    // weapon, called this far after loading the name, next to "portalgun_muzzleflash_FP" on the
    // viewmodel.
    constexpr int kMuzzleFlash_CallOffset = 0x14;
    constexpr const char *kMuzzleFlash_WorldEffect = "portalgun_muzzleflash";

    using tDispatchParticleEffect = void *(*)(const char *name, int attachType, void *entity, const char *attachment, bool reset, int slot, void *filter);
    tDispatchParticleEffect oDispatchParticleEffect = nullptr;

    // C_BaseViewModel [243] FormatViewModelAttachment(attachment, matrix) calls the global
    // FormatViewModelAttachment(player, origin, inverse) here; the global one has no vtable entry.
    constexpr int kViewModel_FormatAttachment = 243;
    constexpr int kFormatAttachment_CallOffset = 0x5c;

    using tFormatViewModelAttachment = void (*)(void *player, Vector &origin, bool inverse);
    tFormatViewModelAttachment oFormatViewModelAttachment = nullptr;

    // The game's ping laser, used as the aim laser: CreatePingPointer(player, destination) starts the
    // effect robot_point_beam at the viewmodel's muzzle; control point 1 is its end, 2 its color. No
    // vtable entry: found through its warning, see the offsets into the function.
    constexpr const char *kPingPointer_Warning = "Trying to create a ping laser, but we have no view model!";
    constexpr int kPingPointer_WarningOffset = 0x39;
    constexpr uint8_t kPingPointer_Start[] = { 0x40, 0x56, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x50 };
    constexpr int kPingPointer_DestroyCallOffset = 0x10;
    constexpr int kPingPointer_EffectLeaOffset = 0x15; // lea rsi, [r14 + effect]
    constexpr uint8_t kPingPointer_EffectLea[] = { 0x49, 0x8D, 0xB6 };
    constexpr int kPingPointer_SetDrawOnlyCallOffset = 0x279;
    constexpr int kPingPointer_SetControlPointCallOffset = 0x289;

    // The server precaches the effect only in coop: PrecacheParticleSystem(name), called right after
    // loading the name, adds it to a string table. It can do so at any time, and more than once. The
    // client gets it with the next update, before that the effect wouldn't be drawn.
    constexpr const char *kLaserEffect = "robot_point_beam";
    constexpr int kPrecacheParticle_CallOffset = 7;
    constexpr int kLaser_PrecacheWaitFrames = 30;
    constexpr int kSplitScreen_AllUsers = -1;

    using tCreatePingPointer = void (*)(void *player, const Vector &destination);
    using tDestroyPingPointer = void (*)(void *player);
    using tSetControlPoint = void (*)(void *effect, int point, const Vector &value);
    using tSetDrawOnlyForSplitScreenUser = void (*)(void *effect, int slot);
    using tPrecacheParticleSystem = int (*)(const char *name);
    tCreatePingPointer CreatePingPointer = nullptr;
    tDestroyPingPointer DestroyPingPointer = nullptr;
    tSetControlPoint SetControlPoint = nullptr;
    tSetDrawOnlyForSplitScreenUser SetDrawOnlyForSplitScreenUser = nullptr;
    tPrecacheParticleSystem PrecacheParticleSystem = nullptr;
    int g_PingEffectOffset = 0; // of the player

    // The trace filter of the shot, built as at the start of TraceFirePortal: the constructor, then
    // the list of classes the shot passes (physics objects, the player, ...). [3] SetPassEntity.
    constexpr int kTraceFirePortal_FilterConstructorCallOffset = 0x46;
    constexpr int kTraceFirePortal_FilterListCallOffset = 0x4F;
    constexpr int kTraceFilter_SetPassEntity = 3;
    constexpr size_t kTraceFilter_Size = 0x40;

    using tTraceFilterConstructor = void (*)(void *filter, void *passEntity, int collisionGroup);
    using tTraceFilterList = void (*)(void *filter);
    tTraceFilterConstructor TraceFilterConstructor = nullptr;
    tTraceFilterList TraceFilterList = nullptr;

    // Ray_t of Strata Source (TraceFirePortal, engine's TraceRay): the SDK's plus the fields at the end.
    struct alignas(16) AlignedVector
    {
        Vector value;
        float pad;
    };
    struct StrataRay
    {
        AlignedVector start, delta, startOffset, extents;
        const void *worldAxisTransform;
        bool isRay, isSwept, unknown4A;
        const void *unknown50;
        float unknown58[3];
    };
    static_assert(offsetof(StrataRay, isRay) == 0x48 && offsetof(StrataRay, unknown58) == 0x58);

    constexpr int kEngineTrace_TraceRay = 5;
    constexpr unsigned int kShotMask = 0x2004003; // of TraceFirePortal
    constexpr int kTrace_EndPosOffset = 0x0C;
    constexpr size_t kTrace_Size = 0x100; // the game's is 0xA0

    // Network fields: player m_hActiveWeapon, C_WeaponPortalgun m_bCanFirePortal1/2, m_iLastFiredPortal,
    // m_bOpenProngs (set with the animation ACT_VM_PICKUP, which leads to the carrying one).
    constexpr int kPlayer_ActiveWeaponHandle = 0x3868;
    constexpr int kPortalgun_CanFirePortal1 = 0x3280;
    constexpr int kPortalgun_CanFirePortal2 = 0x3281;
    constexpr int kPortalgun_LastFiredPortal = 0x3284;
    constexpr int kPortalgun_OpenProngs = 0x3288;
    constexpr int kAimMode_Laser = 2;

    void **g_PortalgunVTable = nullptr;

    Game *g_StrataGame = nullptr;
    void *g_ViewModelOwner = nullptr; // the player, as last seen by hkCalcViewModelView

    // Where the viewmodel's muzzle is in this frame, for the laser and the shot; the server's code
    // runs outside of rendering, where client bones may not be set up.
    Vector g_Muzzle;
    bool g_MuzzleValid = false;

    // The eyes are rendered and the viewmodel is in the hand.
    bool VRViewActive()
    {
        VR *vr = g_StrataGame->m_VR;
        return vr->m_IsInitialized && vr->m_IsVREnabled && !vr->IsMenuVisible();
    }

    struct Section
    {
        uint8_t *begin;
        uint8_t *end;
    };

    // The module's sections that hold code or data (RTTI lives in .rdata).
    std::vector<Section> Sections(uint8_t *base, bool code)
    {
        std::vector<Section> sections;
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            if (((section->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0) != code)
                continue;
            uint8_t *begin = base + section->VirtualAddress;
            sections.push_back({ begin, begin + section->Misc.VirtualSize });
        }
        return sections;
    }

    // The instruction `lea <register>, [rip + text]`; modrm selects the register (0x0D rcx, 0x15 rdx).
    uint8_t *FindStringLea(uint8_t *base, const char *text, uint8_t modrm)
    {
        // With the 0 before and after it, so it isn't the end of a longer string.
        const std::string needle = std::string(1, '\0') + text + std::string(1, '\0');
        uint8_t *string = nullptr;
        for (const Section &section : Sections(base, false))
        {
            uint8_t *found = std::search(section.begin, section.end, needle.begin(), needle.end());
            if (found != section.end)
            {
                string = found + 1;
                break;
            }
        }
        if (!string)
            return nullptr;

        for (const Section &section : Sections(base, true))
        {
            for (uint8_t *p = section.begin; p + 7 <= section.end; ++p)
            {
                if (p[0] == 0x48 && p[1] == 0x8D && p[2] == modrm && p + 7 + *reinterpret_cast<int32_t *>(p + 3) == string)
                    return p;
            }
        }
        return nullptr;
    }

    // The target of the `call rel32` at this address, nullptr if there is none.
    uint8_t *CallTarget(uint8_t *call)
    {
        return *call == 0xE8 ? call + 5 + *reinterpret_cast<int32_t *>(call + 1) : nullptr;
    }

    template <typename F>
    bool InstallHook(const char *name, void *target, F detour, F &original, const char *module = "client.dll")
    {
        const MH_STATUS created = MH_CreateHook(target, reinterpret_cast<void *>(detour), reinterpret_cast<void **>(&original));
        const MH_STATUS enabled = created == MH_OK ? MH_EnableHook(target) : created;
        std::cout << "[Hooks] " << name << " " << module << "+0x" << std::hex << ((uint8_t *)target - (uint8_t *)GetModuleHandleA(module))
                  << std::dec << ": create " << created << ", enable " << enabled << "\n";
        return enabled == MH_OK;
    }

    uint8_t *FindTraceFirePortal(const char *module, int callOffset)
    {
        uint8_t *blast = FindStringLea(reinterpret_cast<uint8_t *>(GetModuleHandleA(module)), "PortalBlast", 0x0D);
        uint8_t *target = blast ? CallTarget(blast + callOffset) : nullptr;
        return target && memcmp(target, kTraceFirePortal_Start, sizeof(kTraceFirePortal_Start)) == 0 ? target : nullptr;
    }

    // Movement (forward, side) given relative to fromYaw, expressed relative to toYaw. Source:
    // forward = (cos yaw, sin yaw), right = (sin yaw, -cos yaw).
    void TurnMove(float forward, float side, float fromYaw, float toYaw, float &outForward, float &outSide)
    {
        const float d = (fromYaw - toYaw) * 3.14159265f / 180.0f;
        outForward += forward * cosf(d) + side * sinf(d);
        outSide += -forward * sinf(d) + side * cosf(d);
    }

    // The player aims with the right controller (view angles of the command: shooting, use). The
    // left stick walks where the left controller points (Stefan, 2026-11-03), the keyboard
    // relative to the head. The engine's view angles stay with the head (set in hkRenderView).
    bool hkCreateMove(void *thisptr, float flInputSampleTime, CUserCmd *cmd)
    {
        const bool result = oCreateMove(thisptr, flInputSampleTime, cmd);

        VR *vr = g_StrataGame->m_VR;
        if (!cmd || !cmd->command_number || !vr->m_IsInitialized || !vr->m_IsVREnabled || vr->IsMenuVisible())
            return result;

        QAngle aim = vr->m_RightControllerAngAbs;
        aim.x = std::clamp(aim.x, -89.0f, 89.0f);
        aim.z = 0;

        float forward = 0, side = 0;
        // Keyboard movement is relative to the engine's view angles, i.e. the head.
        TurnMove(cmd->forwardmove, cmd->sidemove, vr->m_HmdAngAbs.y, aim.y, forward, side);
        vr::InputAnalogActionData_t walk;
        if (vr->GetAnalogActionData(vr->m_ActionWalk, walk) && walk.bActive)
            TurnMove(walk.y * MAX_LINEAR_SPEED, walk.x * MAX_LINEAR_SPEED, vr->m_LeftControllerAngAbs.y, aim.y, forward, side);
        cmd->forwardmove = forward;
        cmd->sidemove = side;
        cmd->upmove = 0;

        cmd->buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
        if (cmd->forwardmove > 0)
            cmd->buttons |= IN_FORWARD;
        else if (cmd->forwardmove < 0)
            cmd->buttons |= IN_BACK;
        if (cmd->sidemove > 0)
            cmd->buttons |= IN_MOVERIGHT;
        else if (cmd->sidemove < 0)
            cmd->buttons |= IN_MOVELEFT;

        cmd->viewangles = aim;

        static bool logged = false;
        if (!logged)
        {
            std::cout << "[VR] first VR command #" << cmd->command_number << ": aim " << aim.x << "," << aim.y << " head yaw "
                      << vr->m_HmdAngAbs.y << ", move " << cmd->forwardmove << "," << cmd->sidemove << "\n";
            logged = true;
        }

        // false: the engine keeps its view angles (the head) instead of taking the aim.
        return false;
    }

    // The viewmodel (portal gun) sits in the right hand instead of in front of the head.
    void hkCalcViewModelView(void *thisptr, void *owner, const Vector &eyePosition, const QAngle &eyeAngles)
    {
        g_ViewModelOwner = owner;
        if (!VRViewActive())
            return oCalcViewModelView(thisptr, owner, eyePosition, eyeAngles);

        VR *vr = g_StrataGame->m_VR;
        const Vector position = vr->GetRecommendedViewmodelAbsPos(eyePosition);
        const QAngle angles = vr->GetRecommendedViewmodelAbsAngle();
        oCalcViewModelView(thisptr, owner, position, angles);
    }

    // The engine moves viewmodel attachments (muzzle, gun lights) to where they appear on screen,
    // as the viewmodel is drawn with a different FOV than the world. It uses the flat view for
    // that, while the eyes draw both with the same FOV: there the attachments are right as they are.
    void hkFormatViewModelAttachment(void *player, Vector &origin, bool inverse)
    {
        if (!VRViewActive())
            oFormatViewModelAttachment(player, origin, inverse);
    }

    // The game's lag lets the gun drift after the hand for about a second.
    void hkCalcViewModelLag(void *thisptr, Vector &origin, QAngle &angles, QAngle &originalAngles)
    {
        if (!VRViewActive())
            oCalcViewModelLag(thisptr, origin, angles, originalAngles);
    }

    // The gun stays in line with the controller and with the laser.
    void hkAddViewmodelBob(void *thisptr, void *viewModel, Vector &origin, QAngle &angles)
    {
        if (!VRViewActive())
            oAddViewmodelBob(thisptr, viewModel, origin, angles);
    }

    // Where the viewmodel's muzzle is now. Only with the player hkCalcViewModelView has seen, as
    // GetClientEntity's result isn't verified to be the entity pointer on Strata Source.
    bool GetMuzzle(Vector &origin)
    {
        Game *game = g_StrataGame;
        void *player = game->GetClientEntity(game->m_EngineClient->GetLocalPlayer());
        if (!player || player != g_ViewModelOwner)
            return false;
        auto viewModel = CallVirtual<uint8_t *>(player, kPlayer_GetViewModel, 0);
        if (!viewModel)
            return false;
        const void *renderable = viewModel + kEntity_RenderableOffset;
        const int attachment = CallVirtual<int>(renderable, kRenderable_LookupAttachment, "muzzle");
        QAngle angles;
        return attachment > 0 && CallVirtual<bool>(renderable, kRenderable_GetAttachment, attachment, &origin, &angles);
    }

    // The shot starts at the gun's muzzle instead of next to the head.
    void hkPortalBlast(void *effectData)
    {
        auto placedBy = *reinterpret_cast<int *>(static_cast<uint8_t *>(effectData) + kPortalBlast_PlacedByOffset);
        if (VRViewActive() && placedBy == PORTAL_PLACED_BY_PLAYER)
        {
            if (g_MuzzleValid)
                *static_cast<Vector *>(effectData) = g_Muzzle;

            static bool logged = false;
            if (!logged)
            {
                std::cout << "[VR] first portal shot: muzzle " << (g_MuzzleValid ? "found" : "not found, the shot starts at the game's position") << "\n";
                logged = true;
            }
        }
        oPortalBlast(effectData);
    }

    // The player's shot goes from the muzzle along the controller instead of from the head, so it
    // stays in line with the gun. Client (prediction) and server have to agree on this.
    bool TraceFromGun(tTraceFirePortal original, void *thisptr, const Vector &start, const Vector &direction, bool portal2, int placedBy, void *result, bool test)
    {
        if (!VRViewActive() || placedBy != PORTAL_PLACED_BY_PLAYER)
            return original(thisptr, start, direction, portal2, placedBy, result, test);

        VR *vr = g_StrataGame->m_VR;
        const Vector gunStart = g_MuzzleValid ? g_Muzzle : vr->GetRightControllerAbsPos();
        return original(thisptr, gunStart, vr->m_RightControllerForward, portal2, placedBy, result, test);
    }

    bool hkTraceFirePortalClient(void *thisptr, const Vector &start, const Vector &direction, bool portal2, int placedBy, void *result, bool test)
    {
        return TraceFromGun(oTraceFirePortalClient, thisptr, start, direction, portal2, placedBy, result, test);
    }

    bool hkTraceFirePortalServer(void *thisptr, const Vector &start, const Vector &direction, bool portal2, int placedBy, void *result, bool test)
    {
        return TraceFromGun(oTraceFirePortalServer, thisptr, start, direction, portal2, placedBy, result, test);
    }

    // The muzzle flash on the weapon presumably sits at the hidden third-person model, near the
    // head; the first-person one on the viewmodel stays.
    void *hkDispatchParticleEffect(const char *name, int attachType, void *entity, const char *attachment, bool reset, int slot, void *filter)
    {
        if (VRViewActive() && strcmp(name, kMuzzleFlash_WorldEffect) == 0)
            return nullptr;
        return oDispatchParticleEffect(name, attachType, entity, attachment, reset, slot, filter);
    }

    // The portal gun the player holds, nullptr if it's something else or the gun can't fire.
    uint8_t *ActivePortalgun(uint8_t *player)
    {
        const uint32_t handle = *reinterpret_cast<uint32_t *>(player + kPlayer_ActiveWeaponHandle);
        if (handle == 0xFFFFFFFF)
            return nullptr;
        auto weapon = reinterpret_cast<uint8_t *>(g_StrataGame->GetClientEntity(handle & 0xFFFF));
        if (!weapon || *reinterpret_cast<void ***>(weapon) != g_PortalgunVTable)
            return nullptr;
        return weapon[kPortalgun_CanFirePortal1] || weapon[kPortalgun_CanFirePortal2] ? weapon : nullptr;
    }

    // Where a shot from the gun ends, with the same filter and mask as the shot.
    Vector TraceShot(void *gun, const Vector &start, const Vector &direction)
    {
        static uint8_t *filter = []
        {
            alignas(16) static uint8_t buffer[kTraceFilter_Size];
            TraceFilterConstructor(buffer, nullptr, 0);
            TraceFilterList(buffer);
            return buffer;
        }();
        CallVirtual<void>(filter, kTraceFilter_SetPassEntity, gun);

        // Vector's constructor doesn't initialize it, so "= {}" wouldn't clear the ray.
        StrataRay ray;
        memset(&ray, 0, sizeof(ray));
        ray.start.value = start;
        ray.delta.value = direction * MAX_TRACE_LENGTH;
        ray.isRay = true;
        ray.isSwept = true;

        alignas(16) uint8_t trace[kTrace_Size];
        CallVirtual<void>(g_StrataGame->m_EngineTrace, kEngineTrace_TraceRay, &ray, kShotMask, filter, trace);
        return *reinterpret_cast<Vector *>(trace + kTrace_EndPosOffset);
    }

    void RemoveLaser()
    {
        if (!CreatePingPointer)
            return;
        Game *game = g_StrataGame;
        auto player = reinterpret_cast<uint8_t *>(game->GetClientEntity(game->m_EngineClient->GetLocalPlayer()));
        if (player && player == g_ViewModelOwner && *reinterpret_cast<void **>(player + g_PingEffectOffset))
            DestroyPingPointer(player);
    }

    // The aim laser runs from the muzzle to where the shot would land, in the color of the last portal.
    void UpdateLaser()
    {
        VR *vr = g_StrataGame->m_VR;
        if (!CreatePingPointer)
            return;

        auto player = static_cast<uint8_t *>(g_ViewModelOwner);
        uint8_t *gun = g_MuzzleValid && vr->m_AimMode == kAimMode_Laser ? ActivePortalgun(player) : nullptr;
        if (!gun)
            return RemoveLaser();

        const Vector aim = TraceShot(gun, g_Muzzle, vr->m_RightControllerForward);
        void *effect = *reinterpret_cast<void **>(player + g_PingEffectOffset);
        if (!effect)
        {
            static int waited = 0;
            if (waited == 0)
                PrecacheParticleSystem(kLaserEffect);
            if (++waited < kLaser_PrecacheWaitFrames)
                return;
            waited = 0;

            CreatePingPointer(player, aim);
            effect = *reinterpret_cast<void **>(player + g_PingEffectOffset);
            std::cout << "[VR] laser: " << (effect ? "created" : "not created") << ", muzzle " << g_Muzzle.x << "," << g_Muzzle.y << "," << g_Muzzle.z
                      << " aim " << aim.x << "," << aim.y << "," << aim.z << "\n";
            if (!effect)
                return;
            SetDrawOnlyForSplitScreenUser(effect, kSplitScreen_AllUsers);
        }
        SetControlPoint(effect, 1, aim);
        const int lastPortal = std::clamp(*reinterpret_cast<int *>(gun + kPortalgun_LastFiredPortal), 0, 2);
        SetControlPoint(effect, 2, g_StrataGame->m_singlePlayerPortalColors[lastPortal] * 0.5f);
    }

    // CalcViewModelView has already put the viewmodel in the hand for this frame.
    void hkParticleSimulate(void *thisptr, float frameTime)
    {
        if (VRViewActive())
        {
            g_MuzzleValid = GetMuzzle(g_Muzzle);
            UpdateLaser();

            VR *vr = g_StrataGame->m_VR;
            const uint8_t *gun = g_MuzzleValid ? ActivePortalgun(static_cast<uint8_t *>(g_ViewModelOwner)) : nullptr;
            const bool carrying = gun && gun[kPortalgun_OpenProngs];
            if (carrying != vr->m_ViewmodelCarrying)
                std::cout << "[VR] gun carrying: " << carrying << "\n";
            vr->m_ViewmodelCarrying = carrying;
        }
        oParticleSimulate(thisptr, frameTime);
    }

    // Renders both eyes into the VR textures instead of the one view into the back buffer. Menus
    // (main menu with its background map, pause menu) keep rendering to the back buffer, which
    // the menu overlay shows.
    void hkRenderView(void *thisptr, const CViewSetup &view, const CViewSetup &hudView, int nClearFlags, int whatToDraw)
    {
        VR *vr = g_StrataGame->m_VR;
        if (!vr->m_IsInitialized || !vr->m_IsVREnabled || vr->IsMenuVisible())
        {
            RemoveLaser();
            return oRenderView(thisptr, view, hudView, nClearFlags, whatToDraw);
        }

        if (!vr->m_CreatedVRTextures)
            vr->CreateVRTextures();

        if (!vr->m_LeftEyeTexture || !vr->m_RightEyeTexture || !vr->m_VKLeftEye.m_Texture || !vr->m_VKRightEye.m_Texture)
        {
            static bool logged = false;
            if (!logged)
            {
                std::cout << "[VR] stereo: no eye textures (material system " << vr->m_LeftEyeTexture << "/" << vr->m_RightEyeTexture
                          << ", D3D11 " << vr->m_VKLeftEye.m_Texture << "/" << vr->m_VKRightEye.m_Texture << "), rendering flat\n";
                logged = true;
            }
            return oRenderView(thisptr, view, hudView, nClearFlags, whatToDraw);
        }

        const Vector position = view.origin;
        vr->m_SetupOrigin = position;

        // The player looks where the HMD looks (movement direction, aiming).
        const Vector hmdAngle = vr->GetViewAngle();
        QAngle inGameAngle(hmdAngle.x, hmdAngle.y, hmdAngle.z);
        g_StrataGame->m_EngineClient->SetViewAngles(inGameAngle);

        CViewSetup eyeView = view;
        eyeView.x = eyeView.m_nUnscaledX = 0;
        eyeView.y = eyeView.m_nUnscaledY = 0;
        eyeView.width = eyeView.m_nUnscaledWidth = vr->m_RenderWidth;
        eyeView.height = eyeView.m_nUnscaledHeight = vr->m_RenderHeight;
        eyeView.fov = vr->m_Fov;
        eyeView.fovViewmodel = vr->m_Fov;
        eyeView.m_flAspectRatio = vr->m_Aspect;
        eyeView.zNear = 6;
        eyeView.zNearViewmodel = 2;
        eyeView.angles = hmdAngle;

        // The HUD is laid out for the window; drawn into the eyes it would sit in a corner.
        const int eyeWhatToDraw = whatToDraw & ~RENDERVIEW_DRAWHUD;

        // The engine renders each eye as it renders the flat view; only the result in the back
        // buffer is complete (parts like the portal views go to an internal target, which is
        // drawn into the back buffer last). So it's copied from there, no redirected target.
        auto renderEye = [&](SharedTextureHolder &eye, const Vector &origin)
        {
            eyeView.origin = origin;
            oRenderView(thisptr, eyeView, hudView, nClearFlags, eyeWhatToDraw);
            vr->CopyBackBufferToEye(eye);
        };
        D3D11Hooks::TraceNote("left eye");
        renderEye(vr->m_VKLeftEye, vr->GetViewOriginLeft(position));
        D3D11Hooks::TraceNote("right eye");
        renderEye(vr->m_VKRightEye, vr->GetViewOriginRight(position));
        D3D11Hooks::TraceNote("eyes done");

        static bool logged = false;
        if (!logged)
        {
            std::cout << "[VR] stereo: first frame, game view " << view.width << "x" << view.height << " fov " << view.fov
                      << " aspect " << view.m_flAspectRatio << " -> eyes " << eyeView.width << "x" << eyeView.height << " fov "
                      << eyeView.fov << " aspect " << eyeView.m_flAspectRatio << ", whatToDraw " << whatToDraw << "\n";
            logged = true;
        }

        if (vr->m_RenderWindow)
            oRenderView(thisptr, view, hudView, nClearFlags, whatToDraw);

        vr->m_RenderedNewFrame = true;
    }
}

void **StrataHooks::FindVTable(const char *module, const char *className)
{
    auto base = reinterpret_cast<uint8_t *>(GetModuleHandleA(module));
    if (!base)
        return nullptr;
    const std::vector<Section> sections = Sections(base, false);

    // RTTI type descriptor: vftable pointer, spare pointer, then the decorated name.
    const std::string name = std::string(".?AV") + className + "@@";
    const char *nameBegin = name.c_str(), *nameEnd = nameBegin + name.size() + 1; // with the terminating 0
    uint8_t *typeDescriptor = nullptr;
    for (const Section &section : sections)
    {
        uint8_t *found = std::search(section.begin, section.end, nameBegin, nameEnd);
        if (found != section.end)
        {
            typeDescriptor = found - 0x10;
            break;
        }
    }
    if (!typeDescriptor)
        return nullptr;
    const uint32_t typeDescriptorRva = (uint32_t)(typeDescriptor - base);

    // Complete object locator of the primary vtable: signature 1, offset 0, type descriptor, and
    // (x64) its own RVA.
    uint8_t *locator = nullptr;
    for (const Section &section : sections)
    {
        for (uint8_t *p = section.begin; p + 24 <= section.end && !locator; p += 4)
        {
            auto fields = reinterpret_cast<const uint32_t *>(p);
            if (fields[0] == 1 && fields[1] == 0 && fields[3] == typeDescriptorRva && fields[5] == (uint32_t)(p - base))
                locator = p;
        }
        if (locator)
            break;
    }
    if (!locator)
        return nullptr;

    // The vtable follows the pointer to the locator.
    for (const Section &section : sections)
    {
        for (uint8_t *p = section.begin; p + 16 <= section.end; p += 8)
        {
            if (*reinterpret_cast<uint8_t **>(p) == locator)
                return reinterpret_cast<void **>(p + 8);
        }
    }
    return nullptr;
}

bool StrataHooks::Init(Game *game)
{
    g_StrataGame = game;

    // The D3D11 hooks have initialized MinHook already.
    MH_STATUS status = MH_Initialize();
    if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
    {
        std::cout << "[Hooks] MH_Initialize failed: " << status << "\n";
        return false;
    }

    void **viewRender = FindVTable("client.dll", "CViewRender");
    if (!viewRender)
    {
        std::cout << "[Hooks] CViewRender vtable not found, no stereo rendering\n";
        return false;
    }

    bool ok = InstallHook("RenderView", viewRender[kViewRender_RenderView], &hkRenderView, oRenderView);

    // ClientModePortalNormal inherits CreateMove from ClientModeShared.
    void **clientMode = FindVTable("client.dll", "ClientModeShared");
    if (!clientMode)
    {
        std::cout << "[Hooks] ClientModeShared vtable not found, no VR movement\n";
        return false;
    }
    ok &= InstallHook("CreateMove", clientMode[kClientMode_CreateMove], &hkCreateMove, oCreateMove);

    void **viewModel = FindVTable("client.dll", "C_BaseViewModel");
    if (!viewModel)
    {
        std::cout << "[Hooks] C_BaseViewModel vtable not found, viewmodel not in the hand\n";
        return false;
    }
    ok &= InstallHook("CalcViewModelView", viewModel[kViewModel_CalcViewModelView], &hkCalcViewModelView, oCalcViewModelView);
    ok &= InstallHook("CalcViewModelLag", viewModel[kViewModel_CalcViewModelLag], &hkCalcViewModelLag, oCalcViewModelLag);

    uint8_t *formatAttachment = CallTarget(static_cast<uint8_t *>(viewModel[kViewModel_FormatAttachment]) + kFormatAttachment_CallOffset);
    if (!formatAttachment)
    {
        std::cout << "[Hooks] FormatViewModelAttachment not found, viewmodel effects will be misplaced\n";
        return false;
    }
    ok &= InstallHook("FormatViewModelAttachment", formatAttachment, &hkFormatViewModelAttachment, oFormatViewModelAttachment);

    g_PortalgunVTable = FindVTable("client.dll", "C_WeaponPortalgun");
    if (!g_PortalgunVTable)
    {
        std::cout << "[Hooks] C_WeaponPortalgun vtable not found, the gun bobs and there is no aim laser\n";
        return false;
    }
    ok &= InstallHook("AddViewmodelBob", g_PortalgunVTable[kPortalgun_AddViewmodelBob], &hkAddViewmodelBob, oAddViewmodelBob);

    auto client = reinterpret_cast<uint8_t *>(GetModuleHandleA("client.dll"));

    uint8_t *simulateName = FindStringLea(client, kParticleSimulate_Name, 0x15);
    uint8_t *particleSimulate = simulateName ? CallTarget(simulateName + kParticleSimulate_CallOffset) : nullptr;
    if (!particleSimulate)
    {
        std::cout << "[Hooks] ParticleMgr()->Simulate not found, no muzzle position and no aim laser\n";
        return false;
    }
    ok &= InstallHook("ParticleSimulate", particleSimulate, &hkParticleSimulate, oParticleSimulate);

    // Registration of the effect: lea r8, callback; lea rdx, "PortalBlast"
    uint8_t *blastName = FindStringLea(client, "PortalBlast", 0x15);
    if (!blastName || blastName[-7] != 0x4C || blastName[-6] != 0x8D || blastName[-5] != 0x05)
    {
        std::cout << "[Hooks] PortalBlast not found, the shot won't start at the muzzle\n";
        return false;
    }
    ok &= InstallHook("PortalBlast", blastName + *reinterpret_cast<int32_t *>(blastName - 4), &hkPortalBlast, oPortalBlast);

    uint8_t *flashName = FindStringLea(client, kMuzzleFlash_WorldEffect, 0x0D);
    uint8_t *dispatchParticleEffect = flashName ? CallTarget(flashName + kMuzzleFlash_CallOffset) : nullptr;
    if (!dispatchParticleEffect)
    {
        std::cout << "[Hooks] DispatchParticleEffect not found, the muzzle flash stays at the third-person weapon\n";
        return false;
    }
    ok &= InstallHook("DispatchParticleEffect", dispatchParticleEffect, &hkDispatchParticleEffect, oDispatchParticleEffect);

    uint8_t *traceClient = FindTraceFirePortal("client.dll", kTraceFirePortal_CallOffsetClient);
    uint8_t *traceServer = FindTraceFirePortal("server.dll", kTraceFirePortal_CallOffsetServer);
    if (!traceClient || !traceServer)
    {
        std::cout << "[Hooks] TraceFirePortal not found (client " << (void *)traceClient << ", server " << (void *)traceServer
                  << "), portals are aimed from the head\n";
        return false;
    }
    ok &= InstallHook("TraceFirePortal", traceClient, &hkTraceFirePortalClient, oTraceFirePortalClient);
    ok &= InstallHook("TraceFirePortal", traceServer, &hkTraceFirePortalServer, oTraceFirePortalServer, "server.dll");

    // The laser is optional: without these functions there just isn't one.
    uint8_t *warning = FindStringLea(client, kPingPointer_Warning, 0x0D);
    uint8_t *ping = warning ? warning - kPingPointer_WarningOffset : nullptr;
    if (ping && memcmp(ping, kPingPointer_Start, sizeof(kPingPointer_Start)) == 0
        && memcmp(ping + kPingPointer_EffectLeaOffset, kPingPointer_EffectLea, sizeof(kPingPointer_EffectLea)) == 0)
    {
        g_PingEffectOffset = *reinterpret_cast<int32_t *>(ping + kPingPointer_EffectLeaOffset + sizeof(kPingPointer_EffectLea));
        DestroyPingPointer = reinterpret_cast<tDestroyPingPointer>(CallTarget(ping + kPingPointer_DestroyCallOffset));
        SetControlPoint = reinterpret_cast<tSetControlPoint>(CallTarget(ping + kPingPointer_SetControlPointCallOffset));
        SetDrawOnlyForSplitScreenUser = reinterpret_cast<tSetDrawOnlyForSplitScreenUser>(CallTarget(ping + kPingPointer_SetDrawOnlyCallOffset));
        uint8_t *effectName = FindStringLea(reinterpret_cast<uint8_t *>(GetModuleHandleA("server.dll")), kLaserEffect, 0x0D);
        PrecacheParticleSystem = effectName ? reinterpret_cast<tPrecacheParticleSystem>(CallTarget(effectName + kPrecacheParticle_CallOffset)) : nullptr;
        TraceFilterConstructor = reinterpret_cast<tTraceFilterConstructor>(CallTarget(traceClient + kTraceFirePortal_FilterConstructorCallOffset));
        TraceFilterList = reinterpret_cast<tTraceFilterList>(CallTarget(traceClient + kTraceFirePortal_FilterListCallOffset));
        if (DestroyPingPointer && SetControlPoint && SetDrawOnlyForSplitScreenUser && PrecacheParticleSystem && TraceFilterConstructor && TraceFilterList && game->m_EngineTrace)
            CreatePingPointer = reinterpret_cast<tCreatePingPointer>(ping);
    }
    std::cout << "[Hooks] aim laser " << (CreatePingPointer ? "available" : "not found") << "\n";

    return ok;
}
