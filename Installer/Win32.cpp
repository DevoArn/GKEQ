// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>

#include "Win32.h"

namespace setup::win32
{

static std::wstring lower (std::wstring s)
{
    std::transform (s.begin(), s.end(), s.begin(), [] (wchar_t c) { return (wchar_t) std::towlower (c); });
    return s;
}

std::wstring commonProgramsFolder()
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED (SHGetKnownFolderPath (FOLDERID_CommonPrograms, 0, nullptr, &path)) && path != nullptr)
        result = path;
    if (path != nullptr)
        CoTaskMemFree (path);
    return result;
}

bool isProcessRunning (const wchar_t* nameFragment)
{
    HANDLE snap = CreateToolhelp32Snapshot (TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return false;

    const std::wstring fragment = lower (nameFragment);
    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof (entry);
    bool found = false;
    if (Process32FirstW (snap, &entry))
    {
        do
        {
            if (lower (entry.szExeFile).find (fragment) != std::wstring::npos)
            {
                found = true;
                break;
            }
        } while (Process32NextW (snap, &entry));
    }
    CloseHandle (snap);
    return found;
}

bool deleteOnReboot (const wchar_t* path)
{
    return MoveFileExW (path, nullptr, MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
}

bool isElevated()
{
    HANDLE token = nullptr;
    if (! OpenProcessToken (GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elevation {};
    DWORD size = 0;
    const bool ok = GetTokenInformation (token, TokenElevation, &elevation, sizeof (elevation), &size) != FALSE;
    CloseHandle (token);
    return ok && elevation.TokenIsElevated != 0;
}

} // namespace setup::win32
