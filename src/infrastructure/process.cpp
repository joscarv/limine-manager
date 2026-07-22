#include "limine_manager/infrastructure/process.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace limine_manager::infrastructure {

ProcessResult PosixProcessRunner::run(const std::vector<std::string> &arguments) const {
    if (arguments.empty())
        throw std::invalid_argument("Process argument list is empty");

    int pipe_fd[2] {};
    if (::pipe(pipe_fd) == -1)
        throw std::runtime_error("pipe failed: " + std::string(std::strerror(errno)));

    const pid_t pid = ::fork();
    if (pid == -1) {
        ::close(pipe_fd[0]);
        ::close(pipe_fd[1]);
        throw std::runtime_error("fork failed: " + std::string(std::strerror(errno)));
    }

    if (pid == 0) {
        ::close(pipe_fd[0]);
        if (::dup2(pipe_fd[1], STDOUT_FILENO) == -1 || ::dup2(pipe_fd[1], STDERR_FILENO) == -1)
            _exit(126);
        ::close(pipe_fd[1]);

        std::vector<char *> argv;
        argv.reserve(arguments.size() + 1);
        for (const auto &argument : arguments)
            argv.push_back(const_cast<char *>(argument.c_str()));
        argv.push_back(nullptr);
        ::execvp(argv.front(), argv.data());
        _exit(errno == ENOENT ? 127 : 126);
    }

    ::close(pipe_fd[1]);
    std::array<char, 4096> buffer {};
    std::string output;
    while (true) {
        const auto count = ::read(pipe_fd[0], buffer.data(), buffer.size());
        if (count > 0)
            output.append(buffer.data(), static_cast<std::size_t>(count));
        else if (count == 0)
            break;
        else if (errno != EINTR) {
            ::close(pipe_fd[0]);
            throw std::runtime_error("read from child failed: " +
                                     std::string(std::strerror(errno)));
        }
    }
    ::close(pipe_fd[0]);

    int status {};
    while (::waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR)
            throw std::runtime_error("waitpid failed: " + std::string(std::strerror(errno)));
    }
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status)
                                            : 128 + (WIFSIGNALED(status) ? WTERMSIG(status) : 0);
    return {exit_code, std::move(output)};
}

} // namespace limine_manager::infrastructure
