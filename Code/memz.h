// If this is defined, the trojan will disable all destructive payloads
// and does display a GUI to manually control all of the non-destructive ones.
#define CLEAN

#ifdef CLEAN
// Metro UI layout — computed at runtime in main()
extern int M_ROWS;
extern int M_WINH;
#define M_COLS   2
#define M_BTNW   210
#define M_BTNH   36
#define M_SP     8
#define M_TITLEH 52
#define M_STARTALLH 40
#define M_COMBOH 28
#define M_LABELH 18
#define M_STATUSH 56
#define M_WINW   (M_COLS * M_BTNW + (M_COLS+1) * M_SP)
// Legacy aliases used by payloads.cpp
#define WINDOWWIDTH  M_WINW
#define WINDOWHEIGHT M_WINH
#endif

#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <shlwapi.h>
#include <psapi.h>
#include <commctrl.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <ole2.h>
#include <objbase.h>

#include "data.h"
#include "payloads.h"

int random();
void strReverseW(LPWSTR str);

DWORD WINAPI payloadThread(LPVOID);

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

#ifndef CLEAN
void killWindows();
void killWindowsInstant();

DWORD WINAPI ripMessageThread(LPVOID);
DWORD WINAPI watchdogThread(LPVOID);
#else
DWORD WINAPI keyboardThread(LPVOID lParam);
extern BOOLEAN enablePayloads;
BOOL CALLBACK CleanWindowsProc(HWND hwnd, LPARAM lParam);
void closeAllPopups();
#endif

// ── Original payloads ────────────────────────────────────────────────────────
int payloadExecute(PAYLOADFUNC);
int payloadCursor(PAYLOADFUNC);
int payloadBlink(PAYLOADFUNC);
int payloadMessageBox(PAYLOADFUNC);
DWORD WINAPI messageBoxThread(LPVOID);
LRESULT CALLBACK msgBoxHook(int, WPARAM, LPARAM);
int payloadChangeText(PAYLOADFUNC);
BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam);
int payloadSound(PAYLOADFUNC);
int payloadPuzzle(PAYLOADFUNC);
int payloadKeyboard(PAYLOADFUNC);
int payloadPIP(PAYLOADFUNC);
int payloadDrawErrors(PAYLOADFUNC);

// ── Suierku-style new payloads ───────────────────────────────────────────────
int payloadGreenStripes(PAYLOADFUNC);   // 苏尔克绿色横纹
int payloadScreenJump(PAYLOADFUNC);     // 屏幕跳动/滚动
int payloadScanCorrupt(PAYLOADFUNC);    // 扫描线腐蚀
int payloadIconStorm(PAYLOADFUNC);      // 图标风暴
int payloadGlitchBlocks(PAYLOADFUNC);   // 数字故障块
int payloadColorInvert(PAYLOADFUNC);    // 区域彩色反转
int payloadTextRain(PAYLOADFUNC);       // 数字雨幕
int payloadScreenShatter(PAYLOADFUNC);  // 屏幕碎裂

// Target screen: virtual screen coordinates (for window positioning / msgbox)
extern int scrx, scry, scrw, scrh;

// Global intensity ramp
void  initGlobalIntensity();
float gIntensity();
float payloadStrength(int times, int runtime);

// Per-monitor DC
HDC getTargetDC();
void releaseTargetDC(HDC hdc);

// Audio beat/amplitude level 0.0 - 1.0, updated by audio thread
extern volatile float gAudioLevel;
// Audio thread
DWORD WINAPI audioThread(LPVOID lParam);

#ifdef CLEAN
extern HWND btnStartAll;
extern HWND comboMonitor;
#define IDC_STARTALL 9999
#define IDC_COMBO_MONITOR 9998
#undef MAX_MONITORS
#define MAX_MONITORS 16

typedef struct {
    RECT   rect;
    WCHAR  name[64];
    WCHAR  deviceName[32]; // e.g. "\\\\.\\DISPLAY1"
    int    index;
    BOOL   primary;        // TRUE if this is the primary monitor
} MONINFO;

extern MONINFO monitors[];
extern int nMonitors;

void updateTargetScreen(int idx);
void setTargetDeviceName(int idx);
void refreshMonitorList();
void autoSelectMonitorForWindow(HWND hwnd);
int getTargetMonitorIdx();
BOOL CALLBACK MonitorEnumProc(HMONITOR hMon, HDC hdcMon, LPRECT lprcMon, LPARAM dwData);
#endif
