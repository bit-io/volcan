# Jak volcan trzyma C++ w HackerScript

`hackerc` 0.5 przetwarza blok `native {C++} [ ... ]` tak (źródło: `hackerc/cmd/project.hcs`):

1. linie zaczynające się od `#` trafiają na początek pliku `.cpp`,
2. reszta staje się ciałem `extern "C" void __hks_native_cpp_block_N() { ... }`,
3. plik kompiluje crate `cc` (`.cpp(true)`), a obiekt jest linkowany statycznie.

Ciało funkcji nie pozwala zdefiniować klas dziedziczących po Louvre ani funkcji `extern "C"`
na poziomie pliku. Dlatego `lib/shim.hcs`:

* ma na początku same `#include`,
* pierwszą linią reszty jest `}` — zamyka wygenerowaną funkcję,
* dalej leży zwykły kod poziomu pliku (klasy, funkcje `vcn_*`),
* ostatnia linia otwiera atrapową funkcję `volcan_shim_tail_unused() {`, a zamykającą klamrę dokleja `hackerc`.

Efekt to poprawny plik C++, w którym `__hks_native_cpp_block_N` jest pustą funkcją.

## Reguły przy edycji `shim.hcs`

* Nawiasy `[` i `]` muszą być zbilansowane w całym bloku (także w komentarzach i stringach) —
  ekstraktor kończy blok na zbilansowanym `]`.
* Nie używaj: znaku dolara, `!!`, `!>`, `<!`, ciągu `native {` ani `direct [` w treści bloku.
* Linie zaczynające się od `#` są wynoszone na górę — bez makr wielolinijkowych i `#ifdef` w środku.
* Jeden blok C++ na bibliotekę (numeracja `__hks_native_cpp_block_N` jest per plik).

## Dlaczego `ffi.hcs` ma bloki Rust

Wywołania funkcji `vcn_*` deklaruje lokalnie blok `native {Rust}` (`extern "C" { fn vcn_x(...); }`
wewnątrz funkcji). Dzięki temu nie trzeba osobnej biblioteki `.a` ani nazwy do `#[link]` —
symbole pochodzą z bloku C++ zlinkowanego statycznie. Jedyny `get <extern:...>` w projekcie
(`link.hcs`) dotyczy samego `libcz-louvre`.
