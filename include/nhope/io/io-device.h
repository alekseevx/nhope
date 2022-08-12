#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <list>
#include <memory>

#include <gsl/span>

#include "nhope/async/future.h"
#include "nhope/utils/noncopyable.h"

namespace nhope {

class AOContext;

using IOHandler = std::function<void(std::exception_ptr, std::size_t)>;

class Reader : public Noncopyable
{
public:
    virtual ~Reader() = default;

    virtual void read(gsl::span<std::uint8_t> buf, IOHandler handler) = 0;
};
using ReaderPtr = std::unique_ptr<Reader>;

class Writter : public Noncopyable
{
public:
    virtual ~Writter() = default;

    virtual void write(gsl::span<const std::uint8_t> data, IOHandler handler) = 0;
};
using WritterPtr = std::unique_ptr<Writter>;

class IODevice
  : public Reader
  , public Writter
{};
using IODevicePtr = std::unique_ptr<IODevice>;

Future<std::vector<std::uint8_t>> read(Reader& dev, std::size_t bytesCount);
Future<std::size_t> write(Writter& dev, std::vector<std::uint8_t> data);

Future<std::vector<std::uint8_t>> readExactly(Reader& dev, std::size_t bytesCount);
Future<std::size_t> writeExactly(Writter& dev, std::vector<std::uint8_t> data);

Future<std::vector<std::uint8_t>> readUntil(Reader& dev, std::vector<std::uint8_t> expect);

Future<std::string> readLine(Reader& dev);
Future<std::vector<std::uint8_t>> readAll(Reader& dev);
Future<std::vector<std::uint8_t>> readAll(ReaderPtr dev);

Future<std::size_t> copy(Reader& src, Writter& dest);
Future<std::size_t> copy(ReaderPtr src, WritterPtr dest);

ReaderPtr concat(AOContext& aoCtx, std::list<ReaderPtr> readers);

template<typename... ReaderT>
ReaderPtr concat(AOContext& aoCtx, std::unique_ptr<ReaderT>... reader)
{
    std::list<ReaderPtr> readers;
    (readers.push_back(std::move(reader)), ...);
    return concat(aoCtx, std::move(readers));
}

}   // namespace nhope
