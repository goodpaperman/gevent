#pragma once


#include "EventBase.h"
#include <thread>

#define GEV_RECONNECT_TIMEOUT 2 // seconds
#define GEV_RECONNECT_TIMEOUT_MAX 256 // seconds
#define GEV_RECONNECT_TRY_MAX -1 // infinite retry

/**
 * @brief event base with auto reconnection support
 * @note only support one auto-reconect connections
 */
class GEventBaseWithAutoReconnect : public GEventBase
{
public:
    GEventBaseWithAutoReconnect(int reconn_min = GEV_RECONNECT_TIMEOUT, int reconn_max = GEV_RECONNECT_TIMEOUT_MAX, int max_retry = GEV_RECONNECT_TRY_MAX);
    ~GEventBaseWithAutoReconnect();

    /**
     * @brief get underline connection handler
     * @return the connection handler that support auto reconnect
     */
    GEventHandler* connector(); 

    /**
     * @brief issue the active connection
     * @param port server port
     * @param host server host
     * @param arg user special data
     * @return true - connection ok; false - failed
     * @note only different with connect is the auto-reconnect character
     */
    bool do_connect(unsigned short port, char const* host = "127.0.0.1", void *arg = nullptr);
    virtual bool filter_handler(GEventHandler *h);

protected:
    virtual void on_error(GEventHandler *h);
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd);

    /**
     * @brief called when detecting connection breaks
     * @return none
     *
     * start the reconnect action at this point 
     */
    virtual void on_connect_break(); 

    /**
     * @brief called when connection established
     * @param app handler binding to new connection
     * @return true - process ok; false - falied
     * 
     * do initialize work at this point
     */
    virtual bool on_connected(GEventHandler *app);

    /**
     * @brief called when reconnect retry reach the limit
     * @param arg user special data passed in do_connect
     * @return none
     */
    virtual void on_retry_max(void *arg); 

protected:
    /**
     * @brief doing the dirty work for reconnect
     * @param arg user special data passed in do_connect
     * @note reconnect will occur on 2/4/8/16/32/64/128/256/256/... seconds 
     *       each time, these value can be set in constructor, 
     *       if max_retry set to -1, no limit will reach.
     */
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

