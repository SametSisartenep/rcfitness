#include <u.h>
#include <libc.h>
#include <thread.h>
#include <draw.h>
#include <mouse.h>
#include <keyboard.h>

#define HZ2MS(hz)	(1000/(hz))

enum {
	Stop,
	Pause,
	Run,
};

typedef struct Stopwatch Stopwatch;
struct Stopwatch
{
	uvlong elapsed;		/* in ms */
	char hms[4][4];
	int state;

	void (*start)(Stopwatch*);
	void (*stop)(Stopwatch*);
	void (*pause)(Stopwatch*);
	void (*update)(Stopwatch*, uvlong);
	void (*draw)(Stopwatch*, Image*, Point, double);
};

char deffont[] = "/lib/font/bit/lucida/unicode.32.font";

Image *screenb;
Keyboardctl *kc;
Mousectl *mc;
Channel *drawc;
Stopwatch *chrono;

uvlong nanosec(void);

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

	for(i = 0; i < nelem(HMS); i++)
		snprint(self->hms[i], sizeof self->hms[i], i < 3? "%02d": "%03d", HMS[i]);
}

static void
stopwatch_draw(Stopwatch *self, Image *dst, Point dp, double scale)
{
	USED(scale);
	int i;

	for(i = 0; i < nelem(self->hms); i++){
		if(i > 0)
			dp = string(dst, dp, display->white, ZP, font, i < 3? ":": ".");
		dp = string(dst, dp, display->white, ZP, font, self->hms[i]);
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

	screenb = allocimage(display, rectsubpt(screen->r, screen->r.min), screen->chan, 0, DNofill);
	if(screenb == nil)
		sysfatal("allocimage: %r");
}

void
redraw(void)
{
	draw(screenb, screenb->r, display->black, nil, ZP);
	chrono->draw(chrono, screenb, Pt(10, 10), 1);
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
mouse(Mousectl *)
{

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
