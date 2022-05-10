#define _GNU_SOURCE
#include <sys/types.h> 
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>  
#include <unistd.h> 
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <stdint.h>
#include <assert.h>
#include <limits.h>

// Global variables
int exitStatus = 0;
int backgroundAllowed = 1;
char *tmpToken = NULL;

/* 
cmd structure which takes:
a char pointer array to hold the command
a char pointer to an infile for redirection of input
a char pointer to an outfile for redicrection of output
an in which is 1 or 0 to determine if process is background or foreground
**/
struct cmd {
  char* args[514];
  char* infile;
  char* outfile;
  int isbackground;
};

// function prototypes
int parsecmd(char* usrInput, struct cmd* command);
int validateInput(char* str);
char *expand(char* token); 
void runCommand(struct cmd* command);
void changeDir(struct cmd* command);
void signalChildHandler(int sigNum);
void handle_SIGTSTP(int sigNum);
void child_handle_SIGINT(int sigNum);
char *itoa_safe(int i, char buf[]);

int main() {
  // declare a struct cmd variable named command to store the users command
  struct cmd command = {0};

  // initialize SIGTSTP_action struct to handle ctrl Z signal
  struct sigaction SIGTSTP_action = {0};
  SIGTSTP_action.sa_handler = handle_SIGTSTP;
  sigfillset(&SIGTSTP_action.sa_mask);
  SIGTSTP_action.sa_flags = SA_RESTART;
  sigaction(SIGTSTP, &SIGTSTP_action, NULL);
  
  // initialize SIGINT_action struct to handle ctrl C signal
  struct sigaction SIGINT_action = {0};
  SIGINT_action.sa_handler = SIG_IGN;
  SIGINT_action.sa_flags = 0;
  sigaction(SIGINT, &SIGINT_action, NULL);

    while (1) {
      // clear out args array if it is not empty
      int i = 0;
      while (command.args[i] != NULL) {
        command.args[i] = NULL;
        i++;
      }
      // reset the rest of cmd struct to prepare for new command from user
      command.infile = NULL;
      command.outfile = NULL;
      command.isbackground = 0;
      tmpToken = NULL;

      // prompt user
      printf(":");
      fflush(stdout);

      // remove left over input from previous call if needed
      // essentially manually flushing input stream
      // following three lines modified slightly from:
      // https://stackoverflow.com/questions/23397235/fgets-falls-through-without-taking-input
      int ch;
      ch = fgetc(stdin);
      if (ch != 0) ungetc(ch, stdin);
      
      // store user input in a char array
      char inputStr[2049];
      fgets(inputStr, 2049, stdin);

      // check for comments (# char) and blank lines
      if (validateInput(inputStr) == -1) {
        continue;
      }
      
      // if user input is valid:
      // if returnedCommand is 0 execute command with runCommand function
      // if returnedCommand is 2 execute built in status function
      // if returnedCommand is 3 execute built in cd function
      int returnedCommand = parsecmd(inputStr, &command);
      switch (returnedCommand) {
        case 0:
          runCommand(&command);
          break;
        case 2: 
          break;
        case 3:
          changeDir(&command);
          break;
      }
      free(tmpToken);
    }

    for (int i=0; command.args[i]; i++) {
      free(command.args[i]);
  }
  return 0; 
}

/**
* Function: validateInput
* takes a char array and returns -1 if first char is # or a space
* @param str array of chars in put by the user
* @return -1 if first char is a '#' or a ' '
*/
int validateInput(char* str) {

  //check for comment
  if (str[0] == '#') {
    return -1;
  }

  // check for blank line
  if (str[0] == ' ') {
    return -1;
  }

  // check for space
  if (str[0] == '\n') {
    return -1;
  }
}

/**
* Function: parsecmd
* takes user input and parses it into a cmd struct
* @param usrInput char* input command from user
* @param command struct cmd* a struct built from parsed shell command 
* @return integer 
*/
int parsecmd(char* usrInput, struct cmd* command) {
  command->infile = NULL;
  command->outfile = NULL;
  
  // initialize integer ret which determines the return value 
  int ret = 0;
  // initialize delimeter for strtok function: a space and a new line char
  char *delimeter = " \n";

  // get the first token!
  char *token = strtok(usrInput, delimeter);

  // if the user enters "exit" execute built in exit command
  if ((!strcmp(token, "exit")) || ((!strcmp(token, "exit")) && strtok(NULL, delimeter)[0] == '&'))  
  {
    // kill any process or job before exiting
    kill(0, SIGTERM);
    ret = 1;
    return ret;
  }

  // if user enters "status" execute built in status command
  if ((!strcmp(token, "status")) || ((!strcmp(token, "status")) && strtok(NULL, delimeter)[0] == '&')) {
    int status = exitStatus;

    // print out exit status or terminating signal of last foreground process
    // the status is received from the global variable exitStatus
    if (WIFEXITED(status)) {
      printf("exit status %d\n", WEXITSTATUS(status));
      fflush(stdout);
    } else if (WIFSIGNALED(status)) {
      printf("terminated by signal %d\n", WTERMSIG(status));
      fflush(stdout);
    }
    ret = 2;
    return ret;
  }

  // initialize variable i which keeps track of index of args array
  int i = 0;
  // initialize count-count of args in args array
  int count = 0;

  // parse each token individually
  while (token != NULL) {
    // check for $$ variable expansion
    if (strstr(token, "$$") != NULL) {
      tmpToken = expand(token);
      command->args[i] = tmpToken;
      // free(tmpToken);
      token = strtok(NULL, delimeter);
      i++;
      count++;
      continue;
    }
    
    // check if token points to infile redirect
    if (token[0] == '>') {
      token = strtok(NULL, delimeter);
      command->outfile = token;
      token = strtok(NULL, delimeter);
      continue;
    }

    // check if token points to outfile redirect
    else if (token[0] == '<') {
      token = strtok(NULL, delimeter);
      command->infile = token;
      token = strtok(NULL, delimeter);
      continue;
    }

    // check for background flag 
    else if (token[0] == '&') {
      token = strtok(NULL, delimeter);

      // check if we are in foreground mode only or not
      if (token == NULL && !backgroundAllowed) {
        command->isbackground = 0;
        break;
      } else if (token == NULL && backgroundAllowed) {
        command->isbackground = 1;
        break;
      }
      else {
        command->args[i] = "&";
        i++;
        count++;
        continue;
      }
      
    }

    // if token is a regular arg, then add it to the args array
    else {
      command->args[i] = token;
    }

    // get next word from usrInput
    token = strtok(NULL, delimeter);
    i++;
    count++;
  }

  // check for built in cd command
  if (strcmp(command->args[0], "cd\n") == 0 || (strcmp(command->args[0], "cd") == 0)) {
    ret = 3;
    return ret;
  }
  command->args[count] = NULL;
  return ret;
  }

/**
* Function expand
* replaces every instance of the substring "$$" with the pid
* @param token a char* to the string containing the $$
* @return returns a pointer to the token with the expanded variable 
*/
char *expand(char* token) {
  char* ret;
  char* pos;
  char* tmp;
  pos = token;

  // count how many instances of "$$" are in the token
  int c;
  for (c = 0; tmp = strstr(pos, "$$"); ++c) {
    pos = tmp + 2;
  }
  // allocate memory for the new string
  tmp = ret = malloc(strlen(token) + 4*c+1);
  int gap;

  // pos marks position of first occurence of $
  // gap is distance between $ and the position of * to token
  while (c--) {
    pos = strstr(token, "$$");
    gap = pos-token;
    // copy gap number of chars from token into tmp
    // increment tmp* gap number of times
    tmp = strncpy(tmp, token, gap) + gap;
    // convert the pid to a string
    char pidBuff[16];
    char* pidString;
    pidString = itoa_safe(getpid(), pidBuff);
    // copy pid into where tmp is pointing (end of new string) and increment tmp
    tmp = strcpy(tmp, pidString) + strlen(pidString);
    //move token pointer passed this instance of "$$"
    token += gap + 2;
  }
  // copy any trailing chars into tmp and return
  strcpy(tmp, token);
  return ret;
}

/**
* Function runCommand
* forks a child process and executes the command passed as a parameter
* @param command a structure that contains the command, any arugments,
*                any in/out file redirection, and whether this command
*                should be run in the background or foreground
*/
void runCommand(struct cmd* command) {
  // initialize SIGCHILD_action struct to handle SIGCHLD signal
  struct sigaction SIGCHILD_action;
  memset (&SIGCHILD_action, 0, sizeof (SIGCHILD_action));
  SIGCHILD_action.sa_handler = &signalChildHandler;
  sigaction (SIGCHLD, &SIGCHILD_action, NULL);

  // declare a variable for the child pid and fork()
  pid_t child_pid;
  child_pid = fork();
  if (child_pid < 0) {
    perror("fork() failed\n");
    fflush(stderr);
    exit(1);
  }

  else if (child_pid == 0) {
    // ***IN THE CHILD PROCESS***
    // set up a new sigaction structure for child process to handle ctrl C
    // listen for ctrl C default signal if foreground process
    if (!command->isbackground) {
    struct sigaction SIGINT_child_action = {0};
    SIGINT_child_action.sa_handler = SIG_DFL;
    SIGINT_child_action.sa_flags = 0;
    sigaction(SIGINT, &SIGINT_child_action, NULL);
    }

    int ifd = 0;
    int ofd = 0;

    // check for input & output redirection
    // redirect input 
    if (command->infile) {
    if ((ifd = open(command->infile, O_RDONLY)) == -1) {
      fprintf(stderr, "unable to open file: %s\n", command->infile);
      fflush(stderr);
      exitStatus = 1;
      exit(1);
    }
    dup2(ifd, 0); 
    fcntl(ifd, F_SETFD, FD_CLOEXEC);
  }

    // redirect output
    if (command->outfile) {
      if ((ofd = open(command->outfile, O_WRONLY | O_CREAT | O_TRUNC, 0666)) == -1) {
        fprintf(stderr, "unable to open file: %s", command->outfile);
        fflush(stderr);
        exitStatus = 1;
        exit(1);
      }
      dup2(ofd, 1);
      fcntl(ofd, F_SETFD, FD_CLOEXEC);
    }

    // if this is a background process and user did not redirect i/o
    // send input to /dev/null and output to /dev/null
    if (command->isbackground) {
      if (ifd == 0) {
        ifd = open("/dev/null", O_RDONLY);
        dup2(ifd, 0); 
        fcntl(ifd, F_SETFD, FD_CLOEXEC);
      }
      if (ofd == 0) {
        ofd = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        dup2(ofd, 1); 
        fcntl(ofd, F_SETFD, FD_CLOEXEC);
      }
    }

    // execute the command with the given arguments from our parsed userInput
    if (execvp(command->args[0], command->args) < 0) {
      if (ofd) {
        close(ofd);
      }
      if (ifd) {
        close(ifd);
      }

      // if the command fails, print an error message
      fprintf(stderr, "%s is not a valid command\n", command->args[0]);
      fflush(stderr);
      perror("execvp");
      fflush(stderr);
      exitStatus = 1;
      exit(0);
    } 
  }

  // wait for pid of foreground process and catch termination signal
  if (!command->isbackground) {
    child_pid = waitpid(child_pid, &exitStatus, 0);
    if (WIFSIGNALED(exitStatus)) {
      printf("terminated by signal %d\n", WTERMSIG(exitStatus));
      fflush(stdout);
    }
  }
  else {
    // if this is a background command-print pid
    if (command->isbackground) {
      pid_t backgroundPid = waitpid(child_pid, &exitStatus, WNOHANG);
      printf("background pid is %d\n", child_pid);
      fflush(stdout);
    }
  }
}

/**
* Function: changeDir
* built in cd function, changes to home directory with no args 
* changes user to directory of first arg if given
* @param command struct cmd* a struct built from parsed shell command 
*/
void changeDir(struct cmd* command) {
  if (command->args[1] == NULL) {
    chdir(getenv("HOME"));
  }
  // if chdir fails, print an error message, else change to input directory
  else if (chdir(command->args[1]) == -1) {
    fprintf(stderr, "directory %s does not exist\n", command->args[1]);
    fflush(stderr);
  }
}

/**
* Function: signalChildHandler
* this function watches for termination of background child processes and terminates them
* asynchronously. After termination, a message is output to inform user of the 
* exit status or signal that the process was terminated by as well as its PID
* @param sigNum the signal number received by the handler
*/
void signalChildHandler (int sigNum) {
  pid_t childPid;
  // wait for state change in child of the calling process
  // WNOHANG allows this function to be run "in the background" and returns
  // control to the user if no child has yet exited
  while ((childPid = waitpid(-1, &exitStatus, WNOHANG)) > 0) {
    if (WIFEXITED(exitStatus)) {     
      char bufForPid[16];
      char* pidStr;
      // convert pid (of child whose state changed) from int to string
      pidStr = itoa_safe(childPid, bufForPid);
      char bufForExitStatus[6];
      char* statStr;
      // convert exit status to a string
      statStr = itoa_safe(exitStatus, bufForExitStatus);
      
      // print message about what process has completed and its exit value
      write(STDOUT_FILENO, "background pid ", 15);
      write(STDOUT_FILENO, pidStr, strnlen(pidStr, 16));
      write(STDOUT_FILENO, " is done: exit value ", 21);
      write(STDOUT_FILENO, statStr, strnlen(statStr, 12));
      write(STDOUT_FILENO, "\n:", 2);
      fflush(stdout);
    }
    else if (WIFSIGNALED(exitStatus)) {
      char bufForPid[16] = {0};
      char* pidStr;
      // convert pid (of child whose state changed) from int to string
      pidStr = itoa_safe(childPid, bufForPid);
      // if a child process was stopped by signal, print corresponding message
      char bufForSigno[16] = {0};
      char* sigNo;
      sigNo = itoa_safe(exitStatus, bufForSigno);
      write(STDOUT_FILENO, "background pid ", 15);
      write(STDOUT_FILENO, pidStr, strnlen(pidStr, 16));
      write(STDOUT_FILENO, " terminated by signal ", 22);
      write(STDOUT_FILENO, sigNo, 8);
      write(STDOUT_FILENO, "\n:", 2);
      fflush(stdout);
    }
  }
}

/**
* Function: handle_SIGTSTP
* handles signal SIGTSTP, when user enters ctrlZ foreground mode is activated
* which means & is ignored and all commands are executed in foreground 
* ctrlZ again toggles foreground mode on/off
* @param sigNum the signal number received by the handler
*/
void handle_SIGTSTP(int sigNum) {
  char* message = "\nEntering foreground-only mode (& is now ignored)\n:";
  if (backgroundAllowed) {
    backgroundAllowed = 0;
    write(STDOUT_FILENO, message, 51);
    fflush(stdout);
  }
  else {
    char* message = "\nExiting foreground-only mode\n:";
    backgroundAllowed = 1;
    write(STDOUT_FILENO, message, 32);
    fflush(stdout);
  }
}

/**
* Function: itoa_safe
* converts an int to a string
* @param i integer-int to be converted 
* @param buf char array-converted string
* return char* to the converted string 
*/
char *itoa_safe(int i, char buf[]) {
  char const digits[] = "0123456789";

    // initialize a pointer to the buffer
    char* p = buf;
    if(i<0){
        *p++ = '-';
        i *= -1;
    }

    // create enough spaces in the buffer to fit the entire number
    int shifter = i;
    do{ 
        ++p;
        shifter = shifter/10;
    }while(shifter);

    // get the last value of the int, add it to the buffer, decrement buffer
    // divide i by 10 each iteration to remove the last value off of i
    *p = '\0';
    do{ 
        *--p = digits[i%10];
        i = i/10;
    }while(i);
    return buf;
}