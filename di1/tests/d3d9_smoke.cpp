// SPDX-License-Identifier: GPL-3.0-only
// Real D3D9 integration probe: links the exact ASI display/import code, no game or saves.
#include "di1/display.hpp"
#include <d3d9.h>
#include <dwmapi.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace crabe::di1;
static int checks{};
static int lastActivation = -1;
static bool lastAppActivation = true;
static IDirect3DDevice9Ex* exDevice{};
static bool useEx{};
static unsigned startupCalls{};
static void startupProbe() { ++startupCalls; }
static bool simulateFocusMinimize{};
static D3DDISPLAYMODEEX exMode{};
static void check(bool ok, const char* message) {
    ++checks;
    std::printf("%s: %s\n",ok ? "PASS" : "FAIL",message);
    std::fflush(stdout);
    if (!ok) std::exit(1);
}
static LRESULT CALLBACK original(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ACTIVATE) lastActivation = LOWORD(wp);
    if (msg == WM_ACTIVATEAPP) lastAppActivation = wp != FALSE;
    if (simulateFocusMinimize && msg == WM_ACTIVATE && LOWORD(wp) == WA_INACTIVE) {
        SendMessageA(hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0);
        ShowWindow(hwnd,SW_HIDE);
    }
    return DefWindowProcA(hwnd,msg,wp,lp);
}
static void checkCapture(HWND hwnd) {
    DwmFlush();
    RECT client{}; GetClientRect(hwnd,&client);
    const auto source = GetDC(hwnd), memory = CreateCompatibleDC(source);
    const auto bitmap = CreateCompatibleBitmap(source,1,1);
    const auto previous = SelectObject(memory,bitmap);
    const bool copied = BitBlt(memory,0,0,1,1,source,client.right/2,client.bottom/2,SRCCOPY|CAPTUREBLT) != FALSE;
    const COLORREF pixel = GetPixel(memory,0,0);
    SelectObject(memory,previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(hwnd,source);
    check(copied,"window image is available to GDI/ShareX-style capture");
    check(GetRValue(pixel)==25 && GetGValue(pixel)==40 && GetBValue(pixel)==60,"capture contains rendered pixels instead of black output");
}
static void pump() {
    MSG msg{};
    while (PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
}
static void draw(IDirect3DDevice9* dev, int frames = 4) {
    for (int i = 0; i < frames; ++i) {
        pump();
        dev->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(25,40,60),1.0f,0);
        const auto before = displaySnapshot().presents;
        const HRESULT result = exDevice ? exDevice->PresentEx(nullptr,nullptr,nullptr,nullptr,0) : dev->Present(nullptr,nullptr,nullptr,nullptr);
        check(SUCCEEDED(result), "D3D9 presentation succeeds");
        check(displaySnapshot().presents == before + 1,"one presentation is paced once, including Ex calls");
    }
}
static D3DPRESENT_PARAMETERS parameters(HWND hwnd, UINT w, UINT h, D3DFORMAT format) {
    D3DPRESENT_PARAMETERS p{};
    p.BackBufferWidth = w; p.BackBufferHeight = h; p.BackBufferFormat = format;
    p.BackBufferCount = 1; p.SwapEffect = D3DSWAPEFFECT_DISCARD; p.hDeviceWindow = hwnd;
    p.Windowed = FALSE; p.FullScreen_RefreshRateInHz = 0; p.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
    return p;
}
static D3DPRESENT_PARAMETERS actualParameters(IDirect3DDevice9* dev) {
    IDirect3DSwapChain9* swap{}; D3DPRESENT_PARAMETERS result{};
    check(SUCCEEDED(dev->GetSwapChain(0,&swap)), "swap chain available");
    check(SUCCEEDED(swap->GetPresentParameters(&result)), "real presentation parameters available");
    swap->Release(); return result;
}
static HRESULT resetDevice(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS& params) {
    if (!exDevice) return dev->Reset(&params);
    exMode.Width = params.BackBufferWidth; exMode.Height = params.BackBufferHeight;
    params.FullScreen_RefreshRateInHz = exMode.RefreshRate;
    return exDevice->ResetEx(&params,params.Windowed ? nullptr : &exMode);
}
int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1],"--factory-only")) {
        Config probe; probe.mode=Mode::Fullscreen;
        check(installDisplayHooks(GetModuleHandleW(nullptr),probe),"factory-only import hooks installed");
        setGraphicsStartupObserver(startupProbe);
        check(startupCalls==0,"startup observer is deferred beyond installation");
        const bool extended=argc>2 && !strcmp(argv[2],"ex");
        for (unsigned i=0; i<2; ++i) {
            IDirect3D9* factory{};
            if (extended) {
                using ExFactory=HRESULT (WINAPI*)(UINT,IDirect3D9Ex**);
                auto createEx=reinterpret_cast<ExFactory>(GetProcAddress(LoadLibraryW(L"d3d9.dll"),"Direct3DCreate9Ex"));
                IDirect3D9Ex* result{};
                check(createEx && SUCCEEDED(createEx(D3D_SDK_VERSION,&result)),"window-free Ex factory succeeds");
                factory=result;
            } else factory=Direct3DCreate9(D3D_SDK_VERSION);
            check(factory && startupCalls==1,"graphics observer runs exactly once");
            if (factory) factory->Release();
        }
        removeDisplayHooks(); return 0;
    }
    Config settings{};
    if (argc > 1 && !strcmp(argv[1],"windowed")) settings.mode = Mode::Windowed;
    if (argc > 1 && !strcmp(argv[1],"fullscreen")) settings.mode = Mode::Fullscreen;
    if (argc > 2 && !strcmp(argv[2],"off")) settings.vsync = VSync::Off;
    if (argc > 2 && !strcmp(argv[2],"on")) settings.vsync = VSync::On;
    if (argc > 3) settings.frameLimit = clampFrameLimit(static_cast<unsigned>(strtoul(argv[3],nullptr,10)));
    useEx = argc > 4 && !strcmp(argv[4],"ex");
    const bool exclusive = settings.mode == Mode::Fullscreen;
    settings.rememberMode = false;
    openLogAndConfig(GetModuleHandleW(nullptr));
    WNDCLASSA wc{}; wc.lpfnWndProc = original; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = "CrabeDI1Smoke";
    check(RegisterClassA(&wc) != 0,"ANSI game-like window class registered");
    HWND hwnd = CreateWindowA(wc.lpszClassName,"DI1 display probe",WS_OVERLAPPEDWINDOW,100,100,900,600,nullptr,nullptr,wc.hInstance,nullptr);
    check(hwnd != nullptr,"window created");
    check(installDisplayHooks(GetModuleHandleW(nullptr),settings),"early import hooks installed");
    IDirect3D9* d3d{};
    IDirect3D9Ex* d3dEx{};
    if (useEx) {
        using ExFactory = HRESULT (WINAPI*)(UINT,IDirect3D9Ex**);
        auto createEx = reinterpret_cast<ExFactory>(GetProcAddress(LoadLibraryW(L"d3d9.dll"),"Direct3DCreate9Ex"));
        check(createEx && SUCCEEDED(createEx(D3D_SDK_VERSION,&d3dEx)),"dynamic D3D9Ex factory succeeds");
        d3d = d3dEx;
    } else d3d = Direct3DCreate9(D3D_SDK_VERSION);
    check(d3d != nullptr,"intercepted D3D9 factory succeeds");
    D3DDISPLAYMODE desktop{}; check(SUCCEEDED(d3d->GetAdapterDisplayMode(0,&desktop)),"desktop mode available");
    auto probeParams = parameters(hwnd,1,1,desktop.Format);
    probeParams.Windowed = TRUE; probeParams.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* probeDevice{};
    check(SUCCEEDED(d3d->CreateDevice(0,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&probeParams,&probeDevice)),"native 1x1 graphics capability probe succeeds");
    check(displaySnapshot().window == nullptr && displaySnapshot().width == 0,"capability probes are not adopted as the game renderer");
    check((GetWindowLongW(hwnd,GWL_STYLE) & WS_CAPTION) == WS_CAPTION,"capability probe does not change window style");
    check(probeDevice->Release() == 0,"capability probe has no retained plugin resources");
    if (exclusive) { ShowWindow(hwnd,SW_SHOW); SetForegroundWindow(hwnd); pump(); }
    auto pp = parameters(hwnd,exclusive ? desktop.Width : 1280,exclusive ? desktop.Height : 720,desktop.Format);
    exMode = {sizeof(exMode),pp.BackBufferWidth,pp.BackBufferHeight,desktop.RefreshRate,desktop.Format,D3DSCANLINEORDERING_PROGRESSIVE};
    if (useEx) pp.FullScreen_RefreshRateInHz = exMode.RefreshRate;
    const bool nativeWindow = argc > 5 && !strcmp(argv[5],"nativewindow");
    if (nativeWindow) { pp.Windowed = TRUE; pp.FullScreen_RefreshRateInHz = 0; }
    IDirect3DDevice9* dev{};
    HRESULT hr{};
    if (useEx) {
        hr = d3dEx->CreateDeviceEx(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,nativeWindow ? nullptr : &exMode,&exDevice);
        dev = exDevice;
    } else hr = d3d->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,hwnd,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&dev);
    if (FAILED(hr)) std::printf("CreateDevice HRESULT=0x%08lX\n",hr);
    check(SUCCEEDED(hr) && dev,"real hardware D3D9 device created");
    const auto real = actualParameters(dev);
    const bool bypassVSync = settings.vsync == VSync::Off ||
        (settings.vsync == VSync::Auto && (!settings.frameLimit || settings.frameLimit >= desktop.RefreshRate));
    check(real.PresentationInterval == (bypassVSync ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE),
        "requested VSync is overridden or preserved according to configuration");
    check(real.Windowed == (exclusive ? FALSE : TRUE),"physical fullscreen/windowed state matches mode");
    check(pp.Windowed == (nativeWindow ? TRUE : FALSE),"engine request state preserved");
    draw(dev);
    if (!exclusive) {
        RECT outer{}; GetWindowRect(hwnd,&outer);
        MONITORINFO mi{sizeof(mi)}; GetMonitorInfoW(MonitorFromWindow(hwnd,MONITOR_DEFAULTTONEAREST),&mi);
        if (settings.mode == Mode::Borderless) {
            check(EqualRect(&outer,&mi.rcMonitor),"borderless fits the selected monitor");
            check((GetWindowLongW(hwnd,GWL_STYLE) & WS_CAPTION) == 0,"borderless has no title bar");
        } else check((GetWindowLongW(hwnd,GWL_STYLE) & WS_CAPTION) == WS_CAPTION,"windowed has a title bar");
        check((GetWindowLongW(hwnd,GWL_EXSTYLE) & WS_EX_TOPMOST) == 0,"managed window is not topmost");
        checkCapture(hwnd);
        simulateFocusMinimize = true;
        SendMessageW(hwnd,WM_ACTIVATE,WA_INACTIVE,0);
        simulateFocusMinimize = false;
        SendMessageW(hwnd,WM_ACTIVATEAPP,FALSE,0);
        check(lastActivation == WA_INACTIVE && !lastAppActivation,"native focus/pause messages are preserved");
        check(!IsIconic(hwnd) && IsWindowVisible(hwnd),"focus callback cannot recursively minimize or hide the capture window");
        HWND captureTool = CreateWindowA(wc.lpszClassName,"Capture focus probe",WS_OVERLAPPEDWINDOW,
            mi.rcWork.right-140,mi.rcWork.bottom-100,120,80,nullptr,nullptr,wc.hInstance,nullptr);
        check(captureTool!=nullptr,"capture-tool focus window created");
        ShowWindow(captureTool,SW_SHOW); SetActiveWindow(captureTool); SetForegroundWindow(captureTool); pump(); draw(dev);
        // Windows may deny background test processes permission to steal the global foreground.
        // Thread activation still delivers real native focus messages between these two windows.
        check(GetActiveWindow()==captureTool,"capture tool can take native window activation");
        check(!IsIconic(hwnd) && IsWindowVisible(hwnd),"native focus loss keeps game window visible");
        checkCapture(hwnd);
        DestroyWindow(captureTool); SetForegroundWindow(hwnd); pump();
        // Scaling uses game imports only: direct User32 pointers give physical client pixels.
        auto realClientToScreen = reinterpret_cast<decltype(&ClientToScreen)>(GetProcAddress(GetModuleHandleW(L"user32.dll"),"ClientToScreen"));
        RECT client{}; GetClientRect(hwnd,&client);
        POINT point{client.right / 2,client.bottom / 2}; realClientToScreen(hwnd,&point);
        check(ScreenToClient(hwnd,&point),"game mouse conversion succeeds");
        check(abs(point.x - 640) <= 2 && abs(point.y - 360) <= 2,"client centre maps to render centre");
        POINT reverse{640,360}; check(ClientToScreen(hwnd,&reverse),"inverse mouse conversion succeeds");
        POINT physical{client.right / 2,client.bottom / 2}; realClientToScreen(hwnd,&physical);
        check(abs(reverse.x - physical.x) <= 2 && abs(reverse.y - physical.y) <= 2,"cursor centre maps back to physical window");
        const auto before = displaySnapshot().mode;
        SendMessageW(hwnd,WM_SYSKEYDOWN,VK_RETURN,(1L<<29)); draw(dev);
        check(displaySnapshot().mode != before,"one Alt+Enter switches managed modes");
        SendMessageW(hwnd,WM_SYSKEYDOWN,VK_RETURN,(1L<<29)|(1L<<30)); draw(dev);
        check(displaySnapshot().mode != before,"Alt+Enter auto-repeat does not switch again");
        SendMessageW(hwnd,WM_SYSKEYDOWN,VK_RETURN,(1L<<29)); draw(dev);
        check(displaySnapshot().mode == before,"second Alt+Enter restores original mode");
        ShowWindow(hwnd,SW_MINIMIZE);
        check(!IsIconic(hwnd),"game automatic minimize is suppressed");
        SendMessageW(hwnd,WM_SYSCOMMAND,SC_MINIMIZE,0); pump();
        check(IsIconic(hwnd),"user minimize is allowed");
        ShowWindow(hwnd,SW_RESTORE); pump();
        check(!IsIconic(hwnd),"minimized window restores");
        draw(dev);
    }
    // A real Reset with a held back buffer must fail, and its HRESULT must reach the caller.
    IDirect3DSurface9* held{}; check(SUCCEEDED(dev->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&held)),"back buffer held for failure-path probe");
    auto resetParams = parameters(hwnd,exclusive ? desktop.Width : 1920,exclusive ? desktop.Height : 1080,desktop.Format);
    if (useEx) resetParams.BackBufferFormat = static_cast<D3DFORMAT>(0x7FFFFFFF);
    check(FAILED(resetDevice(dev,resetParams)),"Reset failure is propagated");
    held->Release();
    resetParams = parameters(hwnd,exclusive ? desktop.Width : 1920,exclusive ? desktop.Height : 1080,desktop.Format);
    check(SUCCEEDED(resetDevice(dev,resetParams)),"Reset recovers after releasing resources");
    const auto resized = displaySnapshot();
    std::printf("Tracked buffer after Reset: %ux%u\n",resized.width,resized.height);
    check(resized.width == (exclusive ? desktop.Width : 1920) && resized.height == (exclusive ? desktop.Height : 1080),"actual resized back buffer is tracked");
    draw(dev);
    if (!exclusive) {
        resetParams = parameters(hwnd,0,0,desktop.Format);
        check(SUCCEEDED(resetDevice(dev,resetParams)),"zero-size Reset safely uses current render dimensions");
        check(displaySnapshot().width == 1920 && displaySnapshot().height == 1080,"zero-size Reset preserves render resolution");
        draw(dev);
    }
    check(dev->Release() == 0,"no plugin references block device destruction");
    check(displaySnapshot().window == nullptr,"window hook is detached on device release");
    removeDisplayHooks(); d3d->Release(); DestroyWindow(hwnd); UnregisterClassA(wc.lpszClassName,wc.hInstance);
    std::printf("PASS: %d D3D9%s integration checks (%s, cap=%u)\n",checks,useEx ? "Ex" : "",exclusive ? "fullscreen" : (settings.mode == Mode::Windowed ? "windowed" : "borderless"),settings.frameLimit);
    return 0;
}
