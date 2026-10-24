#include <wayland-server-core.h>
#include <wayland-server-protocol.h>
#include <wayland-client.h>
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <string>
#include <thread>
#include <vector>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ext-image-capture-source-v1-server.h"
#include "ext-image-capture-source-v1-client.h"
#include "ext-image-copy-capture-v1-client.h"
#include "wlr-screencopy-unstable-v1-client.h"

extern "C" const struct wl_interface ext_foreign_toplevel_handle_v1_interface = { "ext_foreign_toplevel_handle_v1", 1, 0, nullptr, 0, nullptr };

namespace
{
#include PROTOGEN_INC
#include SCREENCOPY_INC
}

static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "BLAD %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

// ---- serwer: wzor pikseli i rozmiary wyjsc ----
static std::atomic<int> g_pattern { 0 };
static std::atomic<int> g_w2 { 16 }, g_h2 { 16 };
static std::atomic<bool> g_remove2 { false };
static std::atomic<bool> g_quit { false };
static std::atomic<bool> g_hold_paint { false };
static std::atomic<bool> g_remove1 { false };

static bool prov_size(int64_t out, int *w, int *h, void *)
{
    if (out == 1) { *w = 64; *h = 48; return true; }
    if (out == 2) { *w = g_w2; *h = g_h2; return true; }
    return false;
}

static bool prov_read(int64_t out, uint8_t *dst, int stride, int w, int h, uint32_t fmt, void *)
{
    // ARGB8888 / XRGB8888 w pamieci (little endian): B, G, R, A
    (void)fmt;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
        {
            uint8_t *p = dst + (size_t)y * (size_t)stride + (size_t)x * 4u;
            p[2] = (uint8_t)((x + g_pattern.load()) & 255);   // R
            p[1] = (uint8_t)y;                                // G
            p[0] = (uint8_t)out;                              // B
            p[3] = 0;
        }
    return true;
}

static int64_t prov_resolve_output(struct wl_resource *o, void *) { return (int64_t)(intptr_t)wl_resource_get_user_data(o); }
static int64_t prov_scale(int64_t out, void *) { return out == 1 ? 1000 : 2000; }

static int64_t prov_resolve(struct wl_resource *source, void *)
{
    return (int64_t)(intptr_t)wl_resource_get_user_data(source);
}

static void output_bind(struct wl_client *c, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(c, &wl_output_interface, version > 4 ? 4 : (int)version, id);
    wl_resource_set_implementation(r, nullptr, data, nullptr);
}

static void src_destroy(struct wl_client *, struct wl_resource *r) { wl_resource_destroy(r); }
static const struct ext_image_capture_source_v1_interface src_impl = { src_destroy };

static void srcmgr_create(struct wl_client *c, struct wl_resource *mgr, uint32_t id, struct wl_resource *output)
{
    struct wl_resource *r = wl_resource_create(c, &ext_image_capture_source_v1_interface, 1, id);
    wl_resource_set_implementation(r, &src_impl, wl_resource_get_user_data(output), nullptr);
    (void)mgr;
}
static const struct ext_output_image_capture_source_manager_v1_interface srcmgr_impl = { srcmgr_create, src_destroy };

static void srcmgr_bind(struct wl_client *c, void *, uint32_t, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(c, &ext_output_image_capture_source_manager_v1_interface, 1, id);
    wl_resource_set_implementation(r, &srcmgr_impl, nullptr, nullptr);
}

static void ptr_release(struct wl_client *, struct wl_resource *r) { wl_resource_destroy(r); }
static void ptr_set_cursor(struct wl_client *, struct wl_resource *, uint32_t, struct wl_resource *, int32_t, int32_t) {}
static const struct wl_pointer_interface ptr_impl = { ptr_set_cursor, ptr_release };
static void seat_get_pointer(struct wl_client *c, struct wl_resource *seat, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(c, &wl_pointer_interface, wl_resource_get_version(seat), id);
    wl_resource_set_implementation(r, &ptr_impl, nullptr, nullptr);
}
static void seat_unsupported(struct wl_client *, struct wl_resource *, uint32_t) {}
static void seat_release(struct wl_client *, struct wl_resource *r) { wl_resource_destroy(r); }
static const struct wl_seat_interface seat_impl = { seat_get_pointer, seat_unsupported, seat_unsupported, seat_release };
static void seat_bind(struct wl_client *c, void *, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(c, &wl_seat_interface, version > 5 ? 5 : (int)version, id);
    wl_resource_set_implementation(r, &seat_impl, nullptr, nullptr);
    wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_POINTER);
}

// ---- klient ----
struct Client
{
    struct wl_display *dpy = nullptr;
    struct wl_registry *reg = nullptr;
    struct wl_shm *shm = nullptr;
    struct wl_seat *seat = nullptr;
    struct wl_output *out1 = nullptr, *out2 = nullptr, *out7 = nullptr;
    struct ext_output_image_capture_source_manager_v1 *srcmgr = nullptr;
    struct ext_image_copy_capture_manager_v1 *mgr = nullptr;
    struct zwlr_screencopy_manager_v1 *wlr = nullptr;
    int nout = 0;
};

static void *wlr_bind_helper(struct wl_registry *r, uint32_t name, uint32_t ver) { return wl_registry_bind(r, name, &zwlr_screencopy_manager_v1_interface, ver > 3 ? 3 : ver); }

static void reg_global(void *d, struct wl_registry *r, uint32_t name, const char *iface, uint32_t ver)
{
    Client *c = (Client *)d;
    if (!std::strcmp(iface, "wl_shm")) c->shm = (wl_shm *)wl_registry_bind(r, name, &wl_shm_interface, 1);
    else if (!std::strcmp(iface, "wl_seat")) c->seat = (wl_seat *)wl_registry_bind(r, name, &wl_seat_interface, 5);
    else if (!std::strcmp(iface, "wl_output")) { wl_output *o = (wl_output *)wl_registry_bind(r, name, &wl_output_interface, ver > 4 ? 4 : ver); (c->nout == 0 ? c->out1 : c->nout == 1 ? c->out2 : c->out7) = o; c->nout++; }
    else if (!std::strcmp(iface, "ext_output_image_capture_source_manager_v1")) c->srcmgr = (ext_output_image_capture_source_manager_v1 *)wl_registry_bind(r, name, &ext_output_image_capture_source_manager_v1_interface, 1);
    else if (!std::strcmp(iface, "zwlr_screencopy_manager_v1")) c->wlr = (zwlr_screencopy_manager_v1 *)wlr_bind_helper(r, name, ver);
    else if (!std::strcmp(iface, "ext_image_copy_capture_manager_v1")) c->mgr = (ext_image_copy_capture_manager_v1 *)wl_registry_bind(r, name, &ext_image_copy_capture_manager_v1_interface, 1);
}
static void reg_remove(void *, struct wl_registry *, uint32_t) {}
static const struct wl_registry_listener reg_listener = { reg_global, reg_remove };

static bool wait_for(struct wl_display *d, const std::function<bool()> &cond, int ms = 3000)
{
    const auto t0 = std::chrono::steady_clock::now();
    while (true)
    {
        wl_display_dispatch_pending(d);
        if (cond()) return true;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count() > ms) return false;
        wl_display_flush(d);
        if (wl_display_prepare_read(d) != 0) continue;
        struct pollfd pfd = { wl_display_get_fd(d), POLLIN, 0 };
        if (poll(&pfd, 1, 20) > 0) wl_display_read_events(d); else wl_display_cancel_read(d);
    }
}

struct Sess
{
    uint32_t w = 0, h = 0;
    bool done = false, stopped = false;
    int nformats = 0;
    bool abgr = false;
    int dones = 0;
};
static void s_size(void *d, struct ext_image_copy_capture_session_v1 *, uint32_t w, uint32_t h) { Sess *s = (Sess *)d; s->w = w; s->h = h; }
static void s_fmt(void *d, struct ext_image_copy_capture_session_v1 *, uint32_t f) { Sess *s = (Sess *)d; s->nformats++; if (f == WL_SHM_FORMAT_ARGB8888) s->abgr = true; }
static void s_dev(void *, struct ext_image_copy_capture_session_v1 *, struct wl_array *) {}
static void s_dfmt(void *, struct ext_image_copy_capture_session_v1 *, uint32_t, struct wl_array *) {}
static void s_done(void *d, struct ext_image_copy_capture_session_v1 *) { Sess *s = (Sess *)d; s->done = true; s->dones++; }
static void s_stopped(void *d, struct ext_image_copy_capture_session_v1 *) { ((Sess *)d)->stopped = true; }
static const struct ext_image_copy_capture_session_v1_listener s_listener = { s_size, s_fmt, s_dev, s_dfmt, s_done, s_stopped };

struct Fr
{
    bool ready = false, failed = false;
    uint32_t reason = 99;
    bool damage_full = false;
    bool got_time = false;
    int w = 0, h = 0;
};
static void f_transform(void *, struct ext_image_copy_capture_frame_v1 *, uint32_t) {}
static void f_damage(void *d, struct ext_image_copy_capture_frame_v1 *, int32_t x, int32_t y, int32_t w, int32_t h) { Fr *f = (Fr *)d; f->damage_full = (x == 0 && y == 0 && w == f->w && h == f->h); }
static void f_time(void *d, struct ext_image_copy_capture_frame_v1 *, uint32_t, uint32_t, uint32_t) { ((Fr *)d)->got_time = true; }
static void f_ready(void *d, struct ext_image_copy_capture_frame_v1 *) { ((Fr *)d)->ready = true; }
static void f_failed(void *d, struct ext_image_copy_capture_frame_v1 *, uint32_t r) { Fr *f = (Fr *)d; f->failed = true; f->reason = r; }
static const struct ext_image_copy_capture_frame_v1_listener f_listener = { f_transform, f_damage, f_time, f_ready, f_failed };

struct Buf
{
    struct wl_buffer *buf = nullptr;
    uint8_t *data = nullptr;
    size_t size = 0;
    int w = 0, h = 0;
};
static Buf make_buf(Client &c, int w, int h, uint32_t fmt = WL_SHM_FORMAT_ARGB8888)
{
    Buf b;
    b.w = w; b.h = h; b.size = (size_t)w * (size_t)h * 4u;
    int fd = memfd_create("sc-test", 0);
    if (fd < 0 || ftruncate(fd, (off_t)b.size) != 0) { std::perror("memfd"); std::exit(2); }
    b.data = (uint8_t *)mmap(nullptr, b.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    std::memset(b.data, 0x11, b.size);
    struct wl_shm_pool *pool = wl_shm_create_pool(c.shm, fd, (int)b.size);
    b.buf = wl_shm_pool_create_buffer(pool, 0, w, h, w * 4, fmt);
    wl_shm_pool_destroy(pool);
    close(fd);
    return b;
}

static bool capture(Client &c, struct ext_image_copy_capture_session_v1 *sess, Buf &b, Fr &fr, int w, int h)
{
    fr = Fr();
    fr.w = w; fr.h = h;
    struct ext_image_copy_capture_frame_v1 *f = ext_image_copy_capture_session_v1_create_frame(sess);
    ext_image_copy_capture_frame_v1_add_listener(f, &f_listener, &fr);
    ext_image_copy_capture_frame_v1_attach_buffer(f, b.buf);
    ext_image_copy_capture_frame_v1_damage_buffer(f, 0, 0, w, h);
    ext_image_copy_capture_frame_v1_capture(f);
    const bool ok = wait_for(c.dpy, [&] { return fr.ready || fr.failed; });
    ext_image_copy_capture_frame_v1_destroy(f);
    wl_display_flush(c.dpy);
    return ok;
}


// ---- klient wlr-screencopy ----
struct WFr
{
    uint32_t nbuf = 0, w = 0, h = 0, stride = 0;
    bool buffer_done = false, ready = false, failed = false, damage = false;
    int version = 3;
};
static void wf_buffer(void *d, struct zwlr_screencopy_frame_v1 *, uint32_t fmt, uint32_t w, uint32_t h, uint32_t stride) { WFr *f = (WFr *)d; f->nbuf++; if (fmt == WL_SHM_FORMAT_ARGB8888) { f->w = w; f->h = h; f->stride = stride; } }
static void wf_flags(void *, struct zwlr_screencopy_frame_v1 *, uint32_t) {}
static void wf_ready(void *d, struct zwlr_screencopy_frame_v1 *, uint32_t, uint32_t, uint32_t) { ((WFr *)d)->ready = true; }
static void wf_failed(void *d, struct zwlr_screencopy_frame_v1 *) { ((WFr *)d)->failed = true; }
static void wf_damage(void *d, struct zwlr_screencopy_frame_v1 *, uint32_t, uint32_t, uint32_t, uint32_t) { ((WFr *)d)->damage = true; }
static void wf_dmabuf(void *, struct zwlr_screencopy_frame_v1 *, uint32_t, uint32_t, uint32_t) {}
static void wf_done(void *d, struct zwlr_screencopy_frame_v1 *) { ((WFr *)d)->buffer_done = true; }
static const struct zwlr_screencopy_frame_v1_listener wf_listener = { wf_buffer, wf_flags, wf_ready, wf_failed, wf_damage, wf_dmabuf, wf_done };

static void wlr_tests(Client &c, const std::string &sock)
{
    // pelne wyjscie 1 (64x48): zwykle copy od razu
    {
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output(c.wlr, 0, c.out1);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        CHECK(wait_for(c.dpy, [&] { return fr.buffer_done; }));
        CHECK(fr.w == 64 && fr.h == 48 && fr.stride == 256 && fr.nbuf == 2);
        Buf b = make_buf(c, 64, 48);
        zwlr_screencopy_frame_v1_copy(f, b.buf);
        CHECK(wait_for(c.dpy, [&] { return fr.ready || fr.failed; }));
        CHECK(fr.ready && !fr.damage);
        const uint8_t *p = b.data + (2 * 64 + 3) * 4;
        CHECK(p[1] == 2 && p[0] == 1 && p[3] == 255);
        zwlr_screencopy_frame_v1_destroy(f);
    }
    // wycinek (10,5) 20x10 na wyjsciu 1 (skala 1.0): piksel (0,0) wycinka to (10,5) wyjscia
    {
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output_region(c.wlr, 0, c.out1, 10, 5, 20, 10);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        CHECK(wait_for(c.dpy, [&] { return fr.buffer_done; }));
        CHECK(fr.w == 20 && fr.h == 10);
        Buf b = make_buf(c, 20, 10);
        zwlr_screencopy_frame_v1_copy(f, b.buf);
        CHECK(wait_for(c.dpy, [&] { return fr.ready || fr.failed; }));
        CHECK(fr.ready);
        const uint8_t *p = b.data;                         // piksel wycinka (0,0) = (10,5) wyjscia
        CHECK(p[1] == 5 && p[0] == 1);
        const uint8_t *q = b.data + (3 * 20 + 4) * 4;      // (4,3) wycinka = (14,8)
        CHECK(q[1] == 8);
        zwlr_screencopy_frame_v1_destroy(f);
    }
    // skala 2.0 (wyjscie 2 ma 24x20 px po zmianie): region logiczny (2,2) 4x4 = piksele (4,4) 8x8
    {
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output_region(c.wlr, 0, c.out2, 2, 2, 4, 4);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        CHECK(wait_for(c.dpy, [&] { return fr.buffer_done || fr.failed; }));
        CHECK(fr.w == 8 && fr.h == 8);
        zwlr_screencopy_frame_v1_destroy(f);
    }
    // copy_with_damage czeka na odmalowanie i dolacza damage
    {
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output(c.wlr, 0, c.out1);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        CHECK(wait_for(c.dpy, [&] { return fr.buffer_done; }));
        Buf b = make_buf(c, 64, 48);
        zwlr_screencopy_frame_v1_copy_with_damage(f, b.buf);
        CHECK(wait_for(c.dpy, [&] { return fr.ready || fr.failed; }));
        CHECK(fr.ready && fr.damage);
        zwlr_screencopy_frame_v1_destroy(f);
    }
    // region poza wyjsciem -> failed; nieznane wyjscie (7) -> failed
    {
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output_region(c.wlr, 0, c.out1, 500, 500, 10, 10);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        CHECK(wait_for(c.dpy, [&] { return fr.failed; }));
        zwlr_screencopy_frame_v1_destroy(f);
        WFr fr7;
        struct zwlr_screencopy_frame_v1 *g = zwlr_screencopy_manager_v1_capture_output(c.wlr, 0, c.out7);
        zwlr_screencopy_frame_v1_add_listener(g, &wf_listener, &fr7);
        CHECK(wait_for(c.dpy, [&] { return fr7.failed; }));
        zwlr_screencopy_frame_v1_destroy(g);
    }
    // bledy protokolu: zly bufor (invalid_buffer), podwojne copy (already_used)
    for (int mode = 0; mode < 2; mode++)
    {
        Client e;
        e.dpy = wl_display_connect(sock.c_str());
        e.reg = wl_display_get_registry(e.dpy);
        wl_registry_add_listener(e.reg, &reg_listener, &e);
        wl_display_roundtrip(e.dpy);
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output(e.wlr, 0, e.out1);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        wait_for(e.dpy, [&] { return fr.buffer_done; });
        if (mode == 0)
        {
            Buf bad = make_buf(e, 10, 10);
            zwlr_screencopy_frame_v1_copy(f, bad.buf);
        }
        else
        {
            Buf ok = make_buf(e, 64, 48);
            zwlr_screencopy_frame_v1_copy(f, ok.buf);
            wait_for(e.dpy, [&] { return fr.ready; });
            zwlr_screencopy_frame_v1_copy(f, ok.buf);
        }
        wl_display_flush(e.dpy);
        wait_for(e.dpy, [&] { return wl_display_get_error(e.dpy) != 0; });
        const struct wl_interface *iface = nullptr;
        uint32_t id = 0;
        CHECK(wl_display_get_protocol_error(e.dpy, &iface, &id) == (mode == 0 ? ZWLR_SCREENCOPY_FRAME_V1_ERROR_INVALID_BUFFER : ZWLR_SCREENCOPY_FRAME_V1_ERROR_ALREADY_USED));
        wl_display_disconnect(e.dpy);
    }
    // wyjscie znika w trakcie copy_with_damage -> failed
    {
        WFr fr;
        struct zwlr_screencopy_frame_v1 *f = zwlr_screencopy_manager_v1_capture_output(c.wlr, 0, c.out1);
        zwlr_screencopy_frame_v1_add_listener(f, &wf_listener, &fr);
        CHECK(wait_for(c.dpy, [&] { return fr.buffer_done; }));
        g_hold_paint = true;
        Buf b = make_buf(c, 64, 48);
        zwlr_screencopy_frame_v1_copy_with_damage(f, b.buf);
        wl_display_roundtrip(c.dpy);
        g_remove1 = true;
        CHECK(wait_for(c.dpy, [&] { return fr.failed; }));
        g_hold_paint = false;
        zwlr_screencopy_frame_v1_destroy(f);
    }
}

static void client_main(const std::string &sock)
{
    Client c;
    c.dpy = wl_display_connect(sock.c_str());
    if (!c.dpy) { std::fprintf(stderr, "klient: brak polaczenia\n"); g_fail++; g_quit = true; return; }
    c.reg = wl_display_get_registry(c.dpy);
    wl_registry_add_listener(c.reg, &reg_listener, &c);
    wl_display_roundtrip(c.dpy);
    CHECK(c.shm && c.srcmgr && c.mgr && c.out1 && c.out2 && c.out7);
    if (!(c.shm && c.srcmgr && c.mgr && c.out1 && c.out2 && c.out7)) { g_quit = true; return; }

    // 1. sesja na wyjsciu 1: parametry bufora i pierwsza klatka od razu
    struct ext_image_capture_source_v1 *src1 = ext_output_image_capture_source_manager_v1_create_source(c.srcmgr, c.out1);
    Sess s1;
    struct ext_image_copy_capture_session_v1 *sess1 = ext_image_copy_capture_manager_v1_create_session(c.mgr, src1, 0);
    ext_image_copy_capture_session_v1_add_listener(sess1, &s_listener, &s1);
    CHECK(wait_for(c.dpy, [&] { return s1.done; }));
    CHECK(s1.w == 64 && s1.h == 48);
    CHECK(s1.abgr && s1.nformats == 2);

    Buf b1 = make_buf(c, 64, 48);
    Fr fr;
    g_pattern = 5;
    CHECK(capture(c, sess1, b1, fr, 64, 48));
    CHECK(fr.ready && !fr.failed && fr.damage_full && fr.got_time);
    // piksel (3,2): R = (3 + wzor) & 255, G = 2, B = numer wyjscia, A = 255 (alfa wymuszona)
    const uint8_t *p = b1.data + (2 * 64 + 3) * 4;
    CHECK(p[1] == 2 && p[0] == 1 && p[3] == 255);
    const int pat1 = p[2] - 3;
    CHECK(pat1 >= 5);

    // 2. druga klatka czeka na odmalowanie: wzor musi sie zmienic
    CHECK(capture(c, sess1, b1, fr, 64, 48));
    CHECK(fr.ready);
    p = b1.data + (2 * 64 + 3) * 4;
    CHECK((int)p[2] - 3 > pat1);

    // 3. bufor o zlym rozmiarze -> failed(buffer_constraints = 1)
    Buf bad = make_buf(c, 32, 32);
    CHECK(capture(c, sess1, bad, fr, 32, 32));
    CHECK(fr.failed && fr.reason == 1);

    // 4. nieznane zrodlo (wyjscie 7) -> sesja od razu stopped
    struct ext_image_capture_source_v1 *src7 = ext_output_image_capture_source_manager_v1_create_source(c.srcmgr, c.out7);
    Sess s7;
    struct ext_image_copy_capture_session_v1 *sess7 = ext_image_copy_capture_manager_v1_create_session(c.mgr, src7, 0);
    ext_image_copy_capture_session_v1_add_listener(sess7, &s_listener, &s7);
    CHECK(wait_for(c.dpy, [&] { return s7.stopped; }));
    CHECK(!s7.done);
    {
        // klatka z zatrzymanej sesji konczy sie failed(stopped = 2)
        Buf b7 = make_buf(c, 8, 8);
        CHECK(capture(c, sess7, b7, fr, 8, 8));
        CHECK(fr.failed && fr.reason == 2);
    }

    // 5. zmiana rozmiaru wyjscia 2: nowe buffer_size + done, stara klatka failed(buffer_constraints), nowa dziala
    struct ext_image_capture_source_v1 *src2 = ext_output_image_capture_source_manager_v1_create_source(c.srcmgr, c.out2);
    Sess s2;
    struct ext_image_copy_capture_session_v1 *sess2 = ext_image_copy_capture_manager_v1_create_session(c.mgr, src2, 0);
    ext_image_copy_capture_session_v1_add_listener(sess2, &s_listener, &s2);
    CHECK(wait_for(c.dpy, [&] { return s2.done; }));
    CHECK(s2.w == 16 && s2.h == 16);
    Buf b2 = make_buf(c, 16, 16, WL_SHM_FORMAT_XRGB8888);
    CHECK(capture(c, sess2, b2, fr, 16, 16));
    CHECK(fr.ready);
    g_w2 = 24; g_h2 = 20;
    CHECK(capture(c, sess2, b2, fr, 16, 16));
    CHECK(fr.failed && fr.reason == 1);
    CHECK(wait_for(c.dpy, [&] { return s2.dones >= 2; }));
    CHECK(s2.w == 24 && s2.h == 20);
    Buf b2n = make_buf(c, 24, 20);
    CHECK(capture(c, sess2, b2n, fr, 24, 20));
    CHECK(fr.ready);

    // 6. zniknieciu wyjscia 2 sesja dostaje stopped
    g_remove2 = true;
    CHECK(wait_for(c.dpy, [&] { return s2.stopped; }));

    // 7. sesja kursora konczy sie od razu (kursor nie jest przechwytywany)
    struct ext_image_copy_capture_cursor_session_v1 *cur = ext_image_copy_capture_manager_v1_create_pointer_cursor_session(c.mgr, src1, wl_seat_get_pointer(c.seat));
    struct ext_image_copy_capture_session_v1 *cs = ext_image_copy_capture_cursor_session_v1_get_capture_session(cur);
    Sess scur;
    ext_image_copy_capture_session_v1_add_listener(cs, &s_listener, &scur);
    CHECK(wait_for(c.dpy, [&] { return scur.stopped; }));

    // 8. bledy protokolu: druga klatka sesji przed skonczeniem pierwszej (osobne polaczenie, bo blad zabija klienta)
    {
        Client e;
        e.dpy = wl_display_connect(sock.c_str());
        e.reg = wl_display_get_registry(e.dpy);
        wl_registry_add_listener(e.reg, &reg_listener, &e);
        wl_display_roundtrip(e.dpy);
        struct ext_image_capture_source_v1 *es = ext_output_image_capture_source_manager_v1_create_source(e.srcmgr, e.out1);
        Sess ss;
        struct ext_image_copy_capture_session_v1 *esess = ext_image_copy_capture_manager_v1_create_session(e.mgr, es, 0);
        ext_image_copy_capture_session_v1_add_listener(esess, &s_listener, &ss);
        wait_for(e.dpy, [&] { return ss.done; });
        ext_image_copy_capture_session_v1_create_frame(esess);
        ext_image_copy_capture_session_v1_create_frame(esess);
        wl_display_flush(e.dpy);
        wait_for(e.dpy, [&] { return wl_display_get_error(e.dpy) != 0; });
        const struct wl_interface *iface = nullptr;
        uint32_t id = 0;
        const uint32_t code = wl_display_get_protocol_error(e.dpy, &iface, &id);
        CHECK(code == EXT_IMAGE_COPY_CAPTURE_SESSION_V1_ERROR_DUPLICATE_FRAME);
        wl_display_disconnect(e.dpy);
    }
    {
        // capture bez bufora -> no_buffer
        Client e;
        e.dpy = wl_display_connect(sock.c_str());
        e.reg = wl_display_get_registry(e.dpy);
        wl_registry_add_listener(e.reg, &reg_listener, &e);
        wl_display_roundtrip(e.dpy);
        struct ext_image_capture_source_v1 *es = ext_output_image_capture_source_manager_v1_create_source(e.srcmgr, e.out1);
        Sess ss;
        struct ext_image_copy_capture_session_v1 *esess = ext_image_copy_capture_manager_v1_create_session(e.mgr, es, 0);
        ext_image_copy_capture_session_v1_add_listener(esess, &s_listener, &ss);
        wait_for(e.dpy, [&] { return ss.done; });
        struct ext_image_copy_capture_frame_v1 *f = ext_image_copy_capture_session_v1_create_frame(esess);
        ext_image_copy_capture_frame_v1_capture(f);
        wl_display_flush(e.dpy);
        wait_for(e.dpy, [&] { return wl_display_get_error(e.dpy) != 0; });
        const struct wl_interface *iface = nullptr;
        uint32_t id = 0;
        CHECK(wl_display_get_protocol_error(e.dpy, &iface, &id) == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_ERROR_NO_BUFFER);
        wl_display_disconnect(e.dpy);
    }
    {
        // nieznana opcja -> invalid_option
        Client e;
        e.dpy = wl_display_connect(sock.c_str());
        e.reg = wl_display_get_registry(e.dpy);
        wl_registry_add_listener(e.reg, &reg_listener, &e);
        wl_display_roundtrip(e.dpy);
        struct ext_image_capture_source_v1 *es = ext_output_image_capture_source_manager_v1_create_source(e.srcmgr, e.out1);
        ext_image_copy_capture_manager_v1_create_session(e.mgr, es, 0x80);
        wl_display_flush(e.dpy);
        wait_for(e.dpy, [&] { return wl_display_get_error(e.dpy) != 0; });
        const struct wl_interface *iface = nullptr;
        uint32_t id = 0;
        CHECK(wl_display_get_protocol_error(e.dpy, &iface, &id) == EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_ERROR_INVALID_OPTION);
        wl_display_disconnect(e.dpy);
    }

    // 9. sprzatanie klienta w trakcie oczekujacej klatki nie moze wywrocic serwera
    {
        struct ext_image_copy_capture_frame_v1 *f = ext_image_copy_capture_session_v1_create_frame(sess1);
        Buf bb = make_buf(c, 64, 48);
        ext_image_copy_capture_frame_v1_attach_buffer(f, bb.buf);
        ext_image_copy_capture_frame_v1_capture(f);
        wl_buffer_destroy(bb.buf);          // bufor znika przed odmalowaniem -> klatka konczy sie bledem, bez crasha
        wl_display_roundtrip(c.dpy);
        Fr dummy;
        ext_image_copy_capture_frame_v1_add_listener(f, &f_listener, &dummy);
        wait_for(c.dpy, [&] { return dummy.ready || dummy.failed; }, 1000);
        ext_image_copy_capture_frame_v1_destroy(f);
        ext_image_copy_capture_session_v1_destroy(sess1);
        wl_display_roundtrip(c.dpy);
    }
    CHECK(c.wlr != nullptr);
    if (c.wlr)
        wlr_tests(c, sock);
    wl_display_disconnect(c.dpy);
    g_quit = true;
}

int main()
{
    setenv("XDG_RUNTIME_DIR", "/tmp", 0);
    struct wl_display *display = wl_display_create();
    const char *name = wl_display_add_socket_auto(display);
    if (!name) { std::fprintf(stderr, "brak gniazda\n"); return 2; }
    wl_display_init_shm(display);
    for (intptr_t i : { 1, 2, 7 })
        wl_global_create(display, &wl_output_interface, 4, (void *)i, output_bind);
    wl_global_create(display, &wl_seat_interface, 5, nullptr, seat_bind);
    wl_global_create(display, &ext_output_image_capture_source_manager_v1_interface, 1, nullptr, srcmgr_bind);

    ScProvider prov;
    prov.resolve = prov_resolve;
    prov.size = prov_size;
    prov.read = prov_read;
    prov.resolve_output = prov_resolve_output;
    prov.scale_permille = prov_scale;
    CHECK(vcn_sc_init(display, prov));
    CHECK(!vcn_sc_init(display, prov));      // drugi raz nie wolno

    std::thread th(client_main, std::string(name));
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    auto last = std::chrono::steady_clock::now();
    while (!g_quit)
    {
        wl_event_loop_dispatch(loop, 5);
        wl_display_flush_clients(display);
        const auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last).count() >= 20)
        {
            last = now;
            g_pattern++;
            if (!g_hold_paint) { vcn_sc_output_painted(1); vcn_sc_output_painted(2); }
        }
        if (g_remove2.exchange(false))
            vcn_sc_output_removed(2);
        if (g_remove1.exchange(false))
            vcn_sc_output_removed(1);
    }
    th.join();
    for (int i = 0; i < 20; i++) { wl_event_loop_dispatch(loop, 1); wl_display_flush_clients(display); }
    CHECK(vcn_sc_session_count() == 0 || vcn_sc_session_count() == 2);   // sesje klienta sprzatnięte przy rozlaczeniu (2 = nie zdazyly)
    CHECK(vcn_sc_frames_ok() >= 8);
    vcn_sc_shutdown();
    wl_display_destroy(display);
    if (g_fail) { std::fprintf(stderr, "screencopy: %d BLEDOW\n", g_fail); return 1; }
    std::printf("screencopy: OK (klatki ok=%lld, failed=%lld)\n", (long long)vcn_sc_frames_ok(), (long long)vcn_sc_frames_failed());
    return 0;
}
