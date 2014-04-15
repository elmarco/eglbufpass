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

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

const char *vertex_src =
	"attribute vec4        position;		 \
   attribute vec2        texcoords;		 \
   varying vec2          tcoords;		 \
           					 \
   void main()					 \
   {						 \
      tcoords = texcoords;				 \
      gl_Position = position;            \
   }							 \
";


const char *fragment_src =
	"                                                      \
   varying highp vec2    tcoords;					       \
   uniform sampler2D samp;				       \
          						       \
   void  main()						       \
   {							       \
gl_FragColor  = texture2D(samp, tcoords);		       \
   }							       \
";

#if 0
gl_FragColor  = texture2D(samp, tcoords);
#endif

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

struct texture {
	int rem_id;
	GLuint local_id;
	EGLImageKHR image;
	struct texture *next;
};

struct client {
	struct display *d;
	struct window *w;
	int sock_fd;
	EGLImageKHR image;
	GLuint tex_id;
	GLuint tex_loc;
	GLuint attr_pos, attr_tex;
	struct texture *texhead;
};

static void x11_window_create(struct window *w)
{
	Window root;
	EGLBoolean b;
	struct display *d = w->display;
	XSetWindowAttributes  swa;
	root = DefaultRootWindow(d->x11.dpy);

	memset(&swa, 0, sizeof(XSetWindowAttributes));
//swa.event_mask  =  ExposureMask | PointerMotionMask | KeyPressMask;

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

static void init_fns(void)
{

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
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
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

	api = EGL_OPENGL_ES_API;
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

static void
draw_rect_from_arrays(struct client *c, const void *verts, const void *tex)
{
	GLuint buf = 0;

	glGenBuffers(1, &buf);
	glBindBuffer(GL_ARRAY_BUFFER, buf);
	glBufferData(GL_ARRAY_BUFFER,
		     (sizeof(GLfloat) * 4 * 4) +
		     (sizeof(GLfloat) * 4 * 2),
		     NULL,
		     GL_STATIC_DRAW);

	if (verts) {
		glBufferSubData(GL_ARRAY_BUFFER,
				0,
				sizeof(GLfloat) * 4 * 4,
				verts);
		glVertexAttribPointer(c->attr_pos, 4, GL_FLOAT,
				      GL_FALSE, 0, 0);
		glEnableVertexAttribArray(c->attr_pos);
	}
	if (tex) {
		glBufferSubData(GL_ARRAY_BUFFER,
				sizeof(GLfloat) * 4 * 4,
				sizeof(GLfloat) * 4 * 2,
				tex);
		glVertexAttribPointer(c->attr_tex, 2, GL_FLOAT,
				      GL_FALSE, 0,
				      (void *)(sizeof(GLfloat) * 4 * 4));
		glEnableVertexAttribArray(c->attr_tex);
	}

	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	if (verts)
		glDisableVertexAttribArray(c->attr_pos);
	if (tex)
		glDisableVertexAttribArray(c->attr_tex);

	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glDeleteBuffers(1, &buf);
}

static GLvoid
client_draw_rect_tex(struct client *c,
		     float x, float y, float w, float h,
                     float tx, float ty, float tw, float th)
{
        float verts[4][4];
        float tex[4][2];

        verts[0][0] = x;
        verts[0][1] = y;
        verts[0][2] = 0.0;
        verts[0][3] = 1.0;
        tex[0][0] = tx;
        tex[0][1] = ty;
        verts[1][0] = x + w;
        verts[1][1] = y;
        verts[1][2] = 0.0;
        verts[1][3] = 1.0;
        tex[1][0] = tx + tw;
        tex[1][1] = ty;
        verts[2][0] = x;
        verts[2][1] = y + h;
        verts[2][2] = 0.0;
        verts[2][3] = 1.0;
        tex[2][0] = tx;
        tex[2][1] = ty + th;
        verts[3][0] = x + w;
        verts[3][1] = y + h;
        verts[3][2] = 0.0;
        verts[3][3] = 1.0;
        tex[3][0] = tx + tw;
        tex[3][1] = ty + th;

        draw_rect_from_arrays(c, verts, tex);
}

static void draw_screen(struct client *c)
{
	client_draw_rect_tex(c, -1, -1, 2, 2,
			     0, 0, 1, 1);
}

static void init_shaders(struct client *c)
{
	GLuint fs, vs, prog;
	GLint stat;

	fs = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fs, 1, (const char **)&fragment_src, NULL);
	glCompileShader(fs);
	glGetShaderiv(fs, GL_COMPILE_STATUS, &stat);
	if (!stat)
		error(1, errno, "Failed to compile FS");

	vs = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vs, 1, (const char **)&vertex_src, NULL);
	glCompileShader(vs);
	glGetShaderiv(vs, GL_COMPILE_STATUS, &stat);
	if (!stat)
		error(1, errno, "failed to compile VS");

	prog = glCreateProgram();
	glAttachShader(prog, fs);
	glAttachShader(prog, vs);
	glLinkProgram(prog);

	glGetProgramiv(prog, GL_LINK_STATUS, &stat);
	if (!stat) {
		char log[1000];
		GLsizei len;
		glGetProgramInfoLog(prog, 1000, &len, log);
		printf("Error linking: %s\n", log);
		exit(1);
	}

	glUseProgram(prog);

	c->attr_pos = glGetAttribLocation(prog, "position");
	c->attr_tex = glGetAttribLocation(prog, "texcoords");
	c->tex_loc = glGetUniformLocation(prog, "samp");
}

void add_texture(struct client *client, struct cmd_buf *cbuf, int fd)
{
	struct texture *texture;
	EGLint attrs[13];

	texture = calloc(1, sizeof(*texture));
	if (!texture)
		error(1, errno, "failed to create texture storage\n");

	texture->rem_id = cbuf->u.buf.id;

	attrs[0] = EGL_DMA_BUF_PLANE0_FD_EXT;
	attrs[1] = fd;
	attrs[2] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
	attrs[3] = cbuf->u.buf.stride;
	attrs[4] = EGL_DMA_BUF_PLANE0_OFFSET_EXT;
	attrs[5] = 0;
	attrs[6] = EGL_WIDTH;
	attrs[7] = cbuf->u.buf.width;
	attrs[8] = EGL_HEIGHT;
	attrs[9] = cbuf->u.buf.height;
	attrs[10] = EGL_LINUX_DRM_FOURCC_EXT;
	attrs[11] = cbuf->u.buf.format;
	attrs[12] = EGL_NONE;
	texture->image = eglCreateImageKHR(client->d->x11.egl_display,
					  EGL_NO_CONTEXT,
					  EGL_LINUX_DMA_BUF_EXT,
					  (EGLClientBuffer)NULL,
					  attrs);

	if (!texture->image)
		error(1, errno, "failed to import image dma-buf");

	glGenTextures(1, &texture->local_id);
	glBindTexture(GL_TEXTURE_2D, texture->local_id);
	glTexParameteri ( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri ( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );

//	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 640, 480, 0,
//		     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, (GLeglImageOES)texture->image);

	if (!client->texhead)
		client->texhead = texture;
	else {
		struct texture *iter = client->texhead;
		while (iter->next)
			iter = iter->next;
		iter->next = texture;
	}
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

	init_shaders(client);

	glViewport(0, 0, client->w->width, client->w->height);
	glClearColor(0.0, 1.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	eglSwapBuffers(client->d->x11.egl_display, client->w->x11.egl_surface);

	glUniform1i(client->tex_loc, 0);
	sleep(1);
	for (;;) {
/* glClear(GL_COLOR_BUFFER_BIT); */
/* eglSwapBuffers(client->d->x11.egl_display, client->w->x11.egl_surface); */
		struct cmd_buf buf;
		int myfd;
		size = sock_fd_read(client->sock_fd, &buf, sizeof(buf), &myfd);
		if (size <= 0)
			break;

		if (buf.type == CMD_TYPE_BUF) {
			add_texture(client, &buf, myfd);
		} else if (buf.type == CMD_TYPE_DIRT) {
			draw_screen(client);

			eglSwapBuffers(client->d->x11.egl_display, client->w->x11.egl_surface);
		}

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
