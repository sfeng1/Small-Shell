#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <inttypes.h>

#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif

char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word);

// declare some variables and arrays with default values
char fgStatus[1000] = "0";
char bgStatus[1000] = "";
int bgFlag = 0;
int isInter = 0;

// empty sig handler
void sigint_handler(int sig){};

int main(int argc, char *argv[])
{
    FILE *input = stdin;
    char *input_fn = "(stdin)";
    if (argc == 2) {
        input_fn = argv[1];
        input = fopen(input_fn, "re");
        if (!input) err(1, "%s", input_fn);
    } else if (argc > 2) {
        errx(1, "too many arguments");
    }

    // struct to save original signals
    struct sigaction oldINT = {0};
    struct sigaction oldSTP = {0};

    char *line = NULL;
    size_t n = 0;

    // structs for custom signal handling
    struct sigaction actionINT = {0};
    actionINT.sa_handler = sigint_handler;
    int fillStatus1;
    fillStatus1 = sigfillset(&actionINT.sa_mask);
    if (fillStatus1 == -1) fprintf(stderr, "%s", "INT signal fill failed\n");
    actionINT.sa_flags = SA_RESTART;

    struct sigaction actionSTP = {0};
    actionSTP.sa_handler = SIG_IGN;
    int fillStatus2;
    fillStatus2 = sigfillset(&actionSTP.sa_mask);
    if (fillStatus2 == -1) fprintf(stderr, "%s", "STP signal fill failed\n");
    actionSTP.sa_flags = 0;

    for (;;) {
        prompt:;
        /* TODO: Manage background processes */

        // reset the background and interactive flag for next line
        bgFlag = 0;
        isInter = 0;

        // check for any active background processes and print their status if available
        for (;;) {
            int checkStatus;
            pid_t checkPid = waitpid(0, &checkStatus, WNOHANG | WUNTRACED);
            if (checkPid <= 0) break;

            // exited normally
            if (WIFEXITED(checkStatus) == 1) {
                checkStatus = WEXITSTATUS(checkStatus);
                fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t)checkPid, checkStatus);
                continue;
            }

            // exited by signal
            if (WIFSIGNALED(checkStatus) == 1) {
                checkStatus = WTERMSIG(checkStatus);
                fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t)checkPid, checkStatus);
                continue;
            }

            // stopped, send continue signal
            if (WIFSTOPPED(checkStatus) == 1) {
                fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t)checkPid);
                kill(checkPid, 18);
                continue;
            }
        }

        /* TODO: prompt */
        // use PS1, set to "" if unset
        if (input == stdin) {
            isInter = 1;
            const char *name = "PS1";
            char *value;
            value = getenv(name);
            if (!value) value = "";
            fprintf(stderr, "%s", value);

            sigaction(SIGINT, &actionINT, &oldINT);
            sigaction(SIGTSTP, &actionSTP, &oldSTP);
        }

        ssize_t line_len = getline(&line, &n, input);

        // correctly handle exit code when end of file is reached by getline
        if (feof(input) > 0) {
            exit(0);
        }

        if (line_len < 0) err(1, "%s", input_fn);

        // prevent erroneous child processes from being created by lines with just the newline character
        if (strcmp(line, "\n") == 0) {
            continue;
        }

        // look for background command in last character of line and process appropriately
        if (line[line_len - 2] == '&') {
            bgFlag = 1;
            line[line_len - 2] = '\n';
            line_len--;
        }

        size_t nwords = wordsplit(line);
        for (size_t i = 0; i < nwords; ++i) {
            char *exp_word = expand(words[i]);
            free(words[i]);
            words[i] = exp_word;
        }

        // Built-in exit command
        if (strcmp(words[0], "exit") == 0) {
            if (nwords > 2) {
                fprintf(stderr, "%s", "too many exit arguments\n");
                goto prompt;
            }
            if (nwords == 2) {
                char *exitPtr = words[1];
                long exitVar;
                exitVar = strtol(exitPtr, &exitPtr, 10);
                if (*exitPtr == '\0') {
                    exit(exitVar);
                } else {
                    fprintf(stderr, "%s", "argument is not an integer\n");
                    goto prompt;
                }
            } else {
                char *exitPid = expand("$?");
                exit(strtol(exitPid, &exitPid, 10));
            }
        }

        // Built-in cd command
        if (strcmp(words[0], "cd") == 0) {

            if (nwords > 2) {
                fprintf(stderr, "%s", "too many cd arguments\n");
                continue;
            }

            if (nwords == 2) {
                int chdirRes;
                chdirRes = chdir(words[1]);
                if (chdirRes == -1) {
                    fprintf(stderr, "%s", "cd has failed\n");
                }
                continue;
            }

            else {
                int chdirRes;
                chdirRes = chdir(getenv("HOME"));
                if (chdirRes == -1) {
                    fprintf(stderr, "%s", "cd home directory failed\n");
                }
                continue;
            }
        }

        // Loop through words and execute command
        size_t argLength = (nwords + 1);
        char *newargv[argLength];
        int childStatus;
        int loopCounter = 0;
        int argCounter =  0;
        pid_t spawnPid = fork();

        switch (spawnPid) {
            // error
            case -1:
                perror("fork() failed!");
                continue;

            // child process
            case 0:
                // restore signals
                if (isInter == 1) {
                    sigaction(SIGTSTP, &oldSTP, NULL);
                    sigaction(SIGINT, &oldINT, NULL);
                }

                for (;;) {
                    if (loopCounter == nwords) {
                        newargv[argCounter] = NULL;
                        break;
                    }

                    // redirect input <
                    if (strcmp(words[loopCounter], "<") == 0) {
                        int redFile = open(words[loopCounter + 1], O_RDONLY | O_CLOEXEC);
                        if (redFile == -1) {
                            fprintf(stderr, "%s", "redirect input has failed \n");
                            exit(3);
                        }

                        int redStatus = dup2(redFile, STDIN_FILENO);
                        if (redStatus == -1) {
                            fprintf(stderr, "%s", "redirect input open dup failed \n");
                            exit(3);
                        }

                        loopCounter++;
                        loopCounter++;
                        continue;
                    }

                    // redirect output >
                    if (strcmp(words[loopCounter], ">") == 0) {
                        int redFile = open(words[loopCounter + 1], O_WRONLY | O_CREAT | O_CLOEXEC | O_TRUNC, 0777);
                        if (redFile == -1) {
                            fprintf(stderr, "%s", "redirect output has failed \n");
                            exit(3);
                        }

                        int redStatus = dup2(redFile, STDOUT_FILENO);
                        if (redStatus == -1) {
                            fprintf(stderr, "%s", "redirect output dup failed \n");
                            exit(3);
                        }

                        loopCounter++;
                        loopCounter++;
                        continue;
                    }

                    // redirect output append >>
                    if (strcmp(words[loopCounter], ">>") == 0) {
                        int redFile = open(words[loopCounter + 1], O_WRONLY | O_CREAT | O_CLOEXEC | O_APPEND, 0777);
                        if (redFile == -1) {
                            fprintf(stderr, "%s", "redirect append output has failed \n");
                            exit(3);
                        }

                        int redStatus = dup2(redFile, STDOUT_FILENO);
                        if (redStatus == -1) {
                            fprintf(stderr, "%s", "redirect output append dup failed \n");
                            exit(3);
                        }

                        loopCounter++;
                        loopCounter++;
                        continue;
                    }

                    newargv[argCounter] = words[loopCounter];
                    loopCounter++;
                    argCounter++;
                }

                execvp(newargv[0], newargv);

                perror("execvp has failed");
                exit(1);

            // parent when child is not a background process
            default:
                if (bgFlag == 0) {
                    waitpid(spawnPid, &childStatus, WUNTRACED);

                    if (WIFEXITED(childStatus)==1) {
                        childStatus = WEXITSTATUS(childStatus);
                        sprintf(fgStatus, "%d", childStatus);
                    }

                    else if (WIFSIGNALED(childStatus)==1) {
                        childStatus = WTERMSIG(childStatus);
                        childStatus = (128 + childStatus);
                        sprintf(fgStatus, "%d", childStatus);
                    }

                    else if (WIFSTOPPED(childStatus)==1) {
                        sprintf(bgStatus, "%jd", (intmax_t) spawnPid);
                        fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) spawnPid);
                        kill(spawnPid, 18);
                        goto prompt;
                    }
                }

                // when child is a background process
                else {
                    waitpid(spawnPid, &childStatus, WNOHANG | WUNTRACED);
                    sprintf(bgStatus, "%jd", (intmax_t) spawnPid);
                }
        }
    }
} // main close brace

char *words[MAX_WORDS] = {0};

/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
    size_t wlen = 0;
    size_t wind = 0;

    char const *c = line;
    for (;*c && isspace(*c); ++c); /* discard leading space */

    for (; *c;) {
        if (wind == MAX_WORDS) break;
        /* read a word */
        if (*c == '#') break;
        for (;*c && !isspace(*c); ++c) {
            if (*c == '\\') ++c;
            void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
            if (!tmp) err(1, "realloc");
            words[wind] = tmp;
            words[wind][wlen++] = *c;
            words[wind][wlen] = '\0';
        }
        ++wind;
        wlen = 0;
        for (;*c && isspace(*c); ++c);
    }
    return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char **start, char **end)
{
    static char *prev;
    if (!word) word = prev;

    char ret = 0;
    *start = NULL;
    *end = NULL;
    char *s = strchr(word, '$');
    if (s) {
        char *c = strchr("$!?", s[1]);
        if (c) {
            ret = *c;
            *start = s;
            *end = s + 2;
        }
        else if (s[1] == '{') {
            char *e = strchr(s + 2, '}');
            if (e) {
                ret = '{';
                *start = s;
                *end = e + 1;
            }
        }
    }
    prev = *end;
    return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
    static size_t base_len = 0;
    static char *base = 0;

    if (!start) {
        /* Reset; new base string, return old one */
        char *ret = base;
        base = NULL;
        base_len = 0;
        return ret;
    }
    /* Append [start, end) to base string
     * If end is NULL, append whole start string to base string.
     * Returns a newly allocated string that the caller must free.
     */
    size_t n = end ? end - start : strlen(start);
    size_t newsize = sizeof *base *(base_len + n + 1);
    void *tmp = realloc(base, newsize);
    if (!tmp) err(1, "realloc");
    base = tmp;
    memcpy(base + base_len, start, n);
    base_len += n;
    base[base_len] = '\0';

    return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word)
{
    char const *pos = word;
    char *start, *end;
    char c = param_scan(pos, &start, &end);
    build_str(NULL, NULL);
    build_str(pos, start);
    while (c) {
        if (c == '!'){
            build_str(bgStatus, NULL);
        }
        else if (c == '$') {
            char pid[1000];
            sprintf(pid, "%jd", (intmax_t) getpid());
            build_str(pid, NULL);
        }
        else if (c == '?') {
            build_str(fgStatus, NULL);
        }
        else if (c == '{') {
            int nlength = strlen(word)-2;
            char parameter_name[nlength];
            strncpy(parameter_name, word + 2, nlength);
            parameter_name[nlength-1] = '\0';

            if (!getenv(parameter_name)) build_str("", NULL);
            else build_str(getenv(parameter_name),NULL);
        }
        pos = end;
        c = param_scan(pos, &start, &end);
        build_str(pos, start);
    }
    return build_str(start, NULL);
}

