#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <sys/sysmacros.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#include "errno.h"

FILE *out;

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
	chdir(path);
	while ((e = readdir(d))) {
		char buf[4096];
		struct stat sb;
		unsigned mode;
		
		lstat(e->d_name, &sb);
		snprintf(buf, sizeof(buf), "%s/%s", path, e->d_name);

		mode = sb.st_mode & 07777;

		switch (e->d_type) {
			case DT_BLK :
				fprintf(out, "B %s %04o %d %d\n", buf, mode,
				       major(sb.st_dev), minor(sb.st_dev)); 
				break;

			case DT_CHR :
				fprintf(out, "C %s %04o %d %d\n", buf, mode,
				       major(sb.st_dev), minor(sb.st_dev)); 
				break;

			case DT_FIFO :
				fprintf(out, "F %s %04o\n", buf, mode);
				break;

			case DT_LNK :
				n = readlink(e->d_name, buf, sizeof(buf) - 1);
				if (n < 0)
					fprintf(out, "L %s/%s %s\n", path, e->d_name, errname());
				else {
					buf[n] = 0;
					fprintf(out, "L %s/%s %s\n", path, e->d_name, buf);
				}
				break;

			case DT_REG :
				fh = open(e->d_name, O_RDONLY);
				if (fh < 0)
					fprintf(out, "F %s %04o %ld %s\n", buf, mode,
						sb.st_size, errname());
				else {
					int n;

					n = read(fh, buf, sizeof(buf) - 1);
					if (n < 0) {
						fprintf(out, "F %s/%s %04o %ld %s\n", path, e->d_name, sb.st_mode,
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

						fprintf(out, "F %s/%s %04o %ld = ", path, e->d_name, mode,
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
				fprintf(out, "S %s %04o %ld\n", buf, mode, sb.st_dev); 
				break;

			case DT_UNKNOWN:
				fprintf(out, "U %s %04o\n", buf, mode);
				break;

			case DT_DIR :
				if (strcmp(".", e->d_name) == 0 || strcmp("..", e->d_name) == 0)
					continue;
				dumpfs(buf);
				break;
		}
	}
	closedir(d);
}

int main(int argc, char *argv[])
{
	char filename[80] = "out";
	char hostname[20];
	time_t t = time(NULL);
	struct tm *tm = localtime(&t);

	if (argc == 1) {
		gethostname(hostname, sizeof(hostname));

		snprintf(filename, sizeof(filename), "dump_sys_proc-%s-%04d%02d%02d%02d%02d%02d",
			hostname, tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
			tm->tm_hour, tm->tm_min, tm->tm_sec);

		out = fopen(filename, "w");
		if (!out) {
			perror("fopen");
			exit(1);
		}
	} else
		out = stdout;

	dumpfs("/proc");
	dumpfs("/sys");

	fclose(out);
	return 0;
}

