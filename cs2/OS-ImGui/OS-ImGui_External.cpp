#include "OS-ImGui_External.h"
#include "..\game\MenuConfig.h"

#ifndef DXGI_SWAP_EFFECT_FLIP_DISCARD
#define DXGI_SWAP_EFFECT_FLIP_DISCARD ((DXGI_SWAP_EFFECT)4)
#endif

// Tearing support (Windows 10 2004+)
// 注意: SwapChain flag 和 Present flag 是不同枚举,值不同!
//   DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING = 0x800 (2048)
//   DXGI_PRESENT_ALLOW_TEARING         = 0x200 (512)
#ifndef DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING
#define DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING 0x800
#endif
#ifndef DXGI_PRESENT_ALLOW_TEARING
#define DXGI_PRESENT_ALLOW_TEARING 0x200
#endif

// IDXGIFactory2 IID: {50c83a1c-e072-4c48-8370-863ae4524f5a}
static const IID IID_MyDXGIFactory2 =
{ 0x50c83a1c, 0xe072, 0x4c48, { 0x83, 0x70, 0x86, 0x3a, 0xe4, 0x52, 0x4f, 0x5a } };

// DXGI_SWAP_CHAIN_DESC1 等价结构体（项目旧 SDK 无此定义）
struct MySwapChainDesc1 {
    UINT Width;
    UINT Height;
    UINT Format;
    BOOL Stereo;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT BufferUsage;
    UINT BufferCount;
    int Scaling;
    int SwapEffect;
    int AlphaMode;
    UINT Flags;
};

/****************************************************
* Copyright (C)	: Liv
* @file			: OS-ImGui_External.cpp
* @author		: Liv
* @email		: 1319923129@qq.com
* @version		: 1.0
* @date			: 2023/6/18	11:21
****************************************************/

// D3D11 Device
namespace OSImGui
{
#ifdef _CONSOLE
    // vtable 索引说明（IDXGIFactory2 继承链）:
    // IUnknown:     [0]QI [1]AddRef [2]Release
    // IDXGIObject:  [3]SetPrivateData [4]SetPrivateDataInterface [5]GetPrivateData [6]GetParent
    // IDXGIFactory: [7]EnumAdapters [8]MakeWindowAssociation [9]GetWindowAssociation [10]CreateSwapChain [11]CreateSoftwareAdapter
    // IDXGIFactory1:[12]EnumAdapters1 [13]IsCurrent
    // IDXGIFactory2:[14]IsWindowedStereoEnabled [15]CreateSwapChainForHwnd ...
    bool D3DDevice::CreateDeviceD3D(HWND hWnd)
    {
        // 直接尝试带 ALLOW_TEARING flag 创建 swap chain
        // DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING = 0x800
        g_AllowTearing = true;

        DXGI_SWAP_CHAIN_DESC sd;
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferDesc.RefreshRate.Denominator = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hWnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

        UINT createDeviceFlags = 0;
        D3D_FEATURE_LEVEL featureLevel;
        const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
        HRESULT res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        g_TearingHR = res;
        if (res == DXGI_ERROR_UNSUPPORTED)
            res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        // 如果带 ALLOW_TEARING flag 创建失败，回退到不带 flag 重试
        if (res != S_OK) {
            sd.Flags = 0;
            g_AllowTearing = false;
            res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
            if (res == DXGI_ERROR_UNSUPPORTED)
                res = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_WARP, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
        }
        if (res != S_OK)
            return false;

        IDXGIDevice1* pDxgiDevice1 = nullptr;
        if (SUCCEEDED(g_pd3dDevice->QueryInterface(__uuidof(IDXGIDevice1), (void**)&pDxgiDevice1))) {
            pDxgiDevice1->SetMaximumFrameLatency(1);
            pDxgiDevice1->Release();
        }

        CreateRenderTarget();
        return true;
    }

    void D3DDevice::CleanupDeviceD3D()
    {
        CleanupRenderTarget();
        if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
        if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
        if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
    }

    void D3DDevice::CreateRenderTarget()
    {
        ID3D11Texture2D* pBackBuffer;
        g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
        if (pBackBuffer == nullptr)
            return;
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }

    void D3DDevice::CleanupRenderTarget()
    {
        if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
    }
#endif
}

// OSImGui External
namespace OSImGui
{

    LRESULT WINAPI WndProc_External(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void OSImGui_External::NewWindow(std::string WindowName, Vec2 WindowSize, std::function<void()> CallBack)
    {
        if (!CallBack)
            throw OSException("CallBack is empty");
        if (WindowName.empty())
            Window.Name = "Window";

        Window.Name = WindowName;
        Window.wName = StringToWstring(Window.Name);
        Window.ClassName = "WindowClass";
        Window.wClassName = StringToWstring(Window.ClassName);
        Window.Size = WindowSize;
        
        Type = NEW;
        CallBackFn = CallBack;

        if (!CreateMyWindow())
            throw OSException("CreateMyWindow() call failed");

        try {
            InitImGui(g_Device.g_pd3dDevice, g_Device.g_pd3dDeviceContext);
        }
        catch (OSException& e)
        {
            throw e;
        }

        MainLoop();
    }

    void  OSImGui_External::AttachAnotherWindow(std::string DestWindowName, std::string DestWindowClassName, std::function<void()> CallBack)
    {
        if (!CallBack)
            throw OSException("CallBack is empty");
        if (DestWindowName.empty() && DestWindowClassName.empty())
            throw OSException("DestWindowName and DestWindowClassName are empty");

        Window.Name = "Window";
        Window.wName = StringToWstring(Window.Name);
        Window.ClassName = "WindowClass";
        Window.wClassName = StringToWstring(Window.ClassName);
        Window.BgColor = ImColor(0, 0, 0, 0);

        DestWindow.hWnd = FindWindowA(
            (DestWindowClassName.empty() ? NULL : DestWindowClassName.c_str()),
            (DestWindowName.empty() ? NULL : DestWindowName.c_str()));
        if (DestWindow.hWnd == NULL)
            throw OSException("DestWindow isn't exist");
        DestWindow.Name = DestWindowName;
        DestWindow.ClassName = DestWindowClassName;

        Type = ATTACH;
        CallBackFn = CallBack;

        if (!CreateMyWindow())
            throw OSException("CreateMyWindow() call failed");

        try {
            InitImGui(g_Device.g_pd3dDevice,g_Device.g_pd3dDeviceContext);
        }
        catch (OSException& e)
        {
            throw e;
        }

        MainLoop();
    }

    ID3D11Device* OSImGui_External::getDevice() {
        return g_Device.g_pd3dDevice;
    }

    bool OSImGui_External::PeekEndMessage()
    {
        MSG msg;
        while (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                return true;
        }
        return false;
    }

    void OSImGui_External::MainLoop()
    {
        using Clock = std::chrono::high_resolution_clock;
        auto frameStart = Clock::now();

        while (!EndFlag)
        {
            frameStart = Clock::now();

            if (PeekEndMessage())
                break;
            if (Type == ATTACH)
            {
                if (!UpdateWindowData())
                    break;
            }
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            this->CallBackFn();
            ImGui::Render();
            const float clear_color_with_alpha[4] = { Window.BgColor.Value.x, Window.BgColor.Value.y , Window.BgColor.Value.z, Window.BgColor.Value.w };
            g_Device.g_pd3dDeviceContext->OMSetRenderTargets(1, &g_Device.g_mainRenderTargetView, NULL);
            g_Device.g_pd3dDeviceContext->ClearRenderTargetView(g_Device.g_mainRenderTargetView, clear_color_with_alpha);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            // VSync 开启: Present(1,0) 锁刷新率
            // VSync 关闭: Present(0, ALLOW_TEARING) 突破 DWM 刷新率限制（需系统支持）
            //             不支持 tearing 时退化为 Present(0, 0)
            if (MenuConfig::VSync) {
                g_Device.g_pSwapChain->Present(1, 0);
            } else {
                UINT presentFlags = g_Device.g_AllowTearing ? DXGI_PRESENT_ALLOW_TEARING : 0;
                g_Device.g_pSwapChain->Present(0, presentFlags);
            }

            if (!MenuConfig::VSync && MenuConfig::MaxFrameRate > 0) {
                auto elapsed = Clock::now() - frameStart;
                auto target = std::chrono::microseconds(1000000 / MenuConfig::MaxFrameRate);
                auto remaining = target - elapsed;
                if (remaining.count() > 0) {
                    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
                    if (ms > 0) Sleep((DWORD)ms);
                }
            }
        }
        CleanImGui();
    }

    // Callback for EnumDisplayMonitors to collect monitor info
    struct EnumMonitorCtx {
        std::vector<MenuConfig::MonitorDesc>* list;
    };

    static BOOL CALLBACK EnumMonitorCallback(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM dwData) {
        auto* ctx = reinterpret_cast<EnumMonitorCtx*>(dwData);
        MONITORINFOEXW mi;
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(hMonitor, &mi);

        MenuConfig::MonitorDesc desc;
        desc.index = (int)ctx->list->size();
        desc.x = (int)lprcMonitor->left;
        desc.y = (int)lprcMonitor->top;
        desc.width = (int)(lprcMonitor->right - lprcMonitor->left);
        desc.height = (int)(lprcMonitor->bottom - lprcMonitor->top);

        // Build name: "Monitor N (WxH)" + [Primary] if primary
        char nameBuf[128];
        _snprintf_s(nameBuf, _TRUNCATE, "Monitor %d (%dx%d)%s",
            desc.index + 1, desc.width, desc.height,
            (mi.dwFlags & MONITORINFOF_PRIMARY) ? " [Primary]" : "");
        desc.name = nameBuf;

        ctx->list->push_back(desc);
        return TRUE;
    }

    bool OSImGui_External::CreateMyWindow()
    {
        // Enumerate all monitors
        MenuConfig::MonitorList.clear();
        EnumMonitorCtx ctx{ &MenuConfig::MonitorList };
        EnumDisplayMonitors(NULL, NULL, EnumMonitorCallback, (LPARAM)&ctx);

        WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc_External, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, Window.wClassName.c_str(), NULL };
        RegisterClassExW(&wc);
        if (Type == ATTACH)
        {

			Window.hWnd = CreateWindowExW(WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, Window.wClassName.c_str(), Window.wName.c_str(), WS_POPUP, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, NULL, NULL, GetModuleHandle(NULL), NULL);
			SetLayeredWindowAttributes(Window.hWnd, 0, 255, LWA_ALPHA);
        }
        else
        {
            Window.BgColor = IM_COL32_BLACK;

            // Select target monitor
            int monX = 0, monY = 0, monW = GetSystemMetrics(SM_CXSCREEN), monH = GetSystemMetrics(SM_CYSCREEN);
            if (!MenuConfig::MonitorList.empty()) {
                int idx = MenuConfig::MonitorIndex;
                if (idx < 0 || idx >= (int)MenuConfig::MonitorList.size())
                    idx = 0;
                const auto& mon = MenuConfig::MonitorList[idx];
                monX = mon.x;
                monY = mon.y;
                monW = mon.width;
                monH = mon.height;
            }

            int winW = (Window.Size.x > 0) ? (int)Window.Size.x : monW;
            int winH = (Window.Size.y > 0) ? (int)Window.Size.y : monH;
            Window.hWnd = CreateWindowExW(WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, Window.wClassName.c_str(), Window.wName.c_str(), WS_POPUP, monX, monY, winW, winH, NULL, NULL, GetModuleHandle(NULL), NULL);
        }
        Window.hInstance = wc.hInstance;

        if (!g_Device.CreateDeviceD3D(Window.hWnd))
        {
            g_Device.CleanupDeviceD3D();
            UnregisterClassW(wc.lpszClassName, wc.hInstance);
            return false;
        }

        ShowWindow(Window.hWnd, SW_SHOWDEFAULT);
        UpdateWindow(Window.hWnd);

        return Window.hWnd != NULL;
    }

    bool OSImGui_External::UpdateWindowData()
    {
        POINT Point{};
        RECT Rect{};

        DestWindow.hWnd = FindWindowA(
            (DestWindow.ClassName.empty() ? NULL : DestWindow.ClassName.c_str()),
            (DestWindow.Name.empty() ? NULL : DestWindow.Name.c_str()));
        if (DestWindow.hWnd == NULL)
            return false;

        GetClientRect(DestWindow.hWnd, &Rect);
        ClientToScreen(DestWindow.hWnd, &Point);

        Window.Pos = DestWindow.Pos = Vec2(static_cast<float>(Point.x), static_cast<float>(Point.y));
        Window.Size = DestWindow.Size = Vec2(static_cast<float>(Rect.right), static_cast<float>(Rect.bottom));

        SetWindowPos(Window.hWnd, HWND_TOPMOST, (int)Window.Pos.x, (int)Window.Pos.y, (int)Window.Size.x, (int)Window.Size.y, SWP_SHOWWINDOW);

        // ���ƴ���״̬�л�
        POINT MousePos;
        GetCursorPos(&MousePos);
        ScreenToClient(Window.hWnd, &MousePos);
        ImGui::GetIO().MousePos.x = static_cast<float>(MousePos.x);
        ImGui::GetIO().MousePos.y = static_cast<float>(MousePos.y);

        bool wantInput = ImGui::GetIO().WantCaptureMouse || ImGui::GetIO().WantTextInput;
        LONG exStyle = GetWindowLong(Window.hWnd, GWL_EXSTYLE);
        if (wantInput) {
            exStyle &= ~(WS_EX_LAYERED | WS_EX_TRANSPARENT);
            SetWindowLong(Window.hWnd, GWL_EXSTYLE, exStyle);
            if (ImGui::GetIO().WantTextInput && GetForegroundWindow() != Window.hWnd)
                SetForegroundWindow(Window.hWnd);
        } else {
            exStyle |= (WS_EX_LAYERED | WS_EX_TRANSPARENT);
            SetWindowLong(Window.hWnd, GWL_EXSTYLE, exStyle);
        }
        return true;
    }

    LRESULT WINAPI WndProc_External(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return true;

        switch (msg)
        {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);

            HBRUSH hBrush = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &ps.rcPaint, hBrush);

            EndPaint(hWnd, &ps);
            break;
        }
        case WM_CREATE:
        {
            MARGINS     Margin = { -1 };
            DwmExtendFrameIntoClientArea(hWnd, &Margin);
            break;
        }
        case WM_SIZE:
            if (g_Device.g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
            {
                g_Device.CleanupRenderTarget();
                UINT flags = g_Device.g_AllowTearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
                g_Device.g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, flags);
                g_Device.CreateRenderTarget();
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }

}