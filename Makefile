
dump_sys_proc: dump_sys_proc.c errno.c
	gcc -g -Wall -o dump_sys_proc dump_sys_proc.c errno.c

clean:
	rm dump_sys_proc out

