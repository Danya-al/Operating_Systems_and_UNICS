#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <utility>

#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace {

constexpr std::uint16_t kPort = 5000;
constexpr int kBacklog = 16;

volatile std::sig_atomic_t was_sighup = 0;
volatile std::sig_atomic_t was_sigterm = 0;

extern "C" void sighup_handler(int) { was_sighup = 1; }
extern "C" void sigterm_handler(int) { was_sigterm = 1; }

class Fd {
public:
    Fd() = default;
    explicit Fd(int fd) : fd_(fd) {}
    ~Fd() { reset(); }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& o) noexcept : fd_(std::exchange(o.fd_, -1)) {}
    Fd& operator=(Fd&& o) noexcept {
        if (this != &o) { reset(); fd_ = std::exchange(o.fd_, -1); }
        return *this;
    }
    int get() const { return fd_; }
    bool valid() const { return fd_ != -1; }
    void reset() { if (fd_ != -1) { ::close(fd_); fd_ = -1; } }
private:
    int fd_ = -1;
};

Fd create_server_socket(std::uint16_t port) {
    Fd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd.valid()) { std::perror("socket"); return Fd(); }

    int yes = 1;
    if (::setsockopt(fd.get(), SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
        std::perror("setsockopt"); return Fd();
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);

    if (::bind(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        std::perror("bind"); return Fd();
    }
    if (::listen(fd.get(), kBacklog) == -1) {
        std::perror("listen"); return Fd();
    }
    return fd;
}

bool setup_signals(sigset_t& orig_mask) {
    sigset_t blocked;
    sigemptyset(&blocked);
    sigaddset(&blocked, SIGHUP);
    sigaddset(&blocked, SIGTERM);
    sigaddset(&blocked, SIGINT);
    if (::sigprocmask(SIG_BLOCK, &blocked, &orig_mask) == -1) {
        std::perror("sigprocmask"); return false;
    }

    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sa.sa_handler = sighup_handler;
    if (::sigaction(SIGHUP, &sa, nullptr) == -1) { std::perror("sigaction SIGHUP"); return false; }
    sa.sa_handler = sigterm_handler;
    if (::sigaction(SIGTERM, &sa, nullptr) == -1 || ::sigaction(SIGINT, &sa, nullptr) == -1) {
        std::perror("sigaction SIGTERM/SIGINT"); return false;
    }
    return true;
}

void handle_new_connection(const Fd& server, Fd& client) {
    Fd fd(::accept(server.get(), nullptr, nullptr));
    if (!fd.valid()) { std::perror("accept"); return; }
    if (!client.valid()) {
        std::cout << "Новое соединение (fd " << fd.get() << "): принято" << std::endl;
        client = std::move(fd);
    } else {
        std::cout << "Новое соединение (fd " << fd.get() << "): закрыто, клиент уже подключён" << std::endl;
    }
}

void handle_client_data(Fd& client) {
    char buf[4096];
    ssize_t n = ::recv(client.get(), buf, sizeof(buf), 0);
    if (n > 0) {
        std::cout << "Получено байт: " << n << std::endl;
        return;
    }
    if (n == 0) std::cout << "Клиент отключился" << std::endl;
    else std::perror("recv");
    client.reset();
}

}

int main() {
    Fd server = create_server_socket(kPort);
    if (!server.valid()) return EXIT_FAILURE;

    sigset_t orig_mask;
    if (!setup_signals(orig_mask)) return EXIT_FAILURE;

    std::cout << "Сервер запущен: PID " << ::getpid() << ", порт " << kPort << std::endl;

    Fd client;
    while (!was_sigterm) {
        fd_set rfds;              
        FD_ZERO(&rfds);
        FD_SET(server.get(), &rfds);
        int max_fd = server.get();
        if (client.valid()) {
            FD_SET(client.get(), &rfds);
            if (client.get() > max_fd) max_fd = client.get();
        }

        int ready = ::pselect(max_fd + 1, &rfds, nullptr, nullptr, nullptr, &orig_mask);
        if (ready == -1) {
            if (errno != EINTR) { std::perror("pselect"); break; }
            if (was_sighup) { 
                was_sighup = 0;
                std::cout << "Получен сигнал SIGHUP" << std::endl;
            }
            continue;                   
        }

        if (FD_ISSET(server.get(), &rfds)) handle_new_connection(server, client);
        if (client.valid() && FD_ISSET(client.get(), &rfds)) handle_client_data(client);
    }

    std::cout << "Завершение работы" << std::endl;
    ::sigprocmask(SIG_SETMASK, &orig_mask, nullptr);
    return EXIT_SUCCESS;
}