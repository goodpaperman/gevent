#include "EventBaseAR.h"
#include "EventHandler.h"

class GMyEventBase : public GEventBaseWithAutoReconnect
{
public:
    GEventHandler* create_handler (); 
}; 


class clt_handler : public GJsonEventHandler
{
public:
    virtual ~clt_handler () {}
#ifdef TEST_TIMER
    virtual bool on_timeout (GDP_PER_TIMER_DATA *gptd); 
#endif
    virtual void on_read_msg (Json::Value const& val); 
};
