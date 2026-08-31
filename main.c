#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RL_BUF_SZ 1024
#define PL_BUF_SZ 64
#define PL_DELIMITER " \t\r\n\a"

typedef struct {
    char **args;
    char *in_file;
    char *out_file;
    int append;
} Command;

typedef struct {
    Command *commands;
    int count;
} Pipeline;

char *read_line() {
    int buf_size = RL_BUF_SZ;
    char *buf = malloc(sizeof(char) * buf_size);
    if (!buf) {
        fprintf(stderr, "Could not allocate buffer memory\n");
        exit(EXIT_FAILURE);
    }
    int pos = 0;
    int c;

    while (1) {
        c = getchar();
        if (c == EOF || c == '\n') { // If line break or EOF then end the
                                     // buffer and return it
            buf[pos] = '\0';
            return buf;
        } else {
            buf[pos] = c;
        }
        pos++;

        if (pos >= buf_size) {
            buf_size += RL_BUF_SZ;
            buf = realloc(buf, buf_size);
            if (!buf) {
                fprintf(stderr, "Could not reallocate buffer size");
                exit(EXIT_FAILURE);
            }
        }
    }
}

Pipeline parse_line(char *line) {
    int buf_size = PL_BUF_SZ;
    int pos = 0;
    char **tokens = malloc(sizeof(char *) * buf_size);
    if (!tokens) {
        fprintf(stderr, "Could not allocate memory for token buffer");
        exit(EXIT_FAILURE);
    }

    Pipeline pipeline;
    pipeline.commands = NULL;
    pipeline.count = 0;

    Command command;
    command.args = tokens;
    command.in_file = NULL;
    command.out_file = NULL;
    command.append = 0;

    char *token = strtok(line, PL_DELIMITER);
    while (token) {
        if (strcmp(token, "<") == 0) {
            token = strtok(NULL, PL_DELIMITER);
            if (token == NULL) {
                fprintf(stderr, "Expected file after '<'\n");
                free(tokens);
                exit(EXIT_FAILURE);
            }
            command.in_file = token;
        } else if (strcmp(token, ">") == 0) {
            token = strtok(NULL, PL_DELIMITER);
            if (token == NULL) {
                fprintf(stderr, "Expected file after '>'\n");
                free(tokens);
                exit(EXIT_FAILURE);
            }
            command.out_file = token;
            command.append = 0;
        } else if (strcmp(token, ">>") == 0) {
            token = strtok(NULL, PL_DELIMITER);
            if (token == NULL) {
                fprintf(stderr, "Expected file after '>>'\n");
                free(tokens);
                exit(EXIT_FAILURE);
            }
            command.out_file = token;
            command.append = 1;
        } else if (strcmp(token, "|") == 0) {
            command.args[pos] = NULL;

            pipeline.count++;
            pipeline.commands =
                realloc(pipeline.commands, sizeof(Command) * pipeline.count);
            if (!pipeline.commands) {
                fprintf(stderr,
                        "Could not allocate memory for pipeline commands");
                exit(EXIT_FAILURE);
            }
            pipeline.commands[pipeline.count - 1] = command;

            pos = 0;
            buf_size = PL_BUF_SZ;
            tokens = malloc(sizeof(char *) * buf_size);
            if (!tokens) {
                fprintf(stderr, "Could not allocate memory for token buffer");
                exit(EXIT_FAILURE);
            }

            command.args = tokens;
            command.in_file = NULL;
            command.out_file = NULL;
            command.append = 0;
        } else {
            tokens[pos] = token;
            pos++;
            if (pos >= buf_size) {
                buf_size += PL_BUF_SZ;
                tokens = realloc(tokens, sizeof(char *) * buf_size);
                if (!tokens) {
                    fprintf(stderr,
                            "Could not reallocate memory for token buffer");
                    exit(EXIT_FAILURE);
                }
                command.args = tokens;
            }
        }
        token = strtok(NULL, PL_DELIMITER); // NULL means move on to next token
    }
    command.args[pos] = NULL; // to identify end of arr

    pipeline.count++;
    pipeline.commands =
        realloc(pipeline.commands, sizeof(Command) * pipeline.count);

    if (!pipeline.commands) {
        fprintf(stderr, "Could not allocate memory for commands");
        exit(EXIT_FAILURE);
    }

    pipeline.commands[pipeline.count - 1] = command;

    return pipeline;
}

int launch(Command *command) {
    pid_t pid, wpid;

    int status;

    pid = fork();
    if (pid == 0) { // child process
        if (command->in_file != NULL) {
            int in_fd = open(command->in_file, O_RDONLY);
            if (in_fd < 0) {
                perror("Error opening input file");
                exit(EXIT_FAILURE);
            }

            if (dup2(in_fd, STDIN_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(in_fd);
        }

        if (command->out_file != NULL) {
            int flags = O_WRONLY | O_CREAT;
            if (command->append) {
                flags |= O_APPEND;
            } else {
                flags |= O_TRUNC;
            }
            int out_fd = open(command->out_file, flags, 0644);
            if (out_fd < 0) {
                perror("Error opening output file");
                exit(EXIT_FAILURE);
            }

            if (dup2(out_fd, STDOUT_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(out_fd);
        }

        if (execvp(command->args[0], command->args) == -1) {
            // execvp expects a program name (args[0]) and an array of
            // strings (args)
            perror("Unsuccesful exec()");
            exit(EXIT_FAILURE);
        }
    } else if (pid < 0) { // error forking
        perror("Could not start child process");
    } else { // fork executed succesfully; parent process
        do {
            wpid = waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
    }

    return 1;
}

// we will be forward declaring the built in commands because the help command
// uses the builtin_arr[] but the array itself contains the help command. To get
// rid of this dependency cycle I forward  declare.
int sh_cd(char **args);
int sh_help(char **args);
int sh_exit(char **args);

char *builtin_arr[] = {"cd", "help", "exit"};

int (*builtin_fx[])(char **) = {&sh_cd, &sh_help, &sh_exit};

int num_builtins = sizeof(builtin_arr) / sizeof(char *);

int sh_cd(char **args) {
    if (args[1] == NULL) { // incase nothing comes after cd
        fprintf(stderr, "Expected argument to \"cd\"\n");
    } else {
        if (chdir(args[1]) != 0) {
            perror("Could not change directory");
        }
    }
    return 1;
}

int sh_help(char **args) {
    printf("Anirudh's Shell\n");
    for (int i = 0; i < num_builtins; i++) {
        printf(" %s\n", builtin_arr[i]);
    }
    return 1;
}

int sh_exit(char **args) { return 0; }

// pipe[i-1] -> stdin
// pipe[i] <- stdout
int execute(Pipeline *pipeline) {
    if (pipeline->count == 1) { // only one command (no pipe)
        Command *command = &pipeline->commands[0];
        if (command->args[0] == NULL) { // empty command
            return 1;
        }

        for (int i = 0; i < num_builtins; i++) {
            if (strcmp(command->args[0], builtin_arr[i]) == 0) {
                return (*builtin_fx[i])(command->args);
            }
        }

        return launch(command);
    }

    int pipes[pipeline->count - 1][2];
    pid_t pids[pipeline->count];

    // create pipes
    for (int i = 0; i < pipeline->count - 1; i++) {
        if (pipe(pipes[i]) == -1) {
            perror("pipe");
            return 1;
        }
    }

    // fork all commands
    for (int i = 0; i < pipeline->count; i++) {
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            return 1;
        }

        if (pid == 0) {
            Command *command = &pipeline->commands[i];

            // connect previous pipe to stdin
            if (i > 0 && command->in_file == NULL) {
                if (dup2(pipes[i - 1][0], STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            // connect current pipe to stdout
            if (i < pipeline->count - 1 && command->out_file == NULL) {
                if (dup2(pipes[i][1], STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }
            }

            // input redirection
            if (command->in_file != NULL) {
                int in_fd = open(command->in_file, O_RDONLY);
                if (in_fd < 0) {
                    perror("Error opening input file");
                    exit(EXIT_FAILURE);
                }

                if (dup2(in_fd, STDIN_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }

                close(in_fd);
            }

            // output redirection
            if (command->out_file != NULL) {
                int flags = O_WRONLY | O_CREAT;
                if (command->append) {
                    flags |= O_APPEND;
                } else {
                    flags |= O_TRUNC;
                }

                int out_fd = open(command->out_file, flags, 0644);

                if (out_fd < 0) {
                    perror("Error opening output file");
                    exit(EXIT_FAILURE);
                }

                if (dup2(out_fd, STDOUT_FILENO) == -1) {
                    perror("dup2");
                    exit(EXIT_FAILURE);
                }

                close(out_fd);
            }

            // close all pipe file descriptors
            for (int j = 0; j < pipeline->count - 1; j++) {
                close(pipes[j][0]);
                close(pipes[j][1]);
            }

            if (execvp(command->args[0], command->args) == -1) {
                perror("execvp");
                exit(EXIT_FAILURE);
            }
        }
        pids[i] = pid;
    }

    // parent closes all pipefds
    for (int i = 0; i < pipeline->count - 1; i++) {
        close(pipes[i][0]);
        close(pipes[i][1]);
    }

    // wait for children
    for (int i = 0; i < pipeline->count; i++) {
        waitpid(pids[i], NULL, 0);
    }

    return 1;
}

void sh_loop() {
    char *line;
    Pipeline pipeline;
    int status;
    char cwd_buf[1024];
    do {
        if (getcwd(cwd_buf, sizeof(cwd_buf)) == NULL) {
            perror("Error getting current directory");
            break;
        }
        char *home = getenv("HOME");
        if (home != NULL && strncmp(cwd_buf, home, strlen(home)) == 0) {
            printf("~%s $ ", cwd_buf + strlen(home));
        } else {
            printf("%s $ ", cwd_buf);
        }
        // read
        line = read_line();
        // parse
        pipeline = parse_line(line);
        // execute
        status = execute(&pipeline);
        // freeing
        free(line);
        for (int i = 0; i < pipeline.count; i++) {
            free(pipeline.commands[i].args);
        }
        free(pipeline.commands);
    } while (status);
}

int main(int argc, char *argv[]) {
    sh_loop();

    return EXIT_SUCCESS;
}
