/***************************************************************************
 *  Title: Runtime environment 
 * -------------------------------------------------------------------------
 *    Purpose: Runs commands
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.1 $
 *    Last Modification: $Date: 2006/10/13 05:24:59 $
 *    File: $RCSfile: runtime.c,v $
 *    Copyright: (C) 2002 by Stefan Birrer
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: runtime.c,v $
 *    Revision 1.1  2005/10/13 05:25:59  sbirrer
 *    - added the skeleton files
 *
 *    Revision 1.6  2002/10/24 21:32:47  sempi
 *    final release
 *
 *    Revision 1.5  2002/10/23 21:54:27  sempi
 *    beta release
 *
 *    Revision 1.4  2002/10/21 04:49:35  sempi
 *    minor correction
 *
 *    Revision 1.3  2002/10/21 04:47:05  sempi
 *    Milestone 2 beta
 *
 *    Revision 1.2  2002/10/15 20:37:26  sempi
 *    Comments updated
 *
 *    Revision 1.1  2002/10/15 20:20:56  sempi
 *    Milestone 1
 *
 ***************************************************************************/
#define __RUNTIME_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>

/************Private include**********************************************/
#include "runtime.h"
#include "io.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

/************Global Variables*********************************************/

#define NBUILTINCOMMANDS (sizeof BuiltInCommands / sizeof(char*))

/************ Job List *********************************************/
jobT *head = NULL;
jobT *tail = NULL;

// Counter to keeo track of the number of jobs
int job_counter = 0;

/************Function Prototypes******************************************/
/* run command */
static void RunCmdFork(commandT*, bool);
/* runs an external program command after some checks */
static void RunExternalCmd(commandT*, bool);
/* resolves the path and checks for exutable flag */
static bool ResolveExternalCmd(commandT*);
/* forks and runs a external program */
static void Exec(commandT*, bool);
/* runs a builtin command */
static void RunBuiltInCmd(commandT*);
/* checks whether a command is a builtin command */
static bool IsBuiltIn(char*);
/************External Declaration*****************************************/

/**************Implementation***********************************************/
int total_task;
void RunCmd(commandT** cmd, int n)
{
  int i;
  total_task = n;
  if(n == 1)
    if (cmd[0]->is_redirect_in && cmd[0]->is_redirect_out){
      RunCmdRedir(cmd);
    }
    else if (cmd[0]->is_redirect_in){
      RunCmdRedirIn(cmd[0], cmd[0]->redirect_in);
    }
    else if (cmd[0]->is_redirect_out){
      RunCmdRedirOut(cmd[0], cmd[0]->redirect_out);
    }
    else {
      RunCmdFork(cmd[0], TRUE);
    }
  else{
    RunCmdPipe(cmd[0], cmd[1]);
    for(i = 0; i < n; i++)
      ReleaseCmdT(&cmd[i]);
  }
}

void RunCmdFork(commandT* cmd, bool fork)
{
  if (cmd->argc<=0)
    return;
  if (IsBuiltIn(cmd->argv[0]))
  {
    RunBuiltInCmd(cmd);
  }
  else
  {
    RunExternalCmd(cmd, fork);
  }
}

/***************************************************************************************************************
******** Redirect Functions ************************************************************************************
***************************************************************************************************************/

void RunCmdRedirOut(commandT* cmd, char* file) {
  // Save the state of the stdout into the output
  int output = dup(1);

  // Get the file descriptor for the file to be outputted
  int fd = open(file, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd < 0)
    return;

  // Tell the system to write to the file for stdout
  dup2(fd,1);
  RunCmdFork(cmd, TRUE);

  // Restore the stdout to the screen
  dup2(output,1);
  close(fd);
}

void RunCmdRedirIn(commandT* cmd, char* file) {
  // Save the state of the stdin into the input
  int input = dup(0);

  // Get the file descriptor for the file to be inputted
  int fd = open(file, O_RDONLY);
  if (fd < 0)
    return;

  // Tell the system to read from the file for stdin
  dup2(fd, 0);
  RunCmdFork(cmd, TRUE);

  // Restore the stdin to the keyboard
  dup2(input, 0);
  close(fd);
}

void RunCmdRedir(commandT** cmd) {
  // Save the state of the stdin and the stdout before we do redirection
  int input = dup(0);
  int output = dup(1);

  // Get the file file descriptors for the input and output files
  int fd_in = open(cmd[0]->redirect_in, O_RDONLY);
  int fd_out =  open(cmd[0]->redirect_out, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
  if (fd_in < 0 || fd_out < 0)
    return;

  // Tell the system to use the files for the stdin and the stdout
  dup2(fd_in, 0);
  dup2(fd_out, 1);
  RunCmdFork(cmd[0], TRUE);

  // Restore the original states of the stdin and the stdout
  dup2(input, 0);
  dup2(output, 1);
  close(fd_in);
  close(fd_out);
}

/*Try to run an external command*/
static void RunExternalCmd(commandT* cmd, bool fork)
{
  if (ResolveExternalCmd(cmd)){
    Exec(cmd, fork);
  }
  else {
    printf("%s: command not found\n", cmd->argv[0]);
    fflush(stdout);
    ReleaseCmdT(&cmd);
  }
}

void RunCmdPipe(commandT *cmd1, commandT *cmd2) {

}

/*Find the executable based on search list provided by environment variable PATH*/
static bool ResolveExternalCmd(commandT* cmd)
{
  char *pathlist, *c;
  char buf[1024];
  int i, j;
  struct stat fs;

  if(strchr(cmd->argv[0],'/') != NULL){
    if(stat(cmd->argv[0], &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(cmd->argv[0],X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(cmd->argv[0]);
          return TRUE;
        }
    }
    return FALSE;
  }
  pathlist = getenv("PATH");
  if(pathlist == NULL) return FALSE;
  i = 0;
  while(i<strlen(pathlist)){
    c = strchr(&(pathlist[i]),':');
    if(c != NULL){
      for(j = 0; c != &(pathlist[i]); i++, j++)
        buf[j] = pathlist[i];
      i++;
    }
    else{
      for(j = 0; i < strlen(pathlist); i++, j++)
        buf[j] = pathlist[i];
    }
    buf[j] = '\0';
    strcat(buf, "/");
    strcat(buf,cmd->argv[0]);
    if(stat(buf, &fs) >= 0){
      if(S_ISDIR(fs.st_mode) == 0)
        if(access(buf,X_OK) == 0){/*Whether it's an executable or the user has required permisson to run it*/
          cmd->name = strdup(buf); 
          return TRUE;
        }
    }
  }
  return FALSE; /*The command is not found or the user don't have enough priority to run.*/
}

/******************************************************************************************
(************* Exec Command ***************************************************************
******************************************************************************************/

static void Exec(commandT* cmd, bool forceFork) {
  pid_t pid;

  // Set the child signal to be blocked
  sigset_t signal_set;
  sigemptyset(&signal_set);
  sigaddset(&signal_set, SIGCHLD);
  sigprocmask(SIG_BLOCK, &signal_set, NULL);

  // Fork the process and exec the commands on the child process
  if ((pid = fork()) < 0) {
    perror("Fork error");
  } else {
    if (pid == 0) {
      // We are in the child process
      // Set the group id of the child and the parent to be the same
      setpgid(0, 0);
      sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
      if (execv(cmd->name, cmd->argv) < 0) {
        printf("Error running %s\n", cmd->argv[0]);
        fflush(stdout);
      }
    } else {
      int process_status;
      addJob(pid, cmd);
      sigprocmask(SIG_UNBLOCK, &signal_set, NULL);
      if (!cmd->bg) {
        // Add the job and then run the foreground process
        waitpid(pid, &process_status, WUNTRACED);

        // If the foreground process is stopped by the not the ctrl-z signal, we remove the job from our list
        if (!WIFSTOPPED(process_status)) {
          removeJob(pid);
        }
      }
    }
  }
}

/************ JOB FUNCTIONS *********************************************
    Includes Init, Add, Find, and Edit
*************************************************************************/

// Initialize the jobs by allocating space for the head and setting the 
// pointers to NULL which we will check later. Then set the tail to be equal 
// to the head meaning that we have an empty list. All of which will be used
// later to check for the completeness of this list.

void InitJobs() {
  head = (jobT *)malloc(sizeof(jobT));
  head->next = NULL;
  head->prev = NULL;
  tail = head;
}

/* Add the job with its process id and fill in the information of the job given at the command line
If the job is a background job then we will give it a job id, else we will set it to zero. */
void addJob(pid_t pid, commandT *cmd) {
  // printf("- [addJob] Job_counter = %d for <%s> at BEGIN\n", job_counter, cmd->cmdline);
  jobT *new_job = (jobT *)malloc(sizeof(jobT));
  new_job->pid = pid;
  new_job->bg = cmd->bg;
  new_job->cmdline = cmd->cmdline;
  new_job->status = RUNNING;
  new_job->next = NULL;
  if(cmd->bg){
    new_job->jid = ++job_counter;
  } else {
    new_job->jid = 0;
  }
  tail->next = new_job;
  new_job->prev = tail;
  tail = new_job;
  // printf("- [addJob] Job_counter = %d for <%s> at END\n", job_counter, cmd->cmdline);
}

// Iterate through the jobs and return the corresponding jobT * for the process id. NULL if not found
jobT* findJob(pid_t pid) {
  jobT *node = head->next;
  while (node != NULL) {
    if (node->pid == pid) {
      return node;
    }
    node = node->next;
  }
  return NULL;
}

// Edit the job's status. 
void editJob(pid_t pid, job_status status) {
  jobT *job = findJob(pid);
  job->status = status;
}

// Remove the job from out list
// If the list is empty, set the job counter to zero.
void removeJob(pid_t pid) {
  // printf("- [removeJob] Job_counter = %d at BEGIN\n", job_counter);
  jobT *job;
  if ((job = findJob(pid)) != NULL) {
    if (job->next != NULL) {
      job->prev->next = job->next;
      job->next->prev = job->prev;
      // printf("Head == tail : %d", (head == tail));
    } else {
      tail = job->prev;
      tail->next = NULL;
    }
    free(job);
  }
  if (head == tail) {
    job_counter = 0;
  }
  // printf("- [removeJob] Job_counter = %d at END\n", job_counter);
}


// Called from the jobs command to print the jobs and their corresponding status.
void printJobs() {
  jobT *node = head->next;
  while (node != NULL) {
    switch (node->status) {
      case (RUNNING):
        printf("[%d]   Running                %s &\n", node->jid, node->cmdline);
        break;
      case (STOPPED):
        printf("[%d]   Stopped                %s\n", node->jid, node->cmdline);
        break;
    }
    fflush(stdout);
    node = node->next;
  }
}

// Called continuously from the main loop to check on the status of the background jobs being processed.
void CheckJobs() {
  jobT *node = head->next;
  while (node != NULL) {
    int status;
    if (waitpid(node->pid, &status, WNOHANG)) {
      if(node->bg){
        printf("[%d]   Done                   %s\n", node->jid, node->cmdline);
        fflush(stdout);
      }
      removeJob(node->pid);
    }
    node = node->next;
  }
}


/*************************************************************************************/
/**** Signal Handler Functions *******************************************************/
/*************************************************************************************/

// This is the signal sent from the TSTP or ctrl-z input
// WE will send the foreground process to the background and stop it
// We had to be sure to increment the process if it didn't already have a 
// job id.
void fgStop() {
  // printf("- [fgStop] Job_counter = %d at BEGIN\n", job_counter);
  jobT* node = head->next;
  while (node != NULL) {
    if (!node->bg) {
      node->status = STOPPED;
      
      if(node->jid == 0){
        node->jid = ++job_counter;
      }
      kill(-node->pid, SIGTSTP);
      printf("[%d]   Stopped                %s\n", node->jid, node->cmdline);
      fflush(stdout);
      break;
    }
    node = node->next;
  }
  // printf("- [fgStop] Job_counter = %d at END\n", job_counter);
}

// Respond to the INT or ctrl-c signal by simply killing the foreground process
void fgInt() {
  jobT* node = head->next;
  while (node) {
    if (!node->bg) {
      kill(-node->pid, SIGINT);
      removeJob(node->pid);
    }
    node = node->next;
  }
}

/*************************************************************************************/
/**** fg and bg functions ************************************************************/
/*************************************************************************************/

// Send the specified job in the background to the foreground
void fg(int jid) {
  jobT *node = head->next;
  int status;
  while(node != NULL){
    if(node->jid == jid) {
      node->bg = 0;
      node->status = RUNNING;
      kill(-node->pid, SIGCONT);
      while(waitpid(node->pid, &status, WUNTRACED|WNOHANG) == 0){
        sleep(1);
      }
      return;
    }
    node = node->next;
  }
  printf("Job %d not found\n", jid);
}

// Do the same thing as the function above except send the continue signal and don't run it in the foreground.
void bg(int jid) {
  jobT *node = head->next;
  while(node != NULL){
    if(node->jid == jid) {
      node->status = RUNNING;
      kill(node->pid, SIGCONT);
      return;
    }
    node = node->next;
  }
  printf("Job %d not found\n", jid);
}


static bool IsBuiltIn(char* cmd)
{
    if(strcmp(cmd, "cd") == 0) {
    return TRUE;
  } else if (strcmp(cmd, "bg") == 0) {
    return TRUE;
  } else if (strcmp(cmd, "fg") == 0) {
    return TRUE;
  } else if (strcmp(cmd, "jobs") == 0) {
    return TRUE;
  }
  return FALSE;        
}


static void RunBuiltInCmd(commandT* cmd)
{
  if(strcmp(cmd->argv[0], "cd") == 0) {
    // FILL IN
  } else if(strcmp(cmd->argv[0], "bg") == 0) {
        if (cmd->argc == 2) {
      bg(atoi(cmd->argv[1]));
    } else {
      printf("Invalid number of arguments for bg\n");
      fflush(stdout);
    }
  } else if(strcmp(cmd->argv[0], "fg") == 0) {
    if (cmd->argc == 2) {
      fg(atoi(cmd->argv[1]));
    } else {
      printf("Invalid number of arguments for fg\n");
      fflush(stdout);
    }
  } else if(strcmp(cmd->argv[0], "jobs") == 0) {
      printJobs();
  }  
}


commandT* CreateCmdT(int n)
{
  int i;
  commandT * cd = malloc(sizeof(commandT) + sizeof(char *) * (n + 1));
  cd -> name = NULL;
  cd -> cmdline = NULL;
  cd -> is_redirect_in = cd -> is_redirect_out = 0;
  cd -> redirect_in = cd -> redirect_out = NULL;
  cd -> argc = n;
  for(i = 0; i <=n; i++)
    cd -> argv[i] = NULL;
  return cd;
}

/*Release and collect the space of a commandT struct*/
void ReleaseCmdT(commandT **cmd){
  int i;
  if((*cmd)->name != NULL) free((*cmd)->name);
  if((*cmd)->cmdline != NULL) free((*cmd)->cmdline);
  if((*cmd)->redirect_in != NULL) free((*cmd)->redirect_in);
  if((*cmd)->redirect_out != NULL) free((*cmd)->redirect_out);
  for(i = 0; i < (*cmd)->argc; i++)
    if((*cmd)->argv[i] != NULL) free((*cmd)->argv[i]);
  free(*cmd);
}
