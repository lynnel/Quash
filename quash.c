
/*
  File: quash.c
  Authors: Roxanne Calderon & Lynne Coblammers
  EECS 678 Project 1
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

int getCommand(char*** cmd, int* numArgs);
int splitCommand(char** cmd, char*** unpiped[], int* numCmds, char* separator);

int execCommand(char** cmd, int numArgs, char** envp); 
int execSimpleCommand(char** cmd, char** envp);
int execPipedCommand(char*** cmdSet, int numCmds, char** envp);
int execRedirectedCommand(char** cmd, int numArgs, char redirectSym, char** envp);
int execBackgroundCommand(char** cmd, char** envp);
int execQuashFromFile(char** argv, int argc, char** envp);
void exitChildHandler(int signal);

int cd(char** args);
int jobs();
int set(char** args);

struct job {
	int pid;
	int jobid;
	char* bgcommand; 
	int finishedFlag; 
} ;
struct job jobArray[1000]; 
int jobCount = 0; 

int main(int argc, char* argv[], char** envp)
{
  if (argc > 1) {
    // arguments to quash, check if redirect
    int redirectInFlag = 0;
    int i = 0;
    while (argv[i] != 0) {
      if (strcmp(argv[i], "<") == 0) {
        redirectInFlag = 1;
      } 
      i++;
    }
    if (redirectInFlag == 0) {
      fprintf(stderr, "\nUsage: quash < \"<file.ext>\"\n");
      return -1;
    }
    execQuashFromFile(argv, argc, envp);
  }

  int numArgs = 1;
  int ret = 0;
  char cwd[1024]; 
  
  while (1) {
    if (getcwd(cwd, sizeof(cwd)) != 0) {
      printf(cwd);
      printf(" > "); 
    }	
    else {
	   perror("getcwd error: "); 
    }
    
    // read in input 
    char** cmd = malloc(numArgs * sizeof(char*));
    if (!cmd) {
      fprintf(stderr, "\nAllocation error\n, Error:%d\n", errno);
      return -1;
    }
    if (getCommand(&cmd, &numArgs) != 0) {
      // error getting command
      continue;
    }

    // determine command type
    if (strcmp(cmd[0], "exit") == 0 || strcmp(cmd[0], "quit") == 0) {
      // free memory and exit
      int i = 0;
      while (cmd[i] != 0) {
        free(cmd[i]);
        i++;
      }
      free(cmd);
      return 0;
    }
    else if (strcmp(cmd[0], "cd") == 0) {
      ret = cd(cmd);
    }
    else if (strcmp(cmd[0], "jobs") == 0) {
      ret = jobs();
    }
    else if (strcmp(cmd[0], "set") == 0) {
      ret = set(cmd);
    }
    else {
      ret = execCommand(cmd, numArgs, envp);
    }

    // free memory before getting next command
    int i = 0;
    while (cmd[i] != 0) {
      free(cmd[i]);
      i++;
    }
    free(cmd);
  }
}

int execQuashFromFile(char** argv, int argc, char** envp)
{
  int fd = open(argv[argc - 1], O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd < 0) {
    fprintf(stderr, "\nError opening %s. Error#%d\n", argv[argc - 1], errno);
    exit(EXIT_FAILURE);
  }
  if (dup2(fd, STDIN_FILENO) < 0) {
    fprintf(stderr, "\nError resetting stdin to %s. Error#%d\n", argv[argc -1], errno);
    exit(EXIT_FAILURE);
  }
  char** cmd = malloc(argc * sizeof(char*));
  if (!cmd) {
    fprintf(stderr, "\nAllocation error\n, Error:%d\n", errno);
    return -1;
  }

  return 0;
}

/*
  Reads input and parses into a command & its arguments
  @param cmd: [in/out] preallocated char** of size numArgs, 
    will hold command and its args on return with NULL as the final value
  @param numArgs: [in] number of args in cmd 
  @return: 0 if command was successfully read in, non-zero for error
*/
int getCommand(char*** cmd, int* numArgs)
{
  int cmdLength = 128;
  char* unparsedCmd = malloc(cmdLength * sizeof(char));
  if (!unparsedCmd) {
    fprintf(stderr, "\ngetCommand allocation error, Error:%d\n", errno);
    return -1;
  }
  
  int index = 0;
  int c;
  // read in command
  do {
    c = getchar();
    if (c == EOF || c == '\n') {
      if (index == 0) {
	       // no command was entered
	       return 1;
      }
      // reached end of input, append null
      unparsedCmd[index] = '\0';
      break;
    }
    // add character to command
    unparsedCmd[index] = c;
    
    index++;
    if (index >= cmdLength) {
      // allocate more space for the buffer
      cmdLength += cmdLength;
      unparsedCmd = realloc(unparsedCmd, cmdLength * sizeof(char));
      if (!unparsedCmd) {
        // error in reallocation
        fprintf(stderr, "\ngetCommand allocation error, Error:%d\n", errno);
        return -1;
      }
    }
  } while (1);

  // parse command into invidual arguments
  int argNum = 0;
  char* arg = strtok(unparsedCmd, " ");
  while (arg != 0) {
    // allocate memory for string storage
    (*cmd)[argNum] = malloc((strlen(arg) + 1) * sizeof(char));
    if (!((*cmd)[argNum])) {
      fprintf(stderr, "\ngetCommand allocation error, Error:%d\n", errno);
      return -1;
    }
    memset((*cmd)[argNum], '\0', (strlen(arg) + 1));
    // NOTE: need to copy because unparsedCmd goes out of scope
    strcpy((*cmd)[argNum], arg);
    argNum++;
    if (argNum >= *numArgs) {
      // need to reallocate, double size
      *numArgs *= 2;
      *cmd = realloc(*cmd, (*numArgs) * sizeof(char*));
      if (!(*cmd)) {
        // error in reallocations
        fprintf(stderr, "\ngetCommand allocation error, Error:%d\n", errno);
        return -1;
      }
    }

    arg = strtok(0, " ");
  }
  // add one last null pointer
  (*cmd)[argNum] = 0;

  // set numArgs to be the actual number of arguments (not size of array)
  *numArgs = argNum;

  free(unparsedCmd);

  return 0;
}

/*
  Splits command into several command vectors around separator provided
  @param cmd: [in] command to remove pipes from
  @param separated: [out] array of command vectors
  @param numCmds: [out] number of separate commands
  @param separator: [in] symbol to separate commands by (e.g. |, <, >)
  @return: 0 for success
*/
int splitCommand(char** cmd, char*** separated[], int* numCmds, char* separator)
{
  int lastIndex = 0; // keeps track of beginning of last command copied
  int index = 0; // keeps track of current place in original command
  int cmdVector = 0; 
  (*numCmds) = 0;
  // read each argument of command
  while (cmd[index] != 0) {
    if (strcmp(cmd[index], separator) == 0) {
      // allocate memory for command
      (*separated)[cmdVector] = malloc(((index - lastIndex) + 1) * sizeof(char*));
      if (!((*separated)[cmdVector])) {
        fprintf(stderr, "\nsplitCommand allocation error, Error:%d\n", errno);
        return -1;
      }
      memset((*separated)[cmdVector], '\0', ((index - lastIndex) + 1));

      int arg = 0;
      // copy each string up to pipe into command vector of separated
      for(; lastIndex < index; ++lastIndex, ++arg) {
        // allocate memory for string in command
        (*separated)[cmdVector][arg] = malloc((strlen(cmd[lastIndex]) + 1) * sizeof(char));
        if (!((*separated)[cmdVector][arg])) {
          fprintf(stderr, "\nsplitCommand allocation error, Error:%d\n", errno);
          return -1;
        }
        memset((*separated)[cmdVector][arg], '\0', (strlen(cmd[lastIndex]) + 1));
        // copy string 
        strcpy((*separated)[cmdVector][arg], cmd[lastIndex]);
      }
      (*separated)[cmdVector][arg] = 0;
      lastIndex = index + 1;
      cmdVector++;
      (*numCmds)++;
    }
    index++;
  }
  // copy last command
  // allocate memory for command
  (*separated)[cmdVector] = malloc((index - lastIndex + 1) * sizeof(char*));
  if (!((*separated)[cmdVector])) {
    fprintf(stderr, "\nsplitCommand allocation error, Error:%d\n", errno);
    return -1;
  }
  memset((*separated)[cmdVector], '\0', (index - lastIndex + 1));

  int arg = 0;
  // copy each string up to pipe into command vector of separated
  for(; lastIndex < index; ++lastIndex, ++arg) {
    // allocate memory for string in command
    (*separated)[cmdVector][arg] = malloc((strlen(cmd[lastIndex]) + 1) * sizeof(char));
    if (!((*separated)[cmdVector][arg])) {
      fprintf(stderr, "\nsplitCommand allocation error, Error:%d\n", errno);
      return -1;
    }
    memset((*separated)[cmdVector][arg], '\0', (strlen(cmd[lastIndex]) + 1));
    // copy string 
    strcpy((*separated)[cmdVector][arg], cmd[lastIndex]);
  }
  (*separated)[cmdVector][arg] = 0;
  (*numCmds)++;
  return 0;
}

/*
  Executes command by starting child process
  @param cmd: cmd with args to execute
  @param envp: array of environment variables to pass to command
  @return: 0 for success, non-zero for failure
*/
int execCommand(char** cmd, int numArgs, char** envp)
{
  // search command for pipes & redirects
  int redirectInFlag = 0;
  int redirectOutFlag = 0;
  int pipeFlag = 0;
  int bgFlag = 0; 
  int i = 0;
  while (cmd[i] != 0) {
    if (strcmp(cmd[i], "|") == 0) {
      pipeFlag = 1;
    }
    else if (strcmp(cmd[i], "<") == 0) {
      redirectInFlag = 1;
    }
    else if (strcmp(cmd[i], ">") == 0) {
      redirectOutFlag = 1;
    }
    else if (strcmp(cmd[i], "&") == 0) {
      bgFlag = 1;
    }
    i++;
  }

  int ret = 0;
  // we have access to numArgs here and this will be portable
  if (bgFlag) {
    // replace final & with null
    cmd[numArgs - 1] = 0;
    ret = execBackgroundCommand(cmd, envp);
  } 
  else if (pipeFlag) {
    // parse into pieces between pipes
    char*** unpipedCmds = malloc(numArgs * sizeof(char**));
    if (!unpipedCmds) {
      fprintf(stderr, "\nAllocation error\n, Error:%d\n", errno);
      return -1;
    }
    memset(unpipedCmds, '\0', numArgs * sizeof(char**));
    int numCmds;
    if (splitCommand(cmd, &unpipedCmds, &numCmds, "|") != 0) {
      fprintf(stderr, "\nError in splitCommand\n");
      return -1;
    }

    // call execPipedCommand with array of commands and number of commands
    ret = execPipedCommand(unpipedCmds, numCmds, envp);

    // free up memory
    int i = 0;
    while (unpipedCmds[i] != 0) {
      int j = 0;
      while (unpipedCmds[i][j] != 0) {
        free(unpipedCmds[i][j]);
        j++;
      }
      free(unpipedCmds[i]);
      i++;
    }
    free(unpipedCmds);
  }
  else if (redirectInFlag || redirectOutFlag) {
    if (redirectInFlag == 1) {
      ret = execRedirectedCommand(cmd, numArgs, '<', envp);
    }
    else {
      // redirecting out
      ret = execRedirectedCommand(cmd, numArgs, '>', envp);
    }
  }
  else {
    ret = execSimpleCommand(cmd, envp);
  }
  return ret;
}
  
int execSimpleCommand(char** cmd, char** envp)
{
  int status;
  pid_t pid;

  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "\nError forking child. Error:%d\n", errno);
    exit(EXIT_FAILURE);
  }
  if (pid == 0) {
    // child process
    #ifdef __linux__
    if (execvpe(cmd[0], cmd, envp) < 0) {
    #endif
    #ifdef __APPLE__
    if (execvP(cmd[0], getenv("PATH"), cmd) < 0) {
    #endif
      if (errno == 2) {
        fprintf(stderr, "\n%s not found.\n", cmd[0]);
      }
      else {
        fprintf(stderr, "\nError execing %s. Error#%d\n", cmd[0], errno);
      }
      exit(EXIT_FAILURE);
    }
    exit(0);
  }
  else {
    // parent process
    if (waitpid(pid, &status, 0) < 0) {
      fprintf(stderr, "\nError in child process %d. Error#%d\n", pid, errno);
      return 1;
    }
    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) == EXIT_FAILURE) {
        return 2;
      }
    }
    return 0;
  }
}
/*
  Executes command containing one or more pipes
  @param cmdSet: array of command vectors to execute
  @param numCmds: number of total piped commands
  @param envp: environment variables
  @return: 0 for success
*/
int execPipedCommand(char*** cmdSet, int numCmds, char** envp)
{
  int status;
  int numPipes = numCmds - 1;
  pid_t pids[numCmds];
  // create all pipes
  int pipefds[numPipes * 2];
  int i = 0;
  for (; i < numPipes; ++i) {
    if (pipe(pipefds + (i * 2)) < 0) {
      fprintf(stderr, "\nError creating pipe %d. Error:%d\n", (i * 2), errno);
      return -1;
    }
  }

  // fork all child processes
  int j = 0;
  for (; j < numCmds; ++j) {
    pids[j] = fork();
    if (pids[j] < 0) {
      fprintf(stderr, "\nError forking child %d. Error:%d\n", j, errno);
      exit(EXIT_FAILURE);
    }
    else if (pids[j] == 0) {
      // if not first command, set up input pipe
      if (j != 0) {
        if (dup2(pipefds[(j - 1) * 2], STDIN_FILENO) < 0) {
          fprintf(stderr, "\nError setting stdin to pipe %d. Error:%d\n", ((j - 1) * 2), errno);
          exit(EXIT_FAILURE);
        }
      }
      // if not last command, set up output pipe
      if (j != numCmds - 1) {
        if (dup2(pipefds[(j * 2) + 1], STDOUT_FILENO) < 0) {
          fprintf(stderr, "\nError setting stdout to pipe %d. Error:%d\n", ((j * 2) + 1), errno);
          exit(EXIT_FAILURE);
        }
      }
      // close all pipes
      i = 0;
      for (; i < numPipes * 2; ++i) {
        close(pipefds[i]);

      }
      // execute command
      #ifdef __linux__
      if (execvpe(cmdSet[j][0], cmdSet[j], envp) < 0) {
      #elif __APPLE__
      if (execvP(cmdSet[j][0], getenv("PATH"), cmdSet[j]) < 0) {
      #endif
        if (errno == 2) {
          fprintf(stderr, "\n%s not found.\n", cmdSet[j][0]);
        }
        else {
          fprintf(stderr, "\nError execing %s. Error#%d\n", cmdSet[j][0], errno);
        }
        exit(EXIT_FAILURE);
      }
      printf("Process %d exiting", (j + 1));
      exit(0);
    }
  }

  // close all pipes
  i = 0;
  for (; i < numPipes * 2; ++i) {
    close(pipefds[i]);
  }

  // wait for all children
  i = 0;
  for (; i < numCmds; ++i) {
    if (waitpid(pids[i], &status, 0) < 0) {
      fprintf(stderr, "\nError in child process %d. Error#%d\n", pids[i], errno);
      return -1;
    }
  }
  return 0;
}

/*
  Executes command with redirect
  @param cmd: command to execute, including redirect & file
  @param envp: environment variables
  @param redirectSym: either < or >
*/
int execRedirectedCommand(char** cmd, int numArgs, char redirectSym, char** envp)
{
  int status;
  int fd;
  pid_t pid;

  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "\nError creating pipe. Error:%d\n", errno);
    return -1;
  }
  if (pid == 0) {
    // child process
    // redirect input or output as needed
    if (redirectSym == '<') {
      fd = open(cmd[numArgs - 1], O_RDONLY, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (fd < 0) {
        fprintf(stderr, "\nError opening %s. Error#%d\n", cmd[numArgs - 1], errno);
        exit(EXIT_FAILURE);
      }
      if (dup2(fd, STDIN_FILENO) < 0) {
        fprintf(stderr, "\nError resetting stdin to %s. Error#%d\n", cmd[numArgs -1], errno);
        exit(EXIT_FAILURE);
      }
    }
    else {
      fd = open(cmd[numArgs - 1], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
      if (fd < 0) {
        fprintf(stderr, "\nError opening %s. Error#%d\n", cmd[numArgs - 1], errno);
        exit(EXIT_FAILURE);
      }
      if (dup2(fd, STDOUT_FILENO) < 0) {
        fprintf(stderr, "\nError resetting stdout to %s. Error#%d\n", cmd[numArgs -1], errno);
        exit(EXIT_FAILURE);
      }
    }
    close(fd);
    // copy command up to redirect
    cmd = realloc(cmd, (numArgs - 1) * sizeof(char*));
    cmd[numArgs - 2] = 0;

    #ifdef __linux__
    if (execvpe(cmd[0], cmd, envp) < 0) {
    #endif
    #ifdef __APPLE__
    if (execvP(cmd[0], getenv("PATH"), cmd) < 0) {
    #endif
      if (errno == 2) {
        fprintf(stderr, "\n%s not found.\n", cmd[0]);
      }
      else {
        fprintf(stderr, "\nError execing %s. Error#%d\n", cmd[0], errno);
      }
      exit(EXIT_FAILURE);
    }
    exit(0);
  }
  else {
    // parent process
    if (waitpid(pid, &status, 0) == -1) {
      fprintf(stderr, "\nError in child process %d. Error#%d\n", pid, errno);
      return -1;
    }
    if (WIFEXITED(status)) {
      if (WEXITSTATUS(status) == EXIT_FAILURE) {
        return -1;
      }
    }
    return 0;
  }
}

int execBackgroundCommand(char** cmd, char** envp)
{
  int status;
  pid_t pid;

  pid = fork();
  if (pid < 0) {
    fprintf(stderr, "\nError forking child. Error:%d\n", errno);
    exit(EXIT_FAILURE);
  }
  if (pid == 0) {
    // child process
    printf("\n[%d]%d  running in background\n",jobCount , getpid()); 
    execSimpleCommand(cmd, envp); 
   // kill(getpid(),0); 
    printf("[%d]%d finished %s\n", jobArray[jobCount].jobid, getpid(), cmd[0]); 

	exit(0);
  }
  else {
    struct job newjob;
    newjob.pid = pid; 
    newjob.jobid = jobCount;
    newjob.bgcommand = (char *) malloc(100);
    strcpy(newjob.bgcommand, cmd[0]);
    newjob.finishedFlag = 0; 
    jobArray[jobCount] = newjob;
    jobCount++;
    while (waitpid(pid, &status, WEXITED | WNOHANG) > 0){} 
    signal(SIGCHLD,exitChildHandler);

   return 0;
  } 
  
}
  
//i think this changes it i am not sure. 
int cd(char** args) 
{
  if (args[1] == '\0') {
    char* home = getenv("HOME");
	  chdir(home); 
  } 
  else { 
	  if (chdir(args[1])!= 0) {
    	  	printf("cd: %s: No such file or directory\n", args[1]); 
	  }
    return 1;
  } 
  return 0;
}

void exitChildHandler(int signal){
     jobArray[jobCount - 1].finishedFlag = 1;
    printf("%d \n",jobArray[jobCount - 1].finishedFlag); 

}

int jobs() 
{
	int i;
  for (i = 0; i < jobCount; i++) {
	if(jobArray[i].finishedFlag == 0){
		printf("[%d] %d %s \n", jobArray[i].jobid, jobArray[i].pid, jobArray[i].bgcommand); 
}
  }
  return 0;
}

/*
  Prints or sets PATH & HOME variables
  @param args: command from commandline
  @return: 0 if successful

  Note: if set is called with no additional
  args, path and home will be printed
*/
int set(char** args) 
{
  if (args[1] == NULL) {
    // print home & path environment variables
    char* path = getenv("PATH");
    char* home = getenv("HOME");
    printf("PATH:%s\n", path);
    printf("HOME:%s\n", home);
  }
  else {
    // figure out whether setting path or home
    char* variable = strtok(args[1], "=");
    if (variable == NULL) {
      fprintf(stderr, "Usage: set <envVariable>=<newValue>\n");
      return 1;
    }
    else if (strcmp(variable, "PATH") == 0) {
      char* newPath = strtok(NULL, "=");
      if (newPath == NULL) {
        fprintf(stderr, "Usage: set <envVariable>=<newValue>\n");
        return 1;
      }
      setenv(variable, newPath, 1);
    }
    else if (strcmp(variable, "HOME") == 0) {
      char* newHome = strtok(NULL, "=");
      if (newHome == NULL) {
                fprintf(stderr, "Usage: set <envVariable>=<newValue>\n");
        return 1;
      }
      setenv(variable, newHome, 1);
    }
    else {
      fprintf(stderr, "set can be used for PATH or HOME\n");
      return 1;
    }
  }
  return 0;
}
