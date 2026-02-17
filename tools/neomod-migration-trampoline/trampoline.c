// migration trampoline: ships as "neosu.exe" in the transition update zip,
// launches "neomod.exe" in the same directory with the same arguments, then exits.
//
// MSVC (64-bit):
//   cl /O1 /GS- trampoline.c /link /SUBSYSTEM:WINDOWS /ENTRY:entry kernel32.lib
// MSVC (32-bit):
//   cl /O1 /GS- trampoline.c /link /SUBSYSTEM:WINDOWS /ENTRY:entry kernel32.lib /MACHINE:X86
//
// MinGW (64-bit):
//   x86_64-w64-mingw32-gcc -O2 -nostdlib -mno-stack-arg-probe -fno-builtin -o neosu.exe trampoline.c -lkernel32 -Wl,--subsystem,windows -Wl,-e,entry
// MinGW (32-bit):
//   i686-w64-mingw32-gcc -O2 -nostdlib -mno-stack-arg-probe -fno-builtin -o neosu.exe trampoline.c -lkernel32 -Wl,--subsystem,windows -Wl,-e,entry

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// replace the last path component's "neosu" with "neomod" in-place.
// returns 0 on failure.
static int replace_exe_name(wchar_t *path, wchar_t *path_end) {
    // scan backwards for the start of the filename
    wchar_t *p = path_end;
    while(p > path && *(p - 1) != L'\\' && *(p - 1) != L'/') {
        p--;
    }

    // find "neosu" in the filename
    wchar_t *fname = p;
    wchar_t *match = NULL;
    while(*p && p + 5 <= path_end) {
        if(p[0] == L'n' && p[1] == L'e' && p[2] == L'o' && p[3] == L's' && p[4] == L'u') {
            match = p;
            break;
        }
        p++;
    }
    if(!match) return 0;

    // shift everything after "neosu" right by 1 to make room for "neomod" (1 char longer)
    // find the end of the string
    wchar_t *end = match + 5;
    while(*end) end++;
    // shift right (including null terminator)
    wchar_t *src = end;
    while(src >= match + 5) {
        *(src + 1) = *src;
        src--;
    }

    // write "neomod"
    match[0] = L'n';
    match[1] = L'e';
    match[2] = L'o';
    match[3] = L'm';
    match[4] = L'o';
    match[5] = L'd';

    return 1;
}

void entry(void) {
    // GetCommandLineW returns something like: "C:\path\neosu.exe" -flag1 -flag2
    // or: C:\path\neosu.exe -flag1 -flag2
    wchar_t *cmdline = GetCommandLineW();
    wchar_t new_cmdline[32768];
    wchar_t *argv0_end;
    DWORD pos = 0;
    STARTUPINFOW si;
    PROCESS_INFORMATION pi;

    // copy cmdline into our mutable buffer
    for(wchar_t *p = cmdline; *p; p++) {
        new_cmdline[pos++] = *p;
    }
    new_cmdline[pos] = L'\0';

    // find the end of argv[0] so we know where the filename is
    if(new_cmdline[0] == L'"') {
        argv0_end = new_cmdline + 1;
        while(*argv0_end && *argv0_end != L'"') {
            argv0_end++;
        }
    } else {
        argv0_end = new_cmdline;
        while(*argv0_end && *argv0_end != L' ') {
            argv0_end++;
        }
    }

    // replace "neosu" with "neomod" in the argv[0] portion
    if(!replace_exe_name(new_cmdline, argv0_end)) {
        ExitProcess(1);
    }

    for(int i = 0; i < (int)sizeof(si); i++) {
        ((char *)&si)[i] = 0;
    }
    si.cb = sizeof(si);

    // start neomod.exe
    if(!CreateProcessW(NULL, new_cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        ExitProcess(1);
    }

    // the stub can die now
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    ExitProcess(0);
}
