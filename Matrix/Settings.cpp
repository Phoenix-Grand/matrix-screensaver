
#include <windows.h>
#include <tchar.h>
#include <cstdio>

#include "message.h"
#include "matrix.h"

// Definitions for globals (linker needs exactly one TU to define these)
TCHAR szMessages[MAXMESSAGES][MAXMSGLEN];
int nNumMessages = 0;

extern TCHAR szAppName[];
extern int Density;
extern int MessageSpeed;
extern int MatrixSpeed;
extern int FontSize;
extern TCHAR szFontName[];
extern BOOL FontBold;
extern BOOL EnablePreviews;
extern BOOL RandomizeMessages;

// -------------------------------------------------------------------------------------------------
// Portable settings: write/read to "matrix-settings-portable.cfg".
// Prefer the executable directory if writable, otherwise use %APPDATA%\Matrix (no trailing backslash).
// Uses GetPrivateProfileString / WritePrivateProfileString to store values.
// -------------------------------------------------------------------------------------------------

static TCHAR g_cfgPath[MAX_PATH] = {0};

static BOOL DirIsWritable(LPCTSTR dir)
{
    // Try to create a tiny temp file in the target directory to verify write access.
    TCHAR testPath[MAX_PATH];
    lstrcpyn(testPath, dir, MAX_PATH);
    size_t len = lstrlen(testPath);
    if (len && testPath[len-1] != TEXT('\\'))
        lstrcat(testPath, TEXT("\\"));
    lstrcat(testPath, TEXT(".__matrix_write_test__.tmp"));

    HANDLE h = CreateFile(testPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (h == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD written = 0;
    WriteFile(h, "ok", 2, &written, NULL);
    CloseHandle(h);
    DeleteFile(testPath);
    return TRUE;
}

static void EnsureDirExists(LPCTSTR dir)
{
    CreateDirectory(dir, NULL); // no-op if already exists
}

static void ComputeConfigPath()
{
    if (g_cfgPath[0] != 0) return;

    // 1) Try executable folder first.
    TCHAR exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    // Remove filename to get directory.
    TCHAR exeDir[MAX_PATH];
    lstrcpyn(exeDir, exePath, MAX_PATH);
    for (int i = lstrlen(exeDir) - 1; i >= 0; --i) {
        if (exeDir[i] == TEXT('\\') || exeDir[i] == TEXT('/')) { exeDir[i] = 0; break; }
    }

    if (DirIsWritable(exeDir)) {
        lstrcpyn(g_cfgPath, exeDir, MAX_PATH);
        size_t len = lstrlen(g_cfgPath);
        if (len && g_cfgPath[len-1] != TEXT('\\'))
            lstrcat(g_cfgPath, TEXT("\\"));
        lstrcat(g_cfgPath, TEXT("matrix-settings-portable.cfg"));
        return;
    }

    // 2) Fallback to %APPDATA%\Matrix
    TCHAR appdata[MAX_PATH] = {0};
    DWORD got = GetEnvironmentVariable(TEXT("APPDATA"), appdata, MAX_PATH);
    if (got > 0 && got < MAX_PATH) {
        TCHAR matrixDir[MAX_PATH];
        lstrcpyn(matrixDir, appdata, MAX_PATH);
        size_t len2 = lstrlen(matrixDir);
        if (len2 && matrixDir[len2-1] != TEXT('\\'))
            lstrcat(matrixDir, TEXT("\\"));
        lstrcat(matrixDir, TEXT("Matrix"));
        EnsureDirExists(matrixDir);

        lstrcpyn(g_cfgPath, matrixDir, MAX_PATH);
        size_t len3 = lstrlen(g_cfgPath);
        if (len3 && g_cfgPath[len3-1] != TEXT('\\'))
            lstrcat(g_cfgPath, TEXT("\\"));
        lstrcat(g_cfgPath, TEXT("matrix-settings-portable.cfg"));
        return;
    }

    // 3) As a last resort, use the EXE directory path (writes may fail if not writable).
    lstrcpyn(g_cfgPath, exeDir, MAX_PATH);
    size_t len4 = lstrlen(g_cfgPath);
    if (len4 && g_cfgPath[len4-1] != TEXT('\\'))
        lstrcat(g_cfgPath, TEXT("\\"));
    lstrcat(g_cfgPath, TEXT("matrix-settings-portable.cfg"));
}

// INI helpers
static UINT INIGetInt(LPCTSTR section, LPCTSTR key, UINT defval)
{
    ComputeConfigPath();
    return GetPrivateProfileInt(section, key, defval, g_cfgPath);
}

static void INISetInt(LPCTSTR section, LPCTSTR key, UINT val)
{
    ComputeConfigPath();
    TCHAR buf[32];
    wsprintf(buf, TEXT("%u"), val);
    WritePrivateProfileString(section, key, buf, g_cfgPath);
}

static void INIGetString(LPCTSTR section, LPCTSTR key, LPTSTR out, DWORD outcch, LPCTSTR defval)
{
    ComputeConfigPath();
    GetPrivateProfileString(section, key, defval, out, outcch, g_cfgPath);
}

static void INISetString(LPCTSTR section, LPCTSTR key, LPCTSTR val)
{
    ComputeConfigPath();
    WritePrivateProfileString(section, key, val, g_cfgPath);
}

// Public API
void LoadSettings()
{
    // Integers
    MessageSpeed = INIGetInt(TEXT("Matrix"), TEXT("MessageSpeed"), MessageSpeed);
    MatrixSpeed  = INIGetInt(TEXT("Matrix"), TEXT("MatrixSpeed"),  MatrixSpeed);
    Density      = INIGetInt(TEXT("Matrix"), TEXT("Density"),      Density);
    FontSize     = INIGetInt(TEXT("Matrix"), TEXT("FontSize"),     FontSize);

    // Booleans
    EnablePreviews    = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("Previews"),   EnablePreviews ? 1 : 0);
    RandomizeMessages = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("Randomize"),  RandomizeMessages ? 1 : 0);
    FontBold          = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("FontBold"),   FontBold ? 1 : 0);

    // Strings
    INIGetString(TEXT("Matrix"), TEXT("FontName"), szFontName, MAX_PATH, szFontName);

    // Messages
    nNumMessages = (int)INIGetInt(TEXT("Messages"), TEXT("Count"), nNumMessages);
    if (nNumMessages < 0) nNumMessages = 0;
    if (nNumMessages > MAXMESSAGES) nNumMessages = MAXMESSAGES;

    for (int i = 0; i < nNumMessages; ++i) {
        TCHAR key[32];
        wsprintf(key, TEXT("Message%d"), i);
        INIGetString(TEXT("Messages"), key, szMessages[i], MAXMSGLEN, TEXT(""));
        szMessages[i][MAXMSGLEN-1] = 0;
    }
}

void SaveSettings()
{
    INISetInt(TEXT("Matrix"), TEXT("MessageSpeed"), (UINT)MessageSpeed);
    INISetInt(TEXT("Matrix"), TEXT("MatrixSpeed"),  (UINT)MatrixSpeed);
    INISetInt(TEXT("Matrix"), TEXT("Density"),      (UINT)Density);
    INISetInt(TEXT("Matrix"), TEXT("FontSize"),     (UINT)FontSize);

    INISetInt(TEXT("Matrix"), TEXT("Previews"),     EnablePreviews ? 1 : 0);
    INISetInt(TEXT("Matrix"), TEXT("Randomize"),    RandomizeMessages ? 1 : 0);
    INISetInt(TEXT("Matrix"), TEXT("FontBold"),     FontBold ? 1 : 0);

    INISetString(TEXT("Matrix"), TEXT("FontName"),  szFontName);

    // Messages
    INISetInt(TEXT("Messages"), TEXT("Count"), (UINT)nNumMessages);
    for (int i = 0; i < nNumMessages; ++i) {
        TCHAR key[32];
        wsprintf(key, TEXT("Message%d"), i);
        INISetString(TEXT("Messages"), key, szMessages[i]);
    }
}
