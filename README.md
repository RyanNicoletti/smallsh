# Smallsh

  * a small shell created in C
  * included functionality:
    * built in commands: "exit", "status", "cd"
    * executes all other valid non-built in bash commands
    * foreground mode-runs new processes in the foreground only
    * run background processes by adding & at the end of any valid command
    * background child process are cleaned up asynchronously 

## Compilation

The code can be compiled with the following flags to make an executable named "smallsh":

```
gcc -std=c99 -o smallsh smallsh.c
```
