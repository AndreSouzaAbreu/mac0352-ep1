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
#include "mosquitto.h"
#include "utils.h"

/* macros */
#define loop for (;;)
#define MAX(a,b) (a > b ? a : b)

/* constants */
#define LISTENQ 1
#define BUFFER_SIZE 200

void mqtt_disconnect(int connection_fd) {
  write(connection_fd, MQTT_PACKET_DISCONNET, 2);
  close(connection_fd);
  exit(1);
}

void read_or_abort_mqtt_connection(int fd, void* buf, size_t nbytes)
{
  if (read(fd, buf, nbytes) != ((ssize_t)nbytes))
  {
    perror("error reading buffer\n");
    mqtt_disconnect(fd);
  }
}

int main(int argc, char **argv)
{

  /* Os sockets. Um que será o socket que vai escutar pelas conexões
   * e o outro que vai ser o socket específico de cada conexão */
  int listen_fd, connection_fd;

  /* Informações sobre o socket (endereço e porta) ficam nesta struct */
  struct sockaddr_in server;

  /* Retorno da função fork para saber quem é o processo filho e
   * quem é o processo pai */
  pid_t child_pid;

  /* Armazena o tamanho da string lida do cliente */
  ssize_t n;

  unsigned char buffer[BUFFER_SIZE + 1];
  unsigned char mqtt_control_packet_type;
  unsigned char mqtt_control_packet_type_flag;
  unsigned char mqtt_remaining_length;
  unsigned char mqtt_packet_identifier[2];
  unsigned char mqtt_topic[256];
  unsigned char mqtt_topic_length;
  unsigned char mqtt_message[1024];
  unsigned char mqtt_message_length;
  unsigned char mqtt_packet_publish[1500];

  /** Nome do arquivo temporário que vai ser criado.
   ** TODO: isso é bem arriscado em termos de segurança. O ideal é
   ** que os nomes dos arquivos sejam criados com a função mkstemp e
   ** essas strings sejam templates para o mkstemp. **/

  /** Variável que vai contar quantos clientes estão conectados.
   ** Necessário para saber se é o primeiro cliente ou não. **/
  int client = 0;

  /* nome do diretório da instância desta aplicação */
  char *app_dir = mkdir_app();

  if (argc != 2)
  {
    fprintf(stderr, "Uso: %s <Porta>\n", argv[0]);
    fprintf(stderr, "Vai rodar um servidor de echo na porta <Porta> TCP\n");
    exit(1);
  }

  /* Criação de um socket. É como se fosse um descritor de arquivo.
   * É possível fazer operações como read, write e close. Neste caso o
   * socket criado é um socket IPv4 (por causa do AF_INET), que vai
   * usar TCP (por causa do SOCK_STREAM), já que o MQTT funciona sobre
   * TCP, e será usado para uma aplicação convencional sobre a Internet
   * (por causa do número 0) */
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1)
  {
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
  bzero(&server, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(atoi(argv[1]));
  if (bind(listen_fd, (struct sockaddr *)&server,
        sizeof(server)) == -1)
  {
    perror("bind :(\n");
    exit(3);
  }

  /* Como este código é o código de um servidor, o socket será um
   * socket passivo. Para isto é necessário chamar a função listen
   * que define que este é um socket de servidor que ficará esperando
   * por conexões nos endereços definidos na função bind. */
  if (listen(listen_fd, LISTENQ) == -1)
  {
    perror("listen :(\n");
    exit(4);
  }

  printf("[Servidor no ar. Aguardando conexões na porta %s]\n", argv[1]);
  printf("[Para finalizar, pressione CTRL+c ou rode um kill ou killall]\n");

  /* O servidor no final das contas é um loop infinito de espera por
   * conexões e processamento de cada uma individualmente */
  loop
  {
    /* O socket inicial que foi criado é o socket que vai aguardar pela conexão
     * na porta especificada. Mas pode ser que existam diversos clientes
     * conectando no servidor. Por isso deve-se utilizar a função accept. Esta
     * função vai retirar uma conexão da fila de conexões que foram aceitas no
     * socket listenfd e vai criar um socket específico para esta conexão. O
     * descritor deste novo socket é o retorno da função accept. */
    connection_fd = accept(listen_fd, (struct sockaddr *)NULL, NULL);
    if (connection_fd == -1)
    {
      perror("accept :(\n");
      exit(5);
    }

    /* Agora o servidor precisa tratar este cliente de forma separada. Para
     * isto é criado um processo filho usando a função fork. O processo vai ser
     * uma cópia deste. Depois da função fork, os dois processos (pai e filho)
     * estarão no mesmo ponto do código, mas cada um terá um PID diferente.
     * Assim é possível diferenciar o que cada processo terá que fazer. O filho
     * tem que processar a requisição do cliente. O pai tem que voltar no loop
     * para continuar aceitando novas conexões. Se o retorno da função fork for
     * zero, é porque está no processo filho. */
    child_pid = fork();
    client += 1;

    /* processo filho será tratado fora deste loop para evitar código muito
     * nestado */
    if (child_pid == 0)
      break;

    /**** PROCESSO PAI ****/
    /* Se for o processo pai, a única coisa a ser feita é fechar o socket
     * conn_fd (ele é o socket do cliente específico que será tratado pelo
     * processo filho) */
    close(connection_fd);
  }

  /**
   * ╭──────────────────────────────────────────────────────────╮
   * │                       FIXED HEADER                       │
   * ╰──────────────────────────────────────────────────────────╯
   */

  printf("[conexão aberta]\n");

  /* Já que está no processo filho, não precisa mais do socket
   * listenfd. Só o processo pai precisa deste socket. */
  close(listen_fd);



  /* we expect exactly 14 bytes in the first packet */
  read_or_abort_mqtt_connection(connection_fd, buffer, 14);

  /* find out packet type */
  mqtt_control_packet_type = buffer[0] >> 4;      /* extract 4 bits on the left */
  mqtt_control_packet_type_flag = buffer[0] % 16; /* extract 4 bits on the right */

  /* close the connection if this is not a mqtt packet of type connection */
  if (mqtt_control_packet_type != MQTT_PACKET_TYPE_CONNECT)
    mqtt_disconnect(connection_fd);

  /* close connection if protocol name is wrong */
  if (memcmp(&buffer[2], MQTT_PROTOCOL_NAME, 6))
    mqtt_disconnect(connection_fd);

  /* alright, send a connack packet */
  write(connection_fd, MQTT_PACKET_CONNACK, 4);

  /******************************************************************************/
  /* processar o segundo pacote */


  read_or_abort_mqtt_connection(connection_fd, buffer, 2);
  mqtt_control_packet_type = buffer[0] >> 4;
  mqtt_control_packet_type_flag = buffer[0] % 16;
  mqtt_remaining_length = buffer[1];

  read_or_abort_mqtt_connection(connection_fd, buffer, mqtt_remaining_length);

  /******************************************************************************/
  /* subscriber */
  if (mqtt_control_packet_type == MQTT_PACKET_TYPE_SUBSCRIBE)
  {
    memcpy(mqtt_packet_identifier, buffer, 2);
    mqtt_topic_length = buffer[3];
    memcpy(mqtt_topic, &buffer[4], mqtt_topic_length);
    mqtt_topic[mqtt_topic_length] = '\0';
    printf("[sub] topic %s\n", mqtt_topic);

    unsigned char mqtt_packet_suback[] = {
      (MQTT_PACKET_TYPE_SUBACK << 4),
      0x03,
      mqtt_packet_identifier[0],
      mqtt_packet_identifier[1],
      MQTT_GRANTED_QOS,
    };
    write(connection_fd, mqtt_packet_suback, 5);

    loop
    {
      /* create and read from pipe */
      char *pipe_name = mkpipe_topic(app_dir,(char*) mqtt_topic, client);
      printf("[sub] pipe name is %s\n", pipe_name);
      if (mkfifo(pipe_name, 0777) == -1)
      {
        perror("mkfifo :(\n");
      }
      int pipe_fd = open(pipe_name, O_RDONLY);
      n = read(pipe_fd, buffer, BUFFER_SIZE);
      buffer[n] = 0;
      printf("[sub] read %s\n", buffer);
      close(pipe_fd);
      unlink(pipe_name);

      /* send a publish packet to the client */
      mqtt_packet_publish[0] = (MQTT_PACKET_TYPE_PUBLISH << 4);
      mqtt_packet_publish[1] = 2 + n + mqtt_topic_length;
      mqtt_packet_publish[2] = 0;
      mqtt_packet_publish[3] = mqtt_topic_length;
      memcpy(&mqtt_packet_publish[4], mqtt_topic, mqtt_topic_length);
      memcpy(&mqtt_packet_publish[4 + mqtt_topic_length], buffer, n);
      printf("[pub] will send packet... n=%zd l=%u\n",n,mqtt_topic_length);
      write(connection_fd, mqtt_packet_publish,4+mqtt_topic_length+n );
    }

    close(connection_fd);
    exit(0);
  }

  /******************************************************************************/
  /* publisher */
  if (mqtt_control_packet_type == MQTT_PACKET_TYPE_PUBLISH)
  {
    mqtt_topic_length = MAX(buffer[0], buffer[1]);
    memcpy(mqtt_topic, &buffer[2], mqtt_topic_length);
    mqtt_topic[(int) mqtt_topic_length] = '\0';

    mqtt_message_length = mqtt_remaining_length - (2+mqtt_topic_length);
    memcpy(mqtt_message, &buffer[2+(int)mqtt_topic_length], mqtt_message_length);
    printf("[pub] topic: %s\nmessage: %s\n", mqtt_topic, mqtt_message);

    DIR *dir;
    struct dirent *file;
    char *dirname;
    dirname = mkdir_topic(app_dir,(char*) mqtt_topic);
    dir = opendir(dirname);
    if (dir == NULL)
    {
      close(connection_fd);
      exit(0);
    }
    printf("\n[pub] writing...\n");
    while ((file = readdir(dir)) != NULL)
    {
      char pipe_name[255];
      char *filename = file->d_name;
      if (strcmp(filename, ".") == 0)
        continue;
      if (strcmp(filename, "..") == 0)
        continue;
      sprintf(pipe_name, "%s/%s", dirname, filename);
      printf("[pub] found pipe %s\n", pipe_name);
      int pipe_fd = open(pipe_name, O_WRONLY);
      write(pipe_fd, mqtt_message, mqtt_message_length);
      close(pipe_fd);
    }
    closedir(dir);
    close(connection_fd);
    return 0;
  }

  /******************************************************************************/
  /* wrong packet type, close connection */
  write(connection_fd, MQTT_PACKET_DISCONNET, 2);
  close(connection_fd);
  perror("wrong packet type\n");
  return 1;
}
