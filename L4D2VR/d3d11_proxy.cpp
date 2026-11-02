// Exports of d3d11.dll (see d3d11.def), forwarded to the system's d3d11.dll. Never link
// d3d11.lib: since this module is called d3d11.dll, an import of d3d11.dll would resolve to it.
#include <Windows.h>
#include <intrin.h>
#include <iostream>
#include "d3d11_hooks.h"

static HMODULE SystemD3D11()
{
    static HMODULE module = []() -> HMODULE
    {
        wchar_t path[MAX_PATH];
        UINT length = GetSystemDirectoryW(path, MAX_PATH);
        if (length == 0 || length >= MAX_PATH - 11)
            return nullptr;
        wcscat_s(path, L"\\d3d11.dll");
        return LoadLibraryW(path);
    }();
    return module;
}

template <typename T>
static T SystemProc(const char *name)
{
    HMODULE module = SystemD3D11();
    T proc = module ? (T)GetProcAddress(module, name) : nullptr;
    if (!proc)
        std::cout << "[D3D11] system d3d11.dll: " << name << " not found\n";
    return proc;
}

// Only the device of the renderer is the game's; other callers (e.g. Qt) are passed through.
static bool CalledByShaderApi(void *returnAddress)
{
    HMODULE caller = nullptr;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)returnAddress, &caller);
    return caller && caller == GetModuleHandleA("shaderapidx11.dll");
}

extern "C" HRESULT WINAPI Proxy_D3D11CreateDevice(IDXGIAdapter *adapter, D3D_DRIVER_TYPE driverType,
    HMODULE software, UINT flags, const D3D_FEATURE_LEVEL *featureLevels, UINT numFeatureLevels, UINT sdkVersion,
    ID3D11Device **device, D3D_FEATURE_LEVEL *featureLevel, ID3D11DeviceContext **immediateContext)
{
    static auto original = SystemProc<PFN_D3D11_CREATE_DEVICE>("D3D11CreateDevice");
    if (!original)
        return E_FAIL;

    HRESULT hr = original(adapter, driverType, software, flags, featureLevels, numFeatureLevels, sdkVersion,
        device, featureLevel, immediateContext);

    const bool game = CalledByShaderApi(_ReturnAddress());
    std::cout << "[D3D11] D3D11CreateDevice from " << (game ? "shaderapidx11" : "other module") << ", hr=0x"
              << std::hex << hr << std::dec << (device ? "" : " (no device requested)") << "\n";
    if (game && SUCCEEDED(hr) && device && *device)
        D3D11Hooks::OnDeviceCreated(*device, immediateContext ? *immediateContext : nullptr);
    return hr;
}

extern "C" HRESULT WINAPI Proxy_D3D11CreateDeviceAndSwapChain(IDXGIAdapter *adapter, D3D_DRIVER_TYPE driverType,
    HMODULE software, UINT flags, const D3D_FEATURE_LEVEL *featureLevels, UINT numFeatureLevels, UINT sdkVersion,
    const DXGI_SWAP_CHAIN_DESC *swapChainDesc, IDXGISwapChain **swapChain, ID3D11Device **device,
    D3D_FEATURE_LEVEL *featureLevel, ID3D11DeviceContext **immediateContext)
{
    static auto original = SystemProc<PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN>("D3D11CreateDeviceAndSwapChain");
    if (!original)
        return E_FAIL;

    const bool game = CalledByShaderApi(_ReturnAddress());
    DXGI_SWAP_CHAIN_DESC newDesc;
    if (game && swapChainDesc)
    {
        newDesc = *swapChainDesc;
        if (D3D11Hooks::SwapChainSize_Callback)
            D3D11Hooks::SwapChainSize_Callback(newDesc.BufferDesc.Width, newDesc.BufferDesc.Height);
        swapChainDesc = &newDesc;
    }

    HRESULT hr = original(adapter, driverType, software, flags, featureLevels, numFeatureLevels, sdkVersion,
        swapChainDesc, swapChain, device, featureLevel, immediateContext);

    std::cout << "[D3D11] D3D11CreateDeviceAndSwapChain from " << (game ? "shaderapidx11" : "other module")
              << ", hr=0x" << std::hex << hr << std::dec << "\n";
    if (game && SUCCEEDED(hr) && device && *device)
    {
        D3D11Hooks::OnDeviceCreated(*device, immediateContext ? *immediateContext : nullptr);
        if (swapChain && *swapChain)
            D3D11Hooks::OnSwapChainCreated(*swapChain);
    }
    return hr;
}
