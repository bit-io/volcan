#include "prelude.h"
extern "C" {
#include STANDALONE_INC
}
#include <iostream>
#include <random>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "BLAD " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; g_fail++; } } while (0)

int main()
{
    std::mt19937 rng(12345);
    const char *alphabet = "abcXYZ019 =#;[]\"'\\\n\t.-_/*$!>{}%\xc4\x85\xff";
    const size_t alen = std::strlen(alphabet);
    char path[] = "/tmp/volcan-fuzz-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    for (int round = 0; round < 3000; round++)
    {
        std::string s;
        const int n = (int)(rng() % 400);
        for (int i = 0; i < n; i++)
        {
            if (rng() % 17 == 0) s.push_back((char)(rng() & 0xff));
            else s.push_back(alphabet[rng() % alen]);
        }
        if (round % 5 == 0) s = "bind.1 = Super+Return : spawn foot\n" + s + "\nrule.1 = app_id=x => float\n";
        FILE *f = std::fopen(path, "wb");
        std::fwrite(s.data(), 1, s.size(), f);
        std::fclose(f);
        vcn_config_clear();
        const int64_t keys = vcn_config_load_c(path);
        CHECK(keys >= 0);
        CHECK(vcn_config_count() >= 0);
        for (int64_t i = 0; i < vcn_config_count(); i++)
        {
            const char *k = vcn_config_key_at_c(i);
            CHECK(k != nullptr);
            if (k) (void)vcn_config_get_c(k);
        }
        (void)vcn_config_int_c("gap", 7);
        (void)vcn_config_has_c("bind.1");
    }
    // plik, ktorego nie ma / katalog / pusta sciezka
    vcn_config_clear();
    CHECK(vcn_config_load_c("/nie/ma/takiego/pliku") < 0);
    CHECK(vcn_config_load_c("/tmp") <= 0);
    CHECK(vcn_config_load_c("") < 0);
    // zawsze poprawny zapis: klucz = wartosc
    FILE *f = std::fopen(path, "wb");
    std::fputs("gap = 12\n# komentarz\nanim.move_ms = 120\n", f);
    std::fclose(f);
    vcn_config_clear();
    CHECK(vcn_config_load_c(path) == 2);
    CHECK(vcn_config_int_c("gap", 0) == 12);
    CHECK(vcn_config_int_c("anim.move_ms", 0) == 120);
    unlink(path);
    if (g_fail) { std::cerr << "fuzz: " << g_fail << " BLEDOW\n"; return 1; }
    std::cout << "fuzz konfiguracji: OK\n";
    return 0;
}
