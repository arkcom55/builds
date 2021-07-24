#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>

int main()
{

int sock;
    struct sockaddr sock_name = {AF_UNIX, "Fred"};
    socklen_t len=sizeof(struct sockaddr)+5;


    if( (sock=socket(AF_UNIX,SOCK_STREAM,0)) ==-1)
    {
        printf("error creating socket");
        return -1;
    }

    if( bind(sock,&sock_name,len) != 0 )
    {

        printf("socket bind error");
        return -1;
    }

    close(sock);


return 0;
}