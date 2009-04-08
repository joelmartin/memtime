/* -*- mode: C; c-file-style: "k&r"; -*-
 *---------------------------------------------------------------------------*
 *
 * Copyright (c) 2000, Johan Bengtsson
 * Copyright (c) 2009, Michael Weber
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 *---------------------------------------------------------------------------*/

#define _USE_BSD

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <signal.h>

#include <errno.h>

#include "machdep.h"

void
usage (FILE *ffile)
{
     fprintf (ffile,
              "Usage:\nmemtime [-t <interval>] [-e] [-m <maxkilobytes>] "
              "[-c <maxcpuseconds>] <cmd> [<params>]\n");
}

int
main (int argc, char *argv[])
{
     struct rusage kid_usage;
     pid_t  kid;
     int    kid_status;
     int    i, opt, echo_args = 0, exit_flag;
     long int sample_time=0, time = 0;
	 
	 unsigned long maxkbytes=0; //kilobytes
	 unsigned long maxseconds=0; //seconds
	 long int maxmillis=0;

     unsigned long max_vsize = 0, max_rss = 0;
     unsigned long start, end;

     memtime_info_t info;

     if (argc < 2) {
          usage(stderr);          
	  exit(EXIT_FAILURE);
     }

     while ((opt = getopt(argc, argv, "+eht:m:c:")) != -1) {

	  switch (opt) {              
	  case 'e' : 
	       echo_args = 1;
	       break;

	  case 't' :
	       errno = 0;
	       sample_time = strtol(optarg, NULL, 0);
	       if (errno) {
		    perror("Illegal argument to t option");
		    exit(EXIT_FAILURE);
	       }
	       break;
	  case 'm' : 
	       errno = 0;
	       maxkbytes = atol(optarg);
	       if (errno) {
		    perror("Illegal argument to m option");
		    exit(EXIT_FAILURE);
	       }
	       break;

	  case 'c' : 
	       errno = 0;
	       maxseconds = atol(optarg);
	       if (errno) {
		    perror("Illegal argument to c option");
		    exit(EXIT_FAILURE);
	       }
		   maxmillis=1000*maxseconds;
	       break;
          case 'h':
               usage (stdout);
               exit(EXIT_SUCCESS);

          default:
               exit(EXIT_FAILURE);
	  }
     }

     if (echo_args) {
	  fprintf(stderr,"Command line: ");
	  for (i = optind; i < argc; i++)
	       fprintf(stderr,"%s ", argv[i]);
	  fprintf(stderr,"\n");
     }

     start = get_time();
    
     switch (kid = sampling_fork()) {
	
     case -1 :
	  perror("sampling_fork failed");
	  exit(EXIT_FAILURE);
	
     case 0 :	
#if defined(CAN_USE_RLIMIT_RSS)	  
	  if (maxkbytes>0) {
	       set_mem_limit((long int)maxkbytes*1024);
	  }
#endif
#if defined(CAN_USE_RLIMIT_CPU)	  
	  if (maxseconds>0) {
	       set_cpu_limit((long int)maxseconds);
	  }
#endif
	  execvp(argv[optind], &(argv[optind]));
	  perror("exec failed");
	  exit(EXIT_FAILURE);
	
     default :
	  break;
     }

     do {

	  get_sample(&info);

	  max_vsize = (info.vsize_kb > max_vsize ? info.vsize_kb : max_vsize);
	  max_rss = (info.rss_kb > max_rss ? info.rss_kb : max_rss);
	  
	  if (sample_time) {
	       time++;
	       if (time == 10 * sample_time) {
		    end = get_time();
		    
		    fprintf(stderr,"%.2f user, %.2f system, %.2f elapsed"
			    " -- VSize = %luKB, RSS = %luKB\n",
			    (double)info.utime_ms/1000.0,
			    (double)info.stime_ms/1000.0,
			    (double)(end - start)/1000.0,
			    info.vsize_kb, info.rss_kb);
		    fflush(stdout);
		    
		    time = 1;
	       }
	  }

	  usleep(100000);

	  exit_flag = ((wait4(kid, &kid_status, WNOHANG, &kid_usage) == kid)
		       && (WIFEXITED(kid_status) || WIFSIGNALED(kid_status)));
#if !defined(CAN_USE_RLIMIT_RSS)	  
	  if ((maxkbytes>0) && (max_vsize>maxkbytes)) {
	  	kill(kid,SIGKILL);
	  }
#endif
#if !defined(CAN_USE_RLIMIT_CPU)	  
	  if ((maxmillis>0) && (info.utime_ms>maxmillis)) {
	  	kill(kid,SIGKILL);
	  }
#endif	  
     } while (!exit_flag);
     
     end = get_time();
     
     if (WIFEXITED(kid_status)) {
	  fprintf(stderr, "Exit [%d]\n", WEXITSTATUS(kid_status));
     } else {
	  fprintf(stderr, "Killed [%d]\n", WTERMSIG(kid_status));
     }

     {
	  double kid_utime = ((double)kid_usage.ru_utime.tv_sec 
			      + (double)kid_usage.ru_utime.tv_usec / 1E6);
	  double kid_stime = ((double)kid_usage.ru_stime.tv_sec 
			      + (double)kid_usage.ru_stime.tv_usec / 1E6);

	  fprintf(stderr, "%.2f user, %.2f system, %.2f elapsed -- "
		  "Max VSize = %luKB, Max RSS = %luKB\n", 
		  kid_utime, kid_stime, (double)(end - start) / 1000.0,
		  max_vsize, max_rss);
     }

     if(WIFEXITED(kid_status))
	  exit(WEXITSTATUS(kid_status));
     else {
          int csig = WTERMSIG(kid_status);
          switch (csig) {
          case SIGHUP: case SIGINT: case SIGUSR1: case SIGUSR2:
          case SIGKILL: case SIGALRM: case SIGTERM: case SIGPIPE:
               kill (getpid(), csig);
          }
          exit (1);
     }
}
