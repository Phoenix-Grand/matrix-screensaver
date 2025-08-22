#include <windows.h>
#include <tchar.h>
#include <cstdio>
#include <vector>
#include <string>
#include <algorithm>

#include "message.h"
#include "matrix.h"

// Existing globals from the project
extern TCHAR szAppName[];
extern int Density;
extern int MessageSpeed;
extern int MatrixSpeed;
extern int FontSize;
extern TCHAR szFontName[];
extern BOOL FontBold;
extern BOOL EnablePreviews;
extern BOOL RandomizeMessages;

// Define storage for these (linker fix).
TCHAR szMessages[MAXMESSAGES][MAXMSGLEN];
int nNumMessages = 0;

// -------------------------------------------------------------------------------------------------
// Portable settings: write/read to "matrix-settings-portable.cfg"
// Prefer the executable directory if writable, otherwise %APPDATA%\\Matrix
// We preserve user comments by doing our own INI updates instead of WritePrivateProfileString.
// -------------------------------------------------------------------------------------------------

static TCHAR g_cfgPath[MAX_PATH] = {0};

static BOOL DirIsWritable(LPCTSTR dir)
{
    // Try to create/open the target file for write (in temp name) to test writability.
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
    CreateDirectory(dir, NULL); // no harm if it already exists
}

static void ComputeConfigPath()
{
    if (g_cfgPath[0] != 0) return;

    // 1) Try executable folder
    TCHAR exePath[MAX_PATH];
    GetModuleFileName(NULL, exePath, MAX_PATH);

    // Remove filename to get directory
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

    // 2) Fallback to %APPDATA%\Matrix (no trailing backslash here)
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

    // 3) As a last resort, drop next to the EXE even if not writable (writes may fail gracefully)
    lstrcpyn(g_cfgPath, exeDir, MAX_PATH);
    size_t len4 = lstrlen(g_cfgPath);
    if (len4 && g_cfgPath[len4-1] != TEXT('\\'))
        lstrcat(g_cfgPath, TEXT("\\"));
    lstrcat(g_cfgPath, TEXT("matrix-settings-portable.cfg"));
}

// Small helpers (TCHAR-aware)
static std::basic_string<TCHAR> t_trim(const std::basic_string<TCHAR>& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == TEXT(' ') || s[a] == TEXT('\t') || s[a] == TEXT('\r') || s[a] == TEXT('\n'))) ++a;
    while (b > a && (s[b-1] == TEXT(' ') || s[b-1] == TEXT('\t') || s[b-1] == TEXT('\r') || s[b-1] == TEXT('\n'))) --b;
    return s.substr(a, b - a);
}

static bool t_iequal(LPCTSTR a, LPCTSTR b)
{
    for (;; ++a, ++b) {
        TCHAR ca = *a, cb = *b;
        if (ca >= TEXT('A') && ca <= TEXT('Z')) ca = ca - TEXT('A') + TEXT('a');
        if (cb >= TEXT('A') && cb <= TEXT('Z')) cb = cb - TEXT('A') + TEXT('a');
        if (ca != cb) return false;
        if (ca == 0) return true;
    }
}

static void SplitKeyVal(const std::basic_string<TCHAR>& line, std::basic_string<TCHAR>& key, std::basic_string<TCHAR>& val)
{
    size_t eq = line.find(TEXT('='));
    if (eq == std::basic_string<TCHAR>::npos) { key = t_trim(line); val.clear(); return; }
    key = t_trim(line.substr(0, eq));
    val = t_trim(line.substr(eq + 1));
}

// Load whole file into vector of lines
static void LoadFileLines(std::vector<std::basic_string<TCHAR>>& lines)
{
    ComputeConfigPath();
#if defined(UNICODE) || defined(_UNICODE)
    // Simple UTF-16 LE reader
    HANDLE h = CreateFile(g_cfgPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD sz = GetFileSize(h, NULL);
    if (sz >= 2) {
        std::vector<BYTE> buf(sz);
        DWORD rd = 0; ReadFile(h, buf.data(), sz, &rd, NULL);
        CloseHandle(h);
        size_t off = 0;
        // skip BOM if present
        if (rd >= 2 && buf[0] == 0xFF && buf[1] == 0xFE) off = 2;
        size_t wlen = (rd - off) / 2;
        const WCHAR* w = (const WCHAR*)(buf.data() + off);
        std::basic_string<WCHAR> text(w, w + wlen);
        std::basic_string<WCHAR> line;
        for (size_t i = 0; i < text.size(); ++i) {
            WCHAR c = text[i];
            if (c == L'\n') { lines.push_back(line); line.clear(); }
            else if (c != L'\r') { line.push_back(c); }
        }
        if (!line.empty()) lines.push_back(line);
    } else {
        CloseHandle(h);
    }
#else
    FILE* f = _tfopen(g_cfgPath, TEXT("r"));
    if (!f) return;
    TCHAR buf[4096];
    while (_fgetts(buf, 4096, f)) {
        size_t L = _tcslen(buf);
        while (L && (buf[L-1] == TEXT('\n') || buf[L-1] == TEXT('\r'))) buf[--L] = 0;
        lines.push_back(std::basic_string<TCHAR>(buf));
    }
    fclose(f);
#endif
}

static void SaveFileLines(const std::vector<std::basic_string<TCHAR>>& lines)
{
    ComputeConfigPath();
#if defined(UNICODE) || defined(_UNICODE)
    HANDLE h = CreateFile(g_cfgPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    // UTF-16 LE BOM
    BYTE bom[2] = {0xFF, 0xFE};
    DWORD wr=0; WriteFile(h, bom, 2, &wr, NULL);
    for (const auto& line : lines) {
        DWORD bytes = (DWORD)(line.size() * sizeof(WCHAR));
        WriteFile(h, line.c_str(), bytes, &wr, NULL);
        WCHAR crlf[2] = {L'\r', L'\n'};
        WriteFile(h, crlf, sizeof(crlf), &wr, NULL);
    }
    CloseHandle(h);
#else
    FILE* f = _tfopen(g_cfgPath, TEXT("w"));
    if (!f) return;
    for (const auto& line : lines) {
        _fputts(line.c_str(), f);
        _fputts(TEXT("\r\n"), f);
    }
    fclose(f);
#endif
}

// Update (or insert) key=value inside [section], preserving comments and other lines.
static void INISetKeyPreserve(LPCTSTR section, LPCTSTR key, LPCTSTR value)
{
    std::vector<std::basic_string<TCHAR>> lines;
    LoadFileLines(lines);

    std::basic_string<TCHAR> tgtSection = section;
    std::basic_string<TCHAR> tgtKey = key;
    bool inSection = false;
    bool sectionFound = false;
    bool keyUpdated = false;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::basic_string<TCHAR> trimmed = lines[i];
        // trim copy for checks
        // fast manual trim
        size_t a = 0, b = trimmed.size();
        while (a < b && (trimmed[a] == TEXT(' ') || trimmed[a] == TEXT('\t') || trimmed[a] == TEXT('\r') || trimmed[a] == TEXT('\n'))) ++a;
        while (b > a && (trimmed[b-1] == TEXT(' ') || trimmed[b-1] == TEXT('\t') || trimmed[b-1] == TEXT('\r') || trimmed[b-1] == TEXT('\n'))) --b;
        trimmed = trimmed.substr(a, b-a);

        // section header?
        if (!trimmed.empty() && trimmed.front() == TEXT('[') && trimmed.back() == TEXT(']')) {
            std::basic_string<TCHAR> name = t_trim(trimmed.substr(1, trimmed.size()-2));
            inSection = t_iequal(name.c_str(), tgtSection.c_str());
            if (inSection) sectionFound = true;
            continue;
        }

        if (!inSection) continue;

        // skip blank or comment lines inside target section
        if (trimmed.empty() || trimmed[0] == TEXT(';')) continue;

        // parse key
        std::basic_string<TCHAR> k, v;
        SplitKeyVal(trimmed, k, v);
        if (t_iequal(k.c_str(), tgtKey.c_str())) {
            // Preserve original indentation (leading whitespace) from the stored line
            const std::basic_string<TCHAR>& orig = lines[i];
            size_t ws = 0;
            while (ws < orig.size() && (orig[ws] == TEXT(' ') || orig[ws] == TEXT('\t'))) ++ws;

            std::basic_string<TCHAR> newline = orig.substr(0, ws);
            newline += tgtKey;
            newline += TEXT("=");
            newline += value;

            lines[i] = newline;
            keyUpdated = true;
            break; // unique key per section
        }
    }

    if (!sectionFound) {
        // append new section and key at end
        if (!lines.empty() && !lines.back().empty()) lines.push_back(TEXT(""));
        lines.push_back(TEXT("[") + std::basic_string<TCHAR>(tgtSection) + TEXT("]"));
        std::basic_string<TCHAR> newline = tgtKey + TEXT("=") + std::basic_string<TCHAR>(value);
        lines.push_back(newline);
    } else if (sectionFound && !keyUpdated) {
        // find the end of section to insert before next section header or EOF
        size_t insertPos = lines.size();
        bool seenSection = false;
        for (size_t i = 0; i < lines.size(); ++i) {
            std::basic_string<TCHAR> L2 = t_trim(lines[i]);
            if (!L2.empty() && L2.front() == TEXT('[') && L2.back() == TEXT(']')) {
                std::basic_string<TCHAR> name = t_trim(L2.substr(1, L2.size()-2));
                if (t_iequal(name.c_str(), tgtSection.c_str())) { seenSection = true; insertPos = i + 1; continue; }
                if (seenSection) { insertPos = i; break; }
            }
        }
        std::basic_string<TCHAR> newline = tgtKey + TEXT("=") + std::basic_string<TCHAR>(value);
        lines.insert(lines.begin() + insertPos, newline);
    }

    SaveFileLines(lines);
}

// Convenience wrappers for reading (we can keep Win32 INI API for reads)
static UINT INIGetInt(LPCTSTR section, LPCTSTR key, UINT defval)
{
    ComputeConfigPath();
    return GetPrivateProfileInt(section, key, defval, g_cfgPath);
}

static void INISetInt_Preserve(LPCTSTR section, LPCTSTR key, UINT val)
{
    TCHAR buf[32];
    wsprintf(buf, TEXT("%u"), val);
    INISetKeyPreserve(section, key, buf);
}

static void INIGetString(LPCTSTR section, LPCTSTR key, LPTSTR out, DWORD outcch, LPCTSTR defval)
{
    ComputeConfigPath();
    GetPrivateProfileString(section, key, defval, out, outcch, g_cfgPath);
}

static void INISetString_Preserve(LPCTSTR section, LPCTSTR key, LPCTSTR val)
{
    INISetKeyPreserve(section, key, val);
}

// -------------------------------------------------------------------------------------------------
// Replacements for the original registry-based functions
// -------------------------------------------------------------------------------------------------

void LoadSettings()
{
    // Defaults (these should already be set elsewhere on first run, but be defensive)
    MessageSpeed = INIGetInt(TEXT("Matrix"), TEXT("MessageSpeed"), MessageSpeed);
    MatrixSpeed  = INIGetInt(TEXT("Matrix"), TEXT("MatrixSpeed"),  MatrixSpeed);
    Density      = INIGetInt(TEXT("Matrix"), TEXT("Density"),      Density);
    FontSize     = INIGetInt(TEXT("Matrix"), TEXT("FontSize"),     FontSize);

    EnablePreviews = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("Previews"), EnablePreviews ? 1 : 0);
    RandomizeMessages = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("Randomize"), RandomizeMessages ? 1 : 0);
    FontBold = (BOOL)INIGetInt(TEXT("Matrix"), TEXT("FontBold"), FontBold ? 1 : 0);

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
    INISetInt_Preserve(TEXT("Matrix"), TEXT("MessageSpeed"), (UINT)MessageSpeed);
    INISetInt_Preserve(TEXT("Matrix"), TEXT("MatrixSpeed"),  (UINT)MatrixSpeed);
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
