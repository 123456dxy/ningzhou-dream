#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000
#endif
#include <windows.h>
#include <compressapi.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Bytes = std::vector<unsigned char>;
static void integer(Bytes& data, uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) data.push_back(static_cast<unsigned char>(value >> (i * 8)));
}
static void literal(Bytes& data, const char* text, size_t size) {
    data.insert(data.end(), text, text + size);
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 3) throw std::runtime_error("Usage: release_pack STAGING PAYLOAD");
        const fs::path root = fs::canonical(argv[1]);
        std::vector<fs::path> files;
        uint64_t total = 12;
        for (const auto& entry : fs::recursive_directory_iterator(root)) {
            if (entry.is_symlink()) throw std::runtime_error("Staging must not contain links");
            if (!entry.is_regular_file()) continue;
            const auto relative = fs::relative(entry.path(), root);
            const auto name = relative.generic_u8string();
            if (name.size() > 4096 || entry.file_size() > 1024ull * 1024 * 1024) throw std::runtime_error("Invalid package entry size");
            total += 12 + name.size() + entry.file_size();
            files.push_back(relative);
        }
        if (files.empty() || total > 1024ull * 1024 * 1024) throw std::runtime_error("Invalid package size");
        std::sort(files.begin(), files.end());
        Bytes raw; raw.reserve(static_cast<size_t>(total));
        literal(raw, "NZFILE01", 8); integer(raw, files.size(), 4);
        for (const auto& relative : files) {
            const auto name = relative.generic_u8string();
            std::ifstream input(root / relative, std::ios::binary);
            if (!input) throw std::runtime_error("Cannot read staged file");
            const auto size = fs::file_size(root / relative);
            integer(raw, name.size(), 4); integer(raw, size, 8);
            raw.insert(raw.end(), name.begin(), name.end());
            const auto offset = raw.size(); raw.resize(offset + static_cast<size_t>(size));
            if (!input.read(reinterpret_cast<char*>(raw.data() + offset), static_cast<std::streamsize>(size))) throw std::runtime_error("Incomplete staged file");
        }
        COMPRESSOR_HANDLE compressor = nullptr;
        if (!CreateCompressor(COMPRESS_ALGORITHM_XPRESS_HUFF, nullptr, &compressor)) throw std::runtime_error("CreateCompressor failed");
        SIZE_T compressedSize = 0;
        const BOOL queried = Compress(compressor, raw.data(), raw.size(), nullptr, 0, &compressedSize);
        if ((!queried && GetLastError() != ERROR_INSUFFICIENT_BUFFER) || compressedSize == 0) {
            CloseCompressor(compressor); throw std::runtime_error("Cannot calculate compressed size");
        }
        Bytes compressed(compressedSize);
        if (!Compress(compressor, raw.data(), raw.size(), compressed.data(), compressed.size(), &compressedSize)) {
            CloseCompressor(compressor); throw std::runtime_error("Cannot compress payload");
        }
        CloseCompressor(compressor);
        std::ofstream output(fs::path(argv[2]), std::ios::binary | std::ios::trunc);
        Bytes header; literal(header, "NZEXEP01", 8); integer(header, raw.size(), 8); integer(header, COMPRESS_ALGORITHM_XPRESS_HUFF, 4);
        if (!output.write(reinterpret_cast<const char*>(header.data()), header.size()) ||
            !output.write(reinterpret_cast<const char*>(compressed.data()), static_cast<std::streamsize>(compressedSize)))
            throw std::runtime_error("Cannot write payload");
        std::printf("Packed %zu files: %.2f MiB raw, %.2f MiB compressed\n", files.size(), raw.size() / 1048576.0, compressedSize / 1048576.0);
        return 0;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Packaging failed: %s\n", e.what()); return 1;
    }
}
