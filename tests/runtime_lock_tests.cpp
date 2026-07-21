#include "limine_manager/application/apply_service.hpp"
#include "limine_manager/application/change_planner.hpp"
#include "limine_manager/infrastructure/filesystem.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

[[noreturn]] void fail(const std::string &message) {
    throw std::runtime_error(message + ": " + std::strerror(errno));
}

void write_byte(int fd, char value) {
    while (true) {
        const auto count = ::write(fd, &value, 1);
        if (count == 1)
            return;
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            fail("pipe write failed");
        throw std::runtime_error("pipe write returned an unexpected byte count");
    }
}

void read_byte(int fd) {
    char value{};
    while (true) {
        const auto count = ::read(fd, &value, 1);
        if (count == 1)
            return;
        if (count == 0)
            throw std::runtime_error("pipe closed before synchronization byte was received");
        if (errno == EINTR)
            continue;
        fail("pipe read failed");
    }
}

void close_fd(int fd) {
    if (::close(fd) < 0)
        fail("close failed");
}

} // namespace

int main() {
    using namespace limine_manager;
    const auto root = std::filesystem::temp_directory_path() /
                      ("limine-manager-runtime-lock-test-" + std::to_string(::getpid()));
    const auto boot = root / "boot";
    const auto runtime = root / "run";
    const auto lock_path = runtime / "apply.lock";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(boot);
    std::filesystem::create_directories(runtime);

    const auto target = boot / "limine.conf";
    std::ofstream(target) << "timeout: 5\n";

    infrastructure::RealFileSystem filesystem;
    application::ChangePlanner planner(filesystem);
    application::ApplyService service(runtime);
    const auto plan = planner.build(target, "timeout: 10\n");

    int ready_pipe[2]{};
    int release_pipe[2]{};
    if (::pipe(ready_pipe) < 0)
        fail("cannot create ready pipe");
    if (::pipe(release_pipe) < 0) {
        const auto saved_errno = errno;
        ::close(ready_pipe[0]);
        ::close(ready_pipe[1]);
        errno = saved_errno;
        fail("cannot create release pipe");
    }

    const auto child = ::fork();
    if (child < 0)
        fail("fork failed");
    if (child == 0) {
        ::close(ready_pipe[0]);
        ::close(release_pipe[1]);

        const int lock_fd =
            ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock_fd < 0 || ::flock(lock_fd, LOCK_EX) < 0)
            _exit(10);

        const char ready = 'R';
        if (::write(ready_pipe[1], &ready, 1) != 1)
            _exit(11);
        ::close(ready_pipe[1]);

        char release{};
        while (true) {
            const auto count = ::read(release_pipe[0], &release, 1);
            if (count == 1)
                break;
            if (count == 0)
                _exit(12);
            if (errno != EINTR)
                _exit(13);
        }

        ::close(release_pipe[0]);
        ::close(lock_fd);
        _exit(0);
    }

    close_fd(ready_pipe[1]);
    close_fd(release_pipe[0]);
    read_byte(ready_pipe[0]);

    bool concurrent_apply_rejected = false;
    try {
        (void)service.apply(plan);
    } catch (const std::runtime_error &error) {
        concurrent_apply_rejected =
            std::string(error.what()) == "another limine-manager apply operation is active";
    }

    assert(concurrent_apply_rejected);
    assert(filesystem.read_text(target) == "timeout: 5\n");
    assert(std::filesystem::exists(lock_path));
    assert(!std::filesystem::exists(boot / "limine.conf.lock"));

    for (const auto &entry : std::filesystem::directory_iterator(boot))
        assert(entry.path() == target);

    write_byte(release_pipe[1], 'X');
    close_fd(ready_pipe[0]);
    close_fd(release_pipe[1]);

    int child_status{};
    const auto waited = ::waitpid(child, &child_status, 0);
    if (waited < 0)
        fail("waitpid failed");
    if (waited != child)
        throw std::runtime_error("waitpid returned an unexpected process id");
    assert(WIFEXITED(child_status));
    assert(WEXITSTATUS(child_status) == 0);

    const auto result = service.apply(plan);
    assert(result.changed);
    assert(!result.backup.empty());
    assert(filesystem.read_text(target) == "timeout: 10\n");
    assert(std::filesystem::exists(result.backup));

    std::filesystem::remove_all(root);
    return 0;
}
