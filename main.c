#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define RL_BUF_SZ 1024
#define PL_BUF_SZ 64
#define PL_DELIMITER " \t\r\n\a"

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

char **parse_line(char *line) {
    int buf_size = PL_BUF_SZ;
    int pos = 0;
    char **tokens = malloc(sizeof(char *) * buf_size);
    if (!tokens) {
        fprintf(stderr, "Could not allocate memory for token buffer");
    }

    char *token = strtok(line, PL_DELIMITER);
    while (token) {
        tokens[pos] = token;
        pos++;
        if (pos >= buf_size) {
            buf_size += PL_BUF_SZ;
            tokens = realloc(tokens, sizeof(char *) * buf_size);
            if (!tokens) {
                fprintf(stderr, "Could not reallocate memory for token buffer");
                exit(EXIT_FAILURE);
            }
        }
        token = strtok(NULL, PL_DELIMITER); // NULL means move on to next token
    }
    tokens[pos] = NULL; // to identify end of arr
    return tokens;
}

int launch(char **args) {
    pid_t pid, wpid;
    int status;
    pid = fork();
    if (pid == 0) { // child process
        if (execvp(args[0], args) == -1) {
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

int execute(char **args) {
    if (args[0] == NULL) { // empty command
        return 1;
    }

    for (int i = 0; i < num_builtins; i++) {
        if (strcmp(args[0], builtin_arr[i]) == 0) {
            return (*builtin_fx[i])(args);
        }
    }
    return launch(
        args); // if not builtin then it just tries to launch to process
}

void sh_loop() {
    char *line;
    char **args;
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
        args = parse_line(line);
        // execute
        status = execute(args);
        // freeing
        free(line);
        free(args);
    } while (status);
}

int main(int argc, char *argv[]) {
    sh_loop();

    return EXIT_SUCCESS;
}
