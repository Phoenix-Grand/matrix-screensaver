// Matrix/Settings.cpp  — portable settings via INI-style .cfg with comment preservation
// Config file: matrix-settings-portable.cfg
// Preferred location: EXE folder if writable; otherwise %APPDATA%\Matrix-ScreenSaver\
// NOTE: MatrixSpeed is stored with intuitive semantics (1 = slow, 10 = fast).
//       Internally the legacy engine expects the opposite; we map on load/save.

#include <windows.h>
#include <tchar.h>
#include <cstdio>

#include "message.h"
#include "matrix.h"

// ------------------------------------------------------------------------------------
// Globals (linker-visible)
TCHAR szMessages[MAXMESSAGES][MAXMSGLEN];
int   nNumMessages = 0;

// Existing globals from the project
extern TCHAR szAppName[];
extern int Density;
extern int MessageSpeed;
extern int MatrixSpeed;   // legacy: smaller == faster (we remap at load/save)
extern int FontSize;
extern TCHAR szFontName[];
extern BOOL FontBold;
extern BOOL EnablePreviews;
extern BOOL RandomizeMessages;

// ------------------------------------------------------------------------------------
// Portable settings config path handling
//   File name: matrix-settings-portable.cfg
//   Location:  EXE folder if writable, else %APPDATA%\Matrix-ScreenSaver\
// ------------------------------------------------------------------------------------

static TCHAR g_cfgPath[MAX_PATH] = {0};

static BOOL DirIsWritable(LPCTSTR dir)
{
    // Try to create/open a temp file to test writability.
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
    CreateDirectory(dir, NULL); // no-op if exists
}

static void ComputeConfigPath()
{
    if (g_cfgPath[0] != 0) return;

    // 1) Prefer the executable directory
    TCHAR exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    // Strip filename to get directory
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

    // 2) Fallback to %APPDATA%\Matrix-ScreenSaver
    TCHAR appdata[MAX_PATH] = {0};
    DWORD got = GetEnvironmentVariable(TEXT("APPDATA"), appdata, MAX_PATH);
    if (got > 0 && got < MAX_PATH) {
        TCHAR matrixDir[MAX_PATH];
        lstrcpyn(matrixDir, appdata, MAX_PATH);
        size_t len2 = lstrlen(matrixDir);
        if (len2 && matrixDir[len2-1] != TEXT('\\'))
            lstrcat(matrixDir, TEXT("\\"));
        lstrcat(matrixDir, TEXT("Matrix-ScreenSaver"));
        EnsureDirExists(matrixDir);

        lstrcpyn(g_cfgPath, matrixDir, MAX_PATH);
        size_t len3 = lstrlen(g_cfgPath);
        if (len3 && g_cfgPath[len3-1] != TEXT('\\'))
            lstrcat(g_cfgPath, TEXT("\\"));
        lstrcat(g_cfgPath, TEXT("matrix-settings-portable.cfg"));
        return;
    }

    // 3) Last resort: next to the EXE even if not writable
    lstrcpyn(g_cfgPath, exeDir, MAX_PATH);
    size_t len4 = lstrlen(g_cfgPath);
    if (len4 && g_cfgPath[len-1] != TEXT('\\'))
        lstrcat(g_cfgPath, TEXT("\\"));
    lstrcat(g_cfgPath, TEXT("matrix-settings-portable.cfg"));
}

// ------------------------------------------------------------------------------------
static UINT INIGetInt(LPCTSTR section, LPCTSTR key, UINT defval)
{
    ComputeConfigPath();
    return GetPrivateProfileInt(section, key, defval, g_cfgPath);
}

static void INIGetString(LPCTSTR section, LPCTSTR key, LPTSTR out, DWORD outcch, LPCTSTR defval)
{
    ComputeConfigPath();
    GetPrivateProfileString(section, key, defval, out, outcch, g_cfgPath);
}

// --- Custom writer that preserves comments & unrelated lines ---

static BOOL WriteWholeTextFile(LPCTSTR path, const TCHAR* text)
{
#ifdef UNICODE
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    WORD bom = 0xFEFF;
    DWORD wrote = 0;
    WriteFile(h, &bom, sizeof(bom), &wrote, NULL);
    size_t len = lstrlen(text);
    WriteFile(h, text, (DWORD)(len * sizeof(TCHAR)), &wrote, NULL);
    CloseHandle(h);
    return TRUE;
#else
    HANDLE h = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD wrote = 0;
    size_t len = lstrlen(text);
    WriteFile(h, text, (DWORD)len, &wrote, NULL);
    CloseHandle(h);
    return TRUE;
#endif
}

static BOOL ReadWholeTextFile(LPCTSTR path, LPTSTR buffer, DWORD cchBuffer, DWORD* outLenChars)
{
    HANDLE h = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return FALSE;
    DWORD sizeBytes = GetFileSize(h, NULL);
    if (sizeBytes == INVALID_FILE_SIZE) { CloseHandle(h); return FALSE; }

#ifdef UNICODE
    DWORD read = 0;
    BYTE* tmp = (BYTE*)LocalAlloc(LMEM_FIXED, sizeBytes + 2);
    if (!tmp) { CloseHandle(h); return FALSE; }
    ReadFile(h, tmp, sizeBytes, &read, NULL);
    CloseHandle(h);

    if (read >= 2 && tmp[0] == 0xFF && tmp[1] == 0xFE) {
        DWORD charsAvail = (read - 2) / 2;
        if (charsAvail >= cchBuffer) charsAvail = cchBuffer - 1;
        memcpy(buffer, tmp + 2, charsAvail * sizeof(TCHAR));
        buffer[charsAvail] = 0;
        if (outLenChars) *outLenChars = charsAvail;
        LocalFree(tmp);
        return TRUE;
    } else {
        int need = MultiByteToWideChar(CP_ACP, 0, (LPCSTR)tmp, read, NULL, 0);
        if (need >= (int)cchBuffer) need = (int)cchBuffer - 1;
        MultiByteToWideChar(CP_ACP, 0, (LPCSTR)tmp, read, buffer, need);
        buffer[need] = 0;
        if (outLenChars) *outLenChars = need;
        LocalFree(tmp);
        return TRUE;
    }
#else
    DWORD read = 0;
    if (sizeBytes >= cchBuffer) sizeBytes = cchBuffer - 1;
    ReadFile(h, buffer, sizeBytes, &read, NULL);
    CloseHandle(h);
    buffer[read] = 0;
    if (outLenChars) *outLenChars = read;
    return TRUE;
#endif
}

static BOOL EnsureSectionExistsAndSetKey(LPTSTR text, DWORD cchText, LPCTSTR section, LPCTSTR key, LPCTSTR value)
{
    TCHAR secHdr[128];
    wsprintf(secHdr, TEXT("[%s]"), section);

    LPTSTR p = text;
    LPTSTR secStart = NULL;
    while (*p) {
        LPTSTR line = p;
        while (*p && *p != TEXT('\r') && *p != TEXT('\n')) p++;
        LPTSTR next = p;
        if (*p == TEXT('\r')) { p++; if (*p == TEXT('\n')) p++; }
        else if (*p == TEXT('\n')) { p++; }

        while (*line == TEXT(' ') || *line == TEXT('\t')) line++;
        if (line[0] == TEXT('[')) {
            if (_tcsnicmp(line, secHdr, lstrlen(secHdr)) == 0) {
                secStart = next;
                break;
            }
        }
    }

    if (!secStart) {
        size_t curLen = lstrlen(text);
        size_t addLen = lstrlen(secHdr) + lstrlen(TEXT("\r\n\r\n")) + 1;
        if (curLen + addLen >= cchText) return FALSE;
        if (curLen > 0 && text[curLen-1] != TEXT('\n')) lstrcat(text, TEXT("\r\n"));
        lstrcat(text, secHdr);
        lstrcat(text, TEXT("\r\n"));
        lstrcat(text, TEXT("\r\n"));
        secStart = text + lstrlen(text);
    }

    LPTSTR q = secStart;
    LPTSTR sectionEnd = q;
    while (*q) {
        LPTSTR line = q;
        while (*q && *q != TEXT('\r') && *q != TEXT('\n')) q++;
        LPTSTR next = q;
        if (*q == TEXT('\r')) { q++; if (*q == TEXT('\n')) q++; }
        else if (*q == TEXT('\n')) { q++; }

        LPTSTR t = line;
        while (*t == TEXT(' ') || *t == TEXT('\t')) t++;
        if (*t == TEXT('[')) { sectionEnd = line; break; }
        if (!*q) { sectionEnd = q; break; }
    }
    if (!*q) sectionEnd = q;

    TCHAR keyEq[256];
    wsprintf(keyEq, TEXT("%s="), key);

    LPTSTR pos = secStart;
    while (pos < sectionEnd) {
        LPTSTR line = pos;
        while (pos < sectionEnd && *pos != TEXT('\r') && *pos != TEXT('\n')) pos++;
        LPTSTR next = pos;
        if (pos < sectionEnd && *pos == TEXT('\r')) { pos++; if (pos < sectionEnd && *pos == TEXT('\n')) pos++; }
        else if (pos < sectionEnd && *pos == TEXT('\n')) { pos++; }

        LPTSTR t = line;
        while (*t == TEXT(' ') || *t == TEXT('\t')) t++;
        if (*t == TEXT(';') || *t == 0) continue;
        if (_tcsnicmp(t, keyEq, lstrlen(keyEq)) == 0) {
            TCHAR newLine[1024];
            wsprintf(newLine, TEXT("%s=%s"), key, value);
            size_t newLen = lstrlen(newLine);
            size_t oldLen = (size_t)(next - line);
            size_t tailLen = lstrlen(next);

            if (newLen + 2 + (line - text) + tailLen + 1 >= cchText) return FALSE;

            memmove(line + newLen, next, (tailLen + 1) * sizeof(TCHAR));
            memcpy(line, newLine, newLen * sizeof(TCHAR));
            return TRUE;
        }
    }

    TCHAR newLine[1024];
    wsprintf(newLine, TEXT("%s=%s\r\n"), key, value);
    size_t insLen = lstrlen(newLine);
    size_t headLen = (size_t)(sectionEnd - text);
    size_t curTotal = lstrlen(text);

    if (curTotal + insLen + 1 >= cchText) return FALSE;

    memmove(sectionEnd + insLen, sectionEnd, (curTotal - headLen + 1) * sizeof(TCHAR));
    memcpy(sectionEnd, newLine, insLen * sizeof(TCHAR));
    return TRUE;
}

static BOOL INISetString_Preserve(LPCTSTR section, LPCTSTR key, LPCTSTR val)
{
    ComputeConfigPath();
    const DWORD BUFSZ = 64 * 1024; // 64KB
    LPTSTR buf = (LPTSTR)LocalAlloc(LMEM_FIXED, BUFSZ * sizeof(TCHAR));
    if (!buf) return FALSE;

    DWORD outLen = 0;
    buf[0] = 0;
    ReadWholeTextFile(g_cfgPath, buf, BUFSZ, &outLen);

    BOOL ok = EnsureSectionExistsAndSetKey(buf, BUFSZ, section, key, val);
    if (ok) ok = WriteWholeTextFile(g_cfgPath, buf);

    LocalFree(buf);
    return ok;
}

static BOOL INISetInt_Preserve(LPCTSTR section, LPCTSTR key, UINT val)
{
    TCHAR tmp[32];
    wsprintf(tmp, TEXT("%u"), val);
    return INISetString_Preserve(section, key, tmp);
}

// ------------------------------------------------------------------------------------
// First-run template generator (with comments) 
// ------------------------------------------------------------------------------------

static void EnsureTemplateCfgExists()
{
    ComputeConfigPath();
    DWORD attrs = GetFileAttributes(g_cfgPath);
    if (attrs != INVALID_FILE_ATTRIBUTES) return; // already exists

    // Build directory if needed (strip filename to get directory)
    TCHAR pathCopy[MAX_PATH];
    lstrcpyn(pathCopy, g_cfgPath, MAX_PATH);
    for (int i = lstrlen(pathCopy) - 1; i >= 0; --i) {
        if (pathCopy[i] == TEXT('\\') || pathCopy[i] == TEXT('/')) { pathCopy[i] = 0; break; }
    }
    if (pathCopy[0]) EnsureDirExists(pathCopy);

    const TCHAR* tpl =
        TEXT("; Matrix portable config") TEXT("\r\n")
        TEXT("; Comments here will remain if keys are updated") TEXT("\r\n")
        TEXT("\r\n")
        TEXT("[Matrix]") TEXT("\r\n")
        TEXT("FontName=MS Sans Serif") TEXT("\r\n")
        TEXT("FontBold=1") TEXT("\r\n")
        TEXT("Randomize=0") TEXT("\r\n")
        TEXT("Previews=1") TEXT("\r\n")
        TEXT("FontSize=12") TEXT("\r\n")
        TEXT("Density=32") TEXT("\r\n")
        TEXT("MatrixSpeed=10") TEXT("\r\n") // intuitive default: 10 = fast
        TEXT("MessageSpeed=150") TEXT("\r\n")
        TEXT("\r\n")
        TEXT("[Messages]") TEXT("\r\n")
        TEXT("Count=1") TEXT("\r\n")
        TEXT("Message0=Hey, Fellow") TEXT("\r\n")
        TEXT("\r\n")
        TEXT("; Additional notes and guidance") TEXT("\r\n")
        TEXT("[Notes]") TEXT("\r\n")
        TEXT("; nothing in this section is read/written") TEXT("\r\n")
        TEXT("; ") TEXT("\r\n")
        TEXT("; FontName=MS Sans Serif") TEXT("\r\n")
        TEXT("; FontBold=1  1/0 True/False") TEXT("\r\n")
        TEXT("; Randomize=0  \"0\" displays your messages in saved order 0,1,2,... (not random)") TEXT("\r\n")
        TEXT("; Previews=1") TEXT("\r\n")
        TEXT("; FontSize=12") TEXT("\r\n")
        TEXT("; Density=32") TEXT("\r\n")
        TEXT("; MatrixSpeed=  animation speed (1 = slow, 10 = fast)") TEXT("\r\n")
        TEXT("; MessageSpeed= how fast messages burn in (50..500)") TEXT("\r\n")
        TEXT("; ") TEXT("\r\n");

    WriteWholeTextFile(g_cfgPath, tpl);
}

// Helper: clamp to [1,10]
static int ClampSpeed(int v) { if (v < 1) return 1; if (v > 10) return 10; return v; }

// Map between stored (intuitive) and internal (legacy) scales
static int StoredToInternalSpeed(int stored) { stored = ClampSpeed(stored); return 11 - stored; }
static int InternalToStoredSpeed(int internal) { internal = ClampSpeed(internal); return 11 - internal; }

// ------------------------------------------------------------------------------------
// Settings I/O
// ------------------------------------------------------------------------------------

void LoadSettings()
{
    EnsureTemplateCfgExists(); // create commented template on first run

    // Load with defaults (defensive: keep current values if missing)
    int storedMatrixSpeed = INIGetInt(TEXT("Matrix"), TEXT("MatrixSpeed"), InternalToStoredSpeed(MatrixSpeed));
    MatrixSpeed  = StoredToInternalSpeed(storedMatrixSpeed);

    MessageSpeed = INIGetInt(TEXT("Matrix"), TEXT("MessageSpeed"), MessageSpeed);
    Density      = INIGetInt(TEXT("Matrix"), TEXT("Density"),      Density);
    FontSize     = INIGetInt(TEXT("Matrix"), TEXT("FontSize"),     FontSize);

    EnablePreviews    = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("Previews"),   EnablePreviews ? 1 : 0);
    RandomizeMessages = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("Randomize"),  RandomizeMessages ? 1 : 0);
    FontBold          = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("FontBold"),   FontBold ? 1 : 0);

    INIGetString(TEXT("Matrix"), TEXT("FontName"), szFontName, MAX_PATH, szFontName);

    // Messages
    nNumMessages = (int)INIGetInt(TEXT("Messages"), TEXT("Count"), nNumMessages);
    if (nNumMessages < 0) nNumMessages = 0;
    if (nNumMessages > MAXMESSAGES) nNumMessages = MAXMESSAGES;

    for (int i = 0; i < nNumMessages; ++i)
    {
        TCHAR key[32];
        wsprintf(key, TEXT("Message%d"), i);
        INIGetString(TEXT("Messages"), key, szMessages[i], MAXMSGLEN, TEXT(""));
        szMessages[i][MAXMSGLEN-1] = 0;
    }
}

void SaveSettings()
{
    // Persist intuitive value (1 = slow, 10 = fast)
    INISetInt_Preserve(TEXT("Matrix"), TEXT("MatrixSpeed"), (UINT)InternalToStoredSpeed(MatrixSpeed));

    INISetInt_Preserve(TEXT("Matrix"), TEXT("MessageSpeed"), (UINT)MessageSpeed);
    INISetInt_Preserve(TEXT("Matrix"), TEXT("Density"),      (UINT)Density);
    INISetInt_Preserve(TEXT("Matrix"), TEXT("FontSize"),     (UINT)FontSize);

    INISetInt_Preserve(TEXT("Matrix"), TEXT("Previews"),     EnablePreviews ? 1 : 0);
    INISetInt_Preserve(TEXT("Matrix"), TEXT("Randomize"),    RandomizeMessages ? 1 : 0);
    INISetInt_Preserve(TEXT("Matrix"), TEXT("FontBold"),     FontBold ? 1 : 0);

    INISetString_Preserve(TEXT("Matrix"), TEXT("FontName"),  szFontName);

    // Messages
    INISetInt_Preserve(TEXT("Messages"), TEXT("Count"), (UINT)nNumMessages);
    for (int i = 0; i < nNumMessages; ++i)
    {
        TCHAR key[32];
        wsprintf(key, TEXT("Message%d"), i);
        INISetString_Preserve(TEXT("Messages"), key, szMessages[i]);
    }
}
