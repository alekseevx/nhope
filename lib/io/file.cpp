#ifdef WIN32
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <cerrno>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <string>
#include <system_error>
#include <tuple>

#include <fmt/format.h>

#include "nhope/async/ao-context.h"
#include "nhope/async/async-invoke.h"
#include "nhope/io/file.h"
#include "nhope/io/io-device.h"
#include "nhope/io/detail/asio-device-wrapper.h"

#include "io-thread-pool.h"

namespace nhope {

namespace {

std::string toFOpenMode(OpenFileMode mode)
{
    switch (mode) {
    case OpenFileMode::ReadOnly:
        return "rb";
    case OpenFileMode::WriteOnly:
        return "wb";
    default:
        throw std::logic_error("Invalid OpenFileMode");
    }
}

class FileImpl final : public File
{
public:
    FileImpl(AOContext& parent, std::string_view fileName, OpenFileMode mode)
      : m_resultCtx(parent)
      /* Reading/writing over regular files can be synchronous, will do them */
      , m_ioCtx(detail::ioThreadPool())
    {
        const auto cstrFileName = std::string(fileName);   // c_str
        const auto fopenMode = toFOpenMode(mode);

        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        m_file = std::fopen(cstrFileName.c_str(), fopenMode.c_str());
        if (m_file == nullptr) {
            const auto err = std::error_code(errno, std::system_category());
            throw std::system_error(err, fmt::format("Unable to open '{}'", fileName));
        }
    }

    ~FileImpl() override
    {
        m_ioCtx.close();
        m_resultCtx.close();

        if (m_file != nullptr) {
            // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
            std::fclose(m_file);
        }
    }

    void read(gsl::span<std::uint8_t> buf, IOHandler handler) override
    {
        asyncInvoke(m_ioCtx, [this, buf] {
            const auto n = std::fread(buf.data(), 1, buf.size(), m_file);
            return std::tuple(this->currError(), n);
        }).then(m_resultCtx, [handler = std::move(handler)](auto args) {
            handler(std::get<0>(args), std::get<1>(args));
        });
    }

    void write(gsl::span<const std::uint8_t> data, IOHandler handler) override
    {
        asyncInvoke(m_ioCtx, [this, data] {
            const auto n = std::fwrite(data.data(), 1, data.size(), m_file);
            return std::tuple(this->currError(), n);
        }).then(m_resultCtx, [handler = std::move(handler)](auto args) {
            handler(std::get<0>(args), std::get<1>(args));
        });
    }

private:
    [[nodiscard]] std::exception_ptr currError() const
    {
        const auto err = std::ferror(m_file);
        return detail::toExceptionPtr(std::error_code(err, std::system_category()));
    }

private:
    FILE* m_file = nullptr;

    AOContext m_resultCtx;
    AOContext m_ioCtx;
};

}   // namespace

nhope::FilePtr File::open(AOContext& aoCtx, std::string_view fileName, OpenFileMode mode)
{
    return std::make_unique<FileImpl>(aoCtx, fileName, mode);
}

Future<std::vector<std::uint8_t>> File::readAll(AOContext& aoCtx, std::string_view fileName)
{
    auto file = File::open(aoCtx, fileName, OpenFileMode::ReadOnly);
    return nhope::readAll(std::move(file));
}

}   // namespace nhope
