#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <mpi.h>
#include <stdbool.h>
#include <math.h>

#include <X11/Xlib.h> // X11 library headers
#include <X11/Xutil.h>
#include <X11/Xos.h>
#define X_RESN 1000 /* x resolution */
#define Y_RESN 1000 /* y resolution */
#define PI  3.14159265358979323846 // Pi


//Simple struct to define dual values in a single variable. 
//Complex = Imaginary + Real
struct complexNumber {
	double i; //imaginary
	double r; //real
};

typedef struct complexNumber ComplexNumber;

//function prototypes
Display * x11setup(Window *win, GC *gc, int width, int height);
void drawFractal(Display *display, Window win, GC gc, XColor *colours, int xpos, int ypos, int height, int width);
void iteration(ComplexNumber *z, ComplexNumber *c);
void drawPoint(Display *display, Window win, GC gc, XColor *colours, int x, int y, int depth, int limit);
int seriesDiverges(int depth, ComplexNumber *z, ComplexNumber *c);
XColor pickColour(int depth);
void setupColours(Display *display, XColor *colours, Colormap screenColourmap);
void resetComplexNumber(ComplexNumber *c);

int main(int argc, char *argv[]) {
	int rank, nprocs, x, y;
	Window win; // initialization for a window
	GC gc; // graphics context
	Display *display = NULL;
	Colormap screenColourmap;
	XColor colours[15];
	unsigned int width = X_RESN, height = Y_RESN; /* window size */
	clock_t start, end, elapsed;
	int pixelToDraw[3];
	MPI_Init(&argc, &argv); 
	MPI_Comm_size(MPI_COMM_WORLD, &nprocs); 
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	ComplexNumber c;
	ComplexNumber z;
	int depth = 1000;
	int cSize = 4;
	double cX = -2;
	double cY = -2;
	double cMinR = cX;
	double cMinI = cY;
	double cMaxR = cMinR + cSize;
	double cMaxI = cMinI + cSize;
	double increaseX = (cMaxR - cMinR) / X_RESN;
	double increaseY = (cMaxI - cMinI) / Y_RESN;
	int drawable[X_RESN][Y_RESN];
        
	if(rank==0)
	{
		display = x11setup(&win, &gc, width, height);
		screenColourmap = DefaultColormap(display, DefaultScreen(display));
		setupColours(display, colours, screenColourmap);
		int probeFlag = 0;
		MPI_Status status;
		int count = 0;
		int start = 1;
		for (int i = 1; i < nprocs; i++) {
			MPI_Send(&start, 1, MPI_INT, i, 98, MPI_COMM_WORLD);
		}
		for (int y = 0; y < Y_RESN; y++) {
			for (int x = 0; x < X_RESN; x++) {
				while (!probeFlag) {
					MPI_Iprobe(MPI_ANY_SOURCE, 99, MPI_COMM_WORLD, &probeFlag, &status);
					if (probeFlag) {
						int coords[2] = {x, y};
						int slave = status.MPI_SOURCE;
						MPI_Recv(&pixelToDraw, 3, MPI_INT, MPI_ANY_SOURCE, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
						MPI_Send(&coords, 2, MPI_INT, slave, 99, MPI_COMM_WORLD); 
						drawable[pixelToDraw[0]][pixelToDraw[1]] = pixelToDraw[2];
						count++;
					} else {
						MPI_Iprobe(MPI_ANY_SOURCE, 98, MPI_COMM_WORLD, &probeFlag, &status);
						if (probeFlag) {
							int slave = status.MPI_SOURCE;
							int coords[2] = {x, y};
							MPI_Recv(&start, 1, MPI_INT, MPI_ANY_SOURCE, 98, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
							MPI_Send(&coords, 2, MPI_INT, slave, 99, MPI_COMM_WORLD); 
						}
					}
				}
				probeFlag = 0;
			}
		}
		MPI_Recv(&pixelToDraw, 3, MPI_INT, MPI_ANY_SOURCE, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		drawable[pixelToDraw[0]][pixelToDraw[1]] = pixelToDraw[2];
		for (int y = 0; y < Y_RESN; y++) {
			for (int x = 0; x < X_RESN; x++) {
				drawPoint(display, win, gc, colours, x, y, depth, drawable[x][y]);
			}
		}
	} else {
		int flag = 1;
		int coords[2];
		MPI_Recv(&start, 1, MPI_INT, 0, 98, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		MPI_Send(&start, 1, MPI_INT, 0, 98, MPI_COMM_WORLD);
		while (flag) {
			MPI_Recv(&coords, 2, MPI_INT, 0, 99, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
			c.r = coords[0] * increaseX - 2;
			c.i = coords[1] * increaseY - 2;
			resetComplexNumber(&z);
			int limit = seriesDiverges(depth, &z, &c);
			pixelToDraw[0] = coords[0];
			pixelToDraw[1] = coords[1];
			pixelToDraw[2] = limit;
			MPI_Send(&pixelToDraw, 3, MPI_INT, 0, 99, MPI_COMM_WORLD);
		}
	}
	
	// main loop
	int running = 1; // loop variable
	start = clock();
	while(running) {
	
		// checks to see if there have been any events,
		// will exit the main loop if any key is pressed
		if(rank==0) {
			if(XPending(display)) {
				XEvent ev;
				XNextEvent(display, &ev);
				switch(ev.type) {
					case KeyPress:
						running = 0;
						break;
				}
			}
		}
		
		end = clock();
		elapsed = end - start;
		// only update the display if > 1 millisecond has passed since the last update
		if(elapsed / (CLOCKS_PER_SEC/1000) > 1 && rank==0) {
			//XClearWindow(display, win);
			start = end;
			XFlush(display);
		}
	}

	if(rank==0 && display) {
		XCloseDisplay(display); // close the display window
		printf("Display closed\n");
	} else {
		MPI_Finalize();
		return 0;
	}
	
	MPI_Finalize();
	return 0;
}

void drawPoint(Display *display, Window win, GC gc, XColor *colours, int x, int y, int depth, int limit) {
	XColor colour;
	if (limit == depth) {
		colour = colours[14];
	} else {
		colour = colours[limit % 14];
	}
	XSetForeground(display, gc, colour.pixel);
	XDrawPoint(display, win, gc, x, y);
}

int seriesDiverges(int depth, ComplexNumber *z, ComplexNumber *c) {
	for (int i = 0; i < depth; i++) {
		iteration(z, c);
		if (pow((*z).r, 2) + pow((*z).i, 2) > 4) {
			return i; 
		}
	}
	return depth; //Recurrence depth beyond scope.
}

void iteration(ComplexNumber *z, ComplexNumber *c) {
	double tempR = pow((*z).r, 2) - pow((*z).i, 2) + (*c).r;
	double tempI = 2 * (*z).r * (*z).i + (*c).i;
	(*z).r = tempR;
	(*z).i = tempI;
}

XColor pickColour(int depth) {
	double tau = PI * 2;
	double tauRainbow = tau / 14;
	double tauPart = tau / 3;
	XColor colour;
	colour.red = (int)(sin(tauRainbow * depth + 1 * tauPart) * 32767 + 32768);
	colour.green = (int)(sin(tauRainbow * depth + 2 * tauPart) * 32767 + 32768);
	colour.blue = (int)(sin(tauRainbow * depth + 3 * tauPart) * 32767 + 32768);
	return colour;
}

void setupColours(Display *display, XColor *colours, Colormap screenColourmap) {
	for (int i = 0; i < 14; i++) {
		colours[i] = pickColour(i);
		XAllocColor(display, screenColourmap, &colours[i]);
	}
	colours[14].pixel = BlackPixel (display, DefaultScreen(display));
}

void resetComplexNumber(ComplexNumber *c) {
	(*c).r = 0;
	(*c).i = 0;
}

Display * x11setup(Window *win, GC *gc, int width, int height) {
	
	/* --------------------------- X11 graphics setup ------------------------------ */
	Display 		*display;
	unsigned int 	win_x,win_y, /* window position */
					border_width, /* border width in pixels */
					display_width, display_height, /* size of screen */
					screen; /* which screen */
	
	char 			window_name[] = "Mandelbrot", *display_name = NULL;
	unsigned long 	valuemask = 0;
	XGCValues 		values;
	
	XSizeHints 		size_hints;
	
	//Pixmap 		bitmap;
	//XPoint 		points[800];
	FILE 			*fopen ();//, *fp;
	//char 			str[100];
	
	XSetWindowAttributes attr[1];
	
	if ( (display = XOpenDisplay (display_name)) == NULL ) { /* connect to Xserver */
		fprintf (stderr, "Cannot connect to X server %s\n",XDisplayName (display_name) );
		exit (-1);
	}
	
	screen = DefaultScreen (display); /* get screen size */
	display_width = DisplayWidth (display, screen);
	display_height = DisplayHeight (display, screen);
	
	win_x = 0; win_y = 0; /* set window position */
	
	border_width = 4; /* create opaque window */
	*win = XCreateSimpleWindow (display, RootWindow (display, screen),
			win_x, win_y, width, height, border_width,
			WhitePixel (display, screen), BlackPixel (display, screen));
			
	size_hints.flags = USPosition|USSize;
	size_hints.x = win_x;
	size_hints.y = win_y;
	size_hints.width = width;
	size_hints.height = height;
	size_hints.min_width = 300;
	size_hints.min_height = 300;
	
	XSetNormalHints (display, *win, &size_hints);
	XStoreName(display, *win, window_name);
	
	*gc = XCreateGC (display, *win, valuemask, &values); /* create graphics context */
	
	XSetBackground (display, *gc, BlackPixel (display, screen));
	XSetForeground (display, *gc, WhitePixel (display, screen));
	XSetLineAttributes (display, *gc, 1, LineSolid, CapRound, JoinRound);
	
	attr[0].backing_store = Always;
	attr[0].backing_planes = 1;
	attr[0].backing_pixel = BlackPixel(display, screen);
	
	XChangeWindowAttributes(display, *win, CWBackingStore | CWBackingPlanes | CWBackingPixel, attr);
	
	XSelectInput(display, *win, KeyPressMask);
	
	XMapWindow (display, *win);
	XSync(display, 0);
	
	/* --------------------------- End of X11 graphics setup ------------------------------ */
	return display;
}