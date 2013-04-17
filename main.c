#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <regex.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#define LINE_COUNT 10

#define SLEEP_TIME 0.25f
#define BREAK_TIME 3.0f
#define DELAY_TIME 0.0f

#define LINE_BUFFER_SIZE 2048
#define CMD_BUFFER_SIZE 2048

#define REGEXP_MATCH_LIMIT 32

#define MAX_FILES 128

int fcount = 0;
int fpna[MAX_FILES];
int fpua[MAX_FILES];
char* fppa[MAX_FILES];
FILE* fpa[MAX_FILES];
FILE* fp = NULL;
struct termios oterm, nterm;

short stop = 0;

void done (int ret) {
  int i = 0;
  (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &oterm);
  for (i = 0; i < fcount; i++)
    if (fpa[i] != NULL) fclose(fpa[i]);
  if (ret == 0)
    printf("\n\x1b[0m  \x1b[37m\x1b[1m(O ___ \x1b[37mO)\n         \x1b[34m'\x1b[37m\x1b[0m \n");
  else if (ret == 1)
    printf("\n\x1b[0m  \x1b[31m(x ___ x)\x1b[0m \n\n");
  else if (ret == 3)
    printf("\n\x1b[0m  \x1b[37m(\x1b[\33m? ___ ?)\x1b[0m \n\n");

  exit(ret);
}

void shandle (int signum) {
  switch (signum) {
    case SIGINT:
    case SIGKILL:
    case SIGTERM:
      done(1); break;
    case SIGQUIT:
      done(0); break;
    case SIGSTOP:
    case SIGTSTP:
      stop = 1;
      break;
    case SIGCONT:
      stop = 0;
      if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &nterm) != 0)
        done(1);
      break;
  }
}

int slen (char *s) {
  int len;
  char *ptr = s;
  for (len = 0; *ptr; ptr++, len++) { }
  return len;
}

void help () {
  printf("\n");
  printf("Usage: flail [OPTION]... [FILE]... \n");
  printf("Print the last 10 lines of FILES to standard output, and begin following. \n");
  printf("Designed to slowly scroll as it follows FILES, if no input has been received \n");
  printf("then flail will output a filler line to reflect this. \n");
  printf("With multiple FILES, print a header for each file. Most time options support decimal input! \n");
  printf("\n");
  printf("\x1b[30m\x1b[1mOPTIONS\x1b[0m\n");
  printf("\x1b[1m  -B K\x1b[0m,\t\x1b[1m--buffer-size K \x1b[0m\t\tSet the input buffer size, default 2024. \n");
  printf("\x1b[1m  -b K\x1b[0m,\t\x1b[1m--break-interval K \x1b[0m\t\tWait K seconds before outputting a filler line, after no new input has been found. \n");
  printf("\x1b[1m  -d K\x1b[0m,\t\x1b[1m--delay-interval K \x1b[0m\t\tDelay the output of lines by K seconds each line, default 0. \n");
  printf("\x1b[1m  -h\x1b[0m,\t\x1b[1m--help \x1b[0m\t\t\t\tDisplay this help dialog! \n");
  printf("\x1b[1m  -i\x1b[0m,\t\x1b[1m--regex-include \x1b[0m\t\tInclude non-matching lines when regexp is set. \n");
  printf("\x1b[1m  -j\x1b[0m,\t\x1b[1m--regex-selection \x1b[0m\t\tHighlight entire line instead of matching portion.\n");
  printf("\x1b[1m  -n K\x1b[0m,\t\x1b[1m--lines K \x1b[0m\t\t\tOutput the last K lines, instead of the last 10. \n");
  printf("\x1b[1m  -q\x1b[0m,\t\x1b[1m--quiet \x1b[0m\t\t\tNever print headers for files.\n");
  printf("\x1b[1m  -r K\x1b[0m,\t\x1b[1m--run-command K \x1b[0m\t\tRun command K before parsing file.\n");
  printf("\x1b[1m  -s K\x1b[0m,\t\x1b[1m--sleep-interval K \x1b[0m\t\tWait K seconds between file scan, input intervals.\n");
  printf("\x1b[1m  -u\x1b[0m,\t\x1b[1m--debug \x1b[0m\t\t\tOutput Debug messages! \x1b[33:-D\x1b[0m \n");
  printf("\x1b[1m  -v\x1b[0m,\t\x1b[1m--verbose \x1b[0m\t\t\tOutput file name for every line.\x1b[33:-D\x1b[0m \n");
  printf("\x1b[1m  -w\x1b[0m,\t\x1b[1m--regex-highlight-switch \x1b[0m\tReverse the regexp highlight.\n");
  printf("\n");
  printf("\x1b[30m\x1b[1mCOMMANDS\x1b[0m\n");
  printf("\x1b[1m  :q\x1b[0m,\t\x1b[1m:quit \x1b[0m\t\t\t\tExit the program! Same as :e or :exit!\n");
  printf("\x1b[1m  :e\x1b[0m,\t\x1b[1m:exit \x1b[0m\t\t\t\tExit the program! Same as :q or :quit!\n");
  printf("\x1b[1m  :h\x1b[0m,\t\x1b[1m:help \x1b[0m\t\t\t\tDisplay this help dialog! \n");
  printf("\x1b[1m  :i\x1b[0m,\t\x1b[1m:regex-include \x1b[0m\t\t\tInclude non-matching lines when regexp is set. \n");
  printf("\x1b[1m  :j\x1b[0m,\t\x1b[1m:regex-selection \x1b[0m\t\tHighlight entire line instead of matching portion.\n");
  printf("\x1b[1m  :u\x1b[0m,\t\x1b[1m:debug \x1b[0m\t\t\t\tOutput Debug messages! \x1b[33:-D\x1b[0m \n");
  printf("\x1b[1m  :w\x1b[0m,\t\x1b[1m:regex-highlight-switch \x1b[0m\tReverse the regexp highlight. \n");
  printf("\n");
  printf("Written by Evin Owen, 2013 -- E-Mail flail bugs to nowhere, because I honestly don't care. \x1b[30m\x1b[1mw00t h00t.\x1b[0m\n\n");
}

double tdif (struct timeval *tvA, struct timeval *tvB) {
  int usec = 0;
  struct timeval tvR = *tvB;

  if (tvA->tv_usec < tvB->tv_usec) {
    usec = (tvB->tv_usec - tvA->tv_usec) / 1000000 + 1;
    tvR.tv_usec -= 1000000 * usec;
    tvR.tv_sec += usec;
  }

  if (tvA->tv_usec - tvB->tv_usec > 1000000) {
    usec = (tvB->tv_usec - tvA->tv_usec) / 1000000;
    tvR.tv_usec += 1000000 * usec;
    tvR.tv_sec -= usec;
  }

  tvR.tv_sec = tvA->tv_sec - tvR.tv_sec;
  tvR.tv_usec = tvA->tv_usec - tvR.tv_usec;

  return tvR.tv_sec + ((double)tvR.tv_usec / 1000000.0f);
}

int main (int argc, char* argv[]) {
  double mtime = 0.0f;

  struct timeval ttime, btime, dtime, ctime;

  fd_set fdin;
  int nstdin = fileno(stdin);

  int i = 0;
  int j = 0;
  int f = 0;
  short flag = 0;
  short cmd = 0;
  short first = 0;
  short print = 0;

  int ihold = 0;
  double dhold = 0;

  int lfile = -1;

  int *iptr = NULL;
  double *dptr = NULL;
  char *cptr = NULL;
  char *aptr = NULL;

  char csingle [1];
  char cdouble [2] = { '\0' , '\0' };
  char cbuffer [CMD_BUFFER_SIZE];
  char crep [CMD_BUFFER_SIZE];

  short wcmd = 0;
  short _dbug = 0;
  short _help = 0;
  short _rselect = 0;
  short _rinc = 0;
  short _rhswitch = 0;
  short _gpos = 0;
  short _retry = 0;
  short _fdesc = 0;
  short _quiet = 0;
  short _verb = 0; 
  short _xclde = 0;
 
  int _maxunch = 0;
  int _pid = 0;

  int _bytec = 0;
  int _linec = LINE_COUNT;
  int _bsize = LINE_BUFFER_SIZE;

  double _stime = SLEEP_TIME;
  double _btime = BREAK_TIME;
  double _dtime = DELAY_TIME;

  int pfno = 0;
  int pfdesc = 0;

  char *path = NULL;
  char *cvalue = NULL;
  char *nothin = NULL;
  char *highlight = NULL;

  char *hnormal = "\x1b[30m\x1b[43m"; 
  char *hswitch = "\x1b[33m";

  pid_t pidres;

  regex_t rep;
  regmatch_t rmatchbox[REGEXP_MATCH_LIMIT + 1];
  int rmake = -1;
  int rmatch = -1;

  cbuffer[0] = '\0';

  for (f = 0; f < fcount; f++) fpa[f] = NULL;

  if (tcgetattr (STDIN_FILENO, &oterm) != 0)
    return -1;

  if (argc > 1) for (i = 1; i < argc; i++) {
    if (cvalue != NULL) i--;
    else if (flag) cvalue = argv[i];
    if (cvalue != NULL) {
      flag = 0;
      if (iptr != NULL) {
        if (iptr == &_linec || iptr == &_bytec)
          if (cvalue[0] == '+') _gpos = 1;
        ihold = (int)strtol(cvalue, &nothin, 10);
        if (ihold >= 0) *iptr = ihold;
      } else if (dptr != NULL) {
        dhold = (double)strtod(cvalue, &nothin);
        if (dhold >= 0.0f) *dptr = dhold;
      } else if (cptr != NULL) {
        strcpy(cptr, cvalue);
      }
      cvalue = NULL;
    } else if (argv[i][0] == '-') {
      wcmd = 0;
      ihold = slen(argv[i]);
      if (ihold <= 1) continue;
      if (argv[i][1] == '-') wcmd = 1;
      if (wcmd) aptr = argv[i];
      else aptr = cdouble;

      for (j = 1; j < ihold; j++) {
	if (wcmd) {
          for (j = 0; j < ihold; j++)
            if (argv[i][j] == '=' && j != ihold - 1) {
              cvalue = argv[i] + j + 1;
	      argv[i][j] = '\0';
	      break;
            }
	} else {
          if (argv[i][j] == '=' && j != ihold - 1) {
            cvalue = argv[i] + j + 1;
            break;
          }
	  aptr[0] = argv[i][j];
	}
        flag = 1;
        iptr = NULL;
        dptr = NULL;
        cptr = NULL;
	
        if (strcmp(aptr, "B") == 0 || strcmp(aptr, "--buffer-size") == 0)
          iptr = &_bsize;
	else if (strcmp(aptr, "c") == 0 || strcmp(aptr, "--bytes") == 0)
          iptr = &_bytec;
        else if (strcmp(aptr, "n") == 0 || strcmp(aptr, "--lines") == 0)
          iptr = &_linec;
        else if (strcmp(aptr, "s") == 0 || strcmp(aptr, "--sleep-interval") == 0)
          dptr = &_stime;
        else if (strcmp(aptr, "b") == 0 || strcmp(aptr, "--break-interval") == 0)
          dptr = &_btime;
        else if (strcmp(aptr, "d") == 0 || strcmp(aptr, "--delay-interval") == 0)
          dptr = &_dtime;
        else if (strcmp(aptr, "r") == 0 || strcmp(aptr, "--run-command") == 0)
          cptr = cbuffer;
        else if (strcmp(aptr, "m") == 0 || strcmp(aptr, "--max-unchanged-stats") == 0)
          iptr = &_maxunch;
        else if (strcmp(aptr, "p") == 0 || strcmp(aptr, "--pid") == 0)
          iptr = &_pid;
        else {
          flag = 0;
          if (strcmp(aptr, "u") == 0 || strcmp(aptr, "--debug") == 0)
            _dbug = 1;
          else if (strcmp(aptr, "f") == 0 || strcmp(aptr, "--follow-descriptor") == 0)
            _fdesc = 1;
          else if (strcmp(aptr, "h") == 0 || strcmp(aptr, "--help") == 0)
            _help = 1;
          else if (strcmp(aptr, "j") == 0 || strcmp(aptr, "--regex-selection") == 0)
            _rselect = 1;
          else if (strcmp(aptr, "i") == 0 || strcmp(aptr, "--regex-include") == 0)
            _rinc = 1;
          else if (strcmp(aptr, "w") == 0 || strcmp(aptr, "--regex-highlight-switch") == 0)
            _rhswitch = 1;
          else if (strcmp(aptr, "r") == 0 || strcmp(aptr, "--retry") == 0)
            _retry = 1;
          else if (strcmp(aptr, "q") == 0 || strcmp(aptr, "--quiet") == 0)
            _quiet = 1;
          else if (strcmp(aptr, "v") == 0 || strcmp(aptr, "--verbose") == 0)
            _verb = 1;
          else if (strcmp(aptr, "x") == 0 || strcmp(aptr, "--regex-exclude") == 0)
            _xclde = 1;
        }
        if (wcmd) break;
      }
    } else {
      fppa[fcount++] = argv[i];
    }
  }

  for (f = 0; f < fcount; f++) {
    if (_dbug)
      printf("\x1b[33m\x1b[1m--[\x1b[37m Debug \x1b[33m] Opening %s \x1b[0m \n", fppa[f]);
    if (_fdesc) {
      if ((fpna[f] = open(fppa[f], O_RDONLY)) == -1) done(1);
      if ((fpa[f] = fdopen(fpna[f], "r")) == NULL) {
        close(fpna[f]);
        done(1);
      }
    } else if ((fpa[f] = fopen(fppa[f], "r")) == NULL) {
      printf("  \x1b[30m\x1b[1mProvided file path sucks!\x1b[0m \n");
      done(1);
    }

    i = 1;
    ihold = 0;
    flag = 0;
    if (_bytec > 0) {
      if (_gpos) fseek(fpa[f], _bytec, SEEK_SET);
      else fseek(fpa[f], _bytec * -1, SEEK_END);  
    } else {
      if (_gpos) while (ihold < _linec) {
        if (!(csingle[0] = (char)fgetc(fpa[f]))) flag = 1;
        else if (csingle[0] == '\n') ihold++;
        if (flag) {
          fseek(fpa[f], 0, SEEK_END);
          break;
        }
      } else while (ihold <= _linec) {
        fseek(fpa[f], i++ * -1, SEEK_END);
        if (ftell(fpa[f]) == 0) flag = 1;
        if (!(csingle[0] = (char)fgetc(fpa[f]))) done(1);
        else if (csingle[0] == '\n') ihold++;
        if (flag) {
          fseek(fpa[f], 0, SEEK_SET);
          break;
        }
      }
    }
    fpua[f] = 0;

    lfile = f;
  }

  if (_rhswitch) highlight = hswitch;
  else highlight = hnormal;

  if (cbuffer[0] != '\0') cmd = 1;

  if (_help) {
    help();
    done(3);
  }

  char buffer [_bsize];

  int __stime = _stime * 1000000;

  if (fcount < 1) {
    printf("  \x1b[30m\x1b[1mProvide target file path!\x1b[0m \n");
    done(1);
  }

  gettimeofday(&btime, NULL);
  gettimeofday(&dtime, NULL);

  signal(SIGINT, shandle);
  signal(SIGKILL, shandle);
  signal(SIGQUIT, shandle); 
  signal(SIGTERM, shandle); 
  //signal(SIGSTOP, shandle);
  //signal(SIGTSTP, shandle);
  signal(SIGCONT, shandle);

  nterm = oterm;
  nterm.c_lflag &= ~ECHO;

  if (tcsetattr (STDIN_FILENO, TCSAFLUSH, &nterm) != 0)
    return -1;

  if (_dbug > 0)
    printf("\x1b[33m\x1b[1m--[\x1b[37m Debug \x1b[33m] %d %2.4f %2.4f %2.4f \x1b[0m \n", _bsize, _stime, _btime, _dtime);

  ctime.tv_sec = 1;
  ctime.tv_usec = 1;

  if (_dbug > 0)
    printf("\x1b[33m\x1b[1m--[\x1b[37m Debug \x1b[33m] Initialized. \n");

  i = 0;
  ihold = 0;
  first = fcount > 1 ? 1 : 0;
  while (1) {
    while (stop) {
      printf("  \x1b[0m\x1b[36m\x1b[2m  z z z \x1b[0m  \n");
      sleep(2);
    }
    gettimeofday(&ttime, NULL);

    if (_pid && kill(_pid, 0)) done(0);

    if (ioctl(nstdin, FIONREAD, &ihold) < 0)
      printf("  Stdin read error! %s \n", strerror(errno));
    else if (ihold > 0) {
      if (fgets(cbuffer, CMD_BUFFER_SIZE, stdin) != NULL) cmd = 1;
    }

    if (cmd > 0) {
      if (_dbug) printf("\x1b[0m\x1b[33m\x1b[1m--[\x1b[37m Debug \x1b[33m] %s\x1b[0m", cbuffer);
      for (i = 0; i < CMD_BUFFER_SIZE; i++)
        if (cbuffer[i] == '\n' || cbuffer[i] == '\r') cbuffer[i] = '\0';
      if (cbuffer[0] == ':') {
        if (strcmp(cbuffer, ":q") == 0 || strcmp(cbuffer, ":quit") == 0)
          done(0);
        else if (strcmp(cbuffer, ":e") == 0 || strcmp(cbuffer, ":exit") == 0)
          done(0);
        else if (strcmp(cbuffer, ":h") == 0 || strcmp(cbuffer, ":help") == 0)
          help();
        else if (strcmp(cbuffer, ":i") == 0 || strcmp(cbuffer, ":regex-include") == 0)
          _rinc = _rinc ? 0 : 1;
        else if (strcmp(cbuffer, ":j") == 0 || strcmp(cbuffer, ":regex-selection") == 0)
          _rselect = _rselect ? 0 : 1;
        else if (strcmp(cbuffer, ":u") == 0 || strcmp(cbuffer, ":debug") == 0)
          _dbug = _dbug ? 0 : 1;
        else if (strcmp(cbuffer, ":w") == 0 || strcmp(cbuffer, ":regex-highlight-switch") == 0)
          _rhswitch = _rhswitch ? 0 : 1;
        else if (strcmp(cbuffer, ":x") == 0 || strcmp(cbuffer, ":regex-exclude") == 0)
          _xclde = _xclde ? 0 : 1;
      } else {
        rmake = regcomp(&rep, cbuffer, 0);
        if (rmake && _dbug)
          printf("\x1b[33m\x1b[1m--[\x1b[37m Debug \x1b[33m] Error Building Regex \x1b[33m\x1b[1m[\x1b[0m%s\x1b[33m\x1b[1m]\x1b[0m \n", cbuffer);
      }
      cmd = 0;

      if (_rhswitch) highlight = hswitch;
      else highlight = hnormal;
    }

    if (_dtime <= 0 || tdif(&ttime, &dtime) >= _dtime) {
      flag = 1;
      dtime = ttime;
      for (f = 0; f < fcount; f++) {
      fp = fpa[f];
      if (fp == NULL) continue;

      while (fgets(buffer, _bsize, fp) != NULL) {
        print = 0;
        fpua[f] = 0;
        btime = ttime;
        ihold = slen(buffer);
        for (i = 0; i < ihold; i++)
          if (buffer[i] == '\n' || buffer[i] == '\r') buffer[i] = '\0';
        flag = 0;
        if (rmake == 0) {
          if (_rselect) {
            rmatch = regexec(&rep, buffer, 0, NULL, 0);
            if (!rmatch) {
              if (!_xclde) print = 1;
            } else if (_rinc || _xclde) print = 1;
          } else {
            rmatch = regexec(&rep, buffer, REGEXP_MATCH_LIMIT, rmatchbox, 0);
            if (!rmatch && !_xclde) print = 1;
            else if (_rinc || _xclde) print = 1;
          }
        } else print = 1;
        if (print) {
          if (!_quiet && !_verb && (lfile != f || first)) printf("\x1b[0m\n\x1b[32m<<-\x1b[37m\x1b[1m %s \x1b[0m\x1b[32m->>\x1b[0m\n", fppa[f]);
          else if (!_quiet && _verb) printf("\x1b[0m\x1b[32m<<-\x1b[37m\x1b[1m %s \x1b[0m\x1b[32m->>\x1b[0m ", fppa[f]);
          if (!rmatch)
            if (_rselect) printf("%s%s\x1b[0m\n", highlight, buffer);
            else printf(
              "%.*s%s%.*s\x1b[0m%.*s\x1b[0m\n",
              rmatchbox[0].rm_so, buffer,
              highlight,
              rmatchbox[0].rm_eo - rmatchbox[0].rm_so, buffer + rmatchbox[0].rm_so,
              _bsize - rmatchbox[0].rm_eo, buffer + rmatchbox[0].rm_eo
            );
          else printf("%s\x1b[0m\n", buffer);
          lfile = f;
          first = 0;
          if (_dtime > 0) break;
        }
      }
      if (flag > 0) {
        fpua[f]++;
        if (_maxunch > 0 && fpua[f] > _maxunch) {
          if (_dbug > 0)
            printf("\x1b[33m\x1b[1m--[\x1b[37m Debug \x1b[33m] Hit Max Unchanged Intervals, reloading file %s \n", fppa[f]);
          ihold = ftell(fp);
          if (_fdesc) {
            //if (fp != NULL) fdclose(fp);
            if ((fp = fdopen(fpna[f], "r")) == NULL)  done(1);
            else fseek(fp, ihold, SEEK_SET);
          } else {
            if (fp != NULL) fclose(fp);
            if ((fp = fopen(fppa[f], "r")) == NULL)  done(1);
            else fseek(fp, ihold, SEEK_SET);
          }
          fpua[f] = 0;
        }
      }

      }

    }

    if (flag > 0) {
      if (_btime > 0 && tdif(&ttime, &btime) >= _btime) {
        btime = ttime;
        printf("  \x1b[0m\x1b[30m\x1b[1m . . . .\x1b[0m  \n");
      }
    }
    flag = 0;
    usleep(__stime);
  }

  done(0);

  return 0;
}
