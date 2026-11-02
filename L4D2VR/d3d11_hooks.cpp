#include "d3d11_hooks.h"
#include "MinHook.h"
#include <iostream>

namespace D3D11Hooks
{
    ID3D11Device *g_Device = nullptr;
    ID3D11DeviceContext *g_Context = nullptr;
    IDXGISwapChain *g_SwapChain = nullptr;

    std::function<void(UINT &width, UINT &height)> SwapChainSize_Callback;
    std::function<void(D3D11_TEXTURE2D_DESC &desc)> CreateTexture2D_Pre_Callback;
    std::function<void(ID3D11Texture2D *texture, const D3D11_TEXTURE2D_DESC &desc)> CreateTexture2D_Post_Callback;
    std::function<void(UINT count, D3D11_VIEWPORT *viewports)> SetViewports_Callback;
    std::function<void(IDXGISwapChain *swapChain)> Present_Pre_Callback;
    std::function<void(IDXGISwapChain *swapChain)> Present_Post_Callback;
}

using namespace D3D11Hooks;

namespace
{
    // vtable indices: declaration order in d3d11.h/dxgi.h/dxgi1_2.h, IUnknown occupies 0..2.
    constexpr int kDevice_CreateTexture2D = 5;
    constexpr int kContext_RSSetViewports = 44;
    constexpr int kFactory_CreateSwapChain = 10;
    constexpr int kFactory2_CreateSwapChainForHwnd = 15;
    constexpr int kSwapChain_Present = 8;
    constexpr int kSwapChain_ResizeBuffers = 13;
    constexpr int kSwapChain1_Present1 = 22;

    typedef HRESULT(STDMETHODCALLTYPE *tCreateTexture2D)(ID3D11Device *, const D3D11_TEXTURE2D_DESC *,
        const D3D11_SUBRESOURCE_DATA *, ID3D11Texture2D **);
    typedef void(STDMETHODCALLTYPE *tRSSetViewports)(ID3D11DeviceContext *, UINT, const D3D11_VIEWPORT *);
    typedef HRESULT(STDMETHODCALLTYPE *tCreateSwapChain)(IDXGIFactory *, IUnknown *, DXGI_SWAP_CHAIN_DESC *,
        IDXGISwapChain **);
    typedef HRESULT(STDMETHODCALLTYPE *tCreateSwapChainForHwnd)(IDXGIFactory2 *, IUnknown *, HWND,
        const DXGI_SWAP_CHAIN_DESC1 *, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *, IDXGIOutput *, IDXGISwapChain1 **);
    typedef HRESULT(STDMETHODCALLTYPE *tPresent)(IDXGISwapChain *, UINT, UINT);
    typedef HRESULT(STDMETHODCALLTYPE *tPresent1)(IDXGISwapChain1 *, UINT, UINT, const DXGI_PRESENT_PARAMETERS *);
    typedef HRESULT(STDMETHODCALLTYPE *tResizeBuffers)(IDXGISwapChain *, UINT, UINT, UINT, DXGI_FORMAT, UINT);

    tCreateTexture2D oCreateTexture2D = nullptr;
    tRSSetViewports oRSSetViewports = nullptr;
    tCreateSwapChain oCreateSwapChain = nullptr;
    tCreateSwapChainForHwnd oCreateSwapChainForHwnd = nullptr;
    tPresent oPresent = nullptr;
    tPresent1 oPresent1 = nullptr;
    tResizeBuffers oResizeBuffers = nullptr;

    // DXGI may implement one entry point through another (CreateSwapChain through
    // CreateSwapChainForHwnd, Present1 through Present); only the outermost call is handled.
    thread_local int t_CreateSwapChainDepth = 0;
    thread_local int t_PresentDepth = 0;

    void *VTableEntry(void *object, int index)
    {
        return (*reinterpret_cast<void ***>(object))[index];
    }

    template <typename T>
    void Hook(void *object, int index, void *detour, T &original, const char *name)
    {
        if (original)
            return;
        MH_Initialize(); // MH_ERROR_ALREADY_INITIALIZED is fine, a real failure shows up in MH_CreateHook
        void *target = VTableEntry(object, index);
        MH_STATUS status = MH_CreateHook(target, detour, reinterpret_cast<void **>(&original));
        if (status == MH_OK)
            status = MH_EnableHook(target);
        std::cout << "[D3D11] hook " << name << ": " << MH_StatusToString(status) << "\n";
    }

    bool IsGameDevice(IUnknown *deviceUnknown)
    {
        if (!deviceUnknown || !g_Device)
            return false;
        if (deviceUnknown == g_Device)
            return true;
        ID3D11Device *device = nullptr;
        if (FAILED(deviceUnknown->QueryInterface(__uuidof(ID3D11Device), (void **)&device)))
            return false;
        device->Release();
        return device == g_Device;
    }

    HRESULT STDMETHODCALLTYPE hkCreateTexture2D(ID3D11Device *device, const D3D11_TEXTURE2D_DESC *desc,
        const D3D11_SUBRESOURCE_DATA *initialData, ID3D11Texture2D **texture)
    {
        if (device != g_Device || !desc)
            return oCreateTexture2D(device, desc, initialData, texture);

        D3D11_TEXTURE2D_DESC newDesc = *desc;
        if (CreateTexture2D_Pre_Callback)
            CreateTexture2D_Pre_Callback(newDesc);

        HRESULT hr = oCreateTexture2D(device, &newDesc, initialData, texture);

        if (SUCCEEDED(hr) && texture && *texture && CreateTexture2D_Post_Callback)
            CreateTexture2D_Post_Callback(*texture, newDesc);
        return hr;
    }

    void STDMETHODCALLTYPE hkRSSetViewports(ID3D11DeviceContext *context, UINT count, const D3D11_VIEWPORT *viewports)
    {
        if (context != g_Context || !viewports || count == 0 || count > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE
            || !SetViewports_Callback)
            return oRSSetViewports(context, count, viewports);

        D3D11_VIEWPORT newViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        memcpy(newViewports, viewports, count * sizeof(D3D11_VIEWPORT));
        SetViewports_Callback(count, newViewports);
        oRSSetViewports(context, count, newViewports);
    }

    HRESULT STDMETHODCALLTYPE hkResizeBuffers(IDXGISwapChain *swapChain, UINT bufferCount, UINT width, UINT height,
        DXGI_FORMAT format, UINT flags)
    {
        if (swapChain == g_SwapChain && SwapChainSize_Callback)
            SwapChainSize_Callback(width, height);
        std::cout << "[D3D11] ResizeBuffers " << width << "x" << height << "\n";
        return oResizeBuffers(swapChain, bufferCount, width, height, format, flags);
    }

    HRESULT STDMETHODCALLTYPE hkPresent(IDXGISwapChain *swapChain, UINT syncInterval, UINT flags)
    {
        const bool handle = swapChain == g_SwapChain && t_PresentDepth == 0 && !(flags & DXGI_PRESENT_TEST);
        ++t_PresentDepth;
        if (handle && Present_Pre_Callback)
            Present_Pre_Callback(swapChain);
        HRESULT hr = oPresent(swapChain, syncInterval, flags);
        if (handle && Present_Post_Callback)
            Present_Post_Callback(swapChain);
        --t_PresentDepth;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE hkPresent1(IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
        const DXGI_PRESENT_PARAMETERS *parameters)
    {
        const bool handle = swapChain == g_SwapChain && t_PresentDepth == 0 && !(flags & DXGI_PRESENT_TEST);
        ++t_PresentDepth;
        if (handle && Present_Pre_Callback)
            Present_Pre_Callback(swapChain);
        HRESULT hr = oPresent1(swapChain, syncInterval, flags, parameters);
        if (handle && Present_Post_Callback)
            Present_Post_Callback(swapChain);
        --t_PresentDepth;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE hkCreateSwapChain(IDXGIFactory *factory, IUnknown *device, DXGI_SWAP_CHAIN_DESC *desc,
        IDXGISwapChain **swapChain)
    {
        if (t_CreateSwapChainDepth > 0 || !desc || !IsGameDevice(device))
            return oCreateSwapChain(factory, device, desc, swapChain);

        DXGI_SWAP_CHAIN_DESC newDesc = *desc;
        if (SwapChainSize_Callback)
            SwapChainSize_Callback(newDesc.BufferDesc.Width, newDesc.BufferDesc.Height);

        ++t_CreateSwapChainDepth;
        HRESULT hr = oCreateSwapChain(factory, device, &newDesc, swapChain);
        --t_CreateSwapChainDepth;

        std::cout << "[D3D11] CreateSwapChain " << newDesc.BufferDesc.Width << "x" << newDesc.BufferDesc.Height
                  << " format=" << newDesc.BufferDesc.Format << " samples=" << newDesc.SampleDesc.Count
                  << " swapEffect=" << newDesc.SwapEffect << " hr=0x" << std::hex << hr << std::dec << "\n";
        if (SUCCEEDED(hr) && swapChain && *swapChain)
            OnSwapChainCreated(*swapChain);
        return hr;
    }

    HRESULT STDMETHODCALLTYPE hkCreateSwapChainForHwnd(IDXGIFactory2 *factory, IUnknown *device, HWND hwnd,
        const DXGI_SWAP_CHAIN_DESC1 *desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC *fullscreenDesc, IDXGIOutput *output,
        IDXGISwapChain1 **swapChain)
    {
        if (t_CreateSwapChainDepth > 0 || !desc || !IsGameDevice(device))
            return oCreateSwapChainForHwnd(factory, device, hwnd, desc, fullscreenDesc, output, swapChain);

        DXGI_SWAP_CHAIN_DESC1 newDesc = *desc;
        if (SwapChainSize_Callback)
            SwapChainSize_Callback(newDesc.Width, newDesc.Height);

        ++t_CreateSwapChainDepth;
        HRESULT hr = oCreateSwapChainForHwnd(factory, device, hwnd, &newDesc, fullscreenDesc, output, swapChain);
        --t_CreateSwapChainDepth;

        std::cout << "[D3D11] CreateSwapChainForHwnd " << newDesc.Width << "x" << newDesc.Height
                  << " format=" << newDesc.Format << " samples=" << newDesc.SampleDesc.Count
                  << " swapEffect=" << newDesc.SwapEffect << " hr=0x" << std::hex << hr << std::dec << "\n";
        if (SUCCEEDED(hr) && swapChain && *swapChain)
            OnSwapChainCreated(*swapChain);
        return hr;
    }

    void HookFactory(ID3D11Device *device)
    {
        IDXGIDevice *dxgiDevice = nullptr;
        IDXGIAdapter *adapter = nullptr;
        IDXGIFactory *factory = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), (void **)&dxgiDevice)) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), (void **)&factory)))
        {
            Hook(factory, kFactory_CreateSwapChain, (void *)&hkCreateSwapChain, oCreateSwapChain,
                "IDXGIFactory::CreateSwapChain");

            IDXGIFactory2 *factory2 = nullptr;
            if (SUCCEEDED(factory->QueryInterface(__uuidof(IDXGIFactory2), (void **)&factory2)))
            {
                Hook(factory2, kFactory2_CreateSwapChainForHwnd, (void *)&hkCreateSwapChainForHwnd,
                    oCreateSwapChainForHwnd, "IDXGIFactory2::CreateSwapChainForHwnd");
                factory2->Release();
            }
        }
        else
            std::cout << "[D3D11] could not get the DXGI factory of the device\n";

        if (factory)
            factory->Release();
        if (adapter)
            adapter->Release();
        if (dxgiDevice)
            dxgiDevice->Release();
    }
}

void D3D11Hooks::OnDeviceCreated(ID3D11Device *device, ID3D11DeviceContext *context)
{
    g_Device = device;
    if (context)
        g_Context = context;
    else
    {
        device->GetImmediateContext(&g_Context);
        g_Context->Release(); // the device keeps its immediate context alive
    }

    std::cout << "[D3D11] device created, feature level 0x" << std::hex << device->GetFeatureLevel() << std::dec << "\n";

    Hook(device, kDevice_CreateTexture2D, (void *)&hkCreateTexture2D, oCreateTexture2D, "ID3D11Device::CreateTexture2D");
    Hook(g_Context, kContext_RSSetViewports, (void *)&hkRSSetViewports, oRSSetViewports,
        "ID3D11DeviceContext::RSSetViewports");
    HookFactory(device);
}

void D3D11Hooks::OnSwapChainCreated(IDXGISwapChain *swapChain)
{
    g_SwapChain = swapChain;

    Hook(swapChain, kSwapChain_Present, (void *)&hkPresent, oPresent, "IDXGISwapChain::Present");
    Hook(swapChain, kSwapChain_ResizeBuffers, (void *)&hkResizeBuffers, oResizeBuffers, "IDXGISwapChain::ResizeBuffers");

    IDXGISwapChain1 *swapChain1 = nullptr;
    if (SUCCEEDED(swapChain->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&swapChain1)))
    {
        Hook(swapChain1, kSwapChain1_Present1, (void *)&hkPresent1, oPresent1, "IDXGISwapChain1::Present1");
        swapChain1->Release();
    }
}
