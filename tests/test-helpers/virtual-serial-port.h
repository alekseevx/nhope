#pragma once

#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <thread>
#include <unistd.h>

#include "fmt/core.h"

namespace nhope::test {

class VirtualSerialPort final
{
public:
    VirtualSerialPort(std::string_view com1, std::string_view com2)
    {
        using namespace std::literals;
        const auto cmd = fmt::format("socat -d -d pty,link={0},rawer pty,link={1},rawer & echo $!", com1, com2);
        auto* const pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            throw std::runtime_error("popen() failed!");
        }
        fscanf(pipe, "%i", &m_pid);
        pclose(pipe);
        while (!(std::filesystem::exists(com1) && std::filesystem::exists(com2))) {
            std::this_thread::yield();
        }
    }

    ~VirtualSerialPort()
    {
        const auto cmd = fmt::format("kill -9 {}", m_pid);
        std::system(cmd.c_str());
    }

private:
    int m_pid{-1};
};

}   // namespace nhope::test