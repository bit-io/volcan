# volcan

Biblioteka do pisania kompozytorów Wayland w **HackerScript**, dla menedżera pakietów **bit** (bit.io).
To bindingi do [Louvre](https://github.com/CuarzoSoftware/Louvre) 3.x (C++, LGPL-2.1).
Cały kod biblioteki — także warstwa C++ — siedzi w plikach `.hcs`; warstwa C++ jest zwykłym blokiem `native {C++}`.

```
lib/
  mod.hcs         punkt wejścia (get <bit:volcan>), dołącza resztę
  shim.hcs        JEDEN blok C++: podklasy Louvre + płaskie API C (vcn_*)
  ffi.hcs ffi_ext.hcs   cienkie opakowania ffi_* na funkcje vcn_*
  screencopy.hcs  udostępnianie ekranu (ext-image-copy-capture-v1)
  easy.hcs        gotowe kompozytory: volcan_main, easy_tiling/floating/kiosk
  link.hcs        get <extern:cz-louvre> use <dynamic>
  text.hcs strings.hcs  napisy UTF-8 przez granicę C, narzędzia na napisach, glob
  events.hcs      kolejka zdarzeń: EV_*, struct Event, event_next()
  compositor.hcs  start, pętla, środowisko, uruchamianie klientów
  surface.hcs output.hcs input.hcs keys.hcs cursor.hcs   powierzchnie, monitory, wskaźnik i klawiatura
  layout.hcs      układy: master, kolumny, siatka, monocle, zakładki, stos, dwindle, centered, scroll
  wm.hcs          zarządca okien: przestrzenie, tryby, scratchpad, wiele wyjść, polecenia tekstowe
  binds.hcs rules.hcs grab.hcs gestures.hcs   skróty z trybami, reguły okien, przeciąganie, gesty
  config.hcs input_cfg.hcs outputs_cfg.hcs startup.hcs   konfiguracja, wejście, wyjścia
  ipc.hcs ipc_events.hcs   IPC (polecenia + subskrypcje zdarzeń)
  state.hcs style.hcs popup.hcs layer.hcs session.hcs xwayland.hcs process.hcs files.hcs
  checked.hcs caps.hcs     typowane uchwyty i statusy, zdolności shima
  app.hcs         fasada w stylu MirAL: volcan_run / volcan_start / volcan_step
  wmx.hcs bar.hcs MRU, zamiana okien, nazwane przestrzenie; pasek stanu
bin/volcanctl     klient IPC (polecenia, subscribe)
examples/minimal  szkic; examples/tiling-wm  kompletny kompozytor z konfiguracją
tests/ scripts/   testy i kontrole (patrz niżej)
docs/             ARCHITECTURE, CONFIG, PROTOCOLS, NATIVE_CPP
```

## Użycie

```
[dependencies]
-> volcan => *
```

```
get <bit:volcan>

fun main() [
    if not compositor_boot("volcan") [ end ]
    compositor_spawn("foot")
    while compositor_running() [
        compositor_dispatch(10)
        let ev = event_next()
        while ev.kind != EV_NONE [
            if ev.kind == EV_SURFACE_MAPPED [
                toplevel_focus(ev.a)
            ]
            ev = event_next()
        ]
    ]
    compositor_destroy()
]
```

Cały przykład: `examples/minimal/cmd/main.hcs`.

## Model: uchwyty i zdarzenia

HackerScript nie ma callbacków do C++, a Louvre jest zbudowany na metodach wirtualnych
(wzorzec Factory Method). Dlatego:

* **Uchwyty.** Powierzchnia i wyjście to `Int` (adres obiektu Louvre), `0` = brak.
  Shim sprawdza uchwyt względem żywych obiektów przy każdym wywołaniu, więc uchwyt
  zniszczonej powierzchni daje wartość domyślną zamiast crasha.
* **Zdarzenia.** Podklasy `LSurface`/`LToplevelRole`/`LSeat`/`LPointer`/`LKeyboard`
  (tworzone w `LCompositor::createObjectRequest`) dopisują zdarzenia do kolejki;
  skrypt odbiera je przez `event_next()`. Domyślnie wołane jest też zachowanie bazowe Louvre.
* **Tryb ręczny.** `compositor_manual_policy(true)` wyłącza domyślną reakcję Louvre na
  żądania okien (move, resize, maximize, fullscreen, minimize, close, activate) — przychodzą
  wtedy tylko jako zdarzenia, a skrypt sam decyduje.
* **Napisy.** Przez granicę C idą bajt po bajcie (UTF-8); `Str` składa `text.hcs`.

### Zdarzenia (`events.hcs`)

| stała | pola |
|---|---|
| `EV_SURFACE_CREATED/DESTROYED/MAPPED/UNMAPPED` | `a` = powierzchnia |
| `EV_SURFACE_ROLE` | `a` = powierzchnia, `b` = rola (`ROLE_*`) |
| `EV_TOPLEVEL_TITLE`, `EV_TOPLEVEL_APP_ID` | `a` = powierzchnia |
| `EV_MOVE_REQUEST`, `EV_*MAXIMIZE_REQUEST`, `EV_*MINIMIZE_REQUEST`, `EV_CLOSE_REQUEST`, `EV_ACTIVATE_REQUEST` | `a` = powierzchnia |
| `EV_RESIZE_REQUEST` | `a` = powierzchnia, `b` = maska `EDGE_*` |
| `EV_*FULLSCREEN_REQUEST` | `a` = powierzchnia, `b` = wyjście lub 0 |
| `EV_OUTPUT_PLUGGED/UNPLUGGED` | `a` = wyjście |
| `EV_POINTER_MOVE` | `x`, `y` = delta |
| `EV_POINTER_BUTTON` | `a` = przycisk, `b` = 1/0 |
| `EV_KEY` | `a` = kod klawisza, `b` = 1/0 |
| `EV_ACTIVE_TOPLEVEL` | `a` = nowa aktywna powierzchnia lub 0 |

## API (skrót)

* **compositor**: `compositor_boot(display)`, `compositor_create/start/dispatch(ms)/running/stop/destroy`,
  `compositor_setenv`, `compositor_defaults`, `compositor_spawn(cmd)`, `compositor_manual_policy(bool)`, `compositor_fd()`.
* **surface**: `surface_list`, `toplevel_list`, `surface_info`, `surface_move/raise/parent/at_point`,
  `toplevel_title/app_id/activated/maximized/fullscreen/minimized`, `toplevel_set_*`, `toplevel_resize`,
  `toplevel_close`, `toplevel_focus`, `toplevel_window_x/y/width/height`.
* **output**: `output_list`, `output_info`, `output_name/x/y/width/height/scale`, `output_set_scale/pos`,
  `output_vsync`, `output_set_vsync`, `output_repaint(_all)`.
* **input**: `cursor_x/y`, `cursor_move_to`, `cursor_set_visible`, `surface_under_cursor`,
  `pointer_focus/set_focus`, `keyboard_focus/set_focus`, `key_pressed`, `active_toplevel`, `seat_name`,
  stałe `BTN_*` i `KEY_*`.

## Budowanie

Wymagane: **Louvre 3.x** zainstalowany z nagłówkami (`pkg-config --cflags cz-louvre`), kompilator C++20
(g++/clang++), `hackerc` 0.5, `cargo`, `bit`.

```sh
export CXXFLAGS="-std=c++20 $(pkg-config --cflags cz-louvre)"
bit build            # w projekcie, który zależy od volcan
```

Jeśli linker nie znajdzie symboli Louvre:
`export RUSTFLAGS="-C link-arg=-lcz-louvre"` (albo `-L` do katalogu z `libcz-louvre.so`).

## Testy

```sh
scripts/check_shim.py            # shim.hcs: nawiasy, zakazane ciągi, duplikaty vcn_*
scripts/check_hcs.py             # .hcs: nawiasy, duplikaty funkcji i stałych
scripts/layout_model.py --check  # model układów dwindle/centered/scroll (niezmienniki)
tests/standalone/run.sh          # C++: konfiguracja, IPC, procesy, libinput, fuzz konfiguracji, screencopy (libwayland)
tests/volcanctl_test.sh          # volcanctl <-> prawdziwy kod IPC
(cd tests && bit build)          # testy HackerScript, wymagają hackerc
```

## Wersja 0.1.0: co jest w środku

* **Kompozytor w jednej linii** (`easy.hcs`): `volcan_main("wm")` to gotowy kompozytor kafelkowy; `easy_tiling`,
  `easy_floating`, `easy_kiosk` zwracają opis `App`, który doprawiasz `app_*` (`docs/EASY.md`, przykłady `examples/default`,
  `floating`, `kiosk`). Pod spodem fasada w stylu MirAL (`app.hcs`, `docs/MIRAL.md`): `volcan_run`, albo własna pętla
  (`volcan_start` / `volcan_step` / `volcan_finish`).
* **Udostępnianie ekranu** (`screencopy.hcs`): własne `ext-image-copy-capture-v1` i `wlr-screencopy` v3 (grim, wf-recorder,
  xdg-desktop-portal-wlr) w shimie, sprawdzone na prawdziwym libwayland (`tests/standalone/screencopy_run.sh`). `docs/SCREENCOPY.md`.
* **IPC zgodne ze swaymsg/waybarem** (`i3.hcs`, `docs/I3.md`), **bezczynność** (`idle.N`: blokada, wygaszanie, wznowienie),
  **widżety paska** (zegar, bateria, plik), reguła **`ontop`**, przykład `examples/workstation`, przewodnik `docs/GUIDE.md`.
* **Animacje w zarządcy okien**: `anim.move_ms` (płynne przesuwanie okien przy zmianie układu), `anim.open_ms` (wejście nowego
  okna), `anim.ease`; niskopoziomowe `anim_move/fade/scale` w `style.hcs` działały już wcześniej.
* **H# przez oficjalne API hlib**: `examples/hsharp-wm` (`use "hlib -> volcan"`, bez `extern` w programie); bibliotekę-fasadę
  w H# buduje `h# lib build`, a `scripts/build_cabi.sh` robi `libvolcan_c.so` z shima.
* Układy (master, columns, grid, monocle, tabbed, stack, dwindle, centered, scroll), przestrzenie, scratchpad, wiele wyjść,
  skróty z trybami i sekwencjami, reguły okien (w tym `sticky`), gesty, IPC z subskrypcjami + `volcanctl`, konfiguracja
  wyjść z profilami, libinput z pliku, przeładowanie w locie, pasek stanu, XWayland (xwayland-satellite), hooki, autostart.

Czego nie ma (Louvre 3.0 nie daje API albo nie zrobione): IME, tablet, VRR, dmabuf w screencopy, przechwytywanie kursora i
toplevelów, backend portalu PipeWire. Szczegóły: `docs/PROTOCOLS.md`, `CHANGELOG.md`. Jak złożyć poważny kompozytor: `docs/GUIDE.md`.

## Co jest sprawdzone, a co nie

**Sprawdzone uruchomieniem:** samodzielny blok C++ shima (konfiguracja, IPC wraz z subskrypcjami, token, procesy,
obserwator pliku), silnik screencopy z prawdziwym klientem libwayland (4 klatki, błędy protokołu, zmiana rozmiaru, zniknięcie
wyjścia), serwer IPC sway/i3 z prawdziwymi gniazdami (ramki, fragmentacja, subskrypcje, śmieci), `wlr-screencopy` z klientem libwayland,
fuzz parsera konfiguracji (3000 losowych plików), oba pliki `.h#` parserem H# 0.9, blok libinput (parser reguł, kompilacja z prawdziwym `libinput.h`), `volcanctl`, model układów
(12000 losowych przypadków), przykładowy plik konfiguracyjny wczytany prawdziwym loaderem, kontrole `check_shim.py`
i `check_hcs.py`.

**Niekompilowane** (brak `hackerc` 0.5 i Skia): wszystkie pliki `.hcs` (w tym `app.hcs`, `easy.hcs`, `screencopy.hcs`), logika sway/i3 (`i3.hcs`: tłumaczenie poleceń, JSON), `idle.hcs`, widżety paska, `ontop`, kod
sklejający screencopy z Louvre (`sc_prov_*`, `vcn_screencopy_enable`, `VOutput::presentationEvent`; API sprawdzone z
źródłami Louvre 3.0.0, bez kompilacji), testy w `tests/cmd`,
przykłady oraz kod C++ sklejający z Louvre (`VSeat::inputDevicePlugged`, `vcn_li_apply`, `vcn_surface_*`,
`pointerConstraintModeChanged`; sygnatury sprawdzone z nagłówkami Louvre i cz-core, ale bez kompilacji). Dotyczy to też
założeń z poniższej listy. `tests/integration` wymaga zbudowanego przykładu i weston.



Nie miałem w środowisku ani `hackerc`, ani `bit`, ani zbudowanego Louvre z jego zależnościami
(cz-core, Skia, SRM, Ream), więc **nic tu nie zostało skompilowane**. Sprawdzone ręcznie:

* składnia `.hcs` zgodna z `docs/SYNTAX.md` i z kodem `hackerc` (`transpiler.hcs`, `project.hcs`, `codegen.hcs`),
  zbilansowane nawiasy, brak zdublowanych nazw funkcji i stałych;
* sygnatury C++ (`LSurface`, `LToplevelRole`, `LSeat`, `LPointer`, `LKeyboard`, `LOutput`, `LCursor`,
  `LCompositor::createObjectRequest`) porównane z nagłówkami Louvre z gałęzi `main` (3.0.0);
  pola zdarzeń (`delta`, `button`, `pressed`, `code`, `isPressed`) z kodu domyślnych implementacji.

Znane ryzyka — pierwsze miejsca do sprawdzenia przy pierwszym buildzie:

1. **Louvre 3.0 to gałąź `main` (przestrzeń nazw `CZ`)**, nie wydania 2.x (`Louvre::`) — bindingi pod 2.x wymagałyby przepisania `shim.hcs`.
2. Typy z `cz-core` (`CZBitset`, `CZEvent`, `CZPointerMoveEvent`…) nie były dostępne lokalnie; operacje na `CZBitset` (`has`, `|`, `& ~`) wzięte z użycia w kodzie Louvre.
3. Ekstraktor bloków `native` w `hackerc` liczy nawiasy kwadratowe `[ ]` także w stringach i komentarzach — w `shim.hcs` są zbilansowane (sprawdzone skryptem), przy edycji tego pilnuj.
4. Blok C++ trafia do jednego pliku `.cpp` na blok; `shim.hcs` jest dołączany tylko przez `compositor.hcs`, żeby nie powstały dwie kopie symboli.
5. Czy `bit` kompiluje bloki C++ z zależności i jak pakuje je do `.hlib` — nie znalazłem tego w kodzie; w razie problemów zbuduj `volcan` jako członka workspace (`get <work:volcan>`).
6. Kolejność linkowania `cz-louvre` względem obiektów z bloku C++ (patrz `RUSTFLAGS` wyżej).
7. Typ `Float` zakładam jako `f64` (tak jak `Int` = `i64` widać w `std`).

## Licencja

MIT (volcan). Louvre — LGPL-2.1, linkowany dynamicznie, nie jest częścią tego repozytorium.
