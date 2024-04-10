# Small Shell

## Overview

This was an C language project to build a custom shell within bash in Linux.
The objective was to demonstrate mastery over execution of commands via child processes and communication between processes via signals. 

## Functionality

The shell program must be able to operate in two modes, interactive mode and non-interactive mode:
  * interactive mode represents the expected shell behavior where a prompt is displayed that accepts user commands
  * non-interactive mode behaves as a program and executes a list of shell commands stored in a file
 
In both modes:
* Commands are read one line at a time
* Comments are ignored if detected in a command line, comments are identfied by a leading '#' character at the start of a new word
* Words in the command line are expanded as appropriate:
  * '$$' is replaced by the process ID of the shell
  * '$?' is replaced by the exit status of the last foreground command
  * '$!' is replaced by the process ID of the most recent background process
  * '$(parameter)' is replaced by the value of the corresponding environment variable named by parameter
* Custom commands for **CD** and **Exit** can be executed and behave as you would expect in bash
* All other commands are executed in a new child process 
  * If the command does not include a '/', its searched for in the system's PATH environment variable through EXECVP.
  * Commands support redirections and are executed in order from left to right
    * '<' opens file following it for stdin, '>' opens the file for stdout, and '>>' opens the file for appending on stdout
  * If the last word is '&' the command is excuted in the background
    *   If a command is run in the background, the shell does not wait for it and moves on to the next command or displays the prompt
  * If a command does not have the  background operator (&), the shell will perform a blocking wait.
    * If a blocking foreground command is stopped, the shell will send the SIGCONT signal and no longer wait on this process
  * Informative messages are printed to the shell when a background process is exited or signaled

Only in interactive mode:
  * A prompt is displayed that shows the status of the user ($ for standard user and # for root user)
  * The custom shell ignores SIGSTP signals
  * the SIGINT signal is ignored at all times except when reading a line of input
    * in this scenario the signal handler will simply do nothing 

## How to run program

1. Boot up a linux operating system (this program was built in ubuntu) 
2. Download main.c and makefile to a local directory
3. Open a bash terminal and navigate to the directory with both files
4. Use the makefile to compile the program, by default the program is named smallsh
5. Run smallsh program directly for interactive mode or redirect a file with commands into the program to run it in non-interactive mode
