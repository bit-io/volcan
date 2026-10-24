#include "prelude.h"
extern "C" {
#include I3IPC_INC
}
#include <iostream>
#include <sys/stat.h>

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

static std::string frame(uint32_t type, const std::string &p)
{
    std::string s("i3-ipc", 6);
    uint32_t len = (uint32_t)p.size();
    s.append((const char *)&len, 4);
    s.append((const char *)&type, 4);
    return s + p;
}

static void send_all(int fd, const std::string &s) { (void)!::write(fd, s.data(), s.size()); }

// Czyta jedna ramke (z kreceniem serwera, bo to ten sam proces). Zwraca false przy zamknieciu/limicie czasu.
static bool read_frame(int fd, uint32_t &type, std::string &payload, int ms = 1000)
{
    std::string buf;
    const auto t0 = std::chrono::steady_clock::now();
    while (true)
    {
        vcn_i3_poll();
        if (buf.size() >= 14)
        {
            uint32_t len = 0;
            std::memcpy(&len, buf.data() + 6, 4);
            if (buf.size() >= 14u + len)
            {
                std::memcpy(&type, buf.data() + 10, 4);
                payload = buf.substr(14, len);
                return true;
            }
        }
        pollfd p { fd, POLLIN, 0 };
        if (::poll(&p, 1, 5) > 0)
        {
            char tmp[4096];
            ssize_t n = ::read(fd, tmp, sizeof(tmp));
            if (n <= 0) return false;
            buf.append(tmp, (size_t)n);
        }
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > ms) return false;
    }
}

static bool nothing_arrives(int fd, int ms = 120)
{
    uint32_t t; std::string p;
    return !read_frame(fd, t, p, ms);
}

int main()
{
    char path[] = "/tmp/volcan-i3-test-XXXXXX";
    int tfd = mkstemp(path);
    close(tfd);
    unlink(path);
    CHECK(vcn_i3_listen_c(path) == 1);
    CHECK(vcn_i3_listen_c(path) == 0);                  // drugi raz nie wolno
    struct stat st {};
    CHECK(stat(path, &st) == 0 && (st.st_mode & 0777) == 0600);

    int c1 = connect_to(path);
    CHECK(c1 >= 0);
    uint32_t type = 99;
    std::string pl;

    // 1. GET_VERSION odpowiada serwer
    if (getenv("I3DBG")) std::cerr << "krok 1\n";
    send_all(c1, frame(7, ""));
    CHECK(read_frame(c1, type, pl) && type == 7 && pl.find("\"human_readable\"") != std::string::npos);

    // 2. fragmentacja: ramka podana w kawalkach
    if (getenv("I3DBG")) std::cerr << "krok 2\n";
    {
        std::string f = frame(8, "");
        send_all(c1, f.substr(0, 3));
        usleep(20000); vcn_i3_poll();
        send_all(c1, f.substr(3, 9));
        usleep(20000); vcn_i3_poll();
        send_all(c1, f.substr(12));
        CHECK(read_frame(c1, type, pl) && type == 8 && pl == "[\"default\"]");
    }

    // 3. subskrypcje: workspace i window; output nie
    if (getenv("I3DBG")) std::cerr << "krok 3\n";
    send_all(c1, frame(2, "[\"workspace\",\"window\"]"));
    CHECK(read_frame(c1, type, pl) && type == 2 && pl == "{\"success\":true}");
    CHECK(vcn_i3_publish_c(0, "{\"change\":\"focus\"}") == 1);
    CHECK(read_frame(c1, type, pl) && type == 0x80000000u && pl == "{\"change\":\"focus\"}");
    CHECK(vcn_i3_publish_c(1, "{}") == 0);
    CHECK(nothing_arrives(c1));
    CHECK(vcn_i3_publish_c(3, "{\"change\":\"new\"}") == 1);
    CHECK(read_frame(c1, type, pl) && type == 0x80000003u);
    CHECK(vcn_i3_publish_c(9, "{}") == 0);              // zly numer zdarzenia

    // 4. zadanie dla skryptu: RUN_COMMAND, odpowiedz ma ten sam typ
    if (getenv("I3DBG")) std::cerr << "krok 4\n";
    send_all(c1, frame(0, "workspace 2"));
    for (int i = 0; i < 100 && vcn_i3_poll() != 1; i++) usleep(5000);
    CHECK(vcn_i3_type() == 0 && std::string(vcn_i3_payload_c()) == "workspace 2");
    CHECK(vcn_i3_poll() == 1);                          // dopoki nie ma odpowiedzi, wciaz to samo zadanie
    CHECK(vcn_i3_reply_c("[{\"success\":true}]") == 1);
    CHECK(read_frame(c1, type, pl) && type == 0 && pl == "[{\"success\":true}]");
    CHECK(vcn_i3_reply_c("{}") == 0);                   // nie ma biezacego zadania

    // 5. dwa zadania z dwoch klientow, kolejnosc zachowana
    if (getenv("I3DBG")) std::cerr << "krok 5\n";
    int c2 = connect_to(path);
    send_all(c1, frame(1, ""));
    send_all(c2, frame(3, ""));
    for (int i = 0; i < 100 && vcn_i3_poll() != 1; i++) usleep(5000);
    CHECK(vcn_i3_type() == 1);
    CHECK(vcn_i3_reply_c("[\"ws\"]") == 1);
    for (int i = 0; i < 100 && vcn_i3_poll() != 1; i++) usleep(5000);
    CHECK(vcn_i3_type() == 3);
    CHECK(vcn_i3_reply_c("[\"out\"]") == 1);
    CHECK(read_frame(c1, type, pl) && type == 1 && pl == "[\"ws\"]");
    CHECK(read_frame(c2, type, pl) && type == 3 && pl == "[\"out\"]");

    // 6. klient znika przed odpowiedzia: bez awarii, zadanie przepada
    if (getenv("I3DBG")) std::cerr << "krok 6\n";
    send_all(c2, frame(4, ""));
    for (int i = 0; i < 100 && vcn_i3_poll() != 1; i++) usleep(5000);
    CHECK(vcn_i3_type() == 4);
    close(c2);
    usleep(20000);
    vcn_i3_poll();
    (void)vcn_i3_reply_c("{}");          // klient zniknal: odpowiedz przepada bez awarii
    CHECK(vcn_i3_poll() == 0);

    // 7. SEND_TICK trafia do subskrybentow tick
    if (getenv("I3DBG")) std::cerr << "krok 7\n";
    int c3 = connect_to(path);
    send_all(c3, frame(2, "[\"tick\"]"));
    CHECK(read_frame(c3, type, pl) && type == 2);
    send_all(c1, frame(10, "ping"));
    CHECK(read_frame(c1, type, pl) && type == 10);
    CHECK(read_frame(c3, type, pl) && type == 0x80000007u && pl.find("ping") != std::string::npos);

    // 8. zla magia i zbyt duza ramka zrywaja polaczenie
    if (getenv("I3DBG")) std::cerr << "krok 8\n";
    {
        int bad = connect_to(path);
        send_all(bad, std::string("xx-ipc\0\0\0\0\0\0\0\0", 14));
        CHECK(!read_frame(bad, type, pl, 500));
        close(bad);
        int big = connect_to(path);
        std::string f("i3-ipc", 6);
        uint32_t len = 0x7fffffff, ty = 0;
        f.append((const char *)&len, 4); f.append((const char *)&ty, 4);
        send_all(big, f);
        CHECK(!read_frame(big, type, pl, 500));
        close(big);
    }

    // 9. nieznany typ
    if (getenv("I3DBG")) std::cerr << "krok 9\n";
    send_all(c1, frame(55, ""));
    CHECK(read_frame(c1, type, pl) && type == 55 && pl.find("false") != std::string::npos);

    // 10. wiele zadan naraz: limit kolejki nie wywraca serwera
    if (getenv("I3DBG")) std::cerr << "krok 10\n";
    { std::string many; for (int i = 0; i < 400; i++) many += frame(1, ""); send_all(c1, many); }
    for (int i = 0; i < 50; i++) { vcn_i3_poll(); usleep(2000); }
    int answered = 0;
    while (vcn_i3_poll() == 1 && answered < 1000) { vcn_i3_reply_c("[]"); answered++; }
    CHECK(answered >= 200 && answered <= 400);

    CHECK(vcn_i3_clients() >= 2);
    vcn_i3_close();
    CHECK(access(path, F_OK) != 0);
    close(c1); close(c3);
    if (g_fail) { std::cerr << "i3ipc: " << g_fail << " BLEDOW\n"; return 1; }
    std::cout << "i3ipc: OK\n";
    return 0;
}
