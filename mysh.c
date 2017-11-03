#include "mysh.h"

/*
   CITS2002 Project 2 2015
   Name(s):             Ammar Abu Shamleh, George Krlevski
   Student number(s):   21521274, 21522548
   Date:                28/10/15
 */

int main(int argc, char *argv[])
{
//  SIGNAL HANDLER FOR CHILD SIGNALS (TO PROCESS AND REAP BACKGROUND ZOMBIES)
    signal(SIGCHLD, childSignalHandler);
    
//  REMEMBER THE PROGRAM'S NAME (TO REPORT ANY LATER ERROR MESSAGES)
    argv0	= (argv0 = strrchr(argv[0],'/')) ? argv0+1 : argv[0];
    argc--;				// skip 1st command-line argument
    argv++;

//  INITIALIZE THE THREE INTERNAL VARIABLES
    char	*p;

    p		= getenv("HOME");
    HOME	= strdup(p == NULL ? DEFAULT_HOME : p);
    check_allocation(HOME);

    p		= getenv("PATH");
    PATH	= strdup(p == NULL ? DEFAULT_PATH : p);
    check_allocation(PATH);

    p		= getenv("CDPATH");
    CDPATH	= strdup(p == NULL ? DEFAULT_CDPATH : p);
    check_allocation(CDPATH);
    

//  DETERMINE IF THIS SHELL IS INTERACTIVE
    interactive		= (isatty(fileno(stdin)) && isatty(fileno(stdout)));

    int exitstatus	= EXIT_SUCCESS;

//  READ AND EXECUTE COMMANDS FROM stdin UNTIL IT IS CLOSED (with control-D)
    while(!feof(stdin)) {
        
        CMDTREE	*t = parse_cmdtree(stdin);
        
	if(t != NULL) {

//  WE COULD DISPLAY THE PARSED COMMAND-TREE, HERE, BY CALLING:
	    //print_cmdtree(t);
        
        //Reset pipe counter and previous_type_was_pipe flag
        previous_type_was_pipe = false;
        npipe = 0;
        
        //Execute commands
	    exitstatus = execute_cmdtree(t); 
	    free_cmdtree(t);
	}
    }
    if(interactive) {
	fputc('\n', stdout);
    }
    return exitstatus;
}
