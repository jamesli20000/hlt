#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <sys/time.h>
#include <errno.h>




#define RECV_SIZE 8192
#define BUF_SIZE 1024


int verbose = 0;
int g_req_counter = 0;
int g_totalSendReq = 0;
int g_totalErrorRecv = 0;
int g_totalFailUnexp = 0;

int g_totalFailConnect = 0;   
int g_totalConnectTimeout = 0;
int g_totalConnectReset = 0;
int g_totalResponseRecv = 0;

int g_nextLevelResponse = 0;
int g_allResponseCounter = 0;



#define DEBUG(msg,...) if(verbose) fprintf(stderr, msg, ##__VA_ARGS__);
#define ERROR(msg,...) fprintf(stderr, msg, ##__VA_ARGS__);


typedef struct ReqData
{
    time_t start_time;
    time_t end_time;
}sReqData;

typedef enum HTTP_METHOD
{
    HTTP_GET=0,
    HTTP_POST,
}eHTTP_METHOD;

void usage()
{
    printf("http_load_test [option][value] \n"
          " -c concurrent \n"
          " -n total_request\n"
          " -s server \n"
          " -p port n"
          " -u url\n"
          " -v verbose"
          " -t method type 0 GET/1 POST\n"
          " -f external file\n");
}

int create_daemon(){    
    int  fd;  
    int pid;
    switch (fork()) {    
        case -1:        
            printf("fork() failed\n");        
            return -1;    
        case 0:        
            break;    
        default:        
            exit(0);    
    }
    pid = getpid();
    if (setsid() == -1) {
        printf("setsid() failed\n");        
        return -1;    
    }
    umask(0);
    fd = open("/dev/null", O_RDWR);    
    if (fd == -1) {        
        printf("open(\"/dev/null\") failed\n");
        return -1;    
    }
    if (dup2(fd, STDIN_FILENO) == -1) {
        printf("dup2(STDIN) failed\n");
        return -1;    
    }
    if (dup2(fd, STDOUT_FILENO) == -1) {
        printf("dup2(STDOUT) failed\n");
        return -1;    
    }
   
    if (fd > STDERR_FILENO) {    
        if (close(fd) == -1) { 
            printf("close() failed\n");
            return -1;        
        }
    }
    return 0;
}



int make_socket_non_blocking (int sfd)
{
    int flags, s;

    flags = fcntl (sfd, F_GETFL, 0);
    if (flags == -1)
    {
        ERROR ("fcntl");
        return -1;
    }

    flags |= O_NONBLOCK;
    s = fcntl (sfd, F_SETFL, flags);
    if (s == -1)
    {
        ERROR ("fcntl");
        return -1;
    }

    return 0;
}

int init_connection(char *hostname, char *port, struct addrinfo **res)
{
    struct addrinfo hints;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    return getaddrinfo(hostname, port, &hints, res);
}

int make_socket(struct addrinfo *res)
{
    int sockfd;

    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if( sockfd < 0 ) return -1;
    
    if( 0 != make_socket_non_blocking(sockfd)) 
    {
        ERROR("make_connection failed\n");
        return -1;
    }

    return sockfd;
}

char *
build_request(char *hostname, char *request_path, eHTTP_METHOD method, char*content_path)
{
    char *request_buffer = malloc(BUF_SIZE);
    if( HTTP_GET == method )
    {
        sprintf(request_buffer, "GET %s HTTP/1.0\r\n"
                                "Host: %s\r\n"
                                "Connection: close\r\n\r\n", 
                                request_path, hostname);
    }else if( HTTP_POST == method )
    {
        FILE*f = fopen(content_path, "rb");
        if(!f)
        {
            ERROR("error when open file:%s\n", content_path);
            exit(0);
        }
        fseek(f, 0, SEEK_END);
        int len = ftell(f);
        fseek(f, 0, SEEK_SET);
        sprintf(request_buffer, "POST %s HTTP/1.0\r\n"
                                "Content-Type: text/plain\r\n"
                                "Content-Length: %d\r\n\r\n",
                                request_path, len);
        char*buffer = malloc(len+strlen(request_buffer)+1);
        memcpy(buffer, request_buffer, strlen(request_buffer));
        fread(buffer+strlen(request_buffer), len, 1, f);
        buffer[len+strlen(request_buffer)] = 0;
        
        DEBUG("buffer content-len:%d, buffer len:%d\n"
              "content:%s\n", len, strlen(request_buffer), buffer);
        free(request_buffer);
        fclose(f);
        return buffer;
    }

    return request_buffer;
}

int
send_request(int sockfd, char *request, int bytes_to_send)
{
    
    size_t bytes_sent       = 0;
    size_t total_bytes_sent = 0;

    while (1) {
        bytes_sent = send(sockfd, request+total_bytes_sent,bytes_to_send-total_bytes_sent, 0);
        if( bytes_sent < 0 )
        {
            ERROR("send error\n");
            if( errno == EAGAIN )
            {
                ERROR("errno == EAGAIN\n");
            }
        }
        total_bytes_sent += bytes_sent;

        DEBUG( "Bytes sent: %ld\n", bytes_sent);

        if (total_bytes_sent >= bytes_to_send) {
            break;
        }
    }
    
    if( bytes_to_send != total_bytes_sent ) ERROR("send_request error\n");

    return total_bytes_sent;
}

int
fetch_response(int sockfd)
{
    int bytes_received = 0;
    int total_rec = 0;
    int status = 0;
    char data[RECV_SIZE];

    while (1) {
        bytes_received = recv(sockfd, data+total_rec, RECV_SIZE, 0);
        total_rec += bytes_received;
        if (bytes_received <= 0)
        {
            break;
        }   
    }
    if(total_rec >= 0 ) 
    {
        DEBUG("fetch_response:%s", data);
        return total_rec; 
    }       
    return -1;
}
 
int timeval_diff(struct timeval*a, struct timeval*b)
{
    int s = a->tv_sec - b->tv_sec;
    int u = a->tv_usec - b->tv_usec;
    if( u < 0 )
    {
        s--;
        u += 1000000;
    }
    return s*1000 + u/1000;
}

void updateRequestCounter()
{
    g_req_counter--;
    g_allResponseCounter++;
}

int main(int argc, char*argv[])
{

    int g_total_n = 0;
    int g_finTime[10] = {0};
    int g_finTime_counter = 0;
    int concur = 10,total_n = 100;
    char*content_path = NULL;
    eHTTP_METHOD method = HTTP_GET;
    
    char *request = NULL; 
    struct timeval starttime,nowtime;
    gettimeofday( &starttime, NULL );
   
    char*port = "80";
    char*server = "localhost";
    char *url = "";
    struct addrinfo *res = NULL;
    
    struct epoll_event event;
    struct epoll_event *events;
    int efd;
    
    int status = 0;
    int i = 0;
   
    int c = -1;
    while ((c = getopt(argc, argv, "u:c:n:s:p:vt:f:")) != -1) {
        switch (c) {
            case 'c':
                concur = atoi(optarg);
                printf("c:%d\n", concur);
                break;
            case 'n':
                total_n = atoi(optarg);
                printf("n:%d\n", total_n);
                g_total_n = total_n;
                break;
            case 's':
                server = optarg;
                printf("server:%s\n", server);
                break;
            case 'p':
                port = optarg;
                printf("port:%s\n", port);
                break;
            case 'u':
                url = optarg;
                printf("url:%s\n", url);
                break;
            case 'v':
                verbose = 1;
                break;
            case 't':
                method = atoi(optarg);
                break;
            case 'f':
                content_path = optarg;
                break;
            default:
                usage();
                exit(0);
        }
    }
    if ( !url )
    {
        ERROR("no url\n");
        usage();
        exit(0);
    }
    
    if( HTTP_POST == method && !content_path )
    {
        ERROR("must set content path if chose POST\n");
        usage();
        exit(0);        
    }
           
    if( 0 != create_daemon() )
    {
        ERROR("create daemon failed\n");
        exit(0);
    }
    g_nextLevelResponse = g_total_n / 10;
    request = build_request(server, url, method, content_path);
    
    DEBUG( "create daemon OK, sizeof event:%d\n", sizeof(event));
    efd = epoll_create1 (0);
    if ( -1 == efd )
    {
        ERROR ("epoll_create failed\n");
        exit(0);
    }
    
    events = calloc (concur, sizeof(event));
    if( 0 != init_connection(server, port, &res) )
    {
        ERROR( "resolve server addr failed\n");
        exit(0);
    }
    DEBUG( "init_connection OK\n");
    ERROR("\n");
    while(1)
    {
        for(; g_req_counter < concur; g_req_counter++,total_n-- )
        {
            if ( total_n <= 0) {
                DEBUG( "task alloc complete\n");
                break;
            }

            int sockfd = make_socket(res);
            if( -1 == sockfd )
            {
                ERROR("fail to connect server\n");
                exit(0);
            }
            
            event.data.fd = sockfd;
            event.events =  EPOLLET | EPOLLOUT;

            if ( -1 == epoll_ctl (efd, EPOLL_CTL_ADD, sockfd, &event) )
            {
                ERROR( "epoll_ctl failed\n");
                exit(0);
            }
            DEBUG( "add new task, total_n:%d\n", total_n);
            
            status = connect(sockfd, res->ai_addr, res->ai_addrlen);
            if( status < 0 && EINPROGRESS != errno )
            {
                ERROR("fail to connect server\n");
                g_totalFailConnect++;
                epoll_ctl(efd, EPOLL_CTL_DEL,sockfd, &event);
                close (sockfd);
            }else
            {
                g_totalSendReq++;
            }
        }

        int n = epoll_wait (efd, events, concur, -1);
        if( n < 0 )
        {
            ERROR("epoll_wait failed\n");
            break;
        }
        DEBUG( "have %d events\n", n);
        i = 0;
        for (; i < n; i++)
        {
            if ((events[i].events & EPOLLERR) )
            {
                DEBUG ( "one epoll error\n");

                updateRequestCounter();
                g_totalErrorRecv++;
                
                int       error = 0;
                socklen_t errlen = sizeof(error);
                if (getsockopt(events[i].data.fd, SOL_SOCKET, SO_ERROR, (void *)&error, &errlen) == 0)
                {
                    DEBUG("error = %s\n", strerror(error));
                    if( ETIMEDOUT == error ||
                        ECONNRESET == error )
                    {
                        g_totalFailConnect++;

                        if(ETIMEDOUT == error) g_totalConnectTimeout++;
                        else if(ECONNRESET == error) g_totalConnectReset++;       
                    }
                }
                epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                close (events[i].data.fd);
            }else if((events[i].events & EPOLLHUP) )
            {
                DEBUG ( "peer disconnect\n");
                epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                close (events[i].data.fd);
                updateRequestCounter();

            }else if( events[i].events & EPOLLOUT)
            {
                int len = 0;
                int err = 0;
                int ret = getsockopt (events[i].data.fd, 
                                    SOL_SOCKET, SO_ERROR, 
                                    &err, &len);
                if( !ret )
                {
                    if( 0 == err )
                    {
                        if( 0 == send_request(events[i].data.fd, request, strlen(request)))
                        {
                            ERROR( "send request failed\n");
                            epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                            close (events[i].data.fd);
                            updateRequestCounter();
                        }else
                        {
                            events[i].events =   EPOLLET | EPOLLIN;
                            epoll_ctl(efd,EPOLL_CTL_MOD,events[i].data.fd,&events[i]);
                            
                            
                        }
                    }else
                    {

                        event.events = 0;
                        event.data.fd = events[i].data.fd; // user data
                        epoll_ctl (efd, EPOLL_CTL_DEL,
                                   events[i].data.fd, &event);
                        close (events[i].data.fd);
                        g_totalFailConnect++;
                        updateRequestCounter();
                    }
                }
                else
                {
                    ERROR("fail to get sockopt\n");
                    epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                    close (events[i].data.fd);
                    updateRequestCounter();
                    
                }
                
            }else if( events[i].events & EPOLLIN)
            {
                DEBUG( "have epollin\n");
                if( -1 ==  fetch_response(events[i].data.fd))
                {
                    DEBUG( "read response failed\n");
                    epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                    close (events[i].data.fd);
                    updateRequestCounter();
                }
                else 
                {
                    DEBUG( "read response OK\n");
                    epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                    close (events[i].data.fd);
                    updateRequestCounter();
                    g_totalResponseRecv++;
                }
                
            }else
            {
                ERROR( "unexpected event come\n");
                epoll_ctl(efd, EPOLL_CTL_DEL,events[i].data.fd, &events[i]);
                close (events[i].data.fd);
                updateRequestCounter();
                g_totalFailUnexp++;  
            }
            if ( g_allResponseCounter >= g_nextLevelResponse )
            {
                ERROR("%d request completed\n", g_nextLevelResponse);
                g_nextLevelResponse += g_total_n / 10;
                gettimeofday( &nowtime, NULL );
                g_finTime[g_finTime_counter] = timeval_diff(&nowtime, &starttime);
                g_finTime_counter++;
            }
       }
       if(total_n<=0 && g_req_counter <= 0)
       {
            ERROR("%d request completed\n", g_nextLevelResponse);
            gettimeofday( &nowtime, NULL );
            g_finTime[g_finTime_counter] = timeval_diff(&nowtime, &starttime);

            break;
       }
    }
    
    free (events);
    free(request);
    close (efd);
    gettimeofday( &nowtime, NULL );
    
    ERROR( "total reqeusts:%d\n"
           "total response received:%d\n"
           "error received:%d\n"      
           "error unexpected:%d\n"
           "error when connect:%d\n"
           " \t\t timeout:%d\n"
           " \t\t reset:%d\n"
           "total time:%dms\n\n",
           g_totalSendReq,
           g_totalResponseRecv,
           g_totalErrorRecv,
           g_totalFailUnexp,
           g_totalFailConnect,
           g_totalConnectTimeout,
           g_totalConnectReset,
           timeval_diff(&nowtime, &starttime));
    for( i = 0; i < 10; i++)
        ERROR("%d0%% completed in %dms\n", (i+1), g_finTime[i]);
           
}
