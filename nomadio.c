
/*
 * Written by Noma Dio <nomadio@lucifier.net>
 * Public Domain.
 */

/*
 * TODO
 *	reset the iface upon failure in ioctl/read/write
 *	state file dump/restore (everything except for music)
 *	reorder uploaded files
 *	remote control the player
 *	radio auto-scan
 *
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/device.h>
#include <fcntl.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <err.h>

#include <dev/usb/usb.h>

#define	USBDEV_DEFAULT	"/dev/ugen0"

#define	NOMAD_TIMEOUT	2000
#define	NOMAD_BLKSIZE	16384
#define	NOMAD_ALIGN	512
#define	NOMAD_ROUND(s)	(((s) + NOMAD_ALIGN - 1) & ~(NOMAD_ALIGN - 1))

#define	NOMAD_CTRL	0
#define	NOMAD_WRITE	1
#define	NOMAD_READ	2
#define	NOMAD_INTWR	3
#define	NOMAD_INTRD	4

#define COMMAND_FILE_INFO		0x02
#define COMMAND_SELECT_FILE		0x03
#define COMMAND_END_FILEIO		0x04
#define COMMAND_SEND_BLOCK		0x05
#define COMMAND_RECV_BLOCK		0x06
#define COMMAND_DELETE_FILE		0x08
#define COMMAND_REORDER			0x0a
#define COMMAND_MEMORY			0x0d
#define COMMAND_MODEL			0x0e
#define COMMAND_FORMAT			0x0f
#define COMMAND_END_UPLOAD		0x10
#define COMMAND_GET_CLOCK		0x11
#define COMMAND_SET_CLOCK		0x12
#define COMMAND_GET_OWNER		0x1c
#define COMMAND_SET_OWNER		0x1d
#define COMMAND_GET_FM_PRESETS		0x1e
#define COMMAND_SET_FM_PRESETS		0x1f
#define COMMAND_START_DOWNLOAD		0x25
#define COMMAND_START_UPLOAD		0x28
#define COMMAND_FILE_ACK		0xf0

#define FILE_OTHER  0
#define FILE_MP3    1
#define FILE_NVF    2

#define FORMAT_OTHER 0
#define FORMAT_MP3   1

#define OFFSET_MODEL		48
#define OFFSET_OWNER		0
#define OFFSET_FILE_COUNT	1
#define OFFSET_FILE_NAME	513
#define OFFSET_FILE_SIZE	524
#define OFFSET_MEMORY		1	/* total/free */
#define	OFFSET_CLOCK		1	/* yy/mm/dd wd hh:mm:ss */
#define	OFFSET_RADIO		1

#define	FM_PRESETS		32
#define	NOMAD_NFILES		64
#define	NOMAD_NAMELEN		256

int fd0, fd1, fd2;

int  ugen_open(const char *, int);
void ugen_timeout(int, int);
int  ugen_ioctl(int, int, int, int, int, void *);

void usage(void) __attribute__((__noreturn__));
void parse_idx(const char *, int *, int *);

void nomad_memory(u_int *, u_int *, u_int *, u_int *);
void nomad_lsfile(int, int, char *, char *, u_long *);
void nomad_download(int, int);
void nomad_delete(int, int);
void nomad_format(int);
void nomad_upload(int, int, const char *);

int
main(argc, argv)
	int argc;
	char *argv[];
{
	char *p, *settime, *setowner, *setradio, *dev;
	char *delete, *readit;
	int ch, timeout;
	int clock, format, info, list, memory, nolabel, owner, radio, writeit;

	dev = USBDEV_DEFAULT;
	timeout = NOMAD_TIMEOUT;
	delete = readit = setradio = setowner = settime = NULL;
	clock = info = list = memory = nolabel =
	owner = radio = writeit = 0;
	format = -1;
	while ((ch = getopt(argc, argv, "cC:d:F:f:g:ilmnoO:rR:t:w")) != -1) {
		switch (ch) {
		case 'C':
			settime = optarg;
			break;
		case 'c':
			clock++;
			break;
		case 'd':
			delete = optarg;
			break;
		case 'F':
			if (optarg[1] != '\0' ||
			    (optarg[0] != '0' && optarg[0] != '1'))
				usage();
			format = *optarg - '0';
			break;
		case 'f':
			dev = optarg;
			if (!dev)
				usage();
			break;
		case 'i':
			info++;
			break;
		case 'l':
			list++;
			break;
		case 'm':
			memory++;
			break;
		case 'n':
			nolabel++;
			break;
		case 'O':
			setowner = optarg;
			break;
		case 'o':
			owner++;
			break;
		case 'R':
			setradio = optarg;
			break;
		case 'r':
			radio++;
			break;
		case 't':
			timeout = strtoul(optarg, &p, 0);
			if (*optarg == '\0' || *p != '\0')
				errx(1, "bad timeout value");
			break;
		case 'g':
			readit = optarg;
			break;
		case 'w':
			writeit++;
			break;
		default:
			usage();
		}

		if (writeit)
			break;
	}

	argc -= optind;
	argv += optind;

	/* open device */
	fd0 = ugen_open(dev, 0);
	fd1 = ugen_open(dev, 1);
	fd2 = ugen_open(dev, 2);
	ugen_timeout(fd2, timeout);

	if (info) {
		u_char buffer[0x74];

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_MODEL,
		    0, 0, 0, NULL))
			err(1, "COMMAND_MODEL");

		errno = 0;
		if (read(fd2, buffer, 0x74) != 0x74) {
			if (errno)
				err(1, "read");
			else
				errx(1, "COMMAND_MODEL: short read");
		}

		printf("%s%s, v%d.%02d.%02d\n", nolabel? "" : "Model:\t",
		    buffer + OFFSET_MODEL, buffer[1], buffer[3], buffer[5]);
	}

	if (memory) {
		u_int itot, iuse, etot, euse;

		nomad_memory(&itot, &iuse, &etot, &euse);

		iuse = itot - iuse;
		printf("%s%u.%uM/%uM",
		    nolabel? "" : "Memory:\t",
		    iuse / 1024 / 1024, iuse / 1024 / 100 % 10,
		    itot / 1024 / 1024);
		if (etot) {
			euse = etot - euse;
			printf(", %u.%uM/%uM",
			    euse / 1024 / 1024, euse / 1024 / 100 % 10,
			    etot / 1024 / 1024);
		}
		putchar('\n');
	}

	if (setowner) {
		/* set owner string */
		u_char buf[64];

		if (strlen(setowner) >= sizeof(buf))
			errx(1, "owner string is too long");

		strcpy(buf, setowner);
		if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_SET_OWNER,
		    0, 0, 64, buf))
			err(1, "COMMAND_SET_CLOCK");

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_FILE_ACK,
		    0, 0, 1, buf))
			err(1, "COMMAND_FILE_ACK");
	}

	if (owner) {
		/* show owner */
		u_char buffer[0x40];

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_GET_OWNER,
		    0, 0, 0, NULL))
			err(1, "COMMAND_GET_OWNER");

		errno = 0;
		if (read(fd2, buffer, 0x40) != 0x40) {
			if (errno)
				err(1, "read");
			else
				errx(1, "COMMAND_GET_OWNER: short read");
		}

		printf("%s%s\n", nolabel? "" : "Owner:\t",
		    buffer + OFFSET_OWNER);
	}

	if (settime) {
		/* set clock */
		u_char buf[14];
		struct tm tm;

		if (!strcmp(settime, "now")) {
			time_t t;

			time(&t);
			bzero(&tm, sizeof(tm));
			localtime_r(&t, &tm);

		} else if (!strptime(settime, "%y/%m/%d %H:%M:%S", &tm))
			errx(1, "cannot parse time");

		USETW(&buf[ 0], tm.tm_year + 44);
		USETW(&buf[ 2], tm.tm_mon + 1);
		USETW(&buf[ 4], tm.tm_mday);
		USETW(&buf[ 6], tm.tm_wday);
		USETW(&buf[ 8], tm.tm_hour);
		USETW(&buf[10], tm.tm_min);
		USETW(&buf[12], tm.tm_sec);

		if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_SET_CLOCK,
		    0, 0, 14, buf))
			err(1, "COMMAND_SET_CLOCK");
	}

	if (clock) {
		/* show clock */
		u_char buf[0x11];

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_GET_CLOCK,
		    0, 0, 0x11, buf))
			err(1, "COMMAND_GET_CLOCK");

		printf("%s%02d/%02d/%02d %d %02d:%02d:%02d\n",
		    nolabel? "" : "Clock:\t",
		    UGETW(&buf[OFFSET_CLOCK + 0]) % 100,
		    UGETW(&buf[OFFSET_CLOCK + 2]),
		    UGETW(&buf[OFFSET_CLOCK + 4]),
		    UGETW(&buf[OFFSET_CLOCK + 6]),
		    UGETW(&buf[OFFSET_CLOCK + 8]),
		    UGETW(&buf[OFFSET_CLOCK + 10]),
		    UGETW(&buf[OFFSET_CLOCK + 12]));
	}

	if (setradio) {
		u_char buf[65];
		int i, n, f;

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_GET_FM_PRESETS,
		    0, 0, 65, buf))
			err(1, "COMMAND_GET_FM_PRESETS");

		if (sscanf(setradio, "%d:%d.%d", &i, &n, &f) != 3)
			errx(1, "invalid radio preset specification");

		if (i < 0 || i >= FM_PRESETS)
			errx(1, "%d: not valid as preset index", i);
		if (n < 0 || f < 0)
			errx(1, "negative radio frequency");
		if (n < 60 || n > 150 || f >= 100)
			errx(1, "%d.%02d: invalid frequency", n, f);

		USETW(&buf[OFFSET_RADIO + i * 2], (n * 100 + f));

		if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_SET_FM_PRESETS,
		    0, 0, 64, buf + OFFSET_RADIO))
			err(1, "COMMAND_SET_FM_PRESETS");
	}

	if (radio) {
		u_char buf[65];
		int i, n;

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_GET_FM_PRESETS,
		    0, 0, 65, buf))
			err(1, "COMMAND_GET_FM_PRESETS");

		for (i = 0; i < FM_PRESETS; i++) {
			n = UGETW(&buf[OFFSET_RADIO + i * 2]);
			printf("%2d: %3d.%02d MHz%s", i, n / 100, n % 100,
			    (i && !((i + 1) % 4))? "\n" : "   ");
		}
	}

	if (list) {
		u_long size;
		char sname[12], name[NOMAD_NAMELEN];
		int i;

		for (i = 0; i < NOMAD_NFILES; i++) {
			nomad_lsfile(0, i, name, sname, &size);
			if (!*name)
				break;
			sname[11] = '\0';
			printf("%2d: %8lu\t%11s\t%s\n", i, size,
			    sname, name);
		}

		for (i = 0; i < NOMAD_NFILES; i++) {
			nomad_lsfile(1, i, name, sname, &size);
			if (!*name)
				break;
			sname[11] = '\0';
			printf("%2d: %8lu\t%11s\t%s\n", i, size,
			    sname, name);
		}
	}

	if (readit) {
		/* download a file */
		int n, i;

		parse_idx(readit, &n, &i);
		nomad_download(n, i);
	}

	if (format != -1)
		nomad_format(format);
	else if (delete) {
		/* delete a file */
		int n, i;

		parse_idx(delete, &n, &i);
		nomad_delete(n, i);
	}

	if (writeit) {
		/* upload a file */
		u_long n;
		char *p, name[NOMAD_NAMELEN];
		int i;

		if (argc < 2)
			errx(1, "not enough arguments for upload");

		errno = 0;
		n = strtoul(*argv, &p, 0);
		if (**argv == '\0' || *p != '\0' ||
		    (errno == ERANGE && n == ULONG_MAX))
			errx(1, "invalid volume index");

		if (n > 1)
			errx(1, "illegal volume index");

		for (i = 0; i < NOMAD_NFILES; i++) {
			nomad_lsfile(n, i, name, NULL, NULL);
			if (!*name)
				break;
		}

		for (argv++, argc--; argc; argc--, argv++)
			nomad_upload((int)n, i++, *argv);
	}

	close(fd2);
	close(fd1);
	close(fd0);

	return (0);
}

void
usage()
{
	extern char *__progname;
	fprintf(stderr,
	    "usage: %s [-f <dev>] [-cilmnorV] [-R <num>:<num>.<num>]\n"
	    "               [-F <num>] [-O <owner>] [-C yy/mm/dd hh:mm:ss]\n"
	    "       %s [-f <dev>] [-cilmnorV] -d <num>[:<num>]\n"
	    "       %s [-f <dev>] [-cilmnorV] -g <num>[:<num>]\n"
	    "       %s [-f <dev>] [-cilmnorV] -w <num> <file> <file> ...\n",
	    __progname, __progname, __progname, __progname);
	exit(1);
}

int
ugen_open(dev, addr)
	const char *dev;
	int addr;
{
	char name[FILENAME_MAX];
	int fd;

	snprintf(name, sizeof(name), "%s.%02d", dev, addr);

	fd = open(name, O_RDWR);
	if (fd < 0)
		err(1, "%s", name);

	return (fd);
}

void
ugen_timeout(fd, timeout)
	int fd, timeout;
{
	if (ioctl(fd, USB_SET_TIMEOUT, &timeout))
		err(1, "USB_SET_TIMEOUT");
}

int
ugen_ioctl(rt, req, val, idx, len, buf)
	int rt, req, val, idx, len;
	void *buf;
{
	struct usb_ctl_request ur;

	ur.ucr_addr = 0;
	ur.ucr_request.bmRequestType = rt;
	ur.ucr_request.bRequest = req;
	USETW(ur.ucr_request.wValue, val);
	USETW(ur.ucr_request.wIndex, idx);
	USETW(ur.ucr_request.wLength, len);
	ur.ucr_data = buf;
	ur.ucr_flags = 0;
	ur.ucr_actlen = 0;

	usleep(400);
	return (ioctl(fd0, USB_DO_REQUEST, &ur));
}

void
parse_idx(p, n, i)
	const char *p;
	int *n, *i;
{
	char *q;
	u_long ulval;

	if (*p == '\0')
		errx(1, "empty index specification");
	errno = 0;
	ulval = strtoul(p, &q, 0);
	if (errno == ERANGE && ulval == ULONG_MAX)
		err(1, "parse index");
	if (*q == ':') {
		*n = (int)ulval;
		ulval = strtoul(q + 1, &q, 0);
		errno = 0;
		if (errno == ERANGE && ulval == ULONG_MAX)
			err(1, "parse index");
	} else
		*n = 0;
	*i = (int)ulval;

	if (*q != '\0')
		errx(1, "invalid index specification");

	if (*n < 0 || *i < 0)
		errx(1, "illegal index specification");
}

void
nomad_memory(itot, ifree, etot, efree)
	u_int *itot, *ifree, *etot, *efree;
{
	u_char buf[0x11];

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_MEMORY,
	    0, 0, 0x11, buf))
		err(1, "COMMAND_MEMORY");

	if (itot)
		*itot  = UGETDW(&buf[OFFSET_MEMORY + 0]);
	if (ifree)
		*ifree = UGETDW(&buf[OFFSET_MEMORY + 4]);
	if (etot)
		*etot  = UGETDW(&buf[OFFSET_MEMORY + 8]);
	if (efree)
		*efree = UGETDW(&buf[OFFSET_MEMORY + 12]);
}

void
nomad_lsfile(bank, n, name, sname, size)
	int bank, n;
	char *name, *sname;
	u_long *size;
{
	u_char buf[0x215];
	u_short *p = (u_short *)&buf[1];
	int i, nbuf;

	buf[0] = buf[1] = 0;
	if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_FILE_INFO,
	    bank, n, 2, buf))
		err(1, "COMMAND_FILE_INFO");

	errno = 0;
	if ((nbuf = read(fd2, buf, 0x215)) != 0x215) {
		if (errno)
			err(1, "read");
		else
			errx(1, "COMMAND_FILE_INFO: short read 0x%x", nbuf);
	}

	if (size)
		*size = UGETDW(&buf[OFFSET_FILE_SIZE]);

	if (sname)
		memcpy(sname, &buf[OFFSET_FILE_NAME], 11);

	if (name) {
		char *q;
		for (q = name, i = 0; i < NOMAD_NAMELEN; i++, p++) {
			u_short c;
			c = UGETW(p);
			if (c == 0)
				break;
			*q++ = c;
		}
		*q = '\0';
	}
}

void
nomad_format(bank)
	int bank;
{
	u_char buf[4];

	printf("Formatting %s memory card (yes/no): ",
	    bank? "external" : "internal");
	fflush(stdout);
	if (fgets(buf, sizeof(buf), stdin) == NULL ||
	    strcasecmp(buf, "yes"))
		return;

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_FORMAT,
	    0x200 | bank, 0, 1, buf))
		err(1, "COMMAND_FORMAT");

	printf("Complete.\n");
}

void
nomad_delete(n, i)
	int n, i;
{
	u_char buf[12];

	/* get size and "short" name */
	nomad_lsfile(n, i, NULL, buf, NULL);

	if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_DELETE_FILE,
	    n, 0, 0, NULL))
		err(1, "COMMAND_DELETE_FILE");

	if (write(fd2, buf, 11) != 11)
		err(1, "write: COMMAND_DELETE_FILE");
}

void
nomad_download(n, i)
	int n, i;
{
	char name[256];
	u_char buf[NOMAD_BLKSIZE];
	u_long size, off;
	int fd, len;

	/* get size and "short" name */
	nomad_lsfile(n, i, name, buf + 12, &size);

	bzero(buf, 12);
	if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_SELECT_FILE,
	    0x100 | n, 0, 12, buf))
		err(1, "COMMAND_SELECT_FILE");

	if (write(fd2, buf + 12, 11) != 11)
		err(1, "write: COMMAND_SELECT_FILE");

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_FILE_ACK,
	    0, 0, 1, buf))
		err(1, "COMMAND_FILE_ACK");

	if ((fd = open(name, O_WRONLY|O_CREAT, 0644)) < 0 )
		err(1, "open: %s", name);

	/* TODO progress bar */
	for (off = 0; size; size -= len - 64, off += len - 64) {

		len = NOMAD_BLKSIZE;
		if (len - 64 > size)
			len = size + 64;

		USETDW(&buf[0], off);
		USETDW(&buf[4], len - 64);

		if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_RECV_BLOCK,
		    0, 0, 8, buf))
			err(1, "COMMAND_RECV_BLOCK");

		if (read(fd2, buf, len) != len)
			err(1, "read");

		if (write(fd, buf + 64, len - 64) != (len - 64))
			err(1, "write");
	}

	if (close(fd))
		err(1, "close");

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_END_FILEIO,
	    0, 0, 1, buf))
		err(1, "COMMAND_END_FILEIO");
}

void
nomad_upload(n, i, name)
	const char *name;
	int n, i;
{
	struct stat sb;
	u_char buf[NOMAD_BLKSIZE], buf2[16], *p;
	const char *q;
	u_long size;
	u_int freemem;
	int fd, len;

	if ((len = strlen(name)) > NOMAD_NAMELEN)
		errx(1, "name too long");

	if (stat(name, &sb))
		err(1, "stat: %s", name);
	size = sb.st_size;

	if (n) {
		nomad_memory(NULL, NULL, NULL, &freemem);
		if (freemem < NOMAD_ROUND(size))
			errx(1, "not enough space on the card");
	} else {
		nomad_memory(NULL, &freemem, NULL, NULL);
		if (freemem < NOMAD_ROUND(size))
			errx(1, "not enough space on the internal");
	}

	if ((fd = open(name, O_RDONLY)) < 0)
		err(1, "open: %s", name);

	bzero(buf, sizeof(buf));

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_START_UPLOAD,
	    0, 0, 18, buf))
		err(1, "COMMAND_START_UPLOAD");

	p = strrchr(name, '.');
	if (p == NULL || strlen(p) > 4)
		p = "MP3";
	else {
		p++;
		p[0] = toupper(p[0]);
		p[1] = toupper(p[1]);
		p[2] = toupper(p[2]);
	}
	sprintf(buf + 12, "IO%02d~%03d%s", n, i, p);
	q = strrchr(name, '/');
	if (!q)
		q = name;
	else
		q++;
	for (len = 0, p = buf + 12 + 11; *q; q++, p += 2, len += 2)
		USETW(p, *q);

	memcpy(buf, "\0\0\0\0\xf3\x30\x9f\x29\0\0\0\0", 12);
	USETW(&buf[2], len);
	USETDW(&buf[8], size);

	if (ugen_ioctl(UT_WRITE_VENDOR_DEVICE, COMMAND_SELECT_FILE,
	    n, 0, 12, buf))
		err(1, "COMMAND_SELECT_FILE");

	len += 11;
	if (write(fd2, buf + 12, len) != len)
		err(1, "write: COMMAND_SELECT_FILE");

	for (; size; size -= len) {

		len = NOMAD_BLKSIZE;
		if (len > size)
			len = size;

		if ((len = read(fd, buf, len)) < 0)
			err(1, "read: %s", name);

		if (len == 0)
			break;

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_FILE_ACK,
		    0, 0, 1, buf))
			err(1, "COMMAND_FILE_ACK");

		if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_SEND_BLOCK,
		    len, 0, 1, buf2))
			err(1, "COMMAND_SEND_BLOCK");

		if (write(fd2, buf, len) != len)
			err(1, "write: COMMAND_SEND_BLOCK");
	}

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_END_FILEIO,
	    0, 0, 1, buf))
		err(1, "COMMAND_END_FILEIO");

	if (ugen_ioctl(UT_READ_VENDOR_DEVICE, COMMAND_END_UPLOAD,
	    0, 0, 1, buf))
		err(1, "COMMAND_END_UPLOAD");
}
