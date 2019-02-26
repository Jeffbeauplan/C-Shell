
/* 
 * tsh - A tiny shell program with job control
 * <The line above is not a sufficient documentation.
 *  You will need to write your program documentation.>
 */

#include "tsh_helper.h"

/*
 * If DEBUG is defined, enable contracts and printing on dbg_printf.
 */
#ifdef DEBUG
/* When debugging is enabled, these form aliases to useful functions */
#define dbg_printf(...) printf(__VA_ARGS__)
#define dbg_requires(...) assert(__VA_ARGS__)
#define dbg_assert(...) assert(__VA_ARGS__)
#define dbg_ensures(...) assert(__VA_ARGS__)
#else
/* When debugging is disabled, no code gets generated for these */
#define dbg_printf(...)
#define dbg_requires(...)
#define dbg_assert(...)
#define dbg_ensures(...)
#endif

/* Function prototypes */
void eval(const char *cmdline);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);
void sigquit_handler(int sig);



/*
 * <Write main's function header documentation. What does main do?>
 * "Each function should be prefaced with a comment describing the purpose
 *  of the function (in a sentence or two), the function's arguments and
 *  return value, any error cases that are relevant to the caller,
 *  any pertinent side effects, and any assumptions that the function makes."
 */
int main(int argc, char **argv) 
{
    char c;
    char cmdline[MAXLINE_TSH];  // Cmdline for fgets
    bool emit_prompt = true;    // Emit prompt (default)

    // Redirect stderr to stdout (so that driver will get all output
    // on the pipe connected to stdout)
    Dup2(STDOUT_FILENO, STDERR_FILENO);

    // Parse the command line
    while ((c = getopt(argc, argv, "hvp")) != EOF)
    {
        switch (c)
        {
        case 'h':                   // Prints help message
            usage();
            break;
        case 'v':                   // Emits additional diagnostic info
            verbose = true;
            break;
        case 'p':                   // Disables prompt printing
            emit_prompt = false;  
            break;
        default:
            usage();
        }
    }

    // Install the signal handlers
    Signal(SIGINT,  sigint_handler);   // Handles ctrl-c
    Signal(SIGTSTP, sigtstp_handler);  // Handles ctrl-z
    Signal(SIGCHLD, sigchld_handler);  // Handles terminated or stopped child

    Signal(SIGTTIN, SIG_IGN);
    Signal(SIGTTOU, SIG_IGN);

    Signal(SIGQUIT, sigquit_handler); 

    // Initialize the job list
    initjobs(job_list);

    // Execute the shell's read/eval loop
    while (true)
    {   
        if (emit_prompt)
        {
            printf("%s", prompt);
            fflush(stdout);
        }

        if ((fgets(cmdline, MAXLINE_TSH, stdin) == NULL) && ferror(stdin))
        {
            app_error("fgets error");
        }

        if (feof(stdin))
        { 
            // End of file (ctrl-d)
            raise(SIGQUIT);
            printf ("\n");
            fflush(stdout);
            fflush(stderr);
            return 0;
        }
        
        // Remove the trailing newline
        cmdline[strlen(cmdline)-1] = '\0';
        
        // Evaluate the command line
        eval(cmdline);

        fflush(stdout);
    } 
    
    return -1; // control never reaches here
}


/* Handy guide for eval:
 *
 * If the user has requested a built-in command (quit, jobs, bg or fg),
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.
 * Note: each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.
 */

/* 
 * <What does eval do?>
 */
void eval(const char *cmdline) 
{
    parseline_return parse_result;     
    struct cmdline_tokens token;
    unblockSig();
    
    // Parse command line
    parse_result = parseline(cmdline, &token);
    
    if (parse_result == PARSELINE_ERROR || parse_result == PARSELINE_EMPTY)
    {
        return;
    }
    
    if (token.builtin != BUILTIN_NONE && (!token.infile && !token.outfile)) {
        switch (token.builtin) {
            case BUILTIN_QUIT:
                raise(SIGQUIT);
                break;
            case BUILTIN_JOBS:
                blockSig();
                return listjobs(job_list, 1);
            case BUILTIN_BG:
                return bgcommand(&token);
            case BUILTIN_FG:
                return fgcommand(&token);
            default:
                break;
        }
    }
    
    blockSig();
    
    //Add foreground job
    if (parse_result == PARSELINE_FG) 
        return addfgjob(&token, cmdline);
    
    //Add background job
    if (parse_result == PARSELINE_BG)
        return addbgjob(&token, cmdline);
    return;
}

/*****************
 * Signal handlers
 *****************/

/* 
 *  reap zombie processes and update job list
 */
void sigchld_handler(int sig) 
{    
    int status;
    pid_t pid;
    
    do
        pid = waitpid(WAIT_ANY, &status, WUNTRACED|WNOHANG);
    while(!updateJobStatus(pid, status));
    return;
}

/* 
 * forwards a SIGINT signal to the foreground process group
 */
void sigint_handler(int sig) 
{   
    blockSig();
    pid_t pid = fgpid(job_list);
    pid_t gpid = __getpgid(pid);
    if(gpid != getpid()) kill(-gpid, SIGINT);
    unblockSig();
    return;
}

/*
 * forwards a SIGTSTP signal to the foreground process group
 */
void sigtstp_handler(int sig) 
{
    blockSig();
    pid_t pid = fgpid(job_list);
    pid_t gpid = __getpgid(pid);
    if(gpid != getpid()) kill(-gpid, SIGTSTP);
    unblockSig();
    return;
}

/*
 * blocks SIGCHLD, SIGINT, SIGTSTP signals
 */
void blockSig() {
    sigset_t ourmask;
    sigaddset(&ourmask, SIGCHLD);
    sigaddset(&ourmask, SIGINT);
    sigaddset(&ourmask, SIGTSTP);
    sigprocmask(SIG_BLOCK, &ourmask, NULL);
}

/*
 * unblocks SIGCHLD, SIGINT, SIGTSTP signals
 */
void unblockSig() {
    sigset_t ourmask;
    sigaddset(&ourmask, SIGCHLD);
    sigaddset(&ourmask, SIGINT);
    sigaddset(&ourmask, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &ourmask, NULL);
}

/*
 * uses the pid or %jid from the second argument in the 
 * command line arguments to retreive a job
 */
struct job_t* getjob(const struct cmdline_tokens *token) {
    struct job_t* job;
    int pid;
    if(token->argv[1][0] == '%') {
        int size = sizeof(token->argv[1])/ sizeof(token->argv[1][0]);
        char jid[size]; 
        memcpy(jid, &token->argv[1][1], size-1);
        int id = atoi((const char*) jid);
        job = getjobjid(job_list, id);
    }
    else {
        pid = atoi(token->argv[1]);
        job = getjobpid(job_list, pid); 
    }
    
    return job;
}

/*
 * updates the job list and job list based on the status of
 * the pid passed in
 */
int updateJobStatus(pid_t pid, int status) {
    if(pid > 0) {
        blockSig();
        struct job_t *job = getjobpid(job_list, pid);
        if(job != NULL) {
            if (WIFSTOPPED (status)) {
                printf("Job [%d] (%d) stopped by signal %d\n", job->jid, job->pid, WSTOPSIG(status));
                job->state = ST;
            }
            else if (WIFEXITED (status) || WIFSIGNALED (status)) {
                if(WIFSIGNALED (status) && WTERMSIG(status) > 0)
                   printf("Job [%d] (%d) terminated by signal %d\n", job->jid, job->pid, status);
                deletejob(job_list, pid);
            }
            return 0;
        }
        unblockSig();
    }
    return -1;
}

/*
 * restarts a job in the background.  
 */ 
void bgcommand(const struct cmdline_tokens *token) {
    if(token->argc < 2) { 
        sio_puts("bg command requires PID or %jobid argument\n");
        return;
    }
    //retrieve job by jid or pid
    blockSig();
    struct job_t *job = getjob(token);
    unblockSig();
    //if job found then restart job in background
    if(job != NULL) {
        kill(job->pid, SIGCONT);
        job->state = BG;
        printf("[%d] (%d) %s\n", job->jid, job->pid, job->cmdline);
    }
    else sio_puts("No such process found\n");
    return;
}

/*
 * restarts a job in the foreground
 */
void fgcommand(const struct cmdline_tokens *token){
    if(token->argc < 2) { 
        sio_puts("fg command requires PID or %jobid argument\n");
        return;
    }
    blockSig();
    //retrieve job by jid or pid
    struct job_t *job = getjob(token);
    if(job != NULL) {
        kill(job->pid, SIGCONT);
        job->state = FG;
        unblockSig();
        sigset_t mask, oldmask;
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTSTP);
        Sigprocmask(SIG_BLOCK, &mask, &oldmask);

        while(fgpid(job_list) != 0)
        {
            sigsuspend(&oldmask);   
        } 
        blockSig();
        deletejob(job_list, job->pid);
    }
    else sio_puts("No such process found\n");
    return;
}

/*
 * Starts a job in the background
 */
void addbgjob(const struct cmdline_tokens *token, const char *cmdline) {
    pid_t pid = fork();
    if(pid == 0) {
        unblockSig();
        int st;
        Setpgid(0,0);
        //restore default handlers
        Signal(SIGINT, SIG_DFL);
        Signal(SIGTSTP, SIG_DFL);
        Signal(SIGCHLD, SIG_DFL);
        //handle I/O Redirection
        if((token->infile != NULL) || (token->outfile != NULL)) {
            int in = open(token->infile, O_RDONLY);
            int out = open(token->outfile, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
            dup2(in, 0);
            dup2(out, 1);
            close(in);
            close(out);
            st = execvp(token->argv[0], token->argv);
            _exit(2);
        }
        st = execve(token->argv[0], token->argv, environ); 
        if(st == -1) printf("%s: Command not found\n", token->argv[0]);
        _exit(2);
    }
    else {
        addjob(job_list, pid, BG, cmdline);
        struct job_t* job = getjobpid(job_list, pid);
        printf("[%d] (%d) %s\n", job->jid, job->pid, job->cmdline);
        unblockSig();
    }
}

/*
 * Starts a job in the foreground
 */
void addfgjob(const struct cmdline_tokens *token, const char *cmdline) {
    pid_t pid = fork();
    if(pid == 0) {
        int st;
        unblockSig();
        //restore default handlers
        Signal(SIGINT, SIG_DFL);
        Signal(SIGTSTP, SIG_DFL);
        Signal(SIGCHLD, SIG_DFL);
        Setpgid(0,0);
        //handle I/O Redirection
        if((token->infile != NULL) || (token->outfile != NULL)) {
            int in = open(token->infile, O_RDONLY);
            int out = open(token->outfile, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
            dup2(in, 0);
            dup2(out, 1);
            close(in);
            close(out);
            st = execvp(token->argv[0], token->argv);
            _exit(2);
        }
        st = execve(token->argv[0], token->argv, environ); 
        if(st == -1) printf("%s: Command not found\n", token->argv[0]);
        _exit(2);
    }
    else {
        addjob(job_list, pid, FG, cmdline);
        unblockSig();
        sigset_t mask, oldmask;
        sigaddset(&mask, SIGCHLD);
        sigaddset(&mask, SIGINT);
        sigaddset(&mask, SIGTSTP);
        Sigprocmask(SIG_BLOCK, &mask, &oldmask);
        while(fgpid(job_list) != 0)
        {
            sigsuspend(&oldmask);   
        } 
    }
}
