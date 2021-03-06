#include <syscall.h>
#include <process/signal.h>
#include "keycode2ascii.h"
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include "functions.h"

char buf[1024];
char command[1024];
int cmd_cur = 0;

void promote() {
    sys_getcwd(buf, 1024);
    printf("shell %s> ", buf);
    fflush(stdout);
}

void parse_cmd() {
    command[cmd_cur] = '\0';
    char *argument= strchr(command, ' ');
    if (argument) {
        *argument = '\0';
    }
    // shell内部功能
    for (int i = 0; functionList[i].name != nullptr ;i++) {
        if (strcmp(functionList[i].name, command) == 0) {
            functionList[i].func(argument? argument + 1: nullptr);
            return;
        }
    }
    // 调用外部程序
    sprintf(buf, "/usr/bin/%s.run", command);
    struct stat st{};
    if (stat(buf, &st) == -1) {
        printf("no such file or directory %s\n", buf);
        return;
    }
    if (argument) {
        *argument = ' ';
        strcat(buf, argument);
        // sprintf(buf, "/usr/bin/%s.run %s", command, argument + 1);
    }
    pid_t pid = sys_create_process_from_file(buf);
    sys_waitpid(pid);
}

int main(int argc, char **argv) {
    printf("=======MJOS START=======\n");
    sys_signal_register(SignalType::SIG_KEY);
    promote();
    int len;
    char c;
    while(1) {
        if ((len = sys_read(0, buf, 1024))) {
            for (int i = 0; i < len; i++) {
                c = buf[i];
                putchar(c);
                if (c != '\n') {
                    command[cmd_cur++] = c;
                } else {
                    parse_cmd();
                    cmd_cur = 0;
                    promote();
                }
            }
            fflush(stdout);
        }
    }
    return 0;
}

bool on_signal(const Signal *signal) {
    if (signal->type == SignalType::SIG_KEY) {
        char c = keycode2ascii(static_cast<keycode_t>(signal->value));
        if (c) {
            sys_write(0, &c, 1);
        }
    }
    return true;
}