# minsh-base

`minsh-base` is a starter repository for **Minish**, a small Unix-like shell
written in C for an undergraduate Operating Systems course.

The project is designed to teach how a shell uses operating-system interfaces
to create processes, execute programs, connect file descriptors, and manage
child processes. 

## Learning objectives

By completing the project, students should learn to:

- create child processes with `fork()`;
- replace a child process image with `execvp()`;
- wait for child processes with `wait()` or `waitpid()`;
- redirect standard input and output with `open()` and `dup2()`;
- connect commands using `pipe()`;
- install signal handlers with `sigaction()`;
- close unused file descriptors and clean up resources correctly;
- handle system-call failures without unnecessarily terminating the shell.

## Repository layout

```text
minsh-base/
├── include/
│   └── parser.h          # Parser interface supplied with the project
├── src/
│   ├── parser.c          # Lexer and parser supplied with the project
│   └── shell.c           # Main student implementation
├── Makefile
├── README.md
└── SUBMIT.md             # Student names, implementation description
```

The parser is provided so students can focus on process execution rather than
writing a lexer and parser. The main implementation task is to complete
`execute_pipeline()` in `src/shell.c` and add helper functions as needed.

## Requirements

- Linux or another POSIX-compatible environment.
- A C compiler such as GCC or Clang.
- `make`.
- A POSIX shell to run the test script.

The project uses strict compiler warnings. The default build flags include
`-std=c11 -Wall -Wextra -Wpedantic -Werror`.

## Build

From the repository root:

```sh
make
```

The executable is created at:

```text
build/minish
```

To remove generated build files:

```sh
make clean
```

## Run

Start the shell interactively:

```sh
./build/minish
```

You can also send commands to it through standard input, which is useful for
testing:

```sh
printf 'echo hello\nexit\n' | ./build/minish
```

The prompt is displayed when standard input is connected to a terminal.
