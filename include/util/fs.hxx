#ifndef YORK_FS_HXX
#define YORK_FS_HXX

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace york::util::fs {
    inline static std::vector<char> read_file_bytes(std::filesystem::path const &path) {
        // open the file in binary mode at the end to quickly check size if needed,
        // or just open normally and use std::filesystem::file_size.
        std::ifstream file(path, std::ios::binary | std::ios::ate);

        if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path.string());

        // get the file size using tellg()
        const auto end = file.tellg();

        if (end < 0) throw std::runtime_error("Failed to determine file size: " + path.string());

        // pre-allocate the vector buffer
        std::vector<char> buffer(static_cast<std::size_t>(end));

        // move back to the beginning of the file
        file.seekg(0, std::ios::beg);

        // read da bytes into the buffer
        if (!buffer.empty() && !file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())))
            throw std::runtime_error("Failed to read file: " + path.string());

        return buffer;
    }

    inline static void write_file_bytes(std::filesystem::path const &path, std::vector<char> const &data) {
        // open the file as ofstream
        std::ofstream file(path, std::ios::binary);

        // if it dont exist or doesnt open throw the error bruh
        if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path.string());

        // write it!!!!!!1!1!
        if (!data.empty()) file.write(data.data(), static_cast<std::streamsize>(data.size()));

        if (!file) throw std::runtime_error("Failed to write file: " + path.string());
    }

    inline static std::string read_file_string(std::filesystem::path const &path) {
        // sigh open the file first
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path.string());

        // now we commence the file byte reading ritual
        const auto end = file.tellg();
        if (end < 0) throw std::runtime_error("Failed to determine file size: " + path.string());

        std::vector<char> buffer(static_cast<std::size_t>(end));
        file.seekg(0, std::ios::beg);

        if (!buffer.empty() && !file.read(buffer.data(), static_cast<std::streamsize>(buffer.size())))
            throw std::runtime_error("Failed to read file: " + path.string());

        // and now the byte vec to string ritual
        // we return an actual string so it owns the data instead of dangling like a string_view
        return std::string(buffer.begin(), buffer.end());
    }

    inline static void write_file_string(std::filesystem::path const &path, std::string const &data) {
        // ohh my godddd ugh open the file output mode hurry
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) throw std::runtime_error("Failed to open file: " + path.string());

        // bruhh now conv given str to byte vec
        // stfu clangd
        // ok finally gosh now write
        file.write(data.data(), static_cast<std::streamsize>(data.size()));

        if (!file) throw std::runtime_error("Failed to write file: " + path.string());
    }
}

#endif // YORK_FS_HXX