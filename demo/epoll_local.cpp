#include <stdio.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#ifdef WIN32

int main(int argc, char *argv[])
{
    printf("this demo support linux only\n"); 
    return -1; 
}

#else
// this demo only support linux
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <string>
#include "json/json.h"
#define LG(format, args...) do {printf("%lu ", pthread_self ()); printf(format"\n", ##args);} while(0)


int running = 1; 
void sig_int (int signo)
{
    running = 0; 
    LG ("%d caught", signo); 
}

static void setnonblocking (int fd)
{
    int opts = fcntl (fd, F_GETFL); 
    if (opts < 0)
    {
        LG("get sock %d flag failed, errno %d", fd, errno); 
        return; 
    }

    opts |= O_NONBLOCK; 
    if (fcntl (fd, F_SETFL, opts) < 0)
        LG("set sock %d flag failed, flag 0x%08x, errno %d", fd, opts, errno); 
}

void* run_proc(void *arg)
{
    int ret = 0; 
    int m_ep = *(int*)arg; 
    struct epoll_event eps; 
    while (running)
    {
        // can handle only one request a time 
        // -1: all timer goes into pipe, no timeout here! 
        ret = epoll_wait (m_ep, &eps, 1, -1); 
        if (ret < 0)
        {
            LG("epoll_wait failed with %d", errno); 
            continue; 
        }


        // always 1 or 0 events
        //LG("%d events detected", ret); 
        if (ret == 0)
        {
            LG("should never timeout !"); 
            continue; 
        }

        bool readd = true; 
        int fd = eps.data.fd; 
        if (eps.events & EPOLLIN)
        {
            // data arrive on connections
            // if recv failled, not added it to epoll anymore
            char buf[1025] = { 0 }; 
            ret = read (fd, buf, 1024); 
            if (ret < 0)
            {
                if (errno != EAGAIN)
                    LG("read %d failed, ret %d, errno %d", fd, ret, errno); 

                break;
            }
            else if (ret == 0)
            {
                // no more data
                LG("connection break detected, fd %d", fd); 
                break;
            }

            buf[ret] = 0; 
            LG("receiving %d: %s", fd, buf); 
        }

        if (eps.events & EPOLLERR || eps.events & EPOLLHUP)
        {
            // exception on connections
            //do_error (key); 
            LG("error on %d detected", fd); 
        }

        // to see any other flag left?
        if (eps.events & ~(EPOLLIN | EPOLLERR | EPOLLHUP))
            LG("unexpect events flag 0x%08x, fd = %d", eps.events, fd); 
    }

    LG("run proc exit"); 
}

void do_read (int fd, int total)
{
    char buf[1024] = { 0 }; 
    int ret = 0, n = 0, key = 0;
    char *ptr = nullptr; 
    while ((total == 0 ||  n++ < total) && fgets (buf, sizeof(buf), stdin) != NULL)
    {
        // skip \n
        buf[strlen(buf) - 1] = 0; 
        //n = sscanf (buf, "%d", &key); 
        key = strtol (buf, &ptr, 10); 
        if (ptr == nullptr)
        {
            LG ("format: int string"); 
            continue; 
        }

        Json::Value root; 
        Json::FastWriter writer; 
        root["key"] = key; 
        // skip space internal
        root["data"] = *ptr == ' ' ? ptr + 1 : ptr;  

        std::string req = writer.write (root); 
        req = req.substr (0, req.length () - 1); // trim tailing \n
        if ((ret = send (fd, req.c_str(), req.length(), 0)) != req.length ())
        {
            LG ("send %d failed, errno %d", req.length (), errno); 
            break; 
        }
        else 
            LG ("send %d", ret); 
    }

    if (total == 0)
        LG ("reach end"); 

    // wait receiving thread 
    //sleep (3); 
    // if use press Ctrl+D, need to notify peer our break
}

int main (int argc, char *argv[])
{
    if (argc < 2)
    {
        LG ("usage: epoll_local port"); 
        return -1; 
    }

    int ret = 0;
    unsigned short port = atoi (argv[1]); 

    struct sigaction act; 
    act.sa_handler = sig_int; 
    sigemptyset(&act.sa_mask);   
    // to ensure read be breaked by SIGINT
    act.sa_flags = 0; //SA_RESTART;  
    if (sigaction (SIGINT, &act, NULL) < 0)
    {
        LG ("install SIGINT failed, errno %d", errno); 
        return -1; 
    }

    int m_ep = epoll_create (1/*just a hint*/); 
    if (m_ep < 0)
    {
        LG("create epoll instance failed, errno %d", errno); 
        return -1; 
    }
    else 
        LG("create epoll fd %d", m_ep); 

    // installl SIGPIPE handler to avoid exit on broken pipe/socket
    act.sa_handler = SIG_IGN; 
    act.sa_flags = SA_RESTART; 
    sigemptyset (&act.sa_mask); 
    if (sigaction (SIGPIPE, &act, NULL) < 0)
    {
        LG("ignore SIGPIPE failed, errno %d", errno); 
        return -1; 
    }
    else
        LG("ignore SIGPIPE ok"); 

    LG ("init ok"); 
    pthread_t tid = 0; 
    ret = pthread_create (&tid, NULL, run_proc, (void *)&m_ep); 
    if (ret != 0)
    {
        LG("create run thread failed, errno %d", errno); 
        return -1; 
    }

    LG ("create run thread ok"); 
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); //INADDR_ANY;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        LG("create socket failed, errno %d", errno);
        return -1;
    }

    ret = ::connect(fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
    if (ret == -1)
    {
        LG("connect to server failed, errno %d", errno);
        return -1;
    }

    sockaddr_in local, remote;
    socklen_t locallen, remotelen;
    locallen = remotelen = sizeof(sockaddr_in);
    if (getsockname(fd, (sockaddr *)&local, &locallen) == -1)
    {
        LG("get connector local address failed, errno %d", errno);
        //memset(&local, 0, sizeof(local));
        locallen = 0;
        // not fatal
    }
    else
        LG("connector local address %s:%d", inet_ntoa(local.sin_addr), ntohs(local.sin_port));

    if (getpeername(fd, (sockaddr *)&remote, &remotelen) == -1)
    {
        LG("get connector remote address failed, errno %d", errno);
        //memset(&remote, 0, sizeof(remote));
        remotelen = 0;
        // not fatal
    }
    else
        LG("connector remote address %s:%d", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

    setnonblocking (fd); 
    LG ("connect ok"); 

    // reuse this event, we don't use it later..
    struct epoll_event ev; 
    ev.events = EPOLLIN; 
    ev.data.fd = fd; 
    if (epoll_ctl (m_ep, EPOLL_CTL_ADD, fd, &ev) < 0)
    {
        LG("epoll ctl %d failed, errno %d", fd, errno); 
        return -1; 
    }

    LG("bind newly connected socket %d to epoll ok", fd);

    int total = 0; 
    do_read (fd, total); 

    running = 0; 
    if (m_ep != -1)
    {
        close (m_ep);
        m_ep = -1; 
    }

    pthread_join (tid, NULL); 
    LG ("fini ok"); 
    return 0; 
}

#endif
