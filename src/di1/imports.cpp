// SPDX-License-Identifier: GPL-3.0-only
#include "di1/imports.hpp"
#include "di1/common.hpp"
#include <cstring>

namespace crabe::di1 {
namespace {
    std::uintptr_t hostBase{}, hostSize{};
    struct DelayDescriptor {
        DWORD attributes, dll, handle, iat, names, bound, unload, timestamp;
    };
    struct Image {
        BYTE* base{};
        IMAGE_NT_HEADERS32* nt{};
        explicit Image(HMODULE module) {
            base = reinterpret_cast<BYTE*>(module);
            if (!readable(base, sizeof(IMAGE_DOS_HEADER))) return;
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x100000) return;
            auto candidate = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
            if (!readable(candidate, sizeof(*candidate)) || candidate->Signature != IMAGE_NT_SIGNATURE ||
                candidate->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return;
            nt = candidate;
        }
        template<typename T> T* at(DWORD rva, std::size_t size = sizeof(T)) const {
            if (!nt || rva >= nt->OptionalHeader.SizeOfImage || size > nt->OptionalHeader.SizeOfImage - rva ||
                !readable(base + rva, size)) return nullptr;
            return reinterpret_cast<T*>(base + rva);
        }
        bool nameIs(DWORD rva, const char* text) const {
            const char* actual = at<char>(rva, strlen(text) + 1);
            return actual && _stricmp(actual, text) == 0;
        }
        void** find(DWORD namesRva, DWORD iatRva, const char* name) const {
            for (DWORD i = 0; i < 65536; ++i) {
                auto entry = at<IMAGE_THUNK_DATA32>(namesRva + i * sizeof(IMAGE_THUNK_DATA32));
                auto thunk = at<IMAGE_THUNK_DATA32>(iatRva + i * sizeof(IMAGE_THUNK_DATA32));
                if (!entry || !thunk || !entry->u1.AddressOfData) return nullptr;
                if (IMAGE_SNAP_BY_ORDINAL32(entry->u1.Ordinal)) continue;
                if (nameIs(entry->u1.AddressOfData + offsetof(IMAGE_IMPORT_BY_NAME, Name), name))
                    return reinterpret_cast<void**>(&thunk->u1.Function);
            }
            return nullptr;
        }
    };
}
void setHostModule(HMODULE module) noexcept {
    Image image(module);
    hostBase = reinterpret_cast<std::uintptr_t>(module);
    hostSize = image.nt ? image.nt->OptionalHeader.SizeOfImage : 0;
}
bool hostCaller(const void* address) noexcept {
    const auto value = reinterpret_cast<std::uintptr_t>(address);
    return value >= hostBase && value - hostBase < hostSize;
}
bool ImportPatch::install(HMODULE module, const char* dll, const char* name, void* detour) {
    if (slot) return true;
    Image image(module);
    if (!image.nt) return false;
    const auto normal = image.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    for (DWORD i = 0; normal.VirtualAddress && (i + 1) * sizeof(IMAGE_IMPORT_DESCRIPTOR) <= normal.Size; ++i) {
        auto entry = image.at<IMAGE_IMPORT_DESCRIPTOR>(normal.VirtualAddress + i * sizeof(IMAGE_IMPORT_DESCRIPTOR));
        if (!entry || !entry->Name) break;
        if (image.nameIs(entry->Name, dll)) {
            slot = image.find(entry->OriginalFirstThunk, entry->FirstThunk, name);
            if (slot) break;
        }
    }
    const auto delay = image.nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_DELAY_IMPORT];
    for (DWORD i = 0; !slot && delay.VirtualAddress && (i + 1) * sizeof(DelayDescriptor) <= delay.Size; ++i) {
        auto entry = image.at<DelayDescriptor>(delay.VirtualAddress + i * sizeof(DelayDescriptor));
        if (!entry || !entry->dll) break;
        if (entry->attributes == 1 && image.nameIs(entry->dll, dll)) slot = image.find(entry->names, entry->iat, name);
    }
    if (!slot) { log("Import not found: %s!%s.", dll, name); return false; }
    DWORD old{};
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { slot = nullptr; return false; }
    original = *slot;
    replacement = detour;
    InterlockedExchangePointer(slot, detour);
    DWORD unused{};
    VirtualProtect(slot, sizeof(void*), old, &unused);
    log("Hooked host import %s!%s.", dll, name);
    return true;
}
void ImportPatch::remove() {
    if (!slot) return;
    DWORD old{};
    if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) {
        InterlockedCompareExchangePointer(slot, original, replacement);
        DWORD unused{};
        VirtualProtect(slot, sizeof(void*), old, &unused);
    }
    slot = nullptr;
}
}
