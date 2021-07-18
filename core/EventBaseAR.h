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
    /**
     * @brief constructor to init reconnect parameters
     * @param reconn_min minimum time interval to do reconnect
     * @param reconn_max maximum time interval to do reconnect
     * @param max_retry max retry times, -1 for infinite
     */
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
     * @retval true - connection ok
     * @retval false - failed
     * @note only different with connect is the auto-reconnect character
     */
    bool do_connect(unsigned short port, char const* host = "127.0.0.1", void *arg = nullptr);

    /** @see GEventBase::filter_handler */
    virtual bool filter_handler(GEventHandler *h);

protected:
    /** @see GEventBase::on_error */
    virtual void on_error(GEventHandler *h);

    /** @see GEventBase::on_timeout */
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd);

    /**
     * @brief called when detecting connection breaks
     *
     * start the reconnect action at this point 
     */
    virtual void on_connect_break(); 

    /**
     * @brief called when connection established
     * @param app handler binding to new connection
     * @retval true - process ok
     * @retval false - falied
     * 
     * do initialize work at this point
     */
    virtual bool on_connected(GEventHandler *app);

    /**
     * @brief called when reconnect retry reach the limit
     * @param arg user special data passed in do_connect
     */
    virtual void on_retry_max(void *arg); 

    /**
     * @brief doing the dirty work for reconnect
     * @param arg user special data passed in do_connect
     * @note reconnect will occur on 2/4/8/16/32/64/128/256/256/... seconds 
     * each time, these value can be set in constructor, 
     * if max_retry set to -1, no limit will reach.
     */
    void do_reconnect(void *arg);

protected:
    std::string m_host;         /**< host to connect to */
    unsigned short m_port;      /**< port to connect to */
    GEventHandler* m_app;       /**< handler for initiative inside connection */
    GEventHandler* m_htimer;    /**< timer handler for reconnect */
    void* m_timer;              /**< timer for reconnect */
    int m_reconn_min;           /**< minimum time interval for reconnect */
    int m_reconn_max;           /**< maximum time interval for reconnect */
    int m_reconn_curr;          /**< current time interval for reconnect, start from m_reconn_min, grows max to m_reconn_max */
    int m_retry_max;            /**< retry max times for reconnect, -1 unlimited */
    int m_retry_curr;           /**< current retry times */
};

