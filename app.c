#include <stdio.h>
#include <unistd.h>

int main() {
    int pc = 0; 

    while (1) { 
        printf("App A1 rodando... (PC=%d)\n", pc++);
        fflush(stdout);
        sleep(1);
    }

    return 0;
}