// Settings.cpp - Portable configuration for Matrix ScreenSaver
// Stores settings in matrix-settings-portable.cfg
// Location: EXE folder if writable, else %APPDATA%\Matrix-ScreenSaver\

#include "stdafx.h"
#include "settings.h"
#include <windows.h>
#include <shlobj.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <sys/stat.h>

// Globals expected by other parts of the program
TCHAR szMessages[MAXMESSAGES][MAXMSGLEN];
int nNumMessages = 0;

// Internal
static std::wstring g_cfgPath;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool IsDirWritable(const std::wstring &dir) {
    std::wstring testfile = dir + L"\\.__writetest";
    HANDLE h = CreateFileW(testfile.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    DeleteFileW(testfile.c_str());
    return true;
}

static std::wstring GetConfigPath() {
    if (!g_cfgPath.empty()) return g_cfgPath;

    // Try EXE folder
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring exedir(exe);
    exedir = exedir.substr(0, exedir.find_last_of(L"\\/"));

    if (IsDirWritable(exedir)) {
        g_cfgPath = exedir + L"\\matrix-settings-portable.cfg";
        return g_cfgPath;
    }

    // Fallback to %APPDATA%\Matrix-ScreenSaver\
    wchar_t appdata[MAX_PATH];
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, appdata);
    std::wstring dir = std::wstring(appdata) + L"\\Matrix-ScreenSaver";
    CreateDirectoryW(dir.c_str(), nullptr);
    g_cfgPath = dir + L"\\matrix-settings-portable.cfg";
    return g_cfgPath;
}

static bool FileExists(const std::wstring &path) {
    struct _stat st;
    return (_wstat(path.c_str(), &st) == 0);
}

// ---------------------------------------------------------------------------
// Template config on first run
// ---------------------------------------------------------------------------
static void WriteTemplateConfig(const std::wstring &path) {
    std::wofstream f(path);
    f << L"[Matrix]\n";
    f << L"FontName=MS Sans Serif\n";
    f << L"FontBold=1\n";
    f << L"Randomize=0\n";
    f << L"Previews=1\n";
    f << L"FontSize=12\n";
    f << L"Density=32\n";
    f << L"MatrixSpeed=5\n";   // middle speed
    f << L"MessageSpeed=150\n\n";

    f << L"[Messages]\n";
    f << L"Count=1\n";
    f << L"Message0=Hello, World!\n\n";

    f << L"; Matrix portable config\n";
    f << L"; Comments here will remain if keys are updated\n\n";
    f << L"[Notes]\n";
    f << L"; nothing in this section is read/written\n";
    f << L"; \n";
    f << L"; FontName=MS Sans Serif\n";
    f << L"; FontBold=1 1/0 True/False\n";
    f << L"; Randomize=0 \"0\" means it will display your messages in the order saved 0,1,2,etc. (not random)\n";
    f << L"; Previews=1\n";
    f << L"; FontSize=12\n";
    f << L"; Density=32\n";
    f << L"; MatrixSpeed=1-10 (1 = slowest, 10 = fastest)\n";
    f << L"; MessageSpeed= delay in ms between chars\n";
}

// ---------------------------------------------------------------------------
// Load / Save
// ---------------------------------------------------------------------------
void LoadSettings() {
    std::wstring cfg = GetConfigPath();
    if (!FileExists(cfg)) {
        WriteTemplateConfig(cfg);
    }

    wchar_t buf[256];

    GetPrivateProfileStringW(L"Matrix", L"FontName", L"MS Sans Serif", buf, 256, cfg.c_str());
    lstrcpynW(g_szFontName, buf, LF_FACESIZE);

    g_bFontBold = GetPrivateProfileIntW(L"Matrix", L"FontBold", 1, cfg.c_str());
    g_bRandomize = GetPrivateProfileIntW(L"Matrix", L"Randomize", 0, cfg.c_str());
    g_bPreviews = GetPrivateProfileIntW(L"Matrix", L"Previews", 1, cfg.c_str());
    g_nFontSize = GetPrivateProfileIntW(L"Matrix", L"FontSize", 12, cfg.c_str());
    g_nDensity  = GetPrivateProfileIntW(L"Matrix", L"Density", 32, cfg.c_str());

    int rawSpeed = GetPrivateProfileIntW(L"Matrix", L"MatrixSpeed", 5, cfg.c_str());
    g_nMatrixSpeed = std::clamp(rawSpeed, 1, 10); // now intuitive: 1 slow ... 10 fast

    g_nMessageSpeed = GetPrivateProfileIntW(L"Matrix", L"MessageSpeed", 150, cfg.c_str());

    nNumMessages = GetPrivateProfileIntW(L"Messages", L"Count", 0, cfg.c_str());
    if (nNumMessages > MAXMESSAGES) nNumMessages = MAXMESSAGES;

    for (int i=0; i<nNumMessages; i++) {
        wchar_t key[32];
        wsprintfW(key, L"Message%d", i);
        GetPrivateProfileStringW(L"Messages", key, L"", szMessages[i], MAXMSGLEN, cfg.c_str());
    }
}

static void UpdateKey(const std::wstring &section, const std::wstring &key, const std::wstring &val, std::vector<std::wstring> &lines) {
    std::wstring header = L"[" + section + L"]";
    bool inSec = false, found=false;
    for (size_t i=0; i<lines.size(); i++) {
        std::wstring s = lines[i];
        if (!s.empty() && s[0]==L'[') {
            inSec = (_wcsicmp(s.c_str(), header.c_str())==0);
        } else if (inSec) {
            if (_wcsnicmp(s.c_str(), key.c_str(), key.size())==0 && s[key.size()]==L'=') {
                lines[i] = key + L"=" + val;
                found=true;
                break;
            }
        }
    }
    if (!found) {
        if (!lines.empty() && lines.back().empty()) lines.pop_back();
        lines.push_back(header);
        lines.push_back(key + L"=" + val);
    }
}

void SaveSettings() {
    std::wstring cfg = GetConfigPath();

    // load existing lines
    std::wifstream fi(cfg);
    std::vector<std::wstring> lines;
    std::wstring line;
    while (std::getline(fi, line)) lines.push_back(line);
    fi.close();

    // Update keys
    UpdateKey(L"Matrix", L"FontName", g_szFontName, lines);
    UpdateKey(L"Matrix", L"FontBold", std::to_wstring(g_bFontBold), lines);
    UpdateKey(L"Matrix", L"Randomize", std::to_wstring(g_bRandomize), lines);
    UpdateKey(L"Matrix", L"Previews", std::to_wstring(g_bPreviews), lines);
    UpdateKey(L"Matrix", L"FontSize", std::to_wstring(g_nFontSize), lines);
    UpdateKey(L"Matrix", L"Density", std::to_wstring(g_nDensity), lines);
    UpdateKey(L"Matrix", L"MatrixSpeed", std::to_wstring(std::clamp(g_nMatrixSpeed,1,10)), lines);
    UpdateKey(L"Matrix", L"MessageSpeed", std::to_wstring(g_nMessageSpeed), lines);

    UpdateKey(L"Messages", L"Count", std::to_wstring(nNumMessages), lines);
    for (int i=0;i<nNumMessages;i++) {
        wchar_t key[32];
        wsprintfW(key,L"Message%d",i);
        UpdateKey(L"Messages", key, szMessages[i], lines);
    }

    // write back
    std::wofstream fo(cfg, std::ios::trunc);
    for (auto &l : lines) fo << l << L"\n";
}
