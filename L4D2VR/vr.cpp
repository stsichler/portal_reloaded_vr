#include "vr.h"
#include <Windows.h>
#include "sdk.h"
#include "game.h"
#include "hooks.h"
#include "trace.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <thread>
#include <type_traits>
#include <algorithm>
#include <vector>
#include "d3d11_hooks.h"

VR::VR(Game *game) 
{
    m_Game = game;
}

void VR::init()
{
    InitOpenVR(); // loads the mod's openvr_api.dll

    char errorString[MAX_STR_LEN];

    vr::HmdError error = vr::VRInitError_None;
    m_System = vr::VR_Init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None) 
    {
        snprintf(errorString, MAX_STR_LEN, "VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
        Game::errorMsg(errorString);
        return;
    }

    vr::EVRInitError peError = vr::VRInitError_None;

    if (!vr::VRCompositor())
    {
        Game::errorMsg("Compositor initialization failed.");
        return;
    }

    m_Input = vr::VRInput();
    m_System = vr::OpenVRInternal_ModuleContext().VRSystem();

    m_System->GetRecommendedRenderTargetSize(&m_RenderWidth, &m_RenderHeight);
    m_AntiAliasing = 0;

    float l_left = 0.0f, l_right = 0.0f, l_top = 0.0f, l_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);

    float r_left = 0.0f, r_right = 0.0f, r_top = 0.0f, r_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Right, &r_left, &r_right, &r_top, &r_bottom);

    float tanHalfFov[2];

    tanHalfFov[0] = std::max({ -l_left, l_right, -r_left, r_right });
    tanHalfFov[1] = std::max({ -l_top, l_bottom, -r_top, r_bottom });

    m_TextureBounds[0].uMin = 0.5f + 0.5f * l_left / tanHalfFov[0];
    m_TextureBounds[0].uMax = 0.5f + 0.5f * l_right / tanHalfFov[0];
    m_TextureBounds[0].vMin = 0.5f - 0.5f * l_bottom / tanHalfFov[1];
    m_TextureBounds[0].vMax = 0.5f - 0.5f * l_top / tanHalfFov[1];

    m_TextureBounds[1].uMin = 0.5f + 0.5f * r_left / tanHalfFov[0];
    m_TextureBounds[1].uMax = 0.5f + 0.5f * r_right / tanHalfFov[0];
    m_TextureBounds[1].vMin = 0.5f - 0.5f * r_bottom / tanHalfFov[1];
    m_TextureBounds[1].vMax = 0.5f - 0.5f * r_top / tanHalfFov[1];

    m_Aspect = tanHalfFov[0] / tanHalfFov[1];
    m_Fov = 2.0f * atan(tanHalfFov[0]) * 360 / (3.14159265358979323846 * 2);

    InstallApplicationManifest("manifest.vrmanifest");
    SetActionManifest("action_manifest.json");

    std::thread configParser(&VR::WaitForConfigUpdate, this);
    configParser.detach();

    // The menu overlay shows m_VKBackBuffer, a copy of the back buffer made in Present_Pre_Callback.
    m_Overlay = vr::VROverlay();
    m_Overlay->CreateOverlay("MenuOverlayKey", "MenuOverlay", &m_MainMenuHandle);
    //m_Overlay->CreateOverlay("HUDOverlayKey", "HUDOverlay", &m_HUDHandle);
    m_Overlay->SetOverlayInputMethod(m_MainMenuHandle, vr::VROverlayInputMethod_Mouse);
    //m_Overlay->SetOverlayInputMethod(m_HUDHandle, vr::VROverlayInputMethod_Mouse);
    m_Overlay->SetOverlayFlag(m_MainMenuHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);
    //m_Overlay->SetOverlayFlag(m_HUDHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

    const vr::HmdVector2_t mouseScaleMenu = { (float)m_RenderWidth, (float)m_RenderHeight };
    m_Overlay->SetOverlayCurvature(m_MainMenuHandle, 0.15f);
    m_Overlay->SetOverlayMouseScale(m_MainMenuHandle, &mouseScaleMenu);

    vr::VRCompositor()->SetTrackingSpace(m_SeatedMode
        ? vr::TrackingUniverseSeated
        : vr::TrackingUniverseStanding);

    UpdatePosesAndActions();

    // Not yet: m_IsVREnabled turns on the in-game VR (stereo rendering, tracking, VR input), which
    // needs the engine hooks (Hooks) for Strata Source first. Until then the game is shown on
    // the menu overlay, also in game.
    // m_IsVREnabled = true;

    m_IsInitialized = true;
    std::cout << "[VR] initialized, menu overlay created\n";
}

// Main menu or pause menu shown. Strata Source runs a background map behind the main menu, so
// IsInGame() is true there too, and the vgui surface reports no cursor; the engine's GameUI panel
// is what tells the menu apart.
bool VR::IsMenuVisible()
{
    if (m_Game->m_EngineVGui)
        return m_Game->m_EngineVGui->IsGameUIVisible();
    return !m_Game->m_EngineClient->IsInGame();
}

// Size of the game window's client area (what the engine renders the menu for).
void VR::GetWindowSize(int &width, int &height)
{
    width = 1280;
    height = 720;
    DXGI_SWAP_CHAIN_DESC desc;
    RECT rect;
    if (D3D11Hooks::g_SwapChain && SUCCEEDED(D3D11Hooks::g_SwapChain->GetDesc(&desc)) &&
        GetClientRect(desc.OutputWindow, &rect) && rect.right > 0 && rect.bottom > 0)
    {
        width = rect.right;
        height = rect.bottom;
    }
}

// Black texture submitted to both eyes while only the overlay is shown. Created directly with
// D3D11 (the material system of Strata Source isn't hooked yet).
void VR::CreateBlankTexture()
{
    if (m_VKBlankTexture.m_Texture || !D3D11Hooks::g_Device)
        return;

    const UINT size = 64;
    std::vector<uint32_t> pixels(size * size, 0xff000000); // opaque black
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = size;
    desc.Height = size;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = { 1, 0 };
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data = { pixels.data(), size * sizeof(uint32_t), 0 };

    if (FAILED(D3D11Hooks::g_Device->CreateTexture2D(&desc, &data, &m_VKBlankTexture.m_Texture)))
    {
        std::cout << "[D3D11] could not create the blank texture\n";
        return;
    }
    m_VKBlankTexture.m_Width = size;
    m_VKBlankTexture.m_Height = size;
    m_VKBlankTexture.m_VRTexture.handle = m_VKBlankTexture.m_Texture;
}

// The game ships its own, older openvr_api.dll in bin\win64, next to this mod. openvr_api.dll is
// delay-loaded, so the mod's copy from <game>\VR\ is loaded before the first OpenVR call.
static void LoadModOpenVR()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    if (GetModuleHandleA("openvr_api.dll"))
    {
        std::cout << "[OpenVR] openvr_api.dll was already loaded by someone else\n";
        return;
    }
    char currentDir[MAX_STR_LEN];
    GetCurrentDirectory(MAX_STR_LEN, currentDir);
    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\openvr_api.dll", currentDir);
    HMODULE module = LoadLibraryA(path);
    std::cout << "[OpenVR] " << path << (module ? " loaded" : " NOT found, using the game's openvr_api.dll") << "\n";
}

bool VR::InitOpenVR()
{
    static bool initialized = false;
    if (initialized)
        return true;

    LoadModOpenVR();

    vr::HmdError error = vr::VRInitError_None;
    vr::IVRSystem *system = vr::VR_Init(&error, vr::VRApplication_Scene);
    if (error != vr::VRInitError_None)
    {
        char errorString[MAX_STR_LEN];
        snprintf(errorString, MAX_STR_LEN, "VR_Init failed: %s", vr::VR_GetVRInitErrorAsEnglishDescription(error));
        Game::errorMsg(errorString);
        ExitProcess(0);
    }

    system->GetRecommendedRenderTargetSize(&m_RenderWidth, &m_RenderHeight);
    std::cout << "[OpenVR] initialized, render target " << m_RenderWidth << "x" << m_RenderHeight << "\n";
    initialized = true;
    return true;
}

// Copies the game's back buffer into m_VKBackBuffer, which the menu overlay shows. A copy,
// because back buffers can't be handed to OpenVR (and with the flip model change every frame).
void VR::CopyBackBuffer(IDXGISwapChain *swapChain)
{
    ID3D11Texture2D *backBuffer = nullptr;
    if (!D3D11Hooks::g_Device || FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer)))
        return;

    D3D11_TEXTURE2D_DESC desc;
    backBuffer->GetDesc(&desc);

    if (!m_VKBackBuffer.m_Texture || m_VKBackBuffer.m_Width != desc.Width || m_VKBackBuffer.m_Height != desc.Height)
    {
        if (m_VKBackBuffer.m_Texture)
            m_VKBackBuffer.m_Texture->Release();
        m_VKBackBuffer = SharedTextureHolder();

        D3D11_TEXTURE2D_DESC copyDesc = desc;
        copyDesc.MipLevels = 1;
        copyDesc.ArraySize = 1;
        copyDesc.SampleDesc = { 1, 0 };
        copyDesc.Usage = D3D11_USAGE_DEFAULT;
        copyDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.CPUAccessFlags = 0;
        copyDesc.MiscFlags = 0;
        if (FAILED(D3D11Hooks::g_Device->CreateTexture2D(&copyDesc, nullptr, &m_VKBackBuffer.m_Texture)))
        {
            std::cout << "[D3D11] could not create the back buffer copy\n";
            backBuffer->Release();
            return;
        }
        m_VKBackBuffer.m_Width = desc.Width;
        m_VKBackBuffer.m_Height = desc.Height;
        m_VKBackBuffer.m_VRTexture.handle = m_VKBackBuffer.m_Texture;
    }

    if (desc.SampleDesc.Count > 1)
        D3D11Hooks::g_Context->ResolveSubresource(m_VKBackBuffer.m_Texture, 0, backBuffer, 0, desc.Format);
    else
        D3D11Hooks::g_Context->CopyResource(m_VKBackBuffer.m_Texture, backBuffer);
    backBuffer->Release();
}

void VR::initD3D11Callbacks()
{
    // The game renders at the HMD resolution: the back buffer is shown on the menu overlay, the
    // eye textures are created with the same size.
    D3D11Hooks::SwapChainSize_Callback = [this](UINT &width, UINT &height)
        {
            if (!InitOpenVR())
                return;
            width = m_RenderWidth;
            height = m_RenderHeight;
        };

    D3D11Hooks::CreateTexture2D_Pre_Callback = [this](D3D11_TEXTURE2D_DESC &desc)
        {
            // TODO: MSAA (AntiAliasing) for the eye textures. With D3D11 this also needs the views
            // the game creates for them (TEXTURE2DMS) and a resolve before submitting.
            static bool warned = false;
            if ((m_CreatingTextureID == VR::Texture_LeftEye || m_CreatingTextureID == VR::Texture_RightEye) &&
                m_AntiAliasing > 1 && !warned)
            {
                std::cout << "[D3D11] AntiAliasing=" << m_AntiAliasing << " is not supported yet, ignored\n";
                warned = true;
            }
        };

    D3D11Hooks::CreateTexture2D_Post_Callback = [this](ID3D11Texture2D *texture, const D3D11_TEXTURE2D_DESC &desc)
        {
            // CreateNamedRenderTargetTextureEx may also create a depth texture; only the color
            // render target is submitted.
            if (m_CreatingTextureID == VR::Texture_None || !(desc.BindFlags & D3D11_BIND_RENDER_TARGET))
                return;

            SharedTextureHolder *target = nullptr;
            switch (m_CreatingTextureID)
            {
            case VR::Texture_LeftEye: target = &m_VKLeftEye; break;
            case VR::Texture_RightEye: target = &m_VKRightEye; break;
            case VR::Texture_HUD: target = &m_VKHUD; break;
            case VR::Texture_Blank: target = &m_VKBlankTexture; break;
            default: return;
            }

            target->m_Texture = texture;
            target->m_Width = desc.Width;
            target->m_Height = desc.Height;
            target->m_VRTexture.handle = texture;
            std::cout << "[D3D11] VR texture " << m_CreatingTextureID << ": " << desc.Width << "x" << desc.Height
                      << " format=" << desc.Format << "\n";
        };

    D3D11Hooks::SetViewports_Callback = [this](UINT count, D3D11_VIEWPORT *viewports)
        {
            // While a menu is shown, the whole back buffer goes to the overlay. Strata Source draws
            // the menu over the whole back buffer, but the scene behind it (background map) with
            // a viewport of the window size: stretch that one too. Only viewports of the window
            // size at 0,0 into the back buffer; render targets (shadow maps etc.) stay as they are
            // (the D3D9 version stretched every viewport, which messed up the shadows).
            if (!m_IsInitialized || !IsMenuVisible())
                return;

            D3D11_VIEWPORT &vp = viewports[0];
            if (vp.TopLeftX != 0 || vp.TopLeftY != 0 || (int)vp.Width != m_WindowWidth || (int)vp.Height != m_WindowHeight)
                return;

            ID3D11RenderTargetView *rtv = nullptr;
            D3D11Hooks::g_Context->OMGetRenderTargets(1, &rtv, nullptr);
            if (!rtv)
                return;
            ID3D11Resource *target = nullptr;
            rtv->GetResource(&target);
            rtv->Release();
            ID3D11Texture2D *backBuffer = nullptr;
            D3D11Hooks::g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer);
            const bool intoBackBuffer = target && target == backBuffer;
            if (target)
                target->Release();
            if (backBuffer)
                backBuffer->Release();
            if (!intoBackBuffer)
                return;

            static bool logged = false;
            if (!logged)
            {
                std::cout << "[VR] menu: stretching the scene viewport " << m_WindowWidth << "x" << m_WindowHeight
                          << " to the back buffer " << m_RenderWidth << "x" << m_RenderHeight << "\n";
                logged = true;
            }
            vp.Width = (float)m_RenderWidth;
            vp.Height = (float)m_RenderHeight;
        };

    D3D11Hooks::Present_Pre_Callback = [this](IDXGISwapChain *swapChain)
        {
            int windowWidth, windowHeight;
            GetWindowSize(windowWidth, windowHeight);
            m_WindowWidth = windowWidth; // for the viewport callback, which runs many times per frame
            m_WindowHeight = windowHeight;

            if (m_IsInitialized)
                CopyBackBuffer(swapChain);
        };

    D3D11Hooks::Present_Post_Callback = [this](IDXGISwapChain *)
        {
            Update();
        };
}

int VR::SetActionManifest(const char *fileName) 
{
    char currentDir[MAX_STR_LEN];
    GetCurrentDirectory(MAX_STR_LEN, currentDir);
    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\SteamVRActionManifest\\%s", currentDir, fileName);

    if (m_Input->SetActionManifestPath(path) != vr::VRInputError_None) 
    {
        Game::errorMsg("SetActionManifestPath failed");
    }

    m_Input->GetActionHandle("/actions/main/in/ActivateVR", &m_ActionActivateVR);
    m_Input->GetActionHandle("/actions/main/in/Jump", &m_ActionJump);
    m_Input->GetActionHandle("/actions/main/in/PrimaryAttack", &m_ActionPrimaryAttack);
    m_Input->GetActionHandle("/actions/main/in/Reload", &m_ActionReload);
    m_Input->GetActionHandle("/actions/main/in/Use", &m_ActionUse);
    m_Input->GetActionHandle("/actions/main/in/Walk", &m_ActionWalk);
    m_Input->GetActionHandle("/actions/main/in/Turn", &m_ActionTurn);
    m_Input->GetActionHandle("/actions/main/in/SecondaryAttack", &m_ActionSecondaryAttack);
    m_Input->GetActionHandle("/actions/main/in/NextItem", &m_ActionNextItem);
    m_Input->GetActionHandle("/actions/main/in/PrevItem", &m_ActionPrevItem);
    m_Input->GetActionHandle("/actions/main/in/ResetPosition", &m_ActionResetPosition);
    m_Input->GetActionHandle("/actions/main/in/Crouch", &m_ActionCrouch);
    m_Input->GetActionHandle("/actions/main/in/Flashlight", &m_ActionFlashlight);
    m_Input->GetActionHandle("/actions/main/in/QuickPing", &m_ActionQuickPing);
    m_Input->GetActionHandle("/actions/main/in/RemoteView", &m_ActionRemoteView);
    m_Input->GetActionHandle("/actions/main/in/MenuSelect", &m_MenuSelect);
    m_Input->GetActionHandle("/actions/main/in/MenuBack", &m_MenuBack);
    m_Input->GetActionHandle("/actions/main/in/MenuUp", &m_MenuUp);
    m_Input->GetActionHandle("/actions/main/in/MenuDown", &m_MenuDown);
    m_Input->GetActionHandle("/actions/main/in/MenuLeft", &m_MenuLeft);
    m_Input->GetActionHandle("/actions/main/in/MenuRight", &m_MenuRight);
    m_Input->GetActionHandle("/actions/main/in/Spray", &m_Spray);
    m_Input->GetActionHandle("/actions/main/in/Scoreboard", &m_Scoreboard);
    m_Input->GetActionHandle("/actions/main/in/ShowHUD", &m_ShowHUD);
    m_Input->GetActionHandle("/actions/main/in/Pause", &m_Pause);
    m_Input->GetActionHandle("/actions/main/in/ThirdAttack", &m_ThirdAttack);
    m_Input->GetActionHandle("/actions/main/in/PointerLeft", &m_ActionPointerLeft);
    m_Input->GetActionHandle("/actions/main/in/PointerRight", &m_ActionPointerRight);

    m_Input->GetActionSetHandle("/actions/main", &m_ActionSet);
    m_ActiveActionSet = {};
    m_ActiveActionSet.ulActionSet = m_ActionSet;

    return 0;
}

void VR::InstallApplicationManifest(const char *fileName)
{
    char currentDir[MAX_STR_LEN];
    GetCurrentDirectory(MAX_STR_LEN, currentDir);
    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\%s", currentDir, fileName);

    vr::VRApplications()->AddApplicationManifest(path);
}

void VR::SetScreenSizeOverride(bool bState) {
    bool isOverriding = m_Game->m_VguiSurface->IsScreenSizeOverrideActive();

    if (bState && !isOverriding || !bState && isOverriding) {
        int iOldWidth, iOldHeight;
        m_Game->m_VguiSurface->GetScreenSize(iOldWidth, iOldHeight);
        m_Game->m_VguiSurface->ForceScreenSizeOverride(bState, m_RenderWidth, m_RenderHeight);
       /*int x = 0, y = 0, w = m_RenderWidth, h = m_RenderHeight;

        if (m_Game->m_ClientMode->GetViewport())
            m_Game->m_ClientMode->AdjustEngineViewport(x, y, w, h);*/

        if (bState) {
            /*IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
            renderContext->Viewport(0, 0, m_RenderWidth, m_RenderHeight);
            renderContext->Release();*/
        }

        m_Game->m_VguiSurface->OnScreenSizeChanged(iOldWidth, iOldHeight);
    }
}

void VR::Update()
{
    if (!m_IsInitialized || !m_Game->m_Initialized)
        return;

    

    if (m_IsVREnabled)
    {
        bool inGame = m_Game->m_EngineClient->IsInGame();

        //SetScreenSizeOverride(inGame);

        // Prevents crashing at menu
        if (!inGame)
        {
            IMatRenderContext *rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
            rndrContext->SetRenderTarget(NULL);
            rndrContext->Release();

            m_Game->m_CachedArmsModel = false;
            m_CreatedVRTextures = false; // Have to recreate textures otherwise some workshop maps won't render
        } 
    }

    SubmitVRTextures();
    UpdatePosesAndActions();
    // UpdateTracking reads the local player with the Portal 2 layout; for Strata Source it needs
    // the engine hooks first. Until then only the poses (overlay, menu laser).
    if (m_Game->m_Hooks)
        UpdateTracking();
    else
        GetPoses();

    if (!m_InitialPosReset)
    {
        if (m_SeatedMode)
            ResetPosition();
        m_InitialPosReset = true;
    }

    // Portal 2/Reloaded: IsCursorVisible(); in Strata Source the vgui surface reports no cursor in
    // the menus (logged for comparison).
    const bool cursorVisible = m_Game->m_VguiSurface->IsCursorVisible();
    const bool menuVisible = IsMenuVisible();
    static int lastCursorVisible = -1, lastMenuVisible = -1;
    if ((int)cursorVisible != lastCursorVisible || (int)menuVisible != lastMenuVisible)
    {
        std::cout << "[VR] menu input: menu visible=" << menuVisible << " (cursor visible=" << cursorVisible << ")\n";
        lastCursorVisible = cursorVisible;
        lastMenuVisible = menuVisible;
    }

    if (menuVisible) {
        ProcessMenuInput();
    } else {
        // ProcessMenuInput may have left the overlay interactive (laser); not outside the menus.
        vr::VROverlay()->SetOverlayFlag(m_MainMenuHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
        ProcessInput();
    }
}

void VR::CreateVRTextures()
{
    int windowWidth, windowHeight;

    IMatRenderContext* rndrContext = m_Game->m_MaterialSystem->GetRenderContext();
    rndrContext->GetWindowSize(windowWidth, windowHeight);
    rndrContext->Release();

    std::cout << "RenderTexture - Width: " << m_RenderWidth << ", Height: " << m_RenderHeight << "\n";

    m_Game->m_MaterialSystem->isGameRunning = false;
    m_Game->m_MaterialSystem->BeginRenderTargetAllocation();
    m_Game->m_MaterialSystem->isGameRunning = true;

    m_CreatingTextureID = Texture_LeftEye;
    m_LeftEyeTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("leftEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);
    
    m_CreatingTextureID = Texture_RightEye;
    m_RightEyeTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("rightEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);

    m_CreatingTextureID = Texture_HUD;
    m_HUDTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("vrHUD", m_RenderWidth, m_RenderHeight, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);
    
    m_CreatingTextureID = Texture_Blank;
    m_BlankTexture = m_Game->m_MaterialSystem->CreateNamedRenderTargetTextureEx("blankTexture", 512, 512, RT_SIZE_NO_CHANGE, m_Game->m_MaterialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SHARED, TEXTUREFLAGS_NOMIP);
    
    m_CreatingTextureID = Texture_None;

    m_Game->m_MaterialSystem->EndRenderTargetAllocation();

    m_CreatedVRTextures = true;
}

void VR::SubmitVRTextures()
{
    if (!m_RenderedNewFrame)
    {
        CreateBlankTexture();
        if (!m_VKBackBuffer.m_Texture || !m_VKBlankTexture.m_Texture)
            return;

        if (!vr::VROverlay()->IsOverlayVisible(m_MainMenuHandle))
            RepositionOverlays();

        vr::VRTextureBounds_t bounds{ 0, 0, 1, 1 };
        const bool menuVisible = IsMenuVisible();
        int windowWidth, windowHeight;
        GetWindowSize(windowWidth, windowHeight);
        if (!menuVisible)
        {
            // The game view only renders to the window portion of the back buffer (Strata Source
            // draws its menus over the whole back buffer). Until we figure out a proper fix,
            // as a workaround only show that portion of the texture. Its texels are square.
            bounds.uMax = (float)windowWidth / m_RenderWidth;
            bounds.vMax = (float)windowHeight / m_RenderHeight;
            vr::VROverlay()->SetOverlayTexelAspect(m_MainMenuHandle, 1.0f);
        }
        else
        {
            // The menu is laid out for the window but stretched over the whole back buffer:
            // texels wider than tall bring it back to the window's aspect ratio.
            const float windowAspect = (float)windowWidth / windowHeight;
            const float backBufferAspect = (float)m_VKBackBuffer.m_Width / m_VKBackBuffer.m_Height;
            vr::VROverlay()->SetOverlayTexelAspect(m_MainMenuHandle, windowAspect / backBufferAspect);
        }

        vr::VROverlay()->SetOverlayTextureBounds(m_MainMenuHandle, &bounds);
        vr::EVROverlayError overlayError = vr::VROverlay()->SetOverlayTexture(m_MainMenuHandle, &m_VKBackBuffer.m_VRTexture);
        vr::VROverlay()->ShowOverlay(m_MainMenuHandle);
        //vr::VROverlay()->HideOverlay(m_HUDHandle);

        vr::EVRCompositorError submitError = vr::VRCompositor()->Submit(vr::Eye_Left, &m_VKBlankTexture.m_VRTexture, NULL, vr::Submit_Default);
        vr::VRCompositor()->Submit(vr::Eye_Right, &m_VKBlankTexture.m_VRTexture, NULL, vr::Submit_Default);

        // Diagnostics: the first overlay frame and every change of the result codes.
        static int lastOverlayError = -1, lastSubmitError = -1, lastInGame = -1, lastMenu = -1;
        const int inGame = m_Game->m_EngineClient->IsInGame() ? 1 : 0;
        if (overlayError != lastOverlayError || submitError != lastSubmitError || inGame != lastInGame || menuVisible != lastMenu)
        {
            lastMenu = menuVisible;
            std::cout << "[VR] overlay frame: inGame=" << inGame << " menu=" << menuVisible << " backbuffer " << m_VKBackBuffer.m_Width << "x"
                      << m_VKBackBuffer.m_Height << " bounds u<" << bounds.uMax << " v<" << bounds.vMax
                      << " SetOverlayTexture=" << overlayError << " Submit=" << submitError << "\n";
            lastOverlayError = overlayError;
            lastSubmitError = submitError;
            lastInGame = inGame;
        }

        return;
    }
    vr::VROverlay()->HideOverlay(m_MainMenuHandle);

    //vr::VROverlay()->SetOverlayTexture(m_HUDHandle, &m_VKHUD.m_VRTexture);

    if (m_Game->m_VguiSurface->IsCursorVisible())
    {
        // We're in the pause menu
        //vr::VROverlay()->ShowOverlay(m_HUDHandle);
    }

    vr::VRCompositor()->Submit(vr::Eye_Left, &m_VKLeftEye.m_VRTexture, &(m_TextureBounds)[0], vr::Submit_Default);
    vr::VRCompositor()->Submit(vr::Eye_Right, &m_VKRightEye.m_VRTexture, &(m_TextureBounds)[1], vr::Submit_Default);

    m_RenderedNewFrame = false;
}

void VR::GetPoseData(vr::TrackedDevicePose_t &poseRaw, TrackedDevicePoseData &poseOut)
{
    if (poseRaw.bPoseIsValid) 
    {
        vr::HmdMatrix34_t mat = poseRaw.mDeviceToAbsoluteTracking;
        Vector pos;
        Vector vel;
        QAngle ang;
        QAngle angvel;
        pos.x = -mat.m[2][3];
        pos.y = -mat.m[0][3];
        pos.z = mat.m[1][3];
        ang.x = asin(mat.m[1][2]) * (180.0 / 3.141592654);
        ang.y = atan2f(mat.m[0][2], mat.m[2][2]) * (180.0 / 3.141592654);
        ang.z = atan2f(-mat.m[1][0], mat.m[1][1]) * (180.0 / 3.141592654);
        vel.x = -poseRaw.vVelocity.v[2];
        vel.y = -poseRaw.vVelocity.v[0];
        vel.z = poseRaw.vVelocity.v[1];
        angvel.x = -poseRaw.vAngularVelocity.v[2] * (180.0 / 3.141592654);
        angvel.y = -poseRaw.vAngularVelocity.v[0] * (180.0 / 3.141592654);
        angvel.z = poseRaw.vAngularVelocity.v[1] * (180.0 / 3.141592654);

        poseOut.TrackedDevicePos = pos;
        poseOut.TrackedDeviceVel = vel;
        poseOut.TrackedDeviceAng = ang;
        poseOut.TrackedDeviceAngVel = angvel;
    }
}

void VR::RepositionOverlays()
{
    vr::TrackedDevicePose_t hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];
    vr::HmdMatrix34_t hmdMat = hmdPose.mDeviceToAbsoluteTracking;
    Vector hmdPosition = { hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3] };
    Vector hmdForward = { -hmdMat.m[0][2], 0, -hmdMat.m[2][2] };

    vr::ETrackingUniverseOrigin trackingOrigin = vr::VRCompositor()->GetTrackingSpace();

    hmdForward[1] = 0;
    VectorNormalize(hmdForward);

    // Main menu overlay: 3 m in front of the HMD, turned towards it (yaw only). A rigid
    // transform: the overlay's size comes from SetOverlayWidthInMeters, its aspect ratio from
    // the texel aspect set in SubmitVRTextures. (The D3D9 version scaled this matrix instead,
    // so the visible area and ComputeOverlayIntersection didn't match: no laser in the menu.)
    Vector menuNewPos = hmdForward * 3 + hmdPosition;
    float hmdYaw = atan2f(hmdMat.m[0][2], hmdMat.m[2][2]);

    vr::HmdMatrix34_t menuTransform =
    {
        cosf(hmdYaw),  0.0f, sinf(hmdYaw), menuNewPos.x,
        0.0f,          1.0f, 0.0f,         menuNewPos.y - 0.25f,
        -sinf(hmdYaw), 0.0f, cosf(hmdYaw), menuNewPos.z
    };

    int windowWidth, windowHeight;
    GetWindowSize(windowWidth, windowHeight);
    const float menuHeight = 1.5f; // meters
    vr::VROverlay()->SetOverlayTransformAbsolute(m_MainMenuHandle, trackingOrigin, &menuTransform);
    vr::VROverlay()->SetOverlayWidthInMeters(m_MainMenuHandle, menuHeight * windowWidth / windowHeight);

    // Reposition HUD overlay
    /*vr::HmdMatrix34_t hudTransform =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    Vector hudDistance = hmdForward * m_HudDistance;
    Vector hudNewPos = hudDistance + hmdPosition;

    hudTransform.m[0][3] = hudNewPos.x;
    hudTransform.m[1][3] = hudNewPos.y - 0.25;
    hudTransform.m[2][3] = hudNewPos.z;

    hudTransform.m[0][0] *= cos(hmdRotationDegrees);
    hudTransform.m[0][2] = sin(hmdRotationDegrees);
    hudTransform.m[2][0] = -sin(hmdRotationDegrees);
    hudTransform.m[2][2] *= cos(hmdRotationDegrees);

    vr::VROverlay()->SetOverlayTransformAbsolute(m_HUDHandle, trackingOrigin, &hudTransform);
    vr::VROverlay()->SetOverlayWidthInMeters(m_HUDHandle, m_HudSize);*/
}

void VR::GetPoses() 
{
    vr::TrackedDevicePose_t hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];

    vr::TrackedDeviceIndex_t leftControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
    vr::TrackedDeviceIndex_t rightControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

    if (m_LeftHanded)
        std::swap(leftControllerIndex, rightControllerIndex);

    // A hand without an assigned controller (not switched on yet, lost) has the index
    // k_unTrackedDeviceIndexInvalid; its pose stays invalid and GetPoseData keeps the last one.
    auto poseOf = [this](vr::TrackedDeviceIndex_t index)
    {
        vr::TrackedDevicePose_t pose = {};
        if (index < vr::k_unMaxTrackedDeviceCount)
            pose = m_Poses[index];
        return pose;
    };
    vr::TrackedDevicePose_t leftControllerPose = poseOf(leftControllerIndex);
    vr::TrackedDevicePose_t rightControllerPose = poseOf(rightControllerIndex);

    GetPoseData(hmdPose, m_HmdPose);
    GetPoseData(leftControllerPose, m_LeftControllerPose);
    GetPoseData(rightControllerPose, m_RightControllerPose);
}

void VR::UpdatePosesAndActions() 
{
    vr::VRCompositor()->WaitGetPoses(m_Poses, vr::k_unMaxTrackedDeviceCount, NULL, 0);
    m_Input->UpdateActionState(&m_ActiveActionSet, sizeof(vr::VRActiveActionSet_t), 1);
}

void VR::GetViewParameters() 
{
    vr::HmdMatrix34_t eyeToHeadLeft = m_System->GetEyeToHeadTransform(vr::Eye_Left);
    vr::HmdMatrix34_t eyeToHeadRight = m_System->GetEyeToHeadTransform(vr::Eye_Right);
    m_EyeToHeadTransformPosLeft.x = eyeToHeadLeft.m[0][3];
    m_EyeToHeadTransformPosLeft.y = eyeToHeadLeft.m[1][3];
    m_EyeToHeadTransformPosLeft.z = eyeToHeadLeft.m[2][3];

    m_EyeToHeadTransformPosRight.x = eyeToHeadRight.m[0][3];
    m_EyeToHeadTransformPosRight.y = eyeToHeadRight.m[1][3];
    m_EyeToHeadTransformPosRight.z = eyeToHeadRight.m[2][3];
}

bool VR::CheckDigitalActionChanged(vr::VRActionHandle_t &actionHandle, bool &state)
{
    vr::InputDigitalActionData_t digitalActionData;
    vr::EVRInputError result = m_Input->GetDigitalActionData(actionHandle, &digitalActionData, sizeof(digitalActionData), vr::k_ulInvalidInputValueHandle);
    
    if (result == vr::VRInputError_None)
    {
        state = digitalActionData.bState;
        return digitalActionData.bChanged;
    }

    state = false;
    return false;
}

bool VR::GetAnalogActionData(vr::VRActionHandle_t &actionHandle, vr::InputAnalogActionData_t &analogDataOut)
{
    vr::EVRInputError result = m_Input->GetAnalogActionData(actionHandle, &analogDataOut, sizeof(analogDataOut), vr::k_ulInvalidInputValueHandle);

    if (result == vr::VRInputError_None)
        return true;

    return false;
}

void VR::ProcessMenuInput()
{
    //vr::VROverlayHandle_t currentOverlay = m_Game->m_EngineClient->IsInGame() ? m_HUDHandle : m_MainMenuHandle;
    vr::VROverlayHandle_t currentOverlay = m_MainMenuHandle;

    // Check if left or right hand controller is pointing at the overlay
    const bool isHoveringOverlay = CheckOverlayIntersectionForController(currentOverlay, vr::TrackedControllerRole_LeftHand) ||
                                   CheckOverlayIntersectionForController(currentOverlay, vr::TrackedControllerRole_RightHand);

    static int lastHovering = -1;
    if ((int)isHoveringOverlay != lastHovering)
    {
        std::cout << "[VR] controller pointing at the menu overlay: " << isHoveringOverlay << "\n";
        lastHovering = isHoveringOverlay;
    }

    // Overlays can't process action inputs if the laser is active, so
    // only activate laser if a controller is pointing at the overlay
    if (isHoveringOverlay)
    {
        vr::VROverlay()->SetOverlayFlag(currentOverlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);

        int windowWidth, windowHeight;
        GetWindowSize(windowWidth, windowHeight);

        // Laser position on the overlay (texture pixels, origin bottom left) -> game window client
        // coordinates (the engine's view of the screen).
        auto toWindow = [&](const vr::VREvent_Mouse_t &mouse, int &x, int &y)
        {
            float laserX = mouse.x;
            float laserY = mouse.y;
            if (!IsMenuVisible()) // same choice of the shown texture area as in SubmitVRTextures
            {
                laserY -= (m_RenderHeight - windowHeight);
                laserY = windowHeight - laserY;
            }
            else // main menu (uses render sized texture)
            {
                laserX = (laserX / m_RenderWidth) * windowWidth;
                laserY = ((-laserY + m_RenderHeight) / m_RenderHeight) * windowHeight;
            }
            x = (int)laserX;
            y = (int)laserY;
        };

        // The menu is Panorama; it gets the mouse from inputsystem.dll, which subclasses the game
        // window. So mouse messages go straight to that window (SendInput hit whatever was under
        // the real cursor, and only if the game had the focus).
        DXGI_SWAP_CHAIN_DESC swapDesc;
        const HWND window = D3D11Hooks::g_SwapChain && SUCCEEDED(D3D11Hooks::g_SwapChain->GetDesc(&swapDesc))
            ? swapDesc.OutputWindow : nullptr;

        vr::VREvent_t vrEvent;
        while (vr::VROverlay()->PollNextOverlayEvent(currentOverlay, &vrEvent, sizeof(vrEvent)))
        {
            int x, y;
            switch (vrEvent.eventType)
            {
            case vr::VREvent_MouseMove:
                toWindow(vrEvent.data.mouse, x, y);
                m_Game->m_VguiInput->SetCursorPos(x, y);
                if (window)
                    PostMessage(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                break;

            case vr::VREvent_MouseButtonDown:
            {
                toWindow(vrEvent.data.mouse, x, y);
                POINT cursor = {};
                GetCursorPos(&cursor);
                if (window)
                    ScreenToClient(window, &cursor);
                std::cout << "[VR] menu click at overlay " << vrEvent.data.mouse.x << "," << vrEvent.data.mouse.y
                          << " -> window " << x << "," << y << " (cursor " << cursor.x << "," << cursor.y
                          << ", game window " << (window ? "found" : "NOT found")
                          << (window && GetForegroundWindow() == window ? ", foreground" : ", NOT foreground") << ")\n";
                if (window)
                {
                    PostMessage(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
                    PostMessage(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
                }
                break;
            }

            case vr::VREvent_MouseButtonUp:
                toWindow(vrEvent.data.mouse, x, y);
                if (window)
                    PostMessage(window, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
                break;

            case vr::VREvent_ScrollDiscrete:
                m_Game->m_VguiInput->InternalMouseWheeled((int)vrEvent.data.scroll.ydelta);
                break;
            }
        }
    }
    else
    {
        vr::VROverlay()->SetOverlayFlag(currentOverlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
        
        bool state;
        if (CheckDigitalActionChanged(m_MenuSelect, state) && state)
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if ((CheckDigitalActionChanged(m_MenuBack, state) && state) || (CheckDigitalActionChanged(m_Pause, state) && state))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_ESCAPE;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (CheckDigitalActionChanged(m_MenuUp, state) && state)
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_UP;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (CheckDigitalActionChanged(m_MenuDown, state) && state)
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_DOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (CheckDigitalActionChanged(m_MenuLeft, state) && state)
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_LEFT;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (CheckDigitalActionChanged(m_MenuRight, state) && state)
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RIGHT;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
    }
}

void VR::ProcessInput()
{
    if (!m_IsVREnabled)
        return;

    //vr::VROverlay()->SetOverlayFlag(m_HUDHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);

    typedef std::chrono::duration<float, std::milli> duration;
    auto currentTime = std::chrono::steady_clock::now();
    duration elapsed = currentTime - m_PrevFrameTime;
    float deltaTime = elapsed.count();
    m_PrevFrameTime = currentTime;

    vr::InputAnalogActionData_t analogActionData;

    if (GetAnalogActionData(m_ActionTurn, analogActionData))
    {
        if (m_SnapTurning)
        {
            if (!m_PressedTurn && analogActionData.x > 0.5)
            {
                m_RotationOffset.y -= m_SnapTurnAngle;
                m_PressedTurn = true;
            }
            else if (!m_PressedTurn && analogActionData.x < -0.5)
            {
                m_RotationOffset.y += m_SnapTurnAngle;
                m_PressedTurn = true;
            }
            else if (analogActionData.x < 0.3 && analogActionData.x > -0.3)
                m_PressedTurn = false;
        }
        // Smooth turning
        else
        {
            float deadzone = 0.2;
            // smoother turning
            float xNormalized = (abs(analogActionData.x) - deadzone) / (1 - deadzone);
            if (analogActionData.x > deadzone)
            {
                m_RotationOffset.y -= m_TurnSpeed * deltaTime * xNormalized;
            }
            if (analogActionData.x < -deadzone)
            {
                m_RotationOffset.y += m_TurnSpeed * deltaTime * xNormalized;
            }
        }

        // Wrap from 0 to 360
        m_RotationOffset.y -= 360 * std::floor(m_RotationOffset.y / 360);
    }

    // Re-align camera upright after portalling
    if (m_RotationOffset.x != 0.f || m_RotationOffset.z != 0.f)
    {
        // Last valid yaw angle, in case we need to revert to it.
        const float lastYaw = m_RotationOffset.y;

        const QAngle targetRotation(0, lastYaw, 0);

        const auto lerp = [](float a, float b, float f) -> float {
            return a * (1.0 - f) + (b * f);
        };

        float lerpedPitch = lerp(m_RotationOffset.x, targetRotation.x, m_CameraUprightRecoverySpeed);
        float lerpedYaw = lerp(m_RotationOffset.y, targetRotation.y, m_CameraUprightRecoverySpeed);
        float lerpedRoll = lerp(m_RotationOffset.z, targetRotation.z, m_CameraUprightRecoverySpeed);

        m_RotationOffset = QAngle(lerpedPitch, lerpedYaw, lerpedRoll);

        if (abs(m_RotationOffset.x) < 0.0001)
            m_RotationOffset.x = 0;
        if (abs(m_RotationOffset.z) < 0.0001)
            m_RotationOffset.z = 0;

        // Just in case any calculation went awry, revert to an upright vector:
        if (std::isnan(m_RotationOffset.x) || std::isnan(m_RotationOffset.y) || std::isnan(m_RotationOffset.z))
        {
            m_RotationOffset = QAngle(0.f, lastYaw, 0.f);
        }
    }

    bool state;
    if (CheckDigitalActionChanged(m_ActionPrimaryAttack, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+attack" : "-attack");
    }

    if (CheckDigitalActionChanged(m_ActionSecondaryAttack, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+attack2" : "-attack2");
    }

    if (CheckDigitalActionChanged(m_ThirdAttack, state) && state)
    {
        m_Game->ClientCmd_Unrestricted("ent_fire @att3 trigger");
    }

    if (CheckDigitalActionChanged(m_ActionJump, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+jump" : "-jump");
    }

    if (CheckDigitalActionChanged(m_ActionCrouch, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+duck" : "-duck");
    }

    if (CheckDigitalActionChanged(m_ActionUse, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+use" : "-use");
    }

    if (CheckDigitalActionChanged(m_ActionQuickPing, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+quick_ping" : "-quick_ping");
    }

    if (CheckDigitalActionChanged(m_ActionRemoteView, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+remote_view" : "-remote_view");
    }

    if (CheckDigitalActionChanged(m_ActionReload, state))
    {
        m_Game->ClientCmd_Unrestricted(state ? "+reload" : "-reload");
    }

    if (CheckDigitalActionChanged(m_ActionPrevItem, state) && state)
    {
        m_Game->ClientCmd_Unrestricted("invprev");
    }
    else if (CheckDigitalActionChanged(m_ActionNextItem, state) && state)
    {
        m_Game->ClientCmd_Unrestricted("invnext");
    }

    if (CheckDigitalActionChanged(m_ActionResetPosition, state) && state)
    {
        ResetPosition();
    }

    if (CheckDigitalActionChanged(m_ActionFlashlight, state) && state)
    {
        m_Game->ClientCmd_Unrestricted("impulse 100");
    }

    if (CheckDigitalActionChanged(m_Spray, state) && state)
    {
        m_Game->ClientCmd_Unrestricted("impulse 201");
    }
    
    /*bool isControllerVertical = m_RightControllerAngAbs.x > 60 || m_RightControllerAngAbs.x < -45;
    if ((PressedDigitalAction(m_ShowHUD) || PressedDigitalAction(m_Scoreboard) || isControllerVertical || m_HudAlwaysVisible)
        && m_RenderedHud)
    {
        if (!vr::VROverlay()->IsOverlayVisible(m_HUDHandle) || m_HudAlwaysVisible)
            RepositionOverlays();

        if (PressedDigitalAction(m_Scoreboard))
            m_Game->ClientCmd_Unrestricted("+showscores");
        else
            m_Game->ClientCmd_Unrestricted("-showscores");

        vr::VROverlay()->ShowOverlay(m_HUDHandle);
    }
    else
    {
        vr::VROverlay()->HideOverlay(m_HUDHandle);
    }*/

    m_RenderedHud = false;

    if (CheckDigitalActionChanged(m_Pause, state) && state)
    {
        m_Game->ClientCmd_Unrestricted("gameui_activate");
        RepositionOverlays();
    }
}

VMatrix VR::VMatrixFromHmdMatrix(const vr::HmdMatrix34_t &hmdMat)
{
    // VMatrix has a different implicit coordinate system than HmdMatrix34_t, but this function does not convert between them
    VMatrix vMat(
        hmdMat.m[0][0], hmdMat.m[1][0], hmdMat.m[2][0], 0.0f,
        hmdMat.m[0][1], hmdMat.m[1][1], hmdMat.m[2][1], 0.0f,
        hmdMat.m[0][2], hmdMat.m[1][2], hmdMat.m[2][2], 0.0f,
        hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3], 1.0f
    );

    return vMat;
}

vr::HmdMatrix34_t VR::GetControllerTipMatrix(vr::ETrackedControllerRole controllerRole)
{
    vr::VRInputValueHandle_t inputValue = vr::k_ulInvalidInputValueHandle;

    if (controllerRole == vr::TrackedControllerRole_RightHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/right", &inputValue);
    }
    else if (controllerRole == vr::TrackedControllerRole_LeftHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/left", &inputValue);
    }

    if (inputValue != vr::k_ulInvalidInputValueHandle)
    {
        char buffer[vr::k_unMaxPropertyStringSize];

        m_System->GetStringTrackedDeviceProperty(vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(controllerRole), vr::Prop_RenderModelName_String, 
                                                 buffer, vr::k_unMaxPropertyStringSize);

        vr::RenderModel_ControllerMode_State_t controllerState = {0};
        vr::RenderModel_ComponentState_t componentState = {0};

        const bool found = vr::VRRenderModels()->GetComponentStateForDevicePath(buffer, vr::k_pch_Controller_Component_Tip, inputValue, &controllerState, &componentState);
        static bool logged[2] = { false, false };
        const int hand = controllerRole == vr::TrackedControllerRole_LeftHand ? 0 : 1;
        if (!logged[hand])
        {
            std::cout << "[VR] render model '" << buffer << "': tip component " << (found ? "found" : "NOT found") << "\n";
            logged[hand] = true;
        }
        if (found)
        {
            return componentState.mTrackingToComponentLocal;
        }
    }

    // Not a hand controller role or tip lookup failed, return identity
    const vr::HmdMatrix34_t identity = 
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    return identity;
}

// Rigid poses: 'to' relative to 'from' (inverse(from) * to), and a * b.
static vr::HmdMatrix34_t RelativePose(const vr::HmdMatrix34_t &from, const vr::HmdMatrix34_t &to)
{
    vr::HmdMatrix34_t r = {};
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                r.m[i][j] += from.m[k][i] * to.m[k][j];
        for (int k = 0; k < 3; ++k)
            r.m[i][3] += from.m[k][i] * (to.m[k][3] - from.m[k][3]);
    }
    return r;
}

static vr::HmdMatrix34_t ComposePose(const vr::HmdMatrix34_t &a, const vr::HmdMatrix34_t &b)
{
    vr::HmdMatrix34_t r = {};
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 3; ++k)
                r.m[i][j] += a.m[i][k] * b.m[k][j];
        r.m[i][3] += a.m[i][3];
    }
    return r;
}

bool VR::CheckOverlayIntersectionForController(vr::VROverlayHandle_t overlayHandle, vr::ETrackedControllerRole controllerRole)
{
    vr::VROverlayIntersectionParams_t  params  = {0};
    vr::VROverlayIntersectionResults_t results = {0};
    params.eOrigin = vr::VRCompositor()->GetTrackingSpace();

    // The ray of SteamVR's laser is the tip pose (/user/hand/*/pose/tip, pointer actions). Once the
    // overlay is interactive, SteamVR suspends the app's input and the action goes inactive, so the
    // tip is kept as an offset to the controller pose (which keeps updating) and the ray always
    // comes from that. Without it the hit test flipped every frame (flickering laser, no clicks).
    // Fallback before the first pointer pose: the render model's "tip" component (missing e.g. on
    // Quest via Virtual Desktop, then it's the grip pose, which points well below the laser).
    const bool left = controllerRole == vr::TrackedControllerRole_LeftHand;
    vr::InputPoseActionData_t pointer = {};
    const bool havePointer =
        m_Input->GetPoseActionDataForNextFrame(left ? m_ActionPointerLeft : m_ActionPointerRight, params.eOrigin,
            &pointer, sizeof(pointer), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None &&
        pointer.bActive && pointer.pose.bPoseIsValid;

    const vr::TrackedDeviceIndex_t deviceIndex = m_System->GetTrackedDeviceIndexForControllerRole(controllerRole);
    const vr::TrackedDevicePose_t *controllerPose =
        deviceIndex < vr::k_unMaxTrackedDeviceCount && m_Poses[deviceIndex].bPoseIsValid ? &m_Poses[deviceIndex] : nullptr;

    if (havePointer && controllerPose)
    {
        if (!m_HavePointerOffset[left])
            std::cout << "[VR] menu laser " << (left ? "left" : "right") << ": tip offset learned from the pointer action\n";
        m_PointerOffset[left] = RelativePose(controllerPose->mDeviceToAbsoluteTracking, pointer.pose.mDeviceToAbsoluteTracking);
        m_HavePointerOffset[left] = true;
    }

    vr::HmdMatrix34_t ray;
    int source;
    if (controllerPose && m_HavePointerOffset[left])
    {
        ray = ComposePose(controllerPose->mDeviceToAbsoluteTracking, m_PointerOffset[left]);
        source = 0;
    }
    else if (havePointer)
    {
        ray = pointer.pose.mDeviceToAbsoluteTracking;
        source = 1;
    }
    else if (controllerPose)
    {
        ray = ComposePose(controllerPose->mDeviceToAbsoluteTracking, GetControllerTipMatrix(controllerRole));
        source = 2;
    }
    else
    {
        return false;
    }

    static int lastSource[2] = { -1, -1 };
    if (lastSource[left] != source)
    {
        static const char *const names[] = { "controller pose x tip offset", "tip pose action", "fallback (render model tip)" };
        std::cout << "[VR] menu laser " << (left ? "left" : "right") << ": " << names[source] << "\n";
        lastSource[left] = source;
    }

    params.vSource    = { ray.m[0][3], ray.m[1][3], ray.m[2][3] };
    params.vDirection = { -ray.m[0][2], -ray.m[1][2], -ray.m[2][2] };
    return m_Overlay->ComputeOverlayIntersection(overlayHandle, &params, &results);
}

QAngle VR::GetRightControllerAbsAngle()
{
    return m_RightControllerAngAbs;
}

QAngle& VR::GetRightControllerAbsAngleConst()
{
    return m_RightControllerAngAbs;
}

Vector VR::GetRightControllerAbsPos(Vector eyePosition)
{
    Vector offset = eyePosition;

    if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
        /*int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
        C_BasePlayer* localPlayer = (C_BasePlayer*)m_Game->GetClientEntity(playerIndex);
        if (!localPlayer)
            return {0, 0, 0};

        offset = localPlayer->EyePosition();*/

        offset = m_SetupOrigin;
    }

    Vector position = offset + m_RightControllerPosRel;

    if (m_6DOF)
        position += m_HmdPosRelative;

    return position;
}

Vector VR::GetRecommendedViewmodelAbsPos(Vector eyePosition)
{
    Vector viewmodelPos = GetRightControllerAbsPos(eyePosition);
    viewmodelPos -= m_ViewmodelForward * m_ViewmodelPosOffset.x;
    viewmodelPos -= m_ViewmodelRight * m_ViewmodelPosOffset.y;
    viewmodelPos -= m_ViewmodelUp * m_ViewmodelPosOffset.z;

    return viewmodelPos;
}

QAngle VR::GetRecommendedViewmodelAbsAngle()
{
    QAngle result{};

    QAngle::VectorAngles(m_ViewmodelForward, m_ViewmodelUp, result);

    return result;
}

void VR::UpdateHMDAngles() {
    QAngle hmdAngLocal = m_HmdPose.TrackedDeviceAng;

    //hmdAngLocal += m_RotationOffset;
    hmdAngLocal.x += m_RotationOffset.x;
    hmdAngLocal.y += m_RotationOffset.y;
    hmdAngLocal.z += m_RotationOffset.z;

    //hmdAngLocal.Normalize();

    QAngle::AngleVectors(hmdAngLocal, &m_HmdForward, &m_HmdRight, &m_HmdUp);

    //hmdAngLocal.x = (hmdAngLocal.x > 180 ? 180)
    hmdAngLocal.Normalize();

    m_HmdAngAbs = hmdAngLocal;
}

void VR::ResetPosition()
{
    m_Center = m_HmdPose.TrackedDevicePos;
    if (!m_SeatedMode)
        m_Center.z = 0;
}

void VR::UpdateTracking()
{
    GetPoses();

    int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
    C_BasePlayer* localPlayer = (C_BasePlayer*)m_Game->GetClientEntity(playerIndex);
    if (!localPlayer)
        return;

    // HMD tracking
    Vector hmdPosLocal = m_HmdPose.TrackedDevicePos;
    Vector hmdPosCentered = hmdPosLocal - m_Center;

    m_HmdPosRelativeRaw = hmdPosCentered;

    //std::cout << "HMD - X: " << hmdWorldPos.x << ", Y: " << hmdWorldPos.y << ", Z: " << hmdWorldPos.z << "\n";

    Vector hmdPosCorrected = hmdPosCentered;
    VectorPivotXY(hmdPosCorrected, { 0, 0, 0 }, m_RotationOffset.y);
    
    UpdateHMDAngles();

    m_HmdPosRelative = hmdPosCorrected * m_VRScale;
    if (!m_SeatedMode)
        // 64 is the eye view height from the player's base position
        // we subtract this here so that we place the HMD height at the actual player height if 6DOF is enabled
        m_HmdPosRelative.z -= 64;
    else
        m_HmdPosRelative.z += m_SeatedHeightOffset;

    // Roomscale setup
    /*Vector cameraMovingDirection = m_Center - m_SetupOriginPrev;
    Vector cameraToPlayer = m_HmdPosAbsPrev - m_SetupOriginPrev;
    cameraMovingDirection.z = 0;
    cameraToPlayer.z = 0;
    float cameraFollowing = DotProduct(cameraMovingDirection, cameraToPlayer);
    float cameraDistance = VectorLength(cameraToPlayer);

    if (localPlayer->m_hGroundEntity != -1 && localPlayer->m_vecVelocity.IsZero())
        m_RoomscaleActive = true;

    // TODO: Get roomscale to work while using thumbstick
    if ((cameraFollowing < 0 && cameraDistance > 1) || (m_PushingThumbstick))
        m_RoomscaleActive = false;*/

    // Check if camera is clipping inside wall
    /*CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    Vector extendedHmdPos = m_HmdPosAbs - m_SetupOrigin;
    VectorNormalize(extendedHmdPos);
    extendedHmdPos = m_HmdPosAbs + (extendedHmdPos * 10);
    ray.Init(m_SetupOrigin, extendedHmdPos);

    m_Game->m_EngineTrace->TraceRay(ray, STANDARD_TRACE_MASK, &tracefilter, &trace);
    if (trace.fraction < 1 && trace.fraction > 0)
    {
        Vector distanceInsideWall = trace.endpos - extendedHmdPos;
        m_CameraAnchor += distanceInsideWall;
        m_HmdPosAbs = m_CameraAnchor - Vector(0, 0, 64) + m_HmdPosLocalInWorld;
    }

    // Reset camera if it somehow gets too far
    m_SetupOriginToHMD = m_HmdPosAbs - m_SetupOrigin;
    if (VectorLength(m_SetupOriginToHMD) > 150)
        ResetPosition();

    m_HmdPosAbsPrev = m_HmdPosAbs;
    m_SetupOriginPrev = m_SetupOrigin;*/

    GetViewParameters();
    m_Ipd = m_EyeToHeadTransformPosRight.x * 2;
    m_EyeZ = m_EyeToHeadTransformPosRight.z;

    // Hand tracking
    Vector leftControllerPosLocal = m_LeftControllerPose.TrackedDevicePos;
    QAngle leftControllerAngLocal = m_LeftControllerPose.TrackedDeviceAng;

    Vector rightControllerPosLocal = m_RightControllerPose.TrackedDevicePos;
    QAngle rightControllerAngLocal = m_RightControllerPose.TrackedDeviceAng;

    //std::cout << "Right Controller - X: " << rightControllerPosLocal.x << "Y: " << rightControllerPosLocal.y << "Z: " << rightControllerPosLocal.z << "\n";

    Vector hmdToController = rightControllerPosLocal - hmdPosLocal;
    //Vector rightControllerPosCorrected = hmdPosCorrected + hmdToController;

    // When using stick turning, pivot the controllers around the HMD
    VectorPivotXY(hmdToController, { 0, 0, 0 }, m_RotationOffset.y);

    m_RightControllerPosRel = hmdToController * m_VRScale;

    //rightControllerAngLocal += m_RotationOffset;
    rightControllerAngLocal.x += m_RotationOffset.x;
    rightControllerAngLocal.y += m_RotationOffset.y;
    rightControllerAngLocal.z += m_RotationOffset.z;

    // Wrap angle from -180 to 180
    //rightControllerAngLocal.Normalize();

    QAngle::AngleVectors(leftControllerAngLocal, &m_LeftControllerForward, &m_LeftControllerRight, &m_LeftControllerUp);
    QAngle::AngleVectors(rightControllerAngLocal, &m_RightControllerForward, &m_RightControllerRight, &m_RightControllerUp);

    const float offset = -30;

    // Adjust controller angle downward
    m_LeftControllerForward = VectorRotate(m_LeftControllerForward, m_LeftControllerRight, offset);
    m_LeftControllerUp = VectorRotate(m_LeftControllerUp, m_LeftControllerRight, offset);

    m_RightControllerForward = VectorRotate(m_RightControllerForward, m_RightControllerRight, offset);
    m_RightControllerUp = VectorRotate(m_RightControllerUp, m_RightControllerRight, offset);

    // controller angles
    QAngle::VectorAngles(m_LeftControllerForward, m_LeftControllerUp, m_LeftControllerAngAbs);
    QAngle::VectorAngles(m_RightControllerForward, m_RightControllerUp, m_RightControllerAngAbs);
    m_RightControllerAngAbs.Normalize();

    PositionAngle viewmodelOffset = PositionAngle{ {4.5, -1, 1.5}, {0,0,0} };

    // Apply both hardcoded and custom (from config) viewmodel offsets here:
    m_ViewmodelPosOffset = viewmodelOffset.position + m_ViewmodelPosCustomOffset;
    m_ViewmodelAngOffset = viewmodelOffset.angle + m_ViewmodelAngCustomOffset;

    m_ViewmodelForward = m_RightControllerForward;
    m_ViewmodelUp = m_RightControllerUp;
    m_ViewmodelRight = m_RightControllerRight;

    // Viewmodel yaw offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelUp, m_ViewmodelAngOffset.y);
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelUp, m_ViewmodelAngOffset.y);

    // Viewmodel pitch offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelRight, m_ViewmodelAngOffset.x);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelRight, m_ViewmodelAngOffset.x);

    // Viewmodel roll offset
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelForward, m_ViewmodelAngOffset.z);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelForward, m_ViewmodelAngOffset.z);

    // The aim trace needs the controller pose of this frame (position relative to the HMD and
    // direction, both rotated by m_RotationOffset). Done before they were updated, the laser was
    // one frame behind, which showed after a snap turn.
    m_AimPos = Trace((uint32_t*)localPlayer);

    if (m_AimMode == 2) {
        C_Portal_Player* portalPlayer = (C_Portal_Player*)localPlayer;

        auto activeWeaponAddr = (*(int(__thiscall**)(void*))(*(uintptr_t*)portalPlayer + 968))(portalPlayer);
        //auto activeWeaponAddr = (*(int(__thiscall**)(void*))(*(uintptr_t*)m_Game->m_Offsets->GetActivePortalWeapon.address))(portalPlayer);

        if (activeWeaponAddr && m_DrawCrosshair) {
            CWeaponPortalBase* activeWeapon = (CWeaponPortalBase*)activeWeaponAddr;

            if (portalPlayer->m_PointLaser) {
                portalPlayer->m_PointLaser->SetControlPoint(1, m_AimPos);
                portalPlayer->m_PointLaser->SetControlPoint(2, m_Game->m_singlePlayerPortalColors[activeWeapon->m_iLastFiredPortal] * 0.5f);
            }
            else {
                std::cout << "Creating Point Laser Beam Sight Thingy" << "\n";
                m_Game->m_Hooks->CreatePingPointer(localPlayer, m_AimPos);
            }
        }
        else if (portalPlayer->m_PointLaser){
            portalPlayer->m_PointLaser->StopEmission(false, true, false);
            portalPlayer->m_PointLaser = NULL;
        }
    }
}

Vector VR::GetViewAngle()
{
    return Vector( m_HmdAngAbs.x, m_HmdAngAbs.y, m_HmdAngAbs.z );
}

Vector VR::GetViewOrigin(Vector setupOrigin)
{
    Vector center = setupOrigin;

    if (m_6DOF)
        center += m_HmdPosRelative;

    return center + (m_HmdForward * -(m_EyeZ * m_VRScale));
}

Vector VR::GetViewOriginLeft(Vector setupOrigin)
{
    Vector viewOriginLeft = GetViewOrigin(setupOrigin);
    viewOriginLeft -= m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginLeft;
}

Vector VR::GetViewOriginRight(Vector setupOrigin)
{
    Vector viewOriginRight = GetViewOrigin(setupOrigin);
    viewOriginRight += m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginRight;
}

Vector VR::Trace(uint32_t* localPlayer) {
    Vector vecStart = GetRightControllerAbsPos();
    Vector vecEnd = vecStart + m_RightControllerForward * MAX_TRACE_LENGTH;

    CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(vecStart, vecEnd);

    m_Game->m_EngineTrace->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trace);

    return trace.endpos;
}

void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{
    float sr, sp, sy, cr, cp, cy;

    SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
    SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
    SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);

    // matrix = (YAW * PITCH) * ROLL
    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;

    // NOTE: Do not optimize this to reduce multiplies! optimizer bug will screw this up.
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = (cr * sp * cy + -sr * -sy);
    matrix[1][2] = (cr * sp * sy + -sr * cy);
    matrix[2][2] = cr * cp;

    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
    memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}


/*
================
R_ConcatTransforms
================
*/

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
    if (&in1 == &out)
    {
        matrix3x4_t in1b;
        MatrixCopy(in1, in1b);
        ConcatTransforms(in1b, in2, out);
        return;
    }
    if (&in2 == &out)
    {
        matrix3x4_t in2b;
        MatrixCopy(in2, in2b);
        ConcatTransforms(in1, in2b, out);
        return;
    }
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
        in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
        in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
        in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
        in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
        in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
        in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
        in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
        in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
        in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
        in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
        in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
        in1[2][2] * in2[2][3] + in1[2][3];
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
    float forward[3];
    float left[3];
    float up[3];

    //
    // Extract the basis vectors from the matrix. Since we only need the Z
    // component of the up vector, we don't get X and Y.
    //
    forward[0] = matrix[0][0];
    forward[1] = matrix[1][0];
    forward[2] = matrix[2][0];
    left[0] = matrix[0][1];
    left[1] = matrix[1][1];
    left[2] = matrix[2][1];
    up[2] = matrix[2][2];

    float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

    // enough here to get angles?
    if (xyDist > 0.001f)
    {
        // (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
        angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

        // (roll)	z = ATAN( left.z, up.z );
        angles[2] = RAD2DEG(atan2f(left[2], up[2]));
    }
    else	// forward is mostly Z, gimbal lock-
    {
        // (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
        angles[1] = RAD2DEG(atan2f(-left[0], left[1]));

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

        // Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
        angles[2] = 0;
    }
}

inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
{
    MatrixAngles(matrix, &angles.x);
}



// transform a set of angles in the input space of parentMatrix to the output space
QAngle TransformAnglesToWorldSpace(const QAngle& angles, const matrix3x4_t& parentMatrix)
{
    matrix3x4_t angToParent, angToWorld;
    AngleMatrix(angles, angToParent);
    ConcatTransforms(parentMatrix, angToParent, angToWorld);
    QAngle out;
    MatrixAngles(angToWorld, out);
    return out;
}


Vector VR::TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle) {
    CGameTrace trTestObstructionsNearPortals;
    Ray_t ray;
    CTraceFilterSkipNPCsAndPlayers tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(cameraPos, eyePos);
    m_Game->m_EngineTrace->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trTestObstructionsNearPortals);

    float flWallHitFraction = trTestObstructionsNearPortals.fraction + 0.01f;
    CPortal_Base2D* pPortal = (CPortal_Base2D*)m_Game->m_Hooks->UTIL_Portal_FirstAlongRay(ray, flWallHitFraction);

    if (trTestObstructionsNearPortals.DidHit() && pPortal) {
        float flRayHitFraction = m_Game->m_Hooks->UTIL_IntersectRayWithPortal(ray, pPortal);
        //Vector vNewEye;
        Vector vHitPoint = ray.m_Start + ray.m_Delta * flRayHitFraction;
        //vNewEye = m_Game->m_Hooks->UTIL_Portal_PointTransform(pPortal->MatrixThisToLinked(), vHitPoint, vNewEye);

        //VMatrix matrix = *(VMatrix*)((uintptr_t)pPortal + 0x4C4);
        VMatrix matrix = pPortal->MatrixThisToLinked();

   
        /*QAngle newAngle;
        m_Game->m_Hooks->UTIL_Portal_AngleTransform(matrix, eyeAngle, newAngle);*/
        eyeAngle = TransformAnglesToWorldSpace(eyeAngle, matrix.As3x4());

        return matrix * vHitPoint;

        //return pPortal->MatrixThisToLinked() * vHitPoint;
    }

    return eyePos;
}

// [CONFIG PARSING UTILITY FUNCTION]
// Generates an error message by stringifying and concatenating 'args...'.
template <typename... Ts>
static void concatErrorMsg(Game& game, const Ts&... args)
{
    std::ostringstream oss;
    (oss << ... << args);
    game.errorMsg(oss.str().c_str());
}

// [CONFIG PARSING UTILITY FUNCTION]
// Attempts to parse an entry with key 'key' from the provided 'userConfig'. If the key is
// missing or if the parsing fails, 'defaultValue' is returned and an error message is
// generated.
template <typename T>
static T parseConfigEntry(
    const std::unordered_map<std::string, std::string>& userConfig, Game& game,
    const char* key, const T& defaultValue)
try
{
    const auto itr = userConfig.find(key);

    if (itr == userConfig.end())
    {
        concatErrorMsg(game, "Config entry with key '", key,
            "' missing -- reverting to default value of '", defaultValue, "'");

        return defaultValue;
    }

    const std::string& configValue = itr->second;

    if constexpr (std::is_same_v<T, bool>)
    {
        return configValue == "true";
    }
    else if constexpr(std::is_floating_point_v<T>)
    {
        return std::stof(configValue);
    }
    else if constexpr(std::is_integral_v<T>)
    {
        return std::stol(configValue);
    }
    else
    {
        // Just a way of generating a compilation failure in case this branch is taken.
        struct invalid_type;
        return invalid_type{};
    }
}
catch (const std::logic_error& e)
{
    concatErrorMsg(game, "Error parsing config entry with key '", key,
        "' -- reverting to default value of '", defaultValue, "' -- error: (", e.what(), ")");

    throw;
}

void VR::ParseConfigFile()
{
    std::ifstream configStream("VR\\config.txt");
    std::unordered_map<std::string, std::string> userConfig;

    std::string line;
    while (std::getline(configStream, line))
    {
        std::istringstream sLine(line);
        std::string key;
        if (std::getline(sLine, key, '='))
        {
            std::string value;
            if (std::getline(sLine, value, '#'))
                userConfig[key] = value;
            else if (std::getline(sLine, value))
                userConfig[key] = value;
        }
    }

    if (userConfig.empty())
        return;

    // Parse a single entry with key 'key' from the config into 'target'.
    // If the entry does not exist, or if the parsing fails, sets 'target' to
    // 'defaultValue'.
    const auto parseOrDefault = [&](const char* key, auto& target,
                                    const auto& defaultValue) 
    { 
        target = parseConfigEntry(userConfig, *m_Game, key, defaultValue);
        std::cout << "Setting '" << key << "' to '" << target << "'\n";
    };

    // Parses a vector or angle from the config into 'target'. The XYZ coordinates
    // are read from three separate config entries with key 'keyPrefix' + 'X'/'Y'/'Z'.
    // If any entry does not exist, or if the parsing fails, sets the corresponding
    // coordinate in 'target' to zero.
    const auto parseXYZOrDefaultZero = [&](std::string keyPrefix, auto& target)
    {
        parseOrDefault((keyPrefix + "X").c_str(), target.x, 0.f);
        parseOrDefault((keyPrefix + "Y").c_str(), target.y, 0.f);
        parseOrDefault((keyPrefix + "Z").c_str(), target.z, 0.f);
    };

    parseOrDefault("SnapTurning", m_SnapTurning, true);
    parseOrDefault("SnapTurnAngle", m_SnapTurnAngle, 45.0f);
    parseOrDefault("TurnSpeed", m_TurnSpeed, 0.15f);
    parseOrDefault("LeftHanded", m_LeftHanded, false);
    parseOrDefault("VRScale", m_VRScale, 43.2f);
    parseOrDefault("IPDScale", m_IpdScale, 1.0f);
    parseOrDefault("6DOF", m_6DOF, true);
    parseOrDefault("SeatedMode", m_SeatedMode, false);
    parseOrDefault("SeatedHeightOffset", m_SeatedHeightOffset, 4.0f);
    /*parseOrDefault("HudDistance", m_HudDistance, 1.3f);
    parseOrDefault("HudSize", m_HudSize, 4.0f);
    parseOrDefault("HudAlwaysVisible", m_HudAlwaysVisible, false);*/
    parseOrDefault("AimMode", m_AimMode, 2);
    parseOrDefault("AntiAliasing", m_AntiAliasing, 0);
    parseOrDefault("RenderWindow", m_RenderWindow, 0);
    parseXYZOrDefaultZero("ViewmodelPosCustomOffset", m_ViewmodelPosCustomOffset);
    parseXYZOrDefaultZero("ViewmodelAngCustomOffset", m_ViewmodelAngCustomOffset);
    parseOrDefault("PortallingDetectionDistanceThreshold", m_PortallingDetectionDistanceThreshold, 35);
    parseOrDefault("ApplyPitchAndRollPortalRotationOffset", m_ApplyPitchAndRollPortalRotationOffset, false);
    parseOrDefault("CameraUprightRecoverySpeed", m_CameraUprightRecoverySpeed, 0.2f);
}

void VR::WaitForConfigUpdate()
{
    char currentDir[MAX_STR_LEN];
    GetCurrentDirectory(MAX_STR_LEN, currentDir);
    char configDir[MAX_STR_LEN];
    sprintf_s(configDir, MAX_STR_LEN, "%s\\VR\\", currentDir);
    HANDLE fileChangeHandle = FindFirstChangeNotificationA(configDir, false, FILE_NOTIFY_CHANGE_LAST_WRITE);

    std::filesystem::file_time_type configLastModified;
    while (1)
    {
        try 
        {
            // Windows only notifies of change within a directory, so extra check here for just config.txt
            auto configModifiedTime = std::filesystem::last_write_time("VR\\config.txt");
            if (configModifiedTime != configLastModified)
            {
                configLastModified = configModifiedTime;
                ParseConfigFile();
                
                std::cout << "Successfully reloaded 'config.txt'\n";
            }
        }
        catch (const std::invalid_argument &e)
        {
            concatErrorMsg(
                *m_Game, "Failed to parse 'config.txt' (", e.what(), ")");
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            concatErrorMsg(
                *m_Game, "'config.txt' not found. (", e.what(), ")");
            
            return;
        }
        
        FindNextChangeNotification(fileChangeHandle);
        WaitForSingleObject(fileChangeHandle, INFINITE);
        Sleep(100); // Sometimes the thread tries to read config.txt before it's finished writing
    }
}
