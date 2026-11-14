#include "raylib.h"
#include <vector>
#include <cmath>
#include <random>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include <unordered_map>
#include <string>

#if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #define Rectangle Win32Rectangle
#define CloseWindow Win32CloseWindow
#define ShowCursor Win32ShowCursor
#define DrawText Win32DrawText
#define LoadImage Win32LoadImage
#define PlaySound Win32PlaySound
#include <windows.h>
    #include <shellapi.h>
#undef Rectangle
#undef CloseWindow
#undef ShowCursor
#undef DrawText
#undef LoadImage
#undef PlaySound
    #ifndef WS_EX_NOACTIVATE
        #define WS_EX_NOACTIVATE 0x08000000L
    #endif
    #undef Rectangle
    #undef CloseWindow
    #undef ShowCursor
#elif defined(__linux__)
    #define Font X11Font
    #include <X11/Xlib.h>
    #include <X11/Xatom.h>
    #include <X11/Xutil.h>
    #include <X11/keysym.h>
    #include <unistd.h>
    #undef Font
#endif

constexpr int SNOWMAN_SKIN_COUNT = 10;

struct WindowData {
    uintptr_t id;
    Rectangle rect;
    bool is_maximized;
};

struct DockInfo {
    bool active;
    Rectangle rect;
};

struct Icicle {
    float rel_x;
    float length;
    float max_length;
    float base_width;
    float grow_rate;
};

struct FallingIcicle {
    uintptr_t origin_win_id;
    Vector2 pos;
    float vx;
    float vy;
    float length;
    float base_width;
    float angle;
    float ang_vel;
};

struct DriftProfile {
    Rectangle rect;
    Rectangle prev_rect;
    bool has_prev;
    bool is_maximized;
    float step;
    float left_shift_accum;
    float pending_cut_right;
    float prev_vx;
    float prev_vy;
    float shake_energy;
    float icicle_spawn_timer;
    std::vector<float> heights;
    std::vector<float> wave_shape;
    std::vector<Icicle> icicles;
    bool alive;
};

struct Snowflake {
    Vector2 pos;
    Vector2 vel;
    float radius;
    float sway_phase;
    float sway_freq;
    float sway_amp;
    unsigned char alpha;
    int layer;
    bool is_avalanche;
    bool is_ice;
    float life;
    float max_life;
};

struct Snowman {
    uintptr_t win_id; // 0 = на нижнем сугробе/доке, иначе ID окна
    bool on_dock; // Стоит на всплывающем нижнем доке
    float rel_x; // X относительно левого края окна/дока (или экрана)
    float rel_y_offset; // Смещение по Y от верха окна/дока (или ground_y)
    int stage; // 1 = 1 шар, 2 = 2 шара, 3 = 3 шара (готовый снеговик)
    int skin; // 0..9 — уникальный скин снеговика
    float r1, r2, r3; // Одинаковый радиус скатанных шаров
    float anim_pop; // Анимация установки шара (0..1)
    bool falling; // Падает вниз, если окно закрыли/сузили/тряхнули или сбили шаром
    Vector2 world_pos; // Экранная позиция основания
    float fall_vx;
    float fall_vy;
    float fall_angle; // Угол опрокидывания при сбивании
    float fall_ang_vel;
    int facing; // -1 или 1 (куда смотрит морковка)
};

struct HeldSnowball {
    bool active;
    Vector2 pos;
    Vector2 vel;
    float radius;
    float roll_accum;
};

struct FallingBall {
    Vector2 pos;
    float vx;
    float vy;
    float start_y;
    float radius;
    float age;
    bool is_thrown;
};

struct ExhaustPuff {
    Vector2 pos;
    Vector2 vel;
    float radius;
    float life;
    float max_life;
};

struct Snowplow {
    bool active;
    float x;
    float y;
    float vib_y;
    float dir; // -1.0 (справа налево) или 1.0 (слева направо)
    float speed;
    float wheel_angle;
    float beacon_phase;
    float exhaust_timer;
};

// ============================================================================
// ВСПОМОГАТЕЛЬНАЯ ГЕОМЕТРИЯ ДЛЯ ПСЕВДО-3D ОТРИСОВКИ
// ============================================================================

inline void DrawTriCCW(Vector2 a, Vector2 b, Vector2 c, Color col) {
    float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross < 0.0f) {
        DrawTriangle(a, b, c, col);
    } else {
        DrawTriangle(a, c, b, col);
    }
}

inline void DrawQuadCCW(Vector2 tl, Vector2 bl, Vector2 br, Vector2 tr, Color col) {
    DrawTriCCW(tl, bl, br, col);
    DrawTriCCW(tl, br, tr, col);
}

// ============================================================================
// ПЛАТФОРМОЗАВИСИМЫЙ СКАНЕР ОКОН, МЕНЮ ПКМ, ДОКОВ, ИГР И КЛАВИШ
// ============================================================================

#if defined(__linux__)
static int X11ErrorHandler(Display*, XErrorEvent*) {
    return 0;
}

class LinuxPlatform {
public:
    Display* dpy = nullptr;
    Window root = 0;
    Window self_win = 0;
    int overlay_x = 0;
    int overlay_y = 0;
    int overlay_w = 1920;
    int overlay_h = 1080;
    float bottom_panel_top = 0.0f;
    DockInfo floating_dock = { false, {} };

    Atom prop_stack = None;
    Atom prop_clients = None;
    Atom prop_active = None;
    Atom prop_type = None;
    Atom prop_state = None;
    Atom prop_gtk_extents = None;
    Atom prop_net_extents = None;
    Atom prop_pid = None;

    Atom state_hidden = None;
    Atom state_max_v = None;
    Atom state_max_h = None;
    Atom state_fullscreen = None;
    Atom state_skip_taskbar = None;
    Atom state_skip_pager = None;
    Atom state_above = None;

    Atom type_dock = None;
    Atom type_desktop = None;
    Atom type_menu = None;
    Atom type_dropdown = None;
    Atom type_popup = None;
    Atom type_tooltip = None;
    Atom type_notification = None;
    Atom type_combo = None;
    Atom type_splash = None;
    Atom type_dialog = None;
    Atom type_utility = None;

    bool prev_f8 = false;
    bool prev_f9 = false;
    bool prev_f10 = false;

    double last_popup_scan = -1.0;
    std::vector<WindowData> cached_popups;

    LinuxPlatform() {
        dpy = XOpenDisplay(NULL);
        if (!dpy) return;
        XSetErrorHandler(X11ErrorHandler);
        root = DefaultRootWindow(dpy);

        prop_stack = XInternAtom(dpy, "_NET_CLIENT_LIST_STACKING", False);
        prop_clients = XInternAtom(dpy, "_NET_CLIENT_LIST", False);
        prop_active = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
        prop_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
        prop_state = XInternAtom(dpy, "_NET_WM_STATE", False);
        prop_gtk_extents = XInternAtom(dpy, "_GTK_FRAME_EXTENTS", False);
        prop_net_extents = XInternAtom(dpy, "_NET_FRAME_EXTENTS", False);
        prop_pid = XInternAtom(dpy, "_NET_WM_PID", False);

        state_hidden = XInternAtom(dpy, "_NET_WM_STATE_HIDDEN", False);
        state_max_v = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        state_max_h = XInternAtom(dpy, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        state_fullscreen = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
        state_skip_taskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
        state_skip_pager = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
        state_above = XInternAtom(dpy, "_NET_WM_STATE_ABOVE", False);

        type_dock = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DOCK", False);
        type_desktop = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
        type_menu = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_MENU", False);
        type_dropdown = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DROPDOWN_MENU", False);
        type_popup = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_POPUP_MENU", False);
        type_tooltip = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_TOOLTIP", False);
        type_notification = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_NOTIFICATION", False);
        type_combo = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_COMBO", False);
        type_splash = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_SPLASH", False);
        type_dialog = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
        type_utility = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_UTILITY", False);
    }

    ~LinuxPlatform() {
        if (dpy) XCloseDisplay(dpy);
    }

    bool IsKeyDownGlobal(KeySym ks, const char keys[32]) {
        if (!dpy) return false;
        KeyCode kc = XKeysymToKeycode(dpy, ks);
        if (kc == 0) return false;
        return (keys[kc / 8] & (1 << (kc % 8))) != 0;
    }

    void PollGlobalHotkeys(bool& f8_pressed, bool& f9_pressed, bool& f10_pressed, bool& ctrl_shift_held) {
        f8_pressed = f9_pressed = f10_pressed = ctrl_shift_held = false;
        if (!dpy) return;
        char keys[32];
        XQueryKeymap(dpy, keys);

        bool f8 = IsKeyDownGlobal(XK_F8, keys);
        bool f9 = IsKeyDownGlobal(XK_F9, keys);
        bool f10 = IsKeyDownGlobal(XK_F10, keys);

        f8_pressed = (f8 && !prev_f8);
        f9_pressed = (f9 && !prev_f9);
        f10_pressed = (f10 && !prev_f10);

        prev_f8 = f8;
        prev_f9 = f9;
        prev_f10 = f10;

        bool ctrl = IsKeyDownGlobal(XK_Control_L, keys) || IsKeyDownGlobal(XK_Control_R, keys);
        bool shift = IsKeyDownGlobal(XK_Shift_L, keys) || IsKeyDownGlobal(XK_Shift_R, keys);
        ctrl_shift_held = (ctrl && shift);
    }

    void SendWmState(Window w, Atom a1, Atom a2) {
        XEvent ev;
        std::memset(&ev, 0, sizeof(ev));
        ev.xclient.type = ClientMessage;
        ev.xclient.window = w;
        ev.xclient.message_type = prop_state;
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = 1;
        ev.xclient.data.l[1] = (long)a1;
        ev.xclient.data.l[2] = (long)a2;
        ev.xclient.data.l[3] = 1;
        XSendEvent(dpy, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ev);
    }

    void FindAndConfigureSelf(const char* appTitle) {
        if (!dpy || self_win != 0) return;
        pid_t my_pid = getpid();
        Atom target = (prop_stack != None) ? prop_stack : prop_clients;
        Atom type; int format; unsigned long nitems = 0, after = 0;
        unsigned char* prop = NULL;

        if (XGetWindowProperty(dpy, root, target, 0, 1024, False, XA_WINDOW,
                               &type, &format, &nitems, &after, &prop) == Success && prop) {
            Window* wins = (Window*)prop;
            for (unsigned long i = 0; i < nitems; i++) {
                Window w = wins[i];
                bool match = false;
                Atom t_p; int f_p; unsigned long n_p = 0, a_p = 0; unsigned char* p_p = NULL;
                if (XGetWindowProperty(dpy, w, prop_pid, 0, 1, False, XA_CARDINAL,
                                       &t_p, &f_p, &n_p, &a_p, &p_p) == Success && p_p) {
                    if (n_p > 0 && *(unsigned long*)p_p == (unsigned long)my_pid) match = true;
                    XFree(p_p);
                }
                if (!match) {
                    char* name = NULL;
                    if (XFetchName(dpy, w, &name) && name) {
                        if (std::strcmp(name, appTitle) == 0) match = true;
                        XFree(name);
                    }
                }
                if (match) {
                    self_win = w;
                    SendWmState(self_win, state_skip_taskbar, state_skip_pager);
                    SendWmState(self_win, state_above, None);
                    XFlush(dpy);
                    break;
                }
            }
            XFree(prop);
        }
    }

    bool GetCardinals4(Window w, Atom prop_atom, long out[4]) {
        if (prop_atom == None) return false;
        Atom type; int format; unsigned long nitems = 0, after = 0;
        unsigned char* data = NULL;
        bool ok = false;
        if (XGetWindowProperty(dpy, w, prop_atom, 0, 4, False, XA_CARDINAL,
                               &type, &format, &nitems, &after, &data) == Success && data) {
            if (nitems >= 4) {
                long* vals = (long*)data;
                for (int i = 0; i < 4; i++) out[i] = vals[i];
                ok = true;
            }
            XFree(data);
        }
        return ok;
    }

    bool IsGameRunning(int screenW, int screenH) {
        if (!dpy || prop_active == None) return false;
        Atom type; int format; unsigned long nitems = 0, after = 0;
        unsigned char* prop = NULL;
        Window active = 0;

        if (XGetWindowProperty(dpy, root, prop_active, 0, 1, False, XA_WINDOW,
                               &type, &format, &nitems, &after, &prop) == Success && prop) {
            if (nitems > 0) active = *(Window*)prop;
            XFree(prop);
        }
        if (active == 0 || active == self_win) return false;

        bool is_fs = false;
        if (prop_state != None && state_fullscreen != None) {
            Atom t_st; int f_st; unsigned long n_st = 0, b_st = 0; unsigned char* p_st = NULL;
            if (XGetWindowProperty(dpy, active, prop_state, 0, 32, False, XA_ATOM,
                                   &t_st, &f_st, &n_st, &b_st, &p_st) == Success && p_st) {
                Atom* atoms = (Atom*)p_st;
                for (unsigned long k = 0; k < n_st; k++) {
                    if (atoms[k] == state_fullscreen) is_fs = true;
                }
                XFree(p_st);
            }
        }

        bool is_browser = false;
        if (prop_pid != None) {
            Atom t_p; int f_p; unsigned long n_p = 0, a_p = 0; unsigned char* p_p = NULL;
            if (XGetWindowProperty(dpy, active, prop_pid, 0, 1, False, XA_CARDINAL,
                                   &t_p, &f_p, &n_p, &a_p, &p_p) == Success && p_p) {
                if (n_p > 0) {
                    unsigned long pid = *(unsigned long*)p_p;
                    char path[64];
                    std::snprintf(path, sizeof(path), "/proc/%lu/comm", pid);
                    FILE* fp = std::fopen(path, "r");
                    if (fp) {
                        char comm[64] = {0};
                        if (std::fgets(comm, sizeof(comm), fp)) {
                            if (std::strstr(comm, "firefox") || std::strstr(comm, "chrome") ||
                                std::strstr(comm, "chromium") || std::strstr(comm, "brave") ||
                                std::strstr(comm, "opera") || std::strstr(comm, "vivaldi") ||
                                std::strstr(comm, "yandex")) {
                                is_browser = true;
                            }
                            if (std::strstr(comm, "wine") || std::strstr(comm, "gamescope") ||
                                std::strstr(comm, "cs2") || std::strstr(comm, "dota2") ||
                                std::strstr(comm, "hl2") || std::strstr(comm, "retroarch") ||
                                std::strstr(comm, "rpcs3") || std::strstr(comm, "pcsx2")) {
                                std::fclose(fp);
                                XFree(p_p);
                                return true;
                            }
                        }
                        std::fclose(fp);
                    }
                }
                XFree(p_p);
            }
        }

        if (is_fs && !is_browser) return true;

        if (!is_browser) {
            XWindowAttributes attr;
            if (XGetWindowAttributes(dpy, active, &attr) && attr.map_state == IsViewable && attr.override_redirect) {
                if (attr.width >= screenW && attr.height >= screenH) return true;
            }
        }

        return false;
    }

    std::vector<WindowData> GetWindows(int screenW, int screenH, const char* appTitle, double now) {
        std::vector<WindowData> list;
        if (!dpy) return list;

        if (self_win == 0) {
            FindAndConfigureSelf(appTitle);
        }

        if (self_win != 0) {
            XWindowAttributes sattr;
            if (XGetWindowAttributes(dpy, self_win, &sattr)) {
                int sx = 0, sy = 0;
                Window dummy;
                XTranslateCoordinates(dpy, self_win, root, 0, 0, &sx, &sy, &dummy);
                overlay_x = sx;
                overlay_y = sy;
                overlay_w = sattr.width;
                overlay_h = sattr.height;
            }
        }

        bottom_panel_top = (float)(screenH - overlay_y);
        floating_dock.active = false;

        if (now - last_popup_scan > 0.04) {
            last_popup_scan = now;
            cached_popups.clear();

            Window r_ret, p_ret;
            Window* root_children = NULL;
            unsigned int n_root_children = 0;
            if (XQueryTree(dpy, root, &r_ret, &p_ret, &root_children, &n_root_children) && root_children) {
                for (int i = (int)n_root_children - 1; i >= 0; i--) {
                    Window rw = root_children[i];
                    if (rw == self_win) continue;

                    XWindowAttributes rattr;
                    if (!XGetWindowAttributes(dpy, rw, &rattr) || rattr.map_state != IsViewable) continue;
                    if (!rattr.override_redirect) continue;

                    bool skip_popup = false;
                    if (prop_type != None) {
                        Atom t2; int f2; unsigned long n2 = 0, b2 = 0; unsigned char* p2 = NULL;
                        if (XGetWindowProperty(dpy, rw, prop_type, 0, 16, False, XA_ATOM,
                                               &t2, &f2, &n2, &b2, &p2) == Success && p2) {
                            Atom* atoms = (Atom*)p2;
                            for (unsigned long k = 0; k < n2; k++) {
                                Atom a = atoms[k];
                                if (a == type_tooltip || a == type_notification ||
                                    a == type_splash || a == type_dock || a == type_desktop) {
                                    skip_popup = true;
                                }
                            }
                            XFree(p2);
                        }
                    }
                    if (skip_popup) continue;

                    float px = (float)rattr.x;
                    float py = (float)rattr.y;
                    float pw = (float)rattr.width;
                    float ph = (float)rattr.height;

                    long gtk_ext[4] = {0, 0, 0, 0};
                    if (GetCardinals4(rw, prop_gtk_extents, gtk_ext) &&
                        (gtk_ext[0] > 0 || gtk_ext[1] > 0 || gtk_ext[2] > 0 || gtk_ext[3] > 0)) {
                        px += (float)gtk_ext[0];
                        py += (float)gtk_ext[2];
                        pw -= (float)(gtk_ext[0] + gtk_ext[1]);
                        ph -= (float)(gtk_ext[2] + gtk_ext[3]);
                    }

                    if (pw >= 100.0f && pw <= 1050.0f &&
                        ph >= 80.0f && ph <= (float)screenH - 20.0f &&
                        py >= 6.0f && py <= (float)screenH - 25.0f) {
                        float lx = px - (float)overlay_x;
                        float ly = py - (float)overlay_y;
                        cached_popups.push_back({ (uintptr_t)rw, Rectangle{ lx, ly, pw, ph }, false });
                    }
                }
                XFree(root_children);
            }
        }

        for (const auto& pw : cached_popups) {
            list.push_back(pw);
        }

        Atom target = (prop_stack != None) ? prop_stack : prop_clients;
        if (target == None) return list;

        Atom type; int format; unsigned long nitems = 0, after = 0;
        unsigned char* prop = NULL;
        if (XGetWindowProperty(dpy, root, target, 0, 1024, False, XA_WINDOW,
                               &type, &format, &nitems, &after, &prop) != Success || !prop) {
            return list;
        }

        Window* wins = (Window*)prop;
        for (int i = (int)nitems - 1; i >= 0; i--) {
            Window w = wins[i];
            if (w == self_win) continue;

            bool skip_type = false, is_dock = false, is_menu_like = false;
            if (prop_type != None) {
                Atom t2; int f2; unsigned long n2 = 0, b2 = 0; unsigned char* p2 = NULL;
                if (XGetWindowProperty(dpy, w, prop_type, 0, 16, False, XA_ATOM,
                                       &t2, &f2, &n2, &b2, &p2) == Success && p2) {
                    Atom* atoms = (Atom*)p2;
                    for (unsigned long k = 0; k < n2; k++) {
                        Atom a = atoms[k];
                        if (a == type_dock) { is_dock = true; skip_type = true; }
                        else if (a == type_desktop || a == type_tooltip ||
                                 a == type_notification || a == type_combo || a == type_splash) {
                            skip_type = true;
                        } else if (a == type_menu || a == type_dropdown || a == type_popup) {
                            is_menu_like = true;
                        }
                    }
                    XFree(p2);
                }
            }

            if (is_dock) {
                XWindowAttributes dattr;
                if (XGetWindowAttributes(dpy, w, &dattr) && dattr.map_state == IsViewable) {
                    int dx = 0, dy = 0;
                    Window dummy;
                    XTranslateCoordinates(dpy, w, root, 0, 0, &dx, &dy, &dummy);
                    if (dy >= screenH - 130 && dy <= screenH - 14 && dattr.height <= 120 && dattr.width >= 80) {
                        float local_dx = (float)(dx - overlay_x);
                        float local_dy = (float)(dy - overlay_y);
                        if (dattr.width >= screenW * 0.90) {
                            if (local_dy < bottom_panel_top) bottom_panel_top = local_dy;
                        } else {
                            if (!floating_dock.active || local_dy < floating_dock.rect.y) {
                                floating_dock.active = true;
                                floating_dock.rect = Rectangle{ local_dx, local_dy, (float)dattr.width, (float)dattr.height };
                            }
                        }
                    }
                }
            }
            if (skip_type) continue;

            bool is_minimized = false, is_maximized = false;
            if (prop_state != None) {
                Atom t_st; int f_st; unsigned long n_st = 0, b_st = 0; unsigned char* p_st = NULL;
                if (XGetWindowProperty(dpy, w, prop_state, 0, 32, False, XA_ATOM,
                                       &t_st, &f_st, &n_st, &b_st, &p_st) == Success && p_st) {
                    Atom* atoms = (Atom*)p_st;
                    for (unsigned long k = 0; k < n_st; k++) {
                        if (atoms[k] == state_hidden) is_minimized = true;
                        if (atoms[k] == state_max_v || atoms[k] == state_max_h || atoms[k] == state_fullscreen) {
                            is_maximized = true;
                        }
                    }
                    XFree(p_st);
                }
            }
            if (is_minimized) continue;

            XWindowAttributes attr;
            if (!XGetWindowAttributes(dpy, w, &attr) || attr.map_state != IsViewable) continue;

            int abs_x = 0, abs_y = 0;
            Window dummy;
            XTranslateCoordinates(dpy, w, root, 0, 0, &abs_x, &abs_y, &dummy);

            float vis_x = (float)abs_x, vis_y = (float)abs_y;
            float vis_w = (float)attr.width, vis_h = (float)attr.height;

            long gtk_ext[4] = {0, 0, 0, 0}, net_ext[4] = {0, 0, 0, 0};
            if (GetCardinals4(w, prop_gtk_extents, gtk_ext) &&
                (gtk_ext[0] > 0 || gtk_ext[1] > 0 || gtk_ext[2] > 0 || gtk_ext[3] > 0)) {
                vis_x += (float)gtk_ext[0];
                vis_y += (float)gtk_ext[2];
                vis_w -= (float)(gtk_ext[0] + gtk_ext[1]);
                vis_h -= (float)(gtk_ext[2] + gtk_ext[3]);
            } else if (GetCardinals4(w, prop_net_extents, net_ext) &&
                       (net_ext[0] > 0 || net_ext[1] > 0 || net_ext[2] > 0 || net_ext[3] > 0)) {
                vis_x -= (float)net_ext[0];
                vis_y -= (float)net_ext[2];
                vis_w += (float)(net_ext[0] + net_ext[1]);
                vis_h += (float)(net_ext[2] + net_ext[3]);
            } else {
                Window r_w, parent_w;
                Window* ch_w = NULL;
                unsigned int n_ch = 0;
                if (XQueryTree(dpy, w, &r_w, &parent_w, &ch_w, &n_ch)) {
                    if (ch_w) XFree(ch_w);
                    if (parent_w != 0 && parent_w != root) {
                        XWindowAttributes pattr;
                        if (XGetWindowAttributes(dpy, parent_w, &pattr)) {
                            int px = 0, py = 0;
                            XTranslateCoordinates(dpy, parent_w, root, 0, 0, &px, &py, &dummy);
                            vis_x = (float)px;
                            vis_y = (float)py;
                            vis_w = (float)pattr.width;
                            vis_h = (float)pattr.height;
                        }
                    }
                }
            }

            if (is_menu_like && (vis_w < 100.0f || vis_h < 80.0f)) continue;
            if (!is_menu_like && (vis_w < 140.0f || vis_h < 80.0f)) continue;

            float local_x = vis_x - (float)overlay_x;
            float local_y = vis_y - (float)overlay_y;

            if (!is_menu_like && vis_w >= (float)screenW - 20.0f) {
                is_maximized = true;
            }

            list.push_back({ (uintptr_t)w, Rectangle{ local_x, local_y, vis_w, vis_h }, is_maximized });
        }

        XFree(prop);
        return list;
    }
};
#endif

#if defined(_WIN32)
typedef HRESULT (WINAPI *PFN_DwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD);
static PFN_DwmGetWindowAttribute pDwmGetWindowAttribute = nullptr;
static HWND g_selfHwnd = NULL;
static bool g_winPrevF8 = false, g_winPrevF9 = false, g_winPrevF10 = false;

void PollWindowsGlobalHotkeys(bool& f8_pressed, bool& f9_pressed, bool& f10_pressed, bool& ctrl_shift_held) {
    bool f8 = (GetAsyncKeyState(VK_F8) & 0x8000) != 0;
    bool f9 = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    bool f10 = (GetAsyncKeyState(VK_F10) & 0x8000) != 0;

    f8_pressed = (f8 && !g_winPrevF8);
    f9_pressed = (f9 && !g_winPrevF9);
    f10_pressed = (f10 && !g_winPrevF10);

    g_winPrevF8 = f8;
    g_winPrevF9 = f9;
    g_winPrevF10 = f10;

    ctrl_shift_held = ((GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0) &&
                      ((GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0);
}

bool IsWindowsGameRunning(int screenW, int screenH) {
    QUERY_USER_NOTIFICATION_STATE qstate;
    if (SUCCEEDED(SHQueryUserNotificationState(&qstate))) {
        int st = (int)qstate;
        // 2 = QUNS_BUSY, 3 = QUNS_RUNNING_D3D_FULL_SCREEN, 4 = QUNS_PRESENTATION_MODE
        if (st == 2 || st == 3 || st == 4) {
            return true;
        }
    }

    HWND fg = GetForegroundWindow();
    if (fg && fg != g_selfHwnd) {
        char cls[64];
        GetClassNameA(fg, cls, sizeof(cls));
        if (std::strcmp(cls, "Progman") != 0 && std::strcmp(cls, "WorkerW") != 0 &&
            std::strcmp(cls, "Shell_TrayWnd") != 0 && std::strcmp(cls, "Windows.UI.Core.CoreWindow") != 0) {
            LONG style = GetWindowLong(fg, GWL_STYLE);
            RECT r;
            GetWindowRect(fg, &r);
            if ((style & WS_CAPTION) == 0 && (r.right - r.left) >= screenW && (r.bottom - r.top) >= screenH) {
                return true;
            }
        }
    }
    return false;
}

float GetWindowsWorkAreaBottom(int screenHeight) {
    RECT workArea;
    if (SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0)) {
        if (workArea.bottom > screenHeight - 130 && workArea.bottom <= screenHeight) {
            return (float)workArea.bottom;
        }
    }
    return (float)screenHeight;
}

DockInfo GetWindowsAutoHideTaskbar(int screenW, int screenH, float base_ground_y) {
    DockInfo di = { false, {} };
    if (base_ground_y < (float)screenH - 4.0f) return di;

    HWND hTray = FindWindowA("Shell_TrayWnd", NULL);
    if (hTray && IsWindowVisible(hTray)) {
        RECT tr;
        if (GetWindowRect(hTray, &tr)) {
            int th = tr.bottom - tr.top;
            int tw = tr.right - tr.left;
            if (tr.top <= screenH - 14 && tr.top >= screenH - 130 && th <= 120 && tw >= 100) {
                di.active = true;
                di.rect = Rectangle{ (float)tr.left, (float)tr.top, (float)tw, (float)th };
            }
        }
    }
    return di;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    if (hwnd == g_selfHwnd) return TRUE;
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    // DWMWA_CLOAKED (14): скрытое Меню «Пуск», Поиск и UWP-окна в Win 10/11 имеют cloaked != 0
    int cloaked = 0;
    if (pDwmGetWindowAttribute && SUCCEEDED(pDwmGetWindowAttribute(hwnd, 14, &cloaked, sizeof(cloaked)))) {
        if (cloaked != 0) return TRUE;
    }

    char cls[80];
    GetClassNameA(hwnd, cls, sizeof(cls));

    if (std::strcmp(cls, "Progman") == 0 ||
        std::strcmp(cls, "WorkerW") == 0 ||
        std::strcmp(cls, "Shell_TrayWnd") == 0 ||
        std::strcmp(cls, "Shell_SecondaryTrayWnd") == 0) {
        return TRUE;
    }

    LONG style = GetWindowLong(hwnd, GWL_STYLE);
    LONG ex = GetWindowLong(hwnd, GWL_EXSTYLE);

    // 1. Меню «Пуск», Поиск и Быстрые настройки в Windows 10 и Windows 11
    bool isStartOrSystemFlyout = (std::strcmp(cls, "Windows.UI.Core.CoreWindow") == 0 ||
                                  std::strcmp(cls, "XamlExplorerHostIslandWindow") == 0 ||
                                  std::strcmp(cls, "ControlCenterWindow") == 0);

    // 2. Все виды контекстных меню ПКМ в Windows 10 и Windows 11 (Explorer, Рабочий стол, Win11 XAML, браузеры)
    bool isContextMenu = (std::strcmp(cls, "#32768") == 0 ||
                          std::strcmp(cls, "MozillaDropShadowWindowClass") == 0 ||
                          std::strcmp(cls, "Xaml_WindowedPopupClass") == 0 ||
                          std::strcmp(cls, "Microsoft.UI.Content.PopupWindowSiteBridge") == 0 ||
                          std::strcmp(cls, "PopupHost") == 0 ||
                          ((style & WS_POPUP) != 0 && (std::strstr(cls, "Chrome_WidgetWin") != nullptr || (ex & WS_EX_TOPMOST) != 0)));

    RECT r;
    if (isStartOrSystemFlyout || isContextMenu) {
        GetWindowRect(hwnd, &r);
    } else if (!pDwmGetWindowAttribute || FAILED(pDwmGetWindowAttribute(hwnd, 9 /* DWMWA_EXTENDED_FRAME_BOUNDS */, &r, sizeof(r)))) {
        GetWindowRect(hwnd, &r);
    }

    int width = r.right - r.left;
    int height = r.bottom - r.top;
    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    if (r.right <= 10 || r.left >= sw - 10) return TRUE;

    if (isContextMenu) {
        if (width < 95 || width > 950 || height < 70 || height > sh - 30 || r.top < 4) return TRUE;
    } else if (isStartOrSystemFlyout) {
        // Отсекаем невидимый полноэкранный буфер TextInputHost (sw x sh), пропускаем открытый Пуск и Поиск
        if (width >= sw - 40 || height >= sh - 25 || width < 220 || height < 220 || r.top < 12) return TRUE;
    } else {
        if (width < 140 || height < 80) return TRUE;
        if (ex & WS_EX_TOOLWINDOW) return TRUE;
    }

    bool is_max = (!isContextMenu && !isStartOrSystemFlyout) && (IsZoomed(hwnd) || (width >= sw - 20));
    auto* list = reinterpret_cast<std::vector<WindowData>*>(lParam);
    list->push_back({ (uintptr_t)hwnd, Rectangle{ (float)r.left, (float)r.top, (float)width, (float)height }, is_max });
    return TRUE;
}
#endif

// ============================================================================
// ФИЗИКА ВОЛНИСТЫХ СУГРОБОВ, КРАТЕРОВ, СОСУЛЕК, СНЕГОВИКОВ (10 СКИНОВ) И 3D-МАШИНЫ
// ============================================================================

inline float EvalWaveAt(float x, unsigned int seed) {
    float p1 = (float)(seed % 100) * 0.17f;
    float p2 = (float)((seed / 7) % 100) * 0.23f;
    float p3 = (float)((seed / 31) % 100) * 0.31f;
    float w = 0.92f
            + std::sin(x * 0.026f + p1) * 0.36f
            + std::cos(x * 0.064f + p2) * 0.24f
            + std::sin(x * 0.125f + p3) * 0.11f;
    return std::clamp(w, 0.30f, 1.55f);
}

void InitWaveShape(std::vector<float>& shape, float step, unsigned int seed) {
    int n = (int)shape.size();
    for (int i = 0; i < n; i++) {
        shape[i] = EvalWaveAt((float)i * step, seed);
    }
}

void FillSculptedFromWave(std::vector<float>& h, const std::vector<float>& wave_shape, float base_h) {
    int n = (int)std::min(h.size(), wave_shape.size());
    for (int i = 0; i < n; i++) {
        h[i] = std::max(2.5f, base_h * wave_shape[i]);
    }
}

inline float GetEdgeTaper(float local_x, float window_width, bool taper_edges) {
    if (!taper_edges) return 1.0f;
    float margin = 8.0f;
    float dist_edge = std::min(local_x - margin, (window_width - margin) - local_x);
    if (dist_edge <= 0.0f) return 0.0f;
    float t = std::clamp(dist_edge / 26.0f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

float GetDriftHeightNear(const std::vector<float>& h_arr, float step, float local_x, float window_width, bool taper_edges) {
    int n = (int)h_arr.size();
    if (n == 0) return 0.0f;
    int center = std::clamp((int)std::round(local_x / step), 0, n - 1);
    float best = 0.0f;
    for (int d = -3; d <= 3; d++) {
        int idx = center + d;
        if (idx >= 0 && idx < n) {
            float lx = (float)idx * step;
            float h = h_arr[idx] * GetEdgeTaper(lx, window_width, taper_edges);
            if (h > best) best = h;
        }
    }
    return best;
}

void DepositSnowAt(std::vector<float>& h, const std::vector<float>& wave_shape,
                   float step, float local_x, float window_width, float amount, float max_h) {
    int n = (int)h.size();
    if (n < 4) return;
    int max_idx = std::min(n - 1, (int)(window_width / step) + 1);
    int center = (int)std::round(local_x / step);
    const int radius = 8;

    for (int d = -radius; d <= radius; d++) {
        int idx = center + d;
        if (idx >= 0 && idx <= max_idx) {
            float u = 1.0f - std::abs((float)d) / (float)(radius + 1);
            float kernel = u * u * (3.0f - 2.0f * u);
            float mult = (idx < (int)wave_shape.size()) ? wave_shape[idx] : 1.0f;
            float fast_start = (h[idx] < 6.5f) ? 1.65f : 1.0f;
            h[idx] = std::min(max_h * mult, std::max(0.0f, h[idx] + amount * fast_start * kernel * mult));
        }
    }
}

void ScoopSnowAt(std::vector<float>& h, float step, float local_x, float radius_px, float depth) {
    int n = (int)h.size();
    int center = (int)std::round(local_x / step);
    int rad_bins = (int)std::ceil(radius_px / step);
    for (int d = -rad_bins; d <= rad_bins; d++) {
        int idx = center + d;
        if (idx >= 0 && idx < n) {
            float u = 1.0f - std::abs((float)d) / (float)(rad_bins + 1);
            if (u > 0.0f) {
                h[idx] = std::max(0.0f, h[idx] - depth * u * u);
            }
        }
    }
}

// Формирование кратера в сугробе с углублением в центре и выброшенными бортиками по краям
void CarveSnowCrater(std::vector<float>& h, float step, float local_x, float window_width, float impact_power, float max_h) {
    int n = (int)h.size();
    if (n < 6) return;
    int max_idx = std::min(n - 1, (int)(window_width / step) + 1);
    int center = (int)std::round(local_x / step);

    float pit_depth = std::clamp(5.5f + impact_power * 6.5f, 5.5f, 13.5f);
    float rim_boost = pit_depth * 0.42f;
    const int pit_bins = 4; // Радиус воронки ~16px
    const int rim_bins = 8; // Бортики по бокам на расстоянии 20..32px

    for (int d = -rim_bins; d <= rim_bins; d++) {
        int idx = center + d;
        if (idx < 0 || idx > max_idx) continue;

        int ad = std::abs(d);
        if (ad <= pit_bins) {
            float u = 1.0f - (float)ad / (float)(pit_bins + 1);
            float bowl = u * u * (3.0f - 2.0f * u);
            h[idx] = std::max(0.4f, h[idx] - pit_depth * bowl);
        } else {
            float t = 1.0f - std::abs((float)(ad - 6)) / 3.0f;
            if (t > 0.0f) {
                h[idx] = std::min(max_h, h[idx] + rim_boost * t * t);
            }
        }
    }
}

bool IsPointOccludedByFrontWindows(float px, float py, const std::vector<WindowData>& occluders, size_t max_occluder_idx) {
    for (size_t fi = 0; fi < max_occluder_idx; fi++) {
        const auto& fw = occluders[fi].rect;
        float fw_top = std::max(0.0f, fw.y - 10.0f);
        if (px >= fw.x && px <= (fw.x + fw.width) &&
            py >= fw_top && py <= (fw.y + fw.height)) {
            return true;
        }
    }
    return false;
}

void PopulateWindowIcicles(std::vector<Icicle>& icicles, float start_x, float end_x, std::mt19937& rng, bool initial_small) {
    std::uniform_real_distribution<float> d01(0.0f, 1.0f);
    float x = start_x + 12.0f + d01(rng) * 14.0f;
    while (x <= end_x - 12.0f) {
        Icicle ic{};
        ic.rel_x = x;
        ic.max_length = 14.0f + d01(rng) * 22.0f;
        ic.length = initial_small ? (0.5f + d01(rng) * 2.5f) : (4.0f + d01(rng) * 7.0f);
        ic.base_width = 2.0f + d01(rng) * 1.8f;
        ic.grow_rate = 1.4f + d01(rng) * 1.8f;
        icicles.push_back(ic);
        x += 16.0f + d01(rng) * 24.0f;
    }
}

void DrawWindowIcicles(const std::vector<Icicle>& icicles, const Rectangle& win_rect, float ground_y,
                       const std::vector<WindowData>& occluders, size_t max_occluder_idx) {
    float top_y = win_rect.y + win_rect.height;
    if (top_y >= ground_y - 6.0f) return;

    Color iceBody = Color{ 206, 234, 254, 215 };
    Color iceShade = Color{ 148, 192, 232, 225 };
    Color iceGlint = Color{ 255, 255, 255, 240 };
    Color iceOutline = Color{ 165, 205, 240, 175 };

    for (const auto& ic : icicles) {
        if (ic.length < 1.0f) continue;
        if (ic.rel_x < 8.0f || ic.rel_x > win_rect.width - 8.0f) continue;

        float cx = win_rect.x + ic.rel_x;
        float max_len = std::min(ic.length, (ground_y - 2.0f) - top_y);
        if (max_len < 1.0f) continue;

        if (IsPointOccludedByFrontWindows(cx, top_y + 2.0f, occluders, max_occluder_idx)) {
            continue;
        }

        float w_scale = std::clamp(max_len / 12.0f, 0.35f, 1.0f);
        float bw = ic.base_width * w_scale;

        Vector2 bl = { cx - bw, top_y };
        Vector2 br = { cx + bw, top_y };
        Vector2 mid = { cx, top_y };
        Vector2 tip = { cx, top_y + max_len };

        DrawCircleV({ cx, top_y + 0.5f }, bw * 0.95f, iceBody);
        DrawTriCCW(bl, tip, mid, iceBody);
        DrawTriCCW(mid, tip, br, iceShade);

        DrawLineEx({ cx - bw * 0.45f, top_y + 1.0f }, { cx - bw * 0.12f, top_y + max_len * 0.78f }, 1.2f, iceGlint);
        DrawLineEx(bl, tip, 0.9f, iceOutline);
        DrawLineEx(br, tip, 0.9f, iceOutline);
    }
}

void DrawFallingIcicle(const FallingIcicle& fi) {
    float rad = fi.angle * 0.0174533f;
    float cs = std::cos(rad);
    float sn = std::sin(rad);

    auto rot = [&](float lx, float ly) -> Vector2 {
        return Vector2{ fi.pos.x + lx * cs - ly * sn, fi.pos.y + lx * sn + ly * cs };
    };

    float half_l = fi.length * 0.5f;
    Vector2 bl = rot(-fi.base_width, -half_l);
    Vector2 br = rot( fi.base_width, -half_l);
    Vector2 mid = rot(0.0f, -half_l);
    Vector2 tip = rot(0.0f, half_l);

    DrawTriCCW(bl, tip, mid, Color{ 215, 238, 255, 230 });
    DrawTriCCW(mid, tip, br, Color{ 155, 198, 238, 235 });
    DrawLineEx(bl, tip, 1.0f, WHITE);
}

void DrawSculptedDrift(const std::vector<float>& h_arr, float step, float start_x, float base_y, float width,
                       const std::vector<WindowData>& occluders, size_t max_occluder_idx, bool is_bottom) {
    int max_bins = std::min((int)h_arr.size(), (int)(width / step) + 1);
    if (max_bins < 2) return;

    bool taper_edges = !is_bottom;
    float max_visible_h = is_bottom ? 46.0f : std::max(0.0f, base_y - 1.0f);
    if (max_visible_h < 0.4f) return;

    for (int i = 0; i < max_bins - 1; i++) {
        float lx0 = (float)i * step;
        float lx1 = lx0 + step;

        float h0 = std::min(max_visible_h, h_arr[i] * GetEdgeTaper(lx0, width, taper_edges));
        float h1 = std::min(max_visible_h, h_arr[i + 1] * GetEdgeTaper(lx1, width, taper_edges));
        float avg_h = (h0 + h1) * 0.5f;
        if (avg_h < 0.35f) continue;

        float x0 = start_x + lx0;
        float x1 = start_x + lx1;
        float mid_x = (x0 + x1) * 0.5f;

        if (!is_bottom && IsPointOccludedByFrontWindows(mid_x, base_y, occluders, max_occluder_idx)) {
            continue;
        }

        float line_fade = std::clamp((avg_h - 0.35f) / 2.65f, 0.0f, 1.0f);
        Color topWhite = Color{ 255, 255, 255, (unsigned char)(255.0f * std::clamp(avg_h / 1.5f, 0.0f, 1.0f)) };
        Color bodyIce = Color{ 222, 234, 248, (unsigned char)(250.0f * std::clamp(avg_h / 1.5f, 0.0f, 1.0f)) };
        Color edgeLine = Color{ 182, 202, 228, (unsigned char)(210.0f * line_fade) };
        Color baseLine = Color{ 165, 188, 218, (unsigned char)(185.0f * line_fade) };

        float y0_top = base_y - h0;
        float y1_top = base_y - h1;
        float y_bot = base_y;

        Vector2 tl = { x0, y0_top };
        Vector2 bl = { x0, y_bot };
        Vector2 br = { x1, y_bot };
        Vector2 tr = { x1, y1_top };

        DrawQuadCCW(tl, bl, br, tr, bodyIce);

        Vector2 tl_mid = { x0, y0_top + h0 * 0.45f };
        Vector2 tr_mid = { x1, y1_top + h1 * 0.45f };
        DrawQuadCCW(tl, tl_mid, tr_mid, tr, topWhite);

        if (line_fade > 0.05f) {
            DrawLineEx(tl, tr, 1.8f, topWhite);
            DrawLineEx({ x0, y0_top - 0.7f }, { x1, y1_top - 0.7f }, 1.1f, edgeLine);
            DrawLineEx({ x0, base_y - 0.5f }, { x1, base_y - 0.5f }, 1.1f, baseLine);
        }
    }
}

void DrawSnowballUnit(Vector2 center, float radius, bool highlight_border) {
    Color outerBorder = highlight_border ? Color{ 110, 175, 245, 255 } : Color{ 168, 192, 220, 245 };
    Color snowShadow = Color{ 212, 228, 246, 255 };
    DrawCircleV(center, radius + (highlight_border ? 1.8f : 1.2f), outerBorder);
    DrawCircleV(center, radius, WHITE);
    DrawCircleSector(center, radius * 0.92f, 20.0f, 160.0f, 16, snowShadow);
    DrawCircleV({ center.x - radius * 0.24f, center.y - radius * 0.24f }, radius * 0.64f, WHITE);
}

// Отрисовка снеговика с 10 различными скинами
void DrawSnowman(const Snowman& sm, Vector2 base_pos, float time, bool is_snap_target) {
    float scale = 0.4f + 0.6f * std::min(1.0f, sm.anim_pop);
    float r1 = sm.r1 * scale;
    float r2 = sm.r2 * scale;
    float r3 = sm.r3 * scale;

    float rad = sm.fall_angle * 0.0174533f;
    float cs = std::cos(rad);
    float sn = std::sin(rad);
    auto rotPt = [&](float lx, float ly) -> Vector2 {
        return Vector2{ base_pos.x + lx * cs - ly * sn, base_pos.y + lx * sn + ly * cs };
    };

    float y1 = -r1 * 0.82f;
    float y2 = y1 - r1 * 0.80f - r2 * 0.75f;
    float y3 = y2 - r2 * 0.80f - r3 * 0.75f;

    Vector2 c1 = rotPt(0.0f, y1);
    Vector2 c2 = rotPt(0.0f, y2);
    Vector2 c3 = rotPt(0.0f, y3);

    int skin = sm.skin % SNOWMAN_SKIN_COUNT;
    float dir = (float)sm.facing;

    Color coalColor = Color{ 36, 40, 48, 255 };
    Color carrotColor = Color{ 245, 115, 25, 255 };
    Color twigColor = Color{ 102, 68, 42, 255 };
    Color goldColor = Color{ 245, 198, 45, 255 };
    Color goldDark = Color{ 195, 145, 18, 255 };

    // 1. Нижний шар
    if (sm.stage >= 1) {
        DrawSnowballUnit(c1, r1, is_snap_target && sm.stage == 1);
    }

    // 2. Второй шар + руки и аксессуары туловища по скину
    if (sm.stage >= 2) {
        if (skin == 7) {
            // Скин 7 (Робот): металлические манипуляторы
            Color steel = Color{ 125, 136, 152, 255 };
            DrawLineEx(rotPt(-r2 * 0.85f, y2), rotPt(-r2 * 1.75f, y2 - r2 * 0.5f), 2.8f * scale, steel);
            DrawCircleV(rotPt(-r2 * 1.75f, y2 - r2 * 0.5f), 2.5f * scale, goldColor);
            DrawLineEx(rotPt(r2 * 0.85f, y2), rotPt(r2 * 1.75f, y2 - r2 * 0.5f), 2.8f * scale, steel);
            DrawCircleV(rotPt(r2 * 1.75f, y2 - r2 * 0.5f), 2.5f * scale, goldColor);
        } else {
            // Классические ветки
            DrawLineEx(rotPt(-r2 * 0.85f, y2 - 2.0f), rotPt(-r2 * 1.75f, y2 - r2 * 0.75f), 2.2f * scale, twigColor);
            DrawLineEx(rotPt(-r2 * 1.45f, y2 - r2 * 0.52f), rotPt(-r2 * 1.65f, y2 - r2 * 0.95f), 1.6f * scale, twigColor);
            DrawLineEx(rotPt(r2 * 0.85f, y2 - 2.0f), rotPt(r2 * 1.75f, y2 - r2 * 0.75f), 2.2f * scale, twigColor);
            DrawLineEx(rotPt(r2 * 1.45f, y2 - r2 * 0.52f), rotPt(r2 * 1.65f, y2 - r2 * 0.95f), 1.6f * scale, twigColor);
        }

        // Варежки для зимнего скина (8)
        if (skin == 8) {
            DrawCircleV(rotPt(-r2 * 1.78f, y2 - r2 * 0.78f), 3.3f * scale, Color{ 215, 45, 55, 255 });
            DrawCircleV(rotPt(r2 * 1.78f, y2 - r2 * 0.78f), 3.3f * scale, Color{ 215, 45, 55, 255 });
        }

        DrawSnowballUnit(c2, r2, is_snap_target && sm.stage == 2);

        // Детали туловища для каждого скина
        if (skin == 2) {
            // Санта: красный кафтан-полоса, чёрный пояс и золотая пряжка
            DrawLineEx(rotPt(-r2 * 0.92f, y2 + 1.0f), rotPt(r2 * 0.92f, y2 + 1.0f), 4.0f * scale, Color{ 32, 34, 40, 255 });
            DrawRectangle(int(c2.x - 3.2f * scale), int(c2.y - 2.0f * scale), int(6.4f * scale), int(5.5f * scale), goldColor);
        } else if (skin == 3) {
            // Викинг: круглый деревянный щит с металлической окантовкой на руке
            Vector2 sh = rotPt(-dir * r2 * 1.25f, y2 + 1.0f);
            DrawCircleV(sh, 7.5f * scale, Color{ 130, 140, 155, 255 });
            DrawCircleV(sh, 6.0f * scale, Color{ 148, 92, 48, 255 });
            DrawCircleV(sh, 2.2f * scale, Color{ 195, 205, 218, 255 });
        } else if (skin == 6) {
            // Ковбой: золотая звезда шерифа на груди
            Vector2 st = rotPt(dir * 2.5f * scale, y2 - 2.5f * scale);
            DrawCircleV(st, 3.2f * scale, goldColor);
            DrawCircleV(st, 1.5f * scale, goldDark);
        } else if (skin == 7) {
            // Робот: железная панель с индикаторами
            DrawRectangle(int(c2.x - 5.0f * scale), int(c2.y - 4.5f * scale), int(10.0f * scale), int(9.0f * scale), Color{ 135, 148, 165, 255 });
            DrawCircleV({ c2.x - 2.2f * scale, c2.y - 1.5f * scale }, 1.3f * scale, Color{ 235, 45, 45, 255 });
            DrawCircleV({ c2.x + 2.2f * scale, c2.y - 1.5f * scale }, 1.3f * scale, Color{ 45, 225, 95, 255 });
        } else if (skin == 9) {
            // Король: золотой скипетр с рубином в руке
            Vector2 sp_b = rotPt(dir * r2 * 1.55f, y2 + 7.0f * scale);
            Vector2 sp_t = rotPt(dir * r2 * 1.65f, y2 - 11.0f * scale);
            DrawLineEx(sp_b, sp_t, 2.2f * scale, goldColor);
            DrawCircleV(sp_t, 3.2f * scale, goldColor);
            DrawCircleV(sp_t, 1.6f * scale, Color{ 225, 30, 45, 255 });
        } else {
            // Пуговицы-угольки
            DrawCircleV(rotPt(0.0f, y2 - r2 * 0.25f), 1.9f * scale, coalColor);
            DrawCircleV(rotPt(0.0f, y2 + r2 * 0.25f), 1.9f * scale, coalColor);
        }
    }

    // 3. Третий шар (голова) + уникальный головной убор, лицо и шарф для каждого из 10 скинов!
    if (sm.stage >= 3) {
        // Рукоять катаны за спиной для скина Ниндзя (4)
        if (skin == 4) {
            DrawLineEx(rotPt(-dir * 3.0f, y2 + 2.0f), rotPt(-dir * 12.0f, y3 - 7.0f * scale), 2.6f * scale, Color{ 30, 32, 38, 255 });
            DrawLineEx(rotPt(-dir * 9.0f, y3 - 3.0f * scale), rotPt(-dir * 12.5f, y3 - 7.5f * scale), 2.8f * scale, Color{ 210, 45, 55, 255 });
        }

        DrawSnowballUnit(c3, r3, false);

        float neck_y = y3 + r3 * 0.82f;
        float flutter = std::sin(time * 4.5f + sm.rel_x * 0.1f) * 2.5f;

        // Шарфы и воротники по скинам
        if (skin == 0 || skin == 8) {
            Color scMain = (skin == 0) ? Color{ 220, 42, 48, 255 } : Color{ 38, 118, 215, 255 };
            Color scDark = (skin == 0) ? Color{ 175, 25, 32, 255 } : Color{ 25, 82, 165, 255 };
            DrawLineEx(rotPt(-r3 * 0.88f, neck_y), rotPt(r3 * 0.88f, neck_y), 4.8f * scale, scMain);
            DrawLineEx(rotPt(-dir * r3 * 0.45f, neck_y), rotPt(-dir * r3 * 0.85f + flutter, neck_y + 11.0f * scale), 4.0f * scale, scDark);
            DrawLineEx(rotPt(-dir * r3 * 0.35f, neck_y), rotPt(-dir * r3 * 0.75f + flutter, neck_y + 10.5f * scale), 3.2f * scale, scMain);
        } else if (skin == 1) {
            // Джентльмен: чёрная бабочка-галстук
            Vector2 bow = rotPt(dir * 1.5f, neck_y);
            DrawTriCCW({ bow.x - 5.0f * scale, bow.y - 3.0f * scale }, { bow.x - 5.0f * scale, bow.y + 3.0f * scale }, bow, coalColor);
            DrawTriCCW({ bow.x + 5.0f * scale, bow.y - 3.0f * scale }, { bow.x + 5.0f * scale, bow.y + 3.0f * scale }, bow, coalColor);
            DrawCircleV(bow, 1.6f * scale, coalColor);
        } else if (skin == 2) {
            // Санта: пушистая белая борода
            DrawCircleV(rotPt(dir * 2.0f, y3 + r3 * 0.55f), 5.5f * scale, WHITE);
            DrawCircleV(rotPt(dir * 1.5f - 3.0f * scale, y3 + r3 * 0.45f), 3.6f * scale, WHITE);
            DrawCircleV(rotPt(dir * 1.5f + 3.0f * scale, y3 + r3 * 0.45f), 3.6f * scale, WHITE);
        } else if (skin == 3) {
            // Викинг: рыжие пышные усы
            Color ginger = Color{ 215, 105, 32, 255 };
            DrawLineEx(rotPt(dir * 1.5f, y3 + 2.2f * scale), rotPt(dir * 1.5f - 6.0f * scale, y3 + 5.5f * scale), 3.0f * scale, ginger);
            DrawLineEx(rotPt(dir * 1.5f, y3 + 2.2f * scale), rotPt(dir * 1.5f + 6.0f * scale, y3 + 5.5f * scale), 3.0f * scale, ginger);
        } else if (skin == 5) {
            // Пират: красная бандана на шее
            DrawLineEx(rotPt(-r3 * 0.85f, neck_y), rotPt(r3 * 0.85f, neck_y), 4.2f * scale, Color{ 205, 32, 38, 255 });
        } else if (skin == 6) {
            // Ковбой: красный шейный платок треугольником
            Vector2 p_l = rotPt(-r3 * 0.85f, neck_y - 1.0f);
            Vector2 p_r = rotPt( r3 * 0.85f, neck_y - 1.0f);
            Vector2 p_b = rotPt(dir * 1.5f, neck_y + 7.5f * scale);
            DrawTriCCW(p_l, p_b, p_r, Color{ 218, 42, 42, 255 });
        } else if (skin == 9) {
            // Король: пурпурная мантия с белым горностаевым воротником
            DrawLineEx(rotPt(-r3 * 0.95f, neck_y + 1.0f), rotPt(r3 * 0.95f, neck_y + 1.0f), 5.6f * scale, Color{ 125, 28, 155, 255 });
            DrawLineEx(rotPt(-r3 * 0.90f, neck_y - 1.2f), rotPt(r3 * 0.90f, neck_y - 1.2f), 3.2f * scale, WHITE);
        }

        // Глаза, маска и нос по скину
        if (skin == 4) {
            // Ниндзя: чёрная маска на лице и прищуренные глаза
            DrawCircleSector(c3, r3 * 0.96f, 10.0f, 170.0f, 14, Color{ 28, 30, 36, 255 });
            DrawLineEx(rotPt(-r3 * 0.45f + dir * 1.2f, y3 - r3 * 0.15f), rotPt(-r3 * 0.12f + dir * 1.2f, y3 - r3 * 0.08f), 1.8f * scale, WHITE);
            DrawLineEx(rotPt( r3 * 0.12f + dir * 1.2f, y3 - r3 * 0.08f), rotPt( r3 * 0.45f + dir * 1.2f, y3 - r3 * 0.15f), 1.8f * scale, WHITE);
        } else if (skin == 7) {
            // Робот: светящийся красный LED-визор
            DrawRectangle(int(c3.x - r3 * 0.65f + dir * 1.2f), int(c3.y - r3 * 0.30f), int(r3 * 1.3f), int(4.2f * scale), Color{ 35, 38, 44, 255 });
            DrawCircleV(rotPt(dir * 2.5f, y3 - r3 * 0.18f), 2.2f * scale, Color{ 255, 45, 45, 255 });
        } else {
            Vector2 eye_l = rotPt(-r3 * 0.28f + dir * 1.2f, y3 - r3 * 0.22f);
            Vector2 eye_r = rotPt( r3 * 0.32f + dir * 1.2f, y3 - r3 * 0.22f);
            DrawCircleV(eye_l, 1.6f * scale, coalColor);
            DrawCircleV(eye_r, 1.6f * scale, coalColor);

            // Монокль для Джентльмена (1)
            if (skin == 1) {
                Vector2 m_eye = (dir > 0) ? eye_r : eye_l;
                DrawCircleLines((int)m_eye.x, (int)m_eye.y, 3.0f * scale, goldColor);
                DrawLineEx(m_eye, rotPt(dir * r3 * 0.75f, neck_y), 0.9f, goldColor);
            }
            // Повязка на глаз для Пирата (5)
            if (skin == 5) {
                Vector2 p_eye = (dir > 0) ? eye_l : eye_r;
                DrawCircleV(p_eye, 2.6f * scale, coalColor);
                DrawLineEx(rotPt(-r3 * 0.85f, y3 - r3 * 0.45f), rotPt(r3 * 0.85f, y3 - r3 * 0.10f), 1.2f * scale, coalColor);
            }
            // Румянец для Санты (2) и Зимнего (8)
            if (skin == 2 || skin == 8) {
                DrawCircleV(rotPt(-r3 * 0.48f + dir * 1.0f, y3 + 0.5f), 2.0f * scale, Color{ 255, 130, 145, 175 });
                DrawCircleV(rotPt( r3 * 0.48f + dir * 1.0f, y3 + 0.5f), 2.0f * scale, Color{ 255, 130, 145, 175 });
            }

            // Морковка (кроме Санты с круглым носом)
            if (skin == 2) {
                DrawCircleV(rotPt(dir * 2.2f, y3 + 0.5f), 2.2f * scale, Color{ 235, 75, 75, 255 });
            } else {
                Vector2 nose_base_top = rotPt(dir * 1.0f, y3 - 1.5f * scale);
                Vector2 nose_base_bot = rotPt(dir * 1.0f, y3 + 2.5f * scale);
                Vector2 nose_tip = rotPt(dir * (r3 * 1.35f), y3 + 0.8f * scale);
                DrawTriCCW(nose_base_top, nose_base_bot, nose_tip, carrotColor);
            }

            // Улыбка (если не закрыта бородой или усами)
            if (skin != 2 && skin != 3) {
                for (int s = -2; s <= 2; s++) {
                    Vector2 sp = rotPt((float)s * 2.1f * scale + dir * 1.0f, y3 + r3 * 0.38f - std::abs((float)s) * 0.45f * scale);
                    DrawCircleV(sp, 1.0f * scale, coalColor);
                }
            }
        }

        // Головные уборы для каждого из 10 скинов!
        float hat_y = y3 - r3 * 0.82f;
        if (skin == 1) {
            // 1. Цилиндр Джентльмена
            DrawLineEx(rotPt(-r3 * 1.15f, hat_y), rotPt(r3 * 1.15f, hat_y), 3.0f * scale, coalColor);
            DrawQuadCCW(rotPt(-r3 * 0.72f, hat_y - 10.5f * scale), rotPt(-r3 * 0.72f, hat_y),
                        rotPt( r3 * 0.72f, hat_y), rotPt( r3 * 0.72f, hat_y - 10.5f * scale), coalColor);
            DrawLineEx(rotPt(-r3 * 0.72f, hat_y - 2.2f * scale), rotPt(r3 * 0.72f, hat_y - 2.2f * scale), 2.2f * scale, Color{ 215, 42, 48, 255 });
        } else if (skin == 2) {
            // 2. Колпак Санты
            DrawTriCCW(rotPt(-r3 * 0.90f, hat_y), rotPt(r3 * 0.90f, hat_y), rotPt(-dir * r3 * 0.65f, hat_y - 12.0f * scale), Color{ 222, 38, 44, 255 });
            DrawLineEx(rotPt(-r3 * 0.95f, hat_y), rotPt(r3 * 0.95f, hat_y), 3.6f * scale, WHITE);
            DrawCircleV(rotPt(-dir * r3 * 0.65f, hat_y - 12.0f * scale), 3.0f * scale, WHITE);
        } else if (skin == 3) {
            // 3. Рогатый шлем Викинга
            Color iron = Color{ 120, 130, 145, 255 };
            Color horn = Color{ 242, 234, 212, 255 };
            DrawCircleSector(rotPt(0.0f, hat_y + 1.0f), r3 * 0.92f, 180.0f, 360.0f, 14, iron);
            DrawLineEx(rotPt(-r3 * 0.98f, hat_y + 1.0f), rotPt(r3 * 0.98f, hat_y + 1.0f), 2.4f * scale, goldDark);
            DrawTriCCW(rotPt(-r3 * 0.85f, hat_y - 1.0f), rotPt(-r3 * 0.75f, hat_y - 4.5f * scale), rotPt(-r3 * 1.55f, hat_y - 10.0f * scale), horn);
            DrawTriCCW(rotPt( r3 * 0.85f, hat_y - 1.0f), rotPt( r3 * 0.75f, hat_y - 4.5f * scale), rotPt( r3 * 1.55f, hat_y - 10.0f * scale), horn);
        } else if (skin == 4) {
            // 4. Налобная повязка Ниндзя
            DrawLineEx(rotPt(-r3 * 0.92f, hat_y + 1.5f), rotPt(r3 * 0.92f, hat_y + 1.5f), 3.8f * scale, Color{ 28, 30, 36, 255 });
            DrawRectangle(int(c3.x - 3.5f * scale + dir * 1.2f), int(c3.y - r3 * 0.76f), int(7.0f * scale), int(3.0f * scale), Color{ 185, 195, 210, 255 });
            DrawLineEx(rotPt(-dir * r3 * 0.85f, hat_y + 1.5f), rotPt(-dir * r3 * 1.55f + flutter, hat_y + 6.0f * scale), 2.4f * scale, Color{ 210, 42, 48, 255 });
        } else if (skin == 5) {
            // 5. Треуголка Пирата
            DrawTriCCW(rotPt(-r3 * 1.35f, hat_y + 1.0f), rotPt(r3 * 1.35f, hat_y + 1.0f), rotPt(0.0f, hat_y - 9.5f * scale), coalColor);
            DrawLineEx(rotPt(-r3 * 1.35f, hat_y + 1.0f), rotPt(r3 * 1.35f, hat_y + 1.0f), 1.6f * scale, goldColor);
            DrawCircleV(rotPt(dir * 1.0f, hat_y - 3.5f * scale), 2.1f * scale, WHITE);
        } else if (skin == 6) {
            // 6. Шляпа Ковбоя
            Color hatBrown = Color{ 138, 78, 36, 255 };
            Color hatDark = Color{ 98, 52, 20, 255 };
            DrawLineEx(rotPt(-r3 * 1.35f, hat_y), rotPt(r3 * 1.35f, hat_y), 3.2f * scale, hatBrown);
            DrawQuadCCW(rotPt(-r3 * 0.75f, hat_y - 7.5f * scale), rotPt(-r3 * 0.80f, hat_y),
                        rotPt( r3 * 0.80f, hat_y), rotPt( r3 * 0.75f, hat_y - 7.5f * scale), hatBrown);
            DrawLineEx(rotPt(-r3 * 0.78f, hat_y - 1.5f * scale), rotPt(r3 * 0.78f, hat_y - 1.5f * scale), 1.8f * scale, hatDark);
        } else if (skin == 7) {
            // 7. Ведро с антенной для Робота
            Color bucket = Color{ 145, 156, 172, 255 };
            DrawQuadCCW(rotPt(-r3 * 0.65f, hat_y - 8.5f * scale), rotPt(-r3 * 0.82f, hat_y + 1.0f),
                        rotPt( r3 * 0.82f, hat_y + 1.0f), rotPt( r3 * 0.65f, hat_y - 8.5f * scale), bucket);
            DrawLineEx(rotPt(0.0f, hat_y - 8.5f * scale), rotPt(0.0f, hat_y - 14.5f * scale), 1.5f * scale, bucket);
            DrawCircleV(rotPt(0.0f, hat_y - 15.0f * scale), 2.2f * scale, Color{ 255, 55, 55, 255 });
        } else if (skin == 8) {
            // 8. Меховая шапка-ушанка
            Color furMain = Color{ 92, 62, 42, 255 };
            Color furTrim = Color{ 165, 132, 102, 255 };
            DrawCircleSector(rotPt(0.0f, hat_y + 1.0f), r3 * 0.95f, 180.0f, 360.0f, 14, furMain);
            DrawLineEx(rotPt(-r3 * 0.95f, hat_y), rotPt(r3 * 0.95f, hat_y), 4.0f * scale, furTrim);
            DrawLineEx(rotPt(-r3 * 0.90f, hat_y), rotPt(-r3 * 1.02f, hat_y + 8.5f * scale), 3.8f * scale, furMain);
            DrawLineEx(rotPt( r3 * 0.90f, hat_y), rotPt( r3 * 1.02f, hat_y + 8.5f * scale), 3.8f * scale, furMain);
        } else if (skin == 9) {
            // 9. Золотая корона Короля
            Vector2 b_l = rotPt(-r3 * 0.82f, hat_y + 1.0f);
            Vector2 b_r = rotPt( r3 * 0.82f, hat_y + 1.0f);
            Vector2 t_l = rotPt(-r3 * 0.88f, hat_y - 7.5f * scale);
            Vector2 t_m = rotPt(0.0f, hat_y - 9.5f * scale);
            Vector2 t_r = rotPt( r3 * 0.88f, hat_y - 7.5f * scale);
            DrawQuadCCW(rotPt(-r3 * 0.82f, hat_y - 3.5f * scale), b_l, b_r, rotPt(r3 * 0.82f, hat_y - 3.5f * scale), goldColor);
            DrawTriCCW(b_l, rotPt(-r3 * 0.25f, hat_y - 3.0f * scale), t_l, goldColor);
            DrawTriCCW(rotPt(-r3 * 0.35f, hat_y - 3.0f * scale), rotPt(r3 * 0.35f, hat_y - 3.0f * scale), t_m, goldColor);
            DrawTriCCW(rotPt(r3 * 0.25f, hat_y - 3.0f * scale), b_r, t_r, goldColor);
            DrawCircleV(rotPt(0.0f, hat_y - 2.0f * scale), 1.6f * scale, Color{ 225, 35, 48, 255 });
        }
    }

    if (is_snap_target && sm.stage < 3) {
        Vector2 next_c = (sm.stage == 1) ? c2 : c3;
        float next_r = (sm.stage == 1) ? r2 : r3;
        DrawCircleLines((int)next_c.x, (int)next_c.y, next_r + 2.0f, Color{ 120, 195, 255, 220 });
    }
}

// Псевдо-3D снегоуборочная машина с микровибрацией корпуса, мигалкой и конусом жёлтого света фар
void DrawSnowplowTruck(const Snowplow& plow) {
    if (!plow.active) return;

    float d = plow.dir;
    float cx = plow.x;
    float ground_by = plow.y;
    float by = plow.y + plow.vib_y;

    float dz_x = -d * 8.0f;
    float dz_y = -6.5f;

    Color shadowCol = Color{ 15, 22, 34, 75 };
    Color chassisCol = Color{ 34, 38, 46, 255 };
    Color bumperCol = Color{ 56, 62, 74, 255 };
    Color bodyTop = Color{ 255, 194, 48, 255 };
    Color bodyMain = Color{ 242, 160, 18, 255 };
    Color bodyShade = Color{ 196, 118, 10, 255 };
    Color bodyOutline = Color{ 135, 78, 6, 255 };
    Color cabTop = Color{ 255, 202, 65, 255 };
    Color cabSide = Color{ 238, 152, 16, 255 };
    Color glassDark = Color{ 38, 56, 78, 245 };
    Color glassLight = Color{ 145, 208, 248, 235 };
    Color glassGlint = Color{ 235, 250, 255, 190 };
    Color chromeCol = Color{ 215, 224, 236, 255 };
    Color tireFar = Color{ 20, 23, 28, 255 };
    Color tireNear = Color{ 30, 34, 42, 255 };
    Color rimCol = Color{ 185, 194, 208, 255 };
    Color hubCol = Color{ 95, 104, 118, 255 };

    DrawRectangleRounded(Rectangle{ cx - 44.0f + d * 6.0f, ground_by - 4.0f, 88.0f, 6.0f }, 0.5f, 4, shadowCol);

    float rear_wx = cx - d * 21.0f;
    float front_wx = cx + d * 19.0f;
    float wy = ground_by - 8.5f;
    for (float wx : { rear_wx, front_wx }) {
        DrawCircleV({ wx + dz_x * 0.75f, wy + dz_y * 0.65f }, 8.2f, tireFar);
    }

    DrawRectangleRounded(Rectangle{ cx - 35.0f, by - 14.0f, 70.0f, 6.5f }, 0.25f, 3, chassisCol);
    DrawRectangleRounded(Rectangle{ cx - 7.0f, by - 14.5f, 14.0f, 6.5f }, 0.35f, 3, Color{ 75, 82, 94, 255 });
    float tail_x = cx - d * 35.0f;
    DrawRectangleRounded(Rectangle{ tail_x - 2.0f, by - 15.0f, 4.0f, 4.5f }, 0.3f, 2, Color{ 235, 35, 40, 255 });

    float bed_front_x = cx + d * 2.0f;
    float bed_back_x = cx - d * 35.0f;
    float bed_top_y = by - 31.0f;
    float bed_bot_y = by - 13.0f;

    Vector2 b_ft = { bed_front_x, bed_top_y };
    Vector2 b_fb = { bed_front_x, bed_bot_y };
    Vector2 b_bb = { bed_back_x, bed_bot_y };
    Vector2 b_bt = { bed_back_x, bed_top_y + 1.5f };

    Vector2 b_ft_3d = { b_ft.x + dz_x, b_ft.y + dz_y };
    Vector2 b_bt_3d = { b_bt.x + dz_x, b_bt.y + dz_y };
    Vector2 b_bb_3d = { b_bb.x + dz_x, b_bb.y + dz_y };

    DrawQuadCCW(b_bt_3d, b_bb_3d, b_bb, b_bt, bodyShade);
    DrawQuadCCW(b_bt_3d, b_bt, b_ft, b_ft_3d, bodyTop);
    Vector2 snow_mound1 = { (b_bt.x + b_ft.x) * 0.5f + dz_x * 0.45f, bed_top_y - 4.5f };
    DrawTriCCW(b_bt, b_ft, snow_mound1, Color{ 242, 248, 255, 245 });
    DrawTriCCW(b_bt_3d, b_bt, snow_mound1, Color{ 215, 228, 245, 245 });

    DrawQuadCCW(b_ft, b_fb, b_bb, b_bt, bodyMain);
    DrawQuadCCW({ b_ft.x, bed_bot_y - 4.5f }, b_fb, b_bb, { b_bb.x, bed_bot_y - 4.5f }, bodyShade);

    for (int r = 1; r <= 3; r++) {
        float t = (float)r / 4.0f;
        float rx = b_ft.x + (b_bb.x - b_ft.x) * t;
        DrawLineEx({ rx, bed_top_y + 1.5f }, { rx, bed_bot_y - 1.0f }, 1.6f, bodyShade);
    }
    DrawLineEx({ b_ft.x - d * 2.0f, bed_bot_y - 5.0f }, { b_bb.x + d * 2.0f, bed_bot_y - 5.0f }, 2.0f, Color{ 42, 46, 54, 210 });
    DrawLineEx(b_ft, b_bt, 1.3f, bodyOutline);
    DrawLineEx(b_bt, b_bb, 1.3f, bodyOutline);

    float exh_x = cx + d * 1.5f;
    DrawLineEx({ exh_x, by - 14.0f }, { exh_x, by - 41.0f }, 2.6f, Color{ 85, 92, 104, 255 });
    DrawLineEx({ exh_x, by - 41.0f }, { exh_x - d * 3.0f, by - 43.0f }, 2.2f, chromeCol);

    float cab_back_x = cx + d * 3.0f;
    float cab_roof_fx = cx + d * 17.5f;
    float hood_fx = cx + d * 31.5f;
    float cab_top_y = by - 38.5f;
    float hood_top_y = by - 25.5f;
    float cab_bot_y = by - 13.0f;

    Vector2 c_bt = { cab_back_x, cab_top_y };
    Vector2 c_rt = { cab_roof_fx, cab_top_y };
    Vector2 c_ht = { hood_fx, hood_top_y };
    Vector2 c_hb = { hood_fx, cab_bot_y };
    Vector2 c_bb = { cab_back_x, cab_bot_y };

    Vector2 c_bt_3d = { c_bt.x + dz_x, c_bt.y + dz_y };
    Vector2 c_rt_3d = { c_rt.x + dz_x, c_rt.y + dz_y };
    Vector2 c_ht_3d = { c_ht.x + dz_x, c_ht.y + dz_y };

    DrawQuadCCW(c_bt_3d, c_bt, c_rt, c_rt_3d, cabTop);
    Vector2 windshield_bot = { cx + d * 21.5f, hood_top_y };
    Vector2 windshield_bot_3d = { windshield_bot.x + dz_x, windshield_bot.y + dz_y };
    DrawQuadCCW(windshield_bot_3d, windshield_bot, c_ht, c_ht_3d, cabTop);

    DrawQuadCCW(c_rt_3d, windshield_bot_3d, windshield_bot, c_rt, glassLight);
    DrawLineEx({ (c_rt.x + c_rt_3d.x) * 0.5f, c_rt.y + 1.5f },
               { (windshield_bot.x + windshield_bot_3d.x) * 0.5f, windshield_bot.y - 1.5f }, 1.6f, glassGlint);

    DrawQuadCCW(c_bt, c_bb, { windshield_bot.x, cab_bot_y }, c_rt, cabSide);
    DrawQuadCCW(windshield_bot, { windshield_bot.x, cab_bot_y }, c_hb, c_ht, cabSide);

    Vector2 w_tl = { cab_back_x + d * 2.5f, cab_top_y + 2.5f };
    Vector2 w_tr = { cab_roof_fx - d * 1.0f, cab_top_y + 2.5f };
    Vector2 w_br = { windshield_bot.x - d * 1.5f, hood_top_y - 1.0f };
    Vector2 w_bl = { cab_back_x + d * 2.5f, hood_top_y - 1.0f };
    DrawQuadCCW(w_tl, w_bl, w_br, w_tr, glassDark);
    DrawQuadCCW({ w_tl.x + d * 0.8f, w_tl.y + 0.8f }, w_bl,
                { w_br.x - d * 0.8f, w_br.y }, { w_tr.x - d * 0.8f, w_tr.y + 0.8f }, glassLight);

    DrawLineEx({ cab_back_x + d * 2.0f, hood_top_y }, { cab_back_x + d * 2.0f, cab_bot_y - 2.0f }, 1.1f, bodyOutline);
    DrawRectangle(int(cab_back_x + d * 3.5f - 1.5f), int(hood_top_y + 2.5f), 3, 2, chassisCol);
    float mir_x = windshield_bot.x - d * 1.0f;
    DrawLineEx({ mir_x, cab_top_y + 5.0f }, { mir_x + d * 2.5f, cab_top_y + 5.0f }, 1.2f, chassisCol);
    DrawRectangleRounded(Rectangle{ mir_x + d * 2.5f - 1.5f, cab_top_y + 3.5f, 3.0f, 6.5f }, 0.3f, 2, chassisCol);

    DrawRectangleRounded(Rectangle{ hood_fx - (d > 0 ? 3.5f : 0.0f), hood_top_y + 2.0f, 3.5f, 8.5f }, 0.2f, 2, chassisCol);
    for (int g = 0; g < 3; g++) {
        float gy = hood_top_y + 3.5f + (float)g * 2.5f;
        DrawLineEx({ hood_fx - d * 3.0f, gy }, { hood_fx, gy }, 1.0f, chromeCol);
    }
    DrawRectangleRounded(Rectangle{ hood_fx - (d > 0 ? 4.0f : 2.0f), cab_bot_y - 4.0f, 6.0f, 4.5f }, 0.3f, 2, bumperCol);

    float head_x = hood_fx + d * 1.0f;
    float head_y = cab_bot_y - 3.5f;
    DrawCircleV({ head_x, head_y }, 2.8f, Color{ 255, 240, 110, 255 });
    Vector2 beam_origin = { head_x, head_y };
    Vector2 beam_top = { head_x + d * 82.0f, head_y - 16.0f };
    Vector2 beam_bot = { head_x + d * 82.0f, head_y + 12.0f };
    Vector2 beam_mid_t = { head_x + d * 54.0f, head_y - 9.0f };
    Vector2 beam_mid_b = { head_x + d * 54.0f, head_y + 7.0f };
    DrawTriCCW(beam_origin, beam_top, beam_bot, Color{ 255, 225, 65, 58 });
    DrawTriCCW(beam_origin, beam_mid_t, beam_mid_b, Color{ 255, 242, 125, 72 });

    float beacon_x = (c_bt.x + c_rt.x) * 0.5f + dz_x * 0.5f;
    float beacon_y = cab_top_y + dz_y * 0.5f - 2.5f;
    float flash = std::pow(0.5f + 0.5f * std::sin(plow.beacon_phase), 2.0f);
    DrawRectangleRounded(Rectangle{ beacon_x - 4.5f, beacon_y, 9.0f, 3.0f }, 0.3f, 2, chassisCol);
    DrawCircleV({ beacon_x, beacon_y - 1.5f }, 16.0f, Color{ 255, 155, 15, (unsigned char)(25 + flash * 125.0f) });
    DrawCircleV({ beacon_x, beacon_y - 1.5f }, 9.0f, Color{ 255, 180, 25, (unsigned char)(50 + flash * 155.0f) });
    DrawCircleV({ beacon_x, beacon_y - 1.5f }, 3.4f, Color{ 255, 215, 65, 255 });

    for (float wx : { rear_wx, front_wx }) {
        DrawCircleSector({ wx, wy }, 11.2f, 185.0f, 355.0f, 16, chassisCol);
        DrawCircleV({ wx, wy }, 8.6f, tireNear);
        for (int t = 0; t < 8; t++) {
            float ang = plow.wheel_angle + (float)t * 0.785398f;
            Vector2 p_in = { wx + std::cos(ang) * 6.8f, wy + std::sin(ang) * 6.8f };
            Vector2 p_out = { wx + std::cos(ang) * 8.6f, wy + std::sin(ang) * 8.6f };
            DrawLineEx(p_in, p_out, 1.4f, Color{ 52, 58, 68, 255 });
        }
        DrawCircleV({ wx, wy }, 4.5f, rimCol);
        DrawCircleV({ wx, wy }, 2.0f, hubCol);
    }

    float blade_base_x = hood_fx + d * 9.5f;
    float blade_top_y = by - 25.0f;
    float blade_bot_y = by - 1.0f;

    DrawLineEx({ hood_fx, by - 11.0f }, { blade_base_x - d * 1.5f, by - 8.0f }, 3.2f, chassisCol);
    DrawLineEx({ hood_fx, by - 15.5f }, { blade_base_x - d * 2.0f, by - 17.5f }, 2.2f, chromeCol);

    Vector2 pl_top_near = { blade_base_x + d * 2.5f, blade_top_y };
    Vector2 pl_mid_near = { blade_base_x + d * 0.5f, (blade_top_y + blade_bot_y) * 0.5f };
    Vector2 pl_bot_near = { blade_base_x + d * 3.5f, blade_bot_y };

    Vector2 pl_top_far = { pl_top_near.x + dz_x * 1.25f, pl_top_near.y + dz_y * 1.15f };
    Vector2 pl_mid_far = { pl_mid_near.x + dz_x * 1.25f, pl_mid_near.y + dz_y * 1.15f };
    Vector2 pl_bot_far = { pl_bot_near.x + dz_x * 1.25f, pl_bot_near.y + dz_y * 1.15f };

    DrawQuadCCW(pl_top_far, pl_mid_far, pl_mid_near, pl_top_near, Color{ 238, 244, 252, 255 });
    DrawQuadCCW(pl_mid_far, pl_bot_far, pl_bot_near, pl_mid_near, Color{ 205, 216, 232, 255 });

    DrawLineEx(pl_top_far, pl_top_near, 3.2f, bodyMain);
    DrawLineEx(pl_bot_far, pl_bot_near, 2.6f, Color{ 245, 140, 18, 255 });
    DrawLineEx(pl_top_near, pl_mid_near, 1.5f, Color{ 140, 152, 170, 255 });
    DrawLineEx(pl_mid_near, pl_bot_near, 1.5f, Color{ 140, 152, 170, 255 });

    DrawLineEx(pl_top_far, { pl_top_far.x, pl_top_far.y - 9.0f }, 1.4f, Color{ 255, 120, 20, 255 });
    DrawCircleV({ pl_top_far.x, pl_top_far.y - 9.5f }, 2.0f, Color{ 255, 65, 30, 255 });
}

// ============================================================================
// ТОЧКА ВХОДА
// ============================================================================

int main() {
#if defined(__linux__)
    setenv("GDK_BACKEND", "x11", 0);
    setenv("QT_QPA_PLATFORM", "xcb", 0);
#endif

    SetConfigFlags(FLAG_WINDOW_TRANSPARENT |
                   FLAG_WINDOW_UNDECORATED |
                   FLAG_WINDOW_TOPMOST |
                   FLAG_WINDOW_UNFOCUSED |
                   FLAG_VSYNC_HINT);

    int screenWidth = GetMonitorWidth(0);
    int screenHeight = GetMonitorHeight(0);
    if (screenWidth == 0) { screenWidth = 1920; screenHeight = 1080; }

    const char* appTitle = "Desktop Snow";
    InitWindow(screenWidth, screenHeight, appTitle);
    SetWindowState(FLAG_WINDOW_MOUSE_PASSTHROUGH | FLAG_WINDOW_TOPMOST | FLAG_WINDOW_UNFOCUSED);

    float ground_y = (float)screenHeight;
#if defined(_WIN32)
    g_selfHwnd = (HWND)GetWindowHandle();
    LONG exStyle = GetWindowLong(g_selfHwnd, GWL_EXSTYLE);
    SetWindowLong(g_selfHwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
    SetWindowPos(g_selfHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    HMODULE hDwm = LoadLibraryA("dwmapi.dll");
    if (hDwm) {
        pDwmGetWindowAttribute = (PFN_DwmGetWindowAttribute)GetProcAddress(hDwm, "DwmGetWindowAttribute");
    }

    ground_y = GetWindowsWorkAreaBottom(screenHeight);
    float win_base_ground = ground_y;
    double last_workarea_check = 0.0;
#elif defined(__linux__)
    LinuxPlatform linuxPlat;
#endif

    SetTargetFPS(60);

    Image img = GenImageColor(32, 32, BLANK);
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            float dx = (float)x - 15.5f;
            float dy = (float)y - 15.5f;
            float dist = std::sqrt(dx * dx + dy * dy) / 15.0f;
            if (dist <= 1.0f) {
                float alpha_f = (dist < 0.45f) ? 1.0f : ((1.0f - dist) / 0.55f);
                if (dist >= 0.45f) alpha_f = alpha_f * alpha_f * (3.0f - 2.0f * alpha_f);
                ImageDrawPixel(&img, x, y, Color{ 255, 255, 255, (unsigned char)(alpha_f * 255.0f) });
            }
        }
    }
    Texture2D flakeTex = LoadTextureFromImage(img);
    SetTextureFilter(flakeTex, TEXTURE_FILTER_BILINEAR);
    UnloadImage(img);

    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> dist_x(0.0f, (float)screenWidth);
    std::uniform_real_distribution<float> dist_01(0.0f, 1.0f);

    constexpr size_t BASE_COUNT = 540;
    std::vector<Snowflake> flakes;
    flakes.reserve(4000);

    auto make_flake = [&](bool start_at_top) -> Snowflake {
        Snowflake f{};
        f.pos = { dist_x(rng), start_at_top ? -(5.0f + dist_01(rng) * 35.0f) : dist_01(rng) * (float)screenHeight };
        f.layer = rng() % 3;
        f.sway_phase = dist_01(rng) * 6.28318f;
        f.is_avalanche = false;
        f.is_ice = false;
        f.life = 0.0f;
        f.max_life = 1.0f;

        if (f.layer == 0) {
            f.vel = { 0.0f, 44.0f + dist_01(rng) * 22.0f };
            f.radius = 1.6f + dist_01(rng) * 0.7f;
            f.alpha = (unsigned char)(120 + rng() % 35);
            f.sway_freq = 1.1f + dist_01(rng) * 0.5f;
            f.sway_amp = 10.0f + dist_01(rng) * 6.0f;
        } else if (f.layer == 1) {
            f.vel = { 0.0f, 72.0f + dist_01(rng) * 30.0f };
            f.radius = 2.4f + dist_01(rng) * 0.9f;
            f.alpha = (unsigned char)(180 + rng() % 40);
            f.sway_freq = 1.4f + dist_01(rng) * 0.6f;
            f.sway_amp = 16.0f + dist_01(rng) * 8.0f;
        } else {
            f.vel = { 0.0f, 108.0f + dist_01(rng) * 38.0f };
            f.radius = 3.3f + dist_01(rng) * 1.1f;
            f.alpha = (unsigned char)(230 + rng() % 25);
            f.sway_freq = 1.7f + dist_01(rng) * 0.7f;
            f.sway_amp = 22.0f + dist_01(rng) * 10.0f;
        }
        return f;
    };

    for (size_t i = 0; i < BASE_COUNT; i++) flakes.push_back(make_flake(false));

    const float DRIFT_STEP = 4.0f;
    const float STANDARD_BALL_RADIUS = 12.5f;
    int max_screen_bins = (int)(screenWidth / DRIFT_STEP) + 64;
    int bottom_bins = (int)(screenWidth / DRIFT_STEP) + 4;

    std::vector<float> bottom_wave(bottom_bins, 1.0f);
    std::vector<float> bottom_drift(bottom_bins, 0.0f);
    InitWaveShape(bottom_wave, DRIFT_STEP, 777);
    FillSculptedFromWave(bottom_drift, bottom_wave, 15.0f);

    DockInfo cur_dock = { false, {} };
    DockInfo prev_dock = { false, {} };
    std::vector<float> dock_wave(max_screen_bins, 1.0f);
    std::vector<float> dock_drift;
    InitWaveShape(dock_wave, DRIFT_STEP, 999);

    std::unordered_map<uintptr_t, DriftProfile> win_drifts;
    std::vector<WindowData> current_windows;
    std::vector<Snowman> snowmen;
    std::vector<FallingIcicle> falling_icicles;
    HeldSnowball held_ball = { false, {}, {}, 4.0f, 0.0f };
    std::vector<FallingBall> falling_balls;
    std::vector<ExhaustPuff> exhaust_puffs;

    Snowplow plow{};
    plow.active = false;
    plow.y = ground_y;
    plow.vib_y = 0.0f;
    float next_plow_dir = -1.0f;

    bool interactive_toggle = false;
    bool prev_passthrough = true;
    float hint_timer = 7.0f;

    bool game_paused = false;
    double last_game_check = -2.0;

    auto spawn_snowplow = [&]() {
        if (plow.active) return;
        plow.active = true;
        plow.dir = next_plow_dir;
        next_plow_dir = -next_plow_dir;
        plow.x = (plow.dir < 0.0f) ? ((float)screenWidth + 60.0f) : -60.0f;
        plow.y = ground_y;
        plow.vib_y = 0.0f;
        plow.speed = 185.0f;
        plow.wheel_angle = 0.0f;
        plow.beacon_phase = 0.0f;
        plow.exhaust_timer = 0.0f;
    };

    auto reset_all_snow = [&]() {
        InitWaveShape(bottom_wave, DRIFT_STEP, (unsigned int)rng());
        FillSculptedFromWave(bottom_drift, bottom_wave, 13.5f);
        for (auto& kv : win_drifts) {
            InitWaveShape(kv.second.wave_shape, DRIFT_STEP, (unsigned int)(kv.first + rng()));
            if (!kv.second.is_maximized && kv.second.rect.y > 4.0f) {
                FillSculptedFromWave(kv.second.heights, kv.second.wave_shape, 9.0f);
            } else {
                std::fill(kv.second.heights.begin(), kv.second.heights.end(), 0.0f);
            }
            kv.second.icicles.clear();
            kv.second.icicle_spawn_timer = 0.5f;
        }
        if (cur_dock.active) {
            FillSculptedFromWave(dock_drift, dock_wave, 9.0f);
        }
        snowmen.clear();
        falling_icicles.clear();
        falling_balls.clear();
        exhaust_puffs.clear();
        held_ball.active = false;
        plow.active = false;
    };

    auto shatter_icicle = [&](Vector2 hit_pos, float ic_len) {
        int shards = std::clamp((int)(ic_len * 0.7f), 8, 22);
        for (int k = 0; k < shards && flakes.size() < 3900; k++) {
            float ang = -3.14159f * dist_01(rng);
            float spd = 40.0f + dist_01(rng) * 125.0f;
            Snowflake sf{};
            sf.pos = { hit_pos.x + (dist_01(rng) - 0.5f) * 8.0f, hit_pos.y - 2.0f };
            sf.vel = { std::cos(ang) * spd, std::sin(ang) * spd - 30.0f };
            sf.radius = 1.2f + dist_01(rng) * 1.3f;
            sf.sway_phase = dist_01(rng) * 6.28f;
            sf.sway_freq = 4.5f;
            sf.sway_amp = 6.0f;
            sf.alpha = 245;
            sf.layer = 2;
            sf.is_avalanche = true;
            sf.is_ice = true;
            sf.life = 0.0f;
            sf.max_life = 0.75f + dist_01(rng) * 0.35f;
            flakes.push_back(sf);
        }
    };

    // Разбивание снежного шара на брызги + выбивание кратера в сугробе
    auto shatter_snowball = [&](Vector2 hit_pos, Vector2 impact_vel, float ball_r,
                                uintptr_t hit_win_id, bool hit_dock, bool hit_ground, float impact_power) {
        int pieces = (int)(26 + ball_r * 1.8f);
        for (int k = 0; k < pieces && flakes.size() < 3900; k++) {
            bool is_dust = (k % 3 != 0);
            float ang = -3.14159f * dist_01(rng);
            float spd = (is_dust ? 55.0f : 85.0f) + dist_01(rng) * (150.0f + impact_power * 75.0f);
            Snowflake af{};
            af.pos = { hit_pos.x + (dist_01(rng) - 0.5f) * ball_r * 1.4f,
                       hit_pos.y - dist_01(rng) * ball_r * 0.8f };
            af.vel = { std::cos(ang) * spd + impact_vel.x * 0.22f,
                       std::sin(ang) * spd - 45.0f };
            af.radius = is_dust ? (1.1f + dist_01(rng) * 0.9f) : (2.1f + dist_01(rng) * 1.6f);
            af.sway_phase = dist_01(rng) * 6.28f;
            af.sway_freq = is_dust ? 4.2f : 2.8f;
            af.sway_amp = is_dust ? 12.0f : 6.0f;
            af.alpha = is_dust ? 215 : 252;
            af.layer = 2;
            af.is_avalanche = true;
            af.is_ice = false;
            af.life = 0.0f;
            af.max_life = 8.0f;
            flakes.push_back(af);
        }

        // Выбиваем кратер в сугробе в точке падения!
        if (hit_win_id != 0) {
            auto wit = win_drifts.find(hit_win_id);
            if (wit != win_drifts.end()) {
                CarveSnowCrater(wit->second.heights, DRIFT_STEP,
                                hit_pos.x - wit->second.rect.x, wit->second.rect.width, impact_power, 20.0f);
            }
        } else if (hit_dock && cur_dock.active) {
            CarveSnowCrater(dock_drift, DRIFT_STEP,
                            hit_pos.x - cur_dock.rect.x, cur_dock.rect.width, impact_power, 22.0f);
        } else if (hit_ground) {
            CarveSnowCrater(bottom_drift, DRIFT_STEP,
                            hit_pos.x, (float)screenWidth, impact_power, 38.0f);
        }
    };

    auto place_or_stack_ball = [&](Vector2 pos, float ball_r, bool allow_midair_drop, Vector2 release_vel, bool is_thrown) -> bool {
        int best_idx = -1;
        float best_dist = 42.0f;
        for (int idx = 0; idx < (int)snowmen.size(); idx++) {
            auto& sm = snowmen[idx];
            if (sm.falling || sm.stage >= 3) continue;
            float stack_y = sm.world_pos.y - (float)sm.stage * (sm.r1 * 1.55f);
            float dx = std::abs(pos.x - sm.world_pos.x);
            float dy = std::abs(pos.y - stack_y);
            if (dx < 34.0f && dy < 44.0f) {
                float d = std::sqrt(dx * dx + dy * dy);
                if (d < best_dist) {
                    best_dist = d;
                    best_idx = idx;
                }
            }
        }
        if (best_idx >= 0) {
            snowmen[best_idx].stage++;
            snowmen[best_idx].anim_pop = 0.25f;
            return true;
        }

        for (size_t wi = 0; wi < current_windows.size(); wi++) {
            const auto& w = current_windows[wi];
            if (w.is_maximized || w.rect.y <= 4.0f) continue;

            float wy = w.rect.y;
            if (pos.x >= w.rect.x + 12.0f && pos.x <= (w.rect.x + w.rect.width - 12.0f) &&
                pos.y >= (wy - 34.0f) && pos.y <= (wy + 16.0f)) {
                if (IsPointOccludedByFrontWindows(pos.x, wy, current_windows, wi)) {
                    break;
                }
                auto wit = win_drifts.find(w.id);
                if (wit != win_drifts.end()) {
                    float lx = pos.x - w.rect.x;
                    int bin = std::clamp((int)(lx / DRIFT_STEP), 0, (int)wit->second.heights.size() - 1);
                    float h_here = wit->second.heights[bin] * GetEdgeTaper(lx, w.rect.width, true);
                    ScoopSnowAt(wit->second.heights, DRIFT_STEP, lx, 16.0f, 2.5f);

                    Snowman sm{};
                    sm.win_id = w.id;
                    sm.on_dock = false;
                    sm.rel_x = lx;
                    sm.rel_y_offset = std::max(1.0f, h_here * 0.45f);
                    sm.stage = 1;
                    sm.skin = (int)(rng() % SNOWMAN_SKIN_COUNT);
                    sm.r1 = ball_r;
                    sm.r2 = ball_r;
                    sm.r3 = ball_r;
                    sm.anim_pop = 0.35f;
                    sm.falling = false;
                    sm.fall_vx = 0.0f;
                    sm.fall_vy = 0.0f;
                    sm.fall_angle = 0.0f;
                    sm.fall_ang_vel = 0.0f;
                    sm.facing = (rng() % 2 == 0) ? 1 : -1;
                    snowmen.push_back(sm);
                    return true;
                }
            }
            if (pos.x >= w.rect.x && pos.x <= (w.rect.x + w.rect.width) &&
                pos.y >= w.rect.y && pos.y <= (w.rect.y + w.rect.height)) {
                break;
            }
        }

        if (cur_dock.active &&
            pos.x >= cur_dock.rect.x + 10.0f && pos.x <= (cur_dock.rect.x + cur_dock.rect.width - 10.0f) &&
            pos.y >= (cur_dock.rect.y - 34.0f) && pos.y <= (cur_dock.rect.y + 12.0f)) {
            float lx = pos.x - cur_dock.rect.x;
            int bin = std::clamp((int)(lx / DRIFT_STEP), 0, (int)dock_drift.size() - 1);
            float h_here = dock_drift[bin];
            ScoopSnowAt(dock_drift, DRIFT_STEP, lx, 16.0f, 2.5f);

            Snowman sm{};
            sm.win_id = 0;
            sm.on_dock = true;
            sm.rel_x = lx;
            sm.rel_y_offset = std::max(1.5f, h_here * 0.45f);
            sm.stage = 1;
            sm.skin = (int)(rng() % SNOWMAN_SKIN_COUNT);
            sm.r1 = ball_r;
            sm.r2 = ball_r;
            sm.r3 = ball_r;
            sm.anim_pop = 0.35f;
            sm.falling = false;
            sm.fall_vx = 0.0f;
            sm.fall_vy = 0.0f;
            sm.fall_angle = 0.0f;
            sm.fall_ang_vel = 0.0f;
            sm.facing = (rng() % 2 == 0) ? 1 : -1;
            snowmen.push_back(sm);
            return true;
        }

        if (pos.y >= (ground_y - 42.0f) && pos.y <= (ground_y + 14.0f)) {
            if (!cur_dock.active || pos.x < cur_dock.rect.x || pos.x > (cur_dock.rect.x + cur_dock.rect.width)) {
                int bin = std::clamp((int)(pos.x / DRIFT_STEP), 0, bottom_bins - 1);
                float h_here = bottom_drift[bin];
                ScoopSnowAt(bottom_drift, DRIFT_STEP, pos.x, 18.0f, 3.0f);

                Snowman sm{};
                sm.win_id = 0;
                sm.on_dock = false;
                sm.rel_x = pos.x;
                sm.rel_y_offset = std::max(2.0f, h_here * 0.45f);
                sm.stage = 1;
                sm.skin = (int)(rng() % SNOWMAN_SKIN_COUNT);
                sm.r1 = ball_r;
                sm.r2 = ball_r;
                sm.r3 = ball_r;
                sm.anim_pop = 0.35f;
                sm.falling = false;
                sm.fall_vx = 0.0f;
                sm.fall_vy = 0.0f;
                sm.fall_angle = 0.0f;
                sm.fall_ang_vel = 0.0f;
                sm.facing = (rng() % 2 == 0) ? 1 : -1;
                snowmen.push_back(sm);
                return true;
            }
        }

        if (allow_midair_drop) {
            FallingBall fb{};
            fb.pos = pos;
            fb.vx = is_thrown ? std::clamp(release_vel.x * 0.95f, -1500.0f, 1500.0f) : (release_vel.x * 0.25f);
            fb.vy = is_thrown ? std::clamp(release_vel.y * 0.95f, -1300.0f, 1300.0f) : std::max(25.0f, release_vel.y * 0.35f);
            fb.start_y = pos.y;
            fb.radius = ball_r;
            fb.age = 0.0f;
            fb.is_thrown = is_thrown;
            falling_balls.push_back(fb);
            return true;
        }

        return false;
    };

    while (!WindowShouldClose()) {
        double now = GetTime();

        // 1. Проверка полноэкранных игр
        if (now - last_game_check > 0.8) {
            last_game_check = now;
            bool is_game = false;
#if defined(_WIN32)
            is_game = IsWindowsGameRunning(screenWidth, screenHeight);
#elif defined(__linux__)
            is_game = linuxPlat.IsGameRunning(screenWidth, screenHeight);
#endif
            if (is_game && !game_paused) {
                game_paused = true;
                SetTargetFPS(4);
            } else if (!is_game && game_paused) {
                game_paused = false;
                SetTargetFPS(60);
            }
        }

        if (game_paused) {
            BeginDrawing();
            ClearBackground(BLANK);
            EndDrawing();
            continue;
        }

        float dt = std::min(GetFrameTime(), 0.05f);
        float time = (float)now;
        if (hint_timer > 0.0f) hint_timer -= dt;

        // 2. Опрос глобальных горячих клавиш
        bool f8_p = false, f9_p = false, f10_p = false, cs_held = false;
#if defined(_WIN32)
        PollWindowsGlobalHotkeys(f8_p, f9_p, f10_p, cs_held);
#elif defined(__linux__)
        linuxPlat.PollGlobalHotkeys(f8_p, f9_p, f10_p, cs_held);
#endif
        if (f8_p) interactive_toggle = !interactive_toggle;
        if (f9_p) spawn_snowplow();
        if (f10_p) reset_all_snow();

        bool interactive_now = interactive_toggle || cs_held;
        bool want_passthrough = !interactive_now;

        if (want_passthrough != prev_passthrough) {
            prev_passthrough = want_passthrough;
            held_ball.active = false;
            if (want_passthrough) {
                SetWindowState(FLAG_WINDOW_MOUSE_PASSTHROUGH | FLAG_WINDOW_UNFOCUSED | FLAG_WINDOW_TOPMOST);
#if defined(_WIN32)
                LONG ex = GetWindowLong(g_selfHwnd, GWL_EXSTYLE);
                SetWindowLong(g_selfHwnd, GWL_EXSTYLE, ex | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE);
                SetWindowPos(g_selfHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
            } else {
                ClearWindowState(FLAG_WINDOW_MOUSE_PASSTHROUGH);
                SetWindowState(FLAG_WINDOW_UNFOCUSED | FLAG_WINDOW_TOPMOST);
#if defined(_WIN32)
                LONG ex = GetWindowLong(g_selfHwnd, GWL_EXSTYLE);
                SetWindowLong(g_selfHwnd, GWL_EXSTYLE, ((ex & ~WS_EX_TRANSPARENT) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE));
                SetWindowPos(g_selfHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
#endif
            }
        }

        // 3. Сканирование окон, меню ПКМ, Пуска и нижней панели / дока
#if defined(_WIN32)
        if (now - last_workarea_check > 0.5) {
            last_workarea_check = now;
            win_base_ground = GetWindowsWorkAreaBottom(screenHeight);
        }
        current_windows.clear();
        EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&current_windows));
        ground_y = win_base_ground;
        cur_dock = GetWindowsAutoHideTaskbar(screenWidth, screenHeight, win_base_ground);
#elif defined(__linux__)
        current_windows = linuxPlat.GetWindows(screenWidth, screenHeight, appTitle, now);
        ground_y = linuxPlat.bottom_panel_top;
        cur_dock = linuxPlat.floating_dock;
#endif

        // --- Обработка появления и скрытия нижней всплывающей панельки (дока) ---
        if (cur_dock.active && !prev_dock.active) {
            int d_bins = std::max(4, (int)(cur_dock.rect.width / DRIFT_STEP) + 2);
            dock_drift.assign(d_bins, 0.0f);
            for (int i = 0; i < d_bins; i++) {
                float wx = cur_dock.rect.x + (float)i * DRIFT_STEP;
                int b_idx = (int)(wx / DRIFT_STEP);
                if (b_idx >= 0 && b_idx < bottom_bins) {
                    dock_drift[i] = std::min(18.0f, std::max(5.5f, bottom_drift[b_idx]));
                    bottom_drift[b_idx] = 0.0f;
                } else {
                    dock_drift[i] = 7.0f * dock_wave[i % (int)dock_wave.size()];
                }
            }
            for (auto& sm : snowmen) {
                if (!sm.falling && sm.win_id == 0 && !sm.on_dock &&
                    sm.rel_x >= cur_dock.rect.x + 8.0f && sm.rel_x <= (cur_dock.rect.x + cur_dock.rect.width - 8.0f)) {
                    sm.on_dock = true;
                    sm.rel_x = sm.rel_x - cur_dock.rect.x;
                }
            }
        } else if (cur_dock.active && prev_dock.active) {
            int d_bins = std::max(4, (int)(cur_dock.rect.width / DRIFT_STEP) + 2);
            if ((int)dock_drift.size() < d_bins) dock_drift.resize(d_bins, 5.0f);
            int b_start = std::max(0, (int)((cur_dock.rect.x + 4.0f) / DRIFT_STEP));
            int b_end = std::min(bottom_bins - 1, (int)((cur_dock.rect.x + cur_dock.rect.width - 4.0f) / DRIFT_STEP));
            for (int b = b_start; b <= b_end; b++) bottom_drift[b] = 0.0f;
        } else if (!cur_dock.active && prev_dock.active) {
            for (int i = 1; i < (int)dock_drift.size() - 1; i++) {
                float lx = (float)i * DRIFT_STEP;
                float h = dock_drift[i] * GetEdgeTaper(lx, prev_dock.rect.width, true);
                if (h > 1.2f && flakes.size() < 3800) {
                    int count = (h > 8.0f) ? 2 : 1;
                    for (int k = 0; k < count; k++) {
                        Snowflake af{};
                        af.pos = { prev_dock.rect.x + lx + (dist_01(rng) - 0.5f) * 4.0f,
                                   prev_dock.rect.y - dist_01(rng) * h };
                        af.vel = { (dist_01(rng) - 0.5f) * 30.0f, 55.0f + dist_01(rng) * 85.0f };
                        af.radius = 2.0f + dist_01(rng) * 1.6f;
                        af.sway_phase = dist_01(rng) * 6.28f;
                        af.sway_freq = 3.0f;
                        af.sway_amp = 8.0f;
                        af.alpha = 240;
                        af.layer = 2;
                        af.is_avalanche = true;
                        af.is_ice = false;
                        af.life = 0.0f;
                        af.max_life = 8.0f;
                        flakes.push_back(af);
                    }
                }
            }
            for (auto& sm : snowmen) {
                if (sm.on_dock) {
                    sm.on_dock = false;
                    sm.falling = true;
                    sm.fall_vx = (dist_01(rng) - 0.5f) * 24.0f;
                    sm.fall_vy = 30.0f;
                    sm.fall_ang_vel = (dist_01(rng) - 0.5f) * 180.0f;
                }
            }
            dock_drift.clear();
        }
        prev_dock = cur_dock;

        // --- Обработка окон, сугробов и сосулек ---
        for (auto& kv : win_drifts) kv.second.alive = false;

        for (const auto& w : current_windows) {
            int needed_bins = std::max(max_screen_bins, (int)(w.rect.width / DRIFT_STEP) + 8);
            bool is_at_top_edge = (w.rect.y <= 4.0f);
            bool no_top_snow = w.is_maximized || is_at_top_edge;

            auto it = win_drifts.find(w.id);
            if (it == win_drifts.end()) {
                DriftProfile dp;
                dp.rect = w.rect;
                dp.prev_rect = w.rect;
                dp.has_prev = true;
                dp.is_maximized = w.is_maximized;
                dp.step = DRIFT_STEP;
                dp.left_shift_accum = 0.0f;
                dp.pending_cut_right = 0.0f;
                dp.prev_vx = 0.0f;
                dp.prev_vy = 0.0f;
                dp.shake_energy = 0.0f;
                dp.icicle_spawn_timer = 0.0f;
                dp.heights.resize(needed_bins, 0.0f);
                dp.wave_shape.resize(needed_bins, 1.0f);
                InitWaveShape(dp.wave_shape, DRIFT_STEP, (unsigned int)w.id);
                if (!no_top_snow && w.rect.width >= 420.0f) {
                    FillSculptedFromWave(dp.heights, dp.wave_shape, 9.5f);
                }
                if (!w.is_maximized && w.rect.width >= 180.0f) {
                    PopulateWindowIcicles(dp.icicles, 0.0f, w.rect.width, rng, false);
                }
                dp.alive = true;
                win_drifts[w.id] = std::move(dp);
            } else {
                auto& dp = it->second;
                if ((int)dp.heights.size() < needed_bins) {
                    dp.heights.resize(needed_bins, 0.0f);
                    dp.wave_shape.resize(needed_bins, 1.0f);
                    InitWaveShape(dp.wave_shape, DRIFT_STEP, (unsigned int)w.id);
                }

                if (no_top_snow) {
                    int max_idx = std::min((int)dp.heights.size() - 1, (int)(w.rect.width / DRIFT_STEP));
                    for (int i = 1; i < max_idx; i++) {
                        float h = dp.heights[i];
                        if (h > 0.6f && flakes.size() < 3900) {
                            float lx = (float)i * DRIFT_STEP;
                            int drops = (h > 7.5f) ? 2 : 1;
                            for (int k = 0; k < drops; k++) {
                                bool is_dust = (k > 0);
                                Snowflake af{};
                                af.pos = { w.rect.x + lx + (dist_01(rng) - 0.5f) * 4.0f,
                                           std::max(4.0f, w.rect.y) + dist_01(rng) * 6.0f };
                                af.vel = { (dist_01(rng) - 0.5f) * 45.0f,
                                           (is_dust ? 55.0f : 75.0f) + dist_01(rng) * 90.0f };
                                af.radius = is_dust ? (1.1f + dist_01(rng) * 0.8f) : (2.0f + dist_01(rng) * 1.6f);
                                af.sway_phase = dist_01(rng) * 6.28f;
                                af.sway_freq = 3.2f;
                                af.sway_amp = 10.0f;
                                af.alpha = is_dust ? 195 : 245;
                                af.layer = 2;
                                af.is_avalanche = true;
                                af.is_ice = false;
                                af.life = 0.0f;
                                af.max_life = 8.0f;
                                flakes.push_back(af);
                            }
                        }
                    }
                    std::fill(dp.heights.begin(), dp.heights.end(), 0.0f);
                    dp.shake_energy = 0.0f;
                    dp.left_shift_accum = 0.0f;
                    dp.pending_cut_right = 0.0f;

                    for (auto& sm : snowmen) {
                        if (!sm.falling && sm.win_id == w.id) {
                            sm.falling = true;
                            sm.fall_vx = (dist_01(rng) - 0.5f) * 30.0f;
                            sm.fall_vy = 35.0f;
                            sm.fall_ang_vel = (dist_01(rng) - 0.5f) * 180.0f;
                        }
                    }
                }

                if (w.is_maximized && !dp.icicles.empty()) {
                    for (const auto& ic : dp.icicles) {
                        if (ic.length >= 2.0f) {
                            FallingIcicle fi{};
                            fi.origin_win_id = w.id;
                            fi.pos = { w.rect.x + ic.rel_x, w.rect.y + w.rect.height + ic.length * 0.5f };
                            fi.vx = (dist_01(rng) - 0.5f) * 25.0f;
                            fi.vy = 40.0f;
                            fi.length = ic.length;
                            fi.base_width = ic.base_width;
                            fi.angle = 0.0f;
                            fi.ang_vel = (dist_01(rng) - 0.5f) * 180.0f;
                            falling_icicles.push_back(fi);
                        }
                    }
                    dp.icicles.clear();
                }

                if (dp.has_prev && !w.is_maximized && dt > 0.0001f) {
                    float old_w = dp.prev_rect.width;
                    float new_w = w.rect.width;
                    float old_h = dp.prev_rect.height;
                    float new_h = w.rect.height;
                    float old_x = dp.prev_rect.x;
                    float new_x = w.rect.x;
                    float old_right = old_x + old_w;
                    float new_right = new_x + new_w;
                    float eff_y = w.rect.y;
                    float bot_y = w.rect.y + w.rect.height;

                    bool is_resizing = (std::abs(new_w - old_w) > 0.2f) || (std::abs(new_h - old_h) > 0.2f);

                    if (!is_resizing) {
                        float vx = (new_x - old_x) / dt;
                        float vy = (w.rect.y - dp.prev_rect.y) / dt;
                        float speed = std::sqrt(vx * vx + vy * vy);
                        bool dir_flip = (vx * dp.prev_vx < 0.0f && std::abs(vx - dp.prev_vx) > 480.0f) ||
                                        (vy * dp.prev_vy < 0.0f && std::abs(vy - dp.prev_vy) > 480.0f);

                        if (dir_flip || speed > 460.0f) {
                            dp.shake_energy = std::min(3.5f, dp.shake_energy + (dir_flip ? 1.35f : dt * 4.2f));
                        } else {
                            dp.shake_energy = std::max(0.0f, dp.shake_energy - dt * 2.8f);
                        }
                        dp.prev_vx = vx;
                        dp.prev_vy = vy;

                        if (dp.shake_energy > 0.25f) {
                            if (!no_top_snow) {
                                int max_b = std::min((int)dp.heights.size() - 1, (int)(new_w / DRIFT_STEP) + 1);
                                float shed_rate = dp.shake_energy * 28.0f * dt;
                                for (int b = 0; b <= max_b; b++) {
                                    float lx = (float)b * DRIFT_STEP;
                                    float h_before = dp.heights[b];
                                    float h_vis = h_before * GetEdgeTaper(lx, new_w, true);

                                    if (h_before > 0.0f) {
                                        if (h_vis > 0.3f && flakes.size() < 3900) {
                                            int burst = (h_vis > 5.0f) ? 3 : 2;
                                            for (int k = 0; k < burst; k++) {
                                                if (dist_01(rng) < dp.shake_energy * 0.42f) {
                                                    bool is_dust = (k > 0 || dist_01(rng) < 0.65f);
                                                    Snowflake af{};
                                                    af.pos = { new_x + lx + (dist_01(rng) - 0.5f) * 8.0f,
                                                               eff_y - dist_01(rng) * std::max(2.0f, h_vis + 4.0f) };
                                                    af.vel = { -vx * 0.24f + (dist_01(rng) - 0.5f) * (is_dust ? 110.0f : 65.0f),
                                                               (is_dust ? 48.0f : 75.0f) + dist_01(rng) * 95.0f };
                                                    af.radius = is_dust ? (1.05f + dist_01(rng) * 0.85f) : (2.0f + dist_01(rng) * 1.5f);
                                                    af.sway_phase = dist_01(rng) * 6.28f;
                                                    af.sway_freq = is_dust ? 4.5f : 3.0f;
                                                    af.sway_amp = is_dust ? 16.0f : 8.0f;
                                                    af.alpha = is_dust ? (unsigned char)(165 + rng() % 65) : 245;
                                                    af.layer = 2;
                                                    af.is_avalanche = true;
                                                    af.is_ice = false;
                                                    af.life = 0.0f;
                                                    af.max_life = 8.0f;
                                                    flakes.push_back(af);
                                                }
                                            }
                                        }
                                        dp.heights[b] = std::max(0.0f, h_before - shed_rate);
                                    }
                                }
                            }

                            if (dp.shake_energy > 0.35f) {
                                for (size_t ic_i = 0; ic_i < dp.icicles.size(); ) {
                                    auto& ic = dp.icicles[ic_i];
                                    float break_prob = dp.shake_energy * (0.25f + ic.length * 0.04f);
                                    if (dist_01(rng) < break_prob) {
                                        if (ic.length >= 2.0f) {
                                            FallingIcicle fi{};
                                            fi.origin_win_id = w.id;
                                            fi.pos = { new_x + ic.rel_x, bot_y + ic.length * 0.5f };
                                            fi.vx = -vx * 0.20f + (dist_01(rng) - 0.5f) * 35.0f;
                                            fi.vy = std::max(25.0f, vy * 0.25f + 35.0f);
                                            fi.length = ic.length;
                                            fi.base_width = ic.base_width;
                                            fi.angle = (dist_01(rng) - 0.5f) * 12.0f;
                                            fi.ang_vel = (dist_01(rng) - 0.5f) * 240.0f;
                                            falling_icicles.push_back(fi);
                                        }
                                        dp.icicles[ic_i] = dp.icicles.back();
                                        dp.icicles.pop_back();
                                        continue;
                                    }
                                    ++ic_i;
                                }
                                dp.icicle_spawn_timer = 1.5f;
                            }

                            if (dp.shake_energy > 1.25f) {
                                for (auto& sm : snowmen) {
                                    if (!sm.falling && sm.win_id == w.id) {
                                        sm.falling = true;
                                        sm.fall_vx = -vx * 0.18f + (dist_01(rng) - 0.5f) * 40.0f;
                                        sm.fall_vy = 30.0f;
                                        sm.fall_ang_vel = (dist_01(rng) - 0.5f) * 200.0f;
                                    }
                                }
                            }
                        }
                    } else {
                        dp.shake_energy = std::max(0.0f, dp.shake_energy - dt * 5.0f);
                        dp.prev_vx = 0.0f;
                        dp.prev_vy = 0.0f;
                    }

                    if (is_resizing) {
                        float dx = new_x - old_x;

                        if (std::abs(dx) > 0.01f) {
                            for (auto& sm : snowmen) {
                                if (!sm.falling && sm.win_id == w.id) {
                                    sm.rel_x -= dx;
                                    if (sm.rel_x < 10.0f || sm.rel_x > (new_w - 10.0f)) {
                                        sm.falling = true;
                                        sm.fall_vx = (sm.rel_x < 10.0f) ? -18.0f : 18.0f;
                                        sm.fall_vy = 35.0f;
                                        sm.fall_ang_vel = (sm.rel_x < 10.0f) ? -140.0f : 140.0f;
                                    }
                                }
                            }
                            for (auto& ic : dp.icicles) {
                                ic.rel_x -= dx;
                            }
                        }

                        for (size_t ic_i = 0; ic_i < dp.icicles.size(); ) {
                            auto& ic = dp.icicles[ic_i];
                            if (ic.rel_x < 8.0f || ic.rel_x > new_w - 8.0f) {
                                if (ic.length >= 2.0f) {
                                    FallingIcicle fi{};
                                    fi.origin_win_id = w.id;
                                    fi.pos = { new_x + ic.rel_x, bot_y + ic.length * 0.5f };
                                    fi.vx = (ic.rel_x < 8.0f) ? -15.0f : 15.0f;
                                    fi.vy = 35.0f;
                                    fi.length = ic.length;
                                    fi.base_width = ic.base_width;
                                    fi.angle = 0.0f;
                                    fi.ang_vel = (dist_01(rng) - 0.5f) * 160.0f;
                                    falling_icicles.push_back(fi);
                                }
                                dp.icicles[ic_i] = dp.icicles.back();
                                dp.icicles.pop_back();
                                continue;
                            }
                            ++ic_i;
                        }

                        if (new_w > old_w + 18.0f) {
                            if (dx < -18.0f) {
                                PopulateWindowIcicles(dp.icicles, 0.0f, -dx, rng, true);
                            }
                            if (new_right > old_right + 18.0f) {
                                PopulateWindowIcicles(dp.icicles, old_w, new_w, rng, true);
                            }
                        }

                        if (!no_top_snow && !dp.is_maximized) {
                            if (std::abs(dx) > 0.01f) {
                                dp.left_shift_accum += dx;
                                int h_sz = (int)dp.heights.size();

                                while (dp.left_shift_accum <= -DRIFT_STEP && h_sz > 2) {
                                    dp.left_shift_accum += DRIFT_STEP;
                                    for (int b = h_sz - 1; b >= 1; b--) {
                                        dp.heights[b] = dp.heights[b - 1];
                                        dp.wave_shape[b] = dp.wave_shape[b - 1];
                                    }
                                    dp.heights[0] = 0.0f;
                                    dp.wave_shape[0] = EvalWaveAt(new_x, (unsigned int)w.id);
                                }

                                while (dp.left_shift_accum >= DRIFT_STEP && h_sz > 2) {
                                    dp.left_shift_accum -= DRIFT_STEP;
                                    float h = std::max(dp.heights[0], dp.heights[1]);
                                    if (h > 0.5f && flakes.size() < 3900) {
                                        int drops = (h > 7.5f) ? 3 : 2;
                                        for (int k = 0; k < drops; k++) {
                                            bool is_dust = (k > 1);
                                            Snowflake af{};
                                            af.pos = { old_x + (dist_01(rng) - 0.5f) * 5.0f, eff_y - dist_01(rng) * h };
                                            af.vel = { -14.0f - dist_01(rng) * 26.0f,
                                                       (is_dust ? 48.0f : 65.0f) + dist_01(rng) * 85.0f };
                                            af.radius = is_dust ? (1.1f + dist_01(rng) * 0.8f) : (2.0f + dist_01(rng) * 1.6f);
                                            af.sway_phase = dist_01(rng) * 6.28f;
                                            af.sway_freq = 3.2f;
                                            af.sway_amp = 9.0f;
                                            af.alpha = is_dust ? 195 : 245;
                                            af.layer = 2;
                                            af.is_avalanche = true;
                                            af.is_ice = false;
                                            af.life = 0.0f;
                                            af.max_life = 8.0f;
                                            flakes.push_back(af);
                                        }
                                    }
                                    for (int b = 0; b < h_sz - 1; b++) {
                                        dp.heights[b] = dp.heights[b + 1];
                                        dp.wave_shape[b] = dp.wave_shape[b + 1];
                                    }
                                    dp.heights[h_sz - 1] = 0.0f;
                                }
                            }

                            if (new_right < old_right - 0.1f) {
                                dp.pending_cut_right += (old_right - new_right);
                                int cur_end = std::clamp((int)(new_w / DRIFT_STEP), 0, (int)dp.heights.size() - 1);
                                while (dp.pending_cut_right >= DRIFT_STEP) {
                                    dp.pending_cut_right -= DRIFT_STEP;
                                    if (cur_end < (int)dp.heights.size()) {
                                        float lx = (float)cur_end * DRIFT_STEP;
                                        float h = dp.heights[cur_end];
                                        if (h > 0.5f && flakes.size() < 3900) {
                                            int drops = (h > 7.5f) ? 3 : 2;
                                            for (int k = 0; k < drops; k++) {
                                                bool is_dust = (k > 1);
                                                Snowflake af{};
                                                af.pos = { new_x + lx + (dist_01(rng) - 0.5f) * 5.0f, eff_y - dist_01(rng) * h };
                                                af.vel = { 14.0f + dist_01(rng) * 26.0f,
                                                           (is_dust ? 48.0f : 65.0f) + dist_01(rng) * 85.0f };
                                                af.radius = is_dust ? (1.1f + dist_01(rng) * 0.8f) : (2.0f + dist_01(rng) * 1.6f);
                                                af.sway_phase = dist_01(rng) * 6.28f;
                                                af.sway_freq = 3.2f;
                                                af.sway_amp = 9.0f;
                                                af.alpha = is_dust ? 195 : 245;
                                                af.layer = 2;
                                                af.is_avalanche = true;
                                                af.is_ice = false;
                                                af.life = 0.0f;
                                                af.max_life = 8.0f;
                                                flakes.push_back(af);
                                            }
                                        }
                                        dp.heights[cur_end] = 0.0f;
                                        cur_end++;
                                    }
                                }
                            } else if (new_right > old_right + 0.1f) {
                                dp.pending_cut_right = 0.0f;
                            }
                        }
                    }

                    if (dp.shake_energy <= 0.25f) {
                        for (auto& ic : dp.icicles) {
                            if (ic.length < ic.max_length) {
                                ic.length = std::min(ic.max_length, ic.length + ic.grow_rate * dt);
                            }
                        }
                        if (dp.icicle_spawn_timer > 0.0f) {
                            dp.icicle_spawn_timer -= dt;
                        } else if (w.rect.width >= 180.0f && (int)dp.icicles.size() < (int)(w.rect.width / 26.0f)) {
                            dp.icicle_spawn_timer = 0.9f + dist_01(rng) * 1.2f;
                            float cand_x = 14.0f + dist_01(rng) * std::max(10.0f, w.rect.width - 28.0f);
                            bool too_close = false;
                            for (const auto& existing : dp.icicles) {
                                if (std::abs(existing.rel_x - cand_x) < 13.0f) {
                                    too_close = true;
                                    break;
                                }
                            }
                            if (!too_close) {
                                Icicle nic{};
                                nic.rel_x = cand_x;
                                nic.max_length = 14.0f + dist_01(rng) * 22.0f;
                                nic.length = 0.4f;
                                nic.base_width = 2.0f + dist_01(rng) * 1.8f;
                                nic.grow_rate = 1.5f + dist_01(rng) * 1.8f;
                                dp.icicles.push_back(nic);
                            }
                        }
                    }
                }

                dp.prev_rect = w.rect;
                dp.rect = w.rect;
                dp.has_prev = true;
                dp.is_maximized = w.is_maximized;
                dp.alive = true;
            }
        }

        // Обрушение лавины и сосулек при закрытии окна или меню ПКМ / Пуск
        for (auto it = win_drifts.begin(); it != win_drifts.end(); ) {
            if (!it->second.alive) {
                const auto& dp = it->second;
                if (!dp.is_maximized && dp.rect.y > 4.0f) {
                    int max_idx = std::min((int)dp.heights.size() - 1, (int)(dp.rect.width / dp.step));
                    float eff_base_y = dp.rect.y;

                    for (int i = 1; i < max_idx - 1; i++) {
                        float lx = (float)i * dp.step;
                        float h = dp.heights[i] * GetEdgeTaper(lx, dp.rect.width, true);
                        if (h > 1.0f && flakes.size() < 3900) {
                            int count = (h > 7.5f) ? 3 : 2;
                            for (int k = 0; k < count; k++) {
                                bool is_dust = (k > 1);
                                Snowflake af{};
                                af.pos = { dp.rect.x + lx + (dist_01(rng) - 0.5f) * 5.0f, eff_base_y - dist_01(rng) * h };
                                af.vel = { (dist_01(rng) - 0.5f) * (is_dust ? 70.0f : 35.0f),
                                           (is_dust ? 55.0f : 75.0f) + dist_01(rng) * 95.0f };
                                af.radius = is_dust ? (1.1f + dist_01(rng) * 0.8f) : (2.0f + dist_01(rng) * 1.7f);
                                af.sway_phase = dist_01(rng) * 6.28f;
                                af.sway_freq = is_dust ? 4.2f : 3.0f;
                                af.sway_amp = is_dust ? 14.0f : 8.0f;
                                af.alpha = is_dust ? 195 : 245;
                                af.layer = 2;
                                af.is_avalanche = true;
                                af.is_ice = false;
                                af.life = 0.0f;
                                af.max_life = 8.0f;
                                flakes.push_back(af);
                            }
                        }
                    }

                    for (const auto& ic : dp.icicles) {
                        if (ic.length >= 2.0f) {
                            FallingIcicle fi{};
                            fi.origin_win_id = it->first;
                            fi.pos = { dp.rect.x + ic.rel_x, dp.rect.y + dp.rect.height + ic.length * 0.5f };
                            fi.vx = (dist_01(rng) - 0.5f) * 20.0f;
                            fi.vy = 45.0f;
                            fi.length = ic.length;
                            fi.base_width = ic.base_width;
                            fi.angle = 0.0f;
                            fi.ang_vel = (dist_01(rng) - 0.5f) * 180.0f;
                            falling_icicles.push_back(fi);
                        }
                    }
                }
                it = win_drifts.erase(it);
            } else {
                ++it;
            }
        }

        // Обновление падающих сосулек и разбивание при ударе
        for (size_t i = 0; i < falling_icicles.size(); ) {
            auto& fi = falling_icicles[i];
            float prev_y = fi.pos.y;
            fi.vy += 580.0f * dt;
            fi.pos.x += fi.vx * dt;
            fi.pos.y += fi.vy * dt;
            fi.angle += fi.ang_vel * dt;

            float tip_y = fi.pos.y + fi.length * 0.45f;
            bool shattered = false;

            for (size_t wi = 0; wi < current_windows.size(); wi++) {
                const auto& w = current_windows[wi];
                if (w.id == fi.origin_win_id || w.is_maximized || w.rect.y <= 4.0f) continue;
                if (IsPointOccludedByFrontWindows(fi.pos.x, w.rect.y, current_windows, wi)) continue;

                if (fi.pos.x >= w.rect.x + 6.0f && fi.pos.x <= (w.rect.x + w.rect.width - 6.0f) &&
                    prev_y <= w.rect.y + 4.0f && tip_y >= w.rect.y) {
                    shatter_icicle({ fi.pos.x, w.rect.y }, fi.length);
                    shattered = true;
                    break;
                }
            }

            if (!shattered && cur_dock.active &&
                fi.pos.x >= cur_dock.rect.x + 4.0f && fi.pos.x <= (cur_dock.rect.x + cur_dock.rect.width - 4.0f) &&
                prev_y <= cur_dock.rect.y + 4.0f && tip_y >= cur_dock.rect.y) {
                shatter_icicle({ fi.pos.x, cur_dock.rect.y }, fi.length);
                shattered = true;
            }

            if (!shattered && tip_y >= ground_y - 2.0f) {
                shatter_icicle({ fi.pos.x, ground_y - 2.0f }, fi.length);
                shattered = true;
            }

            if (shattered || fi.pos.y > ground_y + 20.0f) {
                falling_icicles[i] = falling_icicles.back();
                falling_icicles.pop_back();
                continue;
            }
            ++i;
        }

        // 4. Лепка шаров из сугробов, смена скинов снеговиков, перетаскивание и метание (F8)
        int snap_preview_idx = -1;
        if (interactive_now) {
            Vector2 mp = GetMousePosition();

            Rectangle panelRect = { (float)screenWidth * 0.5f - 310.0f, 14.0f, 620.0f, 38.0f };
            Rectangle btnPlow = { panelRect.x + 310.0f, panelRect.y + 5.0f, 120.0f, 28.0f };
            Rectangle btnClear = { panelRect.x + 438.0f, panelRect.y + 5.0f, 88.0f, 28.0f };
            Rectangle btnExit = { panelRect.x + 534.0f, panelRect.y + 5.0f, 76.0f, 28.0f };

            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                if (CheckCollisionPointRec(mp, btnPlow)) {
                    spawn_snowplow();
                } else if (CheckCollisionPointRec(mp, btnClear)) {
                    reset_all_snow();
                } else if (CheckCollisionPointRec(mp, btnExit)) {
                    interactive_toggle = false;
                    held_ball.active = false;
                } else if (!CheckCollisionPointRec(mp, panelRect)) {
                    bool handled_click = false;

                    // Клик по готовому 3-шаровому снеговику переключает скин и направление взгляда
                    for (auto& sm : snowmen) {
                        if (!sm.falling && sm.stage >= 3 &&
                            std::abs(mp.x - sm.world_pos.x) < 26.0f &&
                            mp.y >= (sm.world_pos.y - 68.0f) && mp.y <= (sm.world_pos.y + 8.0f)) {
                            sm.skin = (sm.skin + 1) % SNOWMAN_SKIN_COUNT;
                            sm.facing = -sm.facing;
                            sm.anim_pop = 0.65f;
                            handled_click = true;
                            break;
                        }
                    }

                    if (!handled_click) {
                        bool on_valid_drift = false;

                        for (size_t wi = 0; wi < current_windows.size(); wi++) {
                            const auto& w = current_windows[wi];
                            if (w.is_maximized || w.rect.y <= 4.0f) continue;

                            if (mp.x >= w.rect.x + 8.0f && mp.x <= (w.rect.x + w.rect.width - 8.0f) &&
                                mp.y >= (w.rect.y - 34.0f) && mp.y <= (w.rect.y + 14.0f)) {
                                if (!IsPointOccludedByFrontWindows(mp.x, w.rect.y, current_windows, wi)) {
                                    auto wit = win_drifts.find(w.id);
                                    if (wit != win_drifts.end()) {
                                        float lx = mp.x - w.rect.x;
                                        if (GetDriftHeightNear(wit->second.heights, DRIFT_STEP, lx, w.rect.width, true) >= 1.6f) {
                                            ScoopSnowAt(wit->second.heights, DRIFT_STEP, lx, 16.0f, 1.8f);
                                            on_valid_drift = true;
                                        }
                                    }
                                }
                                break;
                            }
                            if (mp.x >= w.rect.x && mp.x <= (w.rect.x + w.rect.width) &&
                                mp.y >= w.rect.y && mp.y <= (w.rect.y + w.rect.height)) {
                                break;
                            }
                        }

                        if (!on_valid_drift && cur_dock.active &&
                            mp.x >= cur_dock.rect.x + 8.0f && mp.x <= (cur_dock.rect.x + cur_dock.rect.width - 8.0f) &&
                            mp.y >= (cur_dock.rect.y - 34.0f) && mp.y <= (cur_dock.rect.y + 12.0f)) {
                            float lx = mp.x - cur_dock.rect.x;
                            if (GetDriftHeightNear(dock_drift, DRIFT_STEP, lx, cur_dock.rect.width, true) >= 1.6f) {
                                ScoopSnowAt(dock_drift, DRIFT_STEP, lx, 16.0f, 1.8f);
                                on_valid_drift = true;
                            }
                        }

                        if (!on_valid_drift && mp.y >= (ground_y - 44.0f) && mp.y <= (ground_y + 14.0f)) {
                            if (!cur_dock.active || mp.x < cur_dock.rect.x || mp.x > (cur_dock.rect.x + cur_dock.rect.width)) {
                                if (GetDriftHeightNear(bottom_drift, DRIFT_STEP, mp.x, (float)screenWidth, false) >= 1.6f) {
                                    ScoopSnowAt(bottom_drift, DRIFT_STEP, mp.x, 18.0f, 2.0f);
                                    on_valid_drift = true;
                                }
                            }
                        }

                        if (on_valid_drift) {
                            held_ball.active = true;
                            held_ball.pos = mp;
                            held_ball.vel = { 0.0f, 0.0f };
                            held_ball.radius = 4.2f;
                            held_ball.roll_accum = 0.0f;
                        }
                    }
                }
            }

            if (held_ball.active) {
                float move_dist = std::hypot(mp.x - held_ball.pos.x, mp.y - held_ball.pos.y);
                if (dt > 0.0001f) {
                    Vector2 inst_vel = { (mp.x - held_ball.pos.x) / dt, (mp.y - held_ball.pos.y) / dt };
                    float blend = IsMouseButtonReleased(MOUSE_BUTTON_LEFT) ? 0.65f : std::min(1.0f, dt * 24.0f);
                    held_ball.vel.x = held_ball.vel.x * (1.0f - blend) + inst_vel.x * blend;
                    held_ball.vel.y = held_ball.vel.y * (1.0f - blend) + inst_vel.y * blend;
                }
                held_ball.pos = mp;
                held_ball.roll_accum += move_dist;

                if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
                    bool over_any_drift = false;
                    float grow_step = (15.0f * dt) + (move_dist * 0.045f);
                    float scoop_depth = (12.0f * dt) + (move_dist * 0.04f);

                    for (size_t wi = 0; wi < current_windows.size(); wi++) {
                        const auto& w = current_windows[wi];
                        if (w.is_maximized || w.rect.y <= 4.0f) continue;
                        if (mp.x >= w.rect.x + 6.0f && mp.x <= (w.rect.x + w.rect.width - 6.0f) &&
                            mp.y >= (w.rect.y - 38.0f) && mp.y <= (w.rect.y + 18.0f)) {
                            if (!IsPointOccludedByFrontWindows(mp.x, w.rect.y, current_windows, wi)) {
                                auto wit = win_drifts.find(w.id);
                                if (wit != win_drifts.end()) {
                                    float lx = mp.x - w.rect.x;
                                    if (GetDriftHeightNear(wit->second.heights, DRIFT_STEP, lx, w.rect.width, true) > 0.5f) {
                                        over_any_drift = true;
                                        if (held_ball.radius < STANDARD_BALL_RADIUS) {
                                            ScoopSnowAt(wit->second.heights, DRIFT_STEP, lx, 16.0f, scoop_depth);
                                        }
                                    }
                                }
                            }
                            break;
                        }
                    }

                    if (!over_any_drift && cur_dock.active &&
                        mp.x >= cur_dock.rect.x + 6.0f && mp.x <= (cur_dock.rect.x + cur_dock.rect.width - 6.0f) &&
                        mp.y >= (cur_dock.rect.y - 38.0f) && mp.y <= (cur_dock.rect.y + 16.0f)) {
                        float lx = mp.x - cur_dock.rect.x;
                        if (GetDriftHeightNear(dock_drift, DRIFT_STEP, lx, cur_dock.rect.width, true) > 0.5f) {
                            over_any_drift = true;
                            if (held_ball.radius < STANDARD_BALL_RADIUS) {
                                ScoopSnowAt(dock_drift, DRIFT_STEP, lx, 16.0f, scoop_depth);
                            }
                        }
                    }

                    if (!over_any_drift && mp.y >= (ground_y - 46.0f) && mp.y <= (ground_y + 16.0f)) {
                        if (!cur_dock.active || mp.x < cur_dock.rect.x || mp.x > (cur_dock.rect.x + cur_dock.rect.width)) {
                            if (GetDriftHeightNear(bottom_drift, DRIFT_STEP, mp.x, (float)screenWidth, false) > 0.5f) {
                                over_any_drift = true;
                                if (held_ball.radius < STANDARD_BALL_RADIUS) {
                                    ScoopSnowAt(bottom_drift, DRIFT_STEP, mp.x, 18.0f, scoop_depth);
                                }
                            }
                        }
                    }

                    if (over_any_drift || held_ball.radius > 7.5f) {
                        held_ball.radius = std::min(STANDARD_BALL_RADIUS, held_ball.radius + (over_any_drift ? grow_step : grow_step * 0.45f));
                    }

                    if (move_dist > 1.5f && flakes.size() < 3900 && (rng() % 2 == 0)) {
                        Snowflake sp{};
                        sp.pos = { mp.x + (dist_01(rng) - 0.5f) * held_ball.radius * 1.6f,
                                   mp.y + (dist_01(rng) - 0.5f) * held_ball.radius * 1.6f };
                        sp.vel = { (dist_01(rng) - 0.5f) * 35.0f, 25.0f + dist_01(rng) * 35.0f };
                        sp.radius = 1.2f + dist_01(rng) * 0.9f;
                        sp.sway_phase = 0.0f;
                        sp.sway_freq = 3.0f;
                        sp.sway_amp = 4.0f;
                        sp.alpha = 220;
                        sp.layer = 2;
                        sp.is_avalanche = true;
                        sp.is_ice = false;
                        sp.life = 0.0f;
                        sp.max_life = 0.45f;
                        flakes.push_back(sp);
                    }

                    float best_d = 42.0f;
                    for (int idx = 0; idx < (int)snowmen.size(); idx++) {
                        const auto& sm = snowmen[idx];
                        if (sm.falling || sm.stage >= 3) continue;
                        float stack_y = sm.world_pos.y - (float)sm.stage * (sm.r1 * 1.55f);
                        float dx = std::abs(mp.x - sm.world_pos.x);
                        float dy = std::abs(mp.y - stack_y);
                        if (dx < 34.0f && dy < 44.0f) {
                            float d = std::sqrt(dx * dx + dy * dy);
                            if (d < best_d) {
                                best_d = d;
                                snap_preview_idx = idx;
                            }
                        }
                    }
                }

                if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
                    float throw_speed = std::hypot(held_ball.vel.x, held_ball.vel.y);
                    bool is_thrown = (throw_speed > 240.0f);
                    place_or_stack_ball(held_ball.pos, STANDARD_BALL_RADIUS, true, held_ball.vel, is_thrown);
                    held_ball.active = false;
                }
            }
        } else {
            held_ball.active = false;
        }

        // Обновление летящих и падающих шаров: сбивание снеговиков, сбивание сосулек, кратеры в сугробах!
        for (size_t i = 0; i < falling_balls.size(); ) {
            auto& fb = falling_balls[i];
            fb.age += dt;
            fb.vy += 620.0f * dt;
            fb.vx *= (1.0f - 0.30f * dt);

            Vector2 prev_pos = fb.pos;
            fb.pos.x += fb.vx * dt;
            fb.pos.y += fb.vy * dt;

            float fall_dist = fb.pos.y - fb.start_y;
            float impact_speed = std::hypot(fb.vx, fb.vy);
            float impact_power = std::clamp(impact_speed / 550.0f + std::max(0.0f, fall_dist) / 350.0f, 0.35f, 1.8f);
            bool breaks_on_hit = fb.is_thrown || (fall_dist > 95.0f) || (impact_speed > 280.0f);
            bool resolved = false;

            // 1. Удар о границы экрана
            if (fb.pos.x - fb.radius <= 0.0f || fb.pos.x + fb.radius >= (float)screenWidth || fb.pos.y - fb.radius <= 0.0f) {
                shatter_snowball(fb.pos, { fb.vx, fb.vy }, fb.radius, 0, false, false, impact_power);
                resolved = true;
            }

            // 2. Столкновение со снеговиками — брошенный шар СБИВАЕТ снеговика!
            if (!resolved) {
                for (int idx = 0; idx < (int)snowmen.size(); idx++) {
                    auto& sm = snowmen[idx];
                    if (sm.falling) continue;
                    float top_y = sm.world_pos.y - (float)sm.stage * (sm.r1 * 1.65f);
                    if (std::abs(fb.pos.x - sm.world_pos.x) < (sm.r1 + fb.radius + 4.0f) &&
                        fb.pos.y >= top_y - 8.0f && fb.pos.y <= sm.world_pos.y + 6.0f) {
                        if (!breaks_on_hit && sm.stage < 3) {
                            sm.stage++;
                            sm.anim_pop = 0.25f;
                        } else {
                            // Сбиваем снеговика с места!
                            sm.falling = true;
                            sm.on_dock = false;
                            sm.fall_vx = fb.vx * 0.48f + (dist_01(rng) - 0.5f) * 55.0f;
                            sm.fall_vy = -75.0f - dist_01(rng) * 45.0f;
                            sm.fall_ang_vel = ((fb.vx >= 0.0f) ? 1.0f : -1.0f) * (240.0f + dist_01(rng) * 140.0f);
                            shatter_snowball(fb.pos, { fb.vx, fb.vy }, fb.radius, 0, false, false, impact_power);
                        }
                        resolved = true;
                        break;
                    }
                }
            }

            // 3. Столкновение с падающими сосульками в воздухе
            if (!resolved && fb.is_thrown) {
                for (size_t fi_i = 0; fi_i < falling_icicles.size(); fi_i++) {
                    auto& fic = falling_icicles[fi_i];
                    if (std::hypot(fb.pos.x - fic.pos.x, fb.pos.y - fic.pos.y) < (fb.radius + fic.length * 0.5f + 6.0f)) {
                        shatter_icicle(fic.pos, fic.length);
                        shatter_snowball(fb.pos, { fb.vx, fb.vy }, fb.radius, 0, false, false, impact_power);
                        falling_icicles[fi_i] = falling_icicles.back();
                        falling_icicles.pop_back();
                        resolved = true;
                        break;
                    }
                }
            }

            // 4. Столкновение с сосульками, висящими снизу окон — брошенный шар СБИВАЕТ сосульки!
            if (!resolved && fb.is_thrown) {
                for (size_t wi = 0; wi < current_windows.size(); wi++) {
                    const auto& w = current_windows[wi];
                    if (w.is_maximized) continue;
                    auto wit = win_drifts.find(w.id);
                    if (wit == win_drifts.end()) continue;

                    float bot_y = w.rect.y + w.rect.height;
                    auto& ic_vec = wit->second.icicles;
                    for (size_t ic_i = 0; ic_i < ic_vec.size(); ic_i++) {
                        const auto& ic = ic_vec[ic_i];
                        if (ic.length < 2.0f) continue;
                        float ic_x = w.rect.x + ic.rel_x;
                        if (IsPointOccludedByFrontWindows(ic_x, bot_y + 2.0f, current_windows, wi)) continue;

                        if (std::abs(fb.pos.x - ic_x) <= (ic.base_width + fb.radius + 7.0f) &&
                            fb.pos.y >= (bot_y - 4.0f) && fb.pos.y <= (bot_y + ic.length + fb.radius + 4.0f)) {
                            FallingIcicle fi{};
                            fi.origin_win_id = w.id;
                            fi.pos = { ic_x, bot_y + ic.length * 0.5f };
                            fi.vx = fb.vx * 0.40f + (dist_01(rng) - 0.5f) * 40.0f;
                            fi.vy = std::max(30.0f, fb.vy * 0.25f);
                            fi.length = ic.length;
                            fi.base_width = ic.base_width;
                            fi.angle = (dist_01(rng) - 0.5f) * 20.0f;
                            fi.ang_vel = ((fb.vx >= 0.0f) ? 1.0f : -1.0f) * 320.0f;
                            falling_icicles.push_back(fi);

                            ic_vec[ic_i] = ic_vec.back();
                            ic_vec.pop_back();
                            wit->second.icicle_spawn_timer = 1.5f;

                            shatter_snowball(fb.pos, { fb.vx, fb.vy }, fb.radius, 0, false, false, impact_power);
                            resolved = true;
                            break;
                        }
                    }
                    if (resolved) break;
                }
            }

            // 5. Столкновение с плавающими окнами, меню ПКМ и Пуском (кратер на шапке + брызги!)
            if (!resolved) {
                for (size_t wi = 0; wi < current_windows.size(); wi++) {
                    const auto& w = current_windows[wi];
                    if (w.is_maximized || w.rect.y <= 4.0f) continue;

                    if (IsPointOccludedByFrontWindows(fb.pos.x, fb.pos.y, current_windows, wi)) {
                        continue;
                    }

                    if (fb.pos.x >= w.rect.x + 6.0f && fb.pos.x <= (w.rect.x + w.rect.width - 6.0f) &&
                        prev_pos.y <= w.rect.y + 6.0f && fb.pos.y >= w.rect.y - 6.0f && fb.vy > 0.0f) {
                        if (breaks_on_hit) {
                            shatter_snowball({ fb.pos.x, w.rect.y }, { fb.vx, fb.vy }, fb.radius, w.id, false, false, impact_power);
                        } else {
                            place_or_stack_ball({ fb.pos.x, w.rect.y - 2.0f }, fb.radius, false, { 0, 0 }, false);
                        }
                        resolved = true;
                        break;
                    }

                    if (fb.is_thrown && fb.age > 0.06f &&
                        fb.pos.x + fb.radius >= w.rect.x && fb.pos.x - fb.radius <= (w.rect.x + w.rect.width) &&
                        fb.pos.y + fb.radius >= w.rect.y && fb.pos.y - fb.radius <= (w.rect.y + w.rect.height)) {
                        shatter_snowball(fb.pos, { fb.vx, fb.vy }, fb.radius, 0, false, false, impact_power);
                        resolved = true;
                        break;
                    }
                }
            }

            // 6. Попадание на всплывающий нижний док (кратер + брызги)
            if (!resolved && cur_dock.active &&
                fb.pos.x >= cur_dock.rect.x + 6.0f && fb.pos.x <= (cur_dock.rect.x + cur_dock.rect.width - 6.0f) &&
                prev_pos.y <= cur_dock.rect.y + 6.0f && fb.pos.y >= cur_dock.rect.y - 6.0f && fb.vy > 0.0f) {
                if (breaks_on_hit) {
                    shatter_snowball({ fb.pos.x, cur_dock.rect.y }, { fb.vx, fb.vy }, fb.radius, 0, true, false, impact_power);
                } else {
                    place_or_stack_ball({ fb.pos.x, cur_dock.rect.y - 2.0f }, fb.radius, false, { 0, 0 }, false);
                }
                resolved = true;
            }

            // 7. Попадание в нижний сугроб у края экрана (кратер + снежные брызги!)
            if (!resolved && fb.pos.y >= ground_y - 3.0f) {
                if (breaks_on_hit) {
                    shatter_snowball({ fb.pos.x, ground_y - 4.0f }, { fb.vx, fb.vy }, fb.radius, 0, false, true, impact_power);
                } else {
                    place_or_stack_ball({ fb.pos.x, ground_y - 4.0f }, fb.radius, false, { 0, 0 }, false);
                }
                resolved = true;
            }

            if (resolved) {
                falling_balls[i] = falling_balls.back();
                falling_balls.pop_back();
                continue;
            }
            ++i;
        }

        // Обновление позиций снеговиков (включая опрокидывание при сбивании шаром)
        for (size_t i = 0; i < snowmen.size(); ) {
            auto& sm = snowmen[i];
            if (sm.anim_pop < 1.0f) sm.anim_pop = std::min(1.0f, sm.anim_pop + dt * 5.0f);

            if (!sm.falling) {
                if (sm.on_dock) {
                    if (!cur_dock.active) {
                        sm.on_dock = false;
                        sm.falling = true;
                        sm.fall_vy = 35.0f;
                        sm.fall_ang_vel = (dist_01(rng) - 0.5f) * 180.0f;
                    } else {
                        sm.world_pos = { cur_dock.rect.x + sm.rel_x, cur_dock.rect.y - sm.rel_y_offset };
                    }
                } else if (sm.win_id != 0) {
                    auto wit = win_drifts.find(sm.win_id);
                    if (wit == win_drifts.end() || wit->second.is_maximized || wit->second.rect.y <= 4.0f ||
                        sm.rel_x < 10.0f || sm.rel_x > (wit->second.rect.width - 10.0f)) {
                        sm.falling = true;
                        sm.fall_vy = 40.0f;
                        sm.fall_ang_vel = (dist_01(rng) - 0.5f) * 180.0f;
                    } else {
                        sm.world_pos = { wit->second.rect.x + sm.rel_x, wit->second.rect.y - sm.rel_y_offset };
                    }
                } else {
                    sm.world_pos = { sm.rel_x, ground_y - sm.rel_y_offset };
                }
            }

            if (sm.falling) {
                sm.fall_vy += 460.0f * dt;
                sm.world_pos.x += sm.fall_vx * dt;
                sm.world_pos.y += sm.fall_vy * dt;
                sm.fall_angle += sm.fall_ang_vel * dt;

                if (sm.world_pos.y >= ground_y - 4.0f) {
                    DepositSnowAt(bottom_drift, bottom_wave, DRIFT_STEP, sm.world_pos.x, (float)screenWidth + 16.0f, 2.5f, 38.0f);
                    for (int k = 0; k < 24 && flakes.size() < 3900; k++) {
                        Snowflake af{};
                        af.pos = { sm.world_pos.x + (dist_01(rng) - 0.5f) * 26.0f, ground_y - 10.0f - dist_01(rng) * 20.0f };
                        af.vel = { (dist_01(rng) - 0.5f) * 85.0f, -50.0f - dist_01(rng) * 70.0f };
                        af.radius = 2.2f + dist_01(rng) * 1.6f;
                        af.sway_phase = dist_01(rng) * 6.28f;
                        af.sway_freq = 3.0f;
                        af.sway_amp = 6.0f;
                        af.alpha = 245;
                        af.layer = 2;
                        af.is_avalanche = true;
                        af.is_ice = false;
                        af.life = 0.0f;
                        af.max_life = 0.75f;
                        flakes.push_back(af);
                    }
                    snowmen[i] = snowmen.back();
                    snowmen.pop_back();
                    continue;
                }
            }
            ++i;
        }

        // 5. Снегоуборочная машина: нагребание вала перед ковшом, выброс снега по дуге назад, вибрация и выхлоп
        if (!plow.active && !cur_dock.active) {
            float sum_h = 0.0f;
            int count_b = 0;
            for (int b = 10; b < bottom_bins - 10; b++) {
                sum_h += bottom_drift[b];
                count_b++;
            }
            float avg_h = sum_h / std::max(1, count_b);
            if (avg_h > 26.0f) {
                spawn_snowplow();
            }
        }

        if (plow.active) {
            plow.y = ground_y;
            plow.x += plow.dir * plow.speed * dt;
            plow.wheel_angle += plow.dir * (plow.speed * dt) / 8.6f;
            plow.beacon_phase += dt * 11.0f;
            plow.exhaust_timer += dt;

            int dir_i = (plow.dir > 0.0f) ? 1 : -1;
            float blade_x = plow.x + plow.dir * 42.0f;
            int b_center = (int)(blade_x / DRIFT_STEP);

            float max_ahead_h = 0.0f;
            for (int s = 0; s <= 7; s++) {
                int s_idx = b_center + dir_i * s;
                if (s_idx >= 0 && s_idx < bottom_bins) {
                    if (bottom_drift[s_idx] > max_ahead_h) max_ahead_h = bottom_drift[s_idx];
                }
            }
            if (max_ahead_h > 10.0f) {
                float intensity = std::clamp((max_ahead_h - 10.0f) / 22.0f, 0.30f, 1.0f);
                plow.vib_y = std::sin(time * 64.0f) * (1.7f * intensity);
            } else {
                plow.vib_y = 0.0f;
            }

            float smoke_interval = (max_ahead_h > 10.0f) ? 0.05f : 0.085f;
            if (plow.exhaust_timer >= smoke_interval) {
                plow.exhaust_timer = 0.0f;
                ExhaustPuff ep{};
                ep.pos = { plow.x + plow.dir * 1.5f - plow.dir * 3.0f, plow.y + plow.vib_y - 43.0f };
                ep.vel = { -plow.dir * (16.0f + dist_01(rng) * 14.0f), -20.0f - dist_01(rng) * 14.0f };
                ep.radius = 2.4f + dist_01(rng) * 1.3f;
                ep.life = 0.0f;
                ep.max_life = 0.85f + dist_01(rng) * 0.40f;
                exhaust_puffs.push_back(ep);
            }

            bool in_open_dock_zone = cur_dock.active &&
                                     (blade_x >= cur_dock.rect.x - 16.0f &&
                                      blade_x <= cur_dock.rect.x + cur_dock.rect.width + 16.0f);

            if (!in_open_dock_zone) {
                const float MAX_BERM_H = 44.0f;
                const int push_offsets[3] = { 3, 4, 5 };
                const float push_weights[3] = { 0.30f, 0.45f, 0.25f };

                for (int d = -4; d <= 2; d++) {
                    int idx = b_center + dir_i * d;
                    if (idx >= 0 && idx < bottom_bins) {
                        float cleared = bottom_drift[idx];
                        if (cleared > 0.35f) {
                            bottom_drift[idx] = 0.0f;

                            float pushed_snow = cleared * 0.65f;
                            for (int k = 0; k < 3; k++) {
                                int t_idx = idx + dir_i * push_offsets[k];
                                if (t_idx >= 0 && t_idx < bottom_bins) {
                                    float tx = (float)t_idx * DRIFT_STEP;
                                    bool target_under_dock = cur_dock.active &&
                                        (tx >= cur_dock.rect.x + 2.0f && tx <= cur_dock.rect.x + cur_dock.rect.width - 2.0f);
                                    if (!target_under_dock) {
                                        bottom_drift[t_idx] = std::min(MAX_BERM_H, bottom_drift[t_idx] + pushed_snow * push_weights[k]);
                                    }
                                }
                            }

                            if (cleared > 2.0f && flakes.size() < 3900) {
                                int burst = (cleared > 14.0f) ? 2 : 1;
                                for (int b_i = 0; b_i < burst; b_i++) {
                                    if (dist_01(rng) < 0.72f) {
                                        Snowflake spray{};
                                        spray.pos = { blade_x + (dist_01(rng) - 0.5f) * 6.0f,
                                                      ground_y - 4.0f - dist_01(rng) * std::min(16.0f, cleared) };
                                        spray.vel = { -plow.dir * (28.0f + dist_01(rng) * 62.0f),
                                                      -(95.0f + dist_01(rng) * 115.0f) };
                                        spray.radius = 1.4f + dist_01(rng) * 1.4f;
                                        spray.sway_phase = dist_01(rng) * 6.28f;
                                        spray.sway_freq = 2.8f;
                                        spray.sway_amp = 6.0f;
                                        spray.alpha = 248;
                                        spray.layer = 2;
                                        spray.is_avalanche = true;
                                        spray.is_ice = false;
                                        spray.life = 0.0f;
                                        spray.max_life = 2.0f;
                                        flakes.push_back(spray);
                                    }
                                }
                            }
                        }
                    }
                }

                for (size_t si = 0; si < snowmen.size(); ) {
                    if (snowmen[si].win_id == 0 && !snowmen[si].on_dock && !snowmen[si].falling &&
                        std::abs(snowmen[si].rel_x - blade_x) < 22.0f) {
                        for (int k = 0; k < 20 && flakes.size() < 3900; k++) {
                            Snowflake af{};
                            af.pos = { blade_x, ground_y - 15.0f - dist_01(rng) * 25.0f };
                            af.vel = { -plow.dir * (35.0f + dist_01(rng) * 65.0f), -90.0f - dist_01(rng) * 90.0f };
                            af.radius = 2.2f + dist_01(rng) * 1.5f;
                            af.sway_phase = 0.0f;
                            af.sway_freq = 2.0f;
                            af.sway_amp = 4.0f;
                            af.alpha = 245;
                            af.layer = 2;
                            af.is_avalanche = true;
                            af.is_ice = false;
                            af.life = 0.0f;
                            af.max_life = 1.5f;
                            flakes.push_back(af);
                        }
                        snowmen[si] = snowmen.back();
                        snowmen.pop_back();
                    } else {
                        ++si;
                    }
                }
            }

            if ((plow.dir < 0.0f && plow.x < -95.0f) || (plow.dir > 0.0f && plow.x > (float)screenWidth + 95.0f)) {
                plow.active = false;
                plow.vib_y = 0.0f;
            }
        }

        for (size_t i = 0; i < exhaust_puffs.size(); ) {
            auto& ep = exhaust_puffs[i];
            ep.life += dt;
            ep.pos.x += ep.vel.x * dt;
            ep.pos.y += ep.vel.y * dt;
            ep.radius += 6.2f * dt;
            if (ep.life >= ep.max_life) {
                exhaust_puffs[i] = exhaust_puffs.back();
                exhaust_puffs.pop_back();
                continue;
            }
            ++i;
        }

        // 6. Обновление падающих снежинок, пыли и ледяных осколков
        float wind = std::sin(time * 0.35f) * 18.0f;

        for (size_t i = 0; i < flakes.size(); ) {
            auto& f = flakes[i];

            if (f.is_avalanche) {
                f.life += dt;
                f.vel.y += 420.0f * dt;
                f.pos.x += (f.vel.x + std::sin(time * f.sway_freq + f.sway_phase) * f.sway_amp) * dt;
                f.pos.y += f.vel.y * dt;
            } else {
                float sway_vx = std::cos(time * f.sway_freq + f.sway_phase) * f.sway_amp;
                f.pos.x += (wind + sway_vx) * dt;
                f.pos.y += f.vel.y * dt;
            }

            bool absorbed = false;

            if (!f.is_avalanche) {
                for (size_t wi = 0; wi < current_windows.size(); wi++) {
                    const auto& w = current_windows[wi];
                    if (w.is_maximized || w.rect.y <= 4.0f) continue;

                    if (f.pos.x >= w.rect.x && f.pos.x <= (w.rect.x + w.rect.width)) {
                        float eff_y = w.rect.y;
                        if (f.pos.y >= (eff_y - 5.0f) && f.pos.y <= (eff_y + 5.0f)) {
                            if (IsPointOccludedByFrontWindows(f.pos.x, eff_y, current_windows, wi)) {
                                break;
                            }
                            if (f.layer == 0 && (rng() % 2 == 0)) {
                                break;
                            }
                            auto it = win_drifts.find(w.id);
                            if (it != win_drifts.end() && it->second.shake_energy <= 0.25f) {
                                DepositSnowAt(it->second.heights, it->second.wave_shape,
                                              DRIFT_STEP, f.pos.x - w.rect.x, w.rect.width,
                                              0.65f, 19.0f);
                            }
                            absorbed = true;
                            break;
                        }
                        if (f.layer > 0 && f.pos.y > (eff_y + 5.0f) && f.pos.y < (w.rect.y + w.rect.height)) {
                            if (!IsPointOccludedByFrontWindows(f.pos.x, f.pos.y, current_windows, wi)) {
                                break;
                            }
                        }
                    }
                }
            }

            if (!absorbed && cur_dock.active &&
                f.pos.x >= cur_dock.rect.x && f.pos.x <= (cur_dock.rect.x + cur_dock.rect.width) &&
                f.pos.y >= (cur_dock.rect.y - 5.0f) && f.pos.y <= (cur_dock.rect.y + 6.0f)) {
                if (!f.is_ice) {
                    DepositSnowAt(dock_drift, dock_wave, DRIFT_STEP, f.pos.x - cur_dock.rect.x, cur_dock.rect.width,
                                  f.is_avalanche ? 0.65f : 0.48f, 20.0f);
                }
                absorbed = true;
            }

            if (!absorbed && f.pos.y >= ground_y - 5.0f) {
                bool under_dock = cur_dock.active &&
                                  (f.pos.x >= cur_dock.rect.x + 2.0f && f.pos.x <= cur_dock.rect.x + cur_dock.rect.width - 2.0f);
                if (!f.is_ice && !under_dock && (!plow.active || std::abs(f.pos.x - plow.x) > 48.0f)) {
                    DepositSnowAt(bottom_drift, bottom_wave, DRIFT_STEP, f.pos.x, (float)screenWidth + 16.0f,
                                  f.is_avalanche ? 0.68f : 0.28f, 36.0f);
                }
                absorbed = true;
            }

            if (absorbed || f.pos.y > ground_y + 10.0f || (f.is_avalanche && f.life >= f.max_life)) {
                if (f.is_avalanche) {
                    flakes[i] = flakes.back();
                    flakes.pop_back();
                    continue;
                } else {
                    f = make_flake(true);
                }
            }
            ++i;
        }

        // ====================================================================
        // ОТРИСОВКА КАДРА
        // ====================================================================
        BeginDrawing();
        ClearBackground(BLANK);

        // 1. Сугробы и сосульки на плавающих окнах, меню ПКМ и Пуске
        for (size_t wi = 0; wi < current_windows.size(); wi++) {
            const auto& w = current_windows[wi];
            if (w.is_maximized) continue;

            auto it = win_drifts.find(w.id);
            if (it == win_drifts.end()) continue;

            if (w.rect.y > 4.0f) {
                DrawSculptedDrift(it->second.heights, DRIFT_STEP, w.rect.x, w.rect.y, w.rect.width,
                                  current_windows, wi, false);
            }
            if (!it->second.icicles.empty()) {
                DrawWindowIcicles(it->second.icicles, w.rect, ground_y, current_windows, wi);
            }
        }

        // 2. Падающие сосульки
        for (const auto& fi : falling_icicles) {
            DrawFallingIcicle(fi);
        }

        // 3. Снеговики с 10 скинами
        for (int idx = 0; idx < (int)snowmen.size(); idx++) {
            const auto& sm = snowmen[idx];
            if (!sm.falling && sm.win_id != 0) {
                size_t wi = current_windows.size();
                for (size_t k = 0; k < current_windows.size(); k++) {
                    if (current_windows[k].id == sm.win_id) {
                        wi = k;
                        break;
                    }
                }
                if (wi < current_windows.size() &&
                    IsPointOccludedByFrontWindows(sm.world_pos.x, sm.world_pos.y - 16.0f, current_windows, wi)) {
                    continue;
                }
            }
            DrawSnowman(sm, sm.world_pos, time, idx == snap_preview_idx);
        }

        // 4. Летящие и падающие снежные шары
        for (const auto& fb : falling_balls) {
            DrawSnowballUnit(fb.pos, fb.radius, false);
        }

        // 5. Нижний волнистый сугроб, кратеры и снежный вал перед ковшом
        DrawSculptedDrift(bottom_drift, DRIFT_STEP, -4.0f, ground_y, (float)screenWidth + 8.0f,
                          current_windows, 0, true);

        // 6. Сугроб над поднявшейся нижней панелькой (доком)
        if (cur_dock.active && !dock_drift.empty()) {
            DrawSculptedDrift(dock_drift, DRIFT_STEP, cur_dock.rect.x, cur_dock.rect.y, cur_dock.rect.width,
                              current_windows, 0, false);
        }

        // 7. Выхлопной дым и псевдо-3D снегоуборочная машина
        for (const auto& ep : exhaust_puffs) {
            float t = std::clamp(ep.life / ep.max_life, 0.0f, 1.0f);
            unsigned char ea = (unsigned char)((1.0f - t) * 125.0f);
            DrawCircleV(ep.pos, ep.radius, Color{ 195, 204, 216, ea });
        }
        DrawSnowplowTruck(plow);

        // 8. Снежинки, снежные брызги и ледяные осколки
        for (const auto& f : flakes) {
            unsigned char a = f.alpha;
            if (f.is_avalanche && f.max_life - f.life < 0.35f) {
                float fade = std::clamp((f.max_life - f.life) / 0.35f, 0.0f, 1.0f);
                a = (unsigned char)((float)f.alpha * fade);
            }
            float d = f.radius * 2.0f;
            Color tint = f.is_ice ? Color{ 205, 238, 255, a } : Color{ 255, 255, 255, a };
            DrawTexturePro(flakeTex,
                           Rectangle{ 0, 0, 32, 32 },
                           Rectangle{ f.pos.x, f.pos.y, d, d },
                           Vector2{ f.radius, f.radius },
                           0.0f,
                           tint);
        }

        // 9. Скатываемый из сугроба шар в руке
        if (held_ball.active) {
            DrawSnowballUnit(held_ball.pos, held_ball.radius, true);
            if (held_ball.radius < STANDARD_BALL_RADIUS - 0.3f) {
                DrawCircleLines((int)held_ball.pos.x, (int)held_ball.pos.y, STANDARD_BALL_RADIUS + 1.5f,
                                Color{ 165, 215, 255, 160 });
            }
        }

        // 10. Панель управления в интерактивном режиме (F8 или Ctrl+Shift)
        if (interactive_now) {
            Rectangle panelRect = { (float)screenWidth * 0.5f - 310.0f, 14.0f, 620.0f, 38.0f };
            Rectangle btnPlow = { panelRect.x + 310.0f, panelRect.y + 5.0f, 120.0f, 28.0f };
            Rectangle btnClear = { panelRect.x + 438.0f, panelRect.y + 5.0f, 88.0f, 28.0f };
            Rectangle btnExit = { panelRect.x + 534.0f, panelRect.y + 5.0f, 76.0f, 28.0f };

            DrawRectangleRounded(Rectangle{ panelRect.x - 1.5f, panelRect.y - 1.5f, panelRect.width + 3.0f, panelRect.height + 3.0f }, 0.38f, 6, Color{ 120, 175, 240, 180 });
            DrawRectangleRounded(panelRect, 0.35f, 6, Color{ 20, 26, 36, 240 });
            DrawText("Scoop snow: Stack / Throw / Skin", (int)panelRect.x + 14, (int)panelRect.y + 11, 16, WHITE);

            DrawRectangleRounded(btnPlow, 0.35f, 4, Color{ 235, 155, 25, 240 });
            DrawText("Plow [F9]", (int)btnPlow.x + 18, (int)btnPlow.y + 6, 15, Color{ 20, 20, 20, 255 });

            DrawRectangleRounded(btnClear, 0.35f, 4, Color{ 65, 85, 115, 240 });
            DrawText("Clear [F10]", (int)btnClear.x + 8, (int)btnClear.y + 6, 15, WHITE);

            DrawRectangleRounded(btnExit, 0.35f, 4, Color{ 195, 55, 65, 240 });
            DrawText("Done [F8]", (int)btnExit.x + 8, (int)btnExit.y + 6, 15, WHITE);
        } else if (hint_timer > 0.0f) {
            unsigned char ha = (unsigned char)(std::min(1.0f, hint_timer) * 200.0f);
            Rectangle hRect = { (float)screenWidth * 0.5f - 235.0f, 12.0f, 470.0f, 28.0f };
            DrawRectangleRounded(hRect, 0.4f, 6, Color{ 18, 24, 34, ha });
            DrawText("[F8] Snowman Mode | [F9] Snowplow | [F10] Clear Snow",
                     (int)hRect.x + 16, (int)hRect.y + 7, 14, Color{ 240, 248, 255, ha });
        }

        EndDrawing();
    }

    UnloadTexture(flakeTex);
    CloseWindow();
#if defined(_WIN32)
    if (hDwm) FreeLibrary(hDwm);
#endif
    return 0;
}
