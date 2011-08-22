/*
 * Copyright (C) 2010 Freescale Semiconductor, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <sys/mman.h>
#include <cutils/properties.h>
#include "five_wire_calib.h"
#include <sys/poll.h>

#define ARRAY_SIZE(__arr) (sizeof(__arr)/sizeof(__arr[0]))
#define LOG_BUF_MAX 512

#ifndef TS_DEVICE
#error "no touch screen device defined"
#endif

#define DEV1_(x) #x
#define DEV_(x) DEV1_(x)
#define TS_INPUT_DEV DEV_(TS_DEVICE)

static const char fb_dev[] = "/dev/graphics/fb0";
static const char input_dev[] = "/dev/input/event";
static const char cf_file[] = "/data/system/calibration";
static const char log[] = "/data/ts.log";
static const char default_dev_name[] = TS_INPUT_DEV;
static char const *dev_name = default_dev_name ;
static int log_fd;
static struct fb_var_screeninfo info;
static void *scrbuf;
static unsigned scrsize ;
static int fb_fd, ts_fd, cf_fd, tty_fd ;
static int cal_val[7];

static void log_write(const char *fmt, ...)
{
    char buf[LOG_BUF_MAX];
    va_list ap;

    if (log_fd < 0) return;

    va_start(ap, fmt);
    vsnprintf(buf, LOG_BUF_MAX, fmt, ap);
    buf[LOG_BUF_MAX - 1] = 0;
    va_end(ap);
    write(log_fd, buf, strlen(buf));
}

static void write_conf(int *data)
{
    char param_path[256];
    char buf[200];
    int fd, len;
    int num_written ;

    sprintf(param_path,
	    "/sys/module/%s/parameters/calibration", dev_name);
    fd = open(param_path, O_WRONLY);
    if (fd < 0) {
	log_write("write_conf() error, can not write driver parameters to %s\n", param_path);
	return;
    }
    len = sprintf(buf, "%d,%d,%d,%d,%d,%d,%d",
			    data[0], data[1], data[2],
			    data[3], data[4], data[5],
			    data[6]);
    log_write("write_conf(), write driver parameters:\n%s\n", buf);
    num_written = write(fd, buf, len);
    log_write("write_conf(), wrote %d bytes\n");
    close(fd);
}

static void no_conf(void)
{
    char param_path[256];
    char buf[] = {"0,0,0,0,0,0,0"};
    int fd ;

    sprintf(param_path,
	    "/sys/module/%s/parameters/calibration", dev_name);
    fd = open(param_path, O_WRONLY);
    if (fd < 0) {
	log_write("no_conf() error, can not write driver parameters to %s\n", param_path);
	return;
    }
    write(fd, buf, strlen(buf));
    close(fd);
}

static void save_conf(int *data)
{
    char buf[200];
    int len;

    cf_fd = open(cf_file, O_RDWR | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (cf_fd < 0) {
	log_write("create, open file %s error:%d\n", cf_file, errno);
	return;
    }

    len = sprintf(buf, "%d\n%d\n%d\n%d\n%d\n%d\n%d",
			    data[0], data[1], data[2],
			    data[3], data[4], data[5],
			    data[6]);
    write(cf_fd, buf, len);
    close(cf_fd);
}

#define CSI "\x1b["
#define NOCURSOR CSI "?25l"
#define CLEARSCREEN CSI "2J"

static void tty_gotoxy(unsigned x, unsigned y){
	if (0 <= tty_fd) {
		char buf[80];
		int len = snprintf(buf,sizeof(buf), CSI "%u;%uH", y, x );
		write(tty_fd,buf,len);
	}
}

static void tty_write(unsigned x, unsigned y, const char *fmt, ...)
{
	if (0 <= tty_fd) {
		char buf[LOG_BUF_MAX];
		va_list ap;
		int len ;

		tty_gotoxy(x,y);
		va_start(ap, fmt);
		len = vsnprintf(buf, LOG_BUF_MAX, fmt, ap);
		buf[LOG_BUF_MAX - 1] = 0;
		va_end(ap);
		write(tty_fd, buf, len);
	}
}

static void set_title(char const *title){
	tty_write(10,12,title);
}

static void set_subtitle(char const *title){
	tty_write(10,13,title);
}

/* returns number read */
static int read_input(int timeout_ms, struct input_event *event, int max)
{
	struct pollfd fds ;
	fds.fd = ts_fd ;
	fds.events = POLLIN|POLLERR ;
	if (1 == poll(&fds,1,timeout_ms)) {
		int nread = read(ts_fd,event,sizeof(*event)*max);
		return nread/sizeof(*event);
	}

	return 0 ;
}

static void flush_input(void)
{
	struct input_event ev[64];
	int num_read ;
	/* read ts input */
	while (0 < (num_read = read_input(100,ev,ARRAY_SIZE(ev)))) {
	}
}

static void get_input(int *px, int *py)
{
    int step = 0;
    int num_x = 0 ;
    int num_y = 0 ;
    *px = *py = 0 ;

    while (1) {
	struct input_event ev[64];
	int num_read ;
	/* read ts input */
	if (0 < (num_read = read_input(10000,ev,ARRAY_SIZE(ev)))) {
		int i ;
		for (i = 0; i < num_read ; i++) {
log_write("%s: %d.%d.%d\n", __func__, ev[i].type,ev[i].code,ev[i].value);
		    switch (ev[i].type) {

		    case EV_SYN:
			if (step) {
			    *px /= num_x ;
			    *py /= num_y ;
			    return;
			} else if (num_x && num_y){
				log_write ("%d:%d\n", *px/num_x, *py/num_y);
			}
			break;
		    case EV_KEY:
			if (ev[i].code == BTN_TOUCH && ev[i].value == 0 && (0 < num_x) && (0 < num_y))
			    /* get the final touch */
			    step = 1;
			break;
		    case EV_ABS:
			if (ev[i].code == REL_X) {
			    tty_write(1,2,"%5u",ev[i].value);
			    if (256 < num_x) {
				    *px /= 2 ;
				    num_x /= 2 ;
			    }
			    *px += ev[i].value;
			    num_x++ ;
			}
			else if (ev[i].code == REL_Y) {
			    tty_write(1,3,"%5u",ev[i].value);
			    if (256 < num_y) {
				    *py /= 2 ;
				    num_y /= 2 ;
			    }
			    *py += ev[i].value;
			    num_y++ ;
			}
			break;
		    default:
			break;
		    }
		}
	} else {
		log_write ("idle waiting for touch screen\n");
		exit(1);
	}
    }
}

#define TO565(r,g,b) \
    ((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3))

#define WHITE 0xffffff
#define BLACK 0
#define RED(rgb24) ((rgb24&0xff0000)>>16)
#define GREEN(rgb24) ((rgb24&0x00ff00)>>8)
#define BLUE(rgb24) (rgb24&0xff)
#define RGB16(rgb24) TO565(RED(rgb24),GREEN(rgb24),BLUE(rgb24))

static void draw_line(int x1, int x2, int y1, int y2, unsigned color)
{
    int px_byte = info.bits_per_pixel / 8;
    int start ;
    int i;
    __u16 *buf16;
    __u32 *buf32;

    if (x1 > x2) {
	    int tmp = x1 ; x1 = x2 ; x2 = tmp ;
    }

    if (y1 > y2) {
	    int tmp = y1 ; y1 = y2 ; y2 = tmp ;
    }

    if (0 > x1) x1 = 0 ;
    if (0 > y1) y1 = 0 ;
    if (info.xres <= (unsigned)x2) x2 = info.xres-1 ;
    if (info.yres <= (unsigned)y2) y2 = info.yres-1 ;

    start = (x1 + y1*info.xres) * px_byte;
    if (16 == info.bits_per_pixel)
	    color = RGB16(color);

    if (x1 == x2) {
            /* vertical line */
	    __u8 *buf = (__u8 *)scrbuf + start ;
	    int y ;
	    for (y = y1 ; y < y2 ; y++) {
		    memcpy(buf,&color,px_byte); /* only works little-endian */
		    buf += info.xres*px_byte ;
	    }
    } else if (y1 == y2) {
            /* horizontal line */
	    __u8 *buf = (__u8 *)scrbuf + start ;
	    int x ;
	    for (x = x1 ; x < x2 ; x++) {
		    memcpy(buf,&color,px_byte); /* only works little-endian */
		    buf += px_byte ;
	    }
    } else {
	    /* No diagonals */
	    log_write("No support for diagonal lines: %u..%u %u..%u\n", x1, x2, y1, y2);
    }
}

#define LINE_LEN 16
static void draw_cross(int x, int y, int color)
{
    draw_line(x-(LINE_LEN/2),x+(LINE_LEN/2),y,y,color);
    draw_line(x,x,y-(LINE_LEN/2),y+(LINE_LEN/2),color);
}

static void do_calibration(int xmin, int xmax, int ymin, int ymax)
{
    struct calibrate_point_t points[3];
    int i ;
    int xrange = xmax-xmin;
    int yrange = ymax-ymin;

    memset(scrbuf, 0, scrsize);

    /* print information on screen */
    tty_write(1,1,CLEARSCREEN NOCURSOR);
    set_title("Touchscreen Calibration");

    flush_input();

    points[0].x = info.xres / 4;
    points[0].y = info.yres / 2;
    points[1].x = info.xres / 2;
    points[1].y = info.yres / 4;
    points[2].x = (3 * info.xres) / 4;
    points[2].y = (3 * info.yres) / 4;

retry:
    no_conf();
    for (i = 0; i < 3; i ++) {
        struct calibrate_point_t *pt = points+i ;
	flush_input(); /* in case we're bouncing */
	draw_cross(pt->x, pt->y, WHITE);
        tty_write(1,1,"%5u:%5u",pt->x,pt->y);
	get_input(&pt->i, &pt->j);
	draw_cross(pt->x, pt->y, BLACK);
        points[i].x = (points[i].x*xrange)/info.xres ;
        points[i].y = (points[i].y*yrange)/info.yres ;
    }
	
    if (five_wire_calibrate(points,cal_val)) {
	    log_write("calibration: %u:%u.%d.%d %u:%u.%d.%d %u:%u.%d.%d %u %u\n"
		      , points[0].x, points[0].y, points[0].i, points[0].j
		      , points[1].x, points[1].y, points[1].i, points[1].j
		      , points[2].x, points[2].y, points[2].i, points[2].j
		      , xrange+1, yrange+1 );
	    write_conf(cal_val);
    } else {
	    log_write( "calibration failure\n" );
	    goto retry ;
    }
}

static void draw_box(unsigned x1, unsigned y1, unsigned x2, unsigned y2,unsigned color)
{
	unsigned y ;
	for (y = y1 ; y < y2 ; y++) {
		draw_line(x1,x2,y,y,color);
	}
}

static int point_in_rect
	(unsigned x
	,unsigned y
	,unsigned left
	,unsigned top
	,unsigned right
	,unsigned bottom)
{
	return (x >= left) && (x <= right) && (y >= top) && (y <= bottom);
}

struct test_sample_t {
	unsigned x ;
	unsigned y ;
	unsigned num_i ;
	unsigned num_j ;
	int avg_i ;
	int avg_j ;
	int min_i ;
	int min_j ;
	int max_i ;
	int max_j ;
};

int read_sample(struct test_sample_t *samp)
{
	samp->num_i =
	samp->num_j =
	samp->avg_i = 
        samp->avg_j = 0 ;
	samp->min_i = 
	samp->min_j = 0x7fffffff ;
	samp->max_i = 
	samp->max_j = 0x80000000 ;

	struct input_event ev[64];
	int num_read ;
	int released = 0 ;

	while (0 < (num_read = read_input(5000,ev,ARRAY_SIZE(ev)))) {
		int i ;
		for (i = 0; i < num_read ; i++) {
		    switch (ev[i].type) {
		    case EV_SYN:
			if (released) {
			    if ((0 < samp->num_i) && (0 < samp->num_i)){
				    samp->avg_i /= samp->num_i ;
				    samp->avg_j /= samp->num_j ;
				    return 1 ;
			    } else {
				    log_write ("No samples before release\n");
				    released = 0 ;
			    }
			}
			break;
		    case EV_KEY:
			if (ev[i].code == BTN_TOUCH)
                                released = (ev[i].value == 0);
			break;
		    case EV_ABS: {
			int value = ev[i].value ;
			if (ev[i].code == REL_X) {
			    if (256 < samp->num_i) {
				    samp->avg_i /= 2 ;
				    samp->num_i /= 2 ;
			    }
			    samp->avg_i += value;
			    if (value > samp->max_i)
				    samp->max_i = value ;
			    if (value < samp->min_i)
				    samp->min_i = value ;
			    samp->num_i++ ;
			}
			else if (ev[i].code == REL_Y) {
			    if (256 < samp->num_j) {
				    samp->avg_j /= 2 ;
				    samp->num_j /= 2 ;
			    }
			    samp->avg_j += value;
			    if (value > samp->max_j)
				    samp->max_j = value ;
			    if (value < samp->min_j)
				    samp->min_j = value ;
			    samp->num_j++ ;
			}
			break;
		    }
		    default:
			break;
		    }
		}
	}
	log_write ("timeout waiting for release\n");
	return 0 ;
}

void test_64_points (void) {
	unsigned xincr = info.xres / 9 ;
	unsigned yincr = info.yres / 9 ;
	unsigned y,ypos=yincr ;
	unsigned i = 0 ;
        struct test_sample_t *samples = (struct test_sample_t *)malloc(64*sizeof(struct test_sample_t));
	memset(scrbuf, 0, scrsize);
        no_conf();
    	for (y = 0 ; y < 8 ; y++,ypos+=yincr) {
                unsigned x, xpos=xincr;
		for (x = 0 ; x < 8 ; x++,xpos+=xincr) {
			int got_sample ;
                        struct test_sample_t *s = samples+i ;
			s->x = xpos ;
			s->y = ypos ;
			flush_input();
			draw_cross(xpos,ypos,WHITE);
			got_sample = read_sample(s);
			draw_cross(xpos,ypos,BLACK);
			if(!got_sample)
				break;
			i++ ;
		}
		if (x < 8)
			break;
	}
	if (64 == i) {
		log_write("64-point calibration:\n");
		for (i = 0; i<64; i++) {
			struct test_sample_t *s = samples+i;
			log_write("%u\t%u\t%u\t%u\t%d\t%d\t%d\t%d\t%d\t%d\n",
				  s->x,s->y,
				  s->num_i,s->num_j,
				  s->avg_i,s->avg_j,
				  s->min_i,s->min_j,
				  s->max_i,s->max_j);
		}
	} else {
		log_write("64-point calibration abandoned after %u points\n", i);
	}
	free(samples);
}

static int test_calibration(int xmin,int xmax,int ymin,int ymax)
{
	int num_read ;
	struct input_event ev[64];
	int last_x = -1 ;
	int last_y = -1 ;
	int released = 0 ;
	int const button_size = (16*info.xres)/640 ;
	int const xrange = xmax-xmin ;
	int const yrange = ymax-ymin ;
	int const confirm_left = (info.xres/4)-button_size ;
	int const confirm_right = confirm_left + button_size ;
	int const confirm_top = (info.yres/2)-button_size ;
	int const confirm_bottom = confirm_top + button_size ;
    
	int const test_left = 3*(info.xres/4)-button_size ;
	int const test_right = test_left+button_size ;
	int const test_top = confirm_top ;
	int const test_bottom = confirm_bottom ;
	int last_i = 0, last_j = 0 ;
	tty_write(1,1,CLEARSCREEN NOCURSOR);
	set_title("Touch green box to confirm");
	set_title("Touch red box to run 64-point test");
	draw_box(confirm_left,confirm_top,confirm_right,confirm_bottom,0x00D000);
	draw_box(test_left,test_top,test_right,test_bottom,0xD00000);

	last_y = 0 ;
	
	while (0 < (num_read = read_input(5000, ev,ARRAY_SIZE(ev)))) {
		int i ;
		for (i = 0; i < num_read ; i++) {
		    switch (ev[i].type) {
		    case EV_SYN:
			if (released) {
				if (point_in_rect(last_x,last_y,confirm_left,confirm_top,confirm_right,confirm_bottom)) {
					log_write ("Calibration settings confirmed @%u:%u\n", last_x, last_y);
					save_conf(cal_val);
					return 1 ;
				} else if (point_in_rect(last_x,last_y,test_left,test_top,test_right,test_bottom)) {
					test_64_points();
					return 0 ;
				} else {
					draw_cross(last_x,last_y,0x0000FF);
					log_write("Touch out of range [%u:%u]\n", last_x, last_y);
				}
			} else {
				draw_cross(last_x,last_y,0x808080);
				log_write( "%u:%u..%u:%u\n", last_i, last_j, last_x, last_y);
			}
		    case EV_KEY:
			released = (ev[i].code == BTN_TOUCH && ev[i].value == 0);
			draw_cross(last_x,last_y, released ? 0x808000 : 0x8080FF);
			break;
		    case EV_ABS:
			if (ev[i].code == REL_X) {
			    tty_write(1,2,"%5u",ev[i].value);
			    last_x = (ev[i].value*info.xres)/xrange ;
			    last_i = ev[i].value ;
			}
			else if (ev[i].code == REL_Y) {
			    tty_write(1,3,"%5u",ev[i].value);
			    last_y = (ev[i].value*info.yres)/yrange ;
			    last_j = ev[i].value;
			}
			break;
		    default:
			break;
		    }
		}
	}
	return 0 ;
}

static int check_conf(void)
{
    int data[7];
    char *buffer;
    int ret;
    struct stat s;

    /* check conf file */
    if (stat(cf_file, &s) == 0) {
	/* conf file already existed */
	cf_fd = open(cf_file, O_RDWR);
	if (cf_fd >= 0) {
	    buffer = calloc(1, s.st_size + 1);
	    read(cf_fd, buffer, s.st_size);
	    ret = sscanf(buffer, "%d\n%d\n%d\n%d\n%d\n%d\n%d",
				&data[0], &data[1], &data[2],
				&data[3], &data[4], &data[5],
				&data[6]);
	    if (ret == 7) {
		free(buffer);
		/* write to driver */
		write_conf(data);
		close(cf_fd);
		return 1;
	    }
	    log_write("Failed to get datas from conf file: %d\n", ret);
	    free(buffer);
	    close(cf_fd);
	}
    }
    return 0;
}

static char const tsdevice_tag[] = {
	"tsdev"
};

int main(int argc, char **argv)
{
    struct input_absinfo abs_x ;
    struct input_absinfo abs_y ;

    struct fb_fix_screeninfo finfo;
    struct stat s;
    int i, rv;
    int attempts = 0 ;
    if (!isatty(0)) {
	    char runme[PROPERTY_VALUE_MAX];
	    property_get("ro.calibration", runme, "");
	    if (runme[0] != '1')
		return 0;
    }

    /* open log */
    log_fd = isatty(1) ? 1 : open(log, O_WRONLY | O_CREAT | O_TRUNC);

    log_write("log opened\n");
    if (1 < argc) {
	    dev_name = argv[1];
	    log_write("using device name <%s> from command-line\n", dev_name);
    } else {
	    int fdcmdline = open("/proc/cmdline", O_RDONLY);
	    if (0 < fdcmdline) {
		    char cmdline[2048];
		    int numRead ;
		    numRead = read(fdcmdline,cmdline,sizeof(cmdline)-1);
		    if (0 < numRead) {
			    char *s ;
			    cmdline[numRead] = 0 ;
			    if (0 != (s = strstr(cmdline,tsdevice_tag))) {
				    s += sizeof(tsdevice_tag)-1 ;
				    if ('=' == *s) {
					    dev_name = strdup(++s);
					    s = dev_name ;
					    while (isalnum(*s) || !isspace(*s)) {
						    s++ ;
					    }
					    *s = 0 ;
					    log_write ("using device <%s> from kernel command line\n",dev_name);
				    } else
					    log_write ("calibration device not specified on cmdline\n");
			    } else
				    log_write ("calibration not specified on cmdline\n");
		    } else
			    log_write ("Error reading /proc/cmdline\n");
		    close(fdcmdline);
	    } else
		log_write ("Error opening /proc/cmdline\n");
    }
    if (check_conf())
	goto err_log;

    /* read framebuffer for resolution */
    fb_fd = open(fb_dev, O_RDWR);
    if (fb_fd <= 0) {
	log_write("Failed to open %s\n", fb_dev);
	goto err_log;
    }
    if (-1 == ioctl(fb_fd, FBIOGET_VSCREENINFO, &info)) {
	log_write("Failed to get screen info\n");
	goto err_fb;
    }
    log_write("Screen resolution: %dx%d\n", info.xres, info.yres);
    /* map buffer */
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &finfo) == -1) {
	log_write("Failed to get screen info: %d\n", errno);
        goto err_fb;
    }
    scrbuf = (__u16*) mmap(0, finfo.smem_len,
			    PROT_READ | PROT_WRITE,
			    MAP_SHARED,
			    fb_fd, 0);
    if (scrbuf== MAP_FAILED) {
	log_write("Failed to map screen\n");
	goto err_fb;
    }
    scrsize = finfo.smem_len ;

    for (i = 0; ; i++) {
	/* open touchscreen input dev */
	char ts_dev[256];
	char name[256] = "Unknow";

	sprintf(ts_dev, "%s%d", input_dev, i);

	if (stat(ts_dev, &s) != 0) {
	    log_write("can not find ts device\n");
	    goto err_map;
	}
	ts_fd = open(ts_dev, O_RDWR);
	if (ts_fd < 0) {
	    log_write("Failed to open %s\n", ts_dev);
	    continue;
	}

	ioctl(ts_fd, EVIOCGNAME(sizeof(name)), name);
	log_write("%s: get name: %s\n", ts_dev, name);
	if (strncmp(name, dev_name, strlen(dev_name)) == 0) {
	    break;
	} else {
	    log_write("%s: not %s\n", ts_dev, dev_name);
	    close(ts_fd);
	    continue;
	}
    }

    i = 1 ;
    ioctl( ts_fd, O_NONBLOCK, &i );

    tty_fd = open("/dev/tty0", O_RDWR);

    rv = ioctl( ts_fd, EVIOCGABS(ABS_X), &abs_x);
    if (0 != rv) {
        log_write("Error %m reading ABS_X\n");
        return -1 ;
    }
    else
        log_write ("x range is [0x%x..0x%x]\n", abs_x.minimum, abs_x.maximum);

    rv = ioctl( ts_fd, EVIOCGABS(ABS_Y), &abs_y);
    if (0 != rv) {
        log_write("Error %m reading ABS_Y\n");
        return -1 ;
    }
    else
        log_write ("y range is [0x%x..0x%x]\n", abs_y.minimum, abs_y.maximum);
again:
    do_calibration(abs_x.minimum,abs_x.maximum,abs_y.minimum,abs_y.maximum);

    log_write("Calibration done!!\n");

    if( !test_calibration(abs_x.minimum,abs_x.maximum,abs_y.minimum,abs_y.maximum) ) {
	log_write ("Calibration settings not confirmed\n");
	if (4 > ++attempts)
		goto again ;
	else
		no_conf();
    }
    memset(scrbuf, 0, scrsize);

    close(tty_fd);
    close(ts_fd);
err_map:
    munmap(scrbuf, finfo.smem_len);
err_fb:
    close(fb_fd);
err_log:
    close(log_fd);

    return 0;
}
