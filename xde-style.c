/*****************************************************************************

 Copyright (c) 2008-2014  Monavacon Limited <http://www.monavacon.com/>
 Copyright (c) 2001-2008  OpenSS7 Corporation <http://www.openss7.com/>
 Copyright (c) 1997-2001  Brian F. G. Bidulock <bidulock@openss7.org>

 All Rights Reserved.

 This program is free software: you can redistribute it and/or modify it under
 the terms of the GNU General Public License as published by the Free Software
 Foundation, version 3 of the license.

 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 details.

 You should have received a copy of the GNU General Public License along with
 this program.  If not, see <http://www.gnu.org/licenses/>, or write to the
 Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

 -----------------------------------------------------------------------------

 U.S. GOVERNMENT RESTRICTED RIGHTS.  If you are licensing this Software on
 behalf of the U.S. Government ("Government"), the following provisions apply
 to you.  If the Software is supplied by the Department of Defense ("DoD"), it
 is classified as "Commercial Computer Software" under paragraph 252.227-7014
 of the DoD Supplement to the Federal Acquisition Regulations ("DFARS") (or any
 successor regulations) and the Government is acquiring only the license rights
 granted herein (the license rights customarily provided to non-Government
 users).  If the Software is supplied to any unit or agency of the Government
 other than DoD, it is classified as "Restricted Computer Software" and the
 Government's rights in the Software are defined in paragraph 52.227-19 of the
 Federal Acquisition Regulations ("FAR") (or any successor regulations) or, in
 the cases of NASA, in paragraph 18.52.227-86 of the NASA Supplement to the FAR
 (or any successor regulations).

 -----------------------------------------------------------------------------

 Commercial licensing and support of this software is available from OpenSS7
 Corporation at a fee.  See http://www.openss7.com/

 *****************************************************************************/

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#ifdef _GNU_SOURCE
#include <getopt.h>
#endif
#include <time.h>
#include <signal.h>
#include <syslog.h>
#include <sys/utsname.h>

#include <assert.h>
#include <locale.h>
#include <stdarg.h>
#include <strings.h>
#include <regex.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/Xresource.h>
#ifdef XRANDR
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/randr.h>
#endif
#ifdef XINERAMA
#include <X11/extensions/Xinerama.h>
#endif
#ifdef STARTUP_NOTIFICATION
#define SN_API_NOT_YET_FROZEN
#include <libsn/sn.h>
#endif

const char *program = NAME;

typedef struct {
	int debug;
	int output;
	Bool current;
	Bool list;
	Bool set;
	Bool system;
	Bool user;
	Bool link;
	Bool theme;
	Bool dryrun;
	int screen;
	char *style;
} Options;

Options options = {
	0,
	1,
	True,
	False,
	False,
	True,
	True,
	False,
	False,
	False,
	-1,
	NULL
};

#define OPRINTF(format, ...) do { if (options.output > 1) fprintf(stderr, "I: " format, __VA_ARGS__); } while (0)
#define DPRINTF(format, ...) do { if (options.debug) fprintf(stderr, "D: %s %s():%d " format, __FILE__, __func__, __LINE__, __VA_ARGS__); } while (0)
#define EPRINTF(format, ...) do { fprintf(stderr, "E: %s %s():%d " format, __FILE__, __func__, __LINE__, __VA_ARGS__); } while (0)


#define CHECK_DIRS 3
#define CHECK_WINS 6

typedef struct {
	int refs;			/* references to this structure */
	union {
		struct {
			Window netwm_check;	/* NetWM/EWMH check window */
			Window winwm_check;	/* GNOME1/WMH check window */
			Window maker_check;	/* WindowMaker Noticeboard check window */
			Window motif_check;	/* Motif/MWMH check window */
			Window icccm_check;	/* ICCCM 2.0 manager window */
			Window redir_check;	/* ICCCM root window */
		};
		Window wins[CHECK_WINS];
	};
	long pid;			/* window manager pid */
	char *host;			/* window manager host */
	char *name;			/* window manager name */
	char **argv;			/* window manager command line argv[] */
	int argc;			/* window manager command line argc */
	char **cargv;			/* wm WM_COMMAND argv[] */
	int cargc;			/* wm WM_COMMAND argc */
	XClassHint ch;			/* window manager WM_CLASS */
	char *rcfile;			/* window manager rc file */
	union {
		struct {
			char *pdir;	/* WM private directory */
			char *udir;	/* WM user directory */
			char *sdir;	/* WM system directory */
		};
		char *dirs[CHECK_DIRS];
	};
	char *stylefile;		/* Window manager style file */
	char *style;			/* WM current style */
	char *stylename;		/* WM current style name */
	Bool noenv;			/* Do we have an environment? */
	char *env;			/* Window manager environment */
	size_t nenv;			/* Number of characters in environment */
	XrmDatabase db;			/* WM resource database */
	char **xdg_dirs;		/* XDG data dirs */
} WindowManager;

typedef struct {
	int screen;			/* screen number */
	int x, y;			/* screen position */
	unsigned int width, height;	/* screen dimensions */
	Window root;			/* root window for this screen */
	WindowManager *wm;		/* window manager managing this screen */
} WmScreen;

Display *dpy;
int screen;
Window root;
WindowManager *wm;
WmScreen *screens;
WmScreen *scr;
unsigned int nscr;

WindowManager *
ref_wm()
{
	if (!(wm = scr->wm)) {
		wm = scr->wm = calloc(1, sizeof(*wm));
		return ref_wm();
	}
	wm->refs++;
	return wm;
}

void
delete_wm()
{
	if ((wm = scr->wm)) {
		free(wm->host);
		free(wm->name);
		free(wm->argv);
		if (wm->cargv)
			XFreeStringList(wm->cargv);
		if (wm->ch.res_name)
			XFree(wm->ch.res_name);
		if (wm->ch.res_class)
			XFree(wm->ch.res_class);
		free(wm->rcfile);
		free(wm->pdir);
		free(wm->udir);
		free(wm->sdir);
		free(wm->stylefile);
		free(wm->style);
		free(wm->env);
		if (wm->db)
			XrmDestroyDatabase(wm->db);
		free(wm->xdg_dirs);
	}
}

WindowManager *
unref_wm()
{
	if ((wm = scr->wm)) {
		if (--wm->refs <= 0)
			delete_wm();
		wm = scr->wm = NULL;
	}
	return NULL;
}

Bool xrm_initialized = False;

static void
init_xrm()
{
	if (!xrm_initialized) {
		xrm_initialized = True;
		XrmInitialize();
	}
}

Atom _XA_BB_THEME;
Atom _XA_BLACKBOX_PID;
Atom _XA_MOTIF_WM_INFO;
Atom _XA_NET_SUPPORTING_WM_CHECK;
Atom _XA_NET_WM_NAME;
Atom _XA_NET_WM_PID;
Atom _XA_OB_THEME;
Atom _XA_OPENBOX_PID;
Atom _XA_WINDOWMAKER_NOTICEBOARD;
Atom _XA_WIN_SUPPORTING_WM_CHECK;

typedef struct {
	char *name;
	Atom *atom;
} Atoms;

Atoms atoms[] = {
	/* *INDENT-OFF* */
	{"_BB_THEME",			&_XA_BB_THEME			},
	{"_BLACKBOX_PID",		&_XA_BLACKBOX_PID		},
	{"_MOTIF_WM_INFO",		&_XA_MOTIF_WM_INFO		},
	{"_NET_SUPPORTING_WM_CHECK",	&_XA_NET_SUPPORTING_WM_CHECK	},
	{"_NET_WM_NAME",		&_XA_NET_WM_NAME		},
	{"_NET_WM_PID",			&_XA_NET_WM_PID			},
	{"_OB_THEME",			&_XA_OB_THEME			},
	{"_OPENBOX_PID",		&_XA_OPENBOX_PID		},
	{"_WINDOWMAKER_NOTICEBOARD",	&_XA_WINDOWMAKER_NOTICEBOARD	},
	{"_WIN_SUPPORTING_WM_CHECK",	&_XA_WIN_SUPPORTING_WM_CHECK	},
	{NULL,				NULL				}
	/* *INDENT-ON* */
};

static void
intern_atoms()
{
	int i, j, n;
	char **atom_names;
	Atom *atom_values;

	for (i = 0, n = 0; atoms[i].name; i++)
		if (atoms[i].atom)
			n++;
	atom_names = calloc(n + 1, sizeof(*atom_names));
	atom_values = calloc(n + 1, sizeof(*atom_values));
	for (i = 0, j = 0; j < n; i++)
		if (atoms[i].atom)
			atom_names[j++] = atoms[i].name;
	XInternAtoms(dpy, atom_names, n, False, atom_values);
	for (i = 0, j = 0; j < n; i++)
		if (atoms[i].atom)
			*atoms[i].atom = atom_values[j++];
	free(atom_names);
	free(atom_values);
}

XContext ScreenContext;

static void
init_display()
{
	Window dw;
	unsigned int du;
	int i;

	if (!(dpy = XOpenDisplay(0))) {
		EPRINTF("%s\n", "cannot open display");
		exit(127);
	}
	OPRINTF("%s\n", "opened display");
	ScreenContext = XUniqueContext();
	intern_atoms();
	nscr = ScreenCount(dpy);
	screens = calloc(nscr, sizeof(*screens));
	for (i = 0; i < nscr; i++) {
		scr = screens + i;
		scr->screen = i;
		scr->root = RootWindow(dpy, i);
		XGetGeometry(dpy, scr->root, &dw, &scr->x, &scr->y,
			     &scr->width, &scr->height, &du, &du);
		XSaveContext(dpy, scr->root, ScreenContext, (XPointer) scr);
		OPRINTF("screen %d root 0x%lx %dx%d+%d+%d\n", scr->screen,
				scr->root, scr->width, scr->height, scr->x, scr->y);
	}
	screen = DefaultScreen(dpy);
	scr = screens + screen;
	root = scr->root;
}

/** @name Property retrieval functions
  *
  * @{ */

static char *
get_text(Window win, Atom prop)
{
	XTextProperty tp = { NULL, };

	XGetTextProperty(dpy, win, &tp, prop);
	if (tp.value) {
		tp.value[tp.nitems + 1] = '\0';
		return (char *) tp.value;
	}
	return NULL;
}

static long *
get_cardinals(Window win, Atom prop, Atom type, long *n)
{
	Atom real;
	int format;
	unsigned long nitems, after, num = 1;
	long *data = NULL;

      try_harder:
	if (XGetWindowProperty(dpy, win, prop, 0L, num, False, type, &real, &format,
			       &nitems, &after, (unsigned char **) &data) == Success
	    && format != 0) {
		if (after) {
			num += ((after + 1) >> 2);
			XFree(data);
			goto try_harder;
		}
		if ((*n = nitems) > 0)
			return data;
		if (data)
			XFree(data);
	} else
		*n = -1;
	return NULL;
}

static Bool
get_cardinal(Window win, Atom prop, Atom type, long *card_ret)
{
	Bool result = False;
	long *data, n;

	if ((data = get_cardinals(win, prop, type, &n)) && n > 0) {
		*card_ret = data[0];
		result = True;
	}
	if (data)
		XFree(data);
	return result;
}

Window *
get_windows(Window win, Atom prop, Atom type, long *n)
{
	return (Window *) get_cardinals(win, prop, type, n);
}

Bool
get_window(Window win, Atom prop, Atom type, Window *win_ret)
{
	return get_cardinal(win, prop, type, (long *) win_ret);
}

Time *
get_times(Window win, Atom prop, Atom type, long *n)
{
	return (Time *) get_cardinals(win, prop, type, n);
}

Bool
get_time(Window win, Atom prop, Atom type, Time * time_ret)
{
	return get_cardinal(win, prop, type, (long *) time_ret);
}

Atom *
get_atoms(Window win, Atom prop, Atom type, long *n)
{
	return (Atom *) get_cardinals(win, prop, type, n);
}

Bool
get_atom(Window win, Atom prop, Atom type, Atom *atom_ret)
{
	return get_cardinal(win, prop, type, (long *) atom_ret);
}

Pixmap *
get_pixmaps(Window win, Atom prop, Atom type, long *n)
{
	return (Pixmap *) get_cardinals(win, prop, type, n);
}

Bool
get_pixmap(Window win, Atom prop, Atom type, Pixmap * pixmap_ret)
{
	return get_cardinal(win, prop, type, (long *) pixmap_ret);
}

/** @brief Check for recursive window properties
  * @param atom - property name
  * @param type - property type
  * @return Window - the recursive window property or None
  */
Window
check_recursive(Atom atom, Atom type)
{
	Atom real;
	int format;
	unsigned long nitems, after;
	unsigned long *data = NULL;
	Window check;

	if (XGetWindowProperty(dpy, root, atom, 0L, 1L, False, type, &real,
			       &format, &nitems, &after,
			       (unsigned char **) &data) == Success && format != 0) {
		if (nitems > 0) {
			if ((check = data[0])) {
				XSelectInput(dpy, check,
					     PropertyChangeMask | StructureNotifyMask);
				XSaveContext(dpy, check, ScreenContext, (XPointer) scr);
			}
			XFree(data);
			data = NULL;
		} else {
			if (data)
				XFree(data);
			return None;
		}
		if (XGetWindowProperty(dpy, check, atom, 0L, 1L, False, type, &real,
				       &format, &nitems, &after,
				       (unsigned char **) &data) == Success
		    && format != 0) {
			if (nitems > 0) {
				if (check != (Window) data[0]) {
					XFree(data);
					return None;
				}
			} else {
				if (data)
					XFree(data);
				return None;
			}
			XFree(data);
		} else
			return None;
	} else
		return None;
	return check;
}

/** @} */

/** @name Checks for window manager support.
  *
  * @{ */

/** @brief Check for a EWMH/NetWM compliant window manager.
  */
Window
check_netwm()
{
	int i = 0;

	do {
		wm->netwm_check = check_recursive(_XA_NET_SUPPORTING_WM_CHECK, XA_WINDOW);
	} while (i++ < 2 && !wm->netwm_check);

	if (wm->netwm_check) {
		XSelectInput(dpy, wm->netwm_check,
			     PropertyChangeMask | StructureNotifyMask);
		XSaveContext(dpy, wm->netwm_check, ScreenContext, (XPointer) scr);
	}
	return wm->netwm_check;
}

/** @brief Check for a GNOME1/WMH/WinWM compliant window manager.
  */
Window
check_winwm()
{
	int i = 0;

	do {
		wm->winwm_check = check_recursive(_XA_WIN_SUPPORTING_WM_CHECK, XA_WINDOW);
	} while (i++ < 2 && !wm->winwm_check);

	if (wm->winwm_check) {
		XSelectInput(dpy, wm->winwm_check,
			     PropertyChangeMask | StructureNotifyMask);
		XSaveContext(dpy, wm->winwm_check, ScreenContext, (XPointer) scr);
	}
	return wm->winwm_check;
}

/** @brief Check for a WindowMaker compliant window manager.
  */
Window
check_maker()
{
	int i = 0;

	do {
		wm->maker_check = check_recursive(_XA_WINDOWMAKER_NOTICEBOARD, XA_WINDOW);
	} while (i++ < 2 && !wm->maker_check);

	if (wm->maker_check) {
		XSelectInput(dpy, wm->maker_check,
			     PropertyChangeMask | StructureNotifyMask);
		XSaveContext(dpy, wm->maker_check, ScreenContext, (XPointer) scr);
	}
	return wm->maker_check;
}

/** @brief Check for an OSF/Motif compliant window manager.
  */
Window
check_motif()
{
	int i = 0;
	long *data, n = 0;

	do {
		data = get_cardinals(root, _XA_MOTIF_WM_INFO, AnyPropertyType, &n);
	} while (i++ < 2 && !data);

	if (data && n >= 2)
		wm->motif_check = data[1];
	if (wm->motif_check) {
		XSelectInput(dpy, wm->motif_check,
			     PropertyChangeMask | StructureNotifyMask);
		XSaveContext(dpy, wm->motif_check, ScreenContext, (XPointer) scr);
	}
	return wm->motif_check;
}

/** @brief Check for an ICCCM compliant window manager.
  */
Window
check_icccm()
{
	char buf[32];
	Atom atom;

	snprintf(buf, 32, "WM_S%d", screen);
	if ((atom = XInternAtom(dpy, buf, True)))
		wm->icccm_check = XGetSelectionOwner(dpy, atom);

	if (wm->icccm_check) {
		XSelectInput(dpy, wm->icccm_check,
			     PropertyChangeMask | StructureNotifyMask);
		XSaveContext(dpy, wm->icccm_check, ScreenContext, (XPointer) scr);

	}
	return wm->icccm_check;
}

/** @brief Check whether an ICCCM window manager is present.
  *
  * This pretty much assumes that any ICCCM window manager will select for
  * SubstructureRedirectMask on the root window.
  */
Window
check_redir()
{
	XWindowAttributes wa;

	OPRINTF("checking direction for screen %d\n", screen);

	wm->redir_check = None;
	if (XGetWindowAttributes(dpy, root, &wa))
		if (wa.all_event_masks & SubstructureRedirectMask)
			wm->redir_check = root;
	return wm->redir_check;
}

Bool
find_wm_comp()
{
	Bool have_wm = False;

	OPRINTF("checking wm compliance for screen %d\n", screen);

	if (check_redir()) {
		have_wm = True;
		OPRINTF("redirection on window 0x%lx\n", wm->redir_check);
	}
	if (check_icccm()) {
		have_wm = True;
		OPRINTF("ICCCM 2.0 window 0x%lx\n", wm->icccm_check);
	}
	if (check_motif()) {
		have_wm = True;
		OPRINTF("OSF/Motif window 0x%lx\n", wm->motif_check);
	}
	if (check_maker()) {
		have_wm = True;
		OPRINTF("WindowMaker window 0x%lx\n", wm->maker_check);
	}
	if (check_winwm()) {
		have_wm = True;
		OPRINTF("GNOME/WMH window 0x%lx\n", wm->winwm_check);
	}
	if (check_netwm()) {
		have_wm = True;
		OPRINTF("NetWM/EWMH window 0x%lx\n", wm->netwm_check);
	}

	return have_wm;
}

/** @} */

/** @name /proc filesystem utilities
  *
  * @{ */

char *
get_proc_file(pid_t pid, char *name, size_t *size)
{
#if 0
	struct stat st;
#endif
	char *file, *buf;
	FILE *f;
	size_t fsize, read, total;

	OPRINTF("getting process file %s\n", name);
	file = calloc(256, sizeof(*file));
	snprintf(file, 256, "/proc/%d/%s", pid, name);

#if 0
	/* CANNOT STAT A /PROC FILE!!! */
	if (stat(file, &st)) {
		EPRINTF("could not stat %s\n", file);
		free(file);
		*size = 0;
		return NULL;
	}
#endif
	if (!(f = fopen(file, "rb"))) {
		EPRINTF("%s\n", strerror(errno));
		free(file);
		*size = 0;
		return NULL;
	}
	for (fsize = 0; fgetc(f) != EOF; fsize++) ;
	OPRINTF("file %s size is %d bytes\n", file, (int)fsize);
	buf = calloc(fsize + 256, sizeof(*buf));
	if (!(f = freopen(file, "rb", f))) {
		EPRINTF("%s\n", strerror(errno));
		free(file);
		free(buf);
		*size = 0;
		return NULL;
	}
	free(file);
	/* read entire file into buffer */
	for (total = 0; total < fsize; total += read)
		if (!(read = fread(buf + total, 1, fsize - total, f)))
			if (total < fsize) {
				EPRINTF("total %d less than fsize %d\n",
						(int)total, (int)fsize);
				free(buf);
				fclose(f);
				*size = 0;
				return NULL;
			}
	fclose(f);
	*size = fsize;
	return buf;
}

char *
get_proc_environ(char *name)
{
	char *pos, *end;

	if (!wm->env && (wm->noenv || !wm->pid
	    || !(wm->env = get_proc_file(wm->pid, "environ", &wm->nenv)))) {
		wm->noenv = True;
		goto nope;
	}
	for (pos = wm->env, end = wm->env + wm->nenv; pos < end; pos += strlen(pos) + 1) {
		if (strstr(pos, name) == pos) {
			pos += strlen(name) + 1;	/* +1 for = sign */
			return pos;
		}
	}
      nope:
	return getenv(name);
}

char *
get_proc_comm(pid_t pid)
{
	size_t size;
	char *comm;

	if ((comm = get_proc_file(pid, "comm", &size)))
		if (strrchr(comm, '\n'))
			*strrchr(comm, '\n') = '\0';
	return comm;
}

/** @brief Get the wm's idea of the $XDG_DATA_HOME:$XDG_DATA_DIRS.
  *
  * This is used for window managers that place their theme files in the XDG
  * themes directories, such as Openbox and Metacity, as well as for finding XDE
  * theme files.
  */
void
get_xdg_dirs()
{
	char *home, *xhome, *xdata, *dirs, *pos, *end, **dir;
	int len, n;

	home = get_proc_environ("HOME") ? : ".";
	xhome = get_proc_environ("XDG_DATA_HOME");
	xdata = get_proc_environ("XDG_DATA_DIRS") ? : "/usr/local/share:/usr/share";

	len = (xhome ? strlen(xhome) : strlen(home) + strlen("/.local/share")) +
	    strlen(xdata) + 2;
	dirs = calloc(len, sizeof(*dirs));
	if (xhome)
		strcpy(dirs, xhome);
	else {
		strcpy(dirs, home);
		strcat(dirs, "/.local/share");
	}
	strcat(dirs, ":");
	strcat(dirs, xdata);
	end = dirs + strlen(dirs);
	for (n = 0, pos = dirs; pos < end;
	     n++, *strchrnul(pos, ':') = '\0', pos += strlen(pos) + 1) ;
	if (wm->xdg_dirs) {
		for (dir = wm->xdg_dirs; *dir; dir++)
			free(*dir);
		free(wm->xdg_dirs);
	}
	wm->xdg_dirs = calloc(n + 1, sizeof(*wm->xdg_dirs));
	for (n = 0, pos = dirs; pos < end; n++, pos += strlen(pos) + 1)
		wm->xdg_dirs[n] = strdup(pos);
	free(dirs);
	return;
}

/** @brief Determine if XDE theme name exists for window manager.
  * @return Bool - True when theme exists; False otherwise.
  */
Bool
find_xde_theme(char *name)
{
	char **dir, *file;
	int len, nlen;
	struct stat st;

	if (!wm->xdg_dirs)
		get_xdg_dirs();
	if (!wm->xdg_dirs)
		return False;

	nlen = strlen("/themes/") + strlen(name) + strlen("/xde/theme.ini");

	for (dir = wm->xdg_dirs; *dir; dir++) {
		len = strlen(*dir) + nlen + 1;
		file = calloc(len, sizeof(*file));
		strcpy(file, *dir);
		strcat(file, "/themes/");
		strcat(file, name);
		strcat(file, "/xde/theme.ini");
		if (stat(file, &st)) {
			DPRINTF("%s: %s\n", file, strerror(errno));
			free(file);
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			DPRINTF("%s: not a file\n", file);
			free(file);
			continue;
		}
		free(file);
		return True;
	}
	return False;
}

/** @} */

/** @name Window manager detection
  *
  * @{ */

/** @brief Check window for window manager name.
  *
  * Note that pekwm and openbox are setting a null WM_CLASS property on the
  * check window.  fvwm is setting WM_NAME and WM_CLASS property on the check
  * window.  Recent jwm, blackbox and icewm are properly setting _NET_WM_NAME
  * and WM_CLASS on the check window.
  */
char *
check_name(Window check)
{
	char *name;

	OPRINTF("checking wm name on window 0x%lx\n", check);

	if (!check)
		return NULL;
	if ((name = get_text(check, _XA_NET_WM_NAME)) && name[0])
		goto got_it_xfree;
	if (name)
		XFree(name);
	if ((name = get_text(check, XA_WM_NAME)) && name[0])
		goto got_it_xfree;
	if (name)
		XFree(name);
	if (wm->ch.res_name) {
		XFree(wm->ch.res_name);
		wm->ch.res_name = NULL;
	}
	if (wm->ch.res_class) {
		XFree(wm->ch.res_class);
		wm->ch.res_class = NULL;
	}
	if (XGetClassHint(dpy, check, &wm->ch)) {
		if ((name = wm->ch.res_name) && name[0])
			goto got_it;
		if ((name = wm->ch.res_class) && name[0])
			goto got_it;
	}
	if (wm->cargv) {
		XFreeStringList(wm->cargv);
		wm->cargv = NULL;
		wm->cargc = 0;
	}
	if (XGetCommand(dpy, check, &wm->cargv, &wm->cargc)) {
		if ((name = wm->cargv[0]) && name[0]) {
			name = strrchr(wm->cargv[0], '/') ? : wm->cargv[0];
			goto got_it;
		}
	} else if (XGetCommand(dpy, root, &wm->cargv, &wm->cargc)) {
		if ((name = wm->cargv[0]) && name[0]) {
			name = strrchr(wm->cargv[0], '/') ? : wm->cargv[0];
			goto got_it;
		}
	}
	return NULL;
      got_it:
	wm->name = strdup(name);
	goto first_word;
      got_it_xfree:
	wm->name = strdup(name);
	XFree(name);
      first_word:
	if ((name = strpbrk(wm->name, " \t\n\r:;,.")) && name != wm->name)
		*name = '\0';
	for (name = wm->name; *name; name++)
		*name = tolower(*name);
	OPRINTF("got wm name %s\n", wm->name);
	return wm->name;
}

char *
find_wm_name()
{
	int i;

	OPRINTF("checking wm name on screen %d\n", screen);

	free(wm->name);
	wm->name = NULL;
	for (i = 0; i < CHECK_WINS; i++)
		if (check_name(wm->wins[i]))
			break;
	if (!wm->name) {
		if (wm->maker_check)
			wm->name = strdup("wmaker");
		else if (wm->motif_check)
			wm->name = strdup("mwm");
	} else {
		/* CTWM with the old GNOME support uses the workspace manager window as a 
		   check window. New CTWM is fully NewWM/EWMH compliant. */
		if (!strcmp(wm->name, "workspacemanager"))
			strcpy(wm->name, "ctwm");
	}
	if (!wm->name)
		OPRINTF("could not find wm name on screen %d\n", screen);
	return wm->name;
}

char *
check_host(Window check)
{
	char *host;

	OPRINTF("checking wm host on window 0x%lx\n", check);

	if (!check)
		return NULL;
	if ((host = get_text(check, XA_WM_CLIENT_MACHINE)) && host[0])
		goto got_it_xfree;
	if (host)
		XFree(host);
	return NULL;
      got_it_xfree:
	wm->host = strdup(host);
	XFree(host);
	OPRINTF("got wm host %s\n", wm->host);
	return wm->host;
}

char *
check_same_host()
{
	char buf[66] = { 0, };
	int len1, len2;

	OPRINTF("checking wm same host on screen %d\n", screen);

	if (!wm->host)
		return wm->host;

	/* null out wm->host if it is the same as us */
	gethostname(buf, 64);
	len1 = strlen(wm->host);
	len2 = strlen(buf);
	if (len1 < len2) {
		if (!strncasecmp(wm->host, buf, len1) && buf[len1 + 1] == '.') {
			free(wm->host);
			wm->host = NULL;
			OPRINTF("%s\n", "host is local host");
			return wm->host;
		}
	} else if (len2 > len1) {
		if (!strncasecmp(wm->host, buf, len2) && wm->host[len2 + 1] == '.') {
			free(wm->host);
			wm->host = NULL;
			OPRINTF("%s\n", "host is local host");
			return wm->host;
		}
	} else {
		if (!strcasecmp(wm->host, buf)) {
			free(wm->host);
			wm->host = NULL;
			OPRINTF("%s\n", "host is local host");
			return wm->host;
		}
	}
	OPRINTF("%s\n", "host is remote host");
	return wm->host;
}

char *
find_wm_host()
{
	int i;

	OPRINTF("finding wm host on screen %d\n", screen);

	free(wm->host);
	wm->host = NULL;

	for (i = 0; i < CHECK_WINS; i++)
		if (check_host(wm->wins[i]))
			break;
	if (!wm->host)
		OPRINTF("could not find wm host on screen %d\n", screen);
	return check_same_host();
}

pid_t
check_pid(Window check)
{
	long pid;

	OPRINTF("checking wm pid on window 0x%lx\n", check);

	if (!check)
		return 0;
	if (get_cardinal(check, _XA_NET_WM_PID, XA_CARDINAL, &pid) && pid)
		goto got_it;
	if (wm->name && !strcasecmp(wm->name, "fluxbox"))
		if (get_cardinal(check, _XA_BLACKBOX_PID, XA_CARDINAL, &pid) && pid)
			goto got_it;
	if (wm->name && !strcasecmp(wm->name, "openbox"))
		if (get_cardinal(check, _XA_OPENBOX_PID, XA_CARDINAL, &pid) && pid)
			goto got_it;
	return 0;
      got_it:
	wm->pid = pid;
	OPRINTF("got wm pid %ld\n", pid);
	return pid;

}

pid_t
find_wm_pid()
{
	int i;

	OPRINTF("finding wm pid on screen %d\n", screen);

	wm->pid = 0;
	for (i = 0; i < CHECK_WINS; i++)
		if (check_pid(wm->wins[i]))
			break;
	if (!wm->pid)
		OPRINTF("could not find wm pid on screen %d\n", screen);
	return wm->pid;
}

char **
check_comm(Window check)
{
	char **argv;
	int argc;

	if (!check)
		return NULL;
	if (XGetCommand(dpy, check, &argv, &argc)) {
		if (wm->cargv)
			XFreeStringList(wm->cargv);
		wm->cargv = argv;
		wm->cargc = argc;
		OPRINTF("got wm command with %d args\n", argc);
		return argv;
	}
	return NULL;
}

char **
find_wm_comm()
{
	int i;

	OPRINTF("finding wm command on screen %d\n", screen);

	if (wm->cargv) {
		XFreeStringList(wm->cargv);
		wm->cargv = NULL;
		wm->cargc = 0;
	}
	for (i = 0; i < CHECK_WINS; i++)
		if (check_comm(wm->wins[i]))
			break;
	if (!wm->cargv)
		OPRINTF("could not find wm command on screen %d\n", screen);
	return wm->cargv;
}

char *wm_list[] = { "fluxbox", "blackbox", "openbox", "icewm", "jwm", "pekwm",
	"fvwm", "wmaker", "afterstep", "metacity", "twm", "ctwm", "vtwm",
	"etwm", "echinus", "uwm", "awesome", "matwm", "waimea", "wind", "2bwm",
	"wmx", "flwm", "mwm", "dtwm", "spectrwm", "yeahwm", NULL
};

/** @brief Find the wm process without name or pid.
  *
  * This might be more common that one thinks.  It may be possible to set the
  * style for a simple ICCCM < 2.0 compliant window manager but it needs to be
  * identified using means other than Xlib.  We search for a process with a
  * known window manager command name that has a DISPLAY environment that is
  * the same as ours.
  */
Bool
find_wm_proc_any()
{
	DIR *dir;
	struct dirent *d;
	pid_t pid;
	char *name, *buf, *pos, *end, *dsp, *dspnum;
	size_t size;
	int i;

	if (wm->host || wm->name || wm->pid)
		return False;

	if (!(dir = opendir("/proc"))) {
		EPRINTF("/proc: %s\n", strerror(errno));
		return False;
	}
	dsp = getenv("DISPLAY") ? : "";
	dspnum = strdup(dsp);
	if (strrchr(dspnum, '.'))
		*strrchr(dspnum, '.') = '\0';
	while ((d = readdir(dir))) {
		if (strspn(d->d_name, "0123456789") != strlen(d->d_name))
			continue;
		pid = atoi(d->d_name);
		if (!(name = get_proc_file(pid, "comm", &size))) {
			DPRINTF("no comm for pid %d\n", (int)pid);
			continue;
		}
		if (strrchr(name, '\n'))
			*strrchr(name, '\n') = '\0';
		OPRINTF("checking if '%s' is a window manager\n", name);
		for (i = 0; wm_list[i]; i++)
			if (!strcmp(wm_list[i], name))
				break;
		if (!wm_list[i]) {
			OPRINTF("%s is not a window manager\n", name);
			free(name);
			continue;
		}
		OPRINTF("%s is a window manager\n", name);
		if (!(buf = get_proc_file(pid, "environ", &size))) {
			DPRINTF("no environ for pid %d\n", (int)pid);
			continue;
		}
		OPRINTF("checking DISPLAY for %s\n", name);
		for (pos = buf, end = buf + size; pos < end; pos += strlen(pos) + 1)
			if (!strncmp(pos, "DISPLAY=", 8))
				break;
		if (pos >= end) {
			free(buf);
			OPRINTF("no DISPLAY= for %s\n", name);
			free(name);
			continue;
		}
		OPRINTF("testing %s for %s\n", pos, name);
		pos += strlen("DISPLAY=");
		if (!strcmp(dsp, pos) || !strcmp(dspnum, pos)) {
			free(buf);
			wm->name = name;
			wm->pid = pid;
			break;
		}
		OPRINTF("%s not on %s or %s\n", name, dsp, dspnum);
		free(buf);
		free(name);
	}
	closedir(dir);
	free(dspnum);
	return wm->pid ? True : False;
}

/** @brief Find the wm process by name.
  *
  * We have a window manager name by no process id on the local host.  Find a
  * process on the local host with an command of the same name that has a
  * DISPLAY environment variable and command arguments that indicate that it is
  * running on this screen.  Set the pid from that process.
  *
  * Unfortunately this is the common case for lesser window managers.  They may
  * properly set _NET_WM_NAME on the _NET_SUPPORTING_WM_CHECK window, but do
  * not set _NET_WM_PID.  Currently we just check whether the DISPLAY
  * environment variable of a process with the correct name is identical to
  * ours, which is the common case, particularly when we are run in the same
  * environment as the window manager.  This works in the general case, however,
  * if command arguments to the window manager can select the screen, the wm
  * might not necessarily be running on this screen.
  *
  */
Bool
find_wm_proc_by_name()
{
	DIR *dir;
	struct dirent *d;
	pid_t pid;
	char *buf, *pos, *end, *dsp;
	size_t size;

	if (!wm->name)
		return False;
	if (!(dir = opendir("/proc"))) {
		EPRINTF("/proc: %s\n", strerror(errno));
		return False;
	}
      dsp = strdup(getenv("DISPLAY") ? : "");
	while ((d = readdir(dir))) {
		if (strspn(d->d_name, "0123456789") != strlen(d->d_name))
			continue;
		pid = atoi(d->d_name);
		if (!(buf = get_proc_file(pid, "comm", &size)))
			continue;
		if (strrchr(buf, '\n'))
			*strrchr(buf, '\n') = '\0';
		if (strcmp(buf, wm->name)) {
			free(buf);
			continue;
		}
		free(buf);
		if (!(buf = get_proc_file(pid, "environ", &size)))
			continue;
		for (pos = buf, end = buf + size; pos < end; pos += strlen(pos) + 1)
			if (!strncmp(pos, "DISPLAY=", 8))
				break;
		if (pos >= buf) {
			free(buf);
			continue;
		}
		if (!strcmp(dsp, pos + 8)) {
			free(buf);
			wm->pid = pid;
			break;
		}
		free(buf);
	}
	closedir(dir);
	return wm->pid ? True : False;
}

/** @brief Find the wm process by pid.
  *
  * We have a process id on the local host but no name.  Use the pid to complete
  * the name.
  */
Bool
find_wm_proc_by_pid()
{
	char *buf;
	size_t size;

	if ((buf = get_proc_file(wm->pid, "comm", &size))) {
		if (strrchr(buf, '\n'))
			*strrchr(buf, '\n') = '\0';
		free(wm->name);
		wm->name = buf;
		return True;
	}
	return False;
}

/** @brief Check and fill out information for a window manager process.
  */
Bool
check_proc()
{
	char *buf;
	size_t size;
	Bool have_proc = False;

	OPRINTF("checking wm process for screen %d\n", screen);

	if (!wm->name || (wm->host || !wm->pid)) {
		EPRINTF("%s\n", "not enough information to check wm process");
		return have_proc;	/* no can do */
	}
	/* fill out the command line of the process */
	if ((buf = get_proc_file(wm->pid, "cmdline", &size))) {
		char *pos, *end;
		int i;

		free(wm->argv);
		wm->argc = 0;
		for (pos = buf, end = buf + size; pos < end; pos += strlen(pos) + 1)
			wm->argc++;
		wm->argv = calloc(wm->argc + 1, sizeof(*wm->argv));
		for (i = 0, pos = buf, end = buf + size; pos < end;
		     pos += strlen(pos) + 1)
			wm->argv[i++] = strdup(pos);
		free(buf);
		have_proc = True;
		OPRINTF("got cmdline with %d args\n", wm->argc);
	} else
		DPRINTF("could not get process cmdline for pid %ld", wm->pid);
	/* fill out the environment of the process */
	if ((buf = get_proc_file(wm->pid, "environ", &size))) {
		free(wm->env);
		wm->env = buf;
		wm->nenv = size;
		have_proc = True;
		OPRINTF("got environ with %d bytes\n", (int)size);
	} else {
		wm->noenv = True;
		EPRINTF("could not get process environ for pid %ld", wm->pid);
	}
	return have_proc;
}

/** @brief Find the process associated with a window manager.
  */
Bool
find_wm_proc()
{
	Bool have_proc = False;

	OPRINTF("finding wm process for screen %d\n", screen);

	if (wm->host) {
		/* not on this host */
		OPRINTF("%s\n", "process is remote");
		have_proc = False;
	} else if (wm->name && wm->pid) {
		OPRINTF("%s\n", "process already detected");
		have_proc = True;
	} else if (wm->name && !wm->pid) {
		/* have a name but no pid */
		OPRINTF("%s\n", "need to find process by name");
		have_proc = find_wm_proc_by_name();
	} else if (!wm->name && wm->pid) {
		/* have a pid but no name */
		OPRINTF("%s\n", "need to find process by pid");
		have_proc = find_wm_proc_by_pid();
	} else if (!wm->name && !wm->pid) {
		/* have no pid or name */
		OPRINTF("%s\n", "need to find process without name or pid");
		have_proc = find_wm_proc_any();
	}
	if (have_proc) {
		/* fill out various things from proc */
		OPRINTF("found process for screen %d\n", screen);
		return check_proc();
	}
	return False;
}

/** @brief Check for a window manager on the current screen.
  */
Bool
check_wm()
{
	OPRINTF("checking wm for screen %d\n", screen);
	unref_wm();
	ref_wm();

	find_wm_comp();		/* Check for window manager compliance */
	find_wm_name();		/* Check for window manager name */
	find_wm_host();		/* Check for window manager host */
	find_wm_pid();		/* Check for window manager pid */
	find_wm_comm();		/* Check for window manager command */
	find_wm_proc();		/* Check for window manager proc */

	if (wm->name && wm->pid) {
		WmScreen *s;
		int i;

		OPRINTF("checking for duplicate wm for screen %d\n", screen);

		for (i = 0; i < nscr; i++) {
			if (i == screen)
				continue;
			s = screens + i;
			if (!s->wm)
				continue;
			if ((wm->host == s->wm->host
			     || (wm->host && s->wm->host &&
				 !strcmp(wm->host, s->wm->host))) &&
			    (wm->pid && s->wm->pid && wm->pid == s->wm->pid)) {
				OPRINTF("found duplicate wm on screen %d\n",
						s->screen);
				unref_wm();
				wm = scr->wm = s->wm;
				ref_wm();
			}
		}
		return True;
	}
	unref_wm();
	return False;
}

void
show_wm()
{
	if (wm->netwm_check)
		OPRINTF("%d %s: NetWM 0x%lx\n", screen, wm->name, wm->netwm_check);
	if (wm->winwm_check)
		OPRINTF("%d %s: WinWM 0x%lx\n", screen, wm->name, wm->winwm_check);
	if (wm->maker_check)
		OPRINTF("%d %s: Maker 0x%lx\n", screen, wm->name, wm->maker_check);
	if (wm->icccm_check)
		OPRINTF("%d %s: ICCCM 0x%lx\n", screen, wm->name, wm->icccm_check);
	if (wm->redir_check)
		OPRINTF("%d %s: Redir 0x%lx\n", screen, wm->name, wm->redir_check);
	if (wm->pid)
		OPRINTF("%d %s: pid %ld\n", screen, wm->name, wm->pid);
	if (wm->host)
		OPRINTF("%d %s: host %s\n", screen, wm->name, wm->host);
	if (wm->argv && wm->argc) {
		int i, len;
		char *cmd;

		for (len = 0, i = 0; i < wm->argc; i++)
			len += strlen(wm->argv[i]) + 5;
		cmd = calloc(len, sizeof(*cmd));
		for (i = 0; i < wm->argc; i++) {
			strcat(cmd, i ? ", '" : "'");
			strcat(cmd, wm->argv[i]);
			strcat(cmd, "'");
		}
		OPRINTF("%d %s: cmdline %s\n", screen, wm->name, cmd);
	}
	if (wm->cargv && wm->cargc) {
		int i, len;
		char *cmd;

		for (len = 0, i = 0; i < wm->cargc; i++)
			len += strlen(wm->cargv[i]) + 5;
		cmd = calloc(len, sizeof(*cmd));
		for (i = 0; i < wm->cargc; i++) {
			strcat(cmd, i ? ", '" : "'");
			strcat(cmd, wm->cargv[i]);
			strcat(cmd, "'");
		}
		OPRINTF("%d %s: command %s\n", screen, wm->name, cmd);
	}
	if (wm->ch.res_name || wm->ch.res_class)
		OPRINTF("%d %s: '%s', '%s'\n", screen, wm->name,
				wm->ch.res_name ? : "(none)",
				wm->ch.res_class ? : "(none)");
	if (wm->rcfile)
		OPRINTF("%d %s: rcfile %s\n", screen, wm->name, wm->rcfile);
	if (wm->pdir)
		OPRINTF("%d %s: pdir %s\n", screen, wm->name, wm->pdir);
	if (wm->udir)
		OPRINTF("%d %s: udir %s\n", screen, wm->name, wm->udir);
	if (wm->sdir)
		OPRINTF("%d %s: sdir %s\n", screen, wm->name, wm->sdir);
	if (wm->stylefile)
		OPRINTF("%d %s: stylefile %s\n", screen, wm->name, wm->stylefile);
	if (wm->style)
		OPRINTF("%d %s: style %s\n", screen, wm->name, wm->style);
	if (wm->stylename)
		OPRINTF("%d %s: stylename %s\n", screen, wm->name, wm->stylename);
}

void
show_wms()
{
	int s;

	for (s = 0; s < nscr; s++) {
		screen = s;
		scr = screens + screen;
		root = scr->root;
		wm = scr->wm;
		if (wm)
			show_wm();
	}
}

Bool
detect_wm()
{
	Bool have_wm = False;
	int s;

	for (s = 0; s < nscr; s++) {
		screen = s;
		scr = screens + screen;
		root = scr->root;
		wm = scr->wm;
		if (check_wm()) {
			OPRINTF("found wm %s(%ld) for screen %d\n",
					wm->name, wm->pid, screen);
			have_wm = True;
			if (options.output > 1)
				show_wm();
		}
	}
	if (!have_wm)
		EPRINTF("%s\n", "could not find any window managers");
	return have_wm;
}

/** @} */

char *
get_optarg(char *optname)
{
	int i, l;
	char **argv;
	int argc;

	if (!optname)
		return NULL;

	l = strlen(optname);
	argv = wm->argv ? : wm->cargv ? : NULL;
	argc = wm->argv ? wm->argc : wm->cargv ? wm->cargc : 0;

	for (i = 1; i < argc; i++)
		if (strncmp(argv[i], optname, l) == 0)
			if ((argv[i][l] == '\0' && i < argc - 1) || argv[i][l] == '=')
				break;
	if (i < argc) {
		if (argv[i][l] == '=')
			return argv[i] + l + 1;
		return argv[i + 1];
	}
	return NULL;
}

void
get_simple_dirs(char *wmname)
{
	char *home = get_proc_environ("HOME") ? : ".";

	free(wm->pdir);
	wm->pdir = strdup(wm->rcfile);
	if (strrchr(wm->pdir, '/'))
		*strrchr(wm->pdir, '/') = '\0';
	free(wm->udir);
	wm->udir = calloc(strlen(home) + strlen(wmname) + 3, sizeof(*wm->udir));
	strcpy(wm->udir, home);
	strcat(wm->udir, "/.");
	strcat(wm->udir, wmname);
	free(wm->sdir);
	wm->sdir = calloc(strlen("/usr/share/") + strlen(wmname) + 1, sizeof(*wm->sdir));
	strcpy(wm->sdir, "/usr/share/");
	strcat(wm->sdir, wmname);
	if (!strcmp(wm->pdir, home)) {
		free(wm->pdir);
		wm->pdir = strdup(wm->udir);
	}
}

void
get_rcfile_simple(char *wmname, char *option)
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg(option);
	int len;
	struct stat st;

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			len = strlen(home) + strlen(file) + 2;
			wm->rcfile = calloc(len, sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		char rc[32];

		snprintf(rc, sizeof(rc), "/.%src", wmname);
		len = strlen(home) + strlen(rc) + 1;
		wm->rcfile = calloc(len, sizeof(*wm->rcfile));
		strcpy(wm->rcfile, home);
		strcat(wm->rcfile, rc);
		/* often this is symlinked into the actual directory */
		if (!lstat(wm->rcfile, &st) && S_ISLNK(st.st_mode)) {
			file = calloc(PATH_MAX + 1, sizeof(*file));
			if (readlink(wm->rcfile, file, PATH_MAX) == -1)
				EPRINTF("%s: %s\n", wm->rcfile, strerror(errno));
			if (file[0] == '/') {
				free(wm->rcfile);
				wm->rcfile = strdup(file);
			} else if (file[0]) {
				free(wm->rcfile);
				len = strlen(home) + strlen(file) + 2;
				wm->rcfile = calloc(len, sizeof(*wm->rcfile));
				strcpy(wm->rcfile, home);
				strcat(wm->rcfile, "/");
				strcat(wm->rcfile, file);
			}
			free(file);
		}
	}
	get_simple_dirs(wmname);
}

Bool
test_file(char *path)
{
	struct stat st;

	if (!path) {
		EPRINTF("%s", "null path\n");
		return False;
	}

	OPRINTF("testing file '%s' for existence\n", path);

	if (!stat(path, &st)) {
		if (S_ISREG(st.st_mode)) {
			if (!access(path, R_OK))
				return True;
			else
				DPRINTF("cannot read %s\n", path);
		} else
			DPRINTF("%s not regular file\n", path);
	} else
		EPRINTF("%s: %s\n", path, strerror(errno));
	return False;
}

/** @name FLUXBOX
  */
/** @{ */

/** @brief find the fluxbox init file and default directory.
  *
  * Fluxbox takes a command such as:
  *
  *   fluxbox [-rc RCFILE]
  *
  * When the RCFILE is not specified, ~/.fluxbox/init is used.  The locations of
  * other fluxbox configuration files are specified by the intitial
  * configuration file, but are typically placed under ~/.fluxbox.  System files
  * are placed under /usr/share/fluxbox.
  *
  * System styles are either in /usr/share/fluxbox/styles or in
  * ~/.fluxbox/styles.  Styles under these directories can either be a file or a
  * directory.  When a directory, the actual style file is in a file called
  * theme.cfg.
  */
void
get_rcfile_FLUXBOX()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("-rc");

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			wm->rcfile =
			    calloc(strlen(home) + strlen(file) + 2, sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		wm->rcfile =
		    calloc(strlen(home) + strlen("/.fluxbox/init") + 1,
			   sizeof(*wm->rcfile));
		strcpy(wm->rcfile, home);
		strcat(wm->rcfile, "/.fluxbox/init");
	}
	get_simple_dirs("fluxbox");
}

/** @brief Find a fluxbox style file from a style name.
  *
  * Fluxbox style files are named files or directories in
  * /usr/share/fluxbox/styles or ~/.fluxbox/styles.  When a named directory,
  * the directory must contain a file named theme.cfg.
  */
char *
find_style_FLUXBOX()
{
	char *path = NULL;
	int len, i;
	struct stat st;

	get_rcfile_FLUXBOX();
	if (options.style[0] == '/') {
		len = strlen(options.style) + strlen("/theme.cfg") + 1;
		path = calloc(len, sizeof(*path));
		strcpy(path, options.style);
		if (stat(path, &st)) {
			EPRINTF("%s: %s\n", path, strerror(errno));
			free(path);
			return NULL;
		}
		if (S_ISDIR(st.st_mode)) {
			strcat(path, "/theme.cfg");
			if (stat(path, &st)) {
				EPRINTF("%s %s\n", path, strerror(errno));
				free(path);
				return NULL;
			}
			*strrchr(path, '/') = '\0';
		} else if (!S_ISREG(st.st_mode)) {
			EPRINTF("%s: not directory or file\n", path);
			free(path);
			return NULL;
		}
	} else {
		for (i = 0; i < CHECK_DIRS; i++) {
			if (!wm->dirs[i] || !wm->dirs[i][0])
				continue;
			len = strlen(wm->dirs[i]) + strlen("/styles/") +
			    strlen(options.style) + strlen("/theme.cfg") + 1;
			path = calloc(len, sizeof(*path));
			strcpy(path, wm->dirs[i]);
			strcat(path, "/styles/");
			strcat(path, options.style);
			if (stat(path, &st)) {
				DPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				path = NULL;
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				strcat(path, "/theme.cfg");
				if (stat(path, &st)) {
					DPRINTF("%s: %s\n", path, strerror(errno));
					free(path);
					path = NULL;
					continue;
				}
				*strrchr(path, '/') = '\0';
			} else if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not directory or file\n", path);
				free(path);
				path = NULL;
				continue;
			}
			break;
		}
	}
	return path;
}

/** @brief Get the current fluxbox style.
  *
  * The current fluxbox style is set in the session.styleFile resource in the rc
  * file.
  */
char *
get_style_FLUXBOX()
{
	XrmValue value;
	char *type, *pos;

	if (wm->style)
		return wm->style;

	get_rcfile_FLUXBOX();
	if (!test_file(wm->rcfile)) {
		EPRINTF("rcfile '%s' does not exist\n", wm->rcfile);
		return NULL;
	}
	init_xrm();
	if (!wm->db && !(wm->db = XrmGetFileDatabase(wm->rcfile))) {
		EPRINTF("cannot read database file '%s'\n", wm->rcfile);
		return NULL;
	}
	if (!XrmGetResource(wm->db, "session.styleFile", "Session.StyleFile",
			    &type, &value)) {
		EPRINTF("%s", "no session.styleFile resource in database\n");
		return NULL;
	}
	free(wm->style);
	wm->style = strndup((char *) value.addr, value.size);
	free(wm->stylename);
	wm->stylename = (pos = strrchr(wm->style, '/')) ?
	    strdup(pos + 1) : strdup(wm->style);
	return wm->style;
}

/** @brief Set the fluxbox style.
  *
  * When fluxbox changes the style, it writes the path to the new style in the
  * session.StyleFile resource in the rc file (default ~/.fluxbox/init) and then
  * reloads the configuration.  The session.styleFile entry looks like:
  *
  *   session.styleFile: /usr/share/fluxbox/styles/Airforce
  *
  * Unlike other window managers, it reloads the configuration rather than
  * restarting.  However, fluxbox has the problem that simply reloading the
  * configuration does not result in a change to the menu styles (in particular
  * the font color), so a restart is likely required.
  *
  * Sending SIGUSR2 to the fluxbox PID provided in the _BLACKBOX_PID property on
  * the root window will result in a reconfigure of fluxbox (which is what
  * fluxbox itself does when changing styles); send SIGHUP, a restart.
  *
  * Note that when fluxbox restarts, it dow not change the
  * _NET_SUPPORTING_WM_CHECK root window property but it does change the
  * _BLACKBOX_PID root window property, even if it is just to replace it with
  * the same value again.
  */
void
set_style_FLUXBOX()
{
	char *stylefile, *line, *style;
	int len;

	get_rcfile_FLUXBOX();
	if (!wm->pid) {
		EPRINTF("%s", "cannot set fluxbox style without pid\n");
		goto no_pid;
	}
	if (!test_file(wm->rcfile)) {
		EPRINTF("rcfile '%s' does not exist\n", wm->rcfile);
		goto no_rcfile;
	}
	if (!(stylefile = find_style_FLUXBOX())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		goto no_stylefile;
	}
	if ((style = get_style_FLUXBOX()) && !strcmp(style, stylefile))
		goto no_change;
	init_xrm();
	if (!wm->db && !(wm->db = XrmGetFileDatabase(wm->rcfile))) {
		EPRINTF("cannot read database file '%s'\n", wm->rcfile);
		goto no_db;
	}
	len = strlen(stylefile) + strlen("session.styleFile:\t\t") + 1;
	line = calloc(len, sizeof(*line));
	snprintf(line, len, "session.styleFile:\t\t%s", stylefile);
	XrmPutLineResource(&wm->db, line);
	free(line);
	if (options.dryrun)
		goto no_change;
	XrmPutFileDatabase(wm->db, wm->rcfile);
	kill(wm->pid, SIGUSR2);
      no_change:
	if (wm->db) {
		XrmDestroyDatabase(wm->db);
		wm->db = NULL;
	}
      no_db:
	free(stylefile);
      no_stylefile:
      no_rcfile:
      no_pid:
	return;
}

void
list_dir_FLUXBOX(char *xdir, char *style)
{
	DIR *dir;
	char *dirname, *file;
	struct dirent *d;
	struct stat st;
	int len;

	if (!xdir)
		return;
	len = strlen(xdir) + strlen("/styles") + 1;
	dirname = calloc(len, sizeof(*dirname));
	strcpy(dirname, xdir);
	strcat(dirname, "/styles");
	if ((dir = opendir(dirname))) {
		while ((d = readdir(dir))) {
			if (d->d_name[0] == '.')
				continue;
			len = strlen(dirname) + strlen(d->d_name) +
			    strlen("/theme.cfg") + 2;
			file = calloc(len, sizeof(*file));
			strcpy(file, dirname);
			strcat(file, "/");
			strcat(file, d->d_name);
			if (stat(file, &st)) {
				DPRINTF("%s: %s\n", file, strerror(errno));
				free(file);
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				strcat(file, "/theme.cfg");
				if (stat(file, &st)) {
					DPRINTF("%s: %s\n", file, strerror(errno));
					free(file);
					continue;
				}
				*strrchr(file, '/') = '\0';
			} else if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not a directory or file\n", file);
				free(file);
				continue;
			}
			if (!options.theme || find_xde_theme(d->d_name))
				fprintf(stdout, "%s %s%s\n", d->d_name, file,
					(style && !strcmp(style, file)) ? " *" : "");
			free(file);
		}
		closedir(dir);
	} else
		EPRINTF("%s: %s\n", dirname, strerror(errno));
	free(dirname);
}

/** @brief List fluxbox styles.
  */
void
list_styles_FLUXBOX()
{
	char *style = get_style_FLUXBOX();

	if (options.user) {
		if (wm->pdir)
			list_dir_FLUXBOX(wm->pdir, style);
		if (wm->udir && (!wm->pdir || strcmp(wm->pdir, wm->udir)))
			list_dir_FLUXBOX(wm->udir, style);
	}
	if (options.system) {
		if (wm->sdir)
			list_dir_FLUXBOX(wm->sdir, style);
	}
}

/** @} */

/** @name BLACKBOX
  */
/** @{ */

/** @brief Find the blackbox rc file and default directory.
  *
  * Blackbox takes a command such as:
  *
  *   blackbox [-rc RCFILE]
  *
  * When the RCFILE is not specified, ~/.blackboxrc is used.  The locations of
  * other fluxbox configuration files are specified by the initial configuration
  * file, but are typically placed under ~/.blackbox.  System files are placed
  * under /usr/share/blackbox.
  */
void
get_rcfile_BLACKBOX()
{
	return get_rcfile_simple("blackbox", "-rc");
}

/** @brief Find a blackbox style file from a style name.
  *
  * Blackbox style files are named files in /usr/share/blackbox/styles or
  * ~/.blackbox/styles.
  */
char *
find_style_BLACKBOX()
{
	char *path = NULL;
	int len, i;
	struct stat st;

	get_rcfile_BLACKBOX();
	if (options.style[0] == '/') {
		path = strdup(options.style);
		if (stat(path, &st)) {
			EPRINTF("%s: %s\n", path, strerror(errno));
			free(path);
			return NULL;
		}
		if (!S_ISREG(st.st_mode)) {
			EPRINTF("%s: not a file\n", path);
			free(path);
			return NULL;
		}
	} else if (strchr(options.style, '/')) {
		EPRINTF("%s: path is not absolute\n", options.style);
	} else {
		for (i = 0; i < CHECK_DIRS; i++) {
			if (!wm->dirs[i] || !wm->dirs[i][0])
				continue;
			len = strlen(wm->dirs[i]) + strlen("/styles/") +
			    strlen(options.style) + 1;
			path = calloc(len, sizeof(*path));
			strcpy(path, wm->dirs[i]);
			strcat(path, "/styles/");
			strcat(path, options.style);
			if (stat(path, &st)) {
				DPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				path = NULL;
				continue;
			}
			if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not a file\n", path);
				free(path);
				path = NULL;
				continue;
			}
			break;
		}
	}
	return path;
}

char *
get_style_BLACKBOX()
{
	XrmValue value;
	char *type, *pos;

	if (wm->style)
		return wm->style;

	get_rcfile_BLACKBOX();
	if (!test_file(wm->rcfile)) {
		EPRINTF("rcfile '%s' does not exist\n", wm->rcfile);
		return NULL;
	}
	init_xrm();
	if (!wm->db && !(wm->db = XrmGetFileDatabase(wm->rcfile))) {
		EPRINTF("cannot read database file '%s'\n", wm->rcfile);
		return NULL;
	}
	if (!XrmGetResource(wm->db, "session.styleFile", "Session.StyleFile",
			    &type, &value)) {
		EPRINTF("%s", "no session.styleFile resource in database\n");
		return NULL;
	}
	free(wm->style);
	wm->style = strndup((char *) value.addr, value.size);
	free(wm->stylename);
	wm->stylename = (pos = strrchr(wm->style, '/')) ?
	    strdup(pos + 1) : strdup(wm->style);
	return wm->style;
}

/*
 * When blackbox changes the style, it writes the path to the new style in the
 * session.styleFile resource in the ~/.blackboxrc file and then reloads the
 * configuration.
 *
 * The session.styleFile entry looks like:
 *
 *   session.styleFile:	/usr/share/blackbox/styles/Airforce
 *
 * Unlike other window managers, it reloads the configuration rather than restarting.
 * Sending SIGUSR1 to the blackbox PID provided in _NET_WM_PID property on the
 * _NET_SUPPORTING_WM_CHECK window> will effect the reconfiguration that results in
 * rereading of the style file.
 */
void
set_style_BLACKBOX()
{
	char *stylefile, *line, *style;
	int len;

	if (!wm->pid) {
		EPRINTF("%s", "cannot set blackbox style without pid\n");
		goto no_pid;
	}
	if (!test_file(wm->rcfile)) {
		EPRINTF("rcfile '%s' does not exist\n", wm->rcfile);
		goto no_rcfile;
	}
	if (!(stylefile = find_style_BLACKBOX())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		goto no_stylefile;
	}
	if ((style = get_style_BLACKBOX()) && !strcmp(style, stylefile))
		goto no_change;
	init_xrm();
	if (!wm->db && !(wm->db = XrmGetFileDatabase(wm->rcfile))) {
		EPRINTF("cannot read database file '%s'\n", wm->rcfile);
		goto no_db;
	}
	len = strlen(stylefile) + strlen("session.styleFile:\t\t") + 1;
	line = calloc(len, sizeof(*line));
	snprintf(line, len, "session.styleFile:\t\t%s", stylefile);
	XrmPutLineResource(&wm->db, line);
	free(line);
	if (options.dryrun)
		goto no_change;
	XrmPutFileDatabase(wm->db, wm->rcfile);
	kill(wm->pid, SIGUSR1);
      no_change:
	if (wm->db) {
		XrmDestroyDatabase(wm->db);
		wm->db = NULL;
	}
      no_db:
	free(stylefile);
      no_stylefile:
      no_rcfile:
      no_pid:
	return;
}

void
list_dir_BLACKBOX(char *xdir, char *style)
{
	DIR *dir;
	char *dirname, *file;
	struct dirent *d;
	struct stat st;
	int len;

	if (!xdir)
		return;
	len = strlen(xdir) + strlen("/styles") + 1;
	dirname = calloc(len, sizeof(*dirname));
	strcpy(dirname, xdir);
	strcat(dirname, "/styles");
	if ((dir = opendir(dirname))) {
		while ((d = readdir(dir))) {
			if (d->d_name[0] == '.')
				continue;
			len = strlen(dirname) + strlen(d->d_name) + 2;
			file = calloc(len, sizeof(*file));
			strcpy(file, dirname);
			strcat(file, "/");
			strcat(file, d->d_name);
			if (stat(file, &st)) {
				EPRINTF("%s: %s\n", file, strerror(errno));
				free(file);
				continue;
			}
			if (S_ISREG(st.st_mode))
				if (!options.theme || find_xde_theme(d->d_name))
					fprintf(stdout, "%s %s%s\n", d->d_name, file,
						(style && !strcmp(style, file)) ? " *" : "");
			free(file);
		}
		closedir(dir);
	} else
		EPRINTF("%s: %s\n", dirname, strerror(errno));
	free(dirname);
}

void
list_styles_BLACKBOX()
{
	char *style = get_style_BLACKBOX();

	if (options.user) {
		if (wm->pdir)
			list_dir_BLACKBOX(wm->pdir, style);
		if (wm->udir && (!wm->pdir || strcmp(wm->pdir, wm->udir)))
			list_dir_BLACKBOX(wm->udir, style);
	}
	if (options.system) {
		if (wm->sdir)
			list_dir_BLACKBOX(wm->sdir, style);
	}
}

/** @} */

/** @name OPENBOX
  */
/** @{ */

/** @brief Find the openbox rc file and default directory.
  *
  * Openbox takes a command such as:
  *
  *   openbox [--config-file RCFILE]
  *
  * When RCFILE is not specified, $XDG_CONFIG_HOME/openbox/rc.xml is used.  The
  * locations of other openbox configuration files are specified by the initial
  * configuration file, but are typically placed under ~/.config/openbox.
  * System files are placed under /usr/share/openbox.
  */
void
get_rcfile_OPENBOX()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("--config-file");
	char *cnfg = get_proc_environ("XDG_CONFIG_HOME");
	int len;

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			wm->rcfile =
			    calloc(strlen(home) + strlen(file) + 2, sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		if (cnfg) {
			wm->rcfile = calloc(strlen(cnfg) + strlen("/openbox/rc.xml") + 1,
					    sizeof(*wm->rcfile));
			strcpy(wm->rcfile, cnfg);
		} else {
			wm->rcfile = calloc(strlen(home) + strlen("/.config") +
					    strlen("/openbox/rc.xml") + 1,
					    sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/.config");
		}
		strcat(wm->rcfile, "/openbox/rc.xml");
	}
	free(wm->pdir);
	wm->pdir = strdup(wm->rcfile);
	if (strrchr(wm->pdir, '/'))
		*strrchr(wm->pdir, '/') = '\0';
	free(wm->udir);
	if (cnfg) {
		len = strlen(cnfg) + strlen("/openbox") + 1;
		wm->udir = calloc(len, sizeof(*wm->udir));
		strcpy(wm->udir, cnfg);
		strcat(wm->udir, "/openbox");
	} else {
		len = strlen(home) + strlen("/.config/openbox") + 1;
		wm->udir = calloc(len, sizeof(*wm->udir));
		strcpy(wm->udir, home);
		strcat(wm->udir, "/.config/openbox");
	}
	free(wm->sdir);
	wm->sdir = strdup("/usr/share/openbox");
	return;
}

/** @brief Find an openbox style file.
  *
  * Openbox sytle file are XDG organized: that is, the searched directories are
  * $XDG_DATA_HOME:$XDG_DATA_DIRS with appropriate defaults.  Subdirectories
  * under the themes directory (e.g. /usr/share/themes/STYLENAME) that contain
  * an openbox-3 subdirectory containing a themerc file.
  *
  * Because openbox uses the XDG scheme, it does not distinguish between system
  * and user styles.
  */
char *
find_style_OPENBOX()
{
	char *dirs, *path, *file;
	char *pos, *end;

	if (strchr(options.style, '/')) {
		EPRINTF("path in openbox style name '%s'\n", wm->style);
		return NULL;
	}

	get_rcfile_OPENBOX();

	dirs = calloc(PATH_MAX, sizeof(*dirs));
	path = calloc(PATH_MAX, sizeof(*path));
	file = calloc(PATH_MAX, sizeof(*path));

	strcpy(file, "/themes/");
	strcat(file, wm->style);
	strcat(file, "/openbox-3/themerc");

	if (getenv("XDG_DATA_HOME"))
		strcat(dirs, getenv("XDG_DATA_HOME"));
	else {
		strcat(dirs, getenv("HOME") ? : ".");
		strcat(dirs, "/.local/share");
	}
	strcat(dirs, ":");
	if (getenv("XDG_DATA_DIRS"))
		strcat(dirs, getenv("XDG_DATA_DIRS"));
	else
		strcat(dirs, "/usr/local/share:/usr/share");
	for (pos = dirs, end = pos + strlen(dirs); pos < end;
	     pos = strchrnul(pos, ':'), pos[0] = '\0', pos++) ;
	for (pos = dirs; pos < end; pos += strlen(pos) + 1) {
		strcpy(path, pos);
		strcat(path, file);
		if (test_file(path))
			goto got_it;
	}
	free(path);
	free(dirs);
	free(file);
	EPRINTF("could not find path for style '%s'\n", wm->style);
	return NULL;
      got_it:
	free(dirs);
	free(file);
	return path;

}

char *
get_style_OPENBOX()
{
	get_rcfile_OPENBOX();
	return NULL;
}

/*
 * When openbox changes its theme, it changes the _OB_THEME property on the root
 * window.  openbox also changes the theme section in ~/.config/openbox/rc.xml and
 * writes the file and performs a reconfigure.
 *
 * openbox sets the _OB_CONFIG_FILE property on the root window when the
 * configuration file differs from the default (but not otherwise).
 *
 * openbox does not provide internal actions for setting the theme: it uses an
 * external theme setting program that communicates with the window manager.
 *
 * openbox can be reconfigured by sending an _OB_CONTROL message to the root window
 * with a control type in data.l[0].  The control type can be one of:
 *
 * OB_CONTROL_RECONFIGURE    1   reconfigure
 * OB_CONTROL_RESTART        2   restart
 * OB_CONTROL_EXIT           3   exit
 *
 * When xde-session runs, it sets the OPENBOX_RCFILE environment variable.
 * xde-session and associated tools will always launch openbox with a command such
 * as:
 *
 *   openbox ${OPENBOX_RCFILE:+--config-file $OPENBOX_RCFILE}
 *
 * The default configuration file when OPENBOX_RCFILE is not specified is
 * $XDG_CONFIG_HOME/openbox/rc.xml.  The location of other openbox configuration
 * files are specified by the initial configuration file.  xde-session typically sets
 * OPENBOX_RCFILE to $XDG_CONFIG_HOME/openbox/xde-rc.xml.
 */
#define OB_CONTROL_RECONFIGURE	    1	/* reconfigure */
#define OB_CONTROL_RESTART	    2	/* restart */
#define OB_CONTROL_EXIT		    3	/* exit */

void
set_style_OPENBOX()
{
	XEvent ev;
	char *stylefile;

	if (!(stylefile = find_style_OPENBOX())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}

	if (options.dryrun)
		goto no_change;

	ev.xclient.type = ClientMessage;
	ev.xclient.display = dpy;
	ev.xclient.window = root;
	ev.xclient.message_type = XInternAtom(dpy, "_OB_CONTROL", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = OB_CONTROL_RECONFIGURE;
	ev.xclient.data.l[1] = 0;
	ev.xclient.data.l[2] = 0;
	ev.xclient.data.l[3] = 0;
	ev.xclient.data.l[4] = 0;
	XSendEvent(dpy, root, False,
		   SubstructureNotifyMask | SubstructureRedirectMask, &ev);
	XFlush(dpy);
      no_change:
	return;
}

void
list_dir_OPENBOX(char *xdir)
{
	DIR *dir;
	char *dirname, *file;
	struct dirent *d;
	struct stat st;
	int len;

	if (!xdir || !*xdir)
		return;
	len = strlen(xdir) + strlen("/themes") + 1;
	dirname = calloc(len, sizeof(*dirname));
	strcpy(dirname, xdir);
	strcat(dirname, "/themes");
	if ((dir = opendir(dirname))) {
		while ((d = readdir(dir))) {
			if (d->d_name[0] == '.')
				continue;
			len = strlen(dirname) + strlen(d->d_name) +
				strlen("/openbox-3/themerc") + 2;
			file = calloc(len, sizeof(*file));
			strcpy(file, dirname);
			strcat(file, "/");
			strcat(file, d->d_name);
			strcat(file, "/openbox-3/themerc");
			if (stat(file, &st)) {
				DPRINTF("%s: %s\n", file, strerror(errno));
				free(file);
				continue;
			}
			if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not a file\n", file);
				free(file);
				continue;
			}
			if (!options.theme || find_xde_theme(d->d_name))
				fprintf(stdout, "%s %s\n", d->d_name, file);
			free(file);
		}
		closedir(dir);
	} else
		EPRINTF("%s: %s\n", dirname, strerror(errno));
	free(dirname);
}

void
list_styles_OPENBOX()
{
	char *data, *home, *dirs, *dir, *end;
	int len;

	home = strdup(getenv("HOME") ?: ".");
	len = strlen(home) + strlen("/.local.share") + 1;
	dirs = calloc(len, sizeof(*dirs));
	strcpy(dirs, home);
	strcat(dirs, "/.local/share");
	data = strdup(getenv("XDG_DATA_DIRS") ?: "/usr/local/share:/usr/share");
	home = strdup(getenv("XDG_DATA_HOME") ?: dirs);
	free(dirs);
	len = strlen(home) + strlen(data) + 2;
	dirs = calloc(len, sizeof(*dirs));
	strcpy(dirs, home);
	strcat(dirs, ":");
	strcat(dirs, data);
	free(data);
	free(home);
	for (dir = dirs, end = dir + strlen(dir); dir < end; dir += strlen(dir) + 1) {
		*strchrnul(dir, ':') = '\0';
		list_dir_OPENBOX(dir);
	}
	free(dirs);
}

/** @} */

/** @name ICEWM
  */
/** @{ */

/* @brief Find the icewm rc file and default directory.
 *
 * Icewm takes a command such as: 
 *
 *   icewm [-c, --config=RCFILE] [-t, --theme=FILE]
 *
 * The default if RCFILE is not specified is ~/.icewm/preferences, unless the
 * ICEWM_PRIVCFG environment variable is specified, 
 * 
 *
 */
void
get_rcfile_ICEWM()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("--config");
	char *cnfg = get_proc_environ("ICEWM_PRIVCFG");

	free(wm->udir);
	wm->udir = calloc(strlen(home) + strlen("/.icewm") + 1, sizeof(*wm->udir));
	strcpy(wm->udir, home);
	strcat(wm->udir, "/.icewm");
	free(wm->pdir);
	if (cnfg) {
		if (cnfg[0] == '/')
			wm->pdir = strdup(cnfg);
		else {
			wm->pdir =
			    calloc(strlen(home) + strlen(cnfg) + 2, sizeof(*wm->pdir));
			strcpy(wm->pdir, home);
			strcat(wm->pdir, "/");
			strcat(wm->pdir, cnfg);
		}
	} else
		wm->pdir = strdup(wm->udir);
	free(wm->sdir);
	wm->sdir = strdup("/usr/share/icewm");

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			wm->rcfile =
			    calloc(strlen(wm->pdir) + strlen(file) + 2,
				   sizeof(*wm->rcfile));
			strcpy(wm->rcfile, wm->pdir);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		wm->rcfile =
		    calloc(strlen(wm->pdir) + strlen("/preferences") + 1,
			   sizeof(*wm->rcfile));
		strcpy(wm->rcfile, wm->pdir);
		strcat(wm->rcfile, "/preferences");
	}
}

/** @brief Find an icewm style file.
  *
  * IceWM cannot distinguish between system and user styles.  The theme name
  * specifies a directory in the /usr/share/icewm/themes, ~/.icewm/themes or
  * $ICEWM_PRIVCFG/themes subdirectories.  Theme files can either be a
  * default.theme file in the subdirectory of the theme name, or theme
  * variations can be provided in $VARIATION.theme files.  Theme names can be
  * specified as, e.g. "Airforce" or "Airforce/default" or
  * "ElbergBlue/Wallpaper" for the variation.  The actual file for "Airforce" is
  * "Airforce/default.theme" in one of the theme directories.  The actual type
  * for "ElbergBlue/Wallpaper" is ElbergBlue/Wallpaper.theme.
  */
char *
find_style_ICEWM()
{
	char *p, *file, *path = calloc(PATH_MAX, sizeof(*path));
	int i, len;

	if (options.style[0] == '.' || options.style[0] == '/') {
		EPRINTF("path in icewm style name '%s'\n", wm->style);
		return NULL;
	}
	get_rcfile_ICEWM();
	if (!(strchr(options.style, '/'))) {
		len = strlen(options.style) + strlen("/default.theme") + 1;
		file = calloc(len, sizeof(*file));
		snprintf(file, len, "%s/default.theme", options.style);
	} else if (!(p = strstr(options.style, ".theme"))
		   || p[strlen(".theme") + 1] != '\0') {
		len = strlen(options.style) + strlen(".theme") + 1;
		file = calloc(len, sizeof(*file));
		snprintf(file, len, "%s.theme", options.style);
	} else {
		len = strlen(options.style) + 1;
		file = calloc(len, sizeof(*file));
		snprintf(file, len, "%s", options.style);
	}
	for (i = 0; i < CHECK_DIRS; i++) {
		if (!wm->dirs[i] || !wm->dirs[i][0])
			continue;
		snprintf(path, PATH_MAX, "%s/themes/%s", wm->dirs[i], file);
		if (test_file(path))
			break;
	}
	if (i < CHECK_DIRS) {
		free(file);
		return path;
	}
	free(path);
	return NULL;
}

char *
get_style_ICEWM()
{
	get_rcfile_ICEWM();
	return NULL;
}

/*
 * When icewm changes the style, it writes the new style to the ~/.icewm/theme or
 * $ICEWM_PRIVCFG/theme file and then restarts.  The ~/.icewm/theme file looks like:
 *
 *   Theme="Penguins/default.theme"
 *   #Theme="Airforce/default.theme"
 *   ##Theme="Penguins/default.theme"
 *   ###Theme="Pedestals/default.theme"
 *   ####Theme="Penguins/default.theme"
 *   #####Theme="Airforce/default.theme"
 *   ######Theme="Archlinux/default.theme"
 *   #######Theme="Airforce/default.theme"
 *   ########Theme="Airforce/default.theme"
 *   #########Theme="Airforce/default.theme"
 *   ##########Theme="Penguins/default.theme"
 *
 * icewm cannot distinguish between system an user styles.  The theme name specifies
 * a directory in the /usr/share/icewm/themes, ~/.icewm/themes or
 * $ICEWM_PRIVCFG/themes subdirectories.
 *
 * There are two ways to get icewm to reload the theme, one is to send a SIGHUP to
 * the window manager process.  The other is to send an _ICEWM_ACTION client message
 * to the root window.
 *
 * When xde-session runs, it sets the ICEWM_PRIVCFG environment variable.
 * xde-session and associated tools will always set this environment variable before
 * launching icewm.  icewm respects this environment variable and no special options
 * are necessary when launching icewm.
 *
 * The default configuration directory when ICEWM_PRIVCFG is not specified is
 * ~/.icewm.  The location of all other icewm configuration files are in this
 * directory.  xde-session typically sets ICEWM_PRIVCFG to $XDG_CONFIG_HOME/icewm.
 */

#define ICEWM_ACTION_NOP		0
#define ICEWM_ACTION_PING		1
#define ICEWM_ACTION_LOGOUT		2
#define ICEWM_ACTION_CANCEL_LOGOUT	3
#define ICEWM_ACTION_REBOOT		4
#define ICEWM_ACTION_SHUTDOWN		5
#define ICEWM_ACTION_ABOUT		6
#define ICEWM_ACTION_WINDOWLIST		7
#define ICEWM_ACTION_RESTARTWM		8

void
set_style_ICEWM()
{
	FILE *f;
	struct stat st;
	char *stylefile, *themerc, *buf, *pos, *end, *line;
	int n, len;
	size_t read, total;
	XEvent ev;

	if (!(stylefile = find_style_ICEWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		goto no_stylefile;
	}
	free(stylefile);

	len = strlen(wm->pdir) + strlen("/theme") + 1;
	themerc = calloc(len, sizeof(*themerc));
	snprintf(themerc, len, "%s/theme", wm->pdir);

	if (!(f = fopen(themerc, "r"))) {
		EPRINTF("%s: %s\n", themerc, strerror(errno));
		goto no_themerc;
	}
	if (fstat(fileno(f), &st)) {
		EPRINTF("%s: %s\n", themerc, strerror(errno));
		goto no_stat;
	}
	buf = calloc(st.st_size + 1, sizeof(*buf));
	/* read entire file into buffer */
	for (total = 0; total < st.st_size; total += read)
		if ((read = fread(buf + total, 1, st.st_size - total, f)))
			if (total < st.st_size)
				goto no_buf;

	len = strlen(options.style) + strlen("Theme=\"\"") + 1;
	line = calloc(len, sizeof(*line));
	snprintf(line, len, "Theme=\"%s\"", options.style);
	if (strncmp(buf, line, strlen(line)) == 0)
		goto no_change;

	if (options.dryrun)
		goto no_change;
	if (!(f = freopen(themerc, "w", f))) {
		EPRINTF("%s: %s\n", themerc, strerror(errno));
		goto no_change;
	}
	fprintf(f, "Theme=\"%s\"\n", options.style);
	for (n = 0, pos = buf, end = buf + st.st_size; pos < end && n < 10;
	     n++, pos = pos + strlen(pos) + 1) {
		*strchrnul(pos, '\n') = '\0';
		fprintf(stderr, "#%s\n", pos);
	}
	if (wm->pid) {
		kill(wm->pid, SIGHUP);
		goto no_change;
	}
	ev.xclient.type = ClientMessage;
	ev.xclient.display = dpy;
	ev.xclient.window = root;
	ev.xclient.message_type = XInternAtom(dpy, "_ICEWM_ACTION", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = ICEWM_ACTION_RESTARTWM;
	ev.xclient.data.l[1] = 0;
	ev.xclient.data.l[2] = 0;
	ev.xclient.data.l[3] = 0;
	ev.xclient.data.l[4] = 0;
	XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask,
		   &ev);
      no_change:
	free(line);
      no_buf:
	free(buf);
      no_stat:
	fclose(f);
      no_themerc:
	free(themerc);
      no_stylefile:
	return;
}

void
list_dir_ICEWM(char *xdir, char *style)
{
	DIR *dir, *sub;
	char *dirname, *subdir, *file, *pos, *name;
	struct dirent *d, *e;
	struct stat st;
	int len;

	if (!xdir || !*xdir)
		return;
	len = strlen(xdir) + strlen("/themes") + 1;
	dirname = calloc(len, sizeof(*dirname));
	strcpy(dirname, xdir);
	strcat(dirname, "/themes");
	if (!(dir = opendir(dirname))) {
		EPRINTF("%s: %s\n", dirname, strerror(errno));
		free(dirname);
		return;
	}
	while ((d = readdir(dir))) {
		if (d->d_name[0] == '.')
			continue;
		len = strlen(dirname) + strlen(d->d_name) + 2;
		subdir = calloc(len, sizeof(*subdir));
		strcpy(subdir, dirname);
		strcat(subdir, "/");
		strcat(subdir, d->d_name);
		if (stat(subdir, &st)) {
			EPRINTF("%s: %s\n", subdir, strerror(errno));
			free(subdir);
			continue;
		}
		if (!S_ISDIR(st.st_mode)) {
			DPRINTF("%s: not a directory\n", subdir);
			free(subdir);
			continue;
		}
		if (!(sub = opendir(subdir))) {
			EPRINTF("%s: %s\n", subdir, strerror(errno));
			free(subdir);
			continue;
		}
		while ((e = readdir(sub))) {
			if (e->d_name[0] == '.')
				continue;
			if (!(pos = strstr(e->d_name, ".theme")) ||
			    pos != e->d_name + strlen(e->d_name) - 6)
				continue;
			len = strlen(subdir) + strlen(e->d_name) + 2;
			file = calloc(len, sizeof(*file));
			strcpy(file, subdir);
			strcat(file, "/");
			strcat(file, e->d_name);
			if (stat(file, &st)) {
				EPRINTF("%s: %s\n", file, strerror(errno));
				free(file);
				continue;
			}
			if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not a regular file\n", file);
				free(file);
				continue;
			}
			*(pos = strstr(e->d_name, ".theme")) = '\0';
			len = strlen(d->d_name) + strlen(e->d_name) + 2;
			name = calloc(len, sizeof(*name));
			strcpy(name, d->d_name);
			strcat(name, "/");
			strcat(name, e->d_name);
			if (!options.theme || find_xde_theme(name))
				fprintf(stdout, "%s %s%s\n", name, file,
					(style && !strcmp(style, file)) ? " *" : "");
			*pos = '.';
			free(name);
			free(file);
		}
		closedir(sub);
		free(subdir);
	}
	closedir(dir);
	free(dirname);
}

void
list_styles_ICEWM()
{
	char *style = get_style_ICEWM();

	if (options.user) {
		if (wm->pdir)
			list_dir_ICEWM(wm->pdir, style);
		if (wm->udir && (!wm->pdir || strcmp(wm->pdir, wm->udir)))
			list_dir_ICEWM(wm->udir, style);
	}
	if (options.system) {
		if (wm->sdir)
			list_dir_ICEWM(wm->sdir, style);
	}
}

/** @} */

/** @name JWM
  */
/** @{ */

void
get_rcfile_JWM()
{
	return get_rcfile_simple("jwm", "-rc");
}

/** @brief Find a jwm style from a style name.
  *
  * JWM style files are named files or directories in /usr/share/jwm/styles or
  * ~/.jwm/styles.  When a named directory, the directory must contain a file
  * named style.
  */
char *
find_style_JWM()
{
	char *path = NULL;
	int len, i, j;
	struct stat st;
	char *subdirs[] = { "/styles/", "/themes/" };

	get_rcfile_JWM();
	if (options.style[0] == '/') {
		len = strlen(options.style) + strlen("/style") + 1;
		path = calloc(len, sizeof(*path));
		strcpy(path, options.style);
		if (stat(path, &st)) {
			EPRINTF("%s: %s\n", path, strerror(errno));
			free(path);
			return NULL;
		}
		if (S_ISDIR(st.st_mode)) {
			strcat(path, "/style");
			if (stat(path, &st)) {
				EPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				return NULL;
			}
		} else if (!S_ISREG(st.st_mode)) {
			EPRINTF("%s: not directory or file\n", path);
			free(path);
			return NULL;
		}
	} else {
		for (i = 0; i < CHECK_DIRS; i++) {
			if (!wm->dirs[i] || !wm->dirs[i][0])
				continue;
			for (j = 0; j < 2; j++) {
				len = strlen(wm->dirs[i]) + strlen(subdirs[j]) +
				    strlen(options.style) + strlen("/style") + 1;
				path = calloc(len, sizeof(*path));
				strcpy(path, wm->dirs[i]);
				strcat(path, subdirs[j]);
				strcat(path, options.style);
				if (stat(path, &st)) {
					DPRINTF("%s: %s\n", path, strerror(errno));
					free(path);
					path = NULL;
					continue;
				}
				if (S_ISDIR(st.st_mode)) {
					strcat(path, "/style");
					if (stat(path, &st)) {
						DPRINTF("%s: %s\n", path, strerror(errno));
						free(path);
						path = NULL;
						continue;
					}
				} else if (!S_ISREG(st.st_mode)) {
					DPRINTF("%s: not directory or file\n", path);
					free(path);
					path = NULL;
					continue;
				}
			}
			break;
		}
	}
	return path;
}

/** @brief Get the style for jwm.
  *
  * There are two ways to implement the style system for echinus: symbolic links
  * or <Include></Include> statements.  Both accept absolute or relative paths.
  * The style file in turn links to or include a style file from the appropriate
  * styles subdirectory.
  *
  * The symbolic link approach is likely best.  Either acheives the same result.
  */
char *
get_style_JWM()
{
	char *stylerc = NULL, *stylefile = NULL;
	int i, len;
	struct stat st;

	get_rcfile_JWM();
	for (i = 0; i < CHECK_DIRS; i++) {
		if (i == 1)
			continue;
		if (!wm->dirs[i] || !wm->dirs[i][0])
			continue;
		len = strlen(wm->dirs[i]) + strlen("/style") + 1;
		stylerc = calloc(len, sizeof(*stylerc));
		strcpy(stylerc, wm->dirs[i]);
		strcat(stylerc, "/style");
		if (lstat(stylerc, &st)) {
			DPRINTF("%s: %s\n", stylerc, strerror(errno));
			free(stylerc);
			continue;
		}
		if (S_ISREG(st.st_mode)) {
			FILE *f;
			char *buf, *b, *e;

			if (!(f = fopen(stylerc, "r"))) {
				DPRINTF("%s: %s\n", stylerc, strerror(errno));
				free(stylerc);
				continue;
			}
			buf = calloc(PATH_MAX + 1, sizeof(*buf));
			while (fgets(buf, PATH_MAX, f)) {
				if (!(b = strstr(buf, "<Include>")))
					continue;
				if (!(e = strstr(buf, "</Include>")))
					continue;
				b += strlen("<Include>");
				if (b >= e)
					continue;
				*e = '\0';
				memmove(buf, b, strlen(b) + 1);
				stylefile = buf;
				break;
			}
			fclose(f);
			if (!stylefile) {
				free(buf);
				continue;
			}
		} else if (S_ISLNK(st.st_mode)) {
			stylefile = calloc(PATH_MAX + 1, sizeof(*stylefile));
			if (readlink(stylerc, stylefile, PATH_MAX) <= 0) {
				DPRINTF("%s: %s\n", stylerc, strerror(errno));
				free(stylefile);
				stylefile = NULL;
				free(stylerc);
				continue;
			}
		} else {
			DPRINTF("%s: not link or file\n", stylerc);
			free(stylerc);
			continue;
		}
		if (stylefile[0] != '/') {
			/* make absolute path */
			memmove(stylefile + strlen(wm->dirs[i]) + 1,
				stylefile, strlen(stylefile) + 1);
			memcpy(stylefile, stylerc, strlen(wm->dirs[i]) + 1);
		}
		free(stylerc);
		stylerc = NULL;
		break;
	}
	if (stylefile) {
		char *pos;

		free(wm->style);
		wm->style = strdup(stylefile);
		free(wm->stylename);
		if ((pos = strstr(stylefile, "/style")))
			*pos = '\0';
		wm->stylename = (pos = strrchr(stylefile, '/')) ?
		    strdup(pos + 1) : strdup(stylefile);
		free(stylefile);
	}
	return wm->style;
}

/** @brief Reload a jwm style.
  *
  * JWM can be reloaded or restarted by sending a _JWM_RELOAD or _JWM_RESTART
  * ClientMessage to the root window, or by executing jwm -reload or jwm
  * -restart.
  */
void
reload_style_JWM()
{
	XEvent ev;

	ev.xclient.type = ClientMessage;
	ev.xclient.display = dpy;
	ev.xclient.window = root;
	ev.xclient.message_type = XInternAtom(dpy, "_JWM_RELOAD", False);
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = 0;
	ev.xclient.data.l[1] = 0;
	ev.xclient.data.l[2] = 0;
	ev.xclient.data.l[3] = 0;
	ev.xclient.data.l[4] = 0;
	XSendEvent(dpy, root, False, SubstructureNotifyMask | SubstructureRedirectMask,
		   &ev);
	XSync(dpy, False);
}

/*
 * When jwm changes its style (the way we have it set up), it writes ~/.jwm/style to
 * include a new file and restarts.  The jwm style file, ~/.jwm/style looks like:
 *
 * <?xml version="1.0"?>
 * <JWM>
 *    <Include>/usr/share/jwm/styles/Squared-blue</Include>
 * </JWM>
 *
 * The last component of the path is the theme name.  System styles are located in
 * /usr/share/jwm/styles; user styles are located in ~/.jwm/styles.
 *
 * jwm can be reloaded or restarted by sending a _JWM_RELOAD or _JWM_RESTART
 * ClientMessage to the root window, or by executing jwm -reload or jwm -restart.
 *
 * xde-session sets the environment variable JWM_CONFIG_FILE to point to the primary
 * configuration file; JWM_CONFIG_DIR to point to the system configuration directory
 * (default /usr/share/jwm); JWM_CONFIG_HOME to point to the user configuration
 * directory (default ~/.jwm but set under an xde-session to ~/.config/jwm).
 *
 * Note that older versions of L<jwm(1)> do not provide tilde expansion in
 * configuration files.
 */
void
set_style_JWM()
{
	char *stylefile, *style, *stylerc;
	int len;

	get_rcfile_JWM();
	if (!(stylefile = find_style_JWM())) {
		EPRINTF("cannot find jwm style '%s'\n", options.style);
		goto no_stylefile;
	}
	if ((style = get_style_JWM()) && !strcmp(style, stylefile))
		goto no_change;
	if (options.dryrun)
		goto no_change;
	len = strlen(wm->pdir) + strlen("/style") + 1;
	stylerc = calloc(len, sizeof(*stylerc));
	strcpy(stylerc, wm->pdir);
	strcat(stylerc, "/style");
	if (options.link) {
		unlink(stylerc);
		if (symlink(stylefile, stylerc)) {
			EPRINTF("%s -> %s: %s\n", stylerc, stylefile, strerror(errno));
			goto no_link;
		}
	} else {
		FILE *f;

		unlink(stylerc);
		if (!(f = fopen(stylerc, "w"))) {
			EPRINTF("%s: %s\n", stylerc, strerror(errno));
			goto no_file;
		}
		fprintf(f, "<?xml version=\"1.0\"?>\n");
		fprintf(f, "<JWM>\n");
		fprintf(f, "   <Include>%s</Include>\n", stylefile);
		fprintf(f, "</JWM>\n");
		fclose(f);
	}
	reload_style_JWM();
      no_file:
      no_link:
	free(stylerc);
      no_change:
	free(stylefile);
      no_stylefile:
	return;
}

void
list_dir_JWM(char *xdir, char *style)
{
	DIR *dir;
	char *dirname, *file;
	struct dirent *d;
	struct stat st;
	int len, j;
	char *subdirs[] = { "/styles", "/themes" };

	if (!xdir || !*xdir)
		return;
	for (j = 0; j < 2; j++) {
		len = strlen(xdir) + strlen(subdirs[j]) + 1;
		dirname = calloc(len, sizeof(*dirname));
		strcpy(dirname, xdir);
		strcat(dirname, subdirs[j]);
		if (!(dir = opendir(dirname))) {
			DPRINTF("%s: %s\n", dirname, strerror(errno));
			free(dirname);
			return;
		}
		while ((d = readdir(dir))) {
			if (d->d_name[0] == '.')
				continue;
			len = strlen(dirname) + strlen(d->d_name) + strlen("/style") + 2;
			file = calloc(len, sizeof(*file));
			strcpy(file, dirname);
			strcat(file, "/");
			strcat(file, d->d_name);
			if (stat(file, &st)) {
				EPRINTF("%s: %s\n", file, strerror(errno));
				free(file);
				continue;
			}
			if (S_ISREG(st.st_mode))
				goto got_it;
			else if (!S_ISDIR(st.st_mode)) {
				DPRINTF("%s: not file or directory\n", file);
				free(file);
				continue;
			}
			strcat(file, "/style");
			if (stat(file, &st)) {
				EPRINTF("%s: %s\n", file, strerror(errno));
				free(file);
				continue;
			}
			if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not a file\n", file);
				free(file);
				continue;
			}
		      got_it:
			if (!options.theme || find_xde_theme(d->d_name))
				fprintf(stdout, "%s %s%s\n", d->d_name, file,
					(style && !strcmp(style, file)) ? " *" : "");
			free(file);
		}
		closedir(dir);
		free(dirname);
	}
}

void
list_styles_JWM()
{
	char *style = get_style_JWM();

	if (options.user) {
		if (wm->pdir)
			list_dir_JWM(wm->pdir, style);
		if (wm->udir && (!wm->pdir || strcmp(wm->pdir, wm->udir)))
			list_dir_JWM(wm->udir, style);
	}
	if (options.system) {
		if (wm->sdir)
			list_dir_JWM(wm->sdir, style);
	}
}

/** @} */

/** @name PEKWM
  */
/** @{ */

void
get_rcfile_PEKWM()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("--config");

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			wm->rcfile =
			    calloc(strlen(home) + strlen(file) + 2, sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		wm->rcfile =
		    calloc(strlen(home) + strlen("/.pekwm/config") + 1,
			   sizeof(*wm->rcfile));
		strcpy(wm->rcfile, home);
		strcat(wm->rcfile, "/.pekwm/config");
	}
	get_simple_dirs("pekwm");
}

char *
find_style_PEKWM()
{
	char *path = calloc(PATH_MAX, sizeof(*path)), *res = NULL;
	int i;

	get_rcfile_PEKWM();
	for (i = 0; i < CHECK_DIRS; i++) {
		if (!wm->dirs[i] || !wm->dirs[i][0])
			continue;
		snprintf(path, PATH_MAX, "%s/themes/%s/theme", wm->dirs[i],
			 options.style);
		if (test_file(path)) {
			res = strdup(path);
			break;
		}
	}
	free(path);
	return res;
}

char *
get_style_PEKWM()
{
	get_rcfile_PEKWM();
	return NULL;
}

/*
 * When pekwm changes its style, it places the theme directory in the ~/.pekwm/config
 * file.  This normally has the form:
 *
 *   Files {
 *       Theme = "/usr/share/pekwm/themes/Airforce"
 *   }
 *
 * The last component of the path is the theme name.  The full path is to a directory
 * which contains a F<theme> file.  System styles are located in
 * /usr/share/pekwm/themes; user styles are located in ~/.pekwm/themes.
 *
 * pekwm can be restarted by sending a SIGHUP signal to the pekwm process.  pekwm
 * sets its pid in the _NET_WM_PID(CARDINAL) property on the root window (not the
 * check window) as well as the fqdn of the host in the WM_CLIENT_MACHINE(STRING)
 * property, again on the root window.  The XDE::EWMH module figures this out.
 *
 * When xde-session runs, it sets the PEKWM_RCFILE environment variable.  xde-session
 * and associated tools always launch pekwm with a command such as:
 *
 *   pekwm ${PEKWM_RCFILE:+--config $PEKWM_RCFILE}
 *
 * The default configuration file when PEKWM_RCFILE is not specified is
 * ~/.pekwm/config.  The locations of other pekwm(1) configuration files are
 * specified in the initial configuration file.  xde-session typically sets
 * PEKWM_RCFILE to $XDG_CONFIG_HOME/pekwm/config.
 */
void
set_style_PEKWM()
{
	FILE *f;
	struct stat st;
	char *stylefile, *buf, *pos, *end, *line, *p, *q;
	int len;
	size_t read, total;

	if (!wm->pid) {
		EPRINTF("%s", "cannot set pekwm style without pid\n");
		goto no_stylefile;
	}
	if (!(stylefile = find_style_PEKWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		goto no_stylefile;
	}
	if (!(f = fopen(wm->rcfile, "r"))) {
		EPRINTF("%s: %s\n", wm->rcfile, strerror(errno));
		goto no_rcfile;
	}
	if (fstat(fileno(f), &st)) {
		EPRINTF("%s: %s\n", wm->rcfile, strerror(errno));
		goto no_stat;
	}
	buf = calloc(st.st_size + 1, sizeof(*buf));
	/* read entire file into buffer */
	for (total = 0; total < st.st_size; total += read)
		if ((read = fread(buf + total, 1, st.st_size - total, f)))
			if (total < st.st_size)
				goto no_buf;
	len = strlen(stylefile) + strlen("Theme = \"\"") + 1;
	line = calloc(len, sizeof(*line));
	snprintf(line, len, "Theme = \"%s\"", stylefile);
	if (strstr(buf, line))
		goto no_change;
	if (options.dryrun)
		goto no_change;
	if (!(f = freopen(wm->rcfile, "w", f))) {
		EPRINTF("%s: %s\n", wm->rcfile, strerror(errno));
		goto no_change;
	}
	for (pos = buf, end = buf + st.st_size; pos < end; pos = pos + strlen(pos) + 1) {
		*strchrnul(pos, '\n') = '\0';
		if ((p = strstr(pos, "Theme = ")) && (!(q = strchr(pos, '#')) || p < q))
			fprintf(f, "    %s\n", line);
		else
			fprintf(f, "%s\n", pos);
	}
	kill(wm->pid, SIGHUP);
      no_change:
	free(line);
      no_buf:
	free(buf);
      no_stat:
	fclose(f);
      no_rcfile:
	free(stylefile);
      no_stylefile:
	return;
}

void
list_styles_PEKWM()
{
}

/** @} */

/** @name FVWM
  */
/** @{ */

void
get_rcfile_FVWM()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("-f");
	char *cnfg = get_proc_environ("FVWM_USERDIR");

	free(wm->udir);
	wm->udir = calloc(strlen(home) + strlen("/.fvwm") + 1, sizeof(*wm->udir));
	strcpy(wm->udir, home);
	strcat(wm->udir, "/.fvwm");
	free(wm->pdir);
	if (cnfg) {
		if (cnfg[0] == '/')
			wm->pdir = strdup(cnfg);
		else {
			wm->pdir =
			    calloc(strlen(home) + strlen(cnfg) + 2, sizeof(*wm->pdir));
			strcpy(wm->pdir, home);
			strcat(wm->pdir, "/");
			strcat(wm->pdir, cnfg);
		}
	} else
		wm->pdir = strdup(wm->udir);
	free(wm->sdir);
	wm->sdir = strdup("/usr/share/fvwm");

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			wm->rcfile =
			    calloc(strlen(wm->pdir) + strlen(file) + 2,
				   sizeof(*wm->rcfile));
			strcpy(wm->rcfile, wm->pdir);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		wm->rcfile =
		    calloc(strlen(wm->pdir) + strlen("/config") + 1, sizeof(*wm->rcfile));
		strcpy(wm->rcfile, wm->pdir);
		strcat(wm->rcfile, "/config");
	}
}

char *
find_style_FVWM()
{
	get_rcfile_FVWM();
	return NULL;
}

char *
get_style_FVWM()
{
	get_rcfile_FVWM();
	return NULL;
}

void
set_style_FVWM()
{
	char *stylefile;

	if (!(stylefile = find_style_FVWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_FVWM()
{
}

/** @} */

/** @name WMAKER
  */
/** @{ */

/** @brief Find the windowmaker rc file and default directory.
  *
  * Windowmaker observes the GNUSTEP_USER_ROOT environment variable.  When not
  * specified, it defaults to ~/GNUstep/Defaults/WindowMaker.  The locations of
  * all wmaker configuration files are under the same directory.
  */
void
get_rcfile_WMAKER()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *cnfg = get_proc_environ("GNUSTEP_USER_ROOT");

	free(wm->udir);
	wm->udir = calloc(strlen(home) + strlen("/GNUstep") + 1, sizeof(*wm->udir));
	strcpy(wm->udir, home);
	strcat(wm->udir, "/GNUstep");
	free(wm->pdir);
	if (cnfg) {
		if (cnfg[0] == '/')
			wm->pdir = strdup(cnfg);
		else {
			wm->pdir =
			    calloc(strlen(home) + strlen(cnfg) + 2, sizeof(*wm->pdir));
			strcpy(wm->pdir, home);
			strcat(wm->pdir, "/");
			strcat(wm->pdir, cnfg);
		}
	} else
		wm->pdir = strdup(wm->udir);
	free(wm->sdir);
	wm->sdir = strdup("/usr/share/WindowMaker");

	free(wm->rcfile);
	wm->rcfile =
	    calloc(strlen(wm->pdir) + strlen("/Defaults/WindowMaker") + 1,
		   sizeof(*wm->rcfile));
	strcpy(wm->rcfile, wm->pdir);
	strcat(wm->rcfile, "/Defaults/WindowMaker");
}

char *
find_style_WMAKER()
{
	char *pos, *path = calloc(PATH_MAX, sizeof(*path)), *res = NULL;
	int i, len;

	get_rcfile_WMAKER();
	for (i = 0; i < CHECK_DIRS; i++) {
		if (!wm->dirs[i] || !wm->dirs[i][0])
			continue;
		strcpy(path, wm->dirs[i]);
		if (i < 2)
			strcat(path, "/Library/WindowMaker");
		len = strlen(path);
		pos = path + len;
		snprintf(pos, PATH_MAX - len, "/Themes/%s.themed/style", options.style);
		if (test_file(path)) {
			res = strdup(path);
			break;
		}
		snprintf(pos, PATH_MAX - len, "/Themes/%s.style", options.style);
		if (test_file(path)) {
			res = strdup(path);
			break;
		}
		snprintf(pos, PATH_MAX - len, "/Styles/%s.style", options.style);
		if (test_file(path)) {
			res = strdup(path);
			break;
		}
	}
	free(path);
	return res;
}

char *
get_style_WMAKER()
{
	get_rcfile_WMAKER();
	return NULL;
}

void
set_style_WMAKER()
{
	char *stylefile;

	if (!(stylefile = find_style_WMAKER())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_WMAKER()
{
}

/** @} */

/** @name AFTERSTEP
  */
/** @{ */

void
get_rcfile_AFTERSTEP()
{
}

char *
find_style_AFTERSTEP()
{
	get_rcfile_AFTERSTEP();
	return NULL;
}

char *
get_style_AFTERSTEP()
{
	get_rcfile_AFTERSTEP();
	return NULL;
}

void
set_style_AFTERSTEP()
{
	char *stylefile;

	if (!(stylefile = find_style_AFTERSTEP())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_AFTERSTEP()
{
}

/** @} */

/** @name METACITY
  */
/** @{ */

void
get_rcfile_METACITY()
{
}

char *
find_style_METACITY()
{
	get_rcfile_METACITY();
	return NULL;
}

char *
get_style_METACITY()
{
	get_rcfile_METACITY();
	return NULL;
}

void
list_styles_METACITY()
{
}

void
set_style_METACITY()
{
	char *stylefile;

	if (!(stylefile = find_style_METACITY())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

/** @} */

/* @name XTWM
 *
 * There is really no window manager named xtwm, but twm, ctwm, vtwm and etwm
 * are all similar so this section contains factored routines.
 */
/** @{ */

void
get_rcfile_XTWM(char *xtwm)
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("-f");
	int len;
	struct stat st;

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			len = strlen(home) + strlen(file) + 2;
			wm->rcfile = calloc(len, sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		char buf[16], rc[16];

		/* check if ~/.%src.%d exists first where %d is the screen number */
		snprintf(rc, sizeof(rc), "/.%src", xtwm);
		snprintf(buf, sizeof(buf), ".%d", screen);
		len = strlen(home) + strlen(rc) + strlen(buf) + 1;
		wm->rcfile = calloc(len, sizeof(*wm->rcfile));
		strcpy(wm->rcfile, home);
		strcat(wm->rcfile, rc);
		strcat(wm->rcfile, buf);
		if (stat(wm->rcfile, &st) || !S_ISREG(st.st_mode))
			*strrchr(wm->rcfile, '.') = '\0';
		if (stat(wm->rcfile, &st) || !S_ISREG(st.st_mode)) {
			/* then check ~/.twmrc.%d and ~/.twmrc */
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/.twmrc");
			strcat(wm->rcfile, buf);
			if (stat(wm->rcfile, &st) || !S_ISREG(st.st_mode))
				*strrchr(wm->rcfile, '.') = '\0';
		}
		/* often this is symlinked into the actual directory */
		if (!lstat(wm->rcfile, &st) && S_ISLNK(st.st_mode)) {
			file = calloc(PATH_MAX + 1, sizeof(*file));
			if (readlink(wm->rcfile, file, PATH_MAX) == -1)
				EPRINTF("%s: %s\n", wm->rcfile, strerror(errno));
			if (file[0] == '/') {
				free(wm->rcfile);
				wm->rcfile = strdup(file);
			} else if (file[0]) {
				free(wm->rcfile);
				len = strlen(home) + strlen(file) + 2;
				wm->rcfile = calloc(len, sizeof(*wm->rcfile));
				strcpy(wm->rcfile, home);
				strcat(wm->rcfile, "/");
				strcat(wm->rcfile, file);
			}
			free(file);
		}
	}
	get_simple_dirs(xtwm);
}

char *
find_style_XTWM(char *xtwm)
{
	char *path = NULL;
	int len, i;
	struct stat st;

	get_rcfile_XTWM(xtwm);
	if (options.style[0] == '/') {
		len = strlen(options.style) + strlen("/stylerc") + 1;
		path = calloc(len, sizeof(*path));
		strcpy(path, options.style);
		if (stat(path, &st)) {
			EPRINTF("%s: %s\n", path, strerror(errno));
			free(path);
			return NULL;
		}
		if (S_ISDIR(st.st_mode)) {
			strcat(path, "/stylerc");
			if (stat(path, &st)) {
				EPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				return NULL;
			}
			if (!S_ISREG(st.st_mode)) {
				EPRINTF("%s is not a file\n", path);
				free(path);
				return NULL;
			}
		} else if (!S_ISREG(st.st_mode)) {
			EPRINTF("%s: not a file or directory\n", path);
			free(path);
			return NULL;
		}
	} else {
		for (i = 0; i < CHECK_DIRS; i++) {
			if (!wm->dirs[i] || !wm->dirs[i][0])
				continue;
			len = strlen(wm->dirs[i]) + strlen("/styles/")
			    + strlen(options.style) + strlen("/stylerc") + 1;
			path = calloc(len, sizeof(*path));
			strcpy(path, wm->dirs[i]);
			strcat(path, "/styles/");
			strcat(path, options.style);
			if (stat(path, &st)) {
				DPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				path = NULL;
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				strcat(path, "/stylerc");
				if (stat(path, &st)) {
					DPRINTF("%s: %s\n", path, strerror(errno));
					free(path);
					path = NULL;
					continue;
				}
				if (!S_ISREG(st.st_mode)) {
					DPRINTF("%s is not a file\n", path);
					free(path);
					path = NULL;
					continue;
				}
			} else if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s not a file or directory\n", path);
				free(path);
				path = NULL;
				continue;
			}
			break;
		}
	}
	return path;
}

/** @brief Get the style for a twm variant.
  *
  * The style is a symbolic link in the private configuration directory named
  * stylerc.
  */
char *
get_style_XTWM(char *xtwm)
{
	char *stylerc = NULL, *stylefile = NULL;
	int i, len;
	struct stat st;

	if (wm->style)
		return wm->style;

	get_rcfile_XTWM(xtwm);
	for (i = 0; i < CHECK_DIRS; i++) {
		if (i == 1)
			continue;
		if (!wm->dirs[i] || !wm->dirs[i][0])
			continue;
		len = strlen(wm->dirs[i]) + strlen("/stylerc") + 1;
		stylerc = calloc(len, sizeof(*stylerc));
		strcpy(stylerc, wm->dirs[i]);
		strcat(stylerc, "/stylerc");
		if (lstat(stylerc, &st)) {
			DPRINTF("%s: %s\n", stylerc, strerror(errno));
			free(stylerc);
			stylerc = NULL;
			continue;
		}
		if (!S_ISLNK(st.st_mode)) {
			DPRINTF("%s not a symlink\n", stylerc);
			free(stylerc);
			stylerc = NULL;
			continue;
		}
		stylefile = calloc(PATH_MAX + 1, sizeof(*stylefile));
		if (readlink(stylerc, stylefile, PATH_MAX) <= 0) {
			DPRINTF("%s: %s\n", stylerc, strerror(errno));
			free(stylefile);
			stylefile = NULL;
			free(stylerc);
			stylerc = NULL;
			continue;
		}
		if (stylefile[0] != '/') {
			/* make absolute path */
			memmove(stylefile + strlen(wm->dirs[i]) + 1,
				stylefile, strlen(stylefile) + 1);
			memcpy(stylefile, stylerc, strlen(wm->dirs[i]) + 1);
		}
		free(stylerc);
		stylerc = NULL;
		break;
	}
	if (stylefile) {
		char *pos;

		free(wm->style);
		wm->style = strdup(stylefile);
		free(wm->stylename);
		if ((pos = strstr(stylefile, "/stylerc")))
			*pos = '\0';
		wm->stylename = (pos = strrchr(stylefile, '/')) ?
		    strdup(pos + 1) : strdup(stylefile);
		free(stylefile);
	}
	return wm->style;
}

/** @brief Reload an twm variant style.
  *
  * ctwm and etwm can be restarted by sending SIGHUP to the process.  vtwm will
  * restart on a SIGUSR1.  twm has no restart mechanism and we will have to
  * eventually try to get the session manager to restart it.
  */
void
reload_style_XTWM(char *xtwm)
{
	if (!strcmp(xtwm, "ctwm") || !strcmp(xtwm, "etwm"))
		kill(wm->pid, SIGHUP);
	else if (!strcmp(xtwm, "vtwm"))
		kill(wm->pid, SIGUSR1);
	else 
		EPRINTF("don't know how to restart %s\n", xtwm);
}

/** @brief Set the style for a twm variant.
  *
  * Ourt style system for twm variants places a symbolic link in the user or
  * private directory called stylerc that links to the style file in the style
  * directory.
  */
void
set_style_XTWM(char *xtwm)
{
	char *stylefile, *style, *stylerc;
	int len;

	get_rcfile_XTWM(xtwm);
	if (!wm->pid) {
		EPRINTF("cannot set %s style without a pid\n", xtwm);
		goto no_pid;
	}
	if (!test_file(wm->rcfile)) {
		EPRINTF("%s rcfile '%s' does not exist\n", xtwm, wm->rcfile);
		goto no_rcfile;
	}
	if (!(stylefile = find_style_XTWM(xtwm))) {
		EPRINTF("cannot find %s style '%s'\n", xtwm, options.style);
		goto no_stylefile;
	}
	if ((style = get_style_XTWM(xtwm)) && !strcmp(style, stylefile))
		goto no_change;
	if (options.dryrun)
		goto no_change;
	len = strlen(wm->pdir) + strlen("/stylerc") + 1;
	stylerc = calloc(len, sizeof(*stylerc));
	strcpy(stylerc, wm->pdir);
	strcat(stylerc, "/stylerc");
	unlink(stylerc);
	if (symlink(stylefile, stylerc)) {
		EPRINTF("%s -> %s: %s\n", stylerc, stylefile, strerror(errno));
		goto no_link;
	}
	reload_style_XTWM(xtwm);
      no_link:
	free(stylerc);
      no_change:
	free(stylefile);
      no_stylefile:
      no_rcfile:
      no_pid:
	return;
}

void
list_dir_XTWM(char *xdir, char *style)
{
	DIR *dir;
	char *dirname, *file;
	struct dirent *d;
	struct stat st;
	int len;

	if (!xdir || !*xdir)
		return;
	len = strlen(xdir) + strlen("/styles") + 1;
	dirname = calloc(len, sizeof(*dirname));
	strcpy(dirname, xdir);
	strcat(dirname, "/styles");
	if (!(dir = opendir(dirname))) {
		EPRINTF("%s: %s\n", dirname, strerror(errno));
		free(dirname);
		return;
	}
	while ((d = readdir(dir))) {
		if (d->d_name[0] == '.')
			continue;
		len = strlen(dirname) + strlen(d->d_name) + strlen("/stylerc") + 2;
		file = calloc(len, sizeof(*file));
		strcpy(file, dirname);
		strcat(file, "/");
		strcat(file, d->d_name);
		if (stat(file, &st)) {
			EPRINTF("%s: %s\n", file, strerror(errno));
			free(file);
			continue;
		}
		if (S_ISREG(st.st_mode))
			goto got_it;
		else if (!S_ISDIR(st.st_mode)) {
			DPRINTF("%s: not file or directory\n", file);
			free(file);
			continue;
		}
		strcat(file, "/stylerc");
		if (stat(file, &st)) {
			DPRINTF("%s: %s\n", file, strerror(errno));
			free(file);
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			DPRINTF("%s: not a file\n", file);
			free(file);
			continue;
		}
	      got_it:
		if (!options.theme || find_xde_theme(d->d_name))
			fprintf(stdout, "%s %s%s\n", d->d_name, file,
				(style && !strcmp(style, file)) ? " *" : "");
		free(file);
	}
	closedir(dir);
	free(dirname);
}

void
list_styles_XTWM(char *xtwm)
{
	char *style = get_style_XTWM(xtwm);

	if (options.user) {
		if (wm->pdir)
			list_dir_XTWM(wm->pdir, style);
		if (wm->udir && (!wm->pdir || strcmp(wm->pdir, wm->udir)))
			list_dir_XTWM(wm->udir, style);
	}
	if (options.system) {
		if (wm->sdir)
			list_dir_XTWM(wm->sdir, style);
	}
}



/** @} */

/* @name TWM
 */
/** @{ */

void
get_rcfile_TWM()
{
	return get_rcfile_XTWM("twm");
}

char *
find_style_TWM()
{
	return find_style_XTWM("twm");
}

char *
get_style_TWM()
{
	return get_style_XTWM("twm");
}

void
set_style_TWM()
{
	return set_style_XTWM("twm");
}

void
list_styles_TWM()
{
	return list_styles_XTWM("twm");
}

/** @} */

/** @name CTWM
  */
/** @{ */

void
get_rcfile_CTWM()
{
	return get_rcfile_XTWM("ctwm");
}

char *
find_style_CTWM()
{
	return find_style_XTWM("ctwm");
}

char *
get_style_CTWM()
{
	return get_style_XTWM("ctwm");
}

void
set_style_CTWM()
{
	return set_style_XTWM("ctwm");
}

void
list_styles_CTWM()
{
	return list_styles_XTWM("ctwm");
}

/** @} */

/** @name VTWM
  */
/** @{ */

void
get_rcfile_VTWM()
{
	return get_rcfile_XTWM("vtwm");
}

char *
find_style_VTWM()
{
	return find_style_XTWM("vtwm");
}

char *
get_style_VTWM()
{
	return get_style_XTWM("vtwm");
}

void
set_style_VTWM()
{
	return set_style_XTWM("vtwm");
}

void
list_styles_VTWM()
{
	return list_styles_XTWM("vtwm");
}

/** @} */

/** @name ETWM
  */
/** @{ */

void
get_rcfile_ETWM()
{
	return get_rcfile_XTWM("etwm");
}

char *
find_style_ETWM()
{
	return find_style_XTWM("etwm");
}

char *
get_style_ETWM()
{
	return get_style_XTWM("etwm");
}

void
set_style_ETWM()
{
	return set_style_XTWM("etwm");
}

void
list_styles_ETWM()
{
	return list_styles_XTWM("etwm");
}

/** @} */

/** @name ECHINUS
  */
/** @{ */

void
get_rcfile_ECHINUS()
{
	char *home = get_proc_environ("HOME") ? : ".";
	char *file = get_optarg("-f");
	int len;

	free(wm->rcfile);
	if (file) {
		if (file[0] == '/')
			wm->rcfile = strdup(file);
		else {
			len = strlen(home) + strlen(file) + 2;
			wm->rcfile = calloc(len, sizeof(*wm->rcfile));
			strcpy(wm->rcfile, home);
			strcat(wm->rcfile, "/");
			strcat(wm->rcfile, file);
		}
	} else {
		len = strlen(home) + strlen("/.echinus/echinusrc") + 1;
		wm->rcfile = calloc(len, sizeof(*wm->rcfile));
		strcpy(wm->rcfile, home);
		strcat(wm->rcfile, "/.echinus/echinusrc");
	}
	get_simple_dirs("echinus");
}

/** @brief Find an echinus style from a style name.
  *
  * Echinus style files are named files or directories in
  * /usr/share/echinus/styles or ~/.echinus/styles.  When a named directory, the
  * directory must contain a file named stylerc.
  */
char *
find_style_ECHINUS()
{
	char *path = NULL;
	int len, i;
	struct stat st;

	get_rcfile_ECHINUS();
	if (options.style[0] == '/') {
		len = strlen(options.style) + strlen("/stylerc") + 1;
		path = calloc(len, sizeof(*path));
		strcpy(path, options.style);
		if (stat(path, &st)) {
			EPRINTF("%s: %s\n", path, strerror(errno));
			free(path);
			return NULL;
		}
		if (S_ISDIR(st.st_mode)) {
			strcat(path, "/stylerc");
			if (stat(path, &st)) {
				EPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				return NULL;
			}
		} else if (!S_ISREG(st.st_mode)) {
			EPRINTF("%s: not directory or file\n", path);
			free(path);
			return NULL;
		}
	} else {
		for (i = 0; i < CHECK_DIRS; i++) {
			if (!wm->dirs[i] || !wm->dirs[i][0])
				continue;
			len = strlen(wm->dirs[i]) + strlen("/styles/") +
			    strlen(options.style) + strlen("/stylerc") + 1;
			path = calloc(len, sizeof(*path));
			strcpy(path, wm->dirs[i]);
			strcat(path, "/styles/");
			strcat(path, options.style);
			if (stat(path, &st)) {
				DPRINTF("%s: %s\n", path, strerror(errno));
				free(path);
				path = NULL;
				continue;
			}
			if (S_ISDIR(st.st_mode)) {
				strcat(path, "/stylerc");
				if (stat(path, &st)) {
					DPRINTF("%s: %s\n", path, strerror(errno));
					free(path);
					path = NULL;
					continue;
				}
			} else if (!S_ISREG(st.st_mode)) {
				DPRINTF("%s: not directory or file\n", path);
				free(path);
				path = NULL;
				continue;
			}
			break;
		}
	}
	return path;
}

/** @brief Get the style for echinus.
  *
  * There are two ways to implement the style system for echinus: symbolic links
  * or #include statements.  Both accept absolute and relative paths.  The
  * stylerc file in turn links to or includes a stylerc file from the
  * appropriate styles subdirectory.
  *
  * The symbolic link approach is likely best.  Either acheives the same result.
  */
char *
get_style_ECHINUS()
{
	char *stylerc = NULL, *stylefile = NULL;
	int i, len;
	struct stat st;

	get_rcfile_ECHINUS();
	for (i = 0; i < CHECK_DIRS; i++) {
		if (i == 1)
			continue;
		if (!wm->dirs[i] || !wm->dirs[i][0])
			continue;
		len = strlen(wm->dirs[i]) + strlen("/stylerc") + 1;
		stylerc = calloc(len, sizeof(*stylerc));
		strcpy(stylerc, wm->dirs[i]);
		strcat(stylerc, "/stylerc");
		if (lstat(stylerc, &st)) {
			DPRINTF("%s: %s\n", stylerc, strerror(errno));
			free(stylerc);
			continue;
		}
		if (S_ISREG(st.st_mode)) {
			FILE *f;
			char *buf, *b, *e;

			if (!(f = fopen(stylerc, "r"))) {
				DPRINTF("%s: %s\n", stylerc, strerror(errno));
				free(stylerc);
				continue;
			}
			buf = calloc(PATH_MAX + 1, sizeof(*buf));
			while (fgets(buf, PATH_MAX, f)) {
				if (strncmp(buf, "#include", 8))
					continue;
				if (!(b = strchr(buf, '"')))
					continue;
				if (!(e = strrchr(buf, '"')))
					continue;
				b++;
				if (b >= e)
					continue;
				*e = '\0';
				memmove(buf, b, strlen(b) + 1);
				stylefile = buf;
				break;
			}
			fclose(f);
			if (!stylefile) {
				free(buf);
				continue;
			}
		} else if (S_ISLNK(st.st_mode)) {
			stylefile = calloc(PATH_MAX + 1, sizeof(*stylefile));
			if (readlink(stylerc, stylefile, PATH_MAX) <= 0) {
				DPRINTF("%s: %s\n", stylerc, strerror(errno));
				free(stylefile);
				stylefile = NULL;
				free(stylerc);
				continue;
			}
		} else {
			DPRINTF("%s: not a link or file\n", stylerc);
			free(stylerc);
			continue;
		}
		if (stylefile[0] != '/') {
			/* make absolute path */
			memmove(stylefile + strlen(wm->dirs[i]) + 1,
				stylefile, strlen(stylefile) + 1);
			memcpy(stylefile, stylerc, strlen(wm->dirs[i]) + 1);
		}
		free(stylerc);
		stylerc = NULL;
		break;
	}
	if (stylefile) {
		char *pos;

		free(wm->style);
		wm->style = strdup(stylefile);
		free(wm->stylename);
		if ((pos = strstr(stylefile, "/stylerc")))
			*pos = '\0';
		wm->stylename = (pos = strrchr(stylefile, '/')) ?
		    strdup(pos + 1) : strdup(stylefile);
		free(stylefile);
	}
	return wm->style;
}

void
reload_style_ECHINUS()
{
	if (wm->pid)
		kill(wm->pid, SIGHUP);
	else
		EPRINTF("%s", "cannot restart echinus without a pid\n");
}

/** @brief Set the style for echinus.
  *
  * Our style system for echinus places an '#include' statement in the rc file
  * that includes the stylerc file in the same directory as the rc file.  The
  * stylerc file in turn includes a stylerc file from the appropriate styles
  * subdirectory.  Alternately, we can make the stylerc file a symbolic link to
  * the style directory stylerc file.
  *
  * Sending a SIGHUP will get echinus to restart.
  */
void
set_style_ECHINUS()
{
	char *stylefile, *style, *stylerc;
	int len;

	get_rcfile_ECHINUS();
	if (!wm->pid) {
		EPRINTF("%s", "cannot set echinus style without a pid\n");
		goto no_pid;
	}
	if (!(stylefile = find_style_ECHINUS())) {
		EPRINTF("cannot find echinus style '%s'\n", options.style);
		goto no_stylefile;
	}
	if ((style = get_style_ECHINUS()) && !strcmp(style, stylefile))
		goto no_change;
	if (options.dryrun)
		goto no_change;
	len = strlen(wm->pdir) + strlen("/stylerc") + 1;
	stylerc = calloc(len, sizeof(*stylerc));
	strcpy(stylerc, wm->pdir);
	strcat(stylerc, "/style");
	if (options.link) {
		unlink(stylerc);
		if (symlink(stylefile, stylerc)) {
			EPRINTF("%s -> %s: %s\n", stylerc, stylefile, strerror(errno));
			goto no_link;
		}
	} else {
		FILE *f;

		unlink(stylerc);
		if (!(f = fopen(stylerc, "w"))) {
			EPRINTF("%s: %s\n", stylerc, strerror(errno));
			goto no_file;
		}
		fprintf(f, "#include \"%s\"\n", stylefile);
		fclose(f);
	}
	reload_style_ECHINUS();
      no_file:
      no_link:
	free(stylerc);
      no_change:
	free(stylefile);
      no_stylefile:
      no_pid:
	return;
}

void
list_dir_ECHINUS(char *xdir, char *style)
{
	DIR *dir;
	char *dirname, *file;
	struct dirent *d;
	struct stat st;
	int len;

	if (!xdir || !*xdir)
		return;
	len = strlen(xdir) + strlen("/styles") + 1;
	dirname = calloc(len, sizeof(*dirname));
	strcpy(dirname, xdir);
	strcat(dirname, "/styles");
	if (!(dir = opendir(dirname))) {
		EPRINTF("%s: %s\n", dirname, strerror(errno));
		free(dirname);
		return;
	}
	while ((d = readdir(dir))) {
		if (d->d_name[0] == '.')
			continue;
		len = strlen(dirname) + strlen(d->d_name) + strlen("/stylerc") + 2;
		file = calloc(len, sizeof(*file));
		strcpy(file, dirname);
		strcat(file, "/");
		strcat(file, d->d_name);
		if (stat(file, &st)) {
			EPRINTF("%s: %s\n", file, strerror(errno));
			free(file);
			continue;
		}
		if (S_ISREG(st.st_mode))
			goto got_it;
		else if (!S_ISDIR(st.st_mode)) {
			DPRINTF("%s: not file or directory\n", file);
			free(file);
			continue;
		}
		strcat(file, "/stylerc");
		if (stat(file, &st)) {
			DPRINTF("%s: %s\n", file, strerror(errno));
			free(file);
			continue;
		}
		if (!S_ISREG(st.st_mode)) {
			DPRINTF("%s: not a file\n", file);
			free(file);
			continue;
		}
	      got_it:
		if (!options.theme || find_xde_theme(d->d_name))
			fprintf(stdout, "%s %s%s\n", d->d_name, file,
				(style && !strcmp(style, file)) ? " *" : "");
		free(file);
	}
	closedir(dir);
	free(dirname);
}

void
list_styles_ECHINUS()
{
	char *style = get_style_ECHINUS();

	if (options.user) {
		if (wm->pdir)
			list_dir_ECHINUS(wm->pdir, style);
		if (wm->udir && (!wm->pdir || strcmp(wm->pdir, wm->udir)))
			list_dir_ECHINUS(wm->udir, style);
	}
	if (options.system) {
		if (wm->sdir)
			list_dir_ECHINUS(wm->sdir, style);
	}
}

/** @} */

/** @name UWM
  */
/** @{ */

void
get_rcfile_UWM()
{
}

char *
find_style_UWM()
{
	get_rcfile_UWM();
	return NULL;
}

char *
get_style_UWM()
{
	get_rcfile_UWM();
	return NULL;
}

void
set_style_UWM()
{
	char *stylefile;

	if (!(stylefile = find_style_UWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_UWM()
{
}

/** @} */

/** @name AWESOME
  */
/** @{ */

void
get_rcfile_AWESOME()
{
}

char *
find_style_AWESOME()
{
	get_rcfile_AWESOME();
	return NULL;
}

char *
get_style_AWESOME()
{
	get_rcfile_AWESOME();
	return NULL;
}

void
set_style_AWESOME()
{
	char *stylefile;

	if (!(stylefile = find_style_AWESOME())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_AWESOME()
{
}

/** @} */

/** @name MATWM
  */
/** @{ */

void
get_rcfile_MATWM()
{
}

char *
find_style_MATWM()
{
	get_rcfile_MATWM();
	return NULL;
}

char *
get_style_MATWM()
{
	get_rcfile_MATWM();
	return NULL;
}

void
set_style_MATWM()
{
	char *stylefile;

	if (!(stylefile = find_style_MATWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_MATWM()
{
}

/** @} */

/** @name WAIMEA
  */
/** @{ */

void
get_rcfile_WAIMEA()
{
}

char *
find_style_WAIMEA()
{
	get_rcfile_WAIMEA();
	return NULL;
}

char *
get_style_WAIMEA()
{
	get_rcfile_WAIMEA();
	return NULL;
}

void
set_style_WAIMEA()
{
	char *stylefile;

	if (!(stylefile = find_style_WAIMEA())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_WAIMEA()
{
}

/** @} */

/** @name WIND
  */
/** @{ */

void
get_rcfile_WIND()
{
}

char *
find_style_WIND()
{
	get_rcfile_WIND();
	return NULL;
}

char *
get_style_WIND()
{
	get_rcfile_WIND();
	return NULL;
}

void
set_style_WIND()
{
	char *stylefile;

	if (!(stylefile = find_style_WIND())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_WIND()
{
}

/** @} */

/** @name 2BWM
  */
/** @{ */

void
get_rcfile_2BWM()
{
}

char *
find_style_2BWM()
{
	get_rcfile_2BWM();
	return NULL;
}

char *
get_style_2BWM()
{
	get_rcfile_2BWM();
	return NULL;
}

void
set_style_2BWM()
{
	char *stylefile;

	if (!(stylefile = find_style_2BWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_2BWM()
{
}

/** @} */

/** @name WMX
  */
/** @{ */

void
get_rcfile_WMX()
{
}

char *
find_style_WMX()
{
	get_rcfile_WMX();
	return NULL;
}

char *
get_style_WMX()
{
	get_rcfile_WMX();
	return NULL;
}

void
set_style_WMX()
{
	char *stylefile;

	if (!(stylefile = find_style_WMX())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_WMX()
{
}

/** @} */

/** @name
  */
/** @{ */

void
get_rcfile_FLWM()
{
}

char *
find_style_FLWM()
{
	get_rcfile_FLWM();
	return NULL;
}

char *
get_style_FLWM()
{
	get_rcfile_FLWM();
	return NULL;
}

void
set_style_FLWM()
{
	char *stylefile;

	if (!(stylefile = find_style_FLWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_FLWM()
{
}

/** @} */

/** @name MWM
  */
/** @{ */

void
get_rcfile_MWM()
{
}

char *
find_style_MWM()
{
	get_rcfile_MWM();
	return NULL;
}

char *
get_style_MWM()
{
	get_rcfile_MWM();
	return NULL;
}

void
set_style_MWM()
{
	char *stylefile;

	if (!(stylefile = find_style_MWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_MWM()
{
}

/** @} */

/** @name DTWM
  */
/** @{ */

void
get_rcfile_DTWM()
{
}

char *
find_style_DTWM()
{
	get_rcfile_DTWM();
	return NULL;
}

char *
get_style_DTWM()
{
	get_rcfile_DTWM();
	return NULL;
}

void
set_style_DTWM()
{
	char *stylefile;

	if (!(stylefile = find_style_DTWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_DTWM()
{
}

/** @} */

/** @name SPECTRWM
  */
/** @{ */

void
get_rcfile_SPECTRWM()
{
}

char *
find_style_SPECTRWM()
{
	get_rcfile_SPECTRWM();
	return NULL;
}

char *
get_style_SPECTRWM()
{
	get_rcfile_SPECTRWM();
	return NULL;
}

void
set_style_SPECTRWM()
{
	char *stylefile;

	if (!(stylefile = find_style_SPECTRWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_SPECTRWM()
{
}

/** @} */

/** @name YEAHWM
  */
/** @{ */

void
get_rcfile_YEAHWM()
{
}

char *
find_style_YEAHWM()
{
	get_rcfile_YEAHWM();
	return NULL;
}

char *
get_style_YEAHWM()
{
	get_rcfile_YEAHWM();
	return NULL;
}

void
set_style_YEAHWM()
{
	char *stylefile;

	if (!(stylefile = find_style_YEAHWM())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_YEAHWM()
{
}

/** @} */

/** @name NONE
  */
/** @{ */

void
get_rcfile_NONE()
{
}

char *
find_style_NONE()
{
	get_rcfile_NONE();
	return NULL;
}

char *
get_style_NONE()
{
	get_rcfile_NONE();
	return NULL;
}

void
set_style_NONE()
{
	char *stylefile;

	if (!(stylefile = find_style_NONE())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_NONE()
{
}

/** @} */

/** @name UNKNOWN
  */
/** @{ */

void
get_rcfile_UNKNOWN()
{
}

char *
find_style_UNKNOWN()
{
	get_rcfile_UNKNOWN();
	return NULL;
}

char *
get_style_UNKNOWN()
{
	get_rcfile_UNKNOWN();
	return NULL;
}

void
set_style_UNKNOWN()
{
	char *stylefile;

	if (!(stylefile = find_style_UNKNOWN())) {
		EPRINTF("cannot find style '%s'\n", options.style);
		return;
	}
}

void
list_styles_UNKNOWN()
{
}

/** @} */

typedef struct {
	char *name;
	char *(*get_style) (void);
	void (*list_styles) (void);
	void (*set_style) (void);
} WmOperations;

WmOperations wm_ops[] = {
	/* *INDENT-OFF* */
	{ "fluxbox",		&get_style_FLUXBOX,	&list_styles_FLUXBOX,	&set_style_FLUXBOX	},
	{ "blackbox",		&get_style_BLACKBOX,	&list_styles_BLACKBOX,	&set_style_BLACKBOX	},
	{ "openbox",		&get_style_OPENBOX,	&list_styles_OPENBOX,	&set_style_OPENBOX	},
	{ "icewm",		&get_style_ICEWM,	&list_styles_ICEWM,	&set_style_ICEWM	},
	{ "jwm",		&get_style_JWM,		&list_styles_JWM,	&set_style_JWM		},
	{ "pekwm",		&get_style_PEKWM,	&list_styles_PEKWM,	&set_style_PEKWM	},
	{ "fvwm",		&get_style_FVWM,	&list_styles_FVWM,	&set_style_FVWM		},
	{ "wmaker",		&get_style_WMAKER,	&list_styles_WMAKER,	&set_style_WMAKER	},
	{ "afterstep",		&get_style_AFTERSTEP,	&list_styles_AFTERSTEP,	&set_style_AFTERSTEP	},
	{ "metacity",		&get_style_METACITY,	&list_styles_METACITY,	&set_style_METACITY	},
	{ "twm",		&get_style_TWM,		&list_styles_TWM,	&set_style_TWM		},
	{ "ctwm",		&get_style_CTWM,	&list_styles_CTWM,	&set_style_CTWM		},
	{ "vtwm",		&get_style_VTWM,	&list_styles_VTWM,	&set_style_VTWM		},
	{ "etwm",		&get_style_ETWM,	&list_styles_ETWM,	&set_style_ETWM		},
	{ "echinus",		&get_style_ECHINUS,	&list_styles_ECHINUS,	&set_style_ECHINUS	},
	{ "uwm",		&get_style_UWM,		&list_styles_UWM,	&set_style_UWM		},
	{ "awesome",		&get_style_AWESOME,	&list_styles_AWESOME,	&set_style_AWESOME	},
	{ "matwm",		&get_style_MATWM,	&list_styles_MATWM,	&set_style_MATWM	},
	{ "waimea",		&get_style_WAIMEA,	&list_styles_WAIMEA,	&set_style_WAIMEA	},
	{ "wind",		&get_style_WIND,	&list_styles_WIND,	&set_style_WIND		},
	{ "2bwm",		&get_style_2BWM,	&list_styles_2BWM,	&set_style_2BWM		},
	{ "wmx",		&get_style_WMX,		&list_styles_WMX,	&set_style_WMX		},
	{ "flwm",		&get_style_FLWM,	&list_styles_FLWM,	&set_style_FLWM		},
	{ "mwm",		&get_style_MWM,		&list_styles_MWM,	&set_style_MWM		},
	{ "dtwm",		&get_style_DTWM,	&list_styles_DTWM,	&set_style_DTWM		},
	{ "spectrwm",		&get_style_SPECTRWM,	&list_styles_SPECTRWM,	&set_style_SPECTRWM	},
	{ "yeahwm",		&get_style_YEAHWM,	&list_styles_YEAHWM,	&set_style_YEAHWM	},
	{ "none",		&get_style_NONE,	&list_styles_NONE,	&set_style_NONE		},
	{ "unknown",		&get_style_UNKNOWN,	&list_styles_UNKNOWN,	&set_style_UNKNOWN	},
	{ NULL,			NULL,			NULL,			NULL			}
	/* *INDENT-ON* */
};

WmOperations *
get_wm_ops()
{
	WmOperations *ops;

	if (!wm->name)
		return NULL;
	for (ops = wm_ops; ops->name; ops++)
		if (!strcmp(ops->name, wm->name))
			break;
	return ops->name ? ops : NULL;
}

/** @brief Get the current style of the window manager.
  */
void
current_style()
{
	WmOperations *ops;

	OPRINTF("%s\n", "getting current style");

	if (options.screen == -1) {
		for (screen = 0; screen < nscr; screen++) {
			OPRINTF("getting current style for screen %d\n", screen);
			scr = screens + screen;
			root = scr->root;
			if (!(wm = scr->wm) || !(ops = get_wm_ops()) || !ops->get_style) {
				EPRINTF("cannot get current style for screen %d\n",
					screen);
				continue;
			}
			ops->get_style();
			if (wm->style) {
				if (!options.theme || find_xde_theme(wm->stylename))
					fprintf(stdout, "%s %s\n", wm->stylename,
						wm->style);
			} else
				EPRINTF("cannot get current style for screen %d\n",
					screen);
		}
	} else if (0 <= options.screen && options.screen < nscr) {
		screen = options.screen;
		OPRINTF("getting current style for screen %d\n", screen);
		scr = screens + screen;
		root = scr->root;
		if (!(wm = scr->wm) || !(ops = get_wm_ops()) || !ops->get_style) {
			EPRINTF("cannot get current style for screen %d\n", screen);
			return;
		}
		ops->get_style();
		if (wm->style) {
			if (!options.theme || find_xde_theme(wm->stylename))
				fprintf(stdout, "%s %s\n", wm->stylename, wm->style);
		} else
			EPRINTF("cannot get current style for screen %d\n", screen);
	} else {
		EPRINTF("invalid screen number %d\n", options.screen);
		exit(2);
	}
}

/** @brief List the style of the window manager.
  */
void
list_styles()
{
	WmOperations *ops;

	OPRINTF("%s\n", "listing styles");

	if (options.screen == -1) {
		for (screen = 0; screen < nscr; screen++) {
			scr = screens + screen;
			root = scr->root;
			if (!(wm = scr->wm) || !(ops = get_wm_ops()) || !ops->list_styles)
				continue;
			ops->list_styles();
		}
	} else if (0 <= options.screen && options.screen < nscr) {
		screen = options.screen;
		scr = screens + screen;
		root = scr->root;
		if (!(wm = scr->wm) || !(ops = get_wm_ops()) || !ops->list_styles) {
			EPRINTF("cannot list styles for screen %d\n", screen);
			return;
		}
		ops->list_styles();
	} else {
		EPRINTF("invalid screen number %d\n", options.screen);
		exit(2);
	}
}

/** @brief Set the style of the window manager.
  */
void
set_style()
{
	WmOperations *ops;

	OPRINTF("%s\n", "setting style");

	if (options.screen == -1) {
		for (screen = 0; screen < nscr; screen++) {
			scr = screens + screen;
			root = scr->root;
			if (!(wm = scr->wm) || !(ops = get_wm_ops()) || !ops->set_style)
				continue;
			ops->set_style();
		}
	} else if (0 <= options.screen && options.screen < nscr) {
		screen = options.screen;
		scr = screens + screen;
		root = scr->root;
		if (!(wm = scr->wm) || !(ops = get_wm_ops()) || !ops->set_style) {
			EPRINTF("cannot set style for screen %d\n", screen);
			return;
		}
		ops->set_style();
	} else {
		EPRINTF("invalid screen number %d\n", options.screen);
		exit(2);
	}
}

static void
copying(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stdout, "\
--------------------------------------------------------------------------------\n\
%1$s\n\
--------------------------------------------------------------------------------\n\
Copyright (c) 2008-2014  Monavacon Limited <http://www.monavacon.com/>\n\
Copyright (c) 2001-2008  OpenSS7 Corporation <http://www.openss7.com/>\n\
Copyright (c) 1997-2001  Brian F. G. Bidulock <bidulock@openss7.org>\n\
\n\
All Rights Reserved.\n\
--------------------------------------------------------------------------------\n\
This program is free software: you can  redistribute it  and/or modify  it under\n\
the terms of the  GNU Affero  General  Public  License  as published by the Free\n\
Software Foundation, version 3 of the license.\n\
\n\
This program is distributed in the hope that it will  be useful, but WITHOUT ANY\n\
WARRANTY; without even  the implied warranty of MERCHANTABILITY or FITNESS FOR A\n\
PARTICULAR PURPOSE.  See the GNU Affero General Public License for more details.\n\
\n\
You should have received a copy of the  GNU Affero General Public License  along\n\
with this program.   If not, see <http://www.gnu.org/licenses/>, or write to the\n\
Free Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.\n\
--------------------------------------------------------------------------------\n\
U.S. GOVERNMENT RESTRICTED RIGHTS.  If you are licensing this Software on behalf\n\
of the U.S. Government (\"Government\"), the following provisions apply to you. If\n\
the Software is supplied by the Department of Defense (\"DoD\"), it is classified\n\
as \"Commercial  Computer  Software\"  under  paragraph  252.227-7014  of the  DoD\n\
Supplement  to the  Federal Acquisition Regulations  (\"DFARS\") (or any successor\n\
regulations) and the  Government  is acquiring  only the  license rights granted\n\
herein (the license rights customarily provided to non-Government users). If the\n\
Software is supplied to any unit or agency of the Government  other than DoD, it\n\
is  classified as  \"Restricted Computer Software\" and the Government's rights in\n\
the Software  are defined  in  paragraph 52.227-19  of the  Federal  Acquisition\n\
Regulations (\"FAR\")  (or any successor regulations) or, in the cases of NASA, in\n\
paragraph  18.52.227-86 of  the  NASA  Supplement  to the FAR (or any  successor\n\
regulations).\n\
--------------------------------------------------------------------------------\n\
", NAME " " VERSION);
}

static void
version(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stdout, "\
%1$s (OpenSS7 %2$s) %3$s\n\
Written by Brian Bidulock.\n\
\n\
Copyright (c) 2008, 2009, 2010, 2011, 2012, 2013, 2014  Monavacon Limited.\n\
Copyright (c) 2001, 2002, 2003, 2004, 2005, 2006, 2007, 2008  OpenSS7 Corporation.\n\
Copyright (c) 1997, 1998, 1999, 2000, 2001  Brian F. G. Bidulock.\n\
This is free software; see the source for copying conditions.  There is NO\n\
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.\n\
\n\
Distributed by OpenSS7 under GNU Affero General Public License Version 3,\n\
with conditions, incorporated herein by reference.\n\
\n\
See `%1$s --copying' for copying permissions.\n\
", NAME, PACKAGE, VERSION);
}

static void
usage(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stderr, "\
Usage:\n\
    %1$s [{-l|--list}] [options]\n\
    %1$s {-s|--set} [options] STYLE\n\
    %1$s {-h|--help}\n\
    %1$s {-V|--version}\n\
    %1$s {-C|--copying}\n\
", argv[0]);
}

static void
help(int argc, char *argv[])
{
	if (!options.output && !options.debug)
		return;
	(void) fprintf(stdout, "\
Usage:\n\
    %1$s [{-c|--current}] [options]\n\
    %1$s {-l|--list} [options] [STYLE]\n\
    %1$s {-s|--set} [options] STYLE\n\
    %1$s {-h|--help}\n\
    %1$s {-V|--version}\n\
    %1$s {-C|--copying}\n\
Arguments:\n\
    STYLE\n\
        style name or full path to style file\n\
        name of the style to list or set\n\
Command options:\n\
    -c, --current\n\
        list current style [default when no other command specified]\n\
    -l, --list [STYLE]\n\
        list styles\n\
    -s, --set STYLE\n\
        set the style\n\
    -h, --help, -?, --?\n\
        print this usage information and exit\n\
    -V, --version\n\
        print version and exit\n\
    -C, --copying\n\
        print copying permission and exit\n\
Options:\n\
    -y, --system\n\
        set or list system styles\n\
    -u, --user\n\
        set or list user styles\n\
    -S, --screen SCREEN\n\
        only act on screen number SCREEN [default: all(-1)]\n\
    -L, --link\n\
        link style files where possible\n\
    -t, --theme\n\
        only list styles that are also XDE themes\n\
    -D, --debug [LEVEL]\n\
        increment or set debug LEVEL [default: 0]\n\
    -v, --verbose [LEVEL]\n\
        increment or set output verbosity LEVEL [default: 1]\n\
        this option may be repeated.\n\
", argv[0]);
}

int
main(int argc, char *argv[])
{
	while (1) {
		int c, val;

#ifdef _GNU_SOURCE
		int option_index = 0;
		/* *INDENT-OFF* */
		static struct option long_options[] = {
			{"current",	no_argument,		NULL, 'c'},
			{"list",	no_argument,		NULL, 'l'},
			{"set",		no_argument,		NULL, 's'},
			{"system",	no_argument,		NULL, 'y'},
			{"user",	no_argument,		NULL, 'u'},
			{"screen",	required_argument,	NULL, 'S'},
			{"link",	no_argument,		NULL, 'L'},
			{"theme",	no_argument,		NULL, 't'},

			{"dry-run",	no_argument,		NULL, 'n'},
			{"debug",	optional_argument,	NULL, 'D'},
			{"verbose",	optional_argument,	NULL, 'v'},
			{"help",	no_argument,		NULL, 'h'},
			{"version",	no_argument,		NULL, 'V'},
			{"copying",	no_argument,		NULL, 'C'},
			{"?",		no_argument,		NULL, 'H'},
			{ 0, }
		};
		/* *INDENT-ON* */

		c = getopt_long_only(argc, argv, "clsyuS:LtnD::v::hVCH?", long_options,
				     &option_index);
#else				/* defined _GNU_SOURCE */
		c = getopt(argc, argv, "clsyuS:LtnDvhVC?");
#endif				/* defined _GNU_SOURCE */
		if (c == -1) {
			if (options.debug)
				fprintf(stderr, "%s: done options processing\n", argv[0]);
			break;
		}
		switch (c) {
		case 0:
			goto bad_usage;

		case 'c':	/* -c, --current */
			options.set = False;
			options.list = False;
			options.current = True;
			break;
		case 'l':	/* -l, --list */
			options.set = False;
			options.current = False;
			options.list = True;
			break;
		case 's':	/* -s, --set */
			options.list = False;
			options.current = False;
			options.set = True;
			break;
		case 'y':	/* -y, --system */
			options.user = False;
			options.system = True;
			break;
		case 'u':	/* -u, --user */
			options.system = False;
			options.user = True;
			break;
		case 'S':	/* -S, --screen */
			options.screen = atoi(optarg);
			break;
		case 'L':	/* -L, --link */
			options.link = True;
			break;
		case 't':
			options.theme = True;
			break;
		case 'n':	/* -n, --dry-run */
			options.dryrun = True;
			break;
		case 'D':	/* -D, --debug [level] */
			if (options.debug)
				fprintf(stderr, "%s: increasing debug verbosity\n",
					argv[0]);
			if (optarg == NULL) {
				options.debug++;
			} else {
				if ((val = strtol(optarg, NULL, 0)) < 0)
					goto bad_option;
				options.debug = val;
			}
			break;
		case 'v':	/* -v, --verbose [level] */
			if (options.debug)
				fprintf(stderr, "%s: increasing output verbosity\n",
					argv[0]);
			if (optarg == NULL) {
				options.output++;
				break;
			}
			if ((val = strtol(optarg, NULL, 0)) < 0)
				goto bad_option;
			options.output = val;
			break;
		case 'h':	/* -h, --help */
		case 'H':	/* -H, --? */
			if (options.debug)
				fprintf(stderr, "%s: printing help message\n", argv[0]);
			help(argc, argv);
			exit(0);
		case 'V':	/* -V, --version */
			if (options.debug)
				fprintf(stderr, "%s: printing version message\n",
					argv[0]);
			version(argc, argv);
			exit(0);
		case 'C':	/* -C, --copying */
			if (options.debug)
				fprintf(stderr, "%s: printing copying message\n",
					argv[0]);
			copying(argc, argv);
			exit(0);
		case '?':
		default:
		      bad_option:
			optind--;
			goto bad_nonopt;
		      bad_nonopt:
			if (options.output || options.debug) {
				if (optind < argc) {
					fprintf(stderr, "%s: syntax error near '",
						argv[0]);
					while (optind < argc)
						fprintf(stderr, "%s ", argv[optind++]);
					fprintf(stderr, "'\n");
				} else {
					fprintf(stderr, "%s: missing option or argument",
						argv[0]);
					fprintf(stderr, "\n");
				}
				fflush(stderr);
			      bad_usage:
				usage(argc, argv);
			}
			exit(2);
		}
	}
	if (options.debug) {
		fprintf(stderr, "%s: option index = %d\n", argv[0], optind);
		fprintf(stderr, "%s: option count = %d\n", argv[0], argc);
	}
	if (optind < argc) {
		options.style = strdup(argv[optind++]);
		if (optind < argc)
			goto bad_nonopt;
	}
	init_display();
	if (!detect_wm()) {
		EPRINTF("%s\n", "no detected window manager");
		exit(1);
	}
	if (options.current)
		current_style();
	else if (options.list)
		list_styles();
	else if (options.set)
		set_style();
	else {
		usage(argc, argv);
		exit(2);
	}
	if (options.output > 1)
		show_wms();
	exit(0);
}

// vim: set sw=8 tw=80 com=srO\:/**,mb\:*,ex\:*/,srO\:/*,mb\:*,ex\:*/,b\:TRANS foldmarker=@{,@} foldmethod=marker: