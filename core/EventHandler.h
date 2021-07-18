#pragma once

#ifdef WIN32
// must ensure <winsock2.h> precedes <widnows.h> included, to prevent winsock2.h conflict with winsock.h
#  include <WinSock2.h>
#  include <Windows.h>
#  include <mswsock.h>  // for LPFN_ACCEPTEX & LPFN_GETACCEPTEXSOCKADDRS later in EventBase.h
#else
#  include <unistd.h> // for close
#  include <sys/socket.h>
#  include <sys/epoll.h>
#  include <sys/time.h>
#  include <netinet/in.h> // for struct sockaddr_in
#  include <arpa/inet.h> // for inet_addr/inet_ntoa
#  include <string.h> // for memset/memcpy
#  include <signal.h>
#endif

#include <mutex>
#include "jsoncpp/json.h"
#include "platform.h"


class GEventHandler; 
struct GEV_PER_TIMER_DATA; 
/**
 * @class IEventBase
 * @brief abstract interface for all EventBase
 * @note GEventHandler use this to do some callbacks
 */
class IEventBase
{
public:
#ifdef WIN32
    /** @brief get underline IOCP handler on win32 */
    virtual HANDLE iocp () const = 0; 
#else
    /** @brief get underline event poll fd on linux */
    virtual int epfd () const = 0; 
#endif

    /**
     * @brief setup timer 
     * @param due_msec millisecond for first timeout
     * @param period_msec millisecond for later timeout
     * @param arg user special key
     * @param exist_handler re-use existing handler that user provided
     * @retval timer-id, later user can use this to cancel timer
     * @retval nullptr if failed
     */
    virtual void* timeout(int due_msec, int period_msec, void *arg, GEventHandler *exist_handler) = 0; 

    /** 
     * @brief cancel existing timer
     * @param tid timer-id that timeout returns
     * @retval true - success
     * @retval false - failure
     */
    virtual bool cancel_timer(void* tid) = 0; 

    /**
     * @brief when timer due in underline system timer facility, notify event engine to do timer callbacks
     * @param gptd data binding to due timer
     * @retval true - notify ok
     * @retval false - notify failed
     */
    virtual bool post_timer(GEV_PER_TIMER_DATA *gptd) = 0; 
};


#ifdef WIN32
/**
 * @enum GEV_IOCP_OP
 * @brief IOCP operation type
 */
enum GEV_IOCP_OP
{
    OP_TIMEOUT = 1,  /**< timer event */
    OP_ACCEPT,       /**< accept event */
    OP_RECV,         /**< data event */
};
#else 
/**
 * @struct conn_key_t
 * @brief key information for a connection
 * @note the purpose of this key is to distinguish different connections with same fd ! (when connection break and re-established soon, fd may not change but port will change)
 */
struct conn_key_t
{
    int fd;                   /**< connection file descriptor */
    unsigned short lport;     /**< local port */
    unsigned short rport;     /**< remote port */

    conn_key_t (int f, unsigned short l, unsigned short r); 
    bool operator< (struct conn_key_t const& rhs) const; 
}; 
#endif

/** @brief data that binding to handler, or a connection */
struct GEV_PER_HANDLE_DATA
{
    SOCKET so;            /**< connection file descriptor */
    SOCKADDR_IN laddr;    /**< local ip address */
    SOCKADDR_IN raddr;    /**< remote ip address */

#ifndef WIN32
    /** @brief access key data on win32 */
    conn_key_t key () const; 
#endif

    GEV_PER_HANDLE_DATA(SOCKET s, SOCKADDR_IN *l, SOCKADDR_IN *r); 
    virtual ~GEV_PER_HANDLE_DATA(); 
};

/** @brief data that binding to one action, timer/accept/recv... */
struct GEV_PER_IO_DATA
{
    SOCKET so;          /**< connection file descriptor */
#ifdef WIN32
    GEV_IOCP_OP op;     /**< IOCP type on win32*/
    OVERLAPPED ol;      /**< OVERLAPPED information on win32 */
    WSABUF wsa;         /**< buffer for win32, wsa.len is the buffer length */
    DWORD bytes;        /**< bytes transfered after read completed on win32 */
#else
    char *buf;          /**< data buffer prepared for receiving for linux */
    int len;            /**< bytes transferred after read completed on linux */
#endif

    GEV_PER_IO_DATA(
#ifdef WIN32
            GEV_IOCP_OP o, 
#endif
            SOCKET s, int l); 
    virtual ~GEV_PER_IO_DATA(); 
};

/** @brief data that binding to one timer */
struct GEV_PER_TIMER_DATA
#ifdef WIN32
       : public GEV_PER_IO_DATA
#endif
{
    IEventBase *base;   /**< EventBase this timer belongs to */
    int due_msec;       /**< first due time in milli-second */
    int period_msec;    /**< later due time in milli-second */
    void *user_arg;     /**< user special data binding to this timer */
    bool cancelled;     /**< if cancelled */
#ifdef WIN32
    HANDLE timerque;    /**< timer queue handler on win32 */
    HANDLE timer;       /**< timer handler on win32 */
#else
    timer_t timer;      /**< timer handler on linux */
#endif

    GEV_PER_TIMER_DATA(IEventBase *base, int due, int period, void *arg
#ifdef WIN32
            , HANDLE tq);
#else
            , timer_t tid); 
#endif

    virtual ~GEV_PER_TIMER_DATA(); 
    /** @brief cancel this timer */
    void cancel (); 
};

/**
 * @brief event handler binding to a connection
 *
 * user would better inherit it's own handler from this, 
 * and handle business in callbacks
 */
class GEventHandler
{
public:
    GEventHandler();
    virtual ~GEventHandler();

    /** @brief return data that binding to event handler */
    GEV_PER_HANDLE_DATA* gphd(); 

    /** @brief return data that binding to timer handler */
    GEV_PER_TIMER_DATA* gptd(); 

    /**
     * @brief tell if we are connected
     * @retval true - connected
     * @retval false - not
     */
    bool connected();

    /** @brief break the connection */
    void disconnect(); 

    /** @brief clear data status inside */
    void clear(); 

    /**
     * @brief return underline connection file descriptor
     * @return connection
     */
    SOCKET fd(); 

    /**
     * @brief send data without prewrite handling
     * @param buf data prepare to write
     * @param len data length
     * @retval >0 - success
     * @retval -1 - failed
     */
    int send_raw(char const* buf, int len);

    /**
     * @brief send string without prewrite handling
     * @param str string prepare to write
     * @retval >0 - success
     * @retval -1 - failed
     */
    int send_raw(std::string const& str); 

    /**
     * @brief send data with prewrite handling
     * @param buf data prepare to write
     * @param len data length
     * @retval >0 - success
     * @retval -1 - failed
     */
    int send(char const* buf, int len);

    /**
     * @brief send string with prewrite handling
     * @param str string prepare to write
     * @retval >0 - success
     * @retval -1 - failed
     */
    int send(std::string const& str);
    
    /**
     * @brief tell EventBase whether enable reuse
     * @retval true - will reuse
     * @retval false - no reuse
     *
     * when reuse is enabled, framework won't delete handler object soon
     * after connection is closed, it is user's responsibility to 
     * free the memory
     */
    virtual bool reuse();

    /**
     * @brief tell EventBase whether enable reconnect
     * @retval true - will reconnect
     * @retval false - no reconnect
     *
     * when auto-reconnect is enabled, framework will try to 
     * re-establish the ative connection when detecting
     * conenction break
     */
    virtual bool auto_reconnect();

    /**
     * @brief remember user provide data in handler
     * @param param user provided data
     * @note user special data will useful to distinguish different handlers,
     * especially for timer
     */
    virtual void arg(void *param) = 0;

    /**
     * @brief reset inner data status to initial
     * @param gphd new data per handler
     * @param gptd new data per timer
     * @param base new EventBase
     */
    virtual void reset(GEV_PER_HANDLE_DATA *gphd, GEV_PER_TIMER_DATA *gptd, IEventBase *base);

    /**
     * @brief connection has data arriving 
     * @param gpid data binding to former read
     * @note user must implement this method to handle data on connection
     * @retval true - handle ok
     * @retval false - handle fail
     */
    virtual bool on_read(GEV_PER_IO_DATA *gpid) = 0;

    /**
     * @brief connection has break
     * @param gphd data binding to this handler
     * @note user can implement this method to detect connection status
     */
    virtual void on_error(GEV_PER_HANDLE_DATA *gphd); 

    /**
     * @brief timer has due
     * @param gptd data binding to this timer
     * @retval true - handle ok
     * @retval false - handle fail
     * @note when on_timeout called, handler's base may cleared by cancel_timer, 
     * use gptd->base instead if it is not null.
     * user must implement this method to receive timer notify
     */
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd) = 0; 

    /**
     * @brief shut down underline facilities
     * @param terminal are we going to die ?
     */
    virtual void cleanup(bool terminal);

    /**
     * @brief shut down socket connection
     * @param terminal are we going to die ?
     */
    void close(bool terminal);

protected:
    /**
     * @brief tell framework whether enable pre-read mechanism
     * @retval true - enable
     * @retval false - disable
     * 
     * if pre read mechanism enabled, 
     * before each on_read called, 
     * pre_read will called first to give user a chance 
     * to prepare raw data
     *
     * @par think like this:
     * @code
     *     on_read (pre_read (msg)); 
     * @endcode
     */
    virtual bool has_preread() const; 

    /**
     * @brief tell framework whether enable pre-write mechanism
     * @retval true - enable
     * @retval false - disable
     * 
     * if pre write mechanism enabled, 
     * after each send called, 
     * pre_write will called first to give user a chance 
     * to prepare raw data
     *
     * @par think like this:
     * @code
     *      send (pre_write (msg)); 
     * @endcode
     */
    virtual bool has_prewrite() const; 

    /**
     * @brief pre-read callbacks
     * @param buf data will pass to on_read
     * @param len data length will pass to on_read
     * @return handled data after pre_read
     *
     * this callback only takes effect when has_preread return true, 
     */
    virtual std::string pre_read (char const* buf, int len); 

    /**
     * @brief pre-write callbacks
     * @param buf data will pass to send
     * @param len data length will pass to send
     * @return handled data after pre_write
     *
     * this callback only takes effect when has_prewrite return true, 
     */
    virtual std::string pre_write (char const* buf, int len); 

protected:
    GEV_PER_HANDLE_DATA *m_gphd = nullptr;     /**< data binding to this connection */
    GEV_PER_TIMER_DATA *m_gptd = nullptr;      /**< data binding to this timer */
    IEventBase *m_base = nullptr;              /**< corresponding GEventBase */
    // us so instead of m_gphd, 
    // as the later one may destroyed during using..
    SOCKET m_so;                               /**< underline system connection file descriptor */
};

/**
 * @brief a common handler to process json protocol.
 * @todo just a sample how to use gevent, should move outside of core
 */
class GJsonEventHandler : public GEventHandler
{
public:
    //virtual void on_read();
    /** @see GEventHandler::arg */
    virtual void arg(void *param);
    /** @see GEventHandler::reset */
    virtual void reset(GEV_PER_HANDLE_DATA *gphd, GEV_PER_TIMER_DATA *gptd, IEventBase *base);
    /** @see GEventHandler::on_read */
    virtual bool on_read(GEV_PER_IO_DATA *gpid);
    /** @see GEventHandler::on_read_msg */
    virtual void on_read_msg(Json::Value const& root) = 0;
    /** @see GEventHandler::on_timeout */
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd);
    /** @see GEventHandler::cleanup */
    virtual void cleanup(bool terminal);

    //virtual void on_event(short type);

protected:
    // protect m_stub to prevent multi-entry
#ifdef HAS_ET
    std::mutex m_mutex;              /**< lock to protect m_stub */
#endif

    std::string m_stub;              /**< things left by last read (part of whole message) */
};
