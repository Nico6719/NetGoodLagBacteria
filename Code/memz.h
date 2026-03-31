// If this is defined, the trojan will disable all destructive payloads
// and does display a GUI to manually control all of the non-destructive ones.
#define CLEAN

#ifdef CLEAN
// XP styles and DPI awareness are now handled via app.manifest + resource.rc

// Window attributes
#define BTNWIDTH 200
#define BTNHEIGHT 30
#define COLUMNS 2
#define ROWS nPayloads/COLUMNS
#define SPACE 10
#define STARTALL_HEIGHT 36
#define COMBO_HEIGHT 24
#define LABEL_HEIGHT 16
#define WINDOWWIDTH COLUMNS * BTNWIDTH + (COLUMNS + 1)*SPACE
#define WINDOWHEIGHT ROWS * BTNHEIGHT + (ROWS + 1)*SPACE + 32 + STARTALL_HEIGHT + SPACE + LABEL_HEIGHT + COMBO_HEIGHT + SPACE*2
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
#endif

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

// Target screen: virtual screen coordinates (for window positioning / msgbox)
extern int scrx, scry, scrw, scrh;

// Per-monitor DC: uses the monitor's own device name so coordinates are
// always (0,0) = top-left of that monitor, regardless of DPI or position.
// srcx/srcy are the virtual-screen offsets used only for window placement.
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
