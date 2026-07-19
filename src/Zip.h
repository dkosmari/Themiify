/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

#include <zip.h>

// RAII types for zip archives.

namespace Zip {

    struct Archive;
    struct File;

    struct Error : std::runtime_error {
        Error(const std::string& msg, int code);
        Error(const std::string& msg, zip_t* z);
        Error(const std::string& msg, zip_file_t* zf);
    };

    struct Archive {

        Archive()
            noexcept;

        Archive(const std::filesystem::path& filename,
                int flags);

        ~Archive();

        // Move constructor.
        Archive(Archive&& other)
            noexcept;

        // Move assignment.
        Archive&
        operator =(Archive&& other)
            noexcept;

        void
        open(const std::filesystem::path& filename,
             int flags);

        bool
        is_open()
            const noexcept;

        // alias for is_open()
        operator bool()
            const noexcept;

        void
        close()
            noexcept;

        std::uint64_t
        get_num_entries(zip_flags_t flags)
            const;

        std::filesystem::path
        get_name(std::uint64_t index,
                 zip_flags_t flags)
            const;

        File
        fopen(const std::filesystem::path& filename,
              zip_flags_t flags = 0);

        File
        fopen(std::uint64_t index,
              zip_flags_t flags = 0);

        zip_stat_t
        stat(const std::filesystem::path& filename,
             zip_flags_t flags = 0)
            const;

        zip_stat_t
        stat(std::uint64_t index,
             zip_flags_t flags = 0)
            const;

    private:

        zip_t* handle = nullptr;

    }; // struct Archive


    struct File {

        File()
            noexcept;

        explicit
        File(zip_file_t* zf)
            noexcept;

        ~File()
            noexcept;

        // Move constructor
        File(File&& other)
            noexcept;

        // Move assignment
        File&
        operator =(File&& other)
            noexcept;

        bool
        is_open()
            const noexcept;

        // alias for is_open()
        operator bool()
            const noexcept;

        void
        close()
            noexcept;

        std::size_t
        read(std::span<std::byte> dest);

        // Helper overload
        template<typename T,
                 std::size_t E>
        std::size_t
        read(std::span<T, E> dest)
        {
            return read(std::as_writable_bytes(dest));
        }

    private:

        zip_file_t* handle = nullptr;

    }; // struct File

} // namespace Zip
