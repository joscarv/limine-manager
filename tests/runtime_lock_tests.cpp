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

void write_byte(int fd, char value) {
    while (::write(fd, &value, 1) < 0) {
        if (errno != EINTR)
            throw std::runtime_error(std::string("pipe write failed: ") + std::strerror(errno));
    }
}

void read_byte(int fd) {
    char value{};
    while (::read(fd, &value, 1) < 0) {
        if (errno != EINTR)
            throw std::runtime_error(std::string("pipe read failed: ") + std::strerror(errno));
    }
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
    assert(::pipe(ready_pipe) == 0);
    assert(::pipe(release_pipe) == 0);

    const auto child = ::fork();
    assert(child >= 0);
    if (child == 0) {
        ::close(ready_pipe[0]);
        ::close(release_pipe[1]);

        const int lock_fd = ::open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (lock_fd < 0 || ::flock(lock_fd, LOCK_EX) < 0)
            _exit(10);

        const char ready = 'R';
        if (::write(ready_pipe[1], &ready, 1) != 1)
            _exit(11);

        char release{};
        while (::read(release_pipe[0], &release, 1) < 0) {
            if (errno != EINTR)
                _exit(12);
        }

        ::close(lock_fd);
        _exit(0);
    }

    ::close(ready_pipe[1]);
    ::close(release_pipe[0]);
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

    for (const auto &entry : std::filesystem::directory_iterator(boot)) {
        assert(entry.path() == target);
    }

    write_byte(release_pipe[1], 'X');
    ::close(ready_pipe[0]);
    ::close(release_pipe[1]);

    int child_status{};
    assert(::waitpid(child, &child_status, 0) == child);
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
