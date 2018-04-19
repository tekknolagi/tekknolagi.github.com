---
title: "Driving student programs"
layout: post
date: 2018-04-13 09:24:42 EDT
---

I am a Teaching Fellow for COMP 11, the introductory computer science course at
Tufts. This semester, I opted to take the responsibility to write automated
tests for student code.

Since the course is introductory and explaining the finer details of the C++
I/O idioms can be difficult, the students end up writing code that does not
handle EOF well. For example, they sometimes write programs with interactive
command loops. Maybe a small game project. Their code might look like this:

```c++
int main() {
    string word;
    while (word != "quit") {
        cin >> word;
        /* Do something based on the user input. */
        do_something(word);
    }
}
```

Here, when EOF is reached, two things are true:

1. Reading from `cin` will always fail, and not block on user input
2. `word` will never update from its last value

So this loops infinitely. This means that piping input into student programs
ends poorly:

```
$ ./student_program < input.txt
Input not valid
Input not valid
Input not valid
Input not valid
Input not valid
Input not valid
Input not valid
Input not valid
Input not valid
Input not valid
.
.
.
```

In the upper-level courses the solution is to teach the students properly and
deduct points if the I/O is wrong. But in COMP 11 our primary focus is learning
computational thinking, not the particulars of the `fstream` class.

My solution to this problem was a technical one: write a program that runs the
student program, gets the output, and then terminates it --- without giving it
EOF. Since I hadn't applied any of my knowledge from the operating systems
class recently, I decided to write this program in C. I'll explain it top-down
in the comments.

(Massive props to Tom Hebb, by the way, for helping me figure out I was doing.)

```c
#include <stdio.h> /* for fputc, file pointers */
#include <unistd.h> /* for execv, pipe, read, dup2, close */
#include <sys/types.h> /* for fork, pid_t */
#include <sys/wait.h> /* for usleep */
#include <stdlib.h> /* for exit */
#include <assert.h> /* for assert */
#include <signal.h> /* for kill */
#include <strings.h> /* for bzero */

const int FREAD = 0, FWRITE = 1;

/*
  We lay out which end of the pipe is the read end and which end is the write
  end --- notice how each pipe has space for 2 file descriptors. We'll be
  making a couple pipes to move the output from the child process to the parent
  process. Might as well make creating them easier.
*/
void make_pipe(int p[2]) {
    if (pipe(p) != 0) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }
}

/*
  Problem: Some student programs do not handle EOF well, maybe looping forever
           printing the same thing. This happens when we don't teach them how
           to do proper I/O in C++.
  Idea: Make some pipes for stdin, stdout, stderr. Spawn the first argument as
        a child process, replacing its stdin, stdout, and stderr with these
        pipes. Provide the next arguments as CLI arguments to the child. Print
        the child's output to the parent's output. Wait a bit for the output to
        flush. Kill the program. Clean up.
*/
int main(int argc, char *argv[]) {
    pid_t childpid;

    /* Takes command to be run as first argument. It can optionally have
       additional CLI options. */
    assert(argc >= 2);
    const char *binname = argv[1];

    /* The parent will write to the child's stdin. */
    int child_stdin[2];
    make_pipe(child_stdin);

    /* The child will write to the parent's stdout/stderr. */
    int child_stdout[2];
    make_pipe(child_stdout);
    int child_stderr[2];
    make_pipe(child_stderr);

    /* Fork. */
    if ((childpid = fork()) == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    /* If in the child... */
    if (childpid == 0) {
        /* Close the ends of the pipes that the child will not use. */
        close(child_stdin[FWRITE]);
        close(child_stdout[FREAD]);
        close(child_stderr[FREAD]);

        /* Replace stdin, stdout, and stderr with the pipes instead. */
        dup2(child_stdin[FREAD], STDIN_FILENO);
        dup2(child_stdout[FWRITE], STDOUT_FILENO);
        dup2(child_stderr[FWRITE], STDERR_FILENO);

        /* Exec binname with the given argv. */
        execv(binname, &argv[1]);
        /* Never returns. */
    }
    /* If in the parent... */
    else {
        /* Close the ends of the pipes that the parent will not use. */
        close(child_stdin[FREAD]);
        close(child_stdout[FWRITE]);
        close(child_stderr[FWRITE]);

#define SIZE 1000
        char buf[SIZE] = { 0 };

        char *line = NULL;
        size_t len = 0;
        ssize_t nread;

        /* Slurp all of stdin, piping it to the child process. */
        while ((nread = read(STDIN_FILENO, buf, SIZE)) > 0) {
            write(child_stdin[FWRITE], buf, nread);
            bzero(buf, SIZE);
        }

        const unsigned SECONDS = 1000000;
        usleep(1 * SECONDS);
        kill(childpid, SIGINT);

        bzero(buf, SIZE);

        /* Read all of the child process's output, printing it. */
        while ((nread = read(child_stdout[FREAD], buf, SIZE)) > 0) {
            write(fileno(stdout), buf, nread);
            bzero(buf, SIZE);
        }

        /* Read all of the child process's error output, printing it. */
        while ((nread = read(child_stderr[FREAD], buf, SIZE)) > 0) {
            write(fileno(stderr), buf, nread);
            bzero(buf, SIZE);
        }

        /* Clean up. */
        close(child_stdin[FWRITE]);
        close(child_stdout[FREAD]);
        close(child_stderr[FREAD]);
#undef SIZE
    }
}
```
