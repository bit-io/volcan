#include "prelude.h"
extern "C" {
#include LIBINPUT_INC
}
#include <iostream>

static int g_fail = 0;
#define CHECK(cond) do { if (!(cond)) { std::cerr << "BLAD " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; g_fail++; } } while (0)

int main()
{
    CHECK(li_glob("*", "SYNA Touchpad"));
    CHECK(li_glob("SYNA*", "SYNA Touchpad"));
    CHECK(li_glob("*Touch?ad", "SYNA Touchpad"));
    CHECK(!li_glob("SYNA", "SYNA Touchpad"));
    CHECK(!li_glob("syna*", "SYNA Touchpad"));

    vcn_li_clear();
    CHECK(vcn_li_add_rule_c("touchpad * tap=on natural_scroll=on dwt=off accel_speed=0.35 accel_profile=flat") == 1);
    CHECK(vcn_li_add_rule_c("pointer Logitech* left_handed=on scroll_method=button click_method=none") == 1);
    CHECK(vcn_li_add_rule_c("any * middle_emulation=1 tap_drag=no tap_lock=yes") == 1);
    CHECK(vcn_li_rule_count() == 3);
    CHECK(g_li_rules[0].kind == 1 && g_li_rules[0].tap == 1 && g_li_rules[0].natural == 1 && g_li_rules[0].dwt == 0);
    CHECK(g_li_rules[0].has_speed && g_li_rules[0].speed > 0.349 && g_li_rules[0].speed < 0.351);
    CHECK(g_li_rules[0].accel_profile == 0);
    CHECK(g_li_rules[1].kind == 2 && g_li_rules[1].pattern == "Logitech*" && g_li_rules[1].left_handed == 1);
    CHECK(g_li_rules[1].scroll_method == 3 && g_li_rules[1].click_method == 0);
    CHECK(g_li_rules[2].kind == 0 && g_li_rules[2].middle == 1 && g_li_rules[2].tap_drag == 0 && g_li_rules[2].tap_lock == 1);

    // bledy: nic nie trafia na liste, opis w vcn_li_error
    CHECK(vcn_li_add_rule_c("") == 0);
    CHECK(vcn_li_add_rule_c("touchpad") == 0);
    CHECK(vcn_li_add_rule_c("mouse * tap=on") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * tap=maybe") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * accel_speed=2") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * accel_speed=abc") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * scroll_method=spiral") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * click_method=foot") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * nieznany=1") == 0);
    CHECK(vcn_li_add_rule_c("touchpad * tap") == 0);
    CHECK(vcn_li_error() > 0 && !g_buf1.empty());
    CHECK(vcn_li_rule_count() == 3);
    vcn_li_clear();
    CHECK(vcn_li_rule_count() == 0);

    // sygnatury li_apply_*: wywolanie na nullptr nie jest dozwolone, wiec tylko bierzemy adresy
    int (*f1)(libinput_device *, const LiRule &) = &li_apply_rule;
    int (*f2)(libinput_device *) = &li_apply_all;
    CHECK(f1 != nullptr && f2 != nullptr);

    if (g_fail) { std::cerr << g_fail << " testow nie przeszlo\n"; return 1; }
    std::cout << "libinput: OK\n";
    return 0;
}
