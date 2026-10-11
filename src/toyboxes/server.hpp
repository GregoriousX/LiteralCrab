// SPDX-License-Identifier: GPL-3.0-only
// Toy Box packages from web servers. A server is an http:// or https:// folder URL (a URL without
// a scheme, such as localhost:8000, means http://); its packages
// may sit in any subfolders, under any names. The server lists them in index.txt beside the URL
// (one path relative to it per line; empty lines and lines starting with # are ignored), or, when
// there is no index.txt, in the HTML directory listing most web servers generate, followed into
// subfolders below the URL. Packages are mirrored into the local folder (new and changed ones
// are downloaded, ones the server no longer lists are removed); the library then reads them like
// local packages. Blocking: run it on a background thread.
//
// Servers are untrusted. Only links below a server's URL are followed (no other hosts, no "..");
// listings are capped in size, pages and depth; a package larger than maxPackage (as announced
// or as actually sent) is refused; the whole mirror stays below maxMirror; at most maxPackages
// packages per server and maxServers servers are read; a sync stops after maxSeconds. What does
// arrive is still checked package by package by the library before the game sees it.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace crabe::toyboxes {
using LogFunction = void (*)(const char* format, ...);
struct ServerLimits {
    std::uint64_t maxPackage = 32ull << 20;
    std::uint64_t maxMirror = 1024ull << 20;
    std::size_t maxPackages = 2000, maxServers = 8;
    unsigned maxSeconds = 600;
};
// Returns true when a package was added, changed or removed.
bool syncServers(const std::vector<std::wstring>& urls, const std::wstring& folder, LogFunction log,
    const ServerLimits& limits = {});
}
