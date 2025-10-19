/*******************************************************************************
 * INTERCONTROLLERSIM - Simulador de Controlador de Interrupções e I/O
 *
 * Este módulo simula um controlador de hardware que gerencia dois tipos de
 * interrupções essenciais para o funcionamento do sistema operacional:
 *
 * 1. IRQ0 (Clock/Timer): Interrupção periódica que sinaliza o fim do time slice
 *    - Enviada a cada TIME_SLICE_SECONDS (1 segundo)
 *    - Permite ao kernel implementar escalonamento preemptivo
 *    - Implementa o conceito de quantum de tempo do Round-Robin
 *
 * 2. IRQ1 (I/O Complete): Interrupção que sinaliza conclusão de operação de I/O
 *    - Enviada após IO_DURATION_SECONDS (3 segundos) da requisição
 *    - Simula o tempo que um dispositivo real levaria para completar I/O
 *    - Permite ao kernel desbloquear processos que aguardam I/O
 *
 * O controlador funciona de forma independente como um processo separado,
 * comunicando-se com o kernel exclusivamente através de sinais Unix.
 ******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>

#define TIME_SLICE_SECONDS 1
#define IO_DURATION_SECONDS 3

/*******************************************************************************
 * VARIÁVEIS GLOBAIS
 ******************************************************************************/
pid_t kernel_pid;

/*******************************************************************************
 * HANDLERS DE SINAL
 ******************************************************************************/

/*******************************************************************************
 * handle_io_request - Handler para requisições de I/O do kernel
 *
 * Chamado quando o kernel sinaliza (via SIGUSR2) que um processo iniciou uma
 * operação de entrada/saída. Este handler simula o tempo de processamento do
 * dispositivo de I/O e notifica o kernel quando a operação é concluída.
 *
 * Parâmetros:
 *   sig - Número do sinal recebido (SIGUSR2)
 *
 * Fluxo de execução:
 *   1. Registra o recebimento da requisição de I/O
 *   2. Simula o tempo de processamento do dispositivo (sleep)
 *   3. Envia IRQ1 (SIGALRM) ao kernel indicando conclusão
 *   4. Registra o envio da interrupção
 *
 * Comportamento simulado:
 *   - Representa o tempo que um disco rígido ou outro dispositivo levaria
 *     para completar uma operação de leitura ou escrita
 *   - Durante o sleep, o controlador está "processando" a operação
 *   - Após o sleep, o "hardware" notifica o kernel via interrupção
 *
 * Importante:
 *   - O sleep bloqueia apenas este processo, não afeta o kernel
 *   - Simula latência realista de dispositivos de I/O
 *   - Permite que o kernel continue escalonando outros processos
 *
 * Contexto: Handler de sinal - executado de forma assíncrona
 ******************************************************************************/
void handle_io_request(int sig) {
    printf("InterControllerSim: pedido de I/O recebido, gerando IRQ1 em %d segundos...\n", IO_DURATION_SECONDS);
    fflush(stdout);

    sleep(IO_DURATION_SECONDS);

    kill(kernel_pid, SIGALRM);
    printf("InterControllerSim: IRQ1 enviado ao kernel.\n");
    fflush(stdout);
}

/*******************************************************************************
 * main - Ponto de entrada do controlador de interrupções
 *
 * Inicializa o simulador de controlador de hardware e entra em um loop infinito
 * gerando interrupções de clock (IRQ0) periodicamente e respondendo a requisições
 * de I/O do kernel.
 *
 * Parâmetros:
 *   argc - Número de argumentos (não utilizado)
 *   argv - Array de argumentos (não utilizado)
 *
 * Fluxo de execução:
 *   1. Identifica o PID do kernel (processo pai)
 *   2. Registra o handler para requisições de I/O (SIGUSR2)
 *   3. Entra em loop infinito:
 *      a. Aguarda TIME_SLICE_SECONDS (1 segundo)
 *      b. Envia IRQ0 (SIGUSR1) ao kernel
 *      c. Repete indefinidamente
 *
 * Funcionamento das interrupções:
 *
 *   IRQ0 (Timer/Clock):
 *     - Gerada automaticamente a cada 1 segundo
 *     - Sinaliza o fim do quantum de tempo do processo atual
 *     - Permite implementação de escalonamento preemptivo
 *     - Enviada via SIGUSR1
 *
 *   IRQ1 (I/O Complete):
 *     - Gerada sob demanda quando kernel solicita I/O (SIGUSR2)
 *     - Tratada pelo handler handle_io_request
 *     - Simula latência de 3 segundos do dispositivo
 *     - Enviada via SIGALRM após conclusão
 *
 * Arquitetura:
 *   - Processo independente que simula hardware
 *   - Comunicação assíncrona via sinais Unix
 *   - Não compartilha memória com kernel ou apps
 *   - Representa controlador de interrupções + dispositivo de I/O
 *
 * Importante:
 *   - O loop é infinito, o processo roda durante toda a vida do sistema
 *   - O sleep no loop principal não interfere no handler de I/O
 *   - Handlers de sinal são executados de forma assíncrona
 *   - Representa fielmente o comportamento de hardware real
 *
 * Retorna:
 *   0 (teoricamente, mas na prática nunca retorna)
 ******************************************************************************/
int main(int argc, char *argv[]) {
    kernel_pid = getppid();
    printf("InterControllerSim: Iniciado. Kernel PID = %d\n", kernel_pid);
    fflush(stdout);

    signal(SIGUSR2, handle_io_request);

    while (1) {
        sleep(TIME_SLICE_SECONDS);
        kill(kernel_pid, SIGUSR1);
    }

    return 0;
}
