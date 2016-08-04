/*
 * GLESonGL implementation
 * Version:  1.2
 *
 * Copyright (C) 2003  David Blythe   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * DAVID BLYTHE BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <GLES/egl.h>

#include "ug.h"

#ifndef DEBUG
#define DEBUG(x)
#endif

typedef struct {
    void (*idle)(UGWindow w);
    NativeDisplayType dpy;
    struct ugwindow* win, * winlist;
    EGLDisplay egldpy;
} UGCtx_t;

static UGCtx_t * context;

typedef struct ugwindow {
    Window win;
    int width, height;
    int redraw;
    void (*draw)(UGWindow w);
    void (*reshape)(UGWindow w, int width,int height);
    void (*kbd)(UGWindow w, int key, int x, int y);
    void (*pointer)(UGWindow w, int button, int state, int x, int y);
    void (*motion)(UGWindow w, int x, int y);
    UGCtx_t* ug;
    struct ugwindow* next, * prev;
    EGLSurface surface;
    EGLConfig eglconfig;
    EGLContext eglctx;
} UGWindow_t;


UGCtx APIENTRY
ugInit(void) {
    UGCtx_t* ug = malloc(sizeof *ug);
    if (ug) {
      memset(ug, 0, sizeof *ug);

      if (!(ug->dpy = XOpenDisplay(NULL)))
        goto end;

      ug->egldpy = eglGetDisplay(ug->dpy);

      if (!eglInitialize(ug->egldpy, NULL, NULL))
        goto end;
    }

  	context = ug;
    return (UGCtx)ug;
 end:
    free(ug);
    return 0;
}

void APIENTRY
ugFini(UGCtx ug) {
    UGCtx_t* _ug = (UGCtx_t*)ug;
    eglTerminate(_ug->egldpy);
    free(_ug);
}

UGCtx APIENTRY ugCtxFromWin(UGWindow uwin) {
    return (UGCtx)((UGWindow_t*)uwin)->ug;
}

static void
bind_context(UGCtx_t* ug, UGWindow_t* w) {
    if (!eglMakeCurrent(ug->egldpy, w->surface, w->surface, w->eglctx)) {
      printf("botch makecurrent\n");
      exit(1);
    }
    ug->win = w;
}

UGWindow APIENTRY
ugCreateWindow(UGCtx ug,  const char* config,
		const char* title, int width, int height, int x, int y) {
    UGWindow_t *w = malloc(sizeof *w);
    UGCtx_t* _ug = (UGCtx_t*)ug;

    if (w) {
      EGLint n, vid;
      EGLConfig configs[1];
      int size,screen_num;

      /*XXXblythe horrible hack, need to parse config string*/
      static int attribs[] = { EGL_RED_SIZE, 1, EGL_NONE }; /*XXXblythe*/
      static int attribs2[] = {EGL_RED_SIZE, 1, EGL_DEPTH_SIZE, 1, EGL_NONE};
      int depth = strstr(config, "UG_DEPTH") != 0;

      memset(w, 0, sizeof *w);
      w->ug = _ug;
      if (!eglChooseConfig(_ug->egldpy, depth ? attribs2 : attribs, configs, 1, &n)) {
        free(w);
        return 0;
      }
      w->eglconfig = configs[0];
      eglGetConfigAttrib(_ug->egldpy, configs[0], EGL_NATIVE_VISUAL_ID, &vid);

      screen_num = DefaultScreen(_ug->dpy);
      w->win = XCreateSimpleWindow(_ug->dpy, DefaultRootWindow(_ug->dpy),
                                x, y, width, height, 2,
                                   BlackPixel(_ug->dpy, screen_num),
                                   WhitePixel(_ug->dpy, screen_num));

      /* make the window actually appear on the screen. */
      XMapWindow(_ug->dpy,w->win);

      /* flush all pending requests to the X server. */
      XFlush(_ug->dpy);

      /* subscribe to the given set of event types. */
      XSelectInput(_ug->dpy, w->win, ExposureMask | KeyPressMask |
                   ButtonPressMask | Button1MotionMask |
                   Button2MotionMask | StructureNotifyMask);

      if (!w->win)
      {
        free(w);
        return 0;
      }

      w->next = _ug->winlist;
      _ug->winlist = w;
      w->prev = 0;
      if (w->next) w->next->prev = w;

      w->width = width;
      w->height = height;

      w->surface = eglCreateWindowSurface(_ug->egldpy, w->eglconfig, (NativeWindowType)(w->win), 0);
      /*XXXblythe share*/
      w->eglctx = eglCreateContext(_ug->egldpy, w->eglconfig, NULL, NULL);
      bind_context(_ug, w);
    }
    return (UGWindow)w;
}

void APIENTRY
ugDestroyWindow(UGWindow uwin) {
    UGWindow_t* w = (UGWindow_t*)uwin;
    UGCtx_t* ug = w->ug;
    eglDestroySurface(ug->egldpy, w->surface);

    if (ug->winlist == w) ug->winlist = w->next;
    if (w->next) w->next->prev = w->prev;
    if (w->prev) w->prev->next = w->next;
    free(w);
}

void APIENTRY
ugReshapeFunc(UGWindow uwin, void (*f)(UGWindow uwin, int width, int height)) {
    UGWindow_t *w = (UGWindow_t*)uwin;
    w->reshape = f;
}

void APIENTRY
ugDisplayFunc(UGWindow uwin, void (*f)(UGWindow uwin)) {
    UGWindow_t *w = (UGWindow_t*)uwin;
    w->draw = f;
}

void APIENTRY
ugKeyboardFunc(UGWindow uwin, void (*f)(UGWindow uwin, int key, int x, int y)) {
    UGWindow_t *w = (UGWindow_t*)uwin;
    w->kbd = f;
}

void APIENTRY
ugPointerFunc(UGWindow uwin, void (*f)(UGWindow uwin, int button, int state, int x, int y)) {
    UGWindow_t *w = (UGWindow_t*)uwin;
    w->pointer = f;
}

void APIENTRY
ugMotionFunc(UGWindow uwin, void (*f)(UGWindow uwin, int x, int y)) {
    UGWindow_t *w = (UGWindow_t*)uwin;
    w->motion = f;
}

void APIENTRY
ugIdleFunc(UGCtx ug, void (*f)(UGWindow w)) {
    UGCtx_t *_ug = (UGCtx_t*)ug;
    _ug->idle = f;
}

void APIENTRY
ugMainLoop(UGCtx ug) {
	context = (UGCtx_t *) ug;
  UGWindow_t *w = context->win;
    int done = 0;
    int width,height;
    XEvent an_event;
    while (!done) {
      XNextEvent(context->dpy, &an_event);
      switch (an_event.type) {
        case Expose:
          (w->draw)((UGWindow) w);
          break;
        case ConfigureNotify:
          /* update the size of our window, for expose events. */
          width = an_event.xconfigure.width;
          height = an_event.xconfigure.height;
          break;
        case ButtonPress:
          break;
        case MotionNotify:
          break;
        case KeyPress:
          /* exit the application by braking out of the events loop. */
          done = 1;
          break;
        default: /* ignore any other event types. */
          break;
      } /* end switch on event type */
    } /* end while events handling */

}

void APIENTRY
ugPostRedisplay(UGWindow uwin) {
    UGWindow_t* w = (UGWindow_t*)uwin;
}

void APIENTRY
ugSwapBuffers(UGWindow uwin) {
    UGWindow_t* w = (UGWindow_t*)uwin;
    eglSwapBuffers(w->ug->egldpy, w->surface);
}



