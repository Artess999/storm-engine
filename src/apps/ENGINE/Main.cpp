#include "LifecycleDiagnosticsService.hpp"
#include "SteamApi.hpp"
#include "compiler.h"
#include "file_service.h"
#include "s_debug.h"
#include "storm/fs.h"
#include "storm/logging.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

VFILE_SERVICE *fio = nullptr;
CORE core;
S_DEBUG CDebug;

namespace
{
constexpr char defaultLoggerName[] = "system";
bool isHold = false;
bool bActive = false;

storm::diag::LifecycleDiagnosticsService lifecycleDiagnostics;
} // namespace

LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR szCmdLine, int iCmdShow)
{
    // Prevent multiple instances
    if (!CreateEventA(nullptr, false, false, "Global\\FBBD2286-A9F1-4303-B60C-743C3D7AA7BE") ||
        GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxA(nullptr, "Another instance is already running!", "Error", MB_ICONERROR);
        return 0;
    }

    // Init FS
    FILE_SERVICE File_Service;
    fio = &File_Service;

    // Init diagnostics
    const auto lifecycleDiagnosticsGuard = lifecycleDiagnostics.initialize(true);
    if (!lifecycleDiagnosticsGuard)
    {
        spdlog::error("Unable to initialize lifecycle service");
    }

    // Init stash
    create_directories(fs::GetSaveDataPath());

    // Init logging
    spdlog::set_default_logger(storm::logging::getOrCreateLogger(defaultLoggerName));

    // Init core
    core.Init();

    // Init script debugger
    CDebug.Init();

    /* Read config */
    auto ini = fio->OpenIniFile(fs::ENGINE_INI_FILE_NAME);

    uint32_t dwMaxFPS = 0;
    bool bSteam = false;

    if (ini)
    {
        dwMaxFPS = static_cast<uint32_t>(ini->GetLong(nullptr, "max_fps", 0));
        auto bDebugWindow = ini->GetLong(nullptr, "DebugWindow", 0) == 1;
        auto bAcceleration = ini->GetLong(nullptr, "Acceleration", 0) == 1;
        if (ini->GetLong(nullptr, "logs", 1) == 0) // disable logging
        {
            spdlog::set_level(spdlog::level::off);
        }
        bSteam = ini->GetLong(nullptr, "Steam", 1) != 0;
    }

    // evaluate SteamApi singleton
    steamapi::SteamApi::getInstance(!bSteam);

    /* Register and show window */
    const auto *const windowName = L"Sea Dogs";

    WNDCLASSEX wndclass;
    wndclass.cbSize = sizeof(wndclass);
    wndclass.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wndclass.lpfnWndProc = WndProc;
    wndclass.cbClsExtra = 0;
    wndclass.cbWndExtra = sizeof(uint16_t);
    wndclass.hInstance = hInstance;
    wndclass.hIcon = LoadIcon(hInstance, TEXT("IDI_ICON1"));
    wndclass.hCursor = LoadCursor(hInstance, TEXT("NULL_CURSOR")); // LoadCursor(NULL,IDC_ARROW);
    wndclass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    wndclass.lpszMenuName = nullptr;
    wndclass.lpszClassName = windowName;
    wndclass.hIconSm = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wndclass);

    auto *const hwnd = CreateWindow(windowName, windowName, WS_POPUP, 0, 0, 0, 0, NULL, NULL, hInstance, NULL);
    ShowWindow(hwnd, SW_SHOWNORMAL);

    /* Init stuff */
    core.InitBase();

    /* Message loop */
    auto dwOldTime = GetTickCount();
    MSG msg;

    while (true)
    {
        if (PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                break;
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            if (bActive)
            {
                if (dwMaxFPS)
                {
                    const auto dwMS = 1000u / dwMaxFPS;
                    const auto dwNewTime = GetTickCount();
                    if (dwNewTime - dwOldTime < dwMS)
                        continue;
                    dwOldTime = dwNewTime;
                }
                const auto runResult = core.Run();
                if (!isHold && !runResult)
                {
                    isHold = true;
                    SendMessage(hwnd, WM_CLOSE, 0, 0L);
                }

                lifecycleDiagnostics.notifyAfterRun();
            }
            else
            {
                Sleep(50);
            }
        }
    }

    /* Release */
    core.ReleaseBase();
    ClipCursor(nullptr);

    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    switch (iMsg)
    {
    case WM_CREATE:
        core.Set_Hwnd(hwnd);
        break;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        break;

    case WM_DESTROY:
        core.Event("DestroyWindow", nullptr);
        core.Event("ExitApplication", nullptr);
        CDebug.Release();
        core.CleanUp();
        CDebug.CloseDebugWindow();

        InvalidateRect(nullptr, nullptr, 0);
        PostQuitMessage(0);
        break;

    case WM_ACTIVATE:
        bActive = LOWORD(wParam) == WA_CLICKACTIVE || LOWORD(wParam) == WA_ACTIVE;
        core.AppState(bActive);
        break;

    case WM_KEYDOWN:
        if (wParam == VK_F5) // && bDebugWindow
        {
            if (!CDebug.IsDebug())
                CDebug.OpenDebugWindow(core.hInstance);
            else
            {
                ShowWindow(CDebug.GetWindowHandle(), SW_NORMAL);
                SetFocus(CDebug.SourceView->hOwn);
            }
        }
    case WM_KEYUP:
    case WM_RBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
        // case MM_MCINOTIFY:
    case WM_LBUTTONDBLCLK:
    case WM_CHAR:
    case WM_MOUSEMOVE:
        if (core.Controls)
            core.Controls->EngineMessage(iMsg, wParam, lParam);
        break;

    case WM_MOUSEWHEEL:
        core.Event("evMouseWeel", "l", static_cast<short>(HIWORD(wParam)));
        if (core.Controls)
            core.Controls->EngineMessage(iMsg, wParam, lParam);
        break;

    default:;
    }

    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}
