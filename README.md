# volcan

Biblioteka do pisania kompozytorów Wayland w **HackerScript**, dla menedżera pakietów **bit** (bit.io).
To bindingi do [Louvre](https://github.com/CuarzoSoftware/Louvre) 3.x (C++, LGPL-2.1).
Cały kod biblioteki — także warstwa C++ — siedzi w plikach `.hcs`; warstwa C++ jest zwykłym blokiem `native {C++}`.

```
lib/
  mod.hcs         punkt wejścia (get <bit:volcan>), dołącza resztę
  shim.hcs        JEDEN blok C++: podklasy Louvre + płaskie API C (vcn_*)
  ffi.hcs         80 cienkich opakowań ffi_* na funkcje vcn_*
  link.hcs        get <extern:cz-louvre> use <dynamic> — linkowanie z Louvre
  text.hcs        napisy UTF-8 przez granicę C (dekoder na samej arytmetyce)
  events.hcs      kolejka zdarzeń: EV_*, struct Event, event_next()
  compositor.hcs  start, pętla, środowisko, uruchamianie klientów
  surface.hcs     powierzchnie i okna toplevel
  output.hcs      monitory
  input.hcs       kursor, wskaźnik, klawiatura, seat
examples/minimal  działający szkic kompozytora (kafelki/fokus/skróty)
docs/NATIVE_CPP.md  jak działa blok C++ w hackerc i jakie ma ograniczenia
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

## Co jest sprawdzone, a co nie

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
