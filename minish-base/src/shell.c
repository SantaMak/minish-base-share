/* -*- mode: c; c-basic-offset: 4; indent-tabs-mode: t; -*- */
/*
 * Minish shell.
 *
 * Team Members:
 * [Surname Name, AM]
 * [Surname Name, AM]
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "parser.h"

#define INPUT_SIZE 4096
#define MAX_JOBS 128

struct job {
    pid_t pid;
    int active;
};

static struct job jobs[MAX_JOBS];

static void print_prompt(void);
static int read_line(char *buffer, size_t size);
static int execute_pipeline(const struct pipeline *pipeline);
static void init_bg_jobs(struct job *jobs);
static void add_bg_job(const pid_t pid);
static void remove_bg_job(const pid_t pid);
static void reap_bg_jobs(void);

// Helper function prototypes for signal handlers
static void handle_sigchld(int signo);

int main(void)
{   
    char line[INPUT_SIZE];
    struct pipeline pipeline;
    int error;
    int result;

    // Initialize background jobs array
    init_bg_jobs(jobs);

    // Install SIGCHLD handler
    struct sigaction chld_action;
    memset(&chld_action, 0, sizeof(chld_action));
    chld_action.sa_handler = handle_sigchld;
    sigemptyset(&chld_action.sa_mask);
    chld_action.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &chld_action, NULL) == -1) {
        perror("sigaction");
    }

    // Install SIGINT handler (ignore Ctrl+C in shell)
    struct sigaction int_action;
    memset(&int_action, 0, sizeof(int_action));
    int_action.sa_handler = SIG_IGN;
    sigemptyset(&int_action.sa_mask);
    if (sigaction(SIGINT, &int_action, NULL) == -1) {
        perror("sigaction");
        return 1;
    }
     
    // The main shell loop.
    for (;;) {
        // Print the prompt.
        print_prompt();

        // Read a command line from the user.
        result = read_line(line, sizeof(line));

        // Handle read errors.
        if (result == 0)
            break;

        if (result < 0) {
            if (errno == EINTR)
                continue;

            perror("minish: Cannot read the command line.");
            return EXIT_FAILURE;
        }

        // Parse the line.
        error = parse_line(line, &pipeline);

        // An error occurred during parsing.
        if (error != PARSE_SUCCESS) {
            fprintf(stderr, "minish: %s\n", parse_error_string(error));
            continue;
        }

        // The user just pressed enter. We have nothing to execute at this time.
        if (pipeline.command_count == 0) {
            pipeline_free(&pipeline);
            continue;
        }

        // Execute the command pipeline.
        if ((result = execute_pipeline(&pipeline)) < 0) {
            if (result == -2) {
                pipeline_free(&pipeline);
                break; // exit requested
            }
            perror("minish");
        }

        // Clear the pipeline.
        pipeline_free(&pipeline);
    }
    return EXIT_SUCCESS;
}

static void print_prompt(void)
{
    if (isatty(STDIN_FILENO) != 0) {
        (void)printf("minish$ ");
        (void)fflush(stdout);
    }
}

static int read_line(char *buffer, size_t size)
{
    if (fgets(buffer, size, stdin) == NULL) {
        if (feof(stdin) != 0)
            return 0;

        return -1;
    }

    buffer[strcspn(buffer, "\n")] = '\0';

    return 1;
}

static void init_bg_jobs(struct job *jobs_arr)
{
    int i;
 
    if (!jobs_arr)
        return;
        
    for (i = 0; i < MAX_JOBS; i++) {        
        jobs_arr[i].pid = -1;
        jobs_arr[i].active = 0;
    }
}

static void add_bg_job(const pid_t pid)
{
    int i;

    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active == 0) {
            jobs[i].pid = pid;
            jobs[i].active = 1;
            break;
        }
    }
}

static void remove_bg_job(const pid_t pid)
{
    int i;

    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].pid == pid) {
            jobs[i].pid = -1;
            jobs[i].active = 0;
            break;
        }
    }
}

static void reap_bg_jobs(void) {
    pid_t pid;
    int status; 
    int i;

    for (i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].active) {
            pid = waitpid(jobs[i].pid, &status, WNOHANG);
            if (pid > 0) {
                remove_bg_job(pid);
            }
        }
    }   
}

static void handle_sigchld(int signo) {
    (void)signo;
    reap_bg_jobs();
}

static int execute_pipeline(const struct pipeline *pipeline)
{
    pid_t *pids;    
    int result = 0;
    
    // 1. Handle built-in commands (exit, cd) when command count is 1
    if (pipeline->command_count == 1) {
        const struct command *cmd = &pipeline->commands[0];
        if (cmd->argc > 0 && cmd->argv[0] != NULL) {
            if (strcmp(cmd->argv[0], "exit") == 0) {
                return -2; // Signal main loop to exit
            }
            if (strcmp(cmd->argv[0], "cd") == 0) {
                char *path = (cmd->argc > 1) ? cmd->argv[1] : getenv("HOME");
                if (!path) path = "/";
                if (chdir(path) != 0) {
                    perror("minish: cd");
                }
                return 0;
            }
        }
    }

    // Allocate an array to store the PID of each child process.
    pids = calloc(pipeline->command_count, sizeof(*pids));
    if (pids == NULL)
        return -1;

    // For this basic version, execute the first command in the pipeline
    const struct command *cmd = &pipeline->commands[0];
    pid_t pid = fork();

    if (pid < 0) {
        free(pids);
        return -1;
    }

    if (pid == 0) {
        // Child process

        // Restore default SIGINT behavior for foreground processes if not background
        if (!pipeline->background) {
            struct sigaction dfl;
            memset(&dfl, 0, sizeof(dfl));
            dfl.sa_handler = SIG_DFL;
            sigemptyset(&dfl.sa_mask);
            sigaction(SIGINT, &dfl, NULL);
        }

        // Handle I/O Redirections
        for (size_t i = 0; i < cmd->redirect_count; i++) {
            int fd;
            if (cmd->redirects[i].type == REDIRECT_INPUT) {
                fd = open(cmd->redirects[i].path, O_RDONLY);
                if (fd < 0) {
                    perror("minish: open input file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            } else if (cmd->redirects[i].type == REDIRECT_OUTPUT) {
                fd = open(cmd->redirects[i].path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("minish: open output file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else if (cmd->redirects[i].type == REDIRECT_APPEND) {
                fd = open(cmd->redirects[i].path, O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd < 0) {
                    perror("minish: open append file");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }
        }

        // Execute command
        execvp(cmd->argv[0], cmd->argv);
        perror("minish: execvp");
        exit(EXIT_FAILURE);
    } else {
        // Parent process
        pids[0] = pid;
        if (pipeline->background) {
            add_bg_job(pid);
            printf("[%d] %d\n", 1, (int)pid);
        } else {
            int status;
            waitpid(pid, &status, 0);
        }
    }
        
    free(pids);
    return result;
}