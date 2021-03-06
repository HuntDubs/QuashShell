/**
 * @file execute.c
 *
 * @brief Implements interface functions between Quash and the environment and
 * functions that interpret an execute commands.
 *
 * @note As you add things to this file you may want to change the method signature
 */

#include "execute.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "quash.h"
#include "deque.h"

#define BSIZE 256
#define READ 0
#define WRITE 1

IMPLEMENT_DEQUE_STRUCT(pidQueue, pid_t);
IMPLEMENT_DEQUE(pidQueue, pid_t);
pidQueue pidq;
bool firstCommand = true;

typedef struct Job
{
    int jobID;
    pidQueue pidq;
    pid_t jpid;
    char* cmd;
} Job;

IMPLEMENT_DEQUE_STRUCT(jobQueue, struct Job);
IMPLEMENT_DEQUE(jobQueue, struct Job);
jobQueue jq;
int currentJID = 1;

static int pipes[2][2];

// Remove this and all expansion calls to it
/**
 * @brief Note calls to any function that requires implementation
 */
#define IMPLEMENT_ME()                                                  \
  fprintf(stderr, "IMPLEMENT ME: %s(line %d): %s()\n", __FILE__, __LINE__, __FUNCTION__)

/***************************************************************************
 * Interface Functions
 ***************************************************************************/
 char *get_current_dir_name(void);

// Return a string containing the current working directory.
char* get_current_directory(bool* should_free) {
  char* cwd;
  cwd = get_current_dir_name();
  return cwd;
}

// Returns the value of an environment variable env_var
const char* lookup_env(const char* env_var) {
  return getenv(env_var);
}

// Check the status of background jobs
void check_jobs_bg_status() {
  // TODO: Check on the statuses of all processes belonging to all background
  // jobs. This function should remove jobs from the jobs queue once all
  // processes belonging to a job have completed.
  //IMPLEMENT_ME();

  return;

  // TODO: Once jobs are implemented, uncomment and fill the following line
  // print_job_bg_complete(job_id, pid, cmd);
}

// Prints the job id number, the process id of the first process belonging to
// the Job, and the command string associated with this job
void print_job(int job_id, pid_t pid, const char* cmd) {
  printf("[%d]\t%8d\t%s\n", job_id, pid, cmd);
  fflush(stdout);
}

// Prints a start up message for background processes
void print_job_bg_start(int job_id, pid_t pid, const char* cmd) {
  printf("Background job started: ");
  print_job(job_id, pid, cmd);
}

// Prints a completion message followed by the print job
void print_job_bg_complete(int job_id, pid_t pid, const char* cmd) {
  printf("Completed: \t");
  print_job(job_id, pid, cmd);
}

/***************************************************************************
 * Functions to process commands
 ***************************************************************************/
// Run a program reachable by the path environment variable, relative path, or
// absolute path
void run_generic(GenericCommand cmd) {
  // Execute a program with a list of arguments. The `args` array is a NULL
  // terminated (last string is always NULL) list of strings. The first element
  // in the array is the executable
  char* exec = cmd.args[0];
  char** args = cmd.args;

  execvp(exec, args);

  perror("ERROR: Failed to execute program");
}

// Print strings
  //NOTE: This only works for one argument currently and will not print spaces.
void run_echo(EchoCommand cmd) {
  // Print an array of strings. The args array is a NULL terminated (last
  // string is always NULL) list of strings.
  char** str = cmd.args;

  for(int i =0; str[i] != NULL; i++){
    printf("%s", str[i]);
  }

  putchar('\n');

  //NOTE:


  // Flush the buffer before returning
  fflush(stdout);
}

// Sets an environment variable
void run_export(ExportCommand cmd) {
  // Write an environment variable
  const char* env_var = cmd.env_var;
  const char* val = cmd.val;
  setenv(env_var, val, 1);
}

// Changes the current working directory
void run_cd(CDCommand cmd) {
  // Get the directory name
  const char* oldDir = get_current_directory(false);
  const char* dir = cmd.dir;

  // Check if the directory is valid
  if (dir == NULL) {
    perror("ERROR: Failed to resolve path");
    return;
  }

  chdir(dir);
  setenv("OLD_PWD", oldDir, 1);
  setenv("PWD", dir, 1);
}

// Sends a signal to all processes contained in a job
void run_kill(KillCommand cmd) {
  int signal = cmd.sig;
  int job_id = cmd.job;

  pidQueue currentPIDQueue;
  pid_t currentPID;
  struct Job current;

  for(int i = 0; i < length_jobQueue(&jq); i++){
    current = pop_front_jobQueue(&jq);
    if(current.jobID == job_id){
      currentPIDQueue = current.pidq;
      while(length_pidQueue(&currentPIDQueue) != 0){
        currentPID = pop_front_pidQueue(&currentPIDQueue);
        kill(currentPID, signal);
      }
      push_back_jobQueue(&jq, current);
    }
  }

}


// Prints the current working directory to stdout
void run_pwd() {
  // TODO: Print the current working directory
  //char wd[1024];
  printf("%s\n", get_current_directory(false));

  // Flush the buffer before returning
  fflush(stdout);
}

// Prints all background jobs currently in the job list to stdout
void run_jobs() {
  // TODO: Print background jobs

Job curJob;

if(is_empty_jobQueue(&jq)){
  return;
}

for(int i = 0; i < length_jobQueue(&jq); i++){
  curJob = pop_front_jobQueue(&jq);
  print_job(curJob.jobID, peek_front_pidQueue(&curJob.pidq), curJob.cmd);
  push_back_jobQueue(&jq, curJob);
}

  // Flush the buffer before returning
  fflush(stdout);
}

/***************************************************************************
 * Functions for command resolution and process setup
 ***************************************************************************/

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for child processes.
 *
 * This version of the function is tailored to commands that should be run in
 * the child process of a fork.
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void child_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);
  switch (type) {
  case GENERIC:
    run_generic(cmd.generic);
    break;

  case ECHO:
    run_echo(cmd.echo);
    break;

  case PWD:
    run_pwd();
    break;

  case JOBS:
    run_jobs();
    break;

  case EXPORT:
  case CD:
  case KILL:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief A dispatch function to resolve the correct @a Command variant
 * function for the quash process.
 *
 * This version of the function is tailored to commands that should be run in
 * the parent process (quash).
 *
 * @param cmd The Command to try to run
 *
 * @sa Command
 */
void parent_run_command(Command cmd) {
  CommandType type = get_command_type(cmd);

  switch (type) {
  case EXPORT:
    run_export(cmd.export);
    break;

  case CD:
    run_cd(cmd.cd);
    break;

  case KILL:
    run_kill(cmd.kill);
    break;

  case GENERIC:
  case ECHO:
  case PWD:
  case JOBS:
  case EXIT:
  case EOC:
    break;

  default:
    fprintf(stderr, "Unknown command type: %d\n", type);
  }
}

/**
 * @brief Creates one new process centered around the @a Command in the @a
 * CommandHolder setting up redirects and pipes where needed
 *
 * @note Processes are not the same as jobs. A single job can have multiple
 * processes running under it. This function creates a process that is part of a
 * larger job.
 *
 * @note Not all commands should bld_run_command(holder.cmd); // e run in the child process. A few need to
 * change the quash process in some way
 *
 * @param holder The CommandHolder to try to run
 *
 * @sa Command CommandHolder
 */
void create_process(CommandHolder holder, int pipeEndIndex) {
  // Read the flags field from the parser
  bool p_in  = holder.flags & PIPE_IN;
  bool p_out = holder.flags & PIPE_OUT;
  bool r_in  = holder.flags & REDIRECT_IN;
  bool r_out = holder.flags & REDIRECT_OUT;
  bool r_app = holder.flags & REDIRECT_APPEND; // This can only be true if r_out
                                               // is true
    (void)r_in;
    (void)r_out;
    (void)r_app;

  int prevPipe = (pipeEndIndex - 1) % 2;
  int nextPipe = (pipeEndIndex) % 2;

  if (p_out){
    pipe(pipes[nextPipe]);
  }

  // TODO: Setup pipes, redirects, and new process
  pid_t newPID = fork();
  push_back_pidQueue(&pidq, newPID);

  if (newPID == 0){
    if(p_in){
      dup2(pipes[prevPipe][READ], STDIN_FILENO);
      close(pipes[prevPipe][READ]);
    }
    if (p_out){
      dup2(pipes[nextPipe][WRITE], STDOUT_FILENO);
      close(pipes[nextPipe][WRITE]);
    }

    if (r_in){
      FILE* f = fopen(holder.redirect_in, "r");
      dup2(fileno (f), STDIN_FILENO);
    }

    if (r_out){
      if (r_app){
        FILE* f = fopen (holder.redirect_out, "a");
        dup2(fileno (f), STDOUT_FILENO);
      } else {
        FILE* f = fopen (holder.redirect_out, "w");
        dup2(fileno (f), STDOUT_FILENO);
      }
    }

    child_run_command(holder.cmd); // This should be done in the child branch of a fork
    exit(0);
  } else {
    if (p_out){
      close(pipes[nextPipe][WRITE]);
    }
    parent_run_command(holder.cmd); // This should be done in the parent branch of
                                    // a fork
    pipeEndIndex++;
  }



}

// Run a list of commands
void run_script(CommandHolder* holders) {
  if (holders == NULL)
    return;

  if (firstCommand){
     jq = new_jobQueue(0);
     firstCommand = false;
  }

  check_jobs_bg_status();

  if (get_command_holder_type(holders[0]) == EXIT && get_command_holder_type(holders[1]) == EOC) {
    end_main_loop();
    return;
  }

  pidq = new_pidQueue(0);
  CommandType type;

  // Run all commands in the `holder` array
  for (int i = 0; (type = get_command_holder_type(holders[i])) != EOC; ++i)
    create_process(holders[i], i);

  if (!(holders[0].flags & BACKGROUND)) {
    // Not a background Job
    while(!is_empty_pidQueue(&pidq)){
      pid_t tempPID = pop_front_pidQueue(&pidq);
      int status;
      waitpid(tempPID, &status, 0);
    }
    destroy_pidQueue(&pidq);


  }
  else {
    // A background job.
    Job newJob;
    newJob.jobID = currentJID;
    currentJID++;
    newJob.pidq = pidq;
    newJob.cmd = get_command_string();
    if (!is_empty_pidQueue(&pidq)){
      newJob.jpid = peek_back_pidQueue(&pidq);
    } else {
      fprintf(stderr, "No Process ID Delivered\n");
    }
    push_back_jobQueue(&jq, newJob);

    print_job_bg_start(newJob.jobID, newJob.jpid, newJob.cmd);

  }
}
