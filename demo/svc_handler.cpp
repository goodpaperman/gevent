#include "svc_handler.h"

#ifdef TEST_TIMER
extern GMyEventBase g_base; 
extern void* g_t1, *g_t2; 
bool svc_handler::on_timeout (GDP_PER_TIMER_DATA *gptd)
{
    printf ("time out: id %p, due %d, period %d\n", gptd, gptd->due_msec, gptd->period_msec); 
    IEventBase *base = gptd->base; 
    if (base != dynamic_cast<IEventBase*>(&g_base))
        printf ("base %p != g_base %p\n", base, &g_base); 
#if 1
    // int n = 1; 
    int n = 2; 
    if (gptd->period_msec != 0)
    {
        base->cancel_timer (g_t1); 
        g_t1 = base->timeout (gptd->due_msec * n, 10000, NULL, NULL); 
    }
    else 
        g_t1 = base->timeout (gptd->due_msec * n, 0, NULL, NULL); 

    if (g_t1 == nullptr)
        printf ("create timer failed\n"); 
    else 
        printf ("switch to timer %p\n", g_t1); 
#elif 0
    if (gptd == g_t1)
    {
        base->cancel_timer (g_t1); 
        g_t2 = base->timeout (700, 1000, NULL, NULL); 
        if (g_t2 == nullptr)
            printf ("create timer1 failed\n"); 
        else 
            printf ("switch to timer2 %p\n", g_t2); 
    }
    else 
    {
        base->cancel_timer (g_t2); 
        g_t1 = base->timeout (800, 2000, NULL, NULL); 
        if (g_t1 == nullptr)
            printf ("create timer1 failed\n"); 
        else 
            printf ("switch to time1 %p\n", g_t1); 
    }
#else
    sleep (10); 
    printf ("timer %p handle over work on handler %p\n", gptd, this); 
#endif

    return true; 
}
#endif

void svc_handler::on_read_msg (Json::Value const& val)
{
    int key = val["key"].asInt (); 
    std::string data = val["data"].asString (); 
    printf ("got %d:%s\n", key, data.c_str ()); 

    Json::Value root; 
    Json::FastWriter writer; 
    root["key"] = key + 1; 
    root["data"] = data; 

#if 0
    // to test leader-follower thread pool
    // switch leader when working long.
    sleep (3); 
#endif

    int ret = 0;
    std::string resp = writer.write(root); 
    resp = resp.substr (0, resp.length () - 1); // trim tailing \n
    if ((ret = send (resp)) <= 0)
        printf ("send response failed, errno %d\n", errno); 
    else 
        printf ("response %d\n", ret); 
}
