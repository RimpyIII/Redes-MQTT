/* Por Prof. Daniel Batista <batista@ime.usp.br>
 * Em 16/3/2022
 * 
 * Um código simples de um servidor de eco a ser usado como base para
 * o EP1. Ele recebe uma linha de um cliente e devolve a mesma linha.
 * Teste ele assim depois de compilar:
 * 
 * ./ep1-servidor-exemplo 8000
 * 
 * Com este comando o servidor ficará escutando por conexões na porta
 * 8000 TCP (Se você quiser fazer o servidor escutar em uma porta
 * menor que 1024 você precisará ser root ou ter as permissões
 * necessárias para rodar o código com 'sudo').
 *
 * Depois conecte no servidor via telnet. Rode em outro terminal:
 * 
 * telnet 127.0.0.1 8000
 * 
 * Escreva sequências de caracteres seguidas de ENTER. Você verá que o
 * telnet exibe a mesma linha em seguida. Esta repetição da linha é
 * enviada pelo servidor. O servidor também exibe no terminal onde ele
 * estiver rodando as linhas enviadas pelos clientes.
 * 
 * Obs.: Você pode conectar no servidor remotamente também. Basta
 * saber o endereço IP remoto da máquina onde o servidor está rodando
 * e não pode haver nenhum firewall no meio do caminho bloqueando
 * conexões na porta escolhida.
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>
#include <unistd.h>

/** Para usar o mkfifo() **/
#include <sys/stat.h>
/** Para usar o open e conseguir abrir o pipe **/
#include <fcntl.h>
/** Para poder trabalhar com diretórios**/
#include <dirent.h>
/** Para poder trabalhar com multi thread**/
#include<pthread.h>


#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096


typedef struct{
    int * topic_length_msb;
    int * topic_length_lsb;
    int * connfd;
    char * topic;
    char * pathname;
}sub_par;

void *Thread1(void * args){
    sub_par * sub = args;
    int n;
    unsigned char recvline[MAXLINE + 1];
    while((n = (read(* sub->connfd, recvline, MAXLINE))) > 0){
        if ((int) recvline[0] == 192){// PINGREQ - Control packet type
            if ((int) recvline[1] != 0){
                printf("Error : PINGREQ has no variable header or payload");
                exit(6);
            }
            // Response - PINGRESP
            char pingresp[2] = {208, 0};
            write(* sub->connfd, pingresp, 2); 
        }
        if ((int) recvline[0] == 224){//DISCONNECT - Control packet type
            int fd = open(sub->pathname, O_WRONLY);
            close(fd);
            break;
        }
    }
    return NULL;

}

void * Thread2(void * args){
    sub_par * sub = args;
    int n;
    unsigned char recvline[MAXLINE + 1];
    int fd = open(sub->pathname, O_RDONLY);
    int i;
    while ((n = read(fd, recvline, MAXLINE)) > 0){
        recvline[n] = 0;
        char publish[4 + strlen(sub->topic) + strlen(recvline)];
        publish[0] = 48;
        publish[1] = 2 + strlen(sub->topic) + strlen(recvline);
        publish[2] = * sub->topic_length_msb;
        publish[3] = * sub->topic_length_lsb;
        for(i = 0; i < strlen(sub->topic); i++) publish[i + 4] = sub->topic[i];
        for(i = 0; i < strlen(recvline); i++) publish[i + 4 + strlen(sub->topic)] = recvline[i];
        write(* sub->connfd, publish, 4 + strlen(sub->topic) + strlen(recvline));
        close(fd);
        fd = open(sub->pathname, O_RDONLY);
    }
    close(fd);
    return NULL;
}



int main (int argc, char **argv) {
    /* Os sockets. Um que será o socket que vai escutar pelas conexões
     * e o outro que vai ser o socket específico de cada conexão */
    int listenfd, connfd;
    /* Informações sobre o socket (endereço e porta) ficam nesta struct */
    struct sockaddr_in servaddr;
    /* Retorno da função fork para saber quem é o processo filho e
     * quem é o processo pai */
    pid_t childpid;
    /* Armazena linhas recebidas do cliente */
    unsigned char recvline[MAXLINE + 1];
    /* Armazena o tamanho da string lida do cliente */
    ssize_t n;
   
    if (argc != 2) {
        fprintf(stderr,"Uso: %s <Porta>\n",argv[0]);
        fprintf(stderr,"Vai rodar um servidor de echo na porta <Porta> TCP\n");
        exit(1);
    }

    /* Criação de um socket. É como se fosse um descritor de arquivo.
     * É possível fazer operações como read, write e close. Neste caso o
     * socket criado é um socket IPv4 (por causa do AF_INET), que vai
     * usar TCP (por causa do SOCK_STREAM), já que o MQTT funciona sobre
     * TCP, e será usado para uma aplicação convencional sobre a Internet
     * (por causa do número 0) */
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket :(\n");
        exit(2);
    }

    /* Agora é necessário informar os endereços associados a este
     * socket. É necessário informar o endereço / interface e a porta,
     * pois mais adiante o socket ficará esperando conexões nesta porta
     * e neste(s) endereços. Para isso é necessário preencher a struct
     * servaddr. É necessário colocar lá o tipo de socket (No nosso
     * caso AF_INET porque é IPv4), em qual endereço / interface serão
     * esperadas conexões (Neste caso em qualquer uma -- INADDR_ANY) e
     * qual a porta. Neste caso será a porta que foi passada como
     * argumento no shell (atoi(argv[1]))
     */
    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(atoi(argv[1]));
    if (bind(listenfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
        perror("bind :(\n");
        exit(3);
    }

    /* Como este código é o código de um servidor, o socket será um
     * socket passivo. Para isto é necessário chamar a função listen
     * que define que este é um socket de servidor que ficará esperando
     * por conexões nos endereços definidos na função bind. */
    if (listen(listenfd, LISTENQ) == -1) {
        perror("listen :(\n");
        exit(4);
    }

    printf("[Servidor no ar. Aguardando conexões na porta %s]\n",argv[1]);
    printf("[Para finalizar, pressione CTRL+c ou rode um kill ou killall]\n");
   
    /* O servidor no final das contas é um loop infinito de espera por
     * conexões e processamento de cada uma individualmente */
	for (;;) {
        /* O socket inicial que foi criado é o socket que vai aguardar
         * pela conexão na porta especificada. Mas pode ser que existam
         * diversos clientes conectando no servidor. Por isso deve-se
         * utilizar a função accept. Esta função vai retirar uma conexão
         * da fila de conexões que foram aceitas no socket listenfd e
         * vai criar um socket específico para esta conexão. O descritor
         * deste novo socket é o retorno da função accept. */
        if ((connfd = accept(listenfd, (struct sockaddr *) NULL, NULL)) == -1 ) {
            perror("accept :(\n");
            exit(5);
        }
      
        /* Agora o servidor precisa tratar este cliente de forma
         * separada. Para isto é criado um processo filho usando a
         * função fork. O processo vai ser uma cópia deste. Depois da
         * função fork, os dois processos (pai e filho) estarão no mesmo
         * ponto do código, mas cada um terá um PID diferente. Assim é
         * possível diferenciar o que cada processo terá que fazer. O
         * filho tem que processar a requisição do cliente. O pai tem
         * que voltar no loop para continuar aceitando novas conexões.
         * Se o retorno da função fork for zero, é porque está no
         * processo filho. */
        
        if ( (childpid = fork()) == 0) {
            /**** PROCESSO FILHO ****/
            printf("[Uma conexão aberta]\n");
            /* Já que está no processo filho, não precisa mais do socket
             * listenfd. Só o processo pai precisa deste socket. */
            close(listenfd);
         
            /* Agora pode ler do socket e escrever no socket. Isto tem
             * que ser feito em sincronia com o cliente. Não faz sentido
             * ler sem ter o que ler. Ou seja, neste caso está sendo
             * considerado que o cliente vai enviar algo para o servidor.
             * O servidor vai processar o que tiver sido enviado e vai
             * enviar uma resposta para o cliente (Que precisará estar
             * esperando por esta resposta) 
             */

            /* ========================================================= */
            /* ========================================================= */
            /*                         EP1 INÍCIO                        */
            /* ========================================================= */
            /* ========================================================= */
            /*Nome : Bruno Hideki Akamine                                */
            /*NUSP : 11796322                                            */
            /* TODO: É esta parte do código que terá que ser modificada
             * para que este servidor consiga interpretar comandos MQTT  */
            int loop = 0, pid = (int) getpid();
            while ((n = (read(connfd, recvline, MAXLINE))) > 0){
                recvline[n] = 0;
                char * will_message, * client_id;
                char bits[8];
                if ((int) recvline[0] != 16 && loop == 0){
                    printf("Error : First packet must be a connect packet");
                    exit(1);
                }
                if ((int) recvline[0] == 16){ // CONNECT - Control packet type
                    if (loop != 0){
                        printf("Error : Connect packet only sent in the start of the connection");
                        exit(1);
                    }
                    if (((int) recvline[2] != 0) || ((int) recvline[3] != 4)){//Length of the packet name
                        printf("Incorrect length of the protocol name\n");
                        exit(1);
                    }
                    int i, str_len_msb, str_len_lsb, connack_response = 1, will_flag = 1;
                    unsigned int str_len;
                    
                    if (recvline[4] != 'M' || recvline[5] != 'Q' || recvline[6] != 'T' || recvline[7] != 'T'){ //Checking if the protocol name is correct.
                        printf("Incorrect protocol name\n");
                        exit(1);
                    }
                    if ((int) recvline[8] == 4) connack_response = 0;//Protocol level - if different than four the Protocol Level is not supported by the server.
                    //short int char_to_int = (int) recvline[9]; //Connect flag bits
                    /*for (i = 0; i < 8; i++){//Separating the bits of the 8th byte in char_to_int
                        bits[i] = char_to_int % 2;
                        char_to_int = char_to_int / 2;
                    }*/
                    if ((((int) recvline[9]) % 2) != 0){ // Validation of the reserved flag on the 7th byte
                        printf("Disconnecting : Reserved flag diffent than zero\n");
                        exit(1);
                    }
                    /*Assuming that the flags of the 8th byte of the variable header were set as described below:
                       Clean Session - 1
                       Will Flag - 0
                       Will Qos 1 - 0
                       Will Qos 2 - 0
                       Will Retain - 0
                       User Name - 0
                       Password - 0                                                                              */
                    /*
                    if ((int) bits[1] != 1){// Clean Session - Implementing just the case when is set to 1.
                        printf("Disconnecting : Clean Session with the flag set to 1 wasn't implement in this program\n");
                        exit(1);
                    }
                    if ((int) bits[2] != 1) will_flag = 0;
                    if ((int) bits[3] != 0 || (int) bits[4] != 0){// Will Qos- Implementing just the Qos level 0.
                        printf("Disconnecting : Qos above zero wasn't implement in this program\n");
                        exit(1);
                    }
                    if ((int) bits[5] != 0){// Will Retain - Implementing just non-retained messages.
                        printf("Disconnecting : retained messages wasn't implement in this program\n");
                        exit(1);
                    }
                    if ((int) bits[7] != 0){//User Name Flag - Not implementing clients with Username and Password
                        printf("Disconnecting : Clients with username wasn't implemented in this program\n");
                        exit(1);
                    }
                    if ((int) bits[6] != 0){//Password Flag 
                        printf("Disconnecting : Password flag set to 1 while the username flag is set to 0\n");
                        exit(1);
                    }*/
                    //Keep alive not implemented, considering that all connections have a keep alive set to zero.
                    //Not taking the id of the client. We are going to use the pid of the process as indentifier of the client.
                    
                    //Response - Connack Implementation
                    char connack[4] = {32, 2, 1, connack_response};
                    write(connfd, connack, 4);
                }
                if ((int) recvline[0] >=  48 && recvline[0] <= 63){//PUBLISH - Control packet type
                    // Ignoring the flags in the first four bits - supposing that that all flags are set to 0
                    int i;
                    int remaning_len = (int) recvline[1], topic_length = (int) recvline[3] + (int) recvline[2] * 256;
                    char topic_name[topic_length + 1], * message;
                    for(i = 0; i < topic_length ; i++)
                        topic_name[i] = recvline[i + 4];
                    topic_name[i] = 0;
                    i = i + 4;
                    message = malloc(remaning_len - (topic_length + 2) * sizeof(char));
                    for(int j = 0; j < (remaning_len - (topic_length + 2)); i++, j++)
                        message[j] = recvline[i];
                    char template[15 + topic_length];
                    template[0] = 0;
                    strcat(template, "temp.mac0352.");
                    strcat(template, topic_name);
                    strcat(template, ".");
                    DIR * d;
                    struct dirent *dir;
                    d = opendir("/tmp");
                    while ((dir = readdir(d)) != NULL){
                        int ok = 1;
                        for(i = 0; ok, i < 14 + topic_length; i++) if(template[i] != dir->d_name[i]) ok = 0;
                        if (ok) {
                            char fdname[5 + strlen(dir->d_name)];
                            fdname[0] = 0;
                            strcat(fdname, "/tmp/");
                            strcat(fdname, dir->d_name);
                            int fd = open(fdname,O_WRONLY);
                            write(fd, message, strlen(message));
                            close(fd);
                        }
                    }
                }
                if ((int) recvline[0] == 130){//SUBSCRIBE - Control packet type
                    char * topic, * pid_char, * pathname;
                    int pid_aux, remaning_len = (int) recvline[1], packet_id_msb = (int) recvline[2], packet_id_lsb = (int) recvline[3], topic_length_msb = (int) recvline[4], topic_length_lsb = (int) recvline[5], topic_length = topic_length_msb * 256 + topic_length_lsb, i;
                    topic = malloc(sizeof(char) * (topic_length + 1));//Subscribe only in one topic
                    for(i = 0, pid_aux = pid; pid_aux > 0; i++, pid_aux = pid_aux/10);
                    pid_char = malloc(sizeof(char) * (i + 1));
                    for (i--, pid_aux = pid; pid_aux > 0; i--, pid_aux = pid_aux/10) pid_char[i] = pid_aux % 10 + '0';
                    pid_char[i] = 0;
                    pathname = malloc(sizeof(char) * (20 + strlen(pid_char) + topic_length));
                    pathname[0] = 0;
                    for (i = 0; i < topic_length; i++)
                        topic[i] = recvline[i + 6];
                    topic[i] = 0;
                    if ((int) recvline[i + 6] != 0){
                        printf("Error : Only implemented Qos level 0");
                        exit(6);
                    }
                    // Response - SUBACK
                    char suback[5] = {144, 3, packet_id_msb, packet_id_lsb, 0};
                    write(connfd, suback, 5);
                    strcat(pathname, "/tmp/temp.mac0352.");
                    strcat(pathname, topic);
                    strcat(pathname, ".");
                    strcat(pathname, pid_char);
                    mkfifo((const char *) pathname, 0644);
                    /*
                    int fd = open(pathname, O_RDONLY);
                    while((n = read(fd, recvline, MAXLINE)) > 0){
                        recvline[n] = 0;
                        char publish[4 + topic_length + strlen(recvline)];
                        publish[0] = 48;
                        publish[1] = 2 + topic_length + strlen(recvline);
                        publish[2] = topic_length_msb;
                        publish[3] = topic_length_lsb;
                        for(i = 0; i < topic_length; i++) publish[i + 4] = topic[i];
                        for(i = 0; i < strlen(recvline); i++) publish[i + 4 + topic_length] = recvline[i];
                        write(connfd, publish, 4 + topic_length + strlen(recvline));
                    }
                    */
                    sub_par * args = malloc(sizeof * args);
                    args->topic_length_lsb = &topic_length_lsb;
                    args->topic_length_msb = &topic_length_msb;
                    args->connfd = &connfd;
                    args->pathname = pathname;
                    args->topic = topic;
                    pthread_t t1;
                    pthread_t t2;
                    pthread_create(&t1, NULL, Thread1, args);
                    pthread_create(&t2, NULL, Thread2, args);
                    pthread_join(t1, NULL);
                    pthread_join(t2, NULL);
                    unlink((const char * ) pathname);
                    free(args);
                    break;
                }
                if ((int) recvline[0] == 192){// PINGREQ - Control packet type
                    if ((int) recvline[1] != 0){
                        printf("Error : PINGREQ has no variable header or payload");
                        exit(6);
                    }
                    // Response - PINGRESP
                    char pingresp[2] = {208, 0};
                    write(connfd, pingresp, 2); 
                }
                if ((int) recvline[0] == 224)//DISCONNECT - Control packet type
                    break;
                
                loop++;
            }
        
            /* ========================================================= */
            /* ========================================================= */
            /*                         EP1 FIM                           */
            /* ========================================================= */
            /* ========================================================= */

            /* Após ter feito toda a troca de informação com o cliente,
             * pode finalizar o processo filho */
            printf("[Uma conexão fechada]\n");
            exit(0);
            
        }
        else{
            /**** PROCESSO PAI ****/
            /* Se for o pai, a única coisa a ser feita é fechar o socket
             * connfd (ele é o socket do cliente específico que será tratado
             * pelo processo filho) */
            close(connfd);
            
        }
    }
    exit(0);
}
