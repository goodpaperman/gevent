#include "EventHandler.h"
#include "log.h" 
#define LG writeLog

#ifndef WIN32
conn_key_t::conn_key_t (int f, unsigned short l, unsigned short r)
{
    fd = f; 
    lport = l; 
    rport = r; 
}

bool conn_key_t::operator< (struct conn_key_t const& rhs) const
{
    if (fd < rhs.fd)
        return true; 

    if (fd == rhs.fd) 
    {
        if (lport < rhs.lport)
            return true; 

        if (lport == rhs.lport)
        {
            if (rport < rhs.rport)
                return true; 
        }
    }

    return false; 
}

conn_key_t GEV_PER_HANDLE_DATA::key () const
{
    return conn_key_t(so, ntohs(laddr.sin_port), ntohs(raddr.sin_port)); 
}
#endif

GEV_PER_HANDLE_DATA::GEV_PER_HANDLE_DATA(SOCKET s, SOCKADDR_IN *l, SOCKADDR_IN *r)
{
    so = s;
    if (l)
        memcpy(&laddr, l, sizeof(SOCKADDR_IN));
    else
        memset(&laddr, 0, sizeof(SOCKADDR_IN));

    if (r)
        memcpy(&raddr, r, sizeof(SOCKADDR_IN));
    else
        memset(&raddr, 0, sizeof(SOCKADDR_IN));

    //LG("gphd %p ctor", this); 
}

GEV_PER_HANDLE_DATA::~GEV_PER_HANDLE_DATA()
{
    //LG("gphd %p dctor", this); 
    if (so != INVALID_SOCKET)
    {
        closesocket(so);
        so = INVALID_SOCKET;
    }
}

GEV_PER_IO_DATA::GEV_PER_IO_DATA(
#ifdef WIN32
        GEV_IOCP_OP o, 
#endif
        SOCKET s, int l)
{
    so = s;

#ifdef WIN32
    op = o;
    wsa.len = l;
    wsa.buf = new char[l + 1](); // tailing \0
    memset(&ol, 0, sizeof(ol));
#else
    len = l; 
    buf = new char[len+1](); // tailing \0
#endif
    //LG("gpid %p ctor", this);
}

GEV_PER_IO_DATA::~GEV_PER_IO_DATA()
{
    //LG("gpid %p dctor", this);
#ifdef WIN32
    delete []wsa.buf;
#else
    delete []buf; 
#endif
}

#ifdef WIN32
GEV_PER_TIMER_DATA::GEV_PER_TIMER_DATA(IEventBase *b, int due, int period, void *arg, HANDLE t)
    : GEV_PER_IO_DATA(OP_TIMEOUT, INVALID_SOCKET, 0)
    , timerque(t)
    , timer(NULL)
#else
GEV_PER_TIMER_DATA::GEV_PER_TIMER_DATA(IEventBase *b, int due, int period, void *arg, timer_t tid)
    : timer(tid)
#endif
    , base(b)
    , due_msec(due)
    , period_msec(period)
    , user_arg(arg)
    , cancelled(false)
{
    //LG("gptd %p ctor", this);
}

GEV_PER_TIMER_DATA::~GEV_PER_TIMER_DATA()
{
    //LG("gptd %p dctor", this);
    cancel (); 
}

void GEV_PER_TIMER_DATA::cancel ()
{
    if (cancelled)
    {
        LG("timer %p already cancelled", this);
        return;
    }

    cancelled = true; 
#ifdef WIN32
    if (timer != NULL && timerque != NULL)
    {
        // 3rd param:
        //    NULL: don't wait timer callback function
        //    INVALID_HANDLE_VALUE: wait all callback on this timer completed
        if (!DeleteTimerQueueTimer(timerque, timer, /*NULL*/INVALID_HANDLE_VALUE))
        {
            if (GetLastError() != ERROR_IO_PENDING)
                LG("delete timer queue timer %p failed, errno %d", this, GetLastError());
        }

        timer = NULL;
    }
    else
        LG("timer %p with invalid key %p, que %p", this, timer, timerque); 

    timerque = NULL; 
#else
    if (timer != NULL)
    {
        if (timer_delete (timer) < 0)
            LG("timer_delete %p failed, errno %d", this, errno); 
        else 
            LG("cancel timer %p", this); 

        // leave this field to do map later in map
        //timer = NULL; 
    }
#endif
}


GEventHandler::GEventHandler()
{
    LG("create handler %p", this);
}

GEventHandler::~GEventHandler()
{
    close(true);
    LG("destroy handler %p", this);
}

GEV_PER_HANDLE_DATA* GEventHandler::gphd()
{
    return m_gphd; 
}

GEV_PER_TIMER_DATA* GEventHandler::gptd()
{
    return m_gptd; 
}

SOCKET GEventHandler::fd()
{
    return m_so; 
}

void GEventHandler::close(bool terminal)
{
    LG("close handler, terminal %d, gphd %p, gptd %p, so %d, base %p", terminal, m_gphd, m_gptd, m_so, m_base); 
    if (m_gphd)
    {
        if (terminal)
        {
            delete m_gphd; 
            m_gphd = nullptr;
        }
        else
        {
            if (m_gphd->so != INVALID_SOCKET)
            {
                closesocket(m_gphd->so);
                m_gphd->so = INVALID_SOCKET;
            }
        }
    }

    if (m_gptd)
    {
        if (terminal)
        {
            delete m_gptd; 
            m_gptd = nullptr; 
        }
        else if (!m_gptd->cancelled)
        {
            // gptd will be deleted in map erase
            m_gptd->cancel (); 
            // keep this memory to delete later
            // m_gptd = nullptr;  
        }
    }

    m_so = INVALID_SOCKET;
    m_base = nullptr;
}

bool GEventHandler::reuse()
{
    // just delete us when close.
    return false; 
}

bool GEventHandler::auto_reconnect()
{
    return false; 
}

void GEventHandler::reset(GEV_PER_HANDLE_DATA *gphd, GEV_PER_TIMER_DATA *gptd, IEventBase *base)
{
    m_gphd = gphd; 
    m_gptd = gptd; 
    m_base = base;
    // for timer, no gphd provided
    m_so = gphd ? gphd->so : INVALID_SOCKET;
}

bool GEventHandler::connected()
{
    return m_so != INVALID_SOCKET;
}

void GEventHandler::disconnect()
{
    int fd = -1; 
    if (m_so != INVALID_SOCKET)
    {
        fd = m_so; 
        closesocket(m_so);
        m_so = INVALID_SOCKET;
    }

    if (m_gphd)
    {
        if (fd == -1)
            fd = m_gphd->so; 

        m_gphd->so = INVALID_SOCKET; 
    }

#if 0 // to prevent connection break event eaten by myself !
#  ifndef WIN32
    if (m_base && fd != -1)
    {
        int epfd = m_base->epfd(); 
        struct epoll_event ev; 
        ev.events = EPOLLIN; 
        ev.data.fd = fd; 
        int ret = epoll_ctl (epfd, EPOLL_CTL_DEL, fd, &ev); 
        if (ret < 0)
        {
            LG ("del %d from epoll failed, errno %d", fd, errno); 
        }
        else 
            LG("del %d from epoll before disconnect", fd); 
    }
#  endif
#endif
}

void GEventHandler::clear()
{
    m_so = INVALID_SOCKET;
    if (m_gphd)
        m_gphd->so = INVALID_SOCKET; 
}

int GEventHandler::send(char const* buf, int len)
{
    if (m_so == INVALID_SOCKET)
        return SOCKET_ERROR;

    //return ::send(m_so, buf, len, 0);
    int left = len, ret = 0; 
    char const* ptr = buf; 
    while (ptr < buf + len)
    {
        ret = ::send(m_so, ptr, left, 0); 
        if (ret <= 0)
        {
            LG("send failed, errno %d, sent %d, left %d", WSAGetLastError(), ptr - buf, left); 
            break;
        }

        left -= ret; 
        ptr += ret; 
    }

    return left == 0 ? len : -1; 
}

int GEventHandler::send(std::string const& str)
{
    return this->send(str.c_str(), str.length());
}

void GEventHandler::on_error(GEV_PER_HANDLE_DATA *gphd)
{
    cleanup(false);
}

void GEventHandler::cleanup(bool terminal)
{
}




void GJsonEventHandler::arg(void *param)
{
    // do nothing
}


void GJsonEventHandler::reset(GEV_PER_HANDLE_DATA *gphd, GEV_PER_TIMER_DATA *gptd, IEventBase *base)
{
    GEventHandler::reset(gphd, gptd, base); 
    LG("reset stub: %s", m_stub.c_str()); 
    m_stub.clear(); 
}

bool GJsonEventHandler::on_read(GEV_PER_IO_DATA *gpid)
{
    int ret = 0;
#ifdef IOCP_DUMP
    if (!m_stub.empty())
    {
        LG("using stub: %s", m_stub.c_str());
    }
#endif

#ifdef WIN32
    m_stub.append(gpid->wsa.buf, gpid->bytes); 
#else
#  ifdef HAS_ET
#    ifdef IOCP_DUMP
    LG("queue for lock, len %d", gpid->len); 
#    endif
    std::unique_lock <std::mutex> guard (m_mutex); 
#    ifdef IOCP_DUMP
    LG("enter lock"); 
#    endif
#  endif

    m_stub.append (gpid->buf, gpid->len); 
#endif

    //LG("recv from socket %d, length %d", bufferevent_getfd(m_bev), data.length());
    Json::Value root;
    Json::Reader reader;
    while (!m_stub.empty())
    {
        size_t pos = 0;
        std::string part;
        // multi-part message ? try split
        pos = m_stub.find("}{", 0);
        if (pos != std::string::npos)
        {
            part = m_stub.substr(0, pos + 1);
#ifdef IOCP_DUMP
            LG("multi-part message detected, split..");
            //LOG("multi-part message detected, split..");
#endif
        }
        else
            part = m_stub;

        if (!reader.parse(part, root, false))
        {
            LG("json parse error: %s\n%s", reader.getFormatedErrorMessages().c_str(), part.c_str());
            //LOG("json parse error: %s", part.c_str());
            break;
        }

        // remove processed data
        if (pos != std::string::npos)
            m_stub = m_stub.substr(pos + 1);
        else
            m_stub.clear();

#ifdef IOCP_DUMP
        LG("recv [%d]: %s", part.size(), part.c_str());
        //LOG("recv [%d]: %s", part.size(), part.c_str()); 
#endif
        on_read_msg(root);
    }

    return true; 
}

void GJsonEventHandler::cleanup(bool terminal)
{
#ifdef HAS_ET
#  ifdef IOCP_DUMP
    LG("queue for lock, reset"); 
#  endif
    std::unique_lock <std::mutex> guard (m_mutex); 
#  ifdef IOCP_DUMP
    LG("enter lock"); 
#  endif
    m_stub.clear (); 
#endif
}

bool GJsonEventHandler::on_timeout(GEV_PER_TIMER_DATA *gptd)
{
    //LG("event %d for timer %p, id = %d", type, m_ev, m_id);
    return true; 
}
