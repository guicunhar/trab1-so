#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h> // <<< NOVO: Para usar sprintf

#define MAX_PROCESSES 6
#define IO_DURATION_SECONDS 3

// ... (funções da fila 'blocked_queue' continuam iguais) ...
int blocked_queue[MAX_PROCESSES];
int blocked_front = 0;
int blocked_rear = 0;
int io_in_progress = 0;
void enqueue_blocked(int pid_index) {
    blocked_queue[blocked_rear] = pid_index;
    blocked_rear = (blocked_rear + 1) % MAX_PROCESSES;
}
int dequeue_blocked() {
    if (blocked_front == blocked_rear)
        return -1;
    int pid_index = blocked_queue[blocked_front];
    blocked_front = (blocked_front + 1) % MAX_PROCESSES;
    return pid_index;
}
int blocked_is_empty() {
    return blocked_front == blocked_rear;
}
// ... (fim das funções da fila) ...


typedef enum { READY, RUNNING, BLOCKED } ProcessState;

// <<< NOVO: Esta struct deve ser IDÊNTICA à do app.c
// É usada para transferir o contexto pelo pipe
typedef struct {
    int pc;
    char operation;
} SyscallContext;


typedef struct {
    pid_t pid;
    ProcessState state;
    int io_pending;
    int io_timer;

    // --- CAMPOS DE CONTEXTO ---
    int pipe_read_fd;   // <<< NOVO: FD do pipe por onde o kernel lê o contexto
    int saved_pc;       // <<< NOVO: Para salvar o PC do app
    char syscall_param; // <<< NOVO: Para salvar o parâmetro ('R' ou 'W')

} PCB;

int num_apps = 0;
PCB *pcb_table = NULL;
pid_t controller_pid;
int current_running = -1;

void schedule() {
    // ... (função schedule continua igual) ...
    int next = -1;
    for (int i = (current_running + 1) % num_apps; ; i = (i + 1) % num_apps) {
        if (pcb_table[i].state == READY) {
            next = i;
            break;
        }
        if (i == current_running) break;
    }
    if (next == -1) {
        printf("KERNEL: Nenhum processo READY, aguardando...\n");
        fflush(stdout);
        return;
    }
    if (current_running != -1 && pcb_table[current_running].state == RUNNING) {
        printf("KERNEL: Preemptando processo A%d (PID %d)\n", current_running, pcb_table[current_running].pid);
        fflush(stdout);
        kill(pcb_table[current_running].pid, SIGSTOP);
        pcb_table[current_running].state = READY;
    }
    current_running = next;
    pcb_table[current_running].state = RUNNING;
    printf("KERNEL: Executando processo A%d (PID %d)\n", current_running, pcb_table[current_running].pid);
    fflush(stdout);
    kill(pcb_table[current_running].pid, SIGCONT);
}

void handle_irq0(int sig) {
    // ... (função handle_irq0 continua igual) ...
    // NOTA: Esta função ainda contém o Problema 1 (lógica de I/O)
    // que mencionamos, mas o foco aqui é o Problema 2.
    printf("\nKERNEL: IRQ0 (fim do time slice)\n");
    fflush(stdout);
    schedule();
}

// <<< MODIFICADO: Esta é a função que recebe a syscall
void handle_irq1(int sig) {
    printf("KERNEL: Syscall de I/O do processo A%d (PID %d)\n",
           current_running, pcb_table[current_running].pid);
    fflush(stdout);

    // --- INÍCIO DA LEITURA DO CONTEXTO ---
    SyscallContext ctx; // <<< NOVO
    int fd = pcb_table[current_running].pipe_read_fd; // <<< NOVO

    // <<< NOVO: Lê a informação (PC e OP) que o app enviou pelo pipe
    if (read(fd, &ctx, sizeof(SyscallContext)) > 0) {
        
        // <<< NOVO: Salva o contexto lido no PCB
        pcb_table[current_running].saved_pc = ctx.pc;
        pcb_table[current_running].syscall_param = ctx.operation;

        printf("KERNEL: Contexto salvo: PC=%d, OP=%c\n",
               pcb_table[current_running].saved_pc,
               pcb_table[current_running].syscall_param);
        fflush(stdout);
    } else {
        printf("KERNEL: ERRO ao ler pipe do app A%d\n", current_running);
        fflush(stdout);
    }
    // --- FIM DA LEITURA DO CONTEXTO ---


    // O resto da lógica continua igual...
    kill(pcb_table[current_running].pid, SIGSTOP);
    pcb_table[current_running].state = BLOCKED;
    pcb_table[current_running].io_pending = 1;

    enqueue_blocked(current_running);

    printf("Fila de bloqueados: [");
    for (int i = blocked_front; i != blocked_rear; i = (i + 1) % MAX_PROCESSES)
        printf(" A%d ", blocked_queue[i]);
    printf("]\n");

    if (!io_in_progress) {
        int first = dequeue_blocked();
        if (first != -1) {
            io_in_progress = 1;
            pcb_table[first].io_pending = 1;
            printf("KERNEL: Iniciando I/O de A%d (PID %d)\n", first, pcb_table[first].pid);
            kill(controller_pid, SIGUSR2);
        }
    }

    schedule();
}

void handle_io_complete(int sig) {
    // ... (função handle_io_complete continua igual) ...
    printf("\nKERNEL: IRQ1 (I/O concluída) recebido do InterControllerSim\n");
    printf("KERNEL: Fila de bloqueados após dequeue: [");
    for (int i = blocked_front; i != blocked_rear; i = (i + 1) % MAX_PROCESSES)
        printf(" A%d ", blocked_queue[i]);
    printf("]\n");
    fflush(stdout);
    io_in_progress = 0;
    for (int i = 0; i < num_apps; i++) {
        if (pcb_table[i].state == BLOCKED && pcb_table[i].io_pending) {
            pcb_table[i].state = READY;
            pcb_table[i].io_pending = 0;
            printf("KERNEL: Processo A%d (PID %d) desbloqueado\n", i, pcb_table[i].pid);
            break;
        }
    }
    if (!blocked_is_empty()) {
        int next = dequeue_blocked();
        io_in_progress = 1;
        pcb_table[next].io_pending = 1;
        printf("KERNEL: Iniciando próxima I/O de A%d (PID %d)\n", next, pcb_table[next].pid);
        kill(controller_pid, SIGUSR2);
    }
    schedule();
}

int main(int argc, char *argv[]) {
    // ... (verificação de 'num_apps' continua igual) ...
    if (argc < 2) {
        printf("Uso: %s <num_apps>\n", argv[0]);
        printf("  onde num_apps está entre 3 e 6\n");
        exit(1);
    }
    num_apps = atoi(argv[1]);
    if (num_apps < 3 || num_apps > 6) {
        printf("ERRO: num_apps deve estar entre 3 e 6\n");
        exit(1);
    }

    pcb_table = malloc(num_apps * sizeof(PCB));

    printf("KERNEL: Criando %d processos de aplicacao...\n", num_apps);
    fflush(stdout);

    for (int i = 0; i < num_apps; i++) {
        
        // --- CRIAÇÃO DO PIPE ---
        int app_pipe[2]; // <<< NOVO: [0] é para leitura, [1] é para escrita
        if (pipe(app_pipe) == -1) { // <<< NOVO: Cria o pipe
            perror("pipe");
            exit(1);
        }
        // --- FIM DA CRIAÇÃO DO PIPE ---

        pid_t pid = fork();
        if (pid == 0) {
            // --- PROCESSO FILHO (app) ---
            close(app_pipe[0]); // <<< NOVO: Filho não lê, fecha o FD de leitura

            char pipe_fd_str[20]; // <<< NOVO: Para passar o FD como string
            sprintf(pipe_fd_str, "%d", app_pipe[1]); // <<< NOVO: Converte o FD de escrita

            // <<< MODIFICADO: Passa o FD de escrita como argumento para o app
            execl("./app", "app", pipe_fd_str, NULL);
            perror("execl");
            exit(1);
        }

        // --- PROCESSO PAI (kernel) ---
        close(app_pipe[1]); // <<< NOVO: Pai não escreve, fecha o FD de escrita

        pcb_table[i].pid = pid;
        pcb_table[i].state = READY;
        pcb_table[i].io_pending = 0;
        pcb_table[i].io_timer = 0;
        
        // <<< NOVO: Guarda o FD de leitura e zera os campos de contexto
        pcb_table[i].pipe_read_fd = app_pipe[0];
        pcb_table[i].saved_pc = 0;
        pcb_table[i].syscall_param = '\0';


        printf("KERNEL: Processo A%d criado (PID %d)\n", i, pid);
        fflush(stdout);
    }

    // ... (resto do main continua igual) ...
    sleep(1);
    printf("KERNEL: Parando todos os apps inicialmente...\n");
    fflush(stdout);
    for (int i = 0; i < num_apps; i++) {
        kill(pcb_table[i].pid, SIGSTOP);
    }
    signal(SIGUSR1, handle_irq0);
    signal(SIGUSR2, handle_irq1);
    signal(SIGALRM, handle_io_complete);
    printf("KERNEL: Criando InterControllerSim...\n");
    fflush(stdout);
    controller_pid = fork();
    if (controller_pid == 0) {
        execl("./InterControllerSim", "InterControllerSim", NULL);
        perror("execl");
        exit(1);
    }
    sleep(1);
    printf("KERNEL: Iniciando escalonamento...\n");
    fflush(stdout);
    schedule();
    while (1) {
        pause();
    }
    free(pcb_table);
    return 0;
}