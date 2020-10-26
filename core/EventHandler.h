#pragma once
#include "platform.h"

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


class GEventHandler; 
struct GEV_PER_TIMER_DATA; 
class IEventBase
{
public:
#ifdef WIN32
    virtual HANDLE iocp () const = 0; 
#else
    virtual int epfd () const = 0; 
#endif

    virtual void* timeout(int due_msec, int period_msec, void *arg, GEventHandler *exist_handler) = 0; 
    virtual bool cancel_timer(void* tid) = 0; 
    virtual bool post_timer(GEV_PER_TIMER_DATA *gptd) = 0; 
};


#ifdef WIN32
enum GEV_IOCP_OP
{
    OP_TIMEOUT = 1, 
    OP_ACCEPT,
    OP_RECV,
};
#else 
// the purpose of this key is to distinguish different connections with same fd !
// (when connection break and re-established soon, fd may not change but port will change)
struct conn_key_t
{
    int fd; 
    unsigned short lport; 
    unsigned short rport; 

    conn_key_t (int f, unsigned short l, unsigned short r); 
    bool operator< (struct conn_key_t const& rhs) const; 
}; 
#endif


struct GEV_PER_HANDLE_DATA
{
    SOCKET so;
    SOCKADDR_IN laddr;
    SOCKADDR_IN raddr;

#ifndef WIN32
    conn_key_t key () const; 
#endif

    GEV_PER_HANDLE_DATA(SOCKET s, SOCKADDR_IN *l, SOCKADDR_IN *r); 
    virtual ~GEV_PER_HANDLE_DATA(); 
};

struct GEV_PER_IO_DATA
{
    SOCKET so;
#ifdef WIN32
    GEV_IOCP_OP op;
    OVERLAPPED ol;
    WSABUF wsa;         // wsa.len is buffer length
    DWORD bytes;        // after compeleted, bytes trasnfered
#else
    char *buf; 
    int len; 
#endif

    GEV_PER_IO_DATA(
#ifdef WIN32
            GEV_IOCP_OP o, 
#endif
            SOCKET s, int l); 
    virtual ~GEV_PER_IO_DATA(); 
};

struct GEV_PER_TIMER_DATA
#ifdef WIN32
       : public GEV_PER_IO_DATA
#endif
{
    IEventBase *base; 
    int due_msec; 
    int period_msec; 
    void *user_arg;
    bool cancelled;
#ifdef WIN32
    HANDLE timerque; 
    HANDLE timer; 
#else
    timer_t timer; 
#endif

    GEV_PER_TIMER_DATA(IEventBase *base, int due, int period, void *arg
#ifdef WIN32
            , HANDLE tq);
#else
            , timer_t tid); 
#endif

    virtual ~GEV_PER_TIMER_DATA(); 
    void cancel (); 
};

class GEventHandler
{
public:
    GEventHandler();
    virtual ~GEventHandler();

    GEV_PER_HANDLE_DATA* gphd(); 
    GEV_PER_TIMER_DATA* gptd(); 
    bool connected();
    void disconnect(); 
    void clear(); 
    SOCKET fd(); 

    int send_raw(char const* buf, int len);
    int send_raw(std::string const& str); 
    int send(char const* buf, int len);
    int send(std::string const& str);
    
    virtual bool reuse();
    virtual bool auto_reconnect();
    virtual void arg(void *param) = 0;
    virtual void reset(GEV_PER_HANDLE_DATA *gphd, GEV_PER_TIMER_DATA *gptd, IEventBase *base);
    virtual bool on_read(GEV_PER_IO_DATA *gpid) = 0;
    virtual void on_error(GEV_PER_HANDLE_DATA *gphd); 
    // note when on_timeout called, handler's base may cleared by cancel_timer, use gptd->base instead if it is not null.
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd) = 0; 
    virtual void cleanup(bool terminal);
    void close(bool terminal);

protected:
    // think like this:
    // on_read (pre_read (msg)); 
    // send (pre_write (msg)); 
    //
    // to give user a chance to modify input before send/handle msg.
    virtual bool has_preread() const; 
    virtual bool has_prewrite() const; 
    virtual std::string pre_read (char const* buf, int len); 
    virtual std::string pre_write (char const* buf, int len); 

    GEV_PER_HANDLE_DATA *m_gphd = nullptr; 
    GEV_PER_TIMER_DATA *m_gptd = nullptr; 
    IEventBase *m_base = nullptr;
    // us so instead of m_gphd, 
    // as the later one may destroyed during using..
    SOCKET m_so;
};

// a common handler to process json protocol.
class GJsonEventHandler : public GEventHandler
{
public:
    //virtual void on_read();
    virtual void arg(void *param);
    virtual void reset(GEV_PER_HANDLE_DATA *gphd, GEV_PER_TIMER_DATA *gptd, IEventBase *base);
    virtual bool on_read(GEV_PER_IO_DATA *gpid);
    virtual void on_read_msg(Json::Value const& root) = 0;
    virtual bool on_timeout(GEV_PER_TIMER_DATA *gptd);
    virtual void cleanup(bool terminal);

    //virtual void on_event(short type);

protected:
    // protect m_stub to prevent multi-entry
#ifdef HAS_ET
    std::mutex m_mutex; 
#endif

    std::string m_stub;
};
