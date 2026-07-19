/*
 * Themiify - A theme manager for the Nintendo Wii U
 * Copyright (C) 2026 Fangal-Airbag
 * Copyright (C) 2026 AlphaCraft9658
 * Copyright (C) 2026 Daniel K. O. <dkosmari>
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "Zip.h"

using namespace std::literals;

namespace Zip {

    namespace {

        std::string
        get_error_message(int code)
        {
            zip_error_t e;
            zip_error_init_with_code(&e, code);
            try {
                std::string result = zip_error_strerror(&e);
                zip_error_fini(&e);
                return result;
            }
            catch (...) {
                zip_error_fini(&e);
                return "";
            }
        }

        std::string
        get_error_message(zip_t* z)
        {
            auto e = zip_get_error(z);
            // TODO: do we need to fini errors we didn't init?
            return zip_error_strerror(e);
        }

        std::string
        get_error_message(zip_file_t* zf)
        {
            auto e = zip_file_get_error(zf);
            // TODO: do we need to fini errors we didn't init?
            return zip_error_strerror(e);
        }

    } // namespace

    // ----- //
    // Error //
    // ----- //

    Error::Error(const std::string& msg,
                 int code) :
        std::runtime_error{msg + ": "s + get_error_message(code)}
    {}

    Error::Error(const std::string& msg,
                 zip_t* z) :
        std::runtime_error{msg + ": "s + get_error_message(z)}
    {}

    Error::Error(const std::string& msg,
                 zip_file_t* zf) :
        std::runtime_error{msg + ": "s + get_error_message(zf)}
    {}

    // ------- //
    // Archive //
    // ------- //

    Archive::Archive()
        noexcept = default;

    Archive::Archive(const std::filesystem::path& filename,
                     int flags)
    {
        open(filename, flags);
    }

    Archive::~Archive()
    {
        close();
    }

    Archive::Archive(Archive&& other)
        noexcept :
        handle{other.handle}
    {
        other.handle = nullptr;
    }

    Archive&
    Archive::operator =(Archive&& other)
        noexcept
    {
        if (this != &other) {
            close();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    void
    Archive::open(const std::filesystem::path& filename,
                  int flags)
    {
        close();
        int error_code;
        handle = zip_open(filename.c_str(), flags, &error_code);
        if (!handle)
            throw Error{"zip_open()", error_code};
    }

    bool
    Archive::is_open()
        const noexcept
    {
        return handle;
    }

    Archive::operator bool()
        const noexcept
    {
        return is_open();
    }

    void
    Archive::close()
        noexcept
    {
        if (handle) {
            zip_close(handle);
            handle = nullptr;
        }
    }

    std::uint64_t
    Archive::get_num_entries(zip_flags_t flags)
        const
    {
        auto result = zip_get_num_entries(handle, flags);
        if (result < 0)
            throw Error{"zip_get_num_entries()", handle};
        return result;
    }

    std::filesystem::path
    Archive::get_name(std::uint64_t index,
                      zip_flags_t flags)
        const
    {
        auto result = zip_get_name(handle, index, flags);
        if (!result)
            throw Error{"zip_get_name()", handle};
        return result;
    }

    File
    Archive::fopen(const std::filesystem::path& filename,
                   zip_flags_t flags)
    {
        auto f = zip_fopen(handle, filename.c_str(), flags);
        if (!f)
            throw Error{"zip_fopen()", handle};
        return File{f};
    }

    File
    Archive::fopen(std::uint64_t index,
                   zip_flags_t flags)
    {
        auto f = zip_fopen_index(handle, index, flags);
        if (!f)
            throw Error{"zip_fopen_index()", handle};
        return File{f};
    }

    zip_stat_t
    Archive::stat(const std::filesystem::path& filename,
                  zip_flags_t flags)
        const
    {
        zip_stat_t result;
        zip_stat_init(&result);
        if (auto e = zip_stat(handle, filename.c_str(), flags, &result))
            throw Error{"zip_stat()", e};
        return result;
    }

    zip_stat_t
    Archive::stat(std::uint64_t index,
             zip_flags_t flags)
        const
    {
        zip_stat_t result;
        zip_stat_init(&result);
        if (auto e = zip_stat_index(handle, index, flags, &result))
            throw Error{"zip_stat_index()", e};
        return result;
    }

    // ---- //
    // File //
    // ---- //

    File::File()
        noexcept = default;

    File::File(zip_file_t* zf)
        noexcept :
        handle{zf}
    {}

    File::~File()
        noexcept
    {
        close();
    }

    File::File(File&& other)
        noexcept :
        handle{other.handle}
    {
        other.handle = nullptr;
    }

    File&
    File::operator =(File&& other)
        noexcept
    {
        if (this != &other) {
            close();
            handle = other.handle;
            other.handle = nullptr;
        }
        return *this;
    }

    bool
    File::is_open()
        const noexcept
    {
        return handle;
    }

    File::operator bool()
        const noexcept
    {
        return is_open();
    }

    void
    File::close()
        noexcept
    {
        if (handle) {
            zip_fclose(handle);
            handle = nullptr;
        }
    }

    std::size_t
    File::read(std::span<std::byte> dest)
    {
        auto result = zip_fread(handle, dest.data(), dest.size());
        if (result < 0)
            throw Error{"zip_fread()", handle};
        return result;
    }


} // namespace Zip
