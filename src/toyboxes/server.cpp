// SPDX-License-Identifier: GPL-3.0-only
#include "toyboxes/server.hpp"
#include <windows.h>
#include <winhttp.h>
#include <algorithm>
#include <iterator>
#include <cwchar>
#include <cwctype>
#include <set>
#include <string_view>

namespace crabe::toyboxes {
namespace {
    constexpr std::size_t maxListing = 4u << 20, maxPages = 1000, maxDepth = 8;
    constexpr DWORD maxRedirects = 3;

    struct Handle {
        HINTERNET value{};
        Handle() = default;
        explicit Handle(HINTERNET handle) : value(handle) {}
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        ~Handle() { if (value) WinHttpCloseHandle(value); }
        void reset(HINTERNET handle) noexcept {
            if (value) WinHttpCloseHandle(value);
            value = handle;
        }
        explicit operator bool() const noexcept { return value != nullptr; }
    };

    struct Response {
        Handle connection, request;
        DWORD status{};
        std::uint64_t length = ~0ull;  // Content-Length, or unknown.
        std::wstring type;
    };
    // Sends a GET and reads the status and headers; the body is read by the caller.
    bool open(HINTERNET session, const std::wstring& url, Response& response) {
        URL_COMPONENTS parts{sizeof(parts)};
        wchar_t host[256]{}, path[4096]{};
        parts.lpszHostName = host; parts.dwHostNameLength = 256;
        parts.lpszUrlPath = path; parts.dwUrlPathLength = 4096;
        if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
            (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)) return false;
        response.connection.reset(WinHttpConnect(session, host, parts.nPort, 0));
        if (!response.connection) return false;
        response.request.reset(WinHttpOpenRequest(response.connection.value, L"GET", path, nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
        if (!response.request || !WinHttpSendRequest(response.request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) || !WinHttpReceiveResponse(response.request.value, nullptr)) return false;
        DWORD size = sizeof(response.status);
        WinHttpQueryHeaders(response.request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &response.status, &size, WINHTTP_NO_HEADER_INDEX);
        wchar_t text[256]{};
        size = sizeof(text);
        if (WinHttpQueryHeaders(response.request.value, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                text, &size, WINHTTP_NO_HEADER_INDEX))
            response.length = _wcstoui64(text, nullptr, 10);
        size = sizeof(text);
        if (WinHttpQueryHeaders(response.request.value, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
                text, &size, WINHTTP_NO_HEADER_INDEX))
            response.type = text;
        return true;
    }
    // Reads the body into out (and file, when given), up to limit bytes.
    bool body(Response& response, std::uint64_t limit, std::string* out, HANDLE file) {
        std::uint64_t total = 0;
        char buffer[65536];
        for (;;) {
            DWORD read{};
            if (!WinHttpReadData(response.request.value, buffer, sizeof(buffer), &read)) return false;
            if (!read) return response.length == ~0ull || total == response.length;
            if ((total += read) > limit) return false;
            if (out) out->append(buffer, read);
            DWORD written{};
            if (file && (!WriteFile(file, buffer, read, &written, nullptr) || written != read)) return false;
        }
    }

    std::wstring widen(std::string_view text) {
        const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring out(size > 0 ? size : 0, L'\0');
        if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
        return out;
    }
    // Log text as UTF-8: the CRT's %ls conversion drops non-ASCII names.
    std::string utf8(const std::wstring& text) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size > 0 ? size : 0, '\0');
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }
    std::wstring decode(std::wstring_view text) {
        std::string bytes;
        for (std::size_t at = 0; at < text.size(); ++at) {
            if (text[at] == L'%' && at + 2 < text.size() && iswxdigit(text[at + 1]) && iswxdigit(text[at + 2])) {
                bytes.push_back(static_cast<char>(std::wcstoul(std::wstring(text.substr(at + 1, 2)).c_str(), nullptr, 16)));
                at += 2;
            } else if (text[at] < 0x80) {
                bytes.push_back(static_cast<char>(text[at]));
            } else {
                const auto wide = std::wstring(1, text[at]);
                char utf8[8]{};
                const int size = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), 1, utf8, 8, nullptr, nullptr);
                bytes.append(utf8, size > 0 ? size : 0);
            }
        }
        return widen(bytes);
    }
    // A safe local path for a server path below the URL, or empty when it leaves the folder.
    std::wstring localPath(std::wstring_view relative) {
        std::wstring out, part;
        const auto flush = [&] {
            if (part.empty() || part == L".") { part.clear(); return true; }
            if (part == L"..") return false;
            for (auto& c : part) if (c < 32 || wcschr(L"<>:\"|?*\\", c)) c = L'_';
            while (!part.empty() && (part.back() == L'.' || part.back() == L' ')) part.pop_back();
            if (part.empty()) return false;
            if (!out.empty()) out += L'\\';
            out += part;
            part.clear();
            return true;
        };
        for (const auto c : decode(relative)) {
            if (c == L'/') { if (!flush()) return {}; }
            else part += c;
        }
        return flush() ? out : std::wstring();
    }
    std::wstring strip(std::wstring url) {
        const auto cut = url.find_first_of(L"?#");
        if (cut != std::wstring::npos) url.resize(cut);
        return url;
    }
    // The absolute form of a link on page, or empty.
    std::wstring resolve(const std::wstring& page, std::wstring link) {
        link = strip(link);
        if (link.empty()) return {};
        if (link.rfind(L"http://", 0) == 0 || link.rfind(L"https://", 0) == 0) return link;
        const auto scheme = page.find(L"://");
        if (scheme == std::wstring::npos) return {};
        if (link.rfind(L"//", 0) == 0) return page.substr(0, scheme + 1) + link;
        if (link[0] == L'/') return page.substr(0, page.find(L'/', scheme + 3)) + link;
        return page.substr(0, page.rfind(L'/') + 1) + link;
    }
    bool endsWithZip(const std::wstring& url) {
        return url.size() > 4 && !_wcsicmp(url.c_str() + url.size() - 4, L".zip");
    }
    void crawl(HINTERNET session, const std::wstring& base, const std::wstring& page, std::size_t depth,
        std::set<std::wstring>& visited, std::set<std::wstring>& packages, std::size_t& listed, std::size_t cap, LogFunction log) {
        if (depth > maxDepth || visited.size() >= maxPages || packages.size() > cap || !visited.insert(page).second) return;
        Response response;
        std::string html;
        if (!open(session, page, response) || response.status != 200 || !body(response, maxListing, &html, nullptr)) {
            log("Toy Boxes: cannot list %s.", utf8(decode(page)).c_str());
            return;
        }
        ++listed;
        const auto text = widen(html);
        for (std::size_t at = 0; (at = text.find(L"href=", at)) != std::wstring::npos;) {
            at += 5;
            const wchar_t quote = at < text.size() && (text[at] == L'"' || text[at] == L'\'') ? text[at++] : 0;
            const auto end = quote ? text.find(quote, at) : text.find_first_of(L" >\t\r\n", at);
            if (end == std::wstring::npos) break;
            const auto url = resolve(page, text.substr(at, end - at));
            at = end;
            if (url.size() <= base.size() || url.compare(0, base.size(), base)) continue;  // Only below the URL.
            if (packages.size() > cap) return;  // Enough to report that the server lists too many.
            if (endsWithZip(url)) packages.insert(url);
            else if (url.back() == L'/') crawl(session, base, url, depth + 1, visited, packages, listed, cap, log);
        }
    }
    void removeOthers(const std::wstring& folder, const std::set<std::wstring>& keep) {
        WIN32_FIND_DATAW item{};
        const HANDLE find = FindFirstFileExW((folder + L"\\*").c_str(), FindExInfoBasic, &item, FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring name = item.cFileName;
            if (name == L"." || name == L"..") continue;
            const auto path = folder + L"\\" + name;
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeOthers(path, keep);
                RemoveDirectoryW(path.c_str());  // Only succeeds when empty.
            } else if (!keep.count(path)) {
                DeleteFileW(path.c_str());
            }
        } while (FindNextFileW(find, &item));
        FindClose(find);
    }
    void createFolders(const std::wstring& path) {
        for (auto at = path.find(L'\\', 3); at != std::wstring::npos; at = path.find(L'\\', at + 1))
            CreateDirectoryW(path.substr(0, at).c_str(), nullptr);
    }
}

bool syncServers(const std::vector<std::wstring>& urls, const std::wstring& folder, LogFunction log,
    const ServerLimits& limits) {
    if (urls.empty()) {
        removeOthers(folder, {});
        return false;
    }
    Handle session(WinHttpOpen(L"LiteralCrab-ToyBoxes/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) session.reset(WinHttpOpen(L"LiteralCrab-ToyBoxes/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        log("Toy Boxes: WinHTTP unavailable (Win32 %lu); servers skipped.", GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session.value, 10000, 10000, 15000, 30000);
    // Few redirects, never from https:// to http://.
    DWORD option = maxRedirects;
    WinHttpSetOption(session.value, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &option, sizeof(option));
    option = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(session.value, WINHTTP_OPTION_REDIRECT_POLICY, &option, sizeof(option));
    const auto deadline = GetTickCount64() + limits.maxSeconds * 1000ull;
    std::set<std::wstring> keep;
    std::uint64_t mirrored = 0;  // Bytes of the packages kept so far.
    bool changed = false, complete = true;
    std::size_t serverCount = 0;
    for (auto base : urls) {
        if (base.empty()) continue;
        if (++serverCount > limits.maxServers) {
            log("Toy Boxes: only the first %zu servers are read.", limits.maxServers);
            break;
        }
        // "localhost:8000" or "example.org/toyboxes" mean http://.
        if (base.find(L"://") == std::wstring::npos) base = L"http://" + base;
        if (base.back() != L'/') base += L'/';
        const auto scheme = base.find(L"://");
        const auto root = folder + L"\\" + localPath(scheme == std::wstring::npos ? base : base.substr(scheme + 3));
        std::set<std::wstring> packages;
        Response index;
        std::string list;
        if (open(session.value, base + L"index.txt", index) && index.status == 200 &&
            index.type.find(L"html") == std::wstring::npos && body(index, maxListing, &list, nullptr)) {
            for (std::size_t start = 0; start < list.size();) {
                auto end = list.find('\n', start);
                if (end == std::string::npos) end = list.size();
                auto line = std::string_view(list).substr(start, end - start);
                start = end + 1;
                while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);
                while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
                if (line.empty() || line[0] == '#') continue;
                const auto url = resolve(base, widen(line));
                if (url.size() > base.size() && !url.compare(0, base.size(), base)) packages.insert(url);
                if (packages.size() > limits.maxPackages) break;
            }
        } else {
            std::set<std::wstring> visited;
            std::size_t listed = 0;
            crawl(session.value, base, base, 0, visited, packages, listed, limits.maxPackages, log);
            if (!listed) complete = false;
        }
        if (packages.size() > limits.maxPackages) {
            log("Toy Boxes: server %s lists %zu packages; only the first %zu are read.", utf8(decode(base)).c_str(),
                packages.size(), limits.maxPackages);
            packages.erase(std::next(packages.begin(), static_cast<std::ptrdiff_t>(limits.maxPackages)), packages.end());
        }
        unsigned downloaded = 0, failed = 0, refused = 0;
        const auto localSize = [](const std::wstring& path) -> std::uint64_t {
            WIN32_FILE_ATTRIBUTE_DATA local{};
            if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &local)) return ~0ull;
            return static_cast<std::uint64_t>(local.nFileSizeHigh) << 32 | local.nFileSizeLow;
        };
        const auto keepOld = [&](const std::wstring& path) {
            const auto size = localSize(path);
            if (size != ~0ull && size <= limits.maxPackage && mirrored + size <= limits.maxMirror) {
                keep.insert(path);
                mirrored += size;
            }
        };
        for (const auto& url : packages) {
            const auto relative = localPath(url.substr(base.size()));
            if (relative.empty()) continue;
            const auto path = root + L"\\" + relative;
            if (GetTickCount64() > deadline) {  // Out of time: keep what is there, try again next start.
                keepOld(path);
                complete = false;
                continue;
            }
            Response response;
            if (!open(session.value, url, response) || response.status != 200) {
                ++failed;
                keepOld(path);  // Keep the last copy.
                continue;
            }
            const auto known = localSize(path);
            if (response.length != ~0ull && known == response.length) {
                keepOld(path);  // Unchanged: the body is not read.
                continue;
            }
            // Refused before a byte is read when announced too large; otherwise the transfer is
            // cut off as soon as it exceeds what is left.
            const auto room = (std::min)(limits.maxPackage, limits.maxMirror > mirrored ? limits.maxMirror - mirrored : 0);
            if (response.length != ~0ull && response.length > room) {
                ++refused;
                log("Toy Boxes: %s refused (%llu MB; the limit left is %llu MB).", utf8(decode(url)).c_str(),
                    static_cast<unsigned long long>(response.length >> 20), static_cast<unsigned long long>(room >> 20));
                continue;
            }
            createFolders(path);
            const auto temporary = path + L".part";
            const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool ok = file != INVALID_HANDLE_VALUE && body(response, room, nullptr, file);
            if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
            const auto size = ok ? localSize(temporary) : ~0ull;
            ok = ok && size != ~0ull && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
            if (!ok) {
                DeleteFileW(temporary.c_str());
                ++failed;
                keepOld(path);
                continue;
            }
            keep.insert(path);
            mirrored += size;
            ++downloaded;
            changed = true;
        }
        log("Toy Boxes: server %s lists %zu package(s); %u downloaded, %u unavailable, %u refused.", utf8(decode(base)).c_str(),
            packages.size(), downloaded, failed, refused);
    }
    // Packages a reachable server dropped are removed; an unreachable server keeps its copies.
    if (complete) removeOthers(folder, keep);
    return changed;
}
}
