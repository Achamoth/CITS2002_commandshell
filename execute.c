#include "mysh.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdbool.h>

int last_exitstatus; //Stores exit status of last command
char *dirs[100]; //Assume no more than 100 directories in either PATH or CDPATH
char infile[250]; //Largest file name allowed by Mac OSC ~ 250 characters
char outfile[250];
int pfd[100][2]; //File descriptors on pipe (assumes no more than 100 pipes will be needed in any sequence of commands)
int npipe; //The current pipe we are using (in cases where a sequence of commands requests a pipeline of multiple pipes be established)
bool previous_type_was_pipe; //This is used to determine when the last node was of type N_PIPE, and has no other use. If the last node was of type N_PIPE, then the current command must pipe in



/*
    CITS2002 Project 2 2015
    Names: Ammar Abu Shamleh, George Krlevski
    Student numbers: 21521274, 21522548

   Date:		28/10/15
 */

// -------------------------------------------------------------------

// Checks if the command contains a slash
bool hasSlash(char *cmd){
    if(strchr(cmd, '/') == NULL) return false;
    return true;
}


//Split PATH by ':' character and store each directory in array; returns dynamically allocated array of directories
void path_split(char *path)
{
    //First, clear 'dirs'
    for(int i=0; i<100; i++) {
        dirs[i] = NULL;
    }
    
    char *tmp = strdup(path); //Local copy of path (so path isn't modified by strsep)
    int ndirs = countDirs(tmp); //Number of directories found in PATH
    
    //Split path (directory by directory) and store the directory in 'dirs' each loop, until all directories have been stored
    int j;
    for(j=0; j<ndirs; j++){
        //Store next directory (everything up to next ':' character) in 'result'
        char *result = strsep(&tmp,":");
        
        //If 'result' is not NULL, store it (the directory) in 'dirs' (array of directories)
        if(result != NULL) {
            dirs[j] = result;
        }
        else break; //Otherwise, there are no longer any directories
    }
    
    bool ensureSuccess = (j == ndirs);
    if(!ensureSuccess) {
        //Something has gone wrong (the loop broke before all directories in PATH were stored)
    }
}


//Count and return number of directories in PATH
int countDirs(char *path){
        int count = 1;
        //Until the null character is reached, increment count everytime a colon is encountered
        while(*path != '\0') {
            if(*path == ':') {
                ++count;
            }
            ++path;
        }
        return count;
}

//Accepts string command and attempts to execute it; returns exit status of command
int executeCommand(char **argv, char *infile, char *outfile, bool append, bool pipe_out, bool pipe_in, int this_pipe){
    int statusinfo; //Paramater to wait() (stores exit info about child process)
    int exitstatus; //Stores exit status of child process
    int p_id;
    char fileName[100];
    char dirName[100];
    int cur_arg = 0; //The argument to process next
    bool isTimed = false; //Records whether or not the command is timed
    struct timeval tvalBefore, tvalAfter; //Allows for calculation of execution time
    /* 'args' is used to store and pass any command arguments to execv; it must be incremented if 'time' is called (since passing argv will also pass 'time' as the first argument to execv) */
    char **args = argv;
    FILE *fpin;
    FILE *fpout;
    
    /* INTERNAL COMMANDS */
    
    //EXIT
    if(!(strcmp(argv[cur_arg], "exit"))) {
        cur_arg++;
        if(argv[cur_arg] != NULL) {
            //If there is an additional argument, use it as the exit status
            exit(atoi(argv[cur_arg]));
        }
        else {
            //Otherwise, exit with exit status of last executed command
            exit(last_exitstatus);
        }
    }
    
    //CD
    else if(!(strcmp(argv[cur_arg], "cd"))) {
        cur_arg++;
        //Check if there is an additonal argument
        if(argv[cur_arg] != NULL) {
            if(hasSlash(argv[cur_arg])) {
                //If the argument is a directory, use it as the new working directory
                chdir(argv[cur_arg]);
            }
            else {
                path_split(CDPATH);
                //Otherwise, try to find the argument using the directories in CDPATH
                int i=0;
                //Loop through all directories in CDPATH, and attempt to find argument in each one
                while(dirs[i] != NULL) {
                    sprintf(dirName, "%s/%s", dirs[i++], argv[cur_arg]); //Append argument onto current directory from CDPATH
                    chdir(dirName);
                }
            }
        }
        //If there isn't, use the HOME variable as the new directory
        else {
            chdir(HOME);
        }
        return EXIT_SUCCESS; //Not necessarily; fix this
    }
    
    //TIME
    else if(!(strcmp(argv[cur_arg], "time"))) {
        cur_arg++;
        isTimed = true;
        //Record start time
        gettimeofday(&tvalBefore, NULL);
        //Increment 'args'
        args++;
    }
    
    //SET
    else if(!strcmp(argv[cur_arg], "set")) {
        cur_arg++;
        if(!strcmp(argv[cur_arg], "PATH")) {
            //Set PATH
            strcpy(PATH, argv[++cur_arg]);
            return EXIT_SUCCESS;
        }
        else if(!strcmp(argv[cur_arg], "HOME")) {
            //Set HOME
            strcpy(HOME, argv[++cur_arg]);
            return EXIT_SUCCESS;
        }
        else if(!strcmp(argv[cur_arg], "CDPATH")) {
            //Set CDPATH
            strcpy(CDPATH, argv[++cur_arg]);
            return EXIT_SUCCESS;
        }
        
    }
    
    /* NON-INTERNAL COMMANDS */
    
    p_id = fork();
    
    //Failure to create new child process, return failure status
    if (p_id == -1) {
        return EXIT_FAILURE;
    }
    
    //Otherwise, attempt to execute desired command using child process
    else if (p_id == 0) {
        /* child process */
        
        /* CHECK FOR INFILE AND OUTFILE REDIRECTION */
        if(infile != NULL) {
            fpin = fopen(infile, "r");
            if(fpin != NULL) dup2(fileno(fpin), STDIN_FILENO);
            else {
                //Opening of file failed, report error and exit child process
                perror(NULL);
                exit(EXIT_FAILURE);
            }
        }
        if(outfile != NULL) {
            if(append) fpout = fopen(outfile, "a");
            else fpout = fopen(outfile, "w");
            dup2(fileno(fpout), STDOUT_FILENO);
        }
        
        /* CHECK IF THE OUTPUT AND/OR INPUT IS BEING PIPED */
        if(pipe_out && pipe_in) {
            /* Pipe in from (this_pipe - 1) and pipe out to this_pipe */
            //Change stdin to read end of (this_pipe -1)
            dup2(pfd[this_pipe-1][0], STDIN_FILENO);
            close(pfd[this_pipe-1][1]);
            
            //Change stdout to write end of this_pipe
            dup2(pfd[this_pipe][1], STDOUT_FILENO);
            close(pfd[this_pipe][0]);
        }
        
        else if(pipe_out) {
            //Change stdout to write end of pipe
            dup2(pfd[this_pipe][1], STDOUT_FILENO);
            close(pfd[this_pipe][0]); //read end of pipe is not needed by this process
        }
        else if(pipe_in) {
            //Change stdin to read end of pipe
            dup2(pfd[this_pipe][0], STDIN_FILENO);
            close(pfd[this_pipe][1]); //write end of pipe is not needed by this process
        }
        
        /* EXECUTE COMMAND */
        
        //Check if the command already contains a directory
        if(hasSlash(argv[cur_arg])) {
            int exec_failure = execv(argv[cur_arg], args);
            if(exec_failure) {
                //If execution fails, treat the file as a shell script, and attempt to run the commands inside
                FILE* shellscript = fopen(argv[cur_arg], "r");
                if(shellscript != NULL) {
                    //We have opened the file, and now need to run the commands inside
                    dup2(fileno(shellscript), STDIN_FILENO); //Make the file the child's stdin
                    while(!feof(stdin)) { //Create and execute a command tree from the child's stdin
                        CMDTREE *t = parse_cmdtree(stdin);
                        if(t != NULL) {
                            execute_cmdtree(t);
                            free_cmdtree(t);
                        }
                    }
                    exit(EXIT_SUCCESS);
                }
                else {
                    //Opening of file failed
                    fprintf(stderr, "Error: Couldn't open %s\n", argv[cur_arg]);
                }
            }
        }
        //If it doesn't, try to find and execute the command using the directories in PATH
        else {
            path_split(PATH);
            int i=0;
            //Loop through all directories in PATH, and test for file's existence in each directory
            while(dirs[i]!=NULL) {
                sprintf(fileName,"%s/%s",dirs[i++],argv[cur_arg]);//append directory onto fileName, and then append command onto fileName
                int exists = access(fileName, F_OK); //Check if fileName exists
                if(!exists) {
                    //If it does, attempt to execute command using fileName
                    int exec_failure = execv(fileName,args);
                    if(exec_failure) {
                        //If execution fails, treat the file as a shell script, and attempt to run the commands inside
                        FILE* shellscript = fopen(argv[cur_arg], "r");
                        if(shellscript != NULL) {
                            //We have opened the file, and now need to run the commands inside
                            dup2(fileno(shellscript), STDIN_FILENO); //Make the file the child's stdin
                            while(!feof(stdin)) { //Create and execute a command tree from the child's stdin
                                CMDTREE *t = parse_cmdtree(stdin);
                                if(t != NULL) {
                                    execute_cmdtree(t);
                                    free_cmdtree(t);
                                }
                            }
                        }
                        else {
                            //Opening of file failed
                        }
                    }
                }
            }
        }
        /* the following code is only executed if the above execv fails, and the file can't be run/open as a shellscript */
        fprintf(stderr, "mysh: %s: command not found\n", argv[cur_arg]);
        exit(EXIT_FAILURE);
    }

    else {
        /* parent */
        
        if(pipe_in && pipe_out) {
            //If the child process has piped in its stdin from another child, and is piping out its stdout to another child, this means the child from which the current process is piping its stdin was created by a different parent to the current process. This different parent would have opened the pipe on this_pipe -1 (not this_pipe), thus that is what must be closed for the child to finish reading from the pipe
            close(pfd[this_pipe-1][0]);
            close(pfd[this_pipe-1][1]);
        }
        
        else if(pipe_in) { //If the child is only piping in, then the pipe it's trying to read is the pipe opened by the child's immediate parent, thus this_pipe is closed
            //If the child process has piped its stdin from another child, than the parent's pipes must be closed before the child can finish reading from the pipe
            close(pfd[this_pipe][0]);
            close(pfd[this_pipe][1]);
        }
        
        //Wait for child to finish
        signal(SIGCHLD, SIG_DFL); //Ignore custom signal handler for foreground processes
        wait(&statusinfo); //Only wait for the child process if it's a foreground process
        signal(SIGCHLD, childSignalHandler); //Change signal handler back (for any background processes)
        
        //Record end time
        gettimeofday(&tvalAfter, NULL);
        if(WIFEXITED(statusinfo)) {
            //Child process terminated normally
            exitstatus = WEXITSTATUS(statusinfo);
            if(isTimed) {
                //If the command was timed, calculate processing time and report to stderr stream
                long executiontime = ((tvalAfter.tv_sec - tvalBefore.tv_sec) * 1000000 + tvalAfter.tv_usec) - tvalBefore.tv_usec;
                fprintf(stderr, "time %ldmsec\n", executiontime/1000);
                //Report to stderr stream
            }
        }
        else {
            //Child process terminated abnormally; find and return exit status of child
            exitstatus = WEXITSTATUS(statusinfo);
            if(isTimed) {
                long executiontime = ((tvalAfter.tv_sec - tvalBefore.tv_sec) * 1000000 + tvalAfter.tv_usec) - tvalBefore.tv_usec;
                fprintf(stderr, "time %ldmsec\n", executiontime/1000);
            }
        }
    }
    last_exitstatus = exitstatus;
    return exitstatus;
}


//  THIS FUNCTION SHOULD TRAVERSE THE COMMAND-TREE and EXECUTE THE COMMANDS
//  THAT IT HOLDS, RETURNING THE APPROPRIATE EXIT-STATUS.
//  READ print_cmdtree0() IN globals.c TO SEE HOW TO TRAVERSE THE COMMAND-TREE
int execute_cmdtree(CMDTREE *t)
{
    int  exitstatus = 0;
    int pid;
    int this_pipe = npipe;
    bool pipe_in = false; //Records whether or not the next command needs to pipe in
    bool pipe_out = false;

    //If command tree is empty, exit with failure status
    if(t == NULL) {
	exitstatus	= EXIT_FAILURE;
        }
    //If the command tree is not null, then proceed to execute
    else {
        //Check input type of current node, and process accordingly
        switch (t->type) {
            case N_COMMAND:
                //If previous node was a pipe, that means that this command must (by definition of our traversal method) pipe in from the previous pipe
                if(previous_type_was_pipe) pipe_in = true;
                exitstatus = executeCommand(t->argv, t->infile, t->outfile, t->append, false, pipe_in, this_pipe-1);
                previous_type_was_pipe = false;
                break;
                
            case N_SEMICOLON:
                previous_type_was_pipe = false;
                exitstatus = execute_cmdtree(t->left);
                exitstatus = execute_cmdtree(t->right);
                break;
                
            case N_AND:
                previous_type_was_pipe = false;
                exitstatus = execute_cmdtree(t->left);
                if(exitstatus == EXIT_SUCCESS) {
                    exitstatus = execute_cmdtree(t->right);
                }
                break;
                
            case N_OR:
                previous_type_was_pipe = false;
                exitstatus = execute_cmdtree(t->left);
                if(exitstatus == EXIT_FAILURE || exitstatus != EXIT_SUCCESS) {
                    exitstatus = execute_cmdtree(t->right);
                }
                break;
                
            case N_BACKGROUND:
                pid = fork();
                if(pid == -1) {
                    exitstatus = EXIT_FAILURE;
                }
                else if(pid == 0) {
                    execute_cmdtree(t->left);
                    exit(EXIT_SUCCESS);
                }
                else {
                    if(t->right != NULL) execute_cmdtree(t->right);
                    exitstatus = EXIT_SUCCESS;
                }
                break;
                
            case N_SUBSHELL:
                if(previous_type_was_pipe) pipe_in = true; //Check if the subshell is being piped into
                
                pid = fork();
                
                if(pid == -1) {
                    exitstatus = EXIT_FAILURE;
                }
                
                else if(pid == 0) {
                    /* child process (subshell) */
                    
                    /* piping into subshell */
                    if(pipe_in) {
                        dup2(pfd[this_pipe-1][0], STDIN_FILENO);
                        close(pfd[this_pipe-1][1]);
                    }
                    
                    /* infile and outfile handler for subshell */
                    if(t->infile != NULL) {
                        FILE *fpin = fopen(t->infile, "r");
                        if(fpin == NULL) {
                            //Opening of file failed, report error and exit subshell
                            perror(NULL);
                            exit(EXIT_FAILURE);
                        }
                        else {
                            //Make file stdin of process
                            dup2(fileno(fpin), STDIN_FILENO);
                            fclose(fpin);
                        }
                    }
                    if(t->outfile != NULL) {
                        FILE *fpout;
                        if(t->append) fpout = fopen(t->outfile, "a");
                        else fpout = fopen(t->outfile, "w");
                        if(fpout == NULL) ; //Error opening file; simply continue, and create new file for output
                        else {
                            //Redirect stdout to file
                            dup2(fileno(fpout), STDOUT_FILENO);
                            fclose(fpout);
                        }
                    }
                    
                    previous_type_was_pipe = false;
                    exitstatus = execute_cmdtree(t->left); //Run commands within newly forked subshell (child process)
                    exit(exitstatus); //After processing command sequence, exit subshell
                }
                else {
                    /* back to parent process (original shell) */
                    int statusinfo;
                    signal(SIGCHLD, SIG_DFL);
                    if(pipe_in) close(pfd[this_pipe-1][1]);
                    wait(&statusinfo); //Wait for child shell to finish
                    signal(SIGCHLD, childSignalHandler); //Reactivate signal handler for any background processes
                    exitstatus = WEXITSTATUS(statusinfo); //Get exit status of child shell
                }
                break;
                
            case N_PIPE:
                pipe_out = true; //Next command (left sub-tree) will always need to pipe out (given that t->type == N_PIPE)
                
                if(previous_type_was_pipe) pipe_in = true;
                else pipe_in = false;
                
                pipe(pfd[this_pipe]);
                
                /* LEFT BRANCH OF PIPE */
                
                if(t->left->type == N_COMMAND) exitstatus = executeCommand(t->left->argv, t->left->infile, t->left->outfile, t->left->append, pipe_out, pipe_in, this_pipe);
                
                //Special case where left branch is a subshell (pipe out of subshell)
                else if(t->left->type == N_SUBSHELL) {
                    pid = fork();
                    if(pid==-1) {
                        exitstatus = EXIT_FAILURE;
                    }
                    if(pid==0) {
                        npipe++;
                        if(pipe_in) {
                            //We may also be piping into this subshell
                            dup2(pfd[this_pipe-1][0], STDIN_FILENO);
                            close(pfd[this_pipe-1][1]);
                        }
                        close(pfd[this_pipe][0]);
                        dup2(pfd[this_pipe][1], STDOUT_FILENO);
                        previous_type_was_pipe = false;
                        exitstatus = execute_cmdtree(t->left->left);
                        exit(exitstatus);
                    }
                    else {
                        int statusinfo;
                        signal(SIGCHLD, SIG_DFL); //Consider using sigprocmask()
                        if(pipe_in) close(pfd[this_pipe-1][1]);
                        wait(&statusinfo); //Wait for child shell to finish
                        signal(SIGCHLD, childSignalHandler); //Reactivate signal handler for any background processes
                        exitstatus = WEXITSTATUS(statusinfo); //Get exit status of child shell
                    }
                }
                
                /* RIGHT BRANCH OF PIPE */
                
                if(t->right->type == N_COMMAND) exitstatus = executeCommand(t->right->argv, t->right->infile, t->right->outfile, t->right->append, false, true, this_pipe);
                else if(t->right->type == N_PIPE || t->right->type == N_SUBSHELL) {
                    //Need to recursively call execute_cmdtree
                    npipe++;
                    previous_type_was_pipe = true;
                    execute_cmdtree(t->right);
                }
                break;
                
            //Otherwise, do nothing
            default:
                break;
        }
    }
    return exitstatus;
}

void childSignalHandler() {
    pid_t pid;
    int status;
    while(true) {
        pid = waitpid(-1, &status, WNOHANG);
        if(pid > 0) {
            //Child process has finished and been reaped by waitpid
            printf("waitpid reaped child pid %d\n\n", pid);
        }
        else {
            return;
        }
    }
}
