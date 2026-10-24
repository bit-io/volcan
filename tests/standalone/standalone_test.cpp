#include "prelude.h"
extern "C" {
#include STANDALONE_INC
}
#include <cassert>
#include <iostream>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "BLAD " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; g_fail++; } } while (0)

static int connect_to(const char *path)
{
    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    sockaddr_un a {};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, path, sizeof(a.sun_path) - 1);
    if (::connect(fd, (sockaddr *)&a, sizeof(a)) != 0) { ::close(fd); return -1; }
    return fd;
}

static void send_line(int fd, const std::string &s) { std::string o = s + "\n"; (void)!::write(fd, o.data(), o.size()); }

// Czeka (do ~1 s), az serwer zobaczy polecenie; zwraca 1 gdy jest.
static int poll_cmd()
{
    for (int i = 0; i < 100; i++) { if (vcn_ipc_poll() == 1) return 1; usleep(10000); }
    return 0;
}

static std::string read_line(int fd, int timeout_ms = 1000)
{
    std::string line;
    pollfd p { fd, POLLIN, 0 };
    while (::poll(&p, 1, timeout_ms) > 0)
    {
        char c;
        ssize_t n = ::read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\n') return line;
        line.push_back(c);
    }
    return line;
}

static std::string cur_line() { vcn_ipc_line(); return g_buf1; }

static void test_config()
{
    char path[] = "/tmp/volcan_cfg_XXXXXX";
    int fd = mkstemp(path);
    const char *txt = "# komentarz\nterminal = foot\n[tiling]\ngap = 8\nfloat_apps = a, b\n; inny\n[input]\nrepeat_rate = 30\n";
    (void)!::write(fd, txt, std::strlen(txt));
    ::close(fd);
    CHECK(vcn_config_load_c(path) == 4);
    CHECK(vcn_config_has_c("terminal") == 1);
    CHECK(vcn_config_has_c("tiling.gap") == 1);
    CHECK(vcn_config_int_c("tiling.gap", -1) == 8);
    CHECK(vcn_config_int_c("input.repeat_rate", -1) == 30);
    CHECK(vcn_config_int_c("brak", 77) == 77);
    vcn_config_set_c("x.y", "z");
    CHECK(vcn_config_has_c("x.y") == 1);
    vcn_config_clear();
    CHECK(vcn_config_count() == 0);
    CHECK(vcn_config_load_c("/nie/ma/takiego/pliku") == -1);
    ::unlink(path);
}

static void test_ipc_command_and_subscribe()
{
    std::string path = "/tmp/volcan_ipc_test_" + std::to_string(::getpid()) + ".sock";
    CHECK(vcn_ipc_listen_c(path.c_str()) == 1);
    CHECK(vcn_ipc_subscribers() == 0);
    CHECK(vcn_ipc_publish_c("nikt nie slucha") == 0);

    // zwykle polecenie: jedna linia -> jedna odpowiedz
    int c1 = connect_to(path.c_str());
    CHECK(c1 >= 0);
    send_line(c1, "status");
    CHECK(poll_cmd() == 1);
    CHECK(cur_line() == "status");
    CHECK(vcn_ipc_reply_c("ok status") == 1);
    CHECK(read_line(c1) == "ok status");
    ::close(c1);

    // subskrypcja
    int s1 = connect_to(path.c_str());
    send_line(s1, "subscribe");
    CHECK(poll_cmd() == 1);
    CHECK(cur_line() == "subscribe");
    CHECK(vcn_ipc_subscribe_c() == 1);
    CHECK(read_line(s1) == "ok");
    CHECK(vcn_ipc_subscribers() == 1);

    // drugi subskrybent
    int s2 = connect_to(path.c_str());
    send_line(s2, "subscribe");
    CHECK(poll_cmd() == 1);
    CHECK(vcn_ipc_subscribe_c() == 1);
    CHECK(read_line(s2) == "ok");
    CHECK(vcn_ipc_subscribers() == 2);

    CHECK(vcn_ipc_publish_c("window_open 42") == 2);
    CHECK(read_line(s1) == "window_open 42");
    CHECK(read_line(s2) == "window_open 42");

    // zwykle polecenie nadal dziala obok subskrybentow
    int c2 = connect_to(path.c_str());
    send_line(c2, "windows");
    CHECK(poll_cmd() == 1);
    CHECK(vcn_ipc_reply_c("pusto") == 1);
    CHECK(read_line(c2) == "pusto");
    ::close(c2);

    // martwy subskrybent jest odlaczany przy najblizszej publikacji, zywy dostaje dalej
    ::close(s1);
    usleep(20000);
    int64_t d = vcn_ipc_publish_c("focus 7");
    CHECK(d <= 2);                       // 1 (zamkniety gniazdem peer -> EPIPE) albo 2 (bufor jadra przyjal)
    d = vcn_ipc_publish_c("focus 8");
    CHECK(d == 1);
    CHECK(vcn_ipc_subscribers() == 1);
    CHECK(read_line(s2) == "focus 7");
    CHECK(read_line(s2) == "focus 8");

    // subscribe bez klienta nie moze sie udac
    CHECK(vcn_ipc_subscribe_c() == 0);

    // limit 16 subskrybentow
    std::vector<int> many;
    int accepted = 1;                    // s2 juz jest
    for (int i = 0; i < 20; i++)
    {
        int c = connect_to(path.c_str());
        send_line(c, "subscribe");
        if (!poll_cmd()) { ::close(c); continue; }
        if (vcn_ipc_subscribe_c() == 1) { accepted++; many.push_back(c); }
        else { vcn_ipc_reply_c("error: limit"); ::close(c); }
    }
    CHECK(accepted == 16);
    CHECK(vcn_ipc_subscribers() == 16);

    // zamkniecie zwalnia subskrybentow
    vcn_ipc_close();
    CHECK(vcn_ipc_subscribers() == 0);
    CHECK(read_line(s2, 200).empty());   // EOF
    ::close(s2);
    for (int c : many) ::close(c);
}


static void test_watch()
{
    char dirt[] = "/tmp/volcan_watch_XXXXXX";
    CHECK(mkdtemp(dirt) != nullptr);
    std::string dir = dirt, file = dir + "/volcan.conf", other = dir + "/inny.txt";
    CHECK(vcn_watch_set_c(file.c_str()) == 1);
    CHECK(vcn_watch_poll() == 0);

    auto write_file = [](const std::string &p, const char *txt) { FILE *f = fopen(p.c_str(), "w"); fputs(txt, f); fclose(f); };
    auto wait_poll = [](int ms) { for (int i = 0; i < ms / 10; i++) { if (vcn_watch_poll() == 1) return 1; usleep(10000); } return 0; };

    write_file(other, "x");                          // inny plik: bez zdarzenia
    usleep(300000);
    CHECK(vcn_watch_poll() == 0);

    write_file(file, "a = 1\n");                     // zwykly zapis
    CHECK(vcn_watch_poll() == 0);                    // za wczesnie: okres ciszy 150 ms
    CHECK(wait_poll(1000) == 1);
    CHECK(vcn_watch_poll() == 0);                    // zdarzenie zuzyte

    write_file(dir + "/tmp.new", "a = 2\n");          // atomowy zapis przez rename
    CHECK(rename((dir + "/tmp.new").c_str(), file.c_str()) == 0);
    CHECK(wait_poll(1000) == 1);

    for (int i = 0; i < 5; i++) { write_file(file, "b\n"); usleep(20000); }   // seria zapisow = jedno zdarzenie
    CHECK(wait_poll(1000) == 1);
    usleep(300000);
    CHECK(vcn_watch_poll() == 0);

    CHECK(vcn_watch_set_c("") == 0);
    CHECK(vcn_watch_set_c("/nie/ma/katalogu/x.conf") == 0);
    vcn_watch_close();
    CHECK(vcn_watch_poll() == 0);
    unlink(file.c_str()); unlink(other.c_str()); rmdir(dirt);
}

static void test_ipc_token()
{
    std::string path = "/tmp/volcan_ipc_tok_" + std::to_string(::getpid()) + ".sock";
    vcn_ipc_set_token_c("sekret");
    CHECK(vcn_ipc_listen_c(path.c_str()) == 1);
    int c = connect_to(path.c_str());
    send_line(c, "status");
    for (int i = 0; i < 20; i++) { vcn_ipc_poll(); usleep(5000); }
    CHECK(read_line(c) == "error: brak autoryzacji");
    ::close(c);
    CHECK(vcn_ipc_stat(2) == 1);
    int c2 = connect_to(path.c_str());
    send_line(c2, "sekret status");
    CHECK(poll_cmd() == 1);
    CHECK(cur_line() == "status");
    vcn_ipc_reply_c("ok");
    CHECK(read_line(c2) == "ok");
    ::close(c2);
    vcn_ipc_close();
    vcn_ipc_set_token_c("");
}

static void test_process()
{
    int64_t pid = vcn_process_start_c("exit 3");
    CHECK(pid > 0);
    int64_t p = 0, st = -1;
    int got = 0;
    for (int i = 0; i < 200 && !got; i++) { got = (int)vcn_process_reap(&p, &st); usleep(5000); }
    CHECK(got == 1);
    CHECK(p == pid);
    CHECK(st == 3);
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    test_config();
    test_ipc_command_and_subscribe();
    test_ipc_token();
    test_process();
    test_watch();
    {
        const int64_t a = vcn_now_ms();
        usleep(20000);
        CHECK(vcn_now_ms() >= a + 15);
        CHECK(std::string(vcn_strftime_c("%Y", 0)) == "1970" || std::string(vcn_strftime_c("%Y", 0)) == "1969");
        CHECK(std::string(vcn_strftime_c("", 0)).empty());
        CHECK(std::string(vcn_strftime_c("%H:%M", -1)).size() == 5);
        g_buf0 = "%Y-%m";
        CHECK(vcn_strftime() == 7);
    }
    if (g_fail) { std::cerr << g_fail << " testow nie przeszlo\n"; return 1; }
    std::cout << "standalone: OK\n";
    return 0;
}
