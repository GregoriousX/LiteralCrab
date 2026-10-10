// SPDX-License-Identifier: GPL-3.0-only
// Window placement, input scaling, and Alt+Enter policy adapted from CrabeLoader dev
// src/presentation/render_hook.cpp (Lucas Lhomme). DI1 uses D3D9, not DXGI/D3D11.
#include "di1/display.hpp"
#include "di1/imports.hpp"
#include "di1/frame_pacer.hpp"
#include "minhook/MinHook.h"
#include <d3d9.h>
#include <intrin.h>
#include <atomic>
#include <algorithm>
#include <vector>
#include <cstring>

namespace crabe::di1 {
namespace {
    constexpr UINT applyMessage = WM_APP + 0x5C7;
    constexpr LONG clips = WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    Config options;
    std::atomic<HWND> window{};
    std::atomic<IDirect3DDevice9*> device{};
    std::atomic<unsigned> bufferWidth{}, bufferHeight{};
    std::atomic<Mode> requestedMode{Mode::Borderless};
    std::atomic<bool> managed{}, dirty{}, applying{}, userMinimizing{};
    std::atomic<std::uint64_t> presents{};
    WNDPROC originalWndProc{};
    RECT windowedRect{};
    bool hasWindowedRect{}, hasApplied{};
    Mode appliedMode = Mode::Borderless;
    DWORD windowThread{};
    IDirect3D9* deviceParent{};
    UINT deviceAdapter{};
    HWND deviceFocus{};
    ImportPatch factoryImport, resolverImport, screenToClientImport, clientToScreenImport, showWindowImport;
    using Factory = IDirect3D9* (WINAPI*)(UINT);
    using FactoryEx = HRESULT (WINAPI*)(UINT, IDirect3D9Ex**);
    using CreateDevice = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    using CreateDeviceEx = HRESULT (STDMETHODCALLTYPE*)(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
    using Reset = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    using ResetEx = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
    using Present = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    using PresentEx = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
    using Release = ULONG (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
    Factory realFactory{};
    FactoryEx realFactoryEx{};
    bool deviceExtended{};
    thread_local unsigned creationDepth{}, resetDepth{}, presentDepth{}, focusLossDepth{};
    struct CallScope { unsigned& depth; explicit CallScope(unsigned& value) : depth(value) { ++depth; } ~CallScope() { --depth; } };
    struct Method { void* target; void* original; };
    std::vector<Method> createMethods, createExMethods, resetMethods, resetExMethods, presentMethods, presentExMethods, releaseMethods, cooperativeMethods;
    SRWLOCK hookLock = SRWLOCK_INIT;
    FramePacer framePacer;
    std::atomic<void (*)(bool)> focusObserver{};
    std::atomic<bool> statsRequested{};
    // Present cadence collected in memory on the render thread; written only on request.
    struct FrameStats {
        long long frequency{}, last{}, frames{}, intervalSum{}, intervalMax{}, presentSum{}, presentMax{};
        unsigned over20{}, over33{}, over50{};
        long long now() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
        void frame(long long current) {
            if (!frequency) { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); frequency = value.QuadPart; }
            if (last) {
                const long long interval = current - last;
                ++frames; intervalSum += interval; intervalMax = (std::max)(intervalMax, interval);
                if (interval * 50 > frequency) ++over20;
                if (interval * 30 > frequency) ++over33;
                if (interval * 20 > frequency) ++over50;
            }
            last = current;
        }
        void presented(long long blocked) { presentSum += blocked; presentMax = (std::max)(presentMax, blocked); }
        void report(const char* reason) {
            if (frames && frequency) {
                const double ms = 1000.0 / static_cast<double>(frequency);
                log("Frame stats (%s): %lld frames, avg %.2f ms (%.1f FPS), worst %.2f ms; >20ms %u, >33ms %u, >50ms %u; "
                    "Present blocked avg %.2f ms, worst %.2f ms.", reason, frames, intervalSum * ms / frames,
                    frames * static_cast<double>(frequency) / intervalSum, intervalMax * ms, over20, over33, over50,
                    presentSum * ms / frames, presentMax * ms);
            }
            const auto keep = frequency;
            *this = {}; frequency = keep;
        }
    } frameStats;

    LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
    HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
    ULONG STDMETHODCALLTYPE release(IDirect3DDevice9*);
    HRESULT STDMETHODCALLTYPE cooperative(IDirect3DDevice9*);
    HRESULT STDMETHODCALLTYPE createDevice(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
    HRESULT STDMETHODCALLTYPE createDeviceEx(IDirect3D9Ex*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*, IDirect3DDevice9Ex**);
    HRESULT STDMETHODCALLTYPE resetEx(IDirect3DDevice9Ex*, D3DPRESENT_PARAMETERS*, D3DDISPLAYMODEEX*);
    HRESULT STDMETHODCALLTYPE presentEx(IDirect3DDevice9Ex*, const RECT*, const RECT*, HWND, const RGNDATA*, DWORD);
    void removeMethod(void*& target) {
        if (target) { MH_DisableHook(target); MH_RemoveHook(target); target = nullptr; }
    }
    bool addMethod(std::vector<Method>& methods, void* target, void* detour) {
        for (const auto& method : methods) if (method.target == target) return true;
        Method method{target,nullptr};
        const auto result = MH_CreateHook(target,detour,&method.original);
        if (result != MH_OK) { log("D3D9 method hook refused: %s.",MH_StatusToString(result)); return false; }
        methods.push_back(method);
        MH_QueueEnableHook(target);
        return true;
    }
    void* originalFor(void* active, unsigned index, const std::vector<Method>& methods) {
        const void* target = (*reinterpret_cast<void***>(active))[index];
        AcquireSRWLockShared(&hookLock);
        void* found{};
        for (const auto& method : methods) if (method.target == target) { found = method.original; break; }
        // An overlay may replace the vtable slot and then call the hooked runtime method itself.
        // With a single known implementation, its trampoline is still the correct original.
        if (!found && methods.size() == 1) found = methods.front().original;
        ReleaseSRWLockShared(&hookLock);
        return found;
    }
    bool installFactoryMethods(IDirect3D9* d3d, bool extended) {
        auto table = *reinterpret_cast<void***>(d3d);
        AcquireSRWLockExclusive(&hookLock);
        bool ok = addMethod(createMethods,table[16],reinterpret_cast<void*>(&createDevice));
        if (ok && extended) ok = addMethod(createExMethods,table[20],reinterpret_cast<void*>(&createDeviceEx));
        if (ok) ok = MH_ApplyQueued() == MH_OK;
        ReleaseSRWLockExclusive(&hookLock);
        return ok;
    }
    bool installDeviceMethods(IDirect3DDevice9* created, bool extended) {
        auto table = *reinterpret_cast<void***>(created);
        AcquireSRWLockExclusive(&hookLock);
        const auto previousResetEntries = resetMethods.size();
        bool ok = addMethod(resetMethods,table[16],reinterpret_cast<void*>(&reset)) &&
            addMethod(presentMethods,table[17],reinterpret_cast<void*>(&present)) &&
            addMethod(releaseMethods,table[2],reinterpret_cast<void*>(&release)) &&
            addMethod(cooperativeMethods,table[3],reinterpret_cast<void*>(&cooperative));
        if (ok && extended) ok = addMethod(resetExMethods,table[132],reinterpret_cast<void*>(&resetEx)) &&
            addMethod(presentExMethods,table[121],reinterpret_cast<void*>(&presentEx));
        if (ok) ok = MH_ApplyQueued() == MH_OK;
        if (previousResetEntries && resetMethods.size() > previousResetEntries)
            log("Captured D3D9 lost-device method table for Reset recovery.");
        ReleaseSRWLockExclusive(&hookLock);
        return ok;
    }
    void refreshBuffer(IDirect3DDevice9* active) {
        IDirect3DSurface9* surface{};
        if (SUCCEEDED(active->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &surface)) && surface) {
            D3DSURFACE_DESC desc{};
            if (SUCCEEDED(surface->GetDesc(&desc))) { bufferWidth = desc.Width; bufferHeight = desc.Height; }
            surface->Release(); // No retained D3D resources: Reset remains entirely the game's responsibility.
        }
    }
    RECT defaultWindowedRect(HWND hwnd, LONG style) {
        RECT client{0, 0, static_cast<LONG>(bufferWidth.load()), static_cast<LONG>(bufferHeight.load())};
        if (client.right < 100 || client.bottom < 100) client = {0, 0, 1280, 720};
        AdjustWindowRectEx(&client, static_cast<DWORD>(style & ~WS_VISIBLE), FALSE, WS_EX_APPWINDOW);
        MONITORINFO monitor{sizeof(monitor)};
        RECT work{0, 0, 1920, 1080};
        if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) work = monitor.rcWork;
        LONG w = client.right - client.left, h = client.bottom - client.top;
        const LONG availableW = work.right - work.left, availableH = work.bottom - work.top;
        if (w > availableW || h > availableH) {
            const double scale = 0.85 * (std::min)(static_cast<double>(availableW) / w, static_cast<double>(availableH) / h);
            w = static_cast<LONG>(w * scale); h = static_cast<LONG>(h * scale);
        }
        const LONG x = work.left + (availableW - w) / 2, y = work.top + (availableH - h) / 2;
        return {x, y, x + w, y + h};
    }
    void applyWindow() {
        const HWND hwnd = window.load();
        if (!hwnd || !managed || IsIconic(hwnd) || applying.exchange(true)) return;
        const auto mode = requestedMode.load();
        const LONG style = GetWindowLongW(hwnd, GWL_STYLE), exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
        RECT current{}; GetWindowRect(hwnd, &current);
        const bool wasWindowed = hasApplied && appliedMode == Mode::Windowed && (style & WS_CAPTION) == WS_CAPTION;
        if (wasWindowed && !IsZoomed(hwnd) && current.left > -30000) { windowedRect = current; hasWindowedRect = true; }
        LONG desiredStyle = clips & style;
        RECT desired{};
        if (mode == Mode::Borderless) {
            MONITORINFO monitor{sizeof(monitor)};
            if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
                applying = false; dirty = true; return;
            }
            desiredStyle |= WS_POPUP | WS_VISIBLE;
            desired = monitor.rcMonitor;
        } else {
            desiredStyle |= WS_OVERLAPPEDWINDOW | WS_VISIBLE;
            if (wasWindowed && current.left > -30000) {
                desired = current; desiredStyle |= style & WS_MAXIMIZE;
            } else if (hasWindowedRect && windowedRect.left > -30000 && MonitorFromRect(&windowedRect, MONITOR_DEFAULTTONULL))
                desired = windowedRect;
            else desired = defaultWindowedRect(hwnd, desiredStyle);
        }
        const LONG desiredEx = (exStyle | WS_EX_APPWINDOW) & ~(WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        if (style != desiredStyle || exStyle != desiredEx || !EqualRect(&current, &desired)) {
            SetWindowLongW(hwnd, GWL_STYLE, desiredStyle);
            SetWindowLongW(hwnd, GWL_EXSTYLE, desiredEx);
            UINT flags = SWP_FRAMECHANGED | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
            if (GetForegroundWindow() != hwnd) flags |= SWP_NOACTIVATE;
            SetWindowPos(hwnd, HWND_NOTOPMOST, desired.left, desired.top, desired.right - desired.left, desired.bottom - desired.top, flags);
            log("Window mode %ls: outer %ldx%ld at %ld,%ld; render %ux%u.", modeName(mode),
                desired.right - desired.left, desired.bottom - desired.top, desired.left, desired.top,
                bufferWidth.load(), bufferHeight.load());
        }
        appliedMode = mode; hasApplied = true; applying = false;
    }
    void queueWindow() {
        // Runs on every Present: the rarely set flag is checked before any window query.
        if (!dirty.load(std::memory_order_relaxed)) return;
        const HWND hwnd = window.load();
        if (!hwnd || !managed || IsIconic(hwnd) || !dirty.exchange(false)) return;
        if (GetCurrentThreadId() == windowThread) applyWindow();
        else if (!PostMessageW(hwnd, applyMessage, 0, 0)) dirty = true;
    }
    void detachWindow() {
        HWND hwnd = window.exchange(nullptr);
        bufferWidth = 0; bufferHeight = 0; managed = false; hasApplied = false; hasWindowedRect = false;
        if (!hwnd || !originalWndProc || !IsWindow(hwnd)) { originalWndProc = nullptr; return; }
        if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC)) == wndProc) {
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalWndProc));
            originalWndProc = nullptr;
        } else {
            // A later subclass (e.g. an overlay) still calls wndProc. Keep forwarding to the
            // game's procedure; with managed=false the hook is a pass-through.
            window = hwnd;
        }
    }
    bool attachWindow(HWND hwnd) {
        if (window.load() == hwnd && originalWndProc) return true;
        if (window.load() && window.load() != hwnd) detachWindow();
        window = hwnd; windowThread = GetWindowThreadProcessId(hwnd, nullptr);
        SetLastError(0);
        auto previous = SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&wndProc));
        if (!previous && GetLastError()) { log("Window procedure hook failed: %lu.", GetLastError()); return false; }
        originalWndProc = reinterpret_cast<WNDPROC>(previous);
        return true;
    }
    D3DPRESENT_PARAMETERS effectiveParameters(const D3DPRESENT_PARAMETERS& incoming, IDirect3D9* d3d, UINT adapter, HWND focus) {
        auto effective = incoming;
        if (!effective.hDeviceWindow) effective.hDeviceWindow = focus;
        if (requestedMode.load() != Mode::Fullscreen) {
            effective.Windowed = TRUE;
            effective.FullScreen_RefreshRateInHz = 0; // D3D9 requires this in windowed mode.
            // A zero-sized Reset while minimized must not poison device recovery.
            if (!effective.BackBufferWidth && bufferWidth) effective.BackBufferWidth = bufferWidth.load();
            if (!effective.BackBufferHeight && bufferHeight) effective.BackBufferHeight = bufferHeight.load();
        } else {
            effective.Windowed = FALSE;
            D3DDISPLAYMODE desktop{};
            if (d3d && SUCCEEDED(d3d->GetAdapterDisplayMode(adapter, &desktop))) {
                if (!effective.BackBufferWidth) effective.BackBufferWidth = desktop.Width;
                if (!effective.BackBufferHeight) effective.BackBufferHeight = desktop.Height;
                if (effective.BackBufferFormat == D3DFMT_UNKNOWN) effective.BackBufferFormat = desktop.Format;
            }
        }
        unsigned refresh = incoming.FullScreen_RefreshRateInHz;
        // The monitor rate only matters for VSync=auto with a frame cap.
        if (refresh <= 1 && options.vsync == VSync::Auto && options.frameLimit) {
            MONITORINFOEXW monitor{}; monitor.cbSize = sizeof(monitor);
            DEVMODEW mode{}; mode.dmSize = sizeof(mode);
            if (GetMonitorInfoW(MonitorFromWindow(effective.hDeviceWindow,MONITOR_DEFAULTTONEAREST), &monitor) &&
                EnumDisplaySettingsW(monitor.szDevice,ENUM_CURRENT_SETTINGS,&mode)) refresh = mode.dmDisplayFrequency;
        }
        if (options.vsync == VSync::Off || (options.vsync == VSync::Auto &&
            (!options.frameLimit || (refresh > 1 && options.frameLimit >= refresh))))
            effective.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
        else if (options.vsync == VSync::On) effective.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        // Keep the game's multisampling, depth buffers and other rendering flags.
        return effective;
    }
    D3DDISPLAYMODEEX* fullscreenMode(D3DPRESENT_PARAMETERS& effective, IDirect3D9* parent,
                                    UINT adapter, const D3DDISPLAYMODEEX* incoming, D3DDISPLAYMODEEX& mode) {
        if (effective.Windowed) return nullptr;
        mode = {};
        if (incoming) mode = *incoming;
        else {
            IDirect3D9Ex* extended{};
            if (parent && SUCCEEDED(parent->QueryInterface(__uuidof(IDirect3D9Ex),reinterpret_cast<void**>(&extended)))) {
                extended->GetAdapterDisplayModeEx(adapter,&mode,nullptr);
                extended->Release();
            }
        }
        mode.Size = sizeof(mode);
        mode.Width = effective.BackBufferWidth; mode.Height = effective.BackBufferHeight;
        mode.Format = effective.BackBufferFormat;
        if (effective.FullScreen_RefreshRateInHz > 1) mode.RefreshRate = effective.FullScreen_RefreshRateInHz;
        else effective.FullScreen_RefreshRateInHz = mode.RefreshRate;
        return &mode;
    }
    bool capabilityProbe(const D3DPRESENT_PARAMETERS* incoming) {
        return incoming && incoming->BackBufferWidth && incoming->BackBufferHeight &&
            (incoming->BackBufferWidth < 64 || incoming->BackBufferHeight < 64);
    }
    void reportParameters(D3DPRESENT_PARAMETERS* incoming, const D3DPRESENT_PARAMETERS& effective) {
        const BOOL requestedWindowed = incoming->Windowed;
        const UINT requestedRefresh = incoming->FullScreen_RefreshRateInHz;
        *incoming = effective;
        // The engine's own window-management state remains consistent with its request.
        incoming->Windowed = requestedWindowed;
        incoming->FullScreen_RefreshRateInHz = requestedRefresh;
    }
    IDirect3D9* WINAPI create9(UINT version) {
        // Called by the engine after loader-lock release. No D3D/GPU work runs in DllMain.
        if (!realFactory) {
            const HMODULE d3dModule = LoadLibraryW(L"d3d9.dll");
            const auto nativeResolve = reinterpret_cast<decltype(&GetProcAddress)>(resolverImport.original);
            if (d3dModule && nativeResolve) realFactory = reinterpret_cast<Factory>(nativeResolve(d3dModule,"Direct3DCreate9"));
        }
        if (!realFactory) { log("D3D9 could not be loaded: %lu.", GetLastError()); return nullptr; }
        IDirect3D9* d3d = realFactory(version);
        if (!d3d) return nullptr;
        const bool ok = installFactoryMethods(d3d,false);
        if (!ok) log("D3D9 CreateDevice interception unavailable; window policy will not run.");
        else log("D3D9 factory intercepted before the first device.");
        return d3d;
    }
    HRESULT WINAPI create9Ex(UINT version, IDirect3D9Ex** output) {
        if (!realFactoryEx) {
            const HMODULE d3dModule = LoadLibraryW(L"d3d9.dll");
            const auto nativeResolve = reinterpret_cast<decltype(&GetProcAddress)>(resolverImport.original);
            if (d3dModule && nativeResolve) realFactoryEx = reinterpret_cast<FactoryEx>(nativeResolve(d3dModule,"Direct3DCreate9Ex"));
        }
        if (!realFactoryEx) return D3DERR_NOTAVAILABLE;
        const HRESULT result = realFactoryEx(version,output);
        if (SUCCEEDED(result) && output && *output) {
            if (installFactoryMethods(*output,true)) log("D3D9Ex factory intercepted before the first device.");
            else log("D3D9Ex CreateDevice interception unavailable.");
        }
        return result;
    }
    FARPROC WINAPI resolveFunction(HMODULE module, LPCSTR name) {
        const auto original = reinterpret_cast<decltype(&GetProcAddress)>(resolverImport.original);
        const auto resolved = original(module,name);
        const DWORD error = GetLastError();
        FARPROC result = resolved;
        // DI1 resolves Direct3DCreate9Ex dynamically before using its delay-import fallback.
        // Only the host's lookup is redirected; other DLLs keep their own imports.
        if (resolved && reinterpret_cast<std::uintptr_t>(name) > 0xFFFF && !strncmp(name,"Direct3DCreate9",15) &&
            module == GetModuleHandleW(L"d3d9.dll")) {
            if (!strcmp(name,"Direct3DCreate9Ex")) {
                realFactoryEx = reinterpret_cast<FactoryEx>(resolved);
                result = reinterpret_cast<FARPROC>(&create9Ex);
            } else if (!strcmp(name,"Direct3DCreate9")) {
                realFactory = reinterpret_cast<Factory>(resolved);
                result = reinterpret_cast<FARPROC>(&create9);
            }
        }
        SetLastError(error);
        return result;
    }
    bool gameWindow(HWND hwnd, const void* caller) {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        // A graphics overlay may sit between the engine and the runtime. The game-owned
        // window also identifies that call, without adopting an overlay's own window.
        return hwnd && process == GetCurrentProcessId() && (hostCaller(caller) ||
            reinterpret_cast<HMODULE>(GetWindowLongPtrW(hwnd,GWLP_HINSTANCE)) == GetModuleHandleW(nullptr));
    }
    bool activateDevice(IDirect3D9* d3d, UINT adapter, HWND focus, HWND hwnd,
                        IDirect3DDevice9* created, D3DPRESENT_PARAMETERS* incoming,
                        const D3DPRESENT_PARAMETERS& effective) {
        IDirect3DDevice9Ex* extended{};
        const bool isExtended = SUCCEEDED(created->QueryInterface(__uuidof(IDirect3DDevice9Ex),reinterpret_cast<void**>(&extended)));
        if (extended) extended->Release();
        if (!installDeviceMethods(created,isExtended)) return false;
        deviceExtended = isExtended;
        device = created;
        if (deviceParent) deviceParent->Release();
        deviceParent = d3d; deviceParent->AddRef();
        deviceAdapter = adapter; deviceFocus = focus;
        managed = requestedMode.load() != Mode::Fullscreen;
        if (managed) {
            if (!attachWindow(hwnd)) { managed = false; log("Display window could not be attached."); }
        } else window = hwnd;
        refreshBuffer(created);
        framePacer.reset();
        reportParameters(incoming, effective);
        log("Active D3D9%s back buffer: %ux%u; exclusive=%d.", isExtended ? "Ex" : "", bufferWidth.load(), bufferHeight.load(), !managed.load());
        log("Frame pacing: cap=%u; presentation interval=0x%X (0x80000000=immediate).", options.frameLimit, effective.PresentationInterval);
        dirty = true; queueWindow();
        return true;
    }
    HRESULT STDMETHODCALLTYPE createDevice(IDirect3D9* d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                                           DWORD flags, D3DPRESENT_PARAMETERS* incoming, IDirect3DDevice9** output) {
        const auto originalCreate = reinterpret_cast<CreateDevice>(originalFor(d3d,16,createMethods));
        if (!originalCreate) return D3DERR_INVALIDCALL;
        const HWND hwnd = incoming && incoming->hDeviceWindow ? incoming->hDeviceWindow : focus;
        if (!incoming || !output || creationDepth || capabilityProbe(incoming) || !gameWindow(hwnd,_ReturnAddress()))
            return originalCreate(d3d,adapter,type,focus,flags,incoming,output);
        CallScope scope(creationDepth);
        auto effective = effectiveParameters(*incoming,d3d,adapter,focus);
        const HRESULT result = originalCreate(d3d,adapter,type,focus,flags,&effective,output);
        log("CreateDevice: request %ux%u windowed=%d; actual windowed=%d; HRESULT=0x%08lX.",
            incoming->BackBufferWidth,incoming->BackBufferHeight,incoming->Windowed,effective.Windowed,result);
        if (SUCCEEDED(result) && *output && !activateDevice(d3d,adapter,focus,hwnd,*output,incoming,effective)) {
            (*output)->Release(); *output = nullptr;
            log("Display hooks incomplete; retrying unmodified native CreateDevice.");
            return originalCreate(d3d,adapter,type,focus,flags,incoming,output);
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE createDeviceEx(IDirect3D9Ex* d3d, UINT adapter, D3DDEVTYPE type, HWND focus,
                                             DWORD flags, D3DPRESENT_PARAMETERS* incoming, D3DDISPLAYMODEEX* mode,
                                             IDirect3DDevice9Ex** output) {
        const auto originalCreate = reinterpret_cast<CreateDeviceEx>(originalFor(d3d,20,createExMethods));
        if (!originalCreate) return D3DERR_INVALIDCALL;
        const HWND hwnd = incoming && incoming->hDeviceWindow ? incoming->hDeviceWindow : focus;
        if (!incoming || !output || creationDepth || capabilityProbe(incoming) || !gameWindow(hwnd,_ReturnAddress()))
            return originalCreate(d3d,adapter,type,focus,flags,incoming,mode,output);
        CallScope scope(creationDepth);
        auto effective = effectiveParameters(*incoming,d3d,adapter,focus);
        D3DDISPLAYMODEEX actualMode{};
        auto modeForCreate = fullscreenMode(effective,d3d,adapter,mode,actualMode);
        const HRESULT result = originalCreate(d3d,adapter,type,focus,flags,&effective,modeForCreate,output);
        log("CreateDeviceEx: request %ux%u windowed=%d; actual windowed=%d; HRESULT=0x%08lX.",
            incoming->BackBufferWidth,incoming->BackBufferHeight,incoming->Windowed,effective.Windowed,result);
        if (SUCCEEDED(result) && *output && !activateDevice(d3d,adapter,focus,hwnd,*output,incoming,effective)) {
            (*output)->Release(); *output = nullptr;
            log("Display hooks incomplete; retrying unmodified native CreateDeviceEx.");
            return originalCreate(d3d,adapter,type,focus,flags,incoming,mode,output);
        }
        return result;
    }
    HRESULT STDMETHODCALLTYPE reset(IDirect3DDevice9* active, D3DPRESENT_PARAMETERS* incoming) {
        const auto originalReset = reinterpret_cast<Reset>(originalFor(active,16,resetMethods));
        if (!originalReset) return D3DERR_INVALIDCALL;
        if (active != device.load() || !incoming || resetDepth) return originalReset(active, incoming);
        CallScope scope(resetDepth);
        // GetDirect3D/GetCreationParameters are not legal on a device after a failed Reset.
        // Keep the creation metadata and parent interface, which own no Reset-blocking surface.
        auto effective = effectiveParameters(*incoming, deviceParent, deviceAdapter, deviceFocus);
        const UINT effectiveWidth = effective.BackBufferWidth, effectiveHeight = effective.BackBufferHeight;
        const HRESULT result = originalReset(active, &effective);
        // The runtime can replace the object's vtable on either failure or recovery.
        // Repair it before returning so the engine's next Reset stays intercepted.
        if (!installDeviceMethods(active,deviceExtended)) log("Could not capture D3D9 recovery methods.");
        log("Reset: requested %ux%u; effective %ux%u windowed=%d; HRESULT=0x%08lX.",
            incoming->BackBufferWidth, incoming->BackBufferHeight, effectiveWidth, effectiveHeight, effective.Windowed, result);
        if (SUCCEEDED(result)) {
            framePacer.reset();
            refreshBuffer(active); reportParameters(incoming, effective); dirty = true; queueWindow();
        }
        return result; // Never hide DEVICELOST / INVALIDCALL from the engine.
    }
    HRESULT STDMETHODCALLTYPE resetEx(IDirect3DDevice9Ex* active, D3DPRESENT_PARAMETERS* incoming, D3DDISPLAYMODEEX* mode) {
        const auto originalReset = reinterpret_cast<ResetEx>(originalFor(active,132,resetExMethods));
        if (!originalReset) return D3DERR_INVALIDCALL;
        if (active != device.load() || !incoming || resetDepth) return originalReset(active,incoming,mode);
        CallScope scope(resetDepth);
        auto effective = effectiveParameters(*incoming,deviceParent,deviceAdapter,deviceFocus);
        D3DDISPLAYMODEEX actualMode{};
        auto modeForReset = fullscreenMode(effective,deviceParent,deviceAdapter,mode,actualMode);
        const HRESULT result = originalReset(active,&effective,modeForReset);
        if (!installDeviceMethods(active,deviceExtended)) log("Could not capture D3D9Ex recovery methods.");
        log("ResetEx: requested %ux%u; windowed=%d; HRESULT=0x%08lX.",
            incoming->BackBufferWidth,incoming->BackBufferHeight,effective.Windowed,result);
        if (SUCCEEDED(result)) {
            framePacer.reset(); refreshBuffer(active); reportParameters(incoming,effective); dirty = true; queueWindow();
        }
        return result;
    }
    // Returns the timestamp that afterPresent uses to measure blocking, or 0 for untracked calls.
    long long beforePresent(IDirect3DDevice9* active, bool outer) {
        if (active != device.load() || !outer) return 0;
        queueWindow();
        // No periodic logging here: log() performs synchronous file I/O on the render thread.
        if (++presents == 1)
            log("First Present: render %ux%u; managed=%d.",bufferWidth.load(),bufferHeight.load(),managed.load());
        else if (statsRequested.load(std::memory_order_relaxed) && statsRequested.exchange(false))
            frameStats.report("focus change");
        framePacer.pace(options.frameLimit);
        const long long start = frameStats.now();
        frameStats.frame(start);
        return start;
    }
    void afterPresent(IDirect3DDevice9* active, HRESULT result, long long start) {
        if (!start) return;
        frameStats.presented(frameStats.now() - start);
        if (FAILED(result)) installDeviceMethods(active,deviceExtended);
    }
    HRESULT STDMETHODCALLTYPE present(IDirect3DDevice9* active, const RECT* source, const RECT* destination, HWND overrideWindow, const RGNDATA* region) {
        const auto originalPresent = reinterpret_cast<Present>(originalFor(active,17,presentMethods));
        if (!originalPresent) return D3DERR_INVALIDCALL;
        const bool outer = presentDepth == 0;
        CallScope scope(presentDepth);
        const long long start = beforePresent(active,outer);
        const HRESULT result = originalPresent(active,source,destination,overrideWindow,region);
        afterPresent(active,result,start);
        return result;
    }
    HRESULT STDMETHODCALLTYPE presentEx(IDirect3DDevice9Ex* active, const RECT* source, const RECT* destination,
                                       HWND overrideWindow, const RGNDATA* region, DWORD flags) {
        const auto originalPresent = reinterpret_cast<PresentEx>(originalFor(active,121,presentExMethods));
        if (!originalPresent) return D3DERR_INVALIDCALL;
        const bool outer = presentDepth == 0;
        CallScope scope(presentDepth);
        const long long start = beforePresent(active,outer);
        const HRESULT result = originalPresent(active,source,destination,overrideWindow,region,flags);
        afterPresent(active,result,start);
        return result;
    }
    ULONG STDMETHODCALLTYPE release(IDirect3DDevice9* active) {
        const auto originalRelease = reinterpret_cast<Release>(originalFor(active,2,releaseMethods));
        if (!originalRelease) return 1;
        const bool primary = active == device.load();
        const ULONG count = originalRelease(active);
        if (primary && count == 0) {
            device = nullptr; detachWindow(); frameStats.report("device released");
            if (deviceParent) { deviceParent->Release(); deviceParent = nullptr; }
            log("D3D9 device released.");
        }
        return count;
    }
    HRESULT STDMETHODCALLTYPE cooperative(IDirect3DDevice9* active) {
        using Fn = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
        const auto original = reinterpret_cast<Fn>(originalFor(active,3,cooperativeMethods));
        if (!original) return D3DERR_INVALIDCALL;
        const HRESULT result = original(active);
        // Polled every frame by many engines; only a lost device can carry a new method table.
        if (FAILED(result) && active == device.load()) installDeviceMethods(active,deviceExtended);
        return result;
    }
    BOOL WINAPI screenToClient(HWND hwnd, LPPOINT point) {
        const auto original = reinterpret_cast<decltype(&ScreenToClient)>(screenToClientImport.original);
        const BOOL result = original(hwnd, point);
        if (result && point && managed && options.scaleMouse && hwnd == window.load()) {
            RECT client{};
            if (GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0) {
                point->x = scaleCoordinate(point->x, bufferWidth.load(), client.right);
                point->y = scaleCoordinate(point->y, bufferHeight.load(), client.bottom);
            }
        }
        return result;
    }
    BOOL WINAPI clientToScreen(HWND hwnd, LPPOINT point) {
        const auto original = reinterpret_cast<decltype(&ClientToScreen)>(clientToScreenImport.original);
        if (point && managed && options.scaleMouse && hwnd == window.load()) {
            RECT client{};
            if (GetClientRect(hwnd, &client) && client.right > 0 && client.bottom > 0) {
                point->x = scaleCoordinate(point->x, client.right, bufferWidth.load());
                point->y = scaleCoordinate(point->y, client.bottom, bufferHeight.load());
            }
        }
        return original(hwnd, point);
    }
    BOOL WINAPI showWindow(HWND hwnd, int command) {
        const auto original = reinterpret_cast<decltype(&ShowWindow)>(showWindowImport.original);
        // Suppress only the game's automatic minimize. OS/taskbar/title-bar minimize still works.
        if (managed && options.keepVisibleOnFocusLoss && hwnd == window.load() && !userMinimizing &&
            (command == SW_MINIMIZE || command == SW_FORCEMINIMIZE || command == SW_SHOWMINNOACTIVE ||
             command == SW_SHOWMINIMIZED || (focusLossDepth && command == SW_HIDE))) {
            ClipCursor(nullptr); return IsWindowVisible(hwnd);
        }
        return original(hwnd, command);
    }
    // Managed modes can switch without touching the game's D3D resources. Exclusive needs a restart.
    bool requestManagedMode(Mode mode) {
        if (!managed || mode == Mode::Fullscreen) return false;
        requestedMode = mode; dirty = true; return true;
    }
    LRESULT CALLBACK wndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == applyMessage) { applyWindow(); return 0; }
        if (message == WM_SYSKEYDOWN && wParam == VK_RETURN && (lParam & (1L << 29)) && options.altEnter) {
            if (!(lParam & (1L << 30))) {
                requestManagedMode(requestedMode.load() == Mode::Borderless ? Mode::Windowed : Mode::Borderless);
                if (options.rememberMode) saveMode(requestedMode.load());
                queueWindow();
            }
            return 0;
        }
        if (message == WM_SYSKEYUP && wParam == VK_RETURN && options.altEnter) return 0;
        if (message == WM_WINDOWPOSCHANGING && !applying) {
            auto pos = reinterpret_cast<WINDOWPOS*>(lParam);
            if (pos && pos->hwndInsertAfter == HWND_TOPMOST && !(pos->flags & SWP_NOZORDER)) pos->hwndInsertAfter = HWND_NOTOPMOST;
        }
        if (!applying && (message == WM_DISPLAYCHANGE || message == WM_STYLECHANGED || message == WM_EXITSIZEMOVE || message == WM_SETFOCUS)) dirty = true;
        if ((message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) || message == WM_KILLFOCUS) ClipCursor(nullptr);
        const WNDPROC original = originalWndProc;
        if (!original) return DefWindowProcW(hwnd, message, wParam, lParam);
        if (message == WM_SYSCOMMAND && (wParam & 0xFFF0) == SC_MINIMIZE) {
            if (focusLossDepth && managed && options.keepVisibleOnFocusLoss) return 0;
            userMinimizing = true;
            const auto result = CallWindowProcW(original, hwnd, message, wParam, lParam);
            userMinimizing = false;
            return result;
        }
        if (managed && options.keepVisibleOnFocusLoss &&
            ((message == WM_ACTIVATE && LOWORD(wParam) == WA_INACTIVE) ||
             (message == WM_ACTIVATEAPP && !wParam) || message == WM_KILLFOCUS)) {
            CallScope losingFocus(focusLossDepth);
            const LRESULT result = CallWindowProcW(original,hwnd,message,wParam,lParam);
            if (message == WM_ACTIVATE) {
                statsRequested = true;
                if (const auto observer = focusObserver.load()) observer(false);
            }
            return result;
        }
        if (message == WM_ACTIVATE) {
            const LRESULT result = CallWindowProcW(original,hwnd,message,wParam,lParam);
            statsRequested = true;
            if (const auto observer = focusObserver.load()) observer(LOWORD(wParam) != WA_INACTIVE);
            return result;
        }
        if (message == WM_SIZE && wParam != SIZE_MINIMIZED && !applying) dirty = true;
        if (message == WM_NCDESTROY) {
            originalWndProc = nullptr; window = nullptr; managed = false;
            return CallWindowProcW(original, hwnd, message, wParam, lParam);
        }
        // Forward real focus/pause/input events and all game messages without spoofing activation.
        return CallWindowProcW(original, hwnd, message, wParam, lParam);
    }
}
bool installDisplayHooks(HMODULE host, const Config& settings) {
    options = settings; requestedMode = settings.mode; setHostModule(host);
    const auto initialized = MH_Initialize();
    if (initialized != MH_OK && initialized != MH_ERROR_ALREADY_INITIALIZED) return false;
    if (!factoryImport.install(host,"d3d9.dll","Direct3DCreate9",reinterpret_cast<void*>(&create9)) ||
        !resolverImport.install(host,"kernel32.dll","GetProcAddress",reinterpret_cast<void*>(&resolveFunction))) {
        factoryImport.remove(); resolverImport.remove(); return false;
    }
    if (settings.mode != Mode::Fullscreen) {
        if (settings.scaleMouse) {
            const bool inputOk = screenToClientImport.install(host, "user32.dll", "ScreenToClient", reinterpret_cast<void*>(&screenToClient)) &&
                clientToScreenImport.install(host, "user32.dll", "ClientToScreen", reinterpret_cast<void*>(&clientToScreen));
            if (!inputOk) { screenToClientImport.remove(); clientToScreenImport.remove(); log("Mouse mapping imports unavailable; input left native."); }
        }
        showWindowImport.install(host, "user32.dll", "ShowWindow", reinterpret_cast<void*>(&showWindow));
    }
    return true;
}
void removeDisplayHooks() {
    detachWindow(); device = nullptr;
    if (deviceParent) { deviceParent->Release(); deviceParent = nullptr; }
    factoryImport.remove(); resolverImport.remove(); screenToClientImport.remove(); clientToScreenImport.remove(); showWindowImport.remove();
    for (auto methods : {&createMethods,&createExMethods,&resetMethods,&resetExMethods,&presentMethods,&presentExMethods,&releaseMethods,&cooperativeMethods}) {
        for (auto& method : *methods) removeMethod(method.target);
        methods->clear();
    }
}
void setFocusObserver(void (*observer)(bool active)) { focusObserver = observer; }
DisplaySnapshot displaySnapshot() {
    return {window.load(), bufferWidth.load(), bufferHeight.load(), requestedMode.load(), managed.load(), presents.load()};
}
}
