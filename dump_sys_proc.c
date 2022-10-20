#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <getopt.h>

#define VERSION "2022.1020"

#include "errno.h"

char filename[255] = "out";
FILE *out;

bool to_stdout = false;
bool recursive = true;

__attribute__ ((format (printf, 1, 2)))
void panic(const char *fmt, ...)
{
	va_list valist;

	va_start(valist, fmt);

	vfprintf(stderr, fmt, valist);
	exit(7);
}

/*
 * Process command line options
 */

#define MAX_OPTS 30

/* What is passed to getopt_long */
static struct option opts[MAX_OPTS];

int nr_opts;

struct opts_data {
	void (*callback)(char *optarg);
	const char *description;
	const char *pardesc;
	struct option *opt;
} opts_datas[128];

void register_option(const char  *name, int has_arg, const char x, void (*callback)(char *optarg),
	const char *pardesc, const char *description)
{
	struct option *o;
	struct opts_data *od = opts_datas + x;

	if (x <= 0 || od->description)
		panic("Cannot add command line option '%c' = %d\n",x, x);

	o = opts + nr_opts;

	o->name = name;
	o->has_arg = has_arg;
	o->flag = NULL;
	o->val = x;
	od->callback = callback;
	od->description = description;
	od->pardesc = pardesc;
	od->opt = o;

	nr_opts++;
}

static void help_opt(char *);

void parse_options(int argc, char **argv)
{
	char opt_string[300];
	char *p;
	int op;
	int i;

	/* Compose opt_string from opts */
	p = opt_string;
	for(i = 0; i < 128; i++) {
		struct opts_data *od = opts_datas + i;
		struct option *o = od->opt;

		if (!od->description)
			continue;

		*p++ = i;
		if (o->has_arg != no_argument)
			*p++ = ':';
		if (o->has_arg == optional_argument)
			*p++ = ':';

		*p = 0;
	}

	while ((op = getopt_long(argc, argv, opt_string,
					opts, NULL)) != -1) {
//		if (!optarg && argv[optind] && argv[optind][0] != '-') {
//			optarg = argv[optind];
//			optind++;
//		}
		if (op != '?' && opts_datas[op].callback)

			opts_datas[op].callback(optarg);
		else
			help_opt(NULL);
	}
}

static void help_opt(char *optarg)
{
	int i;

	printf("dump_sys_proc " VERSION " Christoph Lameter <cl@linux.com>\n");

	printf("Outputs to a file that is created in the current directory unless the -c option is given.\n");
	printf("Dumps /sys and /proc contents unless a list of directories is specified.\n\n");

	printf("Usage: dump_sys_proc [<options>] [<directory1>] ... \n");

	for(i = 0; i < 128; i++) {
		struct opts_data *od = opts_datas + i;
		struct option *o = od->opt;
		char buffer[60];

		if (!od->description)
			continue;

		snprintf(buffer, sizeof(buffer), "-%c|--%s %s ", i, o->name, od->pardesc? od->pardesc : " ");
		printf("%-50s %s\n", buffer, od->description);
	}
	exit(1);
}

static void console_opt(char *optarg)
{
	to_stdout = true;
}

static void norecurse_opt(char *optarg)
{
	recursive = false;
}

__attribute__((constructor))
static void opts_init(void)
{
	register_option("console", no_argument, 'c', console_opt, NULL, "Output to stdout");
	register_option("help", no_argument, 'h', help_opt, NULL, "Show these instructions");
	register_option("norecursion", no_argument, 'n', norecurse_opt, NULL, "Do not recurse into directories");
}

/* Main Code */

static int special(char *p, int n)
{
	char *q;

	for(q = p; q < p + n; q++)
		if ((*q < ' ' && *q != '\t') || *q > 126) {
//			printf("Special char %d at %ld\n", *q, q - p);
			return 1;
		}

	return 0;
}

static void dumpfs(const char *path)
{
	struct dirent *e;
	DIR *d;
	int fh;
	int n;

	d = opendir(path);
	if (!d) {
	fprintf(out, "D %s %s\n", path, errname());
		return;
	}
	while ((e = readdir(d))) {
		char file[4096];
		char buf[4096];
		struct stat sb;
		unsigned mode;

		snprintf(file, sizeof(file), "%s/%s", path, e->d_name);

		if (lstat(file, &sb) < 0) {
			fprintf(out, "? %s %s\n", file, errname());
			continue;
		};

		mode = sb.st_mode & 07777;

		switch (e->d_type) {
			case DT_BLK :
				fprintf(out, "B %s %04o %d %d\n", file, mode,
				       major(sb.st_dev), minor(sb.st_dev)); 
				break;

			case DT_CHR :
				fprintf(out, "C %s %04o %d %d\n", file, mode,
				       major(sb.st_dev), minor(sb.st_dev)); 
				break;

			case DT_FIFO :
				fprintf(out, "F %s %04o\n", file, mode);
				break;

			case DT_LNK :
				n = readlink(file, buf, sizeof(buf) - 1);
				if (n < 0)
					fprintf(out, "L %s %s\n", file, errname());
				else {
					buf[n] = 0;
					fprintf(out, "L %s %s\n", file, buf);
				}
				break;

			case DT_REG :
				fh = open(file, O_RDONLY | O_NDELAY);
				if (fh < 0)
					fprintf(out, "F %s %04o %ld %s\n", file, mode,
						sb.st_size, errname());
				else {
					int n;

					n = read(fh, buf, sizeof(buf) - 1);
					if (n < 0) {
						fprintf(out, "F %s %04o %ld read %s\n", file, sb.st_mode,
							sb.st_size, errname());

					} else {
						char *p = buf;

						while (n > 0 && (buf[n - 1] == '\n' || buf[n-1] == ' '))
							n--;

						buf[n] = 0;

						while (n > 0 && *p == '\n') {
							p++;
							n--;
						}

						fprintf(out, "F %s %04o %ld = ", file, mode,
							       sb.st_size ? sb.st_size : n);

						if (n < 100 && !special(p, n))
							fprintf(out, "\"%s\"\n", p);
						else
							fprintf(out, "<<---\n%s\n---\n", p);		
					}
				}
				close(fh);
				break;

			case DT_SOCK :
				fprintf(out, "S %s %04o %ld\n", file, mode, sb.st_dev); 
				break;

			case DT_UNKNOWN:
				fprintf(out, "U %s %04o\n", file, mode);
				break;

			case DT_DIR :
				if (strcmp(".", e->d_name) == 0 || strcmp("..", e->d_name) == 0)
					continue;

				if (recursive)
					dumpfs(file);
				else
					fprintf(out, "D %s\n", file);
				break;
		}
	}
	closedir(d);
}

int main(int argc, char *argv[])
{
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);
	char hostname[20];

	gethostname(hostname, sizeof(hostname));
	snprintf(filename, sizeof(filename), "dump_sys_proc-%s-%04d%02d%02d%02d%02d%02d",
			hostname, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);

	parse_options(argc, argv);

	if (!to_stdout) {
		out = fopen(filename, "w");
		if (!out) {
			perror("fopen");
			exit(1);
		}
	} else
		out = stdout;

	if (argc == optind) {

		dumpfs("/proc");
		dumpfs("/sys");

	} else {
		int i;

		for(i = optind; i < argc; i++)
			dumpfs(argv[i]);
	}

	fclose(out);
	if (!to_stdout) {
		int i;
		printf("Dumped contents of");

		if (argc == optind)
			printf(" /proc /sys");
		else {
			for(i = optind; i < argc; i++) {
				putchar(' ');
				fputs(argv[i], stdout);
			}
		}

		printf(" to %s\n", filename);

	}
	return 0;
}

