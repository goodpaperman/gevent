#include "clt_handler.h"

#ifdef TEST_TIMER
extern void do_read (clt_handler *, int); 
bool clt_handler::on_timeout (GDP_PER_TIMER_DATA *gptd)
{
    printf ("time out ! id %p, due %d, period %d\n", gptd, gptd->due_msec, gptd->period_msec); 
    do_read ((clt_handler *)gptd->user_arg, 1); 
    return true; 
}
#endif

void clt_handler::on_read_msg (Json::Value const& val)
{
    int key = val["key"].asInt (); 
    std::string data = val["data"].asString (); 
    printf ("got %d:%s\n", key, data.c_str ()); 
}
