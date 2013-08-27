#
# iniparser tests Makefile
#

CC      = gcc
CFLAGS  = -g -I./iniparser/src
LFLAGS  = -L./iniparser -liniparser
AR	    = ar
ARFLAGS = rcv
RM      = rm -f


default: all

all: term config
#thread
#demex

term: term_no_den.c
	$(MAKE) -C iniparser
	$(CC) $(CFLAGS) -o term term_no_den.c -I./iniparser/src -I./ -L./iniparser -liniparser -lpthread 
	#-lwiringPi

test2: test2.c
	$(MAKE) -C iniparser
	$(CC) $(CFLAGS) -o test2 test2.c 
	#-lwiringPi

config: testf.c
	$(MAKE) -C iniparser
	$(CC) $(CFLAGS) -o cre_conf testf.c -I./iniparser/src -I./ -L./iniparser -liniparser  -lpthread
	
demex: dem_ex.c
	$(MAKE) -C iniparser
	$(CC) $(CFLAGS) -o dem_ex dem_ex.c -I./iniparser/src -I./ -L./iniparser -liniparser -lpthread

thread: thread.c
	$(MAKE) -C iniparser
	$(CC) $(CFLAGS) -o thread thread.c -I./iniparser/src -I./ -L./iniparser -liniparser -lpthread

clean:
	$(MAKE) clean -C iniparser
	$(RM) term
	$(RM) cre_conf
#	$(RM) *.rrd

rebuild: clean all