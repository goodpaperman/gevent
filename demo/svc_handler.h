#include "EventBase.h"
#include "EventHandler.h"

class GMyEventBase : public GEventBase
{
public:
    GEventHandler* create_handler (); 
}; 


class svc_handler : public GJsonEventHandler
{
public:
    virtual ~svc_handler () {}
#ifdef TEST_TIMER
    virtual bool on_timeout (GEV_PER_TIMER_DATA *gptd); 
#endif
    virtual void on_read_msg (Json::Value const& val); 
};
