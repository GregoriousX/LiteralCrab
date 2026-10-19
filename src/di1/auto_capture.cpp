// SPDX-License-Identifier: GPL-3.0-only
#include "di1/auto_capture.hpp"
#include "di1/common.hpp"
#include "mods/zip.hpp"
#include <dbghelp.h>
#include <atomic>
#include <cstring>
#include <cwchar>

namespace crabe::di1 {
namespace {
using Dump = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
Dump writeDump{};
HMODULE dumpLibrary{}, ownerModule{};
HANDLE requested{}, completed{}, writerThread{};
std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> previousFilter{};
std::atomic<LONG> requestState{}, reportNumber{};
std::atomic<bool> stopping{}, armed{}, started{};
CONTEXT faultContext{};
EXCEPTION_RECORD faultRecord{};
EXCEPTION_POINTERS faultPointers{&faultRecord, &faultContext};
DWORD faultThread{};
wchar_t sourceDirectory[32768]{}, reportsDirectory[32768]{};
// One writer owns these buffers; no heap allocation or logger lock in the crash path.
wchar_t reportDirectory[32768], memberPath[32768], archivePath[32768];
BYTE copyBuffer[16384];
bool appendPath(wchar_t* out, const wchar_t* base, const wchar_t* suffix) noexcept {
    unsigned count{};
    while (*base && count < 32600) out[count++] = *base++;
    if (*base) return false;
    while (*suffix && count < 32767) out[count++] = *suffix++;
    out[count] = 0;
    return !*suffix;
}
void decimal(wchar_t*& cursor, DWORD value, unsigned width) noexcept {
    wchar_t digits[10]; unsigned count{};
    do { digits[count++] = static_cast<wchar_t>(L'0' + value % 10); value /= 10; } while (value && count < 10);
    while (count < width) digits[count++] = L'0';
    while (count) *cursor++ = digits[--count];
}
struct Zip {
    HANDLE file = INVALID_HANDLE_VALUE;
    DWORD position{};
    bool okay = true;
    void bytes(const void* data, DWORD count) noexcept {
        if (!okay || count > MAXDWORD - position) { okay = false; return; }
        DWORD written{};
        okay = WriteFile(file, data, count, &written, nullptr) && written == count;
        position += count;
    }
    void u16(DWORD value) noexcept { BYTE data[2]{BYTE(value),BYTE(value>>8)}; bytes(data, 2); }
    void u32(DWORD value) noexcept { u16(value); u16(value>>16); }
};
bool zipReport() noexcept {
    struct Entry { const char* name{}; DWORD size{}, crc{}, offset{}; };
    Entry entries[5]{};
    constexpr const wchar_t* wide[] = {L"\\crash.dmp",L"\\crash.txt",L"\\LiteralCrab.log",L"\\LiteralCrab-exceptions.log",L"\\LiteralCrab.ini"};
    constexpr const char* narrow[] = {"crash.dmp","crash.txt","LiteralCrab.log","LiteralCrab-exceptions.log","LiteralCrab.ini"};
    appendPath(archivePath, reportDirectory, L"\\report.zip.partial");
    Zip zip;
    zip.file = CreateFileW(archivePath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (zip.file == INVALID_HANDLE_VALUE) return false;
    unsigned count{};
    for (unsigned i=0; i<5 && zip.okay; ++i) {
        appendPath(memberPath, reportDirectory, wide[i]);
        HANDLE input = CreateFileW(memberPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (input == INVALID_HANDLE_VALUE) continue;
        Entry& entry = entries[count++]; entry.name = narrow[i]; entry.offset = zip.position;
        const auto nameLength = static_cast<DWORD>(std::strlen(entry.name));
        zip.u32(0x04034B50); zip.u16(20); zip.u16(8); zip.u16(0); zip.u16(0); zip.u16(0x5A21);
        zip.u32(0); zip.u32(0); zip.u32(0); zip.u16(nameLength); zip.u16(0); zip.bytes(entry.name, nameLength);
        DWORD bytes{};
        while (zip.okay) {
            if (!ReadFile(input, copyBuffer, sizeof(copyBuffer), &bytes, nullptr)) { zip.okay = false; break; }
            if (!bytes) break;
            if (bytes > MAXDWORD - entry.size) { zip.okay = false; break; }
            entry.crc = mods::zip::crc32(copyBuffer, bytes, entry.crc); entry.size += bytes;
            zip.bytes(copyBuffer, bytes);
        }
        CloseHandle(input);
        zip.u32(0x08074B50); zip.u32(entry.crc); zip.u32(entry.size); zip.u32(entry.size);
    }
    const DWORD central = zip.position;
    for (unsigned i=0; i<count; ++i) {
        const Entry& entry = entries[i]; const auto length = static_cast<DWORD>(std::strlen(entry.name));
        zip.u32(0x02014B50); zip.u16(20); zip.u16(20); zip.u16(8); zip.u16(0); zip.u16(0); zip.u16(0x5A21);
        zip.u32(entry.crc); zip.u32(entry.size); zip.u32(entry.size); zip.u16(length);
        zip.u16(0); zip.u16(0); zip.u16(0); zip.u16(0); zip.u32(0); zip.u32(entry.offset); zip.bytes(entry.name,length);
    }
    const DWORD centralBytes = zip.position-central;
    zip.u32(0x06054B50); zip.u16(0); zip.u16(0); zip.u16(count); zip.u16(count); zip.u32(centralBytes); zip.u32(central); zip.u16(0);
    if (zip.okay) zip.okay = FlushFileBuffers(zip.file) != FALSE;
    CloseHandle(zip.file);
    if (!zip.okay) return false; // keep raw files and incomplete archive as evidence
    wchar_t finalPath[32768];
    appendPath(finalPath, reportDirectory, L"\\report.zip");
    return MoveFileExW(archivePath, finalPath, MOVEFILE_WRITE_THROUGH) != FALSE;
}
struct Text {
    char data[2048]{};
    unsigned count{};
    void add(const char* value) noexcept { while (*value && count < sizeof(data)) data[count++] = *value++; }
    void hex(DWORD value) noexcept {
        add("0x");
        for (int shift=28; shift>=0 && count<sizeof(data); shift-=4) data[count++] = "0123456789ABCDEF"[(value>>shift)&15];
    }
};
bool writeReport() noexcept {
    SYSTEMTIME now{}; GetLocalTime(&now);
    wchar_t name[128]; wchar_t* cursor = name;
    const wchar_t prefix[] = L"\\Crash-";
    for (const auto character : prefix) if (character) *cursor++ = character;
    decimal(cursor, now.wYear,4); decimal(cursor,now.wMonth,2); decimal(cursor,now.wDay,2); *cursor++=L'-';
    decimal(cursor,now.wHour,2); decimal(cursor,now.wMinute,2); decimal(cursor,now.wSecond,2); *cursor++=L'-';
    decimal(cursor,now.wMilliseconds,3); *cursor++=L'-'; decimal(cursor,GetCurrentProcessId(),1); *cursor++=L'-';
    decimal(cursor,static_cast<DWORD>(reportNumber.load()),1); *cursor=0;
    if (!appendPath(reportDirectory,reportsDirectory,name) || !CreateDirectoryW(reportDirectory,nullptr)) return false;
    appendPath(memberPath,reportDirectory,L"\\crash.dmp");
    HANDLE dump = CreateFileW(memberPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    bool dumped{}; DWORD dumpError = GetLastError();
    if (dump != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{faultThread,&faultPointers,FALSE};
        const auto flags = static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules |
            MiniDumpWithProcessThreadData | MiniDumpWithFullMemoryInfo | MiniDumpIgnoreInaccessibleMemory);
        dumped = writeDump(GetCurrentProcess(),GetCurrentProcessId(),dump,flags,&info,nullptr,nullptr) != FALSE;
        dumpError = dumped ? ERROR_SUCCESS : GetLastError(); CloseHandle(dump);
    }
    Text text;
    text.add("LiteralCrab in-process report\r\nTop-level exception filter reached; previous/native filter is called after capture.\r\n");
    text.add("exception="); text.hex(faultRecord.ExceptionCode); text.add(" eip="); text.hex(faultContext.Eip);
    text.add(" thread="); text.hex(faultThread); text.add("\r\ndump_written="); text.add(dumped ? "1":"0");
    text.add(" dump_error="); text.hex(dumpError); text.add("\r\n");
    constexpr const wchar_t* logs[] = {L"\\LiteralCrab.log",L"\\LiteralCrab-exceptions.log",L"\\LiteralCrab.ini"};
    for (unsigned i=0; i<3; ++i) {
        wchar_t source[32768];
        appendPath(source,sourceDirectory,logs[i]); appendPath(memberPath,reportDirectory,logs[i]);
        const BOOL copied = CopyFileW(source,memberPath,TRUE);
        text.add("copy_index="); text.hex(i); text.add(" error="); text.hex(copied ? ERROR_SUCCESS : GetLastError()); text.add("\r\n");
    }
    appendPath(memberPath,reportDirectory,L"\\crash.txt");
    HANDLE file=CreateFileW(memberPath,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if (file != INVALID_HANDLE_VALUE) { DWORD written{}; WriteFile(file,text.data,text.count,&written,nullptr); CloseHandle(file); }
    return zipReport();
}
DWORD WINAPI reportWorker(void*) noexcept {
    for (;;) {
        if (WaitForSingleObject(requested,INFINITE) != WAIT_OBJECT_0 || stopping.load()) break;
        writeReport();
        SetEvent(completed);
    }
    return 0;
}
LONG WINAPI topLevelException(EXCEPTION_POINTERS* pointers) noexcept {
    const DWORD error = GetLastError();
    if (pointers && pointers->ExceptionRecord && pointers->ContextRecord && GetCurrentThreadId() != GetThreadId(writerThread) &&
        reportNumber.load() < 8 && requestState.exchange(1) == 0) {
        ResetEvent(completed);
        faultRecord = *pointers->ExceptionRecord; faultRecord.ExceptionRecord = nullptr;
        faultContext = *pointers->ContextRecord; faultThread = GetCurrentThreadId(); ++reportNumber;
        if (SetEvent(requested) && WaitForSingleObject(completed,15000) == WAIT_OBJECT_0) requestState = 0;
        // Timeout deliberately leaves the request busy; do not overwrite a live dump.
    }
    const auto previous = previousFilter.load();
    SetLastError(error);
    return previous && previous != topLevelException ? previous(pointers) : EXCEPTION_CONTINUE_SEARCH;
}
bool createRoot() noexcept {
    if (!appendPath(reportsDirectory,sourceDirectory,L"\\LiteralCrab-diagnostics")) return false;
    if (CreateDirectoryW(reportsDirectory,nullptr) || GetLastError() == ERROR_ALREADY_EXISTS) return true;
    wchar_t local[32768]{};
    const DWORD count=GetEnvironmentVariableW(L"LOCALAPPDATA",local,_countof(local));
    if (!count || count>=32000) return false;
    appendPath(reportsDirectory,local,L"\\LiteralCrab");
    if (!CreateDirectoryW(reportsDirectory,nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    appendPath(local,reportsDirectory,L"\\Diagnostics");
    std::wcscpy(reportsDirectory,local);
    return CreateDirectoryW(reportsDirectory,nullptr) || GetLastError()==ERROR_ALREADY_EXISTS;
}
}
void armAutomaticCapture(HMODULE plugin,const wchar_t* directory) {
    const auto length = directory ? std::wcslen(directory) : 0;
    if (!length || length>=32000) return;
    std::wcscpy(sourceDirectory,directory);
    while (sourceDirectory[std::wcslen(sourceDirectory)-1]==L'\\' && std::wcslen(sourceDirectory)>3)
        sourceDirectory[std::wcslen(sourceDirectory)-1]=0;
    ownerModule=plugin; armed=true;
}
void startAutomaticCapture() noexcept {
    const DWORD error = GetLastError();
    if (!armed || started.exchange(true)) { SetLastError(error); return; }
    dumpLibrary=LoadLibraryExW(L"dbghelp.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    writeDump=dumpLibrary ? reinterpret_cast<Dump>(GetProcAddress(dumpLibrary,"MiniDumpWriteDump")) : nullptr;
    if (!writeDump || !createRoot()) {
        log("Diagnostics: in-process crash reports unavailable (Win32 %lu).",GetLastError());
        stopAutomaticCapture();
        SetLastError(error); return;
    }
    requested=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    completed=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    if (requested && completed) writerThread=CreateThread(nullptr,1<<20,reportWorker,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);
    if (writerThread) {
        previousFilter=SetUnhandledExceptionFilter(topLevelException);
        log("Diagnostics: automatic crash ZIP enabled inside the game; reports: %ls. No helper process.",reportsDirectory);
    } else {
        log("Diagnostics: crash-report thread could not start (Win32 %lu).",GetLastError());
        stopAutomaticCapture();
    }
    SetLastError(error);
}
// Used only by the isolated tests; production ASI and worker are pinned until exit.
bool stopAutomaticCapture() noexcept {
    if (writerThread) {
        const auto current=SetUnhandledExceptionFilter(previousFilter.load());
        if (current != topLevelException) SetUnhandledExceptionFilter(current);
        stopping=true; SetEvent(requested);
        if (WaitForSingleObject(writerThread,2000)!=WAIT_OBJECT_0) return false;
        CloseHandle(writerThread); writerThread=nullptr;
    }
    if (requested) { CloseHandle(requested); requested=nullptr; }
    if (completed) { CloseHandle(completed); completed=nullptr; }
    if (dumpLibrary) { FreeLibrary(dumpLibrary); dumpLibrary=nullptr; }
    started=false; armed=false; stopping=false; requestState=0; reportNumber=0; previousFilter=nullptr; ownerModule=nullptr;
    return true;
}
}
