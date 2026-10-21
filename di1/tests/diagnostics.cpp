// SPDX-License-Identifier: GPL-3.0-only
// Same translation unit lets this probe exercise the actual exception observer
// without exposing a production test API or faulting any game process.
#include "../../src/di1/diagnostics.cpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

namespace crabe::di1 {
void log(const char*, ...) {}
// This target exercises the observer itself; the startup/config target uses
// the real capture implementation and checks its deferred worker lifecycle.
void armAutomaticCapture(HMODULE, const wchar_t*) {}
void startAutomaticCapture() noexcept {}
void setGraphicsStartupObserver(void (*)()) {}
}
namespace {
unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
std::string contents(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
bool handledAccessViolation() {
    __try { RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
}
int main() {
    using namespace crabe::di1;
    namespace fs = std::filesystem;
    const fs::path folder = fs::current_path() / (L"diagnostics-probe-" + std::to_wstring(GetCurrentProcessId()));
    fs::create_directories(folder);
    struct Vector { unsigned size; const char* expected; };
    const Vector vectors[] = {
        {0,"E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855"},
        {3,"BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD"},
        {55,"9F4390F8D30C2DD92EC9F095B65E2B9AE9B0A925A5258E241C9F1E910F734318"},
        {56,"B35439A4AC6F0948B6D6F9E3C6AF0F5F590CE20F1BDE7090EF7970686EC6738A"},
        {64,"FFE054FE7AE0CB6DC65C3AF9B61D5209F439851DB43D0BA5997337DF154668EB"},
        {65,"635361C48BB9EAB14198E76EA8AB7F1A41685D6AD62AA9146D301D4F17EB0AE0"},
        {20000,"CC17FAAAD36649C4603DDA4D8FF97CB149722AF0BCAC0746305A2134AD2D0B97"}
    };
    for (const auto& vector : vectors) {
        const auto path = folder / (std::to_wstring(vector.size) + L".bin");
        { std::ofstream file(path, std::ios::binary); file << (vector.size == 3 ? "abc" : std::string(vector.size, 'a')); }
        char digest[65]{};
        CHECK(diagnosticFileSha256(path.c_str(), digest));
        CHECK(std::strcmp(digest, vector.expected) == 0);
    }
    char digest[65] = "stale";
    CHECK(!diagnosticFileSha256((folder / L"absent").c_str(), digest) && !digest[0]);
    const auto path = folder / L"exceptions.log";
    CHECK(!openExceptionDiagnostics((folder / L"absent" / L"bad.log").c_str(), nullptr, nullptr));
    CHECK(openExceptionDiagnostics(path.c_str(), GetModuleHandleW(nullptr), GetModuleHandleW(nullptr)));
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    record.ExceptionAddress = reinterpret_cast<void*>(0x12345678);
    record.NumberParameters = 2;
    record.ExceptionInformation[0] = 1; record.ExceptionInformation[1] = 0x87654321;
    CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
    context.Eip = 0x12345678; context.Eax = 0xA1B2C3D4; context.EFlags = 0x202;
    DWORD stack[64]; for (unsigned i = 0; i < 64; ++i) stack[i] = 0xABCDEF00 + i;
    context.Esp = reinterpret_cast<DWORD>(stack);
    EXCEPTION_POINTERS pointers{&record, &context};
    const auto savedContext = context; const auto savedRecord = record;
    SetLastError(0x10203040);
    CHECK(observeException(&pointers) == EXCEPTION_CONTINUE_SEARCH);
    CHECK(GetLastError() == 0x10203040);
    CHECK(!std::memcmp(&context, &savedContext, sizeof(context)));
    CHECK(!std::memcmp(&record, &savedRecord, sizeof(record)));
    auto text = contents(path);
    CHECK(text.find("code=0xC0000005") != text.npos);
    CHECK(text.find("parameter=0x87654321") != text.npos);
    CHECK(text.find("0xABCDEF3F") != text.npos);
    CHECK(text.find("not proof of crash") != text.npos);
    CHECK(handledAccessViolation()); // observer must allow the application's own SEH to run
    const auto prior = eventCount;
    record.ExceptionCode = 0xE06D7363; // ordinary C++ exception: no event
    CHECK(observeException(&pointers) == EXCEPTION_CONTINUE_SEARCH && eventCount == prior);
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    busy = 1;
    CHECK(observeException(&pointers) == EXCEPTION_CONTINUE_SEARCH && eventCount == prior);
    busy = 0;
    void* guard = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    CHECK(guard != nullptr);
    if (guard) {
        DWORD old{}; CHECK(VirtualProtect(guard, 4096, PAGE_READWRITE | PAGE_GUARD, &old));
        context.Esp = reinterpret_cast<DWORD>(guard);
        CHECK(observeException(&pointers) == EXCEPTION_CONTINUE_SEARCH);
        MEMORY_BASIC_INFORMATION info{}; VirtualQuery(guard, &info, sizeof(info));
        CHECK((info.Protect & PAGE_GUARD) != 0);
        VirtualFree(guard, 0, MEM_RELEASE);
    }
    context.Esp = 1;
    for (unsigned i = 0; i < 300; ++i) CHECK(observeException(&pointers) == EXCEPTION_CONTINUE_SEARCH);
    CHECK(eventCount == maximumEvents);
    CHECK(contents(path).find("SESSION LIMIT reached") != std::string::npos);
    removeExceptionDiagnostics(); removeExceptionDiagnostics();
    const auto size = fs::file_size(path);
    CHECK(handledAccessViolation());
    CHECK(fs::file_size(path) == size);
    CHECK(openExceptionDiagnostics(path.c_str(), nullptr, nullptr));
    CHECK(eventCount == 0 && handledAccessViolation() && eventCount == 1);
    removeExceptionDiagnostics();
    CHECK(fs::file_size(path) > size); // append survives reopening
    std::printf("Diagnostics hashes/first-chance SEH/context/guards/budget/remove: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
