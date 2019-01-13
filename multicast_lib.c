#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h> 
#include <stdlib.h>

int send_multicast(char *broadcast_addr, int *port_addr, char *message) 
{
   int sock = -1;
   //char message[] = "retyergtr";
   struct sockaddr_in addr;
   int addrlen, cnt, mes_size=strlen(message)*sizeof(char)+2, optval = 1;
   struct ip_mreq mreq;
   //char message_raw[sizeof(message)+2];
   char message_raw[mes_size]; //Исправить говнокод, выдилить память нормально
   /* set up socket */
   if(sock <= 0){
    sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
 	 return -1;
    }
   }
   setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof optval);
   setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &optval, sizeof optval);
   
   bzero((char *)&addr, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   //addr.sin_port = htons(EXAMPLE_PORT);
   addr.sin_port = htons(port_addr);
   addrlen = sizeof(addr);
   /* send */
   addr.sin_addr.s_addr = inet_addr(broadcast_addr);
   //addr.sin_addr.s_addr = inet_addr(EXAMPLE_GROUP);
   //printf("sock:%d\n", sock);
  
   sprintf(message_raw, "%c%s%c",25,message,01);
   //printf("size %d, sending: %s\n", mes_size, message_raw);
   cnt = sendto(sock, message_raw, mes_size, 0, (struct sockaddr *) &addr, addrlen);
   if (cnt < 0) {
     return -2;
   }
   close(sock);
   return 0;
}
/*
int recive_multicast(uint32_t broadcast_addr, uint16_t port_addr, char *message, int *m_len)
{
   struct sockaddr_in addr;
   int addrlen, sock, cnt;
   struct ip_mreq mreq;
   char message[50];

   // set up socket
   sock = socket(AF_INET, SOCK_DGRAM, 0);
   if (sock < 0) {
	 perror("socket");
	 exit(1);
   }
   bzero((char *)&addr, sizeof(addr));
   addr.sin_family = AF_INET;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   addr.sin_port = htons(EXAMPLE_PORT);
   addrlen = sizeof(addr);


	  // receive 
	  if (bind(sock, (struct sockaddr *) &addr, sizeof(addr)) < 0) {		
		 perror("bind");
	 exit(1);
	  }	   
	  mreq.imr_multiaddr.s_addr = inet_addr(EXAMPLE_GROUP);			
	  mreq.imr_interface.s_addr = htonl(INADDR_ANY);		 
	  if (setsockopt(sock, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			 &mreq, sizeof(mreq)) < 0) {
	 perror("setsockopt mreq");
	 exit(1);
	  }			
	  while (1) {
	 cnt = recvfrom(sock, message, sizeof(message), 0, 
			(struct sockaddr *) &addr, &addrlen);
	 if (cnt < 0) {
		perror("recvfrom");
		exit(1);
	 } else if (cnt == 0) {
		break;
	 }
	 printf("%s: message = \"%s\"\n", inet_ntoa(addr.sin_addr), message);
	}
}
	*/