#include <stdio.h>
#include "svc_handler.h"
#include <signal.h>

#ifdef TEST_LOG
#include "log.h"
extern Log g_logger; 
#endif

GMyEventBase g_base; 
GEventHandler* GMyEventBase::create_handler () 
{
    return new svc_handler; 
}

void sig_int (int signo)
{
    printf ("%d caught\n", signo); 
    //signal (SIGINT, sig_int); 
    g_base.exit (1); 
    printf ("exit ok\n"); 
}

#ifdef HAS_ALLSIG
void install_all_signal ()
{
    //signal (SIGINT, sig_int); 
    struct sigaction act; 
    act.sa_handler = sig_int; 
    sigemptyset(&act.sa_mask);   
    act.sa_flags = SA_RESTART; 
    for (int i=1; i<NSIG; ++ i)
    {
        if (sigaction (i, &act, NULL) < 0)
        {
            printf ("install %d failed, errno %d\n", i, errno); 
            continue; 
            //return -1; 
        }
        else
            printf ("install %d ok\n", i); 
    }
}
#endif

#ifdef TEST_TIMER
void* g_t1, *g_t2; 
#endif

int main (int argc, char *argv[])
{
    if (argc < 2)
    {
        printf ("usage: epoll_svc port\n"); 
        return -1; 
    }

#ifdef TEST_LOG
    g_logger.initLog ("test", "1.0", "epoll_svc"); 
#endif

    unsigned short port = atoi (argv[1]);

#ifndef WIN32
#  ifdef HAS_ALLSIG
    install_all_signal (); 
#  else
    struct sigaction act; 
    act.sa_handler = sig_int; 
    sigemptyset(&act.sa_mask);   
    act.sa_flags = SA_RESTART; 
    if (sigaction (SIGINT, &act, NULL) < 0)
    {
        printf ("install SIGINT failed, errno %d\n", errno); 
        return -1; 
    }
    else
        printf ("install SIGINT ok\n"); 
#  endif
#endif

    // to test small message block
    if (!g_base.init (/*8, 10*/))
        return -1; 

    printf ("init ok\n"); 
    do
    {
#ifdef TEST_TIMER
        //g_t1 = g_base.timeout (1000, 1000, NULL, NULL); 
        //g_t1 = g_base.timeout (10, 1000, NULL, NULL); 
        g_t1 = g_base.timeout (10, 0, NULL, NULL); 
        if (g_t1 == NULL)
        {
            printf ("timeout failed\n"); 
            break; 
        }
        else 
            printf ("set timer %p ok\n", g_t1); 
#endif

        printf ("timeout ok\n"); 
        if (!g_base.listen (port))
        {
            g_base.exit (0); 
            printf ("exit ok\n"); 
            break; 
        }

        printf ("listen ok\n"); 
        g_base.run (); 
        printf ("run  over\n"); 
    } while (0); 

    g_base.fini (); 
    printf ("fini ok\n"); 

    g_base.cleanup (); 
    printf ("cleanup ok\n"); 
    return 0; 
}
