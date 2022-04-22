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

/* constants */
#define LISTENQ 1
#define BUFFER_SIZE 1024

/* local helpers */
void abort_mqtt_connection(int connection_fd);
void read_or_abort_mqtt_connection(int fd, void* buf, size_t nbytes);

/******************************************************************************/
/* Implementation of a simple MQTT server version 3.1.1 */

int main(int argc, char **argv)
{
  /* let's first declare the variables used throughout the program */

  /* connection_fd is the file descriptor of the socket that will  be  listening
   * for connections, whereas listen_fd is the fd of the socket of a  particular
   * MQTT connection */
  int listen_fd, connection_fd;

  /* socket information (ip address and port) is stored in this struct */
  struct sockaddr_in server;

  /* variable to store the pid returned by fork, to distinguis between parent 
   * and child process*/
  pid_t child_pid;

  /* buffer to read incoming data */
  unsigned char buffer[BUFFER_SIZE];

  /* variable to store how many bytes were read */
  ssize_t offset;

  /* variables specific to MQTT. Their name already tells you what they are */
  unsigned char mqtt_control_packet_type;
  unsigned char mqtt_remaining_length;
  unsigned char mqtt_packet_identifier[2];
  unsigned char mqtt_topic[MQTT_TOPIC_MAXLENGTH];
  unsigned char mqtt_topic_length;
  unsigned char mqtt_message[MQTT_MESSAGE_MAXLENGTH];
  unsigned char mqtt_message_length;
  unsigned char mqtt_packet_publish[MQTT_PACKET_PUBLISH_MAXLENGTH];
  unsigned char mqtt_packet_publish_length;

  /* variable to identify current client */
  int client = 0;

  /* name of the directory where all the application data of this instance will
   * be stored */
  char *app_dir = mkdir_app();

  /* dir where we will store files of active clients. A client  is  active  only
   * if there is a file in this dir whose filename matches the client id */
  char *active_clients_dir = mkdir_active_clients(app_dir);

  /****************************************************************************/

  /* print error messages if user did not provide a port to run on*/
  if (argc != 2)
  {
    fprintf(stderr, "Description: Runs a mosquitto server on specified port\n");
    fprintf(stderr, "Usage: %s <port>\n", argv[0]);
    exit(1);
  }

  /* Criação de um socket. É como se fosse um descritor de arquivo.  É  possível
   * fazer operações como read, write e close. Neste caso o socket criado  é  um
   * socket IPv4 (por causa  do  AF_INET),  que  vai  usar  TCP  (por  causa  do
   * SOCK_STREAM), já que o MQTT funciona sobre  TCP,  e  será  usado  para  uma
   * aplicação convencional sobre a Internet (por causa do número 0) */
  listen_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd == -1)
  {
    perror("[ERROR] unable to create TCP socket\n");
    exit(2);
  }

  /* Agora é necessário informar  os  endereços  associados  a  este  socket.  É
   * necessário informar o endereço / interface e a porta, pois mais  adiante  o
   * socket ficará esperando conexões nesta porta  e  neste(s)  endereços.  Para
   * isso é necessário preencher a struct servaddr. É necessário  colocar  lá  o
   * tipo de socket (No nosso caso AF_INET porque é IPv4), em  qual  endereço  /
   * interface serão esperadas  conexões  (Neste  caso  em  qualquer  uma,  pois
   * INADDR_ANY) e qual a porta. Neste caso será a porta que  foi  passada  como
   * argumento no shell (atoi(argv[1]))
   */
  bzero(&server, sizeof(server));
  server.sin_family = AF_INET;
  server.sin_addr.s_addr = htonl(INADDR_ANY);
  server.sin_port = htons(atoi(argv[1]));
  if (bind(listen_fd, (struct sockaddr *)&server, sizeof(server)) == -1)
  {
    fprintf(stderr, "[ERROR]: could not bind port %s\n", argv[1]);
    exit(3);
  }

  /* Como este código é o código  de  um  servidor,  o  socket  será  um  socket
   * passivo. Para isto é necessário chamar a função listen que define que  este
   * é um socket de servidor que ficará esperando  por  conexões  nos  endereços
   * definidos na função bind. */
  if (listen(listen_fd, LISTENQ) == -1)
  {
    fprintf(stderr, "[ERROR]: could not activate socket\n");
    exit(4);
  }

  printf("[NOTICE] server is running on port %s\n", argv[1]);

  /*
   * ╭─────────────────────────────────────────────────────────────────────────╮
   * │                                                                SERVIDOR │
   * ╰─────────────────────────────────────────────────────────────────────────╯
   */

  /* O servidor entra em loop infinito processando uma por conexão por vez */
  loop
  {

    /* O socket inicial que foi criado é o socket que vai aguardar pela  conexão
     * na porta  especificada.  Mas  pode  ser  que  existam  diversos  clientes
     * conectando no servidor. Por isso deve-se utilizar a função  accept.  Esta
     * função vai retirar uma conexão da fila de conexões que foram  aceitas  no
     * socket listenfd e vai criar um socket específico  para  esta  conexão.  O
     * descritor deste novo socket é o retorno da função accept. */
    connection_fd = accept(listen_fd, (struct sockaddr *)NULL, NULL);
    if (connection_fd == -1)
    {
      perror("[ERROR] Could not open socket to for incoming connection\n");
      exit(5);
    }

    /* Agora o servidor precisa tratar este  cliente  de  forma  separada.  Para
     * isto é criado um processo filho usando a função fork. O processo vai  ser
     * uma cópia deste. Depois da função fork, os dois processos (pai  e  filho)
     * estarão no mesmo ponto do código, mas cada  um  terá  um  PID  diferente.
     * Assim é possível diferenciar o que cada processo terá que fazer. O  filho
     * tem que processar a requisição do cliente. O pai tem que voltar  no  loop
     * para continuar aceitando novas conexões. Se o retorno da função fork  for
     * zero, é porque está no processo filho. */
    child_pid = fork();
    client += 1;

    /* processo filho será tratado fora deste loop para evitar código nestado */
    if (child_pid == 0)
      break;

    /* Se for o processo pai, a única coisa a ser feita é fechar o socket
     * conn_fd (ele é o socket do cliente específico que será tratado pelo
     * processo filho logo abaixo) */
    close(connection_fd);
  }

  /*
   * ╭─────────────────────────────────────────────────────────────────────────╮
   * │                                                                 CLIENTE │
   * ╰─────────────────────────────────────────────────────────────────────────╯
   */

  /* imprime uma mensagem informativa */
  printf("[NOTICE] new connection (client id = %d)\n", client);

  /* Já que está no processo filho, não precisa mais do socket listenfd.
   * Só o processo pai precisa deste socket. */
  close(listen_fd);

  /* De agora em diante iremos processar os pacotes enviados pelos clientes.
   * Os diagramas abaixo ilustram a estrutura dos pacotes */

  /* Structure of an MQTT Control Packet
     ┌─────────────────────────────────────────────────────────┐
     │   Fixed header, present in all MQTT Control Packets     │
     ├─────────────────────────────────────────────────────────┤
     │   Variable header, present in some MQTT Control Packets │
     ├─────────────────────────────────────────────────────────┤
     │   Payload, present in some MQTT Control Packets         │
     └─────────────────────────────────────────────────────────┘

     FIXED HEADER
     ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
     │ Bit    │   7   │   6   │   5   │   4   │   3   │   2   │   1   │   0   │
     ├────────┼───────┴───────┴───────┴───────┼───────┴───────┴───────┴───────┤
     │ byte 1 │ MQTT Control Packet type      │ Flag specific for each type   │ 
     ├────────┼───────────────────────────────┴───────────────────────────────┤
     │ byte 2 │                      Remaining Length                         │
     └────────┴───────────────────────────────────────────────────────────────┘
  */


  /***************************************************************************/
  /* CONNECT PACKET */

  /* we expect exactly 14 bytes in the first packet (must be acontrol packet)
   *
   * fixed header: 2 bytes 
   * variable header: 10 bytes
   * payload: 2 bytes
   *
   * abort connection if there is less than 14 bytes to read */
  read_or_abort_mqtt_connection(connection_fd, buffer, 14);

  /* packet type are the 4 MSB of the first byte*/
  mqtt_control_packet_type = buffer[0] >> 4;

  /* close the connection if this is not a mqtt packet of type connection */
  if (mqtt_control_packet_type != MQTT_PACKET_TYPE_CONNECT)
  {
    fprintf(stderr,
        "[ERROR]: client %d sent wrong packet type during CONNECT", client);
    abort_mqtt_connection(connection_fd);
  }

  /* Protocol Name
     ┌──────┬────────────────┐
     │ byte │ description    │
     ├──────┼────────────────┤
     │  1   │ Length MSB (0) │
     │  2   │ Length LSB (4) │
     │  3   │ 'M'            │
     │  4   │ 'Q'            │
     │  5   │ 'T'            │
     │  6   │ 'T'            │
     └──────┴────────────────┘
  */

  /* close connection if protocol name is wrong */
  if (memcmp(&buffer[2], MQTT_PROTOCOL_NAME, 6))
  {
    fprintf(stderr,
        "[ERROR]: client %d sent wrong protocol name during CONNECT", client);
    abort_mqtt_connection(connection_fd);
  }

  /***************************************************************************/
  /* CONNACK PACKET
     ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
     │ Bit    │   7   │   6   │   5   │   4   │   3   │   2   │   1   │   0   │
     ├────────┼───────┴───────┴───────┴───────┼───────┴───────┴───────┴───────┤
     │ byte 1 │ MQTT Control Packet type (2)  │            RESERVED (0)       │ 
     ├────────┼───────────────────────────────┴───────────────────────────────┤
     │ byte 2 │                      Remaining Length (2)                     │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 3 │ Connect Acknowledgement Flags (0)                             │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 4 │ Connect Return Code (0)                                       │
     └────────┴───────────────────────────────────────────────────────────────┘  
  */

  /* connection was sucessful */
  /* so send a CONNACK packet */
  write(connection_fd, MQTT_PACKET_CONNACK, 4);

  /***************************************************************************/
  /* SECOND PACKET */

  /* we don't know if the client is a subscriber or publisher
   * we have to find that out checking the packet type */
  read_or_abort_mqtt_connection(connection_fd, buffer, 2);
  mqtt_control_packet_type = buffer[0] >> 4;
  mqtt_remaining_length = buffer[1];

  /* read the remaining data (variable header + payload) into the buffer */
  read_or_abort_mqtt_connection(connection_fd, buffer, mqtt_remaining_length);

  /***************************************************************************/
  /* SUBSCRIBER */
  if (mqtt_control_packet_type == MQTT_PACKET_TYPE_SUBSCRIBE)
  {

    /*************************************************************************/
    /* Subscribe Packet
     ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
     │ Bit    │   7   │   6   │   5   │   4   │   3   │   2   │   1   │   0   │
     ├────────┼───────┴───────┴───────┴───────┼───────┴───────┴───────┴───────┤
     │ byte 1 │ MQTT Control Packet type (8)  │             Flags             │ 
     ├────────┼───────────────────────────────┴───────────────────────────────┤
     │ byte 2 │                      Remaining Length                         │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 3 │ Package Identifier MSB                                        │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 4 │ Package Identifier LSB                                        │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 5 │ Topic Length LSB                                              │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 6 │ Topic Length MSB                                              │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 7 │ Topic                                                         │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │  ....  │ Topic                                                         │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte n │ Topic                                                         │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte m │ Requested QoS                                                 │
     └────────┴───────────────────────────────────────────────────────────────┘  
    */

    /* we have discarded the first two bytes (Fixed Header), so we are dealing
     * with the Variable Header. Thus, we have an offset of -2 regarding the
     * table above */

    /* topic identifier is the first two bytes */
    memcpy(mqtt_packet_identifier, buffer, 2);

    /* topic length is the fourth byte */
    mqtt_topic_length = buffer[3];

    /* topic name starts at fifth byte and ends after topic_length bytes */
    memcpy(mqtt_topic, &buffer[4], mqtt_topic_length);
    mqtt_topic[mqtt_topic_length] = '\0';

    /* print some informative message */
    printf("[NOTICE] client %d is listening on topic %s\n", client, mqtt_topic);

    /**************************************************************************/
    /* Suback Packet
     ┌────────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
     │ Bit    │   7   │   6   │   5   │   4   │   3   │   2   │   1   │   0   │
     ├────────┼───────┴───────┴───────┴───────┼───────┴───────┴───────┴───────┤
     │ byte 1 │ MQTT Control Packet type (9)  │            FLAGS (0)          │ 
     ├────────┼───────────────────────────────┴───────────────────────────────┤
     │ byte 2 │                      Remaining Length (3)                     │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 3 │ Package Identifier MSB                                        │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 4 │ Package Identifier LSB                                        │
     ├────────┼───────────────────────────────────────────────────────────────┤
     │ byte 5 │ Granted QoS (0)                                               │
     └────────┴───────────────────────────────────────────────────────────────┘  
    */  
    /* now let's send back a response */
    unsigned char mqtt_packet_suback[5];

    /* first byte is packet type */
    mqtt_packet_suback[0] = MQTT_PACKET_TYPE_SUBACK << 4;

    /* second byte is remaining length */
    mqtt_remaining_length = 3;
    mqtt_packet_suback[1] = mqtt_remaining_length;

    /* third and fourth bytes are packet identifier */
    mqtt_packet_suback[2] = mqtt_packet_identifier[0];
    mqtt_packet_suback[3] = mqtt_packet_identifier[1];

    /* last byte is granted QOS */
    mqtt_packet_suback[4] = MQTT_GRANTED_QOS;

    /* send suback */
    write(connection_fd, mqtt_packet_suback, 5);

    /**************************************************************************/

    /* let's create a file to indicate that this client is active, meaning it
     * can receive messages from publishers*/
    char client_filename[300];
    sprintf(client_filename, "%s/%d", active_clients_dir, client);

    /* just open a file and immediatly close it, which creates an empty file */
    FILE*  client_file = fopen(client_filename, "w");
    if (client_file == NULL)
    {
      fprintf(stderr,
          "[ERROR]: could not create file for client %d\n", client);
      abort_mqtt_connection(connection_fd);
    }
    fclose(client_file);

    /* now let's fork  the  process.  The  parent  process  will  be  constantly
     * listening for incoming packets  from  the  subscriber,  while  the  child
     *  process  will  be  constantly  listening  for  incoming  messages   from
     * publishers */

    child_pid = fork();

    if (child_pid)
    {
      loop
      {
        /* wait for incoming requests */
        read(connection_fd, buffer, 2);
        mqtt_control_packet_type = buffer[0] >> 4;
        mqtt_remaining_length = buffer[1];

        /* we got a PING request, so send a PING response */
        if (mqtt_control_packet_type == MQTT_PACKET_TYPE_PINGREQ &&
            mqtt_remaining_length == 0)
        {
          write(connection_fd, MQTT_PACKET_PINGRESP, 2);
          continue;
        }

        /* regardless of what happens next, this  client  will  be  disconnected
         * because we only accept PING or DISCONNECT  requests.  So  remove  its
         * file from the directory of active clients */
        unlink(client_filename);

        /* clean disconnect request :) */
        if (mqtt_control_packet_type == MQTT_PACKET_TYPE_DISCONNECT &&
            mqtt_remaining_length == 0)
        {
          printf("[NOTICE] client %d disconnected\n", client);
          close(connection_fd);
          exit(0);
        }

        /* bad, unexpected message! Abort! */
        fprintf(stderr, "[ERROR] unexpected packet from client %d\n", client);
        abort_mqtt_connection(connection_fd);
      }
    }

    /* wait for new messages eternally */
    loop
    {
      /* we will use a pipe to read incoming messages */
      char* pipe_name = mkpipe_topic(app_dir,(char*) mqtt_topic, client);

      /* create pipe */
      if (mkfifo(pipe_name, 0777) == -1)
      {
        fprintf(stderr,
            "[ERROR] Could not create FIFO for client %d\n", client);
        abort_mqtt_connection(connection_fd);
        break;
      }

      /* open pipe and read a message from it */
      int pipe_fd = open(pipe_name, O_RDONLY);
      mqtt_message_length = read(pipe_fd, mqtt_message, MQTT_MESSAGE_MAXLENGTH);
      mqtt_message[mqtt_message_length] = 0;

      /* close and remove pipe */
      close(pipe_fd);
      unlink(pipe_name);

      /* before we write any message, we need to check if the client  is  active
       * we do so by checking if there exists  its  file  in  the  directory  of
       * active clients */
      int client_file_fd = open(client_filename, O_RDONLY);

      /* file does not exist, meaning client is no longer active. Just exit */ 
      if (client_file_fd == -1)
      {
        exit(0);
      }

      /* client is active, close the file fd and move on */
      close(client_file_fd);

      /************************************************************************/
      /* let's send a publish packet now */

      /* Publish Packet
       ┌──────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
       │ byte │   7   │   6   │   5   │   4   │   3   │   2   │   1   │   0   │
       ├──────┼───────┴───────┴───────┴───────┼───────┴───────┴───────┴───────┤
       │   1  │ MQTT Control Packet type (3)  │            FLAGS (0)          │ 
       ├──────┼───────────────────────────────┴───────────────────────────────┤
       │   2  │                      Remaining Length                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │   3  │ Topic Legnth MSB                                              │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │   4  │ Topic Length LSB                                              │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │   7  │ Topic                                                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │  ... │ Topic                                                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │  n   │ Topic                                                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │ n+1  │ Message                                                       │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │  ... │ Message                                                       │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │ n+m  │ Message                                                       │
       └──────┴───────────────────────────────────────────────────────────────┘  
      */   

      /* first byte is packet type */
      mqtt_packet_publish[0] = MQTT_PACKET_TYPE_PUBLISH << 4;

      /* second byte is remaining length */
      mqtt_remaining_length = 2 + mqtt_message_length + mqtt_topic_length;
      mqtt_packet_publish[1] = mqtt_remaining_length;

      /* third and fourth bytes are the topic length */
      mqtt_packet_publish[2] = 0;
      mqtt_packet_publish[3] = mqtt_topic_length;

      /* then we append the topic name */
      memcpy(&mqtt_packet_publish[4], mqtt_topic, mqtt_topic_length);

      /* then we append the message */
      offset = 4 + mqtt_topic_length;
      memcpy(&mqtt_packet_publish[offset], mqtt_message, mqtt_message_length);

      /* write */
      mqtt_packet_publish_length = 2 + mqtt_remaining_length;
      write(connection_fd, mqtt_packet_publish, mqtt_packet_publish_length);
    }
  }

  /****************************************************************************/
  /* publisher */
  if (mqtt_control_packet_type == MQTT_PACKET_TYPE_PUBLISH)
  {
    /* let's process the packet, which is illustrated below */
    /* Publish Packet
       ┌──────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
       │ byte │   7   │   6   │   5   │   4   │   3   │   2   │   1   │   0   │
       ├──────┼───────┴───────┴───────┴───────┼───────┴───────┴───────┴───────┤
       │   1  │ MQTT Control Packet type (3)  │            FLAGS (0)          │ 
       ├──────┼───────────────────────────────┴───────────────────────────────┤
       │   2  │                      Remaining Length                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │   3  │ Topic Legnth MSB                                              │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │   4  │ Topic Length LSB                                              │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │   7  │ Topic                                                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │  ... │ Topic                                                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │  n   │ Topic                                                         │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │ n+1  │ Message                                                       │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │  ... │ Message                                                       │
       ├──────┼───────────────────────────────────────────────────────────────┤
       │ n+m  │ Message                                                       │
       └──────┴───────────────────────────────────────────────────────────────┘  
    */   

    /* topic length is second byte */
    mqtt_topic_length = buffer[1];

    /* copy topic name */
    memcpy(mqtt_topic, &buffer[2], mqtt_topic_length);
    mqtt_topic[(int) mqtt_topic_length] = '\0';

    /* message length = payload size - first two bytes - topic length */
    offset = 2 + mqtt_topic_length;
    mqtt_message_length = mqtt_remaining_length - offset;

    /* copy message from buffer */
    memcpy(mqtt_message, &buffer[offset], mqtt_message_length);
    mqtt_message[mqtt_message_length] = '\0';

    /* print some informative message */
    printf("[NOTICE] client %d publishing on topic: %s\n", client, mqtt_topic);

    /**************************************************************************/
    /* let's actually publish the message now */

    /* open the dir which has the pipes of clients listening on this topic */
    struct dirent *file;
    char *dirname = mkdir_topic(app_dir,(char*) mqtt_topic);
    DIR  *dir = opendir(dirname);

    /* no dir means no clients... */
    if (dir == NULL)
    {
      close(connection_fd);
      exit(0);
    }

    /* write the message on each client pipe */
    while ((file = readdir(dir)) != NULL)
    {
      char pipe_name[255];
      char *filename = file->d_name;

      /* skip special dirs */
      if (strcmp(filename, ".") == 0)
        continue;
      if (strcmp(filename, "..") == 0)
        continue;

      /* open pipe, write into it, and close it */
      sprintf(pipe_name, "%s/%s", dirname, filename);
      int pipe_fd = open(pipe_name, O_WRONLY);
      write(pipe_fd, mqtt_message, mqtt_message_length);
      close(pipe_fd);
    }

    /* close the directory and the connection */
    closedir(dir);
    close(connection_fd);
    return 0;
  }

  /****************************************************************************/
  /* wrong packet type, abort connection */
  fprintf(stderr,
      "[ERROR]: client %d sent wrong packet type after CONNACK", client);
  abort_mqtt_connection(connection_fd);
}

/******************************************************************************/
/* local helpers */

void abort_mqtt_connection(int connection_fd)
{
  write(connection_fd, MQTT_PACKET_DISCONNET, 2);
  close(connection_fd);
  exit(10);
}

void read_or_abort_mqtt_connection(int fd, void* buf, size_t nbytes)
{
  if (read(fd, buf, nbytes) != ((ssize_t)nbytes))
  {
    fprintf(stderr, "[ERROR]: unable to read from connection into buffer\n");
    abort_mqtt_connection(fd);
  }
}
