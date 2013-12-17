#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <error.h>
#include <X11/Xlib.h>
#include "fdpass.h"
#include "common.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
struct x11_window {
	Window win;
	EGLSurface egl_surface;
};

struct window {
	struct display *display;
	int width, height;
	struct x11_window x11;
};

struct x11_display {
	Display *dpy;
	EGLDisplay egl_display;
	EGLConfig egl_conf;
	EGLContext egl_ctx;
};
struct display {
	struct x11_display x11;
};
  
struct client {
	struct display *d;
	struct window *w;
	int sock_fd;
	EGLImageKHR image;
};

static void x11_window_create(struct window *w)
{
	Window root;
	EGLBoolean b;
	struct display *d = w->display;
	XSetWindowAttributes  swa;
	root = DefaultRootWindow(d->x11.dpy);

	memset(&swa, 0, sizeof(XSetWindowAttributes));
	w->x11.win = XCreateWindow(d->x11.dpy, root, 0, 0,
				   w->width, w->height, 0,
				   CopyFromParent, InputOutput,
				   CopyFromParent, CWEventMask,
				   &swa);

	XMapWindow(d->x11.dpy, w->x11.win);

	
	w->x11.egl_surface = eglCreateWindowSurface( d->x11.egl_display,
						     d->x11.egl_conf,
						     (EGLNativeWindowType)w->x11.win, NULL);

	if (!w->x11.egl_surface)
		error(1, errno, "failed to init egl surface");

	b = eglMakeCurrent(d->x11.egl_display,
			   w->x11.egl_surface,
			   w->x11.egl_surface,
			   d->x11.egl_ctx);
	if (!b)
		error(1, errno, "Cannot activate EGL context");
}
static struct window *window_create(struct display *d)
{

	struct window *w;

	w = calloc(1, sizeof(struct window));
	if (!w)
		error(1, errno, "can't allocate window");

	w->display = d;
	w->width = 250;
	w->height = 250;

	x11_window_create(w);
	return w;
}

static void x11_window_destroy(struct window *w)
{
	struct display *d = w->display;

	eglMakeCurrent(d->x11.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglDestroySurface(d->x11.egl_display, w->x11.egl_surface);
	XDestroyWindow(d->x11.dpy, w->x11.win);
}
static void window_destroy(struct window *window)
{
	x11_window_destroy(window);
	free(window);
}

static void x11_display_init(struct display *d)
{
	static const EGLint conf_att[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		EGL_RED_SIZE, 1,
		EGL_GREEN_SIZE, 1,
		EGL_BLUE_SIZE, 1,
		EGL_ALPHA_SIZE, 0,
		EGL_NONE,
	};
	static const EGLint ctx_att[] = {
		EGL_CONTEXT_CLIENT_VERSION, 2,
		EGL_NONE
	};
	EGLBoolean b;
	EGLenum api;
	EGLint major, minor, n;
	d->x11.dpy = XOpenDisplay(NULL);
	if (!d->x11.dpy)
		error(1, errno, "Unable to open X server display");

	d->x11.egl_display = eglGetDisplay((EGLNativeDisplayType)d->x11.dpy);
	if (d->x11.egl_display == EGL_NO_DISPLAY)
		error(1, errno, "Failed to get EGL display");

	if (!eglInitialize(d->x11.egl_display, &major, &minor))
		error(1, errno, "Failed to init EGL display");
	fprintf(stderr, "EGL major/minor: %d.%d\n", major, minor);
	fprintf(stderr, "EGL version: %s\n",
		eglQueryString(d->x11.egl_display, EGL_VERSION));
	fprintf(stderr, "EGL vendor: %s\n",
		eglQueryString(d->x11.egl_display, EGL_VENDOR));
	fprintf(stderr, "EGL extensions: %s\n",
		eglQueryString(d->x11.egl_display, EGL_EXTENSIONS));

	api = EGL_OPENGL_API;
	b = eglBindAPI(api);
	if (!b)
		error(1, errno, "cannot bind OpenGLES API");

	b = eglChooseConfig(d->x11.egl_display, conf_att, &d->x11.egl_conf,
			    1, &n);

	if (!b || n != 1)
		error(1, errno, "cannot find suitable EGL config");

	d->x11.egl_ctx = eglCreateContext(d->x11.egl_display,
					  d->x11.egl_conf,
					  EGL_NO_CONTEXT,
					  ctx_att);
	if (!d->x11.egl_ctx)
		error(1, errno, "cannot create EGL context");
	
	eglMakeCurrent(d->x11.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
}

static struct display *display_create(void)
{
	struct display *d;

	d = calloc(1, sizeof(*d));
	if (!d)
		error(1, errno, "cannot allocate memory");

	x11_display_init(d);
	return d;
}

static void display_destroy(struct display *d)
{
	XCloseDisplay(d->x11.dpy);
	free(d);
}

struct client *client_create(int sock_fd)
{
	struct client *client;
	ssize_t size;
	client = calloc(1, sizeof(struct client));
	if (!client)
		error(1, errno, "cannot allocate memory");

	client->d = display_create();
	client->w = window_create(client->d);
	client->sock_fd = sock_fd;

	sleep(1);
	for (;;) {
		EGLint attrs[13];
		struct bufinfo buf;
		int myfd;
		size = sock_fd_read(client->sock_fd, &buf, sizeof(buf), &myfd);
		if (size <= 0)
			break;

		printf("read %d: %d %d %dx%d\n", (int)size, buf.id, buf.stride, buf.width, buf.height);

		attrs[0] = EGL_DMA_BUF_PLANE0_FD_EXT;
		attrs[1] = myfd;
		attrs[2] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
		attrs[3] = buf.stride;
		attrs[4] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
		attrs[5] = 0;
		attrs[6] = EGL_WIDTH;
		attrs[7] = buf.width;
		attrs[8] = EGL_HEIGHT;
		attrs[9] = buf.height;
		attrs[10] = EGL_LINUX_DRM_FOURCC_EXT;
		attrs[11] = buf.format;
		attrs[12] = EGL_NONE;
		client->image = eglCreateImageKHR(client->d->x11.egl_display,
						  EGL_NO_CONTEXT,
						  EGL_LINUX_DMA_BUF_EXT,
						  (EGLClientBuffer)NULL,
						  attrs);

		if (!client->image)
			error(1, errno, "failed to import image dma-buf");

						  
	}
	return client;
}

void client_destroy(struct client *client)
{
	window_destroy(client->w);
	display_destroy(client->d);
	close(client->sock_fd);
	free(client);

}
