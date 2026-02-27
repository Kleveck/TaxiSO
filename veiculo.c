#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE 700

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "comum.h"

int cancelado = 0;

void handle_cancel(int sig) {
    (void)sig;
    cancelado = 1;
}

static void enviar_msg_cliente(pid_t pid_cliente, int id_servico, const char *texto) {
    char pipe_cliente[40];
    sprintf(pipe_cliente, PIPE_CLIENTE, pid_cliente);

    int fd = open(pipe_cliente, O_WRONLY);
    if (fd == -1) {
        return;
    }

    resposta rsp;
    memset(&rsp, 0, sizeof(rsp));
    rsp.status = 1;
    rsp.id_servico = id_servico;
    strncpy(rsp.msg, texto, sizeof(rsp.msg) - 1);
    rsp.msg[sizeof(rsp.msg) - 1] = '\0';

    write(fd, &rsp, sizeof(resposta));
    close(fd);
}

int main(int argc, char *argv[]) {
    setbuf(stdout, NULL);

    if (argc < 4) {
        printf("Erro: Veiculo iniciado sem argumentos suficientes.\n");
        printf("Uso: veiculo <id_servico> <distancia_km> <pid_cliente>\n");
        return 1;
    }

    int id_servico = atoi(argv[1]);
    int distancia  = atoi(argv[2]);
    pid_t pid_cliente = (pid_t)atoi(argv[3]);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_cancel;
    sigaction(SIGUSR1, &sa, NULL);

    enviar_msg_cliente(pid_cliente, id_servico, "[Veiculo] TAXI CHEGOU.");

    printf("INICIO_VIAGEM %d\n", id_servico);
    fflush(stdout);

    float progresso = 0.0f;
    //int tempo_por_km = 5;
    int tempo_por_km = INTERVALO_TEMPO;
    int tempo_total = distancia * tempo_por_km;  
    int tempo_por_passo = tempo_total / 10;      

    while (progresso < 100.0f && !cancelado) {
        sleep(tempo_por_passo);  
        progresso += 10.0f;
        printf("PERC: %.0f\n", progresso);
    }

    if (cancelado) {

        int perc = (int)progresso;
        if (perc < 0) perc = 0;
        if (perc > 100) perc = 100;

        // REPORTAR PERCENTAGEM FINAL ANTES DE "Cancelado"
        printf("PERC: %d\n", perc);
        fflush(stdout);

        printf("Cancelado\n");
        fflush(stdout);

         int km_efetivos = (distancia * perc) / 100;

        char msg[MSG_SIZE];
        sprintf(msg,  "[Veiculo] Viagem cancelada! Distancia percorrida: %d km. Valor a pagar: %d EUR", km_efetivos, km_efetivos);
        enviar_msg_cliente(pid_cliente, id_servico, msg);
    } else {
        printf("Concluido\n");

        char msg[MSG_SIZE];
        sprintf(msg, "[Veiculo] Viagem concluida! Valor a pagar: %d EUR", distancia);
        enviar_msg_cliente(pid_cliente, id_servico, msg);
    }

    return 0;
}
