/*******************************************************************************
 * APP - Processo de Aplicação para Simulação de Sistema Operacional
 *
 * Este módulo implementa um processo de aplicação que executa instruções
 * sequenciais e realiza chamadas de sistema (syscalls) para operações de I/O.
 * Cada instância deste programa representa um processo independente gerenciado
 * pelo kernel.
 *
 * Funcionalidades principais:
 *   - Execução sequencial de instruções com Program Counter (PC)
 *   - Syscalls de I/O em pontos predefinidos da execução
 *   - Comunicação com o kernel via pipes bidirecionais
 *   - Restauração de contexto após operações de I/O
 *
 * Comportamento:
 *   - Executa 30 instruções (PC de 0 a 29)
 *   - Faz syscall READ nos PCs 5 e 15
 *   - Faz syscall WRITE nos PCs 10 e 20
 *   - Comunica-se com o kernel através de pipes
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>

#define MAX_ITERATIONS 10

/*******************************************************************************
 * VARIÁVEIS GLOBAIS
 ******************************************************************************/
int pc = 0;
int pipe_from_kernel_fd;
int pipe_to_kernel_fd;
int use_io = 0; // 0 = sem I/O, 1 = com I/O

/*
 * SyscallContext - Contexto de uma chamada de sistema
 *
 * Estrutura enviada ao kernel quando o processo faz uma syscall de I/O.
 * Permite que o kernel saiba exatamente onde o processo estava e o que
 * ele quer fazer.
 *
 * Campos:
 *   pc        - Program Counter no momento da syscall
 *   operation - Tipo de operação ('R' para READ, 'W' para WRITE)
 */
typedef struct {
    int pc;
    char operation;
} SyscallContext;

/*******************************************************************************
 * FUNÇÕES DO PROCESSO
 ******************************************************************************/

/*******************************************************************************
 * syscall_io - Realiza uma chamada de sistema para operação de I/O
 *
 * Esta função simula uma syscall de entrada/saída, enviando uma requisição
 * ao kernel e sinalizando através de um sinal. O processo será bloqueado
 * pelo kernel até que a operação de I/O seja concluída.
 *
 * Parâmetros:
 *   operation - Tipo de operação ('R' para READ, 'W' para WRITE)
 *
 * Fluxo de execução:
 *   1. Identifica o PID do kernel (processo pai)
 *   2. Registra a syscall no log para debug
 *   3. Prepara o contexto com PC atual e operação solicitada
 *   4. Envia o contexto ao kernel através do pipe
 *   5. Sinaliza o kernel com SIGUSR2 (IRQ2)
 *
 * Comportamento esperado após a chamada:
 *   - O kernel receberá o sinal e lerá o contexto do pipe
 *   - O processo será bloqueado (SIGSTOP) pelo kernel
 *   - Quando o I/O terminar, o kernel restaurará o contexto
 *   - O processo continuará executando do PC onde parou
 *
 * Importante:
 *   - Esta função não retorna imediatamente ao chamador
 *   - O processo fica suspenso até o kernel liberá-lo
 ******************************************************************************/
void syscall_io(char operation) {
    pid_t kernel_pid = getppid();

    if (operation == 'R') {
        printf("  App (PID %d, PC=%d): syscall READ do disco D1\n", getpid(), pc);
    } else if (operation == 'W') {
        printf("  App (PID %d, PC=%d): syscall WRITE no disco D1\n", getpid(), pc);
    }

    SyscallContext ctx = { .pc = pc, .operation = operation };
    write(pipe_to_kernel_fd, &ctx, sizeof(SyscallContext));
    kill(kernel_pid, SIGUSR2);
}

/*******************************************************************************
 * main - Ponto de entrada do processo de aplicação
 *
 * Inicializa o processo de aplicação, configura a comunicação com o kernel
 * e executa um loop de instruções sequenciais com syscalls de I/O em pontos
 * predefinidos.
 *
 * Parâmetros:
 *   argc - Número de argumentos da linha de comando
 *   argv - Array de argumentos:
 *          argv[1] = file descriptor do pipe kernel→app (para receber dados)
 *          argv[2] = file descriptor do pipe app→kernel (para enviar dados)
 *
 * Fluxo de execução:
 *   1. Valida os argumentos (deve receber 2 file descriptors)
 *   2. Configura os pipes para comunicação com o kernel
 *   3. Coloca o pipe de leitura em modo não-bloqueante
 *   4. Entra no loop principal de execução:
 *      a. Verifica se há contexto restaurado do kernel (novo PC)
 *      b. Executa a instrução atual (incrementa PC)
 *      c. Faz syscall de I/O em PCs específicos:
 *         - PC 5 e 15: READ
 *         - PC 10 e 20: WRITE
 *      d. Aguarda 1 segundo entre instruções
 *   5. Fecha os pipes ao terminar
 *
 * Restauração de contexto:
 *   - O kernel envia um novo PC através do pipe quando o processo retorna
 *     de uma operação de I/O bloqueante
 *   - O pipe não-bloqueante permite verificar sem travar o processo
 *
 * Syscalls de I/O:
 *   - READ (R): Simula leitura do disco D1
 *   - WRITE (W): Simula escrita no disco D1
 *   - Cada syscall bloqueia o processo até a conclusão
 *
 * Término:
 *   - O processo termina após executar MAX_ITERATIONS (30) instruções
 *   - Fecha os pipes antes de sair
 *
 * Retorna:
 *   0 em caso de término normal
 ******************************************************************************/
int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Uso: app <fd_from_kernel> <fd_to_kernel> <use_io>\n");
        exit(1);
    }

    pipe_from_kernel_fd = atoi(argv[1]);
    pipe_to_kernel_fd = atoi(argv[2]);
    use_io = atoi(argv[3]); // 0 = sem IO, 1 = com IO

    fcntl(pipe_from_kernel_fd, F_SETFL, O_NONBLOCK);

    printf("App iniciado (PID %d) - IO=%d - Pipes K→A:%d / A→K:%d\n",
           getpid(), use_io, pipe_from_kernel_fd, pipe_to_kernel_fd);
    fflush(stdout);

    while (pc < MAX_ITERATIONS) {
        int restored_pc = -1;
        if (read(pipe_from_kernel_fd, &restored_pc, sizeof(int)) > 0) {
            // Restaurando contexto — não incrementa PC agora
            pc = restored_pc;
            printf("  App (PID %d): restaurando contexto (PC=%d)\n", getpid(), pc);
            fflush(stdout);
        } else {
            // Executando instrução normal
            printf("  App (PID %d): executando instrucao (PC=%d)\n", getpid(), pc);
            fflush(stdout);

            if (use_io) {
                if (pc == 5 || pc == 15) syscall_io('R');
                else if (pc == 10 || pc == 20) syscall_io('W');
            }

            pc++; // só incrementa se for instrução normal
        }

        sleep(1);
    }


    close(pipe_from_kernel_fd);
    close(pipe_to_kernel_fd);
    return 0;
}
