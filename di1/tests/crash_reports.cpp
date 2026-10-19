// SPDX-License-Identifier: GPL-3.0-only
// Probe processes belong solely to this test. Production never launches a helper.
#include "../../src/di1/auto_capture.cpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <cstdio>

namespace crabe::di1 { void log(const char*, ...) {} }
namespace {
unsigned failures{}, previousCalls{};
DWORD previousError{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
LONG WINAPI nativeFilter(EXCEPTION_POINTERS*) {
    previousError=GetLastError(); ++previousCalls; SetLastError(0xAABBCCDD); return EXCEPTION_CONTINUE_EXECUTION;
}
bool handled() {
    __try { RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
int probe(const wchar_t* directory, bool crash) {
    using namespace crabe::di1;
    SetErrorMode(SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS);
    armAutomaticCapture(GetModuleHandleW(nullptr),directory);
    SetLastError(0x55667788); startAutomaticCapture();
    if (GetLastError()!=0x55667788 || !writerThread) return 71;
    const HANDLE first=writerThread; startAutomaticCapture();
    if (writerThread!=first || !handled() || reportNumber!=0) return 72;
    if (crash) {
        const ULONG_PTR detail[2]{1,0x13572468};
        RaiseException(EXCEPTION_ACCESS_VIOLATION,EXCEPTION_NONCONTINUABLE,2,detail);
        return 73;
    }
    return stopAutomaticCapture() ? 0:74;
}
bool validDump(const std::string& bytes) {
    if (bytes.size()<sizeof(MINIDUMP_HEADER)) return false;
    MINIDUMP_HEADER header{}; std::memcpy(&header,bytes.data(),sizeof(header));
    if (header.Signature!=MINIDUMP_SIGNATURE) return false;
    for (ULONG i=0; i<header.NumberOfStreams; ++i) {
        const std::uint64_t position=std::uint64_t(header.StreamDirectoryRva)+i*sizeof(MINIDUMP_DIRECTORY);
        if (position+sizeof(MINIDUMP_DIRECTORY)>bytes.size()) return false;
        MINIDUMP_DIRECTORY directory{}; std::memcpy(&directory,bytes.data()+position,sizeof(directory));
        if (directory.StreamType!=ExceptionStream) continue;
        if (std::uint64_t(directory.Location.Rva)+sizeof(MINIDUMP_EXCEPTION_STREAM)>bytes.size()) return false;
        MINIDUMP_EXCEPTION_STREAM exception{}; std::memcpy(&exception,bytes.data()+directory.Location.Rva,sizeof(exception));
        if (std::uint64_t(exception.ThreadContext.Rva)+sizeof(CONTEXT)>bytes.size()) return false;
        CONTEXT context{}; std::memcpy(&context,bytes.data()+exception.ThreadContext.Rva,sizeof(context));
        return exception.ExceptionRecord.ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
            exception.ExceptionRecord.ExceptionInformation[1]==0x13572468 && context.Eip!=0 && context.Esp!=0;
    }
    return false;
}
}
int wmain(int argc,wchar_t** argv) {
    if (argc==4 && std::wcscmp(argv[1],L"--probe")==0) return probe(argv[2],std::wcscmp(argv[3],L"crash")==0);
    using namespace crabe::di1;
    namespace fs=std::filesystem;
    wchar_t executable[32768]{}; GetModuleFileNameW(nullptr,executable,_countof(executable));
    const fs::path root=fs::current_path()/(L"crash-reports-probe-"+std::to_wstring(GetCurrentProcessId()));
    for (const bool crash : {false,true}) {
        const auto folder=root/(crash ? L"fatal":L"normal"); fs::create_directories(folder);
        { std::ofstream file(folder/L"LiteralCrab.log",std::ios::binary); file<<"test log preserved\n"; }
        { std::ofstream file(folder/L"LiteralCrab-exceptions.log"); file<<"test first chance log\n"; }
        { std::ofstream file(folder/L"LiteralCrab.ini"); file<<"[Diagnostics]\nCrashReports=1\n"; }
        std::wstring command=L"\""+std::wstring(executable)+L"\" --probe \""+folder.wstring()+L"\" "+(crash ? L"crash":L"normal");
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION child{};
        const BOOL created=CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child);
        CHECK(created); if (!created) continue;
        const auto wait=WaitForSingleObject(child.hProcess,25000);
        CHECK(wait==WAIT_OBJECT_0);
        if (wait!=WAIT_OBJECT_0) { TerminateProcess(child.hProcess,99); WaitForSingleObject(child.hProcess,2000); }
        DWORD exit{}; GetExitCodeProcess(child.hProcess,&exit);
        CHECK(exit==(crash ? EXCEPTION_ACCESS_VIOLATION:0));
        CloseHandle(child.hProcess); CloseHandle(child.hThread);
        std::vector<fs::path> reports;
        for (const auto& item:fs::directory_iterator(folder/L"LiteralCrab-diagnostics")) if (item.is_directory()) reports.push_back(item.path());
        CHECK(reports.size()==(crash ? 1u:0u));
        if (crash && reports.size()==1) {
            crabe::mods::zip::Archive archive((reports[0]/L"report.zip").wstring());
            CHECK(archive.entries().size()==5);
            const auto* dump=archive.find("crash.dmp"); const auto* log=archive.find("LiteralCrab.log");
            const auto* report=archive.find("crash.txt");
            CHECK(dump && validDump(archive.read(*dump)));
            CHECK(log && archive.read(*log)=="test log preserved\n");
            CHECK(report && archive.read(*report).find("dump_written=1")!=std::string::npos);
            for (const auto& entry:archive.entries()) CHECK(!archive.read(entry).empty()); // CRC for every member
        }
    }
    const auto manual=root/L"native-chain"; fs::create_directories(manual);
    const auto prior=SetUnhandledExceptionFilter(nativeFilter);
    armAutomaticCapture(GetModuleHandleW(nullptr),manual.c_str()); startAutomaticCapture();
    CHECK(writerThread!=nullptr);
    CONTEXT context{}; RtlCaptureContext(&context); const auto saved=context;
    EXCEPTION_RECORD record{}; record.ExceptionCode=EXCEPTION_ACCESS_VIOLATION;
    record.ExceptionAddress=reinterpret_cast<void*>(context.Eip);
    const auto savedRecord=record; EXCEPTION_POINTERS pointers{&record,&context};
    SetLastError(0x10203040);
    CHECK(topLevelException(&pointers)==EXCEPTION_CONTINUE_EXECUTION);
    CHECK(GetLastError()==0xAABBCCDD && previousCalls==1 && previousError==0x10203040);
    CHECK(!std::memcmp(&context,&saved,sizeof(context)) && !std::memcmp(&record,&savedRecord,sizeof(record)));
    CHECK(reportNumber==1 && requestState==0);
    reportNumber=8;
    CHECK(topLevelException(&pointers)==EXCEPTION_CONTINUE_EXECUTION && previousCalls==2 && reportNumber==8);
    CHECK(stopAutomaticCapture());
    CHECK(SetUnhandledExceptionFilter(prior)==nativeFilter);
    // Disabled/unarmed path creates no writer and preserves the caller's error.
    SetLastError(0x12345678); startAutomaticCapture();
    CHECK(!writerThread && GetLastError()==0x12345678);
    std::printf("In-process fatal dump/ZIP/normal SEH/native filter/context/worker lifetime: %s\n",failures ? "FAILED":"passed");
    return failures ? 1:0;
}
