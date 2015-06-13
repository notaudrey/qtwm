#include <xcb/xcb.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "list.h"

#ifdef DEBUG
#define PDEBUG(Args...) \
    fprintf(stderr, "qtwm: "); fprintf(stderr, ##Args);
#else
#define PDEBUG(Args...)
#endif

/*
 * Based off of https://github.com/rtyler/tinywm-ada/blob/master/tinywm-xcb.c
 * Uses some code from mcwm (http://hack.org/mc/hacks/mcwm/)
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

xcb_screen_t* screen;

/*
 * Forward declarations
 */

void new_window(xcb_window_t window);
struct client_win* setup_window(xcb_window_t window);
bool get_geom(xcb_drawable_t window, int16_t* x, int16_t* y, uint16_t* w, uint16_t* h);
void move_window(xcb_drawable_t window, uint16_t x, uint16_t y);

/**
 * Actual functions
 */

int main(int argc, char** argv) {
    // This is to do with window interaction?
    uint32_t values[3];

    xcb_drawable_t win;
    // Root window
    xcb_drawable_t root;

    // X event(s)
    xcb_generic_event_t *ev;
    // ???
    xcb_get_geometry_reply_t *geom;

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

    // Grab stuff. Some key press, and mouse clicks
    xcb_grab_key(dpy, 1, root, XCB_MOD_MASK_2, XCB_NO_SYMBOL,
                 XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
    xcb_grab_button(dpy, 0, root, XCB_EVENT_MASK_BUTTON_PRESS |
                    XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC, root, XCB_NONE, 1,
                    XCB_MOD_MASK_1);
    xcb_grab_button(dpy, 0, root, XCB_EVENT_MASK_BUTTON_PRESS |
                    XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
                    XCB_GRAB_MODE_ASYNC, root, XCB_NONE, 3,
                    XCB_MOD_MASK_1);
    // Flush the display
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
                values[0] = (pointer->root_x + geom->width > screen->width_in_pixels) ?
                            (screen->width_in_pixels - geom->width) : pointer->root_x;
                values[1] = (pointer->root_y + geom->height > screen->height_in_pixels)?
                            (screen->height_in_pixels - geom->height):pointer->root_y;
                // Do the movement
                xcb_configure_window(dpy, win, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
                xcb_flush(dpy);
            }
            // Window resizing
            else if(values[2] == 3) {
                geom = xcb_get_geometry_reply(dpy, xcb_get_geometry(dpy, win), NULL);
                values[0] = pointer->root_x - geom->x;
                values[1] = pointer->root_y - geom->y;
                xcb_configure_window(dpy, win, XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
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
        case XCB_MAP_REQUEST: {
            xcb_map_request_event_t *e;

            PDEBUG("event: Map request");
            e = (xcb_map_request_event_t*) ev;
            new_window(e->window);
        }
        break;
        }
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
    // Position
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
    values[0] = BORDER_COLOR_UNFOCUSED;
    xcb_change_window_attributes(dpy, window, XCB_CW_BORDER_PIXEL, values);

    // Set border width
    values[0] = BORDER_WIDTH;
    mask = XCB_CONFIG_WINDOW_BORDER_WIDTH;
    xcb_change_window_attributes(dpy, window, mask, values);

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

bool get_geom(xcb_drawable_t window, int16_t* x, int16_t* y, uint16_t* w, uint16_t* h) {
    xcb_get_geometry_reply_t* geom;

    geom = xcb_get_geometry_reply(dpy, xcb_get_geometry(dpy, window), NULL);
    if(geom == NULL) {
        PDEBUG("Unable to get window geometry!");
        return false;
    }

    *x = geom->x;
    *y = geom->y;
    *w = geom->width;
    *h = geom->height;

    free(geom);

    return true;
}

void move_window(xcb_drawable_t window, uint16_t x, uint16_t y) {
    uint32_t values[2];

    if(window == screen->root || window == 0) {
        PDEBUG("Attempted to move root window!");
        return;
    }
    
    values[0] = x;
    values[1] = y;

    xcb_configure_window(dpy, window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);

    xcb_flush(dpy);
}












