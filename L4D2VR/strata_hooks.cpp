#include "strata_hooks.h"
#include <Windows.h>
#include <algorithm>
#include <cstdint>
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

    Game *g_StrataGame = nullptr;

    struct Section
    {
        uint8_t *begin;
        uint8_t *end;
    };

    // The module's sections that hold data (RTTI lives in .rdata).
    std::vector<Section> DataSections(uint8_t *base)
    {
        std::vector<Section> sections;
        auto dos = reinterpret_cast<IMAGE_DOS_HEADER *>(base);
        auto nt = reinterpret_cast<IMAGE_NT_HEADERS64 *>(base + dos->e_lfanew);
        IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
        for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
        {
            if (section->Characteristics & IMAGE_SCN_MEM_EXECUTE)
                continue;
            uint8_t *begin = base + section->VirtualAddress;
            sections.push_back({ begin, begin + section->Misc.VirtualSize });
        }
        return sections;
    }

    // The player aims with the right controller (view angles of the command: shooting, use) and
    // walks relative to the head: keyboard and left stick, turned from the head's yaw into the
    // aim's. The engine's view angles stay with the head (set in hkRenderView).
    bool hkCreateMove(void *thisptr, float flInputSampleTime, CUserCmd *cmd)
    {
        const bool result = oCreateMove(thisptr, flInputSampleTime, cmd);

        VR *vr = g_StrataGame->m_VR;
        if (!cmd || !cmd->command_number || !vr->m_IsInitialized || !vr->m_IsVREnabled || vr->IsMenuVisible())
            return result;

        // Keyboard movement is relative to the engine's view angles, i.e. the head.
        float forward = cmd->forwardmove;
        float side = cmd->sidemove;
        vr::InputAnalogActionData_t walk;
        if (vr->GetAnalogActionData(vr->m_ActionWalk, walk) && walk.bActive)
        {
            forward += walk.y * MAX_LINEAR_SPEED;
            side += walk.x * MAX_LINEAR_SPEED;
        }

        QAngle aim = vr->m_RightControllerAngAbs;
        aim.x = std::clamp(aim.x, -89.0f, 89.0f);
        aim.z = 0;

        // Source: forward = (cos yaw, sin yaw), right = (sin yaw, -cos yaw).
        const float d = (vr->m_HmdAngAbs.y - aim.y) * 3.14159265f / 180.0f;
        cmd->forwardmove = forward * cosf(d) + side * sinf(d);
        cmd->sidemove = -forward * sinf(d) + side * cosf(d);
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

    // Renders both eyes into the VR textures instead of the one view into the back buffer. Menus
    // (main menu with its background map, pause menu) keep rendering to the back buffer, which
    // the menu overlay shows.
    void hkRenderView(void *thisptr, const CViewSetup &view, const CViewSetup &hudView, int nClearFlags, int whatToDraw)
    {
        VR *vr = g_StrataGame->m_VR;
        if (!vr->m_IsInitialized || !vr->m_IsVREnabled || vr->IsMenuVisible())
            return oRenderView(thisptr, view, hudView, nClearFlags, whatToDraw);

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

        IMaterialSystem *materialSystem = g_StrataGame->m_MaterialSystem;
        auto renderEye = [&](ITexture *target, const Vector &origin)
        {
            eyeView.origin = origin;
            IMatRenderContext *context = StrataMaterialSystem::GetRenderContext(materialSystem);
            StrataRenderContext::SetRenderTarget(context, target);
            StrataRenderContext::Release(context);
            oRenderView(thisptr, eyeView, hudView, nClearFlags, eyeWhatToDraw);
        };
        renderEye(vr->m_LeftEyeTexture, vr->GetViewOriginLeft(position));
        renderEye(vr->m_RightEyeTexture, vr->GetViewOriginRight(position));

        IMatRenderContext *context = StrataMaterialSystem::GetRenderContext(materialSystem);
        StrataRenderContext::SetRenderTarget(context, nullptr);
        StrataRenderContext::Release(context);

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
    const std::vector<Section> sections = DataSections(base);

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

    void *renderView = viewRender[kViewRender_RenderView];
    MH_STATUS created = MH_CreateHook(renderView, (void *)&hkRenderView, reinterpret_cast<void **>(&oRenderView));
    MH_STATUS enabled = created == MH_OK ? MH_EnableHook(renderView) : created;
    std::cout << "[Hooks] CViewRender vtable client.dll+0x" << std::hex << ((uint8_t *)viewRender - (uint8_t *)GetModuleHandleA("client.dll"))
              << ", RenderView +0x" << ((uint8_t *)renderView - (uint8_t *)GetModuleHandleA("client.dll")) << std::dec
              << ": create " << created << ", enable " << enabled << "\n";

    // ClientModePortalNormal inherits CreateMove from ClientModeShared.
    void **clientMode = FindVTable("client.dll", "ClientModeShared");
    if (!clientMode)
    {
        std::cout << "[Hooks] ClientModeShared vtable not found, no VR movement\n";
        return false;
    }
    void *createMove = clientMode[kClientMode_CreateMove];
    MH_STATUS createdMove = MH_CreateHook(createMove, (void *)&hkCreateMove, reinterpret_cast<void **>(&oCreateMove));
    MH_STATUS enabledMove = createdMove == MH_OK ? MH_EnableHook(createMove) : createdMove;
    std::cout << "[Hooks] CreateMove client.dll+0x" << std::hex << ((uint8_t *)createMove - (uint8_t *)GetModuleHandleA("client.dll"))
              << std::dec << ": create " << createdMove << ", enable " << enabledMove << "\n";

    return enabled == MH_OK && enabledMove == MH_OK;
}
