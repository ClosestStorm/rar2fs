/*
    Copyright (C) 2009-2011 Hans Beckerus (hans.beckerus@gmail.com)

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    This program take use of the freeware "Unrar C++ Library" (libunrar)
    by Alexander Roshal and some extensions to it.

    Unrar source may be used in any software to handle RAR archives
    without limitations free of charge, but cannot be used to re-create
    the RAR compression algorithm, which is proprietary. Distribution
    of modified Unrar source in separate form or as a part of other
    software is permitted, provided that it is clearly stated in
    the documentation and source comments that the code may not be used
    to develop a RAR (WinRAR) compatible archiver.
*/

#include <signal.h>
#include <syslog.h>
#include <pthread.h>
#include <sys/wait.h>
#include "common.h"
#include "filecache.h"
#include "configdb.h"

#if defined ( __UCLIBC__ ) || !defined ( __linux ) || !defined ( __i386 )
#define stack_trace(a,b,c)
#else
#include <execinfo.h>

/* get REG_EIP from ucontext.h */
#include <ucontext.h>

/*!
 *****************************************************************************
 *
 ****************************************************************************/
static void
stack_trace(int sig, siginfo_t *info, void *secret)
{
  ucontext_t *uc = (ucontext_t *)secret;

  /* Do something useful with siginfo_t */
  char buf[256];
  snprintf(buf, sizeof(buf), "Got signal %d, faulty address is 0x%p, "
     "from 0x%p", sig, info->si_addr,
     (void*)uc->uc_mcontext.gregs[REG_EIP]);
  printf("%s\n", buf);
  syslog(LOG_INFO, "%s", buf);
#if 1
  {
     void *trace[30];
     char **messages = (char **)NULL;
     int i, trace_size = 0;

     trace_size = backtrace(trace, 30);
     /* overwrite sigaction with caller's address */
     trace[1] = (void *) uc->uc_mcontext.gregs[REG_EIP];
     messages = backtrace_symbols(trace, trace_size);
     if (messages)
     {
        /* skip first stack frame (points here) */
        for (i=1; i<trace_size; ++i)
        {
           printf("%s\n", messages[i]);
           syslog(LOG_INFO, "%s", messages[i]);
        }
        free(messages);
     }
  }
#else
  void *pc0 = __builtin_return_address(0);
  void *pc1 = __builtin_return_address(1);
  void *pc2 = __builtin_return_address(2);
  void *pc3 = __builtin_return_address(3);

  printf("Frame 0: PC=%p\n", pc0);
  printf("Frame 1: PC=%p\n", pc1);
  printf("Frame 2: PC=%p\n", pc2);
  printf("Frame 3: PC=%p\n", pc3);
#endif
}
#endif


/*!
 *****************************************************************************
 *
 ****************************************************************************/

int glibc_test = 0;

static void
sig_handler(int signum, siginfo_t *info, void* secret)
{
   switch(signum)
   {
   case SIGUSR1:
   {
      printd(4, "Caught signal SIGUSR1\n");
      printd(3, "Invalidating path cache\n");
      pthread_mutex_lock(&file_access_mutex);
      inval_cache_path(NULL);
      pthread_mutex_unlock(&file_access_mutex);
   }
   break;
   case SIGSEGV:
   {
      if (!glibc_test)
      {
         printd(4, "Caught signal SIGSEGV\n");
         stack_trace(SIGSEGV, info, secret);
      }
      else
      {
         printf("glibc validation failed\n");
      }
      exit(EXIT_FAILURE);
   }
   break;
   case SIGCHLD:
   {
      printd(4, "Caught signal SIGCHLD\n");
   }
   break;
   }
}

/*!
 *****************************************************************************
 *
 ****************************************************************************/

#if defined ( __APPLE__ ) || defined ( __FreeBSD__ )
/* This is such a big mess! Simply cast to void* to walk away from it. */
#define SIG_FUNC_ (void*)
#elif defined (__sun__)
#define SIG_FUNC_
#else
#define SIG_FUNC_ (__sighandler_t)
#endif

void
sighandler_init()
{
   struct sigaction act;

   if (OBJ_SET(OBJ_UNRAR_PATH))
   {
       /* Avoid child zombies for SIGCHLD */
       sigaction(SIGCHLD, NULL, &act);
       act.sa_handler = SIG_FUNC_ sig_handler;
       act.sa_flags |= SA_NOCLDWAIT;
       sigaction(SIGCHLD, &act, NULL);
   }

   sigaction(SIGUSR1, NULL, &act);
   sigemptyset(&act.sa_mask);
   act.sa_handler = SIG_FUNC_ sig_handler;
   /* make sure a system call is restarted to avoid exit */
   act.sa_flags = SA_RESTART | SA_SIGINFO;
   sigaction(SIGUSR1, &act, NULL);

   sigaction(SIGSEGV, NULL, &act);
   sigemptyset(&act.sa_mask);
   act.sa_handler = SIG_FUNC_ sig_handler;
   act.sa_flags = SA_RESTART | SA_SIGINFO;
   sigaction(SIGSEGV, &act, NULL);
}

void
sighandler_destroy()
{
}

#undef SIG_FUNC_

