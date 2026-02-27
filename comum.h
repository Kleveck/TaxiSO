#ifndef COMUM_H
#define COMUM_H

#include <sys/types.h>
#include <unistd.h>

#define PIPE_NAME "pipe_controlador"
#define MSG_SIZE 100
#define PIPE_CLIENTE "cliente_%d"

#define MAX_USERS 30
#define MAX_SERVICOS 100
#define INTERVALO_TEMPO 5
//#define INTERVALO_TEMPO 1

typedef enum CommandType{
    listar,
    utiliz,
    frota,
    cancelar,
    km,
    hora,
    terminar,
    invalido
} CommandType;

typedef enum request_type{
    REQ_LOGIN,
    REQ_AGENDAR,
    REQ_CANCELAR,
    REQ_CONSULTAR,    
    REQ_HORA_ATUAL,
    REQ_SAIR
} request_type;

typedef struct dados_login{
    char username[20];
} dados_login;

typedef struct dados_agendamento{
    int hora;
    int distancia;
} dados_agendamento;

typedef struct pedido {
    pid_t pid;
    request_type type;
    union {
        dados_login login;
        dados_agendamento agendar;
        int id_cancelar;
        int id_consultar;
    } dados;
    char msg[MSG_SIZE];
}pedido;

typedef struct resposta {
    int status;
    int id_servico;
    char msg[MSG_SIZE];
}resposta;

typedef struct {
    pid_t pid;
    char username[20];
    int em_viagem;
} Utilizador;

typedef struct {
    int   id;
    pid_t pid_cliente;
    pid_t pid_veiculo;
    int   fd_veiculo;
    char  username[20];
    int   distancia;       
    int   hora_agendada;   
    int   status;          
    int   perc_concluida; 
} Servico;


#endif