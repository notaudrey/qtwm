/**
 * Compliant to X11/ICCCM/EWMH specs? Who does that?
 */

#include <xcb/xcb.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <xcb/xinerama.h>

#include "config.h"
#include "list.h"

#ifdef DEBUG
#define PDEBUG(...) \
    fprintf(stderr, "qtwm: "); fprintf(stderr, __VA_ARGS__); fprintf(stderr, "\n");
#else
#define PDEBUG(Args...)
#endif

#define XCB_MOVE        XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y
#define XCB_MOVE_RESIZE XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT
#define XCB_RESIZE      XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT

/*
 * Based off of tinywm-xcb:
 * https://github.com/rtyler/tinywm-ada/blob/master/tinywm-xcb.c
 *
 * Uses some code from mcwm:
 * http://hack.org/mc/hacks/mcwm/
 *
 * Also uses some code from monsterwm-xcb
 * https://github.com/Cloudef/monsterwm-xcb/blob/master/monsterwm.c
 *
 * As well as some bspwm
 * https://github.com/baskerville/bspwm/blob/master/bspwm.c
 *
 * And some dwm
 * http://git.suckless.org/dwm/tree/dwm.c
 */

/*
 * Structs
 */

struct client_win {
    /* Window ID */
    xcb_drawable_t id;
    /* x/y coords */
    int16_t x;
    int16_t y;
    /* Width and height */
    uint16_t w;
    uint16_t h;
    /* Window item */
    struct item* window_item;
};

/*
 * Globals
 */

/* Connection to the X display */
xcb_connection_t* dpy;

/* Screen */
xcb_screen_t* screen;

/* List of current windows */
struct item* winlist = NULL;

/*
 * Forward declarations
 */

void new_window(xcb_window_t window);
void set_border_color(xcb_window_t window, bool focus);
void set_border_width(xcb_window_t window);
struct client_win* setup_window(xcb_window_t window);
void forgetwindow(xcb_window_t window);
bool get_geom(xcb_drawable_t window, int16_t* x, int16_t* y, uint16_t* w, uint16_t* h);
void move_window(xcb_drawable_t window, int16_t x, int16_t y);
void resize_window(xcb_drawable_t window, uint16_t w, uint16_t h);
void move_resize_window(xcb_drawable_t window, int16_t x, int16_t y, uint16_t w, uint16_t h);
void configure_request(xcb_configure_request_event_t* e);
struct client_win* find_client(xcb_drawable_t window);

/**
 * Actual functions
 */

int main(int argc, char** argv) {
    // This is to do with window interaction
    uint32_t values[3];

    xcb_drawable_t win;
    // Root window
    xcb_drawable_t root;

    // X event(s)
    xcb_generic_event_t* ev;
    // ???
    xcb_get_geometry_reply_t* geom;
    // For making moving easier
    xcb_button_press_event_t* bstart;
    int16_t x = 0, y = 0;
    uint16_t w = 0, h = 0;

    // For events
    uint32_t mask = 0;
    uint32_t not_values[2];


    // Connect to the display
    dpy = xcb_connect(NULL, NULL);

    if(xcb_connection_has_error(dpy)) {
        // Unable to connect to display, or similar(?)
        PDEBUG("Error connecting to X display!");
        return 1;
    }

    // ???(get_information_about_display(display)).???;
    screen = xcb_setup_roots_iterator(xcb_get_setup(dpy)).data;
    // Get the root window of the screen
    root = screen->root;

    // Xinerama, for multihead
#ifdef MULTIHEAD
    bool xinerama_is_active = false;
    if(xcb_get_extension_data(dpy, &xcb_xinerama_id)->present) {
        // Xinerama might actually be there
        xcb_xinerama_is_active_reply_t* xia = xcb_xinerama_is_active_reply(dpy, xcb_xinerama_is_active(dpy), NULL);
        if(xia) {
            xinerama_is_active = xia->state;
            free(xia);
        }
    }
    if(xinerama_is_active) {
        // Multihead?
        xcb_xinerama_query_screens_reply_t* xsq = xcb_xinerama_query_screens_reply(dpy, xcb_xinerama_query_screens(dpy), NULL);
        xcb_xinerama_screen_info_t* xsi = xcb_xinerama_query_screens_screen_info(xsq);
        int32_t n = xcb_xinerama_query_screens_screen_info_length(xsq);
        for(int32_t i = 0; i < n; i++) {
            xcb_xinerama_screen_info_t info = xsi[i];
            xcb_rectangle_t rect = (xcb_rectangle_t) { info.x_org, info.y_org, info.width, info.height };
            // Add monitor
            PDEBUG("%dx%d+%dx%d", rect.x, rect.y, rect.width, rect.height);
        }
        free(xsq);
    } else {
        PDEBUG("Warning: Xinerama is inactive.");
    }
#endif

    // Grab modifiers
    /*xcb_grab_key(dpy, 1, root, MODIFIER_MASK, XCB_NO_SYMBOL,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);*/
    // Grab move button
    xcb_grab_button(dpy, 0, root, XCB_EVENT_MASK_BUTTON_PRESS |
                    XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC, root, XCB_NONE, MOVE_MOUSE_BUTTON,
                    MODIFIER_MASK);
    // Grab resize button
    xcb_grab_button(dpy, 0, root, XCB_EVENT_MASK_BUTTON_PRESS |
                    XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC, root, XCB_NONE, RESIZE_MOUSE_BUTTON,
                    MODIFIER_MASK);

    // Do this to the root window so that new windows show up in XCB_CREATE_NOTIFY
    mask = XCB_CW_EVENT_MASK;
    not_values[0] = XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;
    xcb_change_window_attributes_checked(dpy, root, mask, not_values);

    xcb_flush(dpy);

    // Main loop
    // while(true), essentially
    for(;;) {
        // Wait for next event from XCB
        ev = xcb_wait_for_event(dpy);
        // Magic?
        switch(ev->response_type & ~0x80) {
        // Button pressed
        case XCB_BUTTON_PRESS: {
            // Button press event.
            xcb_button_press_event_t *e;
            // Typecast obv.
            e = (xcb_button_press_event_t*) ev;
            // Get clicked window
            win = e->child;
            // Stacking
            values[0] = XCB_STACK_MODE_ABOVE;
            // Get geometry and stuff from interacting with the window
            xcb_configure_window(dpy, win, XCB_CONFIG_WINDOW_STACK_MODE, values);
            geom = xcb_get_geometry_reply(dpy, xcb_get_geometry(dpy, win), NULL);
            // Move mouse pointer as needed
            if(e->detail == 1) {
                values[2] = 1;
                xcb_warp_pointer(dpy, XCB_NONE, win, 0, 0, 0, 0, 1, 1);
            } else {
                values[2] = 3;
                xcb_warp_pointer(dpy, XCB_NONE, win, 0, 0, 0, 0, geom->width, geom->height);
            }
            // Grab for necessary events
            xcb_grab_pointer(dpy, 0, root, XCB_EVENT_MASK_BUTTON_RELEASE |
                             XCB_EVENT_MASK_BUTTON_MOTION | XCB_EVENT_MASK_POINTER_MOTION_HINT,
                             XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, root, XCB_NONE, XCB_CURRENT_TIME);
            // Flush
            bstart = e;
            get_geom(win, &x, &y, &w, &h);
            xcb_flush(dpy);
        }
        break;
        // Mouse moved
        case XCB_MOTION_NOTIFY: {
            xcb_query_pointer_reply_t *pointer;
            pointer = xcb_query_pointer_reply(dpy, xcb_query_pointer(dpy, root), 0);
            // Window movement
            if(values[2] == 1) {
                geom = xcb_get_geometry_reply(dpy, xcb_get_geometry(dpy, win), NULL);
                int32_t xdiff = pointer->root_x - bstart->root_x;
                int32_t ydiff = pointer->root_y - bstart->root_y;
                move_window(win, bstart->root_x + xdiff, bstart->root_y + ydiff);
                xcb_flush(dpy);
            }
            // Window resizing
            else if(values[2] == 3) {
                geom = xcb_get_geometry_reply(dpy, xcb_get_geometry(dpy, win), NULL);
                values[0] = pointer->root_x - geom->x;
                values[1] = pointer->root_y - geom->y;
                xcb_configure_window(dpy, win, XCB_RESIZE, values);
                xcb_flush(dpy);
            }
        }
        break;
        // Mouse released
        case XCB_BUTTON_RELEASE:
            // Return the pointer
            xcb_ungrab_pointer(dpy, XCB_CURRENT_TIME);
            xcb_flush(dpy);
            break;
        // Window wants to be mapped
        case XCB_MAP_REQUEST: {
            xcb_map_request_event_t *e;

            PDEBUG("event: Map request");
            e = (xcb_map_request_event_t*) ev;
            new_window(e->window);
        }
        break;
        case XCB_CREATE_NOTIFY: {
            xcb_create_notify_event_t *e;

            PDEBUG("event: Create notify");
            e = (xcb_create_notify_event_t*) ev;
            new_window(e->window);
        }
        break;
        case XCB_DESTROY_NOTIFY: {
            PDEBUG("event: destroy notification");
            xcb_destroy_notify_event_t *e;

            e = (xcb_destroy_notify_event_t*) ev;

            // Adjust window focus

            // Forget about this windodw
            forgetwindow(e->window);
            xcb_flush(dpy);
        }
        break;
        case XCB_CONFIGURE_REQUEST: {
            configure_request((xcb_configure_request_event_t*) ev);
        }
        break;
        case XCB_FOCUS_IN:
        case XCB_FOCUS_OUT: {
            int32_t response_type = ev->response_type & ~0x80;
            if(response_type == XCB_FOCUS_IN) {
                xcb_focus_in_event_t* e = (xcb_focus_in_event_t*) ev;
                set_border_color(e->event, true);
                PDEBUG("Focusing on a window!");
            } else {
                xcb_focus_out_event_t* e = (xcb_focus_out_event_t*) ev;
                set_border_color(e->event, false);
                PDEBUG("Unfocusing on a window!");
            }
        }
        break;
        }
        free(ev);
    }

    return 0;
}

void new_window(xcb_window_t window) {
    // Figure out what window we're placing
    struct client_win* client;
    /*
     * If we already manage this window, skip. There's probably a good reason for why.
     */

    client = setup_window(window);
    if(client == NULL) {
        PDEBUG("Couldn't set up window: Out of memory!");
        return;
    }

    // Determine if it needs to be remapped or not.
    // Set up borders/etc., add to list of managed windows
    // xcb_map_window(connection, id)
    xcb_map_window(dpy, client->id);
    // "Declare window normal"? Some ICCCM thing it looks like
    // Move pointer as necessary
    // xcb_flush(conn);
    xcb_flush(dpy);
}

struct client_win* setup_window(xcb_window_t window) {
    uint32_t values[2];
    uint32_t mask = 0;
    struct item* item;
    struct client_win* client;

    // Set border color
    set_border_color(window, false);
    set_border_width(window);

    mask = XCB_CW_EVENT_MASK;
    values[0] = XCB_EVENT_MASK_ENTER_WINDOW;
    xcb_change_window_attributes(dpy, window, mask, values);

    // Add this window to the X Save Set
    xcb_change_save_set(dpy, XCB_SET_MODE_INSERT, window);

    xcb_flush(dpy);

    // Remember window and store a few things about it

    item = additem(&winlist);

    if(item == NULL) {
        PDEBUG("Out of memory!");
        return NULL;
    }

    client = malloc(sizeof(struct client_win));
    if(client == NULL) {
        PDEBUG("Out of memory!");
        return NULL;
    }

    item->data = client;

    // Initialize client
    client->id = window;
    client->x = 0;
    client->y = 0;
    client->w = 0;
    client->h = 0;
    client->window_item = item;

    // Get geometry, store in client
    if(!get_geom(window, &client->x, &client->y, &client->w, &client->h)) {
        PDEBUG("Couldn't get geometry for initial window setup!");
    }

    // ICCCM nonsense would go here.

    return client;
}

void set_border_color(xcb_window_t window, bool focus) {
    uint32_t values[1];
    values[0] = focus ? BORDER_COLOR_FOCUSED : BORDER_COLOR_UNFOCUSED;
    xcb_change_window_attributes(dpy, window, XCB_CW_BORDER_PIXEL, values);
}

void set_border_width(xcb_window_t window) {
    uint32_t values[1];
    values[0] = BORDER_WIDTH;
    xcb_change_window_attributes(dpy, window, XCB_CONFIG_WINDOW_BORDER_WIDTH, values);
}

void forgetwindow(xcb_window_t window) {
    struct item* item;
    struct client_win* client;

    for(item = winlist; item != NULL; item = item->next) {
        client = item->data;
        if(window == client->id) {
            PDEBUG("Found client. Forgetting...");

            // Workspaces (as needed)

            free(item->data);
            delitem(&winlist, item);
        }
    }
}

bool get_geom(xcb_drawable_t window, int16_t* x, int16_t* y, uint16_t* w, uint16_t* h) {
    xcb_get_geometry_reply_t* geom;

    geom = xcb_get_geometry_reply(dpy, xcb_get_geometry(dpy, window), NULL);
    if(geom == NULL) {
        PDEBUG("Unable to get window geometry!");
        return false;
    }
    PDEBUG("Got geometry: %dx%d+%dx%d", geom->x, geom->y, geom->width, geom->height)

    *x = geom->x;
    *y = geom->y;
    *w = geom->width;
    *h = geom->height;

    free(geom);

    return true;
}

void move_window(xcb_drawable_t window, int16_t x, int16_t y) {
    uint32_t values[2] = { x, y };
    PDEBUG("Moving window to (%d %d)", x, y);
    xcb_configure_window(dpy, window, XCB_MOVE, values);
    xcb_flush(dpy);
}

void resize_window(xcb_drawable_t window, uint16_t w, uint16_t h) {
    uint32_t values[2] = { w, h };
    PDEBUG("Resizing window to (%d, %d)!", w, h);
    xcb_configure_window(dpy, window, XCB_RESIZE, values);
    xcb_flush(dpy);
}

void move_resize_window(xcb_drawable_t window, int16_t x, int16_t y, uint16_t w, uint16_t h) {
    uint32_t values[4] = { x, y, w, h };
    PDEBUG("Changing geometry to %dx%d+%dx%d!", x, y, w, h);
    xcb_configure_window(dpy, window, XCB_MOVE_RESIZE, values);
    xcb_flush(dpy);
}

void configure_request(xcb_configure_request_event_t* e) {
    struct client_win* client;

    if((client = find_client(e->window))) {
        PDEBUG("X asked us to configure a window. We should implement that at some point.");
        PDEBUG("configure_request: Not implemented");
        // Configurations. Taken from looking at mcwm again.
        //
        // XCB_CONFIG_WINDOW_WIDTH
        // XCB_CONFIG_WINDOW_HEIGHT
        // XCB_CONFIG_WINDOW_SIBLING
        // XCB_CONFIG_WINDOW_STACK_MODE
    } else {
        // Unmapped window
        PDEBUG("X requested that we configure a window we don't know about yet!");
    }
}

struct client_win* find_client(xcb_drawable_t window) {
    struct client_win* win;
    struct item* item;

    for(item = winlist; item != NULL; item = item->next) {
        win = item->data;
        if(win->id == window) {
            PDEBUG("Found client");
            return win;
        }
    }

    return NULL;
}

