// matrix.cpp — portable settings + proper Configure dialog
// - Saves to matrix-settings-portable.cfg (exe folder if writable, else %APPDATA%\Matrix\)
// - Shows the settings path in the Configure window.

#include <windows.h>
#include <commctrl.h>   // trackbars, buttons, etc.
#include <tchar.h>
#include <stdio.h>
#include <shlobj.h>     // SHGetFolderPath, SHCreateDirectoryEx
#include "resource/resource.h"
#include "palette.h"
#include "message.h"
#include "matrix.h"

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "comctl32")
#pragma comment(lib, "shell32")

Message message;

TCHAR szAppName[] = _T("Matrix Screensaver");

HINSTANCE hInst;

RECT ScreenSize;

HPALETTE hPalette;
HDC hdcSymbols;
HBITMAP hSymbolBitmap;

// state for matrix
int dispx, dispy;
int maxrows, maxcols;
int numrows, numcols;
int xChar, yChar;

HFONT hfont;

int  MessageSpeed      = 150;   //
int  Density           = 32;    // 5..50
int  MatrixSpeed       = 5;     // 1..10
int  FontSize          = 12;    // 8..30
BOOL FontBold          = TRUE;
BOOL RandomizeMessages = FALSE;
TCHAR szFontName[512]  = _T("MS Sans Serif");

void LoadSettings(void);

LRESULT CALLBACK WndProc (HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam);

int  Configure(HWND hwndParent);
BOOL ChangePassword(HWND hwnd);
BOOL VerifyPassword(HWND hwnd);

bool fScreenSaving = false;

HDC     hdcMessage;
HBITMAP hBitmapMsg;

int jjrand(void)
{
    static unsigned short reg = (unsigned short)(GetTickCount() & 0xffff);
    unsigned short mask = 0xb400;

    if (reg & 1) reg = (reg >> 1) ^ mask;
    else         reg = (reg >> 1);

    return reg;
}

Matrix* matrix;

inline int INTENSITY(int n) { return (n < 0 ? -1 : n/32); }

// ===================== Portable settings (INI) with fallback =====================

static const TCHAR* kCfgFileName = _T("matrix-settings-portable.cfg");
static const TCHAR* kIniSection  = _T("Settings");
static TCHAR gCfgPath[MAX_PATH]  = {0};   // cache for UI display

static BOOL IsFolderWritable(const TCHAR* folder) {
    TCHAR testPath[MAX_PATH];
    lstrcpyn(testPath, folder, MAX_PATH);
    size_t len = lstrlen(testPath);
    if (len > 0 && testPath[len-1] != TEXT('\\')) {
        if (len + 1 < MAX_PATH) { testPath[len] = TEXT('\\'); testPath[len+1] = 0; len++; }
        else return FALSE;
    }
    if (len + 16 >= MAX_PATH) return FALSE;
    lstrcat(testPath, TEXT("~writetest.tmp"));
    HANDLE h = CreateFile(testPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    CloseHandle(h);
    DeleteFile(testPath);
    return TRUE;
}

static void EnsureDirExists(const TCHAR* folder) {
    SHCreateDirectoryEx(NULL, folder, NULL); // OK if already exists
}

static void GetConfigPath(TCHAR* outPath, size_t cchOut) {
    // 1) Try "<exe folder>\matrix-settings-portable.cfg"
    TCHAR exePath[MAX_PATH];
    DWORD n = GetModuleFileName(NULL, exePath, (DWORD)MAX_PATH);
    if (n && n < MAX_PATH) {
        // Trim to folder
        TCHAR* lastSlash = _tcsrchr(exePath, TEXT('\\'));
        if (lastSlash) {
            *(lastSlash + 1) = 0; // keep trailing backslash
            if (IsFolderWritable(exePath)) {
                lstrcpyn(outPath, exePath, (int)cchOut);
                if (lstrlen(outPath) + (int)lstrlen(kCfgFileName) + 1 < (int)cchOut) {
                    lstrcat(outPath, kCfgFileName);
                    return;
                }
            }
        }
    }

    // 2) Fallback: %APPDATA%\Matrix\matrix-settings-portable.cfg
    TCHAR appdata[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, appdata))) {
        // Build %APPDATA%\Matrix\
        TCHAR folder[MAX_PATH];
        lstrcpyn(folder, appdata, MAX_PATH);
        size_t len = lstrlen(folder);
        if (len > 0 && folder[len-1] != TEXT('\\')) {
            if (len + 1 < MAX_PATH) { folder[len] = TEXT('\\'); folder[len+1] = 0; len++; }
        }
        if (len + 7 < MAX_PATH) lstrcat(folder, TEXT("Matrix"));
        EnsureDirExists(folder);

        // Full file path
        lstrcpyn(outPath, folder, (int)cchOut);
        len = lstrlen(outPath);
        if (len > 0 && outPath[len-1] != TEXT('\\')) {
            if (len + 1 < (int)cchOut) { outPath[len] = TEXT('\\'); outPath[len+1] = 0; len++; }
        }
        if (len + (int)lstrlen(kCfgFileName) + 1 < (int)cchOut) {
            lstrcat(outPath, kCfgFileName);
            return;
        }
    }

    // 3) Last resort: current directory
    lstrcpyn(outPath, kCfgFileName, (int)cchOut);
}

static void ClampSettings() {
    if (Density     < 5)  Density     = 5;
    if (Density     > 50) Density     = 50;
    if (MatrixSpeed < 1)  MatrixSpeed = 1;
    if (MatrixSpeed > 10) MatrixSpeed = 10;
    if (FontSize    < 8)  FontSize    = 8;
    if (FontSize    > 30) FontSize    = 30;
}

void LoadSettings(void) {
    GetConfigPath(gCfgPath, MAX_PATH);

    MessageSpeed      = GetPrivateProfileInt(kIniSection, _T("MessageSpeed"),      MessageSpeed,      gCfgPath);
    Density           = GetPrivateProfileInt(kIniSection, _T("Density"),           Density,           gCfgPath);
    MatrixSpeed       = GetPrivateProfileInt(kIniSection, _T("MatrixSpeed"),       MatrixSpeed,       gCfgPath);
    FontSize          = GetPrivateProfileInt(kIniSection, _T("FontSize"),          FontSize,          gCfgPath);
    FontBold          = GetPrivateProfileInt(kIniSection, _T("FontBold"),          FontBold,          gCfgPath) ? TRUE : FALSE;
    RandomizeMessages = GetPrivateProfileInt(kIniSection, _T("RandomizeMessages"), RandomizeMessages, gCfgPath) ? TRUE : FALSE;

    GetPrivateProfileString(kIniSection, _T("FontName"), szFontName, szFontName,
                            (DWORD)(sizeof(szFontName)/sizeof(szFontName[0])), gCfgPath);

    ClampSettings();
}

static void SaveSettings(void) {
    // ints as strings
    TCHAR buf[32];

    _stprintf_s(buf, _T("%d"), MessageSpeed);      WritePrivateProfileString(kIniSection, _T("MessageSpeed"),      buf, gCfgPath);
    _stprintf_s(buf, _T("%d"), Density);           WritePrivateProfileString(kIniSection, _T("Density"),           buf, gCfgPath);
    _stprintf_s(buf, _T("%d"), MatrixSpeed);       WritePrivateProfileString(kIniSection, _T("MatrixSpeed"),       buf, gCfgPath);
    _stprintf_s(buf, _T("%d"), FontSize);          WritePrivateProfileString(kIniSection, _T("FontSize"),          buf, gCfgPath);
    _stprintf_s(buf, _T("%d"), FontBold ? 1 : 0);  WritePrivateProfileString(kIniSection, _T("FontBold"),          buf, gCfgPath);
    _stprintf_s(buf, _T("%d"), RandomizeMessages ? 1 : 0);
    WritePrivateProfileString(kIniSection, _T("RandomizeMessages"), buf, gCfgPath);

    // string
    WritePrivateProfileString(kIniSection, _T("FontName"), szFontName, gCfgPath);
}

// ===================== Matrix render code (unchanged) =====================

void Matrix::ScrollDown(HDC hdc)
{
    if (started == false) {
        if (--initcount <= 0) started = true;
        return;
    }

    for (int i = 0; i < numrows; i++) update[i] = false;

    int oldchar = state ? 127 : -1;

    for (int i = 0; i < numrows; i++) {
        int oldins = INTENSITY(oldchar);
        int runins = INTENSITY(run[i]);

        if (runins > oldins && runins >= 0) {
            run[i] -= 32;
            update[i] = true;
            if (runins == 3) i++;
        } else if (oldins >= 0 && runins < 0) {
            run[i] = jjrand() % 26 + 96;
            update[i] = true;
            i++;
        }
        oldchar = run[i];
    }

    if (--statecount <= 0) {
        state ^= 1;
        if (state == 0)  statecount = jjrand() % (DENSITY_MAX + 1 - Density) + (DENSITY_MIN * 2);
        else             statecount = jjrand() % (3 * Density / 2) + DENSITY_MIN;
    }

    if (blippos >= 0 && blippos < runlen) {
        update[blippos]   = true;
        update[blippos+1] = true;
        update[blippos+8] = true;
        update[blippos+9] = true;
    }

    blippos += 2;

    if (blippos >= bliplen) {
        bliplen = numrows + jjrand() % 50;
        blippos = 0;
    }

    if (blippos >= 0 && blippos < runlen) {
        update[blippos]   = true;
        update[blippos+1] = true;
        update[blippos+8] = true;
        update[blippos+9] = true;
    }
}

void DecodeMatrix(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);

    UseNicePalette(hdc, hPalette);
    SelectObject(hdc, hfont);
    SetBkColor(hdc, 0);

    for (int x = 0; x < numcols; x++) {
        matrix[x].jjrandomise();
        matrix[x].ScrollDown(hdc);

        for (int y = 0; y < numrows; y++) {
            if (!matrix[x].update[y]) continue;

            if (matrix[x].run[y] < 0) {
                RECT rect;
                SetRect(&rect,  x * xChar, y * yChar, (x + 1) * xChar, (y + 1) * yChar);
                ExtTextOut(hdc, x * xChar, y * yChar, ETO_OPAQUE, &rect, _T(""), 0, 0);
            } else {
                if (matrix[x].blippos == y  || matrix[x].blippos+1 == y
                 || matrix[x].blippos+8 == y || matrix[x].blippos+9 == y) {
                    TCHAR c = (TCHAR)(matrix[x].run[y] & 31);
                    BitBlt(hdc, x*xChar, y*yChar, 14, 14, hdcSymbols, c*14, 14*4, SRCCOPY);
                } else {
                    TCHAR c = (TCHAR)matrix[x].run[y];
                    int sx = c & 31;
                    int sy = c / 32;
                    BitBlt(hdc, x*xChar, y*yChar, 14, 14, hdcSymbols, sx*14, sy*14, SRCCOPY);
                }
            }
        }
    }

    DoMessages(hdc);
    ReleaseDC(hwnd, hdc);
}

void Matrix::jjrandomise()
{
    int p = 0;
    for (int i = 1; i < 20; i++) {
        while (run[p] < 96 && p < numrows) p++;
        if (p >= numrows) break;
        run[p] = jjrand() % 26 + 96;
        update[p] = true;
        p += jjrand() % 10;
    }
}

void InitMatrix(HWND hwnd)
{
    matrix = new Matrix[maxcols];
    for (int i = 0; i < maxcols; i++) matrix[i].Init(maxrows);
    SetTimer(hwnd, 0xDeadBeef, MatrixSpeed * 10, 0); // uses MatrixSpeed
}

// ===================== Normal app / saver plumbing =====================

int Normal(int iCmdShow)
{
    HWND hwnd;
    MSG  msg;
    WNDCLASSEX  wndclass;
    DWORD exStyle, style;
    HCURSOR hcurs;

    if (iCmdShow == SW_MAXIMIZE) {
        exStyle = WS_EX_TOPMOST;
        style   = WS_POPUP | WS_VISIBLE;
        hcurs   = LoadCursor(hInst, MAKEINTRESOURCE(IDC_BLANKCURSOR));
    } else {
        exStyle = WS_EX_CLIENTEDGE;
        style   = WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN;
        hcurs   = LoadCursor(NULL, IDC_ARROW);
    }

    wndclass.cbSize        = sizeof(wndclass);
    wndclass.style         = 0;
    wndclass.lpfnWndProc   = WndProc;
    wndclass.cbClsExtra    = 0;
    wndclass.cbWndExtra    = 0;
    wndclass.hInstance     = hInst;
    wndclass.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor       = hcurs;
    wndclass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndclass.lpszMenuName  = 0;
    wndclass.lpszClassName = szAppName;
    wndclass.hIconSm       = LoadIcon(NULL, IDI_APPLICATION);

    RegisterClassEx(&wndclass);

    InitMessage();

    hwnd = CreateWindowEx(exStyle, szAppName, szAppName, style,
                          CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                          NULL, NULL, hInst, NULL);

    ShowWindow(hwnd, iCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL,0,0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    DeInitMessage();
    return (int)msg.wParam;
}

int ScreenSave(void)
{
    UINT nPreviousState;
    fScreenSaving = true;

    SystemParametersInfo(SPI_SETSCREENSAVERRUNNING, TRUE,  &nPreviousState, 0);
    Normal(SW_MAXIMIZE);
    SystemParametersInfo(SPI_SETSCREENSAVERRUNNING, FALSE, &nPreviousState, 0);
    return 0;
}

BOOL GetCommandLineOption(PSTR szCmdLine, int *chOption, HWND *hwndParent)
{
    int ch = *szCmdLine++;
    if (ch == '-' || ch == '/') ch = *szCmdLine++;
    if (ch >= 'A' && ch <= 'Z') ch += 'a' - 'A';

    *chOption = ch;
    ch = *szCmdLine++;

    if (ch == ':') ch = *szCmdLine++;
    while (ch == ' ' || ch == '\t') ch = *szCmdLine++;

    if (isdigit(ch)) {
        unsigned int i = (unsigned int)atoi(szCmdLine-1);
        *hwndParent = (HWND)(UINT_PTR)i;
    } else {
        *hwndParent = NULL;
    }
    return TRUE;
}

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE, PSTR szCmdLine, int iCmdShow)
{
    int  chOption;
    HWND hwndParent;

    hInst = hInstance;
    (void)GetCommandLine();

    // Single-instance guard
    if (FindWindowEx(NULL, NULL, szAppName, szAppName)) return 0;

    SetRect(&ScreenSize, 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));

    xChar = 14; yChar = 14;
    maxcols = ScreenSize.right / xChar;
    maxrows = ScreenSize.bottom / yChar + 1;

    LoadSettings();

    GetCommandLineOption(szCmdLine, &chOption, &hwndParent);

    switch (chOption) {
        case 's': return ScreenSave();              // screen saver
        case 'p': return 0;                         // preview (shell-hosted)
        case 'a': return ChangePassword(hwndParent);
        case 'c': return Configure(hwndParent);     // proper config dialog
        default:  return Normal(iCmdShow);
    }
}

//-----------------------------------------------------------------------------
int main(void) {}

LRESULT CALLBACK WndProc (HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam)
{
    int i, j;
    HDC hdc;
    static HANDLE    holddc;
    static HPALETTE  holdpal;
    static bool      fHere = false;
    static POINT     ptLast;
    POINT            ptCursor, ptCheck;
    static LARGE_INTEGER freq;
    LARGE_INTEGER    pc1, pc2;
    static DWORD     median;
    static int       fpscount;

    switch (iMsg)
    {
    case WM_CREATE:
        hdc = GetDC(hwnd);

        holdpal = UseNicePalette(hdc, hPalette);
        hdcSymbols = CreateCompatibleDC(hdc);

        // load bitmap as a DDB, with palette!
        hPalette = ReadBMPPalette(hInst, hdc, MAKEINTRESOURCE(IDB_BITMAP1));
        extern HBITMAP hDDB;
        hSymbolBitmap = hDDB;

        holddc = (HANDLE)SelectObject(hdcSymbols, hSymbolBitmap);

        ReleaseDC(hwnd, hdc);

        InitMatrix(hwnd);
        i = QueryPerformanceFrequency(&freq);

        if (fScreenSaving) SetCursor(NULL);
        return 0;

    case WM_SIZE:
        numcols = (short)LOWORD(lParam) / xChar + 1;
        numrows = (short)HIWORD(lParam) / yChar + 1;

        if (numrows <= 0 || numrows >= maxrows) numrows = maxrows - 1;
        if (numcols <= 0 || numcols >= maxcols) numcols = maxcols - 1;

        for (i = numcols; i < maxcols; i++) {
            matrix[i].started   = false;
            matrix[i].initcount = jjrand() % 20;
            matrix[i].blippos   = jjrand() % numrows;
            for (j = 0; j < numrows; j++) matrix[i].run[j] = -1;
        }
        return 0;

    case WM_TIMER:
        {
            if (!fScreenSaving) QueryPerformanceCounter(&pc1);

            DecodeMatrix(hwnd);

            if (!fScreenSaving) {
                QueryPerformanceCounter(&pc2);
                TCHAR buf[64];
                median += DWORD(DWORD(freq.QuadPart) / DWORD(pc2.QuadPart - pc1.QuadPart));
                if (++fpscount == 16) {
                    wsprintf(buf, _T("%s - %u FPS"), szAppName, median / 16);
                    SetWindowText(hwnd, buf);
                    median = 0; fpscount = 0;
                }
            }
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, 0xdeadbeef);

        SelectObject(hdcSymbols, holddc);
        SelectPalette(hdcSymbols, holdpal, FALSE);
        DeleteDC    (hdcSymbols);
        DeleteObject(hSymbolBitmap);
        DeleteObject(hPalette);

        PostQuitMessage(0);
        return 0;

    case WM_CLOSE:
        if (fScreenSaving && VerifyPassword(hwnd) || !fScreenSaving)
            DestroyWindow(hwnd);
        return 0;

    case WM_ACTIVATEAPP:
    case WM_ACTIVATE:
        if (wParam != FALSE) break;

    case WM_MOUSEMOVE:
        if (!fScreenSaving) return 0;
        if (!fHere) {
            GetCursorPos(&ptLast);
            fHere = true;
        } else {
            GetCursorPos(&ptCheck);
            if (ptCursor.x = ptCheck.x - ptLast.x) { if (ptCursor.x < 0) ptCursor.x *= -1; }
            if (ptCursor.y = ptCheck.y - ptLast.y) { if (ptCursor.y < 0) ptCursor.y *= -1; }
            if ((ptCursor.x + ptCursor.y) > 3) PostMessage(hwnd, WM_CLOSE, 0, 0l);
        }
        break;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        if (!fScreenSaving) return 0;
        GetCursorPos(&ptCursor);
        ptCursor.x++; ptCursor.y++; SetCursorPos(ptCursor.x, ptCursor.y);
        GetCursorPos(&ptCheck);
        if (ptCheck.x != ptCursor.x && ptCheck.y != ptCursor.y) ptCursor.x -= 2;
        ptCursor.y -= 2; SetCursorPos(ptCursor.x, ptCursor.y);
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        PostMessage(hwnd, WM_CLOSE, 0, 0l);
        break;
    }
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

// ===================== Configure dialog (no .rc; shows path) =====================

#define IDC_DENSITY     2001
#define IDC_MSPEED      2002
#define IDC_FONTSIZE    2003
#define IDC_FONTBOLD    2004
#define IDC_RANDOMMSG   2005
#define IDC_FONTNAME    2006
#define IDC_CFGPATH     2007

static void ReadControlsIntoGlobals(HWND h)
{
    Density           = (int)SendDlgItemMessage(h, IDC_DENSITY,  TBM_GETPOS, 0, 0);
    MatrixSpeed       = (int)SendDlgItemMessage(h, IDC_MSPEED,   TBM_GETPOS, 0, 0);
    FontSize          = (int)SendDlgItemMessage(h, IDC_FONTSIZE, TBM_GETPOS, 0, 0);
    FontBold          = (IsDlgButtonChecked(h, IDC_FONTBOLD)   == BST_CHECKED);
    RandomizeMessages = (IsDlgButtonChecked(h, IDC_RANDOMMSG)  == BST_CHECKED);
    GetDlgItemText(h, IDC_FONTNAME, szFontName, (int)(sizeof(szFontName)/sizeof(szFontName[0])));
    ClampSettings();
}

static void WriteGlobalsIntoControls(HWND h)
{
    SendDlgItemMessage(h, IDC_DENSITY,  TBM_SETRANGE, TRUE, MAKELPARAM(5,50));
    SendDlgItemMessage(h, IDC_MSPEED,   TBM_SETRANGE, TRUE, MAKELPARAM(1,10));
    SendDlgItemMessage(h, IDC_FONTSIZE, TBM_SETRANGE, TRUE, MAKELPARAM(8,30));

    SendDlgItemMessage(h, IDC_DENSITY,  TBM_SETPOS, TRUE, Density);
    SendDlgItemMessage(h, IDC_MSPEED,   TBM_SETPOS, TRUE, MatrixSpeed);
    SendDlgItemMessage(h, IDC_FONTSIZE, TBM_SETPOS, TRUE, FontSize);

    CheckDlgButton(h, IDC_FONTBOLD,   FontBold ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(h, IDC_RANDOMMSG,  RandomizeMessages ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemText(h, IDC_FONTNAME, szFontName);

    // show the settings path
    SetDlgItemText(h, IDC_CFGPATH, gCfgPath);
}

static void CreateConfigChildren(HWND hDlg)
{
    RECT rc; GetClientRect(hDlg, &rc);
    int w = rc.right - rc.left;
    int xL = 12;
    int xR = 120;
    int y  = 12;

    CreateWindowEx(0, WC_STATIC, _T("Density"),
        WS_CHILD|WS_VISIBLE, xL, y, 100, 18, hDlg, 0, hInst, 0); y += 18;
    CreateWindowEx(0, TRACKBAR_CLASS, _T(""),
        WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS, xL, y, w-24, 32, hDlg, (HMENU)IDC_DENSITY, hInst, 0); y += 40;

    CreateWindowEx(0, WC_STATIC, _T("Matrix speed"),
        WS_CHILD|WS_VISIBLE, xL, y, 100, 18, hDlg, 0, hInst, 0); y += 18;
    CreateWindowEx(0, TRACKBAR_CLASS, _T(""),
        WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS, xL, y, w-24, 32, hDlg, (HMENU)IDC_MSPEED, hInst, 0); y += 40;

    CreateWindowEx(0, WC_STATIC, _T("Font size"),
        WS_CHILD|WS_VISIBLE, xL, y, 100, 18, hDlg, 0, hInst, 0); y += 18;
    CreateWindowEx(0, TRACKBAR_CLASS, _T(""),
        WS_CHILD|WS_VISIBLE|TBS_AUTOTICKS, xL, y, w-24, 32, hDlg, (HMENU)IDC_FONTSIZE, hInst, 0); y += 40;

    CreateWindowEx(0, WC_BUTTON, _T("Bold font"),
        WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, xL, y, 200, 20, hDlg, (HMENU)IDC_FONTBOLD, hInst, 0); y += 24;

    CreateWindowEx(0, WC_BUTTON, _T("Randomize messages"),
        WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX, xL, y, 200, 20, hDlg, (HMENU)IDC_RANDOMMSG, hInst, 0); y += 28;

    CreateWindowEx(0, WC_STATIC, _T("Font name"),
        WS_CHILD|WS_VISIBLE, xL, y, 100, 18, hDlg, 0, hInst, 0);
    CreateWindowEx(WS_EX_CLIENTEDGE, WC_EDIT, szFontName,
        WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL, xR, y, w - xR - 12, 22, hDlg, (HMENU)IDC_FONTNAME, hInst, 0);
    y += 30;

    // Config file path label (multi-line-friendly STATIC)
    CreateWindowEx(0, WC_STATIC, _T("Config file:"),
        WS_CHILD|WS_VISIBLE, xL, rc.bottom - 60, 100, 18, hDlg, 0, hInst, 0);
    CreateWindowEx(WS_EX_CLIENTEDGE, WC_STATIC, gCfgPath,
        WS_CHILD|WS_VISIBLE|SS_LEFT, xR, rc.bottom - 64, w - xR - 12, 24, hDlg, (HMENU)IDC_CFGPATH, hInst, 0);

    // OK / Cancel
    CreateWindowEx(0, WC_BUTTON, _T("OK"),
        WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON, w-180, rc.bottom-36, 80, 24, hDlg, (HMENU)IDOK, hInst, 0);
    CreateWindowEx(0, WC_BUTTON, _T("Cancel"),
        WS_CHILD|WS_VISIBLE, w-92, rc.bottom-36, 80, 24, hDlg, (HMENU)IDCANCEL, hInst, 0);
}

static INT_PTR CALLBACK CfgDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_BAR_CLASSES|ICC_STANDARD_CLASSES };
        InitCommonControlsEx(&icc);
        // refresh gCfgPath (in case location changed due to permissions)
        GetConfigPath(gCfgPath, MAX_PATH);
        CreateConfigChildren(hDlg);
        WriteGlobalsIntoControls(hDlg);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam)) {
            case IDOK:
                ReadControlsIntoGlobals(hDlg);
                SaveSettings();
                DestroyWindow(hDlg);
                return 0;
            case IDCANCEL:
                DestroyWindow(hDlg);
                return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hDlg);
        return 0;
    }
    return DefWindowProc(hDlg, msg, wParam, lParam);
}

int Configure(HWND hwndParent)
{
    WNDCLASS wc{};
    wc.lpfnWndProc   = CfgDlgProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_3DFACE+1);
    wc.lpszClassName = _T("MatrixCfgWnd");
    RegisterClass(&wc);

    int W = 560, H = 340;
    HWND h = CreateWindowEx(WS_EX_DLGMODALFRAME, wc.lpszClassName, _T("Matrix Settings (portable)"),
                            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                            CW_USEDEFAULT, CW_USEDEFAULT, W, H,
                            hwndParent, NULL, hInst, NULL);

    // center
    RECT rc; GetWindowRect(h, &rc);
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    int x = (sw - (rc.right-rc.left))/2;
    int y = (sh - (rc.bottom-rc.top))/2;
    SetWindowPos(h, NULL, x, y, 0, 0, SWP_NOSIZE|SWP_NOZORDER);

    ShowWindow(h, SW_SHOW);
    UpdateWindow(h);

    MSG msg;
    while (IsWindow(h) && GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(h, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    UnregisterClass(wc.lpszClassName, hInst);
    return 0;
}
