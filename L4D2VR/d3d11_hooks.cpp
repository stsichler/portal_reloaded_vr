#include "d3d11_hooks.h"
#include "MinHook.h"
#include <iostream>
#ifdef L4D2VR_DIAGNOSTICS
#include <fstream>
#include <string>
#endif

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

#ifdef L4D2VR_DIAGNOSTICS
    // Diagnostic frame trace: F11 writes the D3D11 calls of the next frames to l4d2vr_frame_N.txt and
    // what each of them presents to l4d2vr_frame_N_K.bmp.
    constexpr int kTraceFrames = 3;
    constexpr int kContext_PSSetShaderResources = 8;
    constexpr int kContext_DrawIndexed = 12;
    constexpr int kContext_Draw = 13;
    constexpr int kContext_DrawIndexedInstanced = 20;
    constexpr int kContext_DrawInstanced = 21;
    constexpr int kContext_OMSetRenderTargets = 33;
    constexpr int kContext_OMSetDepthStencilState = 36;
    constexpr int kContext_RSSetScissorRects = 45;
    constexpr int kContext_CopySubresourceRegion = 46;
    constexpr int kContext_CopyResource = 47;
    constexpr int kContext_ClearRenderTargetView = 50;
    constexpr int kContext_ClearDepthStencilView = 53;
    constexpr int kContext_ResolveSubresource = 57;

    typedef void(STDMETHODCALLTYPE *tPSSetShaderResources)(ID3D11DeviceContext *, UINT, UINT, ID3D11ShaderResourceView *const *);
    typedef void(STDMETHODCALLTYPE *tDrawIndexed)(ID3D11DeviceContext *, UINT, UINT, INT);
    typedef void(STDMETHODCALLTYPE *tDraw)(ID3D11DeviceContext *, UINT, UINT);
    typedef void(STDMETHODCALLTYPE *tDrawIndexedInstanced)(ID3D11DeviceContext *, UINT, UINT, UINT, INT, UINT);
    typedef void(STDMETHODCALLTYPE *tDrawInstanced)(ID3D11DeviceContext *, UINT, UINT, UINT, UINT);
    typedef void(STDMETHODCALLTYPE *tOMSetRenderTargets)(ID3D11DeviceContext *, UINT, ID3D11RenderTargetView *const *, ID3D11DepthStencilView *);
    typedef void(STDMETHODCALLTYPE *tOMSetDepthStencilState)(ID3D11DeviceContext *, ID3D11DepthStencilState *, UINT);
    typedef void(STDMETHODCALLTYPE *tRSSetScissorRects)(ID3D11DeviceContext *, UINT, const D3D11_RECT *);
    typedef void(STDMETHODCALLTYPE *tCopySubresourceRegion)(ID3D11DeviceContext *, ID3D11Resource *, UINT, UINT, UINT, UINT, ID3D11Resource *, UINT, const D3D11_BOX *);
    typedef void(STDMETHODCALLTYPE *tCopyResource)(ID3D11DeviceContext *, ID3D11Resource *, ID3D11Resource *);
    typedef void(STDMETHODCALLTYPE *tClearRenderTargetView)(ID3D11DeviceContext *, ID3D11RenderTargetView *, const FLOAT[4]);
    typedef void(STDMETHODCALLTYPE *tClearDepthStencilView)(ID3D11DeviceContext *, ID3D11DepthStencilView *, UINT, FLOAT, UINT8);
    typedef void(STDMETHODCALLTYPE *tResolveSubresource)(ID3D11DeviceContext *, ID3D11Resource *, UINT, ID3D11Resource *, UINT, DXGI_FORMAT);

    tPSSetShaderResources oPSSetShaderResources = nullptr;
    tDrawIndexed oDrawIndexed = nullptr;
    tDraw oDraw = nullptr;
    tDrawIndexedInstanced oDrawIndexedInstanced = nullptr;
    tDrawInstanced oDrawInstanced = nullptr;
    tOMSetRenderTargets oOMSetRenderTargets = nullptr;
    tOMSetDepthStencilState oOMSetDepthStencilState = nullptr;
    tRSSetScissorRects oRSSetScissorRects = nullptr;
    tCopySubresourceRegion oCopySubresourceRegion = nullptr;
    tCopyResource oCopyResource = nullptr;
    tClearRenderTargetView oClearRenderTargetView = nullptr;
    tClearDepthStencilView oClearDepthStencilView = nullptr;
    tResolveSubresource oResolveSubresource = nullptr;

    bool g_Trace = false;
    int g_TraceIndex = 0;
    int g_TraceFrame = 0;
    std::ofstream g_TraceFile;
    unsigned g_TraceDraws = 0;
    ID3D11DepthStencilState *g_TraceDepthState = nullptr;
    UINT g_TraceStencilRef = 0;
    D3D11_VIEWPORT g_TraceViewport = {};
    D3D11_RECT g_TraceScissor = {};

    bool TraceActive(ID3D11DeviceContext *context) { return g_Trace && context == g_Context; }

    void TraceFlushDraws()
    {
        if (g_TraceDraws)
            g_TraceFile << "    " << g_TraceDraws << " draws\n";
        g_TraceDraws = 0;
    }

    // Size, format and identity of a resource.
    std::string TraceResourceName(ID3D11Resource *resource)
    {
        if (!resource)
            return "null";
        char pointer[32];
        sprintf_s(pointer, "%p", resource);
        std::string name = pointer;
        D3D11_RESOURCE_DIMENSION dimension;
        resource->GetType(&dimension);
        if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
        {
            D3D11_TEXTURE2D_DESC desc;
            static_cast<ID3D11Texture2D *>(resource)->GetDesc(&desc);
            name += " " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " fmt " + std::to_string(desc.Format);
            if (desc.SampleDesc.Count > 1)
                name += " msaa " + std::to_string(desc.SampleDesc.Count);
        }
        ID3D11Texture2D *backBuffer = nullptr;
        if (g_SwapChain && SUCCEEDED(g_SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer)))
        {
            if (backBuffer == resource)
                name += " BACKBUFFER";
            backBuffer->Release();
        }
        return name;
    }

    std::string TraceBackBufferName(IDXGISwapChain *swapChain)
    {
        ID3D11Texture2D *backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer)))
            return "?";
        std::string name = TraceResourceName(backBuffer);
        backBuffer->Release();
        return name;
    }

    std::string TraceViewName(ID3D11View *view)
    {
        if (!view)
            return "null";
        ID3D11Resource *resource = nullptr;
        view->GetResource(&resource);
        std::string name = TraceResourceName(resource);
        if (resource)
            resource->Release();
        return name;
    }

    void TraceStart()
    {
        if (g_Trace || !(GetAsyncKeyState(VK_F11) & 1))
            return;
        const std::string fileName = "l4d2vr_frame_" + std::to_string(++g_TraceIndex) + ".txt";
        g_TraceFile.open(fileName);
        g_TraceDraws = 0;
        g_TraceDepthState = nullptr;
        g_TraceStencilRef = 0;
        g_TraceViewport = {};
        g_TraceScissor = {};
        g_TraceFrame = 0;
        g_Trace = true;
        std::cout << "[D3D11] tracing " << kTraceFrames << " frames to " << fileName << "\n";
    }

    // Writes the back buffer as a bitmap. Assumes 32 bits per pixel, BGRA.
    void TraceScreenshot(IDXGISwapChain *swapChain, const std::string &fileName)
    {
        ID3D11Texture2D *backBuffer = nullptr;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void **)&backBuffer)))
            return;
        D3D11_TEXTURE2D_DESC desc;
        backBuffer->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ID3D11Texture2D *staging = nullptr;
        if (desc.SampleDesc.Count == 1 && SUCCEEDED(g_Device->CreateTexture2D(&desc, nullptr, &staging)))
        {
            g_Context->CopyResource(staging, backBuffer);
            D3D11_MAPPED_SUBRESOURCE mapped;
            if (SUCCEEDED(g_Context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped)))
            {
                BITMAPINFOHEADER info = {};
                info.biSize = sizeof(info);
                info.biWidth = desc.Width;
                info.biHeight = -static_cast<LONG>(desc.Height); // top-down
                info.biPlanes = 1;
                info.biBitCount = 32;
                info.biCompression = BI_RGB;
                BITMAPFILEHEADER file = {};
                file.bfType = 0x4D42;
                file.bfOffBits = sizeof(file) + sizeof(info);
                file.bfSize = file.bfOffBits + desc.Width * 4 * desc.Height;
                std::ofstream bitmap(fileName, std::ios::binary);
                bitmap.write(reinterpret_cast<const char *>(&file), sizeof(file));
                bitmap.write(reinterpret_cast<const char *>(&info), sizeof(info));
                for (UINT y = 0; y < desc.Height; ++y)
                    bitmap.write(static_cast<const char *>(mapped.pData) + y * mapped.RowPitch, desc.Width * 4);
                g_Context->Unmap(staging, 0);
            }
            staging->Release();
        }
        backBuffer->Release();
    }

    void TraceEnd(IDXGISwapChain *swapChain)
    {
        if (!g_Trace)
            return;
        TraceFlushDraws();
        g_TraceFile << "=== present " << g_TraceFrame << " of back buffer " << TraceBackBufferName(swapChain) << "\n";
        TraceScreenshot(swapChain, "l4d2vr_frame_" + std::to_string(g_TraceIndex) + "_" + std::to_string(g_TraceFrame) + ".bmp");
        if (++g_TraceFrame < kTraceFrames)
            return;
        g_TraceFile.close();
        g_Trace = false;
    }

    void STDMETHODCALLTYPE hkPSSetShaderResources(ID3D11DeviceContext *context, UINT start, UINT count, ID3D11ShaderResourceView *const *views)
    {
        if (TraceActive(context) && views)
        {
            for (UINT i = 0; i < count; ++i)
            {
                ID3D11Resource *resource = nullptr;
                if (views[i])
                    views[i]->GetResource(&resource);
                if (!resource)
                    continue;
                D3D11_RESOURCE_DIMENSION dimension;
                resource->GetType(&dimension);
                if (dimension == D3D11_RESOURCE_DIMENSION_TEXTURE2D)
                {
                    D3D11_TEXTURE2D_DESC desc;
                    static_cast<ID3D11Texture2D *>(resource)->GetDesc(&desc);
                    if (desc.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL))
                    {
                        TraceFlushDraws();
                        g_TraceFile << "  PS t" << start + i << " = " << TraceResourceName(resource) << "\n";
                    }
                }
                resource->Release();
            }
        }
        oPSSetShaderResources(context, start, count, views);
    }

    void STDMETHODCALLTYPE hkDrawIndexed(ID3D11DeviceContext *context, UINT indexCount, UINT startIndex, INT baseVertex)
    {
        if (TraceActive(context))
            ++g_TraceDraws;
        oDrawIndexed(context, indexCount, startIndex, baseVertex);
    }

    void STDMETHODCALLTYPE hkDraw(ID3D11DeviceContext *context, UINT vertexCount, UINT startVertex)
    {
        if (TraceActive(context))
            ++g_TraceDraws;
        oDraw(context, vertexCount, startVertex);
    }

    void STDMETHODCALLTYPE hkDrawIndexedInstanced(ID3D11DeviceContext *context, UINT indexCount, UINT instanceCount, UINT startIndex, INT baseVertex, UINT startInstance)
    {
        if (TraceActive(context))
            ++g_TraceDraws;
        oDrawIndexedInstanced(context, indexCount, instanceCount, startIndex, baseVertex, startInstance);
    }

    void STDMETHODCALLTYPE hkDrawInstanced(ID3D11DeviceContext *context, UINT vertexCount, UINT instanceCount, UINT startVertex, UINT startInstance)
    {
        if (TraceActive(context))
            ++g_TraceDraws;
        oDrawInstanced(context, vertexCount, instanceCount, startVertex, startInstance);
    }

    void STDMETHODCALLTYPE hkOMSetRenderTargets(ID3D11DeviceContext *context, UINT count, ID3D11RenderTargetView *const *targets, ID3D11DepthStencilView *depth)
    {
        if (TraceActive(context))
        {
            TraceFlushDraws();
            g_TraceFile << "RT " << TraceViewName(count && targets ? targets[0] : nullptr) << " | DS " << TraceViewName(depth) << "\n";
        }
        oOMSetRenderTargets(context, count, targets, depth);
    }

    void STDMETHODCALLTYPE hkOMSetDepthStencilState(ID3D11DeviceContext *context, ID3D11DepthStencilState *state, UINT stencilRef)
    {
        if (TraceActive(context) && (state != g_TraceDepthState || stencilRef != g_TraceStencilRef))
        {
            g_TraceDepthState = state;
            g_TraceStencilRef = stencilRef;
            TraceFlushDraws();
            D3D11_DEPTH_STENCIL_DESC desc = {};
            if (state)
                state->GetDesc(&desc);
            g_TraceFile << "  depth " << desc.DepthEnable << " write " << desc.DepthWriteMask << " func " << desc.DepthFunc
                        << " stencil " << desc.StencilEnable;
            if (desc.StencilEnable)
                g_TraceFile << " ref " << stencilRef << " func " << desc.FrontFace.StencilFunc << " pass " << desc.FrontFace.StencilPassOp
                            << " fail " << desc.FrontFace.StencilFailOp << " zfail " << desc.FrontFace.StencilDepthFailOp
                            << " read " << (int)desc.StencilReadMask << " write " << (int)desc.StencilWriteMask;
            g_TraceFile << "\n";
        }
        oOMSetDepthStencilState(context, state, stencilRef);
    }

    void STDMETHODCALLTYPE hkRSSetScissorRects(ID3D11DeviceContext *context, UINT count, const D3D11_RECT *rects)
    {
        if (TraceActive(context) && count && rects && memcmp(rects, &g_TraceScissor, sizeof(D3D11_RECT)))
        {
            g_TraceScissor = rects[0];
            TraceFlushDraws();
            g_TraceFile << "  scissor " << rects[0].left << "," << rects[0].top << " - " << rects[0].right << "," << rects[0].bottom << "\n";
        }
        oRSSetScissorRects(context, count, rects);
    }

    void STDMETHODCALLTYPE hkCopySubresourceRegion(ID3D11DeviceContext *context, ID3D11Resource *destination, UINT destinationSub, UINT x, UINT y, UINT z, ID3D11Resource *source, UINT sourceSub, const D3D11_BOX *box)
    {
        if (TraceActive(context))
        {
            TraceFlushDraws();
            g_TraceFile << "  COPY " << TraceResourceName(source) << " -> " << TraceResourceName(destination) << " at " << x << "," << y;
            if (box)
                g_TraceFile << " box " << box->left << "," << box->top << " - " << box->right << "," << box->bottom;
            g_TraceFile << "\n";
        }
        oCopySubresourceRegion(context, destination, destinationSub, x, y, z, source, sourceSub, box);
    }

    void STDMETHODCALLTYPE hkCopyResource(ID3D11DeviceContext *context, ID3D11Resource *destination, ID3D11Resource *source)
    {
        if (TraceActive(context))
        {
            TraceFlushDraws();
            g_TraceFile << "  COPY " << TraceResourceName(source) << " -> " << TraceResourceName(destination) << "\n";
        }
        oCopyResource(context, destination, source);
    }

    void STDMETHODCALLTYPE hkClearRenderTargetView(ID3D11DeviceContext *context, ID3D11RenderTargetView *target, const FLOAT color[4])
    {
        if (TraceActive(context))
        {
            TraceFlushDraws();
            g_TraceFile << "  CLEAR color " << TraceViewName(target) << " to " << color[0] << "," << color[1] << "," << color[2] << "," << color[3] << "\n";
        }
        oClearRenderTargetView(context, target, color);
    }

    void STDMETHODCALLTYPE hkClearDepthStencilView(ID3D11DeviceContext *context, ID3D11DepthStencilView *view, UINT flags, FLOAT depth, UINT8 stencil)
    {
        if (TraceActive(context))
        {
            TraceFlushDraws();
            g_TraceFile << "  CLEAR depth " << TraceViewName(view) << " flags " << flags << " to " << depth << " stencil " << (int)stencil << "\n";
        }
        oClearDepthStencilView(context, view, flags, depth, stencil);
    }

    void STDMETHODCALLTYPE hkResolveSubresource(ID3D11DeviceContext *context, ID3D11Resource *destination, UINT destinationSub, ID3D11Resource *source, UINT sourceSub, DXGI_FORMAT format)
    {
        if (TraceActive(context))
        {
            TraceFlushDraws();
            g_TraceFile << "  RESOLVE " << TraceResourceName(source) << " -> " << TraceResourceName(destination) << "\n";
        }
        oResolveSubresource(context, destination, destinationSub, source, sourceSub, format);
    }
#else
    void TraceStart() {}
    void TraceEnd(IDXGISwapChain *) {}
#endif

    void STDMETHODCALLTYPE hkRSSetViewports(ID3D11DeviceContext *context, UINT count, const D3D11_VIEWPORT *viewports)
    {
        if (context != g_Context || !viewports || count == 0 || count > D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE
            || !SetViewports_Callback)
            return oRSSetViewports(context, count, viewports);

        D3D11_VIEWPORT newViewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        memcpy(newViewports, viewports, count * sizeof(D3D11_VIEWPORT));
        SetViewports_Callback(count, newViewports);
#ifdef L4D2VR_DIAGNOSTICS
        if (TraceActive(context) && memcmp(&newViewports[0], &g_TraceViewport, sizeof(D3D11_VIEWPORT)))
        {
            g_TraceViewport = newViewports[0];
            TraceFlushDraws();
            g_TraceFile << "  viewport " << g_TraceViewport.TopLeftX << "," << g_TraceViewport.TopLeftY << " "
                        << g_TraceViewport.Width << "x" << g_TraceViewport.Height << "\n";
        }
#endif
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
        if (handle)
            TraceEnd(swapChain);
        if (handle && Present_Pre_Callback)
            Present_Pre_Callback(swapChain);
        HRESULT hr = oPresent(swapChain, syncInterval, flags);
        if (handle && Present_Post_Callback)
            Present_Post_Callback(swapChain);
        if (handle)
            TraceStart();
        --t_PresentDepth;
        return hr;
    }

    HRESULT STDMETHODCALLTYPE hkPresent1(IDXGISwapChain1 *swapChain, UINT syncInterval, UINT flags,
        const DXGI_PRESENT_PARAMETERS *parameters)
    {
        const bool handle = swapChain == g_SwapChain && t_PresentDepth == 0 && !(flags & DXGI_PRESENT_TEST);
        ++t_PresentDepth;
        if (handle)
            TraceEnd(swapChain);
        if (handle && Present_Pre_Callback)
            Present_Pre_Callback(swapChain);
        HRESULT hr = oPresent1(swapChain, syncInterval, flags, parameters);
        if (handle && Present_Post_Callback)
            Present_Post_Callback(swapChain);
        if (handle)
            TraceStart();
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
#ifdef L4D2VR_DIAGNOSTICS
    Hook(g_Context, kContext_PSSetShaderResources, (void *)&hkPSSetShaderResources, oPSSetShaderResources, "PSSetShaderResources");
    Hook(g_Context, kContext_DrawIndexed, (void *)&hkDrawIndexed, oDrawIndexed, "DrawIndexed");
    Hook(g_Context, kContext_Draw, (void *)&hkDraw, oDraw, "Draw");
    Hook(g_Context, kContext_DrawIndexedInstanced, (void *)&hkDrawIndexedInstanced, oDrawIndexedInstanced, "DrawIndexedInstanced");
    Hook(g_Context, kContext_DrawInstanced, (void *)&hkDrawInstanced, oDrawInstanced, "DrawInstanced");
    Hook(g_Context, kContext_OMSetRenderTargets, (void *)&hkOMSetRenderTargets, oOMSetRenderTargets, "OMSetRenderTargets");
    Hook(g_Context, kContext_OMSetDepthStencilState, (void *)&hkOMSetDepthStencilState, oOMSetDepthStencilState, "OMSetDepthStencilState");
    Hook(g_Context, kContext_RSSetScissorRects, (void *)&hkRSSetScissorRects, oRSSetScissorRects, "RSSetScissorRects");
    Hook(g_Context, kContext_CopySubresourceRegion, (void *)&hkCopySubresourceRegion, oCopySubresourceRegion, "CopySubresourceRegion");
    Hook(g_Context, kContext_CopyResource, (void *)&hkCopyResource, oCopyResource, "CopyResource");
    Hook(g_Context, kContext_ClearRenderTargetView, (void *)&hkClearRenderTargetView, oClearRenderTargetView, "ClearRenderTargetView");
    Hook(g_Context, kContext_ClearDepthStencilView, (void *)&hkClearDepthStencilView, oClearDepthStencilView, "ClearDepthStencilView");
    Hook(g_Context, kContext_ResolveSubresource, (void *)&hkResolveSubresource, oResolveSubresource, "ResolveSubresource");
#endif
    HookFactory(device);
}

void D3D11Hooks::TraceNote(const char *note)
{
#ifdef L4D2VR_DIAGNOSTICS
    if (!g_Trace)
        return;
    TraceFlushDraws();
    g_TraceFile << "=== " << note << "\n";
#endif
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
