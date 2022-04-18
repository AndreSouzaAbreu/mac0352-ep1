#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* local headers */
#include "utils.h"

/* macros */
#define forever for (;;)

/* constants */
#define LISTENQ 1
#define MAXDATASIZE 100
#define MAXLINE 4096

int main(int argc, char **argv) {
  char *appdir = mkdir_app();

  /* char *topic_dir = mkdir_topic(dir, topic); */
  /* printf("%s\n%s\n", dir, topic_dir); */
  /* rmdir(dir); */

  /* Os sockets. Um que será o socket que vai escutar pelas conexões
   * e o outro que vai ser o socket específico de cada conexão */
  int listen_fd, conff_fd;

  /* Informações sobre o socket (endereço e porta) ficam nesta struct */
  struct sockaddr_in servaddr;

  /* Retorno da função fork para saber quem é o processo filho e
   * quem é o processo pai */
  pid_t child_pid;

  /* Armazena linhas recebidas do cliente */
  char recvline[MAXLINE + 1];

  /* Armazena o tamanho da string lida do cliente */
  ssize_t n;

  /** Nome do arquivo temporário que vai ser criado.
   ** TODO: isso é bem arriscado em termos de segurança. O ideal é
   ** que os nomes dos arquivos sejam criados com a função mkstemp e
   ** essas strings sejam templates para o mkstemp. **/

  /** Variável que vai contar quantos clientes estão conectados.
   ** Necessário para saber se é o primeiro cliente ou não. **/
  int client = 0;

  if (argc != 2) {
    fprintf(stderr, "Uso: %s <Porta>\n", argv[0]);
    fprintf(stderr, "Vai rodar um servidor de echo na porta <Porta> TCP\n");
    exit(1);
  }

  /** Criando o pipe onde vou guardar as mensagens do primeiro
   ** cliente. Esse pipe vai ser lido pelos clientes seguintes. **/

  /* for (i = 0; i < 2; i++) { */
  /*   if (mkfifo((const char *)meu_pipe[i], 0644) == -1) { */
  /*     perror("mkfifo :(\n"); */
  /*   } */
  /* } */

  /* Criação de um socket. É como se fosse um descritor de arquivo.
   * É possível fazer operações como read, write e close. Neste caso o
   * socket criado é um socket IPv4 (por causa do AF_INET), que vai
   * usar TCP (por causa do SOCK_STREAM), já que o MQTT funciona sobre
   * TCP, e será usado para uma aplicação convencional sobre a Internet
   * (por causa do número 0) */
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1) {
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
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(atoi(argv[1]));
  if (bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr)) == -1) {
    perror("bind :(\n");
    exit(3);
  }

  /* Como este código é o código de um servidor, o socket será um
   * socket passivo. Para isto é necessário chamar a função listen
   * que define que este é um socket de servidor que ficará esperando
   * por conexões nos endereços definidos na função bind. */
  if (listen(listen_fd, LISTENQ) == -1) {
    perror("listen :(\n");
    exit(4);
  }

  printf("[Servidor no ar. Aguardando conexões na porta %s]\n", argv[1]);
  printf("[Para finalizar, pressione CTRL+c ou rode um kill ou killall]\n");

  /* O servidor no final das contas é um loop infinito de espera por
   * conexões e processamento de cada uma individualmente */
  forever {

    /* O socket inicial que foi criado é o socket que vai aguardar
     * pela conexão na porta especificada. Mas pode ser que existam
     * diversos clientes conectando no servidor. Por isso deve-se
     * utilizar a função accept. Esta função vai retirar uma conexão
     * da fila de conexões que foram aceitas no socket listenfd e
     * vai criar um socket específico para esta conexão. O descritor
     * deste novo socket é o retorno da função accept. */
    if ((conff_fd = accept(listen_fd, (struct sockaddr *)NULL, NULL)) == -1) {
      perror("morri foi aqui no accept :(\n");
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
    child_pid = fork();
    client += 1;

    /**** PROCESSO PAI ****/
    /* Se for o pai, a única coisa a ser feita é fechar o socket
     * conff_fd (ele é o socket do cliente específico que será tratado
     * pelo processo filho) */
    if (child_pid != 0) {
      close(conff_fd);
      continue;
    }

    /**** PROCESSO FILHO ****/
    printf("[Uma conexão aberta]\n");

    /* Já que está no processo filho, não precisa mais do socket
     * listenfd. Só o processo pai precisa deste socket. */
    close(listen_fd);

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

    /* TODO: É esta parte do código que terá que ser modificada
     * para que este servidor consiga interpretar comandos MQTT  */

    /** Se for o primeiro cliente, continua funcionando assim
     ** como estava antes, com a diferença de que agora, tudo que for
     ** recebido vai ser colocado no pipe também (o novo write
     ** no fim do while). Note que estou considerando que
     ** terão 2 clientes conectados. O primeiro, que vai ser o cliente de echo
     ** de fato, e o outro que só vai receber as mensagens do primeiro.
     ** Por isso que foi adicionado um open, um write e um close abaixo.
     ** Obs.: seria necessário um tratamento para o caso do
     ** primeiro cliente sair. Isso está faltando aqui mas não é necessário
     ** para o próposito desse exemplo. Além disso, precisa
     ** revisar se esse unlink está no lugar certo. A depender
     ** do SO, pode não ser ok fazer o unlink logo depois do open. **/

    read(conff_fd, recvline, MAXLINE);
    char m = recvline[0];
    n = read(conff_fd, recvline, MAXLINE);
    recvline[n] = 0;
    char topic[100];
    strcpy(topic, recvline);

    if (m == 'w') {

      printf("writing on topic %s\n", topic);

      const int maxclients = 1001;
      int pipe_fds[maxclients];
      int clients = -1;

      DIR *dir;
      struct dirent *file;
      char *dirname;
      dirname = mkdir_topic(appdir, topic);
      dir = opendir(dirname);
      printf("writing on dir %s\n", dirname);
      if (dir == NULL) {
        continue;
      }
      while ((file = readdir(dir)) != NULL) {
        char pipe_name[255];
        char *filename = file->d_name;
        if (strcmp(filename, ".") == 0)
          continue;
        if (strcmp(filename, "..") == 0)
          continue;
        sprintf(pipe_name, "%s/%s", dirname, filename);
        printf("added %s\n", pipe_name);
        int pipe_fd = open(pipe_name, O_WRONLY);
        clients++;
        pipe_fds[clients] = pipe_fd;
      }
      closedir(dir);

      while ((n = read(conff_fd, recvline, MAXLINE)) > 0) {
        recvline[n] = 0;
        int len = strlen(recvline);
        for (int i = 0; i <= clients; i += 1) {
          write(pipe_fds[i], recvline, len);
        }
      }

      /* close all pipes */
      for (int i = 0; i <= clients; i += 1) {
        close(pipe_fds[i]);
      }

    } else {

      forever {
        char *pipe_name = mkpipe_topic(appdir, topic, client);
        if (mkfifo((const char *)pipe_name, 0777) == -1) {
          perror("mkfifo :(\n");
        }
        int pipe_fd = open(pipe_name, O_RDONLY);
        while ((n = read(pipe_fd, recvline, MAXLINE)) > 0) {
          recvline[n] = 0;
          write(conff_fd, recvline, strlen(recvline));
        }
        close(pipe_fd);
        unlink((char *)pipe_name);
      }
    }

    /* ========================================================= */
    /* ========================================================= */
    /*                         EP1 FIM                           */
    /* ========================================================= */
    /* ========================================================= */

    /* Após ter feito toda a troca de informação com o cliente,
     * pode finalizar o processo filho */
    printf("[Uma conexão fechada]\n");
    break;
  }

  exit(0);
}
