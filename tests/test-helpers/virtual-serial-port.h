#pragma once

#include <csignal>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <sys/wait.h>
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

        const auto cmd =
          fmt::format("socat -d -d pty,link={0},rawer pty,link={1},rawer 2>/dev/null & echo $! ", com1, com2);

        // NOLINTNEXTLINE
        auto* pipe = popen(cmd.c_str(), "r");
        if (pipe == nullptr) {
            throw std::runtime_error("Unable to start socat");
        }

        // NOLINTNEXTLINE
        if (std::fscanf(pipe, "%i", &m_pid) != 1) {
            throw std::runtime_error("Unable to get the socat pid");
        }
        pclose(pipe);

        while (!(std::filesystem::exists(com1) && !std::filesystem::exists(com2))) {
            std::this_thread::yield();
        }
    }

    ~VirtualSerialPort()
    {
        kill(m_pid, SIGINT);

        int wstatus = 0;
        waitpid(m_pid, &wstatus, 0);
    }

private:
    int m_pid{-1};
};

}   // namespace nhope::test
