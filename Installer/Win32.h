#pragma once

#include <string>

// Small Win32 helpers for the installer, kept in a separate translation unit
// so <windows.h> never meets the JUCE headers.
namespace setup::win32
{
    /** All-users Start Menu "Programs" folder (empty string on failure). */
    std::wstring commonProgramsFolder();

    /** True if a running process' executable name contains the fragment (case-insensitive). */
    bool isProcessRunning (const wchar_t* nameFragment);

    /** Schedules a file or empty folder for deletion at the next reboot (needs admin rights). */
    bool deleteOnReboot (const wchar_t* path);

    /** True when the current process runs with elevated (administrator) rights. */
    bool isElevated();
}
