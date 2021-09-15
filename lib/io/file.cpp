#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
#include <memory>
#include <system_error>
#include <vector>

#include "asio/posix/descriptor_base.hpp"
#include "fmt/format.h"
#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/async/executor.h"
#include "nhope/io/file.h"
#include "nhope/io/io-device.h"

namespace nhope {

namespace {
using namespace std::literals;

class FileDevice final : public IoDevice
{
public:
    explicit FileDevice(Executor& e, const FileSettings& settings)
      : m_mode(settings.mode)
      , m_ctx(e)
    {
        std::ios_base::openmode flags = std::ios::binary;
        switch (settings.mode) {
        case FileMode::ReadOnly:
            flags |= std::ios::in;
            break;
        case FileMode::WriteOnly:
            flags |= std::ios::out;
            break;
        case FileMode::ReadWrite:
            flags |= std::ios::in | std::ios::out;
            break;
        }

        m_file.open(settings.fileName, flags);
        if (!m_file.is_open()) {
            std::error_code errc(errno, std::system_category());
            throw IoError(errc, fmt::format(R"(file "{0}" was not opened)", settings.fileName));
        }
    }

    Future<std::vector<std::uint8_t>> read(size_t bytesCount) override
    {
        return nhope::asyncInvoke(m_ctx, [this, bytesCount] {
            checkFile();
            std::vector<std::uint8_t> res(bytesCount);
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            m_file.read(reinterpret_cast<char*>(res.data()), static_cast<long>(bytesCount));
            res.resize(static_cast<std::size_t>(m_file.gcount()));
            return res;
        });
    }

    Future<size_t> write(gsl::span<const std::uint8_t> data) override
    {
        std::vector<char> send(data.begin(), data.end());
        return nhope::asyncInvoke(m_ctx, [this, send = std::move(send)] {
            if (m_mode == FileMode::ReadOnly) {
                throw IoError("can`t write on read only device"sv);
            }
            checkFile();
            auto before = m_file.tellp();
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            if (m_file.write(reinterpret_cast<const char*>(send.data()), static_cast<long>(send.size()))) {
                m_file.flush();
                return static_cast<std::size_t>(m_file.tellp() - before);
            }
            throw IoError("data was not been writted on file"sv);
        });
    }

    [[nodiscard]] Executor& executor() const override
    {
        return m_ctx.executor();
    }

private:
    void checkFile()
    {
        if (m_file.eof()) {
            throw IoEof();
        }
        if (m_file.fail()) {
            std::error_code errc(errno, std::system_category());
            throw IoError(errc, "file error");
        }
    }

    std::fstream m_file;
    const FileMode m_mode;
    mutable nhope::AOContext m_ctx;
};

}   // namespace

std::unique_ptr<IoDevice> openFile(nhope::Executor& executor, const FileSettings& settings)
{
    return std::make_unique<FileDevice>(executor, settings);
}

Future<std::vector<std::uint8_t>> readFile(const std::filesystem::path& fileName, Executor& executor)
{
    FileSettings s;
    s.mode = FileMode::ReadOnly;
    s.fileName = fileName.string();
    std::shared_ptr<IoDevice> d = openFile(executor, s);
    auto ctx = std::make_shared<AOContext>(executor);
    return readAll(*d).then(*ctx, [d, ctx](auto r) {
        return r;
    });
}

}   // namespace nhope
