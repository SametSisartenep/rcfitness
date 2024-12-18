#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>
#include <geometry.h>

#define HZ2MS(hz)	(1000/(hz))
#define max(a, b)	((a) > (b)? (a): (b))

enum {
	Stop,
	Pause,
	Run,
};

typedef struct Stopwatch Stopwatch;
struct Stopwatch
{
	uvlong elapsed;		/* in ms */
	char hms[3][6+1];	/* HH MM SS.ss */
	int state;

	void (*start)(Stopwatch*);
	void (*stop)(Stopwatch*);
	void (*pause)(Stopwatch*);
	void (*update)(Stopwatch*, uvlong);
	void (*draw)(Stopwatch*, Image*, Point, double);
};

Rectangle UR = {0,0,1,1};
char deffont[] = "/lib/font/bit/lucida/unicode.32.font";
Image *d7bg, *d7fg;
int d7scale = 60;

Image *screenb;
Keyboardctl *kc;
Mousectl *mc;
Channel *drawc;
Stopwatch *chrono;

uvlong nanosec(void);

static Image *
eallocimage(Display *d, Rectangle r, ulong chan, int repl, ulong col)
{
	Image *i;

	i = allocimage(d, r, chan, repl, col);
	if(i == nil)
		sysfatal("allocimage: %r");
	return i;
}

/*
 * 7-segment display interface
 *
 * 	   A
 * 	   ----
 * 	  |    | B
 * 	F | G  |
 * 	   ----
 * 	  |    | C
 * 	E |    |
 * 	   ----   [] H
 * 	   D
 *
 * bits: 7 6 5 4 3 2 1 0
 * func: H G F E D C B A
 */
static Point
d7(Image *dst, Point dp, uchar bits, int scale, Image *fg, Image *bg)
{
	enum { TV, TH, TD, NSEGS };
	struct {
		Point2 poly[6];
		Point pts[6+1];
		int npts;
	} segs[NSEGS] = {	/* segment parameters */
	 [TV]	{ .poly = {
			{ 1,   0, 1 },
			{ 1.5, 1, 1 },
			{ 1.5, 5, 1 },
			{ 1,   6, 1 },
			{ 0.5, 5, 1 },
			{ 0.5, 1, 1 },
		}, .npts = 6+1 },
	 [TH]	{ .npts = 6+1 },
	 [TD]	{ .poly = {
			{ 0, 0, 1 },
			{ 1, 0, 1 },
			{ 1, 1, 1 },
			{ 0, 1, 1 },
		}, .npts = 4+1 },
	};
	struct {
		Point p;
		int segtype;
	} loc[8] = {		/* segment locations (layout) */
		{ 1,  2, TH },	/* A */
		{ 6,  1, TV },	/* B */
		{ 6,  7, TV },	/* C */
		{ 1, 14, TH },	/* D */
		{ 0,  7, TV },	/* E */
		{ 0,  1, TV },	/* F */
		{ 1,  8, TH },	/* G */
		{ 8, 13, TD },	/* H (dot) */
	};
	Rectangle bbox = {
		{ 0, 0 },
		{ 9, 14 },
	};
	Point segpt[7];
	double maxlen;
	int i, j;

	maxlen = 0;
	for(i = 0; i < segs[TV].npts-1; i++)
		maxlen = max(maxlen, vec2len(segs[TV].poly[i]));

	bbox.max.x = (double)bbox.max.x/maxlen * scale;
	bbox.max.y = (double)bbox.max.y/maxlen * scale;
	for(i = 0; i < nelem(loc); i++){
		loc[i].p.x = (double)loc[i].p.x/maxlen * scale;
		loc[i].p.y = (double)loc[i].p.y/maxlen * scale;
	}

	/* normalize TV and build TH out of it */
	for(i = 0; i < segs[TV].npts-1; i++){
		segs[TV].poly[i] = divpt2(segs[TV].poly[i], maxlen);
		segs[TH].poly[i] = Vec2(segs[TV].poly[i].y, -segs[TV].poly[i].x);

		segs[TV].pts[i] = Pt(segs[TV].poly[i].x*scale, segs[TV].poly[i].y*scale);
		segs[TH].pts[i] = Pt(segs[TH].poly[i].x*scale, segs[TH].poly[i].y*scale);
	}
	segs[TH].pts[i] = segs[TH].pts[0];
	segs[TV].pts[i] = segs[TV].pts[0];

	/* normalize TD */
	for(i = 0; i < segs[TD].npts-1; i++){
		segs[TD].poly[i] = divpt2(segs[TD].poly[i], maxlen);
		segs[TD].pts[i] = Pt(segs[TD].poly[i].x*scale, segs[TD].poly[i].y*scale);
	}
	segs[TD].pts[i] = segs[TD].pts[0];

	/* paint case */
	bbox = rectaddpt(bbox, addpt(dst->r.min, dp));
	draw(dst, bbox, bg, nil, ZP);

	/* paint segments */
	for(i = 0; i < nelem(loc); i++){
		if((bits & 1<<i) == 0)
			continue;

		for(j = 0; j < segs[loc[i].segtype].npts-1; j++){
			segpt[j] = addpt(segs[loc[i].segtype].pts[j], loc[i].p);
			segpt[j] = addpt(segpt[j], bbox.min);
		}
		segpt[j] = segpt[0];

		fillpoly(dst, segpt, segs[loc[i].segtype].npts, 0, fg, ZP);
		if(scale > 16)
			poly(dst, segpt, segs[loc[i].segtype].npts, 0, 0, 0, display->black, ZP);
	}

	return Pt(bbox.max.x + 1, bbox.min.y);
}

static Point
string7(Image *dst, Point dp, char *s, int scale, Image *fg, Image *bg)
{
	static struct {
		char c;
		uchar bits;
	} dmap[] = {
		'0', 0x3F,
		'1', 0x06,
		'2', 0x5B,
		'3', 0x4F,
		'4', 0x66,
		'5', 0x6D,
		'6', 0x7D,
		'7', 0x07,
		'8', 0x7F,
		'9', 0x6F,
	};
	uchar bits;
	int i;

	if(s == nil)
		return dp;

	for(; *s != 0; s++)
		for(i = 0; i < nelem(dmap); i++)
			if(dmap[i].c == *s){
				bits = dmap[i].bits;
				if(s[1] == '.')
					bits |= 0x80;
				dp = d7(dst, dp, bits, scale, fg, bg);
				break;
			}
	return dp;
}

static void
stopwatch_start(Stopwatch *self)
{
	if(self->state == Stop)
		self->elapsed = 0;

	self->state = Run;
}

static void
stopwatch_stop(Stopwatch *self)
{
	if(self->state == Run)
		self->state = Stop;
}

static void
stopwatch_pause(Stopwatch *self)
{
	if(self->state == Run)
		self->state = Pause;
}

static void
stopwatch_update(Stopwatch *self, uvlong dt)
{
	int HMS[4], i;
	double t;

	self->elapsed += dt;
	t = self->elapsed;
	t /= 60*60*1000;	HMS[0] = t;	t -= HMS[0];
	t *= 60;		HMS[1] = t;	t -= HMS[1];
	t *= 60;		HMS[2] = t;	t -= HMS[2];
	t *= 1000;		HMS[3] = t;

	for(i = 0; i < nelem(HMS)-2; i++)
		snprint(self->hms[i], sizeof self->hms[i], "%02d", HMS[i]);
	snprint(self->hms[i], sizeof self->hms[i], "%02d.%03d", HMS[i], HMS[i+1]);
}

static void
stopwatch_draw(Stopwatch *self, Image *dst, Point dp, double scale)
{
	int i;

	for(i = 0; i < nelem(self->hms); i++){
		if(i > 0)
			dp.x += scale/3;
		dp = string7(dst, dp, self->hms[i], scale, d7fg, d7bg);
	}
}

void
timer(void *arg)
{
	Stopwatch *s;
	uvlong t0, t1;
	uvlong dt;	/* in ms */

	threadsetname("tic-tac");

	s = arg;
	t0 = nanosec();
	for(;;){
		t1 = nanosec();
		dt = (t1 - t0)/1000000ULL;

		if(s->state == Run){
			s->update(s, dt);
			nbsend(drawc, nil);
		}

		t0 = t1;
		sleep(HZ2MS(13));
	}
}

Stopwatch *
mkstopwatch(void)
{
	Stopwatch *s;

	s = malloc(sizeof *s);
	if(s == nil)
		sysfatal("malloc: %r");

	memset(s, 0, sizeof *s);
	s->start = stopwatch_start;
	s->stop = stopwatch_stop;
	s->pause = stopwatch_pause;
	s->update = stopwatch_update;
	s->draw = stopwatch_draw;

	s->update(s, 0);
	proccreate(timer, s, mainstacksize);

	return s;
}

void
rmstopwatch(Stopwatch *s)
{
	free(s);
}

void
initscreenb(void)
{
	if(screenb != nil)
		freeimage(screenb);

	screenb = eallocimage(display, rectsubpt(screen->r, screen->r.min), screen->chan, 0, DNofill);
}

void
redraw(void)
{
	draw(screenb, screenb->r, display->black, nil, ZP);
	chrono->draw(chrono, screenb, Pt(10, 10), d7scale);
	draw(screen, screen->r, screenb, nil, ZP);
	flushimage(display, 1);
}

void
resize(void)
{
	if(getwindow(display, Refnone) < 0)
		sysfatal("resize failed");

	initscreenb();
	nbsend(drawc, nil);
}

void
mouse(Mousectl *mc)
{
	if(mc->buttons & 8)
		d7scale += 2;
	if(mc->buttons & 16)
		d7scale -= 2;
	if(mc->buttons != 0)
		nbsend(drawc, nil);
}

void
key(Rune r)
{
	switch(r){
	case Kdel:
		threadexitsall(nil);
	case Kesc:
		if(chrono->state == Run)
			chrono->pause(chrono);
		else if(chrono->state == Pause)
			chrono->start(chrono);
		break;
	case ' ':
		if(chrono->state == Run)
			chrono->stop(chrono);
		else if(chrono->state == Stop)
			chrono->start(chrono);
		break;
	}
}

void
usage(void)
{
	fprint(2, "usage: %s\n", argv0);
	exits("usage");
}

void
threadmain(int argc, char *argv[])
{
	Rune r;

	ARGBEGIN{
	default: usage();
	}ARGEND;

	if(initdraw(nil, deffont, "chrono") < 0)
		sysfatal("initdraw: %r");
	if((mc = initmouse(nil, screen)) == nil)
		sysfatal("initmouse: %r");
	if((kc = initkeyboard(nil)) == nil)
		sysfatal("initkeyboard: %r");

	d7bg = eallocimage(display, UR, XRGB32, 1, 0x333333FF);
	d7fg = eallocimage(display, UR, XRGB32, 1, DRed);

	initscreenb();
	drawc = chancreate(sizeof(void*), 1);
	nbsend(drawc, nil);
	chrono = mkstopwatch();

	enum { MOUSE, RESIZE, KEYS, DRAW, NONE };
	Alt a[] = {
	 [MOUSE]	{mc->c, &mc->Mouse, CHANRCV},
	 [RESIZE]	{mc->resizec, nil, CHANRCV},
	 [KEYS]		{kc->c, &r, CHANRCV},
	 [DRAW]		{drawc, nil, CHANRCV},
	 [NONE]		{nil, nil, CHANEND}
	};
	for(;;)
		switch(alt(a)){
		case MOUSE:
			mouse(mc);
			break;
		case RESIZE:
			resize();
			break;
		case KEYS:
			key(r);
			break;
		case DRAW:
			redraw();
			break;
		default:
			sysfatal("main loop interrupted");
		}
}
