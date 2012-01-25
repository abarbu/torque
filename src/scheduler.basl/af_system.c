/*
*         OpenPBS (Portable Batch System) v2.3 Software License
*
* Copyright (c) 1999-2000 Veridian Information Solutions, Inc.
* All rights reserved.
*
* ---------------------------------------------------------------------------
* For a license to use or redistribute the OpenPBS software under conditions
* other than those described below, or to purchase support for this software,
* please contact Veridian Systems, PBS Products Department ("Licensor") at:
*
*    www.OpenPBS.org  +1 650 967-4675                  sales@OpenPBS.org
*                        877 902-4PBS (US toll-free)
* ---------------------------------------------------------------------------
*
* This license covers use of the OpenPBS v2.3 software (the "Software") at
* your site or location, and, for certain users, redistribution of the
* Software to other sites and locations.  Use and redistribution of
* OpenPBS v2.3 in source and binary forms, with or without modification,
* are permitted provided that all of the following conditions are met.
* After December 31, 2001, only conditions 3-6 must be met:
*
* 1. Commercial and/or non-commercial use of the Software is permitted
*    provided a current software registration is on file at www.OpenPBS.org.
*    If use of this software contributes to a publication, product, or
*    service, proper attribution must be given; see www.OpenPBS.org/credit.html
*
* 2. Redistribution in any form is only permitted for non-commercial,
*    non-profit purposes.  There can be no charge for the Software or any
*    software incorporating the Software.  Further, there can be no
*    expectation of revenue generated as a consequence of redistributing
*    the Software.
*
* 3. Any Redistribution of source code must retain the above copyright notice
*    and the acknowledgment contained in paragraph 6, this list of conditions
*    and the disclaimer contained in paragraph 7.
*
* 4. Any Redistribution in binary form must reproduce the above copyright
*    notice and the acknowledgment contained in paragraph 6, this list of
*    conditions and the disclaimer contained in paragraph 7 in the
*    documentation and/or other materials provided with the distribution.
*
* 5. Redistributions in any form must be accompanied by information on how to
*    obtain complete source code for the OpenPBS software and any
*    modifications and/or additions to the OpenPBS software.  The source code
*    must either be included in the distribution or be available for no more
*    than the cost of distribution plus a nominal fee, and all modifications
*    and additions to the Software must be freely redistributable by any party
*    (including Licensor) without restriction.
*
* 6. All advertising materials mentioning features or use of the Software must
*    display the following acknowledgment:
*
*     "This product includes software developed by NASA Ames Research Center,
*     Lawrence Livermore National Laboratory, and Veridian Information
*     Solutions, Inc.
*     Visit www.OpenPBS.org for OpenPBS software support,
*     products, and information."
*
* 7. DISCLAIMER OF WARRANTY
*
* THIS SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, AND NON-INFRINGEMENT
* ARE EXPRESSLY DISCLAIMED.
*
* IN NO EVENT SHALL VERIDIAN CORPORATION, ITS AFFILIATED COMPANIES, OR THE
* U.S. GOVERNMENT OR ANY OF ITS AGENCIES BE LIABLE FOR ANY DIRECT OR INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*
* This license will be governed by the laws of the Commonwealth of Virginia,
* without reference to its choice of law rules.
*/

#include <pbs_config.h>   /* the master config generated by configure */

#include <sys/time.h>
#if (PLOCK_DAEMONS & 2)
#include <sys/lock.h>
#endif /* PLOCK_DAEMONS */
#include <sys/resource.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <malloc.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#ifdef _CRAY
#include        <sys/category.h>
#endif  /* _CRAY */
#include <time.h>
#include <ctype.h>

#include "libpbs.h"
#include "log.h"
#include "net_connect.h"
#include "sched_cmds.h"
#include "portability.h"
#include "af_system.h"
#include "af_config.h"
#include "af_server.h"
#include "af_que.h"
#include "af_cnodemap.h"
#include "af_resmom.h"
#include "rpp.h"
#include "dis.h"
/* Macros */
#ifndef OPEN_MAX
#ifdef  _POSIX_OPEN_MAX
#define OPEN_MAX        _POSIX_OPEN_MAX
#else
#define OPEN_MAX        40
#endif  /* _POSIX_OPEN_MAX */
#endif  /* OPEN_MAX */

/* File Scope Variables. Variables available only within this file. */
static char ident[] = "@(#) $RCSfile$ $Revision: 3245 $";
static char     **glob_argv;

static int  alarm_time = 180;

static sigset_t  allsigs;
static sigset_t  oldsigs;

static char *oldpath;
static char     *homedir = PBS_SERVER_HOME;
static char     path_log[_POSIX_PATH_MAX];
static char     *logfile = NULLSTR;
static char     *configfile = NULLSTR;     /* name of config file */
static char *dbfile = "sched_out";
static fd_set fdset;
static char     host[PBS_MAXSERVERNAME];

/* File Scope Variables */

/* External Variables */
extern char *msg_daemonname;

/* External Functions */


/* Structures and Unions */
/* External Functions */
/* Functions */
/*
 * lock_out - lock out other daemons from this directory.
 */

static void
lock_out(
  int fds,
  int op         /* F_WRLCK  or  F_UNLCK */
)
  {

  struct flock flock;

  flock.l_type   = op;
  flock.l_whence = SEEK_SET;
  flock.l_start  = 0;
  flock.l_len    = 0;     /* whole file */

  if (fcntl(fds, F_SETLK, &flock) < 0)
    {
    (void)strcpy(log_buffer, "pbs_sched: another scheduler running\n");
    log_err(errno, msg_daemonname, log_buffer);
    fprintf(stderr, log_buffer);
    exit(1);
    }
  }

/*
**      Clean up after a signal.
*/
static void
die(int sig)
  {
  static char id[] = "die";

  if (sig > 0)
    {
    sprintf(log_buffer, "caught signal %d", sig);
    log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER,
               id, log_buffer);
    }
  else
    {
    log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER,
               id, "abnormal termination");
    }

  SystemClose();

  log_close(1);
  exit(1);
  }

static void initSchedCycle(void)

  {
  CNode *cn;
  Server *s;
  static  char id[] = "initSchedCycle";

  if (sigprocmask(SIG_BLOCK, &allsigs, &oldsigs) == -1)
    log_err(errno, id, "sigprocmaskSIG_BLOCK)"); /* start CS */

  for (s = AllServersHeadGet(); s; s = s->nextptr)
    {
    for (cn = ServerNodesHeadGet(s); cn; cn = cn->nextptr)
      {
      CNodeStateRead(cn, STATIC_RESOURCE);
      }
    }

  if (sigprocmask(SIG_SETMASK, &oldsigs, NULL) == -1)
    log_err(errno, id, "sigprocmask(SIG_SETMASK)");  /* CS end */
  }

static void addDefaults(void)
  {
  /*      Daemons from "localhost" and the current host automatically have */
  /*      permission to call the Scheduler */
  addClient("localhost");
  addClient(ServerInetAddrGet(AllServersLocalHostGet()));

  addRes("*", "CNodeOsGet", "arch");
  addRes("*", "CNodeLoadAveGet", "loadave");
  addRes("*", "CNodeIdletimeGet", "idletime");

  }

/*
**      Got an alarm call.
*/
static void
toolong(int sig)
  {
  static  char    id[] = "toolong";

  struct  stat    sb;
  pid_t   cpid;

  log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, id, "alarm call");
  DBPRT(("alarm call\n"))

  SystemClose();

  if ((cpid = fork()) > 0)        /* parent re-execs itself */
    {
    rpp_terminate();
#ifndef linux
    sleep(5);
#endif

    /* hopefully, that gave the child enough */
    /*   time to do its business. anyhow:    */
    (void)waitpid(cpid, NULL, 0);

    if (chdir(oldpath) == -1)
      {
      sprintf(log_buffer, "chdir to %s", oldpath);
      log_err(errno, id, log_buffer);
      }

    sprintf(log_buffer, "restart dir %s object %s",

            oldpath, glob_argv[0]);

    log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER,
               id, "restart");
    execv(glob_argv[0], glob_argv);
    sprintf(log_buffer, "execv %s", glob_argv[0]);
    log_err(errno, id, log_buffer);
    exit(3);
    }

  /*
  **      Child (or error on fork) gets here and tried
  **      to dump core.
  */
  if (stat("core", &sb) == -1)
    {
    if (errno == ENOENT)
      {
      log_close(1);
      abort();
      rpp_terminate();
      exit(2);        /* not reached (hopefully) */
      }

    log_err(errno, id, "stat");
    }

  log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER,

             id, "exiting without core dump");
  log_close(1);
  rpp_terminate();
  exit(0);
  }

static void
restart(int sig)
  {
  static char id[] = "restart";

  Server *local;
  int p1way, p2way, socket, fd1way;

  printf("restarted\n");
  fflush(stdout);

  if (sig)
    {
    log_close(1);
    log_open(logfile, path_log);
    sprintf(log_buffer, "restart on signal %d", sig);
    }
  else
    {
    sprintf(log_buffer, "restart command");
    }

  log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, id, log_buffer);

#ifdef DEBUG
  printf("Before Restart Free:\n");
  ServerPrint(AllServersLocalHostGet());
  printDynamicArrayTable();
  okClientPrint();
  varstrPrint();
  mallocTablePrint();
  ResPrint();
#endif

  local = AllServersLocalHostGet();
  p1way = ServerPortNumberOneWayGet(local);
  p2way = ServerPortNumberTwoWayGet(local);
  socket = ServerSocketGet(local);
  fd1way = ServerFdOneWayGet(local);

  freeConfig();
  AllServersAdd(host, 0);
  local = AllServersHeadGet();
  AllServersLocalHostPut(local);
  ServerPortNumberOneWayPut(local, p1way);
  ServerPortNumberTwoWayPut(local, p2way);
  ServerSocketPut(local, socket);
  ServerFdOneWayPut(local, fd1way);

#ifdef DEBUG
  printf("After Restart Free:\n");
  ServerPrint(AllServersLocalHostGet());
  printDynamicArrayTable();
  okClientPrint();
  varstrPrint();
  mallocTablePrint();
  ResPrint();
#endif
  addDefaults();

  if (configfile)
    {
    if (readConfig(configfile) != 0)
      die(0);
    }

  initSchedCycle();
  }

/* getArgs: parses 'argc' # of command line arguments as passed in 'argv'. */
static void
getArgs(int argc, char *argv[])
  {
  extern  char    *optarg;
  extern  int     optind;
  int  errflg = 0;
  int  port;
  extern  char    pbs_current_user[];
  char  *ptr;
  int  c;

  glob_argv = argv;

  (void)strcpy(pbs_current_user, "Scheduler");

  port = (int) get_svrport(PBS_SCHEDULER_SERVICE_NAME,
                           "tcp", PBS_SCHEDULER_SERVICE_PORT);

  /*  Get the default port */

  while ((c = getopt(argc, argv, "L:S:d:p:a:c:")) != EOF)
    {
    switch (c)
      {

      case 'L':
        logfile = optarg;
        break;

      case 'S':
        port = atoi(optarg);

        if (port <= 0)
          {
          fprintf(stderr,
                  "%s: illegal port\n", optarg);
          errflg = 1;
          }

        break;

      case 'd':
        homedir = optarg;
        break;

      case 'p':
        dbfile = optarg;
        break;

      case 'a':
        alarm_time = strtol(optarg, &ptr, 10);

        if (alarm_time <= 0 || *ptr != '\0')
          {
          fprintf(stderr,
                  "%s: bad alarm time\n", optarg);
          errflg = 1;
          }

        break;

      case 'c':
        configfile = optarg;
        break;

      case '?':
        errflg = 1;
        break;
      }
    }

  if (errflg || optind != argc)
    {
    static  char    *options[] =
      {
      "[-L logfile]",
      "[-S port]",
      "[-d home]",
      "[-p print_file]",
      "[-a alarm]",
      "[-c configfile]",
      NULL
      };
    int     i;

    fprintf(stderr, "usage: %s\n", argv[0]);

    for (i = 0; options[i]; i++)
      fprintf(stderr, "\t%s\n", options[i]);

    exit(1);
    }

  ServerPortNumberOneWayPut(AllServersLocalHostGet(), port);
  }

/* cdToPrivDir: check to make sure that priv_path is not group and other
  writeable before chdir to priv_path. As a bonus,
  pbs_environment file is also checked for security. */

static void cdToPrivDir(void)
  {
  int  c;
  char            path_priv[_POSIX_PATH_MAX];

  (void)sprintf(path_priv, "%s/sched_priv", homedir);

#if !defined(DEBUG) && !defined(NO_SECURITY_CHECK)
  c  = chk_file_sec(path_priv, 1, 0, S_IWGRP | S_IWOTH, 1, NULL);
  c |= chk_file_sec(PBS_ENVIRON, 0, 0, S_IWGRP | S_IWOTH, 0, NULL);

  if (c != 0)
    exit(1);

#endif /* not DEBUG and not NO_SECURITY_CHECK */
  if (chdir(path_priv) == -1)
    {
    perror(path_priv);
    exit(1);
    }
  }

/* secureEnv: sets up a secure working environment for the daemons */
static void secureEnv(void)
  {
  int c;

  /* The following is code to reduce security risks                */
  /* start out with standard umask, system resource limit infinite */
  umask(022);

  if (setup_env(PBS_ENVIRON) == -1)
    exit(1);

  c = getgid();

  /* secure suppl. group ids */
  if (setgroups(1, (gid_t *)&c) == -1)
    {
    perror("setgroups");
    exit(1);
    }

  c = sysconf(_SC_OPEN_MAX);

  while (--c > 2)
    (void)close(c); /* close any file desc left open by parent */

#ifndef DEBUG
#ifdef _CRAY
  (void)limit(C_JOB,      0, L_CPROC, 0);

  (void)limit(C_JOB,      0, L_CPU,   0);

  (void)limit(C_JOBPROCS, 0, L_CPU,   0);

  (void)limit(C_PROC,     0, L_FD,  255);

  (void)limit(C_JOB,      0, L_FSBLK, 0);

  (void)limit(C_JOBPROCS, 0, L_FSBLK, 0);

  (void)limit(C_JOB,      0, L_MEM  , 0);

  (void)limit(C_JOBPROCS, 0, L_MEM  , 0);

#else   /* not  _CRAY */

    {

    struct rlimit rlimit;

    rlimit.rlim_cur = RLIM_INFINITY;
    rlimit.rlim_max = RLIM_INFINITY;
    (void)setrlimit(RLIMIT_CPU,   &rlimit);
    (void)setrlimit(RLIMIT_FSIZE, &rlimit);
    (void)setrlimit(RLIMIT_DATA,  &rlimit);
    (void)setrlimit(RLIMIT_STACK, &rlimit);
#ifdef  RLIMIT_RSS
    (void)setrlimit(RLIMIT_RSS, &rlimit);
#endif  /* RLIMIT_RSS */
#ifdef  RLIMIT_VMEM
    (void)setrlimit(RLIMIT_VMEM, &rlimit);
#endif  /* RLIMIT_VMEM */
    }
#endif  /* not _CRAY */
#endif  /* not DEBUG */
  }

/* signalHandleSet: specifies various signals and signal handlers that will
      be handled by the program. */
static void signalHandleSet(void)
  {
  static  char id[] = "signalHandleSet";

  struct  sigaction       act;

  if (sigemptyset(&allsigs) == -1)
    {
    char *prob = "sigemptyset";
    log_err(errno, id, prob);
    perror(prob);
    die(0);
    }

  /* The following seemed to allow signals to be caught again after */
  /* a SIGALRM was received */
  if (sigprocmask(SIG_SETMASK, &allsigs, NULL) == -1)
    log_err(errno, id, "sigprocmask(SIG_SETMASK)");

  act.sa_flags = 0;

  /* Remember to block these during critical sections so we don't get confused */
  if (sigaddset(&allsigs, SIGHUP) == -1 || \
      sigaddset(&allsigs, SIGINT) == -1 || \
      sigaddset(&allsigs, SIGTERM) == -1)
    {
    char *prob = "sigaddset";
    log_err(errno, id, prob);
    perror(prob);
    die(0);
    }

  act.sa_mask = allsigs;

  act.sa_handler = restart;       /* do a restart on SIGHUP */

  if (sigaction(SIGHUP, &act, NULL) == -1)
    {
    char *prob = "sigaction";
    log_err(errno, id, prob);
    perror(prob);
    die(0);
    };

  act.sa_handler = toolong;       /* handle an alarm call */

  if (sigaction(SIGALRM, &act, NULL) == -1)
    {
    char *prob = "sigaction";
    log_err(errno, id, prob);
    perror(prob);
    die(0);
    }

  act.sa_handler = die;           /* bite the biscuit for all following */

  if (sigaction(SIGINT, &act, NULL) == -1 || \
      sigaction(SIGTERM, &act, NULL) == -1)
    {
    char *prob = "sigaction";
    log_err(errno, id, prob);
    perror(prob);
    die(0);
    }
  }

/* SystemInit: does various things to initialize the scheduler. This includes
       obtaining command line arguments via 'argc' and argv'. */

void
SystemInit(int argc, char *argv[])
  {

  static  char id[] = "SystemInit";
  pid_t           pid;
  int  lockfds;
  int  gethostname();

#ifndef DEBUG
  if (IamRoot() == 0)
    {
	exit(1);
    }
#endif  /* DEBUG */

  if (gethostname(host, sizeof(host)) == -1)
    {
    char    *prob = "gethostname";

    log_err(errno, id, prob);
    perror(prob);
    die(1);
    }

  varstrInit();

  mallocTableInit();
  /* initialized to the localhost */
  AllServersInit();
  AllServersAdd(host, 0);
  AllServersLocalHostPut(AllServersHeadGet());
  getArgs(argc, argv);

  /* Save the original working directory for "restart" */

  if ((oldpath = getcwd((char *)NULL, MAXPATHLEN)) == NULL)
    {
    fprintf(stderr, "cannot get current working directory\n");
    exit(1);
    }

  cdToPrivDir();

  secureEnv();

  (void)sprintf(path_log, "%s/sched_logs", homedir);

  if (log_open(logfile, path_log) == -1)
    {
    fprintf(stderr, "%s: logfile could not be opened\n", argv[0]);
    exit(1);
    }

  if (ServerOpenInit(AllServersLocalHostGet()) != 0)
    {
    die(0);
    }

  addDefaults();

  /* initialize nodes before reading config file */

  if (configfile)
    {
    if (readConfig(configfile) != 0)
      {
      die(0);
      }
    }

  lockfds = open("sched.lock", O_CREAT | O_TRUNC | O_WRONLY, 0644);

  if (lockfds < 0)
    {
    char    *prob = "lock file";

    log_err(errno, id, prob);
    perror(prob);
    die(0);
    }

  lock_out(lockfds, F_WRLCK);

#ifndef DEBUG
  lock_out(lockfds, F_UNLCK);

  if ((pid = fork()) == -1)       /* error on fork */
    {
    char    *prob = "fork";

    log_err(errno, id, prob);
    perror(prob);
    die(0);
    }
  else if (pid > 0)               /* parent exits */
    exit(0);

  if ((pid = setsid()) == -1)
    {
    log_err(errno, id, "setsid");
    die(0);
    }

  lock_out(lockfds, F_WRLCK);

  freopen(dbfile, "a", stdout);
  setvbuf(stdout, NULL, _IOLBF, 0);
  dup2(fileno(stdout), fileno(stderr));
#else
  pid = getpid();
#endif
  freopen("/dev/null", "r", stdin);

  /* write schedulers pid into lockfile */
  (void)sprintf(log_buffer, "%ld\n", pid);
  (void)write(lockfds, log_buffer, strlen(log_buffer) + 1);

#if (PLOCK_DAEMONS & 2)
  (void)plock(PROCLOCK);  /* lock daemon into memory */
#endif


  /* Since the signal set contains routines that mallocs, be sure to set */

  FD_ZERO(&fdset);

  signalHandleSet();
  initSchedCycle();

  sprintf(log_buffer, "%s startup pid %ld", argv[0], pid);
  log_record(PBSEVENT_SYSTEM, PBS_EVENTCLASS_SERVER, id, log_buffer);
  }

/* SystemStateRead: get the internal state of the system. Be sure
   "SystemInit()" was called before invoking this function!.
   RETURNS any of the schedule commands.
   NOTE: SCH_ERROR if an error was encountered.  */
void
SystemStateRead(void (*sched_main)(void))
  {

  static char         id[] = "SystemStateRead";
  int   cmd;
  Server  *s;
  CNode  *cn;
  int   server_sock;
  int   go;
  extern int  rpp_fd;

  if (sigprocmask(SIG_BLOCK, &allsigs, &oldsigs) == -1)
    log_err(errno, id, "sigprocmask(SIG_BLOCK)");

  server_sock = ServerSocketGet(AllServersLocalHostGet());

  if (sigprocmask(SIG_SETMASK, &oldsigs, NULL) == -1)
    log_err(errno, id, "sigprocmask(SIG_SETMASK)");

  for (go = 1; go;)
    {

    if (rpp_fd != -1)
      FD_SET(rpp_fd, &fdset);

    FD_SET(server_sock, &fdset);

#ifdef DEBUG
    printf("%s: Before select\n", id);

#endif
    if (select(FD_SETSIZE, &fdset, NULL, NULL, NULL) == -1)
      {

      if (errno != EINTR)
        {
        log_err(errno, id, "select");
        die(0);
        }

      continue;
      }

#ifdef DEBUG
    printf("%s: After select\n", id);

#endif
    if (rpp_fd != -1 && FD_ISSET(rpp_fd, &fdset))
      {

      /* start CS */
      if (sigprocmask(SIG_BLOCK, &allsigs, &oldsigs) == -1)
        log_err(errno, id, "sigprocmask(SIG_BLOCK)");

      if (rpp_io() == -1)
        log_err(errno, id, "rpp_io");

      /* CS end */
      if (sigprocmask(SIG_SETMASK, &oldsigs, NULL) == -1)
        log_err(errno, id, "sigprocmask(SIG_SETMASK)");
      }

    if (!FD_ISSET(server_sock, &fdset))
      {

#ifdef DEBUG
      printf("%s: server_sock is not set!\n", id);
#endif
      continue;
      }

    if (sigprocmask(SIG_BLOCK, &allsigs, &oldsigs) == -1)
      log_err(errno, id, "sigprocmaskSIG_BLOCK)"); /* start CS */

    if (ServerOpen(AllServersLocalHostGet()) == 0)
      {


      cmd = ServerRead(AllServersLocalHostGet());

      if (cmd == SCH_ERROR || cmd == SCH_SCHEDULE_NULL)
        {
        continue;
        }

      switch (cmd)
        {

        case SCH_SCHEDULE_NEW:

        case SCH_SCHEDULE_TERM:

        case SCH_SCHEDULE_TIME:

        case SCH_SCHEDULE_RECYC:

        case SCH_SCHEDULE_CMD:

        case SCH_SCHEDULE_FIRST:
          alarm(alarm_time);

          for (s = AllServersHeadGet(); s; s = s->nextptr)
            {
            ServerStateRead(s);

            for (cn = ServerNodesHeadGet(s); cn; cn = cn->nextptr)
              {
              if (CNodeQueryMomGet(cn))
                {
                CNodeStateRead(cn, DYNAMIC_RESOURCE);

                if (CNodeOsGet(cn) == NULLSTR)
                  {
                  CNodeStateRead(cn, STATIC_RESOURCE);
                  }
                }
              }
            }

          sched_main();

          SystemCloseServers();

          alarm(0);
          break;

        case SCH_CONFIGURE:

        case SCH_RULESET:
          restart(0);
          break;

        case SCH_QUIT:
          go = 0;
          break;

        case SCH_ERROR:

        case SCH_SCHEDULE_NULL:
          break;

        default:
          log_err(-1, id, "unknown command");
        }

      }

    if (sigprocmask(SIG_SETMASK, &oldsigs, NULL) == -1)
      log_err(errno, id, "sigprocmask(SIG_SETMASK)"); /* CS end */

    } /* for */
  }

void SystemCloseServers(void)
  {
  Server *s;

  for (s = AllServersHeadGet(); s; s = s->nextptr)
    {
    if (ServerFdTwoWayGet(s) >= 0)
      ServerClose(s);
    }
  }

void SystemClose(void)
  {
  SystemCloseServers();

  ServerCloseFinal(AllServersLocalHostGet());
#ifdef DEBUG
  printf("Before FINAL Free:\n");
  printDynamicArrayTable();
  okClientPrint();
  varstrPrint();
  mallocTablePrint();
  ResPrint();
#endif

  freeConfig();
  varstrFreeByScope(0);
#ifdef DEBUG
  printf("After FINAL Free:\n");
  printDynamicArrayTable();
  okClientPrint();
  varstrPrint();
  mallocTablePrint();
  ResPrint();
#endif
  }