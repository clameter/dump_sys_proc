
dump_sys_proc: dump_sys_proc.c errno.c
	gcc -g -Wall -D_DEFAULT_SOURCE -std=c17 -o dump_sys_proc dump_sys_proc.c errno.c

clean:
	rm -f dump_sys_proc out

