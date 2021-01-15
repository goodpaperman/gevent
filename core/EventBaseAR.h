#pragma once


#include "EventBase.h"
#include <thread>

#define GEV_RECONNECT_TIMEOUT 2 // seconds
#define GEV_RECONNECT_TIMEOUT_MAX 256 // seconds
#define GEV_RECONNECT_TRY_MAX -1 // infinite retry

class GEventBaseWithAutoReconnect : public GEventBase
{
public:
    GEventBaseWithAutoReconnect(int reconn_min = GEV_RECONNECT_TIMEOUT, int reconn_max = GEV_RECONNECT_TIMEOUT_MAX, int max_retry = GEV_RECONNECT_TRY_MAX);
    ~GEventBaseWithAutoReconnect();

    GEventHandler* connector(); 
    bool do_connect(unsigned short port, char const* host = "127.0.0.1", void *arg = nullptr);
    // whether this handler should be processed in foreach, 
    // true - process; false - skip 
    virtual bool filter_handler(GEventHandler *h);

protected:
    virtual void on_error(GEventHandler *h);
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd);

    virtual void on_connect_break(); 
    virtual bool on_connected(GEventHandler *app);
    virtual void on_retry_max(void *arg); 

protected:
    void do_reconnect(void *arg);

protected:
    std::string m_host;
    unsigned short m_port; 
    GEventHandler* m_app;
    GEventHandler* m_htimer; 
    void* m_timer;
    int m_reconn_min; 
    int m_reconn_max; 
    int m_reconn_curr;
    int m_retry_max; 
    int m_retry_curr; 
};

