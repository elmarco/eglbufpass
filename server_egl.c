#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <dirent.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <GL/gl.h>
#include <GL/glext.h>/* display objects */

#include <errno.h>
#include <error.h>
#include <gbm.h>
#include <xf86drm.h>
#include "common.h"
PFNGLGENTEXTURESEXTPROC my_glGenTextures;
PFNGLGENFRAMEBUFFERSPROC my_glGenFramebuffers;
GLuint tex_ids[4];
GLuint fb_id;
struct rnode_display {
	int fd;
	struct gbm_device *gbm_dev;
	EGLDisplay egl_display;
	EGLConfig egl_conf;
	EGLContext egl_ctx;
};

struct rnode_window {
	struct gbm_surface *gbm_surface;
	EGLSurface egl_surface;
};

struct window {
	struct display *display;
	int width, height;
	struct rnode_window rnode;
};

struct display {
	struct rnode_display rnode;
};

struct server {
	struct display *d;
	struct window *w;
	GLuint tex_id;
	GLuint fb_id;
	EGLImageKHR image;
	int sock_fd;
};

static int rnode_open(void)
{
	DIR *dir;
	struct dirent *e;
	int r, fd;
	char *p;

	dir = opendir("/dev/dri/");
	if (!dir)
		error(1, errno, "cannot open /dev/dri/");

	fd = -1;
	while ((e = readdir(dir))) {
		if (e->d_type != DT_CHR)
			continue;

		if (strncmp(e->d_name, "renderD", 7))
			continue;

		r = asprintf(&p, "/dev/dri/%s", e->d_name);
		if (r < 0)
			error(1, errno, "cannot allocate pathname");

		r = open(p, O_RDWR | O_CLOEXEC | O_NOCTTY | O_NONBLOCK);
		if (r < 0){
			free(p);
			error(0, errno, "cannot open %s", p);
			continue;
		}
		fd = r;
		fprintf(stderr, "using render node %s\n", p);
		free(p);
		break;
	}

	if (fd < 0)
		error(1, 0, "cannot open any render-node in /dev/dri");
	return fd;
}

static void rnode_init(struct display *d)
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

	d->rnode.fd = rnode_open();
	d->rnode.gbm_dev = gbm_create_device(d->rnode.fd);
	if (!d->rnode.gbm_dev)
		error(1, errno, "cannot create gbm device");

	d->rnode.egl_display = eglGetDisplay((EGLNativeDisplayType)d->rnode.gbm_dev);
	if (!d->rnode.egl_display)
		error(1, errno, "cannot create EGL display");

	b = eglInitialize(d->rnode.egl_display, &major, &minor);
	if (!b)
		error(1, errno, "Cannot initialise EGL");

	fprintf(stderr, "EGL major/minor: %d.%d\n", major, minor);
	fprintf(stderr, "EGL version: %s\n",
		eglQueryString(d->rnode.egl_display, EGL_VERSION));
	fprintf(stderr, "EGL vendor: %s\n",
		eglQueryString(d->rnode.egl_display, EGL_VENDOR));
	fprintf(stderr, "EGL extensions: %s\n",
		eglQueryString(d->rnode.egl_display, EGL_EXTENSIONS));
		
	api = EGL_OPENGL_API;
	b = eglBindAPI(api);
	if (!b)
		error(1, errno, "cannot bind OpenGLES API");

	b = eglChooseConfig(d->rnode.egl_display, conf_att, &d->rnode.egl_conf,
			    1, &n);

	if (!b || n != 1)
		error(1, errno, "cannot find suitable EGL config");

	d->rnode.egl_ctx = eglCreateContext(d->rnode.egl_display,
					    d->rnode.egl_conf,
					    EGL_NO_CONTEXT,
					    ctx_att);
	if (!d->rnode.egl_ctx)
		error(1, errno, "cannot create EGL context");

	eglMakeCurrent(d->rnode.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
}

static void rnode_destroy(struct display *d)
{
	eglMakeCurrent(d->rnode.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglDestroyContext(d->rnode.egl_display, d->rnode.egl_ctx);
	eglTerminate(d->rnode.egl_display);
	gbm_device_destroy(d->rnode.gbm_dev);
	close(d->rnode.fd);
}

static struct display *display_create(void)
{
	struct display *display = calloc(1, sizeof(*display));
	if (!display)
		error(1, ENOMEM, "cannot allocate display");

	rnode_init(display);
	return display;
}

static void display_destroy(struct display *display)
{
	rnode_destroy(display);
	free(display);
}

static void rnode_window_create(struct window *w)
{
	struct display *d = w->display;
	EGLBoolean b;

	w->rnode.gbm_surface = gbm_surface_create(d->rnode.gbm_dev,
						  w->width, w->height,
						  GBM_FORMAT_XRGB8888,
						  GBM_BO_USE_RENDERING);
	if (!w->rnode.gbm_surface)
		error(1, errno, "cannot create gbm surface");

	w->rnode.egl_surface = eglCreateWindowSurface(d->rnode.egl_display,
						      d->rnode.egl_conf,
						      (EGLNativeWindowType)w->rnode.gbm_surface, NULL);
	if (!w->rnode.egl_surface)
		error(1, errno, "Cannot create EGL surface");

	b = eglMakeCurrent(d->rnode.egl_display,
			   w->rnode.egl_surface,
			   w->rnode.egl_surface,
			   d->rnode.egl_ctx);
	if (!b)
		error(1, errno, "Cannot activate EGL context");
}

static void rnode_window_destroy(struct window *w)
{
	struct display *d = w->display;

	eglMakeCurrent(d->rnode.egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
		       EGL_NO_CONTEXT);
	eglDestroySurface(d->rnode.egl_display, w->rnode.egl_surface);
	gbm_surface_destroy(w->rnode.gbm_surface);
}

static struct window *window_create(struct display *display)
{
	struct window *window = calloc(1, sizeof(*window));
	if (!window)
		error(1, errno, "cannot allocate window");

	window->display = display;
	window->width = 100;
	window->height = 100;

	rnode_window_create(window);
	return window;
}

static void window_destroy(struct window *window)
{
	rnode_window_destroy(window);
	free(window);
}

static void init_fns(void)
{
	my_glGenTextures = (void *)eglGetProcAddress("glGenTextures");
	my_glGenFramebuffers = (void *)eglGetProcAddress("glGenFramebuffers");
}

static void server_init_texture(struct server *server)
{
	EGLBoolean b;
	EGLint name, handle, stride;
	int r;
	int fd;

	/* create some textures */
	(*my_glGenTextures)(1, &server->tex_id);

	glBindTexture(GL_TEXTURE_2D, server->tex_id);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

	(*my_glGenFramebuffers)(1, &server->fb_id);
	glBindFramebufferEXT(GL_FRAMEBUFFER_EXT, server->fb_id);
	glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT, GL_TEXTURE_2D, server->tex_id, 0);

	glClearColor(1.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	/* create an EGL image from that texture */
	server->image = eglCreateImageKHR(server->d->rnode.egl_display, server->d->rnode.egl_ctx, EGL_GL_TEXTURE_2D_KHR, (EGLClientBuffer)(unsigned long)server->tex_id, NULL);

	fprintf(stderr,"got image %p\n", server->image);

	b = eglExportDRMImageMESA(server->d->rnode.egl_display,
				  server->image,
				  &name, &handle,
				  &stride);

	if (!b)
		error(1, errno, "failed to export image\n");

	fprintf(stderr,"image exported %d %d %d\n", name, handle, stride);

	r = drmPrimeHandleToFD(server->d->rnode.fd, handle, 0, &fd);
	if (r < 0)
		error(1, errno, "cannot get prime-fd for handle");

}

static void server_fini_texture(struct server *server)
{

	eglDestroyImageKHR(server->d->rnode.egl_display, server->image);
}

struct server *server_create(int sock_fd)
{
	struct server *server;

	server = calloc(1, sizeof(struct server));
	if (!server)
		error(1, errno, "cannot allocate memory");
	server->d = display_create();
	server->w = window_create(server->d);
	server->sock_fd = sock_fd;
	init_fns();

	server_init_texture(server);

	return server;
}


void server_destroy(struct server *server)
{
	server_fini_texture(server);
	window_destroy(server->w);
	display_destroy(server->d);
	close(server->sock_fd);
	free(server);
}
