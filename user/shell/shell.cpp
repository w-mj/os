#include <syscall.h>
#include <process/signal.h>
#include "keycode2ascii.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main() {
    sys_signal_register(SignalType::SIG_KEY);
    char buf[214];
    sprintf(buf, "hello %s %d", "?", 123);
    sys_write(1, buf, strlen(buf));
    write(1, "Hello world\n", 12);
    volatile int a = 0;
    // fork();
    printf("hello printf\n");
//    DIR *dir;
//    struct dirent *ent;
//    if ((dir = opendir ("c:\\src\\")) != NULL) {
//      /* print all the files and directories within directory */
//      while ((ent = readdir (dir)) != NULL) {
//        printf ("%s\n", ent->d_name);
//      }
//      closedir (dir);
//    } else {
//      /* could not open directory */
//      perror ("");
//    }
    while(1);
    return 0;
}

bool on_signal(const Signal *signal) {
    char msg[] = "0";
    if (signal->type == SignalType::SIG_KEY) {
        char c = keycode2ascii(static_cast<keycode_t>(signal->value));
        if (c) {
            msg[0] = c;
            sys_write(1, msg, 1);
        }
    }
    return true;
}