#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include "tokenizer.h"

/* Convenience macro to silence compiler warnings abcout unused function parameters. */
#define unused __attribute__((unused))
#define ARGSNUM 10

/* Whether the shell is connected to an actual terminal or not. */
bool shell_is_interactive;

/* File descriptor for the shell input */
int shell_terminal;

// pgid that matches pid of the first process of a session
static int session_pgid = 0;

/* Terminal mode settings for the shell */
struct termios shell_tmodes;

/* Process group id for the shell */
pid_t shell_pgid;

int cmd_exit(struct tokens* tokens);
int cmd_help(struct tokens* tokens);

int cmd_pwd(struct tokens* tokens);  //实现ls命令
int cmd_cd(struct tokens* tokens);  //实现cd命令
void signal_handler(int sig);  //signal 处理函数
int cmd_wait(struct tokens* tokens);  //实现wait命令
char* find_cmd_path(char* cmd_name);  //程序环境变量读取路径
void run_program(char** args, int in_fd, int out_fd,int run_bg);  //程序运行
void cmd_others(struct tokens* tokens);  //实现其他命令

/* Built-cin command functions take token array (see parse.h) and return int */
typedef int cmd_fun_t(struct tokens* tokens);

/* Built-cin command struct and lookup table */
typedef struct fun_desc {
  cmd_fun_t* fun;
  char* cmd;
  char* doc;
} fun_desc_t;

int ignore_signals[] = {
  SIGINT, SIGQUIT, SIGTERM, SIGTSTP, SIGCONT, SIGTTIN, SIGTTOU
};

fun_desc_t cmd_table[] = {
    {cmd_help, "?", "show this help menu"},
    {cmd_exit, "exit", "exit the command shell"},
    {cmd_pwd, "pwd", " pwd command"},
    {cmd_cd, "cd", "cd command"},
    {cmd_wait, "wait", "wait command"},

};

/* Prints a helpful description for the given command */
int cmd_help(unused struct tokens* tokens) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    printf("%s - %s\n", cmd_table[i].cmd, cmd_table[i].doc);
  return 1;
}

/* Exits this shell */
int cmd_exit(unused struct tokens* tokens) { exit(0); }

//实现pwd命令
int cmd_pwd(unused struct tokens* tokens) {
  char buf[4096];
  getcwd(buf, 4096);
  printf("%s\n", buf);
  return 1;
}

//实现cd命令
int cmd_cd(unused struct tokens* tokens) {
  //如果输入的参数是一个，则回到当前HOME目录
  if (tokens_get_length(tokens) == 1) { 
    chdir(getenv("HOME"));
  } 
  //如果输入的参数是两个，则回到第二个参数所指定的目录
  else if (tokens_get_length(tokens) == 2) {
    if (chdir(tokens_get_token(tokens, 1)) == -1) {//如果目录不存在，则报错
      printf("cd: %s: No such file or directory\n", tokens_get_token(tokens, 1));
    }
  } else {
    printf("cd: too many arguments\n");
  }
  return 1;
}

//实现wait命令
int cmd_wait(unused struct tokens *tokens) {
  int status, pid;
  pid = waitpid(-1, &status, WUNTRACED);

  if (pid > 0) {
    if (WIFEXITED(status)) {
      printf("Process %d exited with status %d\n", pid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
      printf("Process %d terminated by signal %d\n", pid, WTERMSIG(status));
    } else if (WIFSTOPPED(status)) {
      // 将停止的进程调回前台
      tcsetpgrp(shell_terminal, getpgid(pid));
      waitpid(pid, &status, WCONTINUED);
      tcsetpgrp(shell_terminal, shell_pgid);
      printf("Process %d stopped by signal %d\n", pid, WSTOPSIG(status));
    } else if (WIFCONTINUED(status)) {
      printf("Process %d continued\n", pid);
    }
  }
  return 1;
}
//程序环境变量读取路径
char* find_cmd_path(char* cmd_name) {
  if (access(cmd_name, F_OK) == 0) return cmd_name;
  char* path_name = malloc(1024);
  strcpy(path_name, getenv("PATH"));
  if (path_name == NULL) {
    return NULL;
  }
  char* token = strtok(path_name, ":");
  char* cmd_path = malloc(128);
  while (token != NULL) {
    strcpy(cmd_path, token);
    strcat(cmd_path, "/");
    strcat(cmd_path, cmd_name);
    if (access(cmd_path, F_OK) == 0) {
      free(path_name);
      return cmd_path;
    }
    token = strtok(NULL, ":");
  }
  free(path_name);
  return NULL;
}

//程序运行
void run_program(char** args, int in_fd, int out_fd, int run_bg) {
  pid_t fork_pid = fork();

  if (fork_pid == -1) {
    printf("Failed to create new process: %s\n", strerror(errno));
  }
  if (fork_pid > 0) {
    if (!run_bg) {
      int status;
      if (!session_pgid) {
        session_pgid = fork_pid;
      }
      setpgid(fork_pid, session_pgid);
      waitpid(fork_pid, &status, 0);
      if (status != EXIT_SUCCESS) {
        if (status != SIGINT && status != SIGQUIT) {
          printf("Program failed: %d\n", status);
        }
      }
    }
  }
  if (fork_pid == 0) {
    if (run_bg) {
      setpgid(0, 0);
    }
    if (in_fd) {
      dup2(in_fd, STDIN_FILENO);
    }
    if (out_fd) {
      dup2(out_fd, STDOUT_FILENO);
    }
    execv(find_cmd_path(args[0]), args);
    exit(EXIT_FAILURE);
  }
}

//实现其他命令
void cmd_others(unused struct tokens* tokens) {
  char* args[ARGSNUM];
  char* token;
  int pipe_fds[2];
  int token_num = 0, arg_num = 0;
  int cin = 0, cout = 0;
  int run_bg = 0; // 新增变量，用于标识是否后台运行
  int length = tokens_get_length(tokens);
  if(length == 0) return;   

  for (; token_num < tokens_get_length(tokens); token_num++,arg_num++) {
    token = tokens_get_token(tokens, token_num);
    if (token[0] == '&') {
      run_bg = 1;
      arg_num--;
      continue; // 跳过后台运行符，不加入参数列表
    }
    // 管道符
    if (token[0] == '|') {
      if (pipe(pipe_fds) == -1) {
        printf("Failed to create new pipe\n");
        return;
      }
      cout = pipe_fds[1];
      args[arg_num] = NULL;
      run_program(args, cin, cout, run_bg);
      close(cout);

      if (cin) {
        close(cin);
      }
      cin = pipe_fds[0];
      arg_num = -1;
    } else if (token[0] == '>') {
      token_num++;
      arg_num = 0;  // 重置为下一个参数的开始位置
      cout = creat(tokens_get_token(tokens, token_num), S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
      if (cout == -1) {
        exit(EXIT_FAILURE);
      }
      // 重定向输入
    } else if (token[0] == '<') {
      arg_num++;
      token_num++;
      arg_num = 0;  // 重置为下一个参数的开始位置
      cin = open(tokens_get_token(tokens, token_num), O_RDONLY);
      if (cin == -1) {
        exit(EXIT_FAILURE);
      }
    } else {
      // 普通参数
      args[arg_num] = token;
    }
  }
    args[arg_num] = NULL;
    for(int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++){
      signal(ignore_signals[i], SIG_DFL);
    }
  run_program(args, cin, cout, run_bg);
  session_pgid = 0; // 当前命令结束后，重置 pgid
    
}

/* Looks up the built-cin command, if it exists. */
int lookup(char cmd[]) {
  for (unsigned int i = 0; i < sizeof(cmd_table) / sizeof(fun_desc_t); i++)
    if (cmd && (strcmp(cmd_table[i].cmd, cmd) == 0))
      return i;
  return -1;
}

/* Intialization procedures for this shell */
void init_shell() {
  /* Our shell is connected to standard input. */
  shell_terminal = STDIN_FILENO;

  /* Check if we are running interactively */
  shell_is_interactive = isatty(shell_terminal);

  if (shell_is_interactive) {
    /* If the shell is not currently cin the foreground, we must pause the shell until it becomes a
     * foreground process. We use SIGTTIN to pause the shell. When the shell gets moved to the
     * foreground, we'll receive a SIGCONT. */
    while (tcgetpgrp(shell_terminal) != (shell_pgid = getpgrp()))
      kill(-shell_pgid, SIGTTIN);

    /* Saves the shell's process id */
    shell_pgid = getpid();

    /* Take control of the terminal */
    tcsetpgrp(shell_terminal, shell_pgid);

    /* Save the current termios to a variable, so it can be restored later. */
    tcgetattr(shell_terminal, &shell_tmodes);

    for (int i = 0; i < sizeof(ignore_signals) / sizeof(int); i++){
    signal(ignore_signals[i], SIG_IGN);
  }
  }
}

int main(unused int argc, unused char* argv[]) {
  init_shell();

  static char line[4096];
  int line_num = 0;

  /* Please only print shell prompts when standard input is not a tty */
  if (shell_is_interactive)
    fprintf(stdout, "%d: ", line_num);

  while (fgets(line, 4096, stdin)) {
    /* Split our line into words. */
    struct tokens* tokens = tokenize(line);

    /* Find which built-cin function to run. */
    int fundex = lookup(tokens_get_token(tokens, 0));

    if (fundex >= 0) {
      cmd_table[fundex].fun(tokens);
    }  else {
      cmd_others(tokens);
}

    if (shell_is_interactive)
      /* Please only print shell prompts when standard input is not a tty */
      fprintf(stdout, "%d: ", ++line_num);

    /* Clean up memory */
    tokens_destroy(tokens);
  }

  return 0;
}
