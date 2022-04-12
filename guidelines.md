# GUIDELINES FOR IMPLEMETING A PROTCOL

## Read RFC

Figure out:

- Default ports
- Sequences of READ and WRITE Operations
- Who closes the connection

## Define how many sockets will be used

Some protocolos may use more than one socket

## Define how many and which regions of memory are needed

It may be needed to use both main memory and disk space.

## Define data structures for each socket

It is recommended to use standard libraries for using sockets.

## Create sockets and configure them to match step 1

Configure which protocols will be used on Transport Layer and Network Layer.

     TCP -> SOCK_STREAM
Internet -> AF_INET
    IPv4 -> 0

## Set the addresses to be used by each socket.

Such as IPv4 addresses.

## Establish the connection

Link socket to address struct
