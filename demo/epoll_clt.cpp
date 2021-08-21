#include <stdio.h>
#include "clt_handler.h"
#include <signal.h>

#ifdef TEST_LOG
#include "log.h"
extern Log g_logger; 
#endif

//#define TEST_READ
//#define TEST_CONN
//#define TEST_TIMER

GMyEventBase g_base; 
GEventHandler* GMyEventBase::create_handler () 
{
    return new clt_handler; 
}


int sig_caught = 0; 
void sig_int (int signo)
{
    sig_caught = 1; 
    printf ("%d caught\n", signo); 
    g_base.exit (0); 
    printf ("exit ok\n"); 
}

void do_read (GEventHandler *eh, int total)
{
    char buf[1024] = { 0 }; 
    int ret = 0, n = 0, key = 0, err = 0;
    char *ptr = nullptr; 
    while ((total == 0 ||  n++ < total) && fgets (buf, sizeof(buf), stdin) != NULL)
    {
        // skip \n
        buf[strlen(buf) - 1] = 0; 
        //n = sscanf (buf, "%d", &key); 
        key = strtol (buf, &ptr, 10); 
        if (ptr == nullptr)
        {
            printf ("format: int string\n"); 
            continue; 
        }

        Json::Value root; 
        Json::FastWriter writer; 
        root["key"] = key; 
        // skip space internal
        root["data"] = *ptr == ' ' ? ptr + 1 : ptr;  

        std::string req = writer.write (root); 
        req = req.substr (0, req.length () - 1); // trim tailing \n
        if ((ret = eh->send (req)) <= 0)
        {
            err = 1; 
            printf ("send %lu failed, errno %d\n", req.length (), errno); 
            break; 
        }
        else 
            printf ("send %d\n", ret); 
    }

    if (total == 0)
        printf ("reach end\n"); 

    if (!err)
    {
        eh->disconnect (); 
        printf ("call disconnect to notify server\n"); 
    }

    // wait receiving thread 
    //sleep (3); 
    // if use press Ctrl+D, need to notify peer our break
}

#ifdef TEST_TIMER
void test_timer (unsigned short port, int period_msec, int times)
{
    int n = 0; 
    GEventHandler *eh = nullptr; 

    do
    {
        eh = g_base.connect (port); 
        if (eh == nullptr)
            break;

        printf ("connect ok\n"); 
        void* t = g_base.timeout (1000, period_msec, eh, NULL); 
        if (t == NULL)
        {
            printf ("timeout failed\n"); 
            break; 
        }
        else 
            printf ("set timer %p ok\n", t); 

        // to wait timer
        do
        {
            sleep (400); 
            printf ("wake up from sleep\n"); 
        } while (!sig_caught && n++ < times);

        g_base.cancel_timer (t); 
    } while (0); 
}
#endif

#ifdef TEST_CONN
void test_conn (unsigned short port, int per_read, int times)
{
#  ifdef WIN32
    srand (GetCurrentProcessId()); 
#  else
    srand (getpid ()); 
#  endif
    int n = 0, elapse = 0; 
    clt_handler *eh = nullptr; 

    do
    {
        eh = (clt_handler *)g_base.connect (port); 
        if (eh == nullptr)
            break;

        printf ("connect ok\n"); 

        do_read (eh, per_read); 
#  ifdef WIN32
        elapse = rand() % 1000; 
        Sleep(elapse); 
#  else
        elapse = rand () % 1000000; 
        usleep (elapse); 
#  endif

#if 0
        // on error, eh will be deleted automatically, so use with care
        // and if no error occurs, we call disconnect has no effect on us
        eh->disconnect (); 
        printf ("disconnect\n"); 
#endif
        printf ("running  %.3f ms\n", elapse/1000.0); 
    } while (!sig_caught && n++ < times);
}
#endif

#ifdef TEST_READ
void test_read (unsigned short port, int total)
{
    GEventHandler *eh = nullptr; 

    do
    {
        eh = g_base.connect (port); 
        if (eh == nullptr)
            break;

        printf ("connect ok\n"); 
        do_read (eh, total); 
    } while (0); 
}
#endif

int main (int argc, char *argv[])
{
    if (argc < 2)
    {
        printf ("usage: epoll_clt port\n"); 
        return -1; 
    }

#ifdef TEST_LOG
    g_logger.initLog ("test", "1.0", "epoll_clt"); 
#endif

    unsigned short port = atoi (argv[1]); 

#ifndef WIN32
    //signal (SIGINT, sig_int); 
    struct sigaction act; 
    act.sa_handler = sig_int; 
    sigemptyset(&act.sa_mask);   
    // to ensure read be breaked by SIGINT
    act.sa_flags = 0; //SA_RESTART;  
    if (sigaction (SIGINT, &act, NULL) < 0)
    {
        printf ("install SIGINT failed, errno %d\n", errno); 
        return -1; 
    }
#endif

    if (!g_base.init (2))
        return -1; 

    printf ("init ok\n"); 

#if defined(TEST_READ)
    test_read (port, 0); // 0 means infinite loop until user break
#elif defined(TEST_CONN)
    test_conn (port, 10, 100); 
#elif defined (TEST_TIMER)
    test_timer (port, 10, 1000); 
#else
#  error please define TEST_XXX macro to do something!
#endif

    if (!sig_caught)
    {
        // Ctrl + D ?
        g_base.exit (0); 
        printf ("exit ok\n"); 
    }
    else 
        printf ("has caught Ctrl+C\n"); 

    g_base.fini (); 
    printf ("fini ok\n"); 

    g_base.cleanup (); 
    printf ("cleanup ok\n"); 
    return 0; 
}
