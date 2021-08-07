#include "EventHandler.h"
#include "log.h" 

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

    //writeLog("gphd %p ctor", this); 
}

GEV_PER_HANDLE_DATA::~GEV_PER_HANDLE_DATA()
{
    //writeLog("gphd %p dctor", this); 
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
    //writeLog("gpid %p ctor", this);
}

GEV_PER_IO_DATA::~GEV_PER_IO_DATA()
{
    //writeLog("gpid %p dctor", this);
#ifdef WIN32
    delete []wsa.buf;
#else
    delete []buf; 
#endif
}

#ifdef WIN32
GEV_PER_TIMER_DATA::GEV_PER_TIMER_DATA(IEventBase *b, int due, int period, void *arg, HANDLE t)
    : GEV_PER_IO_DATA(OP_TIMEOUT, INVALID_SOCKET, 0)
    , base(b)
    , due_msec(due)
    , period_msec(period)
    , user_arg(arg)
    , cancelled(false)
    , timerque(t)
    , timer(NULL)
#else
GEV_PER_TIMER_DATA::GEV_PER_TIMER_DATA(IEventBase *b, int due, int period, void *arg, timer_t tid)
    : base(b)
    , due_msec(due)
    , period_msec(period)
    , user_arg(arg)
    , cancelled(false)
    , timer(tid)
#endif
{
    //writeLog("gptd %p ctor", this);
}

GEV_PER_TIMER_DATA::~GEV_PER_TIMER_DATA()
{
    //writeLog("gptd %p dctor", this);
    cancel (); 
}

void GEV_PER_TIMER_DATA::cancel ()
{
    if (cancelled)
    {
        writeLog("timer %p already cancelled", this);
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
                writeLog("delete timer queue timer %p failed, errno %d", this, GetLastError());
        }

        timer = NULL;
    }
    else
        writeLog("timer %p with invalid key %p, que %p", this, timer, timerque); 

    timerque = NULL; 
#else
    if (timer != NULL)
    {
        if (timer_delete (timer) < 0)
            writeLog("timer_delete %p failed, errno %d", this, errno); 
        else 
            writeLog("cancel timer %p", this); 

        // leave this field to do map later in map
        //timer = NULL; 
    }
#endif
}


GEventHandler::GEventHandler()
{
    writeLog("create handler %p", this);
}

GEventHandler::~GEventHandler()
{
    close(true);
    writeLog("destroy handler %p", this);
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
    writeLog("close handler, terminal %d, gphd %p, gptd %p, so %d, base %p", terminal, m_gphd, m_gptd, m_so, m_base); 
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

        // keep m_so to ensure the handler can be destroyed later in iocp callbacks.
        //m_gphd->so = INVALID_SOCKET; 
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

bool GEventHandler::has_preread() const
{
    return false;
}

bool GEventHandler::has_prewrite() const
{
    return false; 
}

std::string GEventHandler::pre_read(char const* buf, int len)
{
    return std::string(buf, len); 
}

std::string GEventHandler::pre_write(char const* buf, int len)
{
    return std::string(buf, len); 
}

int GEventHandler::send(std::string const& msg)
{
    if (m_so == INVALID_SOCKET)
        return SOCKET_ERROR;

    //return ::send(m_so, buf, len, 0);

    if (has_prewrite())
    {
        std::string str = pre_write(msg.c_str(), msg.size());
        return send_raw(str.c_str(), str.size());
    }
    else
        return send_raw(msg.c_str(), msg.size()); 
}

int GEventHandler::send(char const* buf, int len)
{
    if (m_so == INVALID_SOCKET)
        return SOCKET_ERROR;

    //return ::send(m_so, buf, len, 0);

    if (has_prewrite())
    {
        std::string msg = pre_write(buf, len);
        return send_raw(msg.c_str(), msg.size());
    }
    else
        return send_raw(buf, len); 
}


int GEventHandler::send_raw(std::string const& str)
{
    return this->send_raw(str.c_str(), str.length());
}

int GEventHandler::send_raw(char const* buf, int len)
{
    if (m_so == INVALID_SOCKET)
        return SOCKET_ERROR;

    int left = len, ret = 0; 
    char const* ptr = buf; 
    while (ptr < buf + len)
    {
        ret = ::send(m_so, ptr, left, 0); 
        if (ret <= 0)
        {
            writeLog("send failed, errno %d, sent %d, left %d", WSAGetLastError(), ptr - buf, left); 
            break;
        }

        left -= ret; 
        ptr += ret; 
    }

    return left == 0 ? len : -1; 
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
    if (!m_stub.empty())
    {
        writeLog("reset stub: %s", m_stub.c_str());
        m_stub.clear();
    }
}

bool GJsonEventHandler::on_read(GEV_PER_IO_DATA *gpid)
{
#ifdef GEVENT_DUMP
    if (!m_stub.empty())
    {
        writeLog("using stub: %s", m_stub.c_str());
    }
#endif

#ifdef WIN32
    if (has_preread())
    {
        std::string msg = pre_read(gpid->wsa.buf, gpid->bytes);
        m_stub.append(msg);
    }
    else
        m_stub.append(gpid->wsa.buf, gpid->bytes);

#else
#  ifdef HAS_ET
#    ifdef GEVENT_DUMP
    writeLog("queue for lock, len %d", gpid->len); 
#    endif
    std::unique_lock <std::mutex> guard (m_mutex); 
#    ifdef GEVENT_DUMP
    writeLog("enter lock"); 
#    endif
#  endif

    if (has_preread ())
    {
        std::string msg = pre_read(gpid->buf, gpid->len);
        m_stub.append (msg); 
    }
    else 
        m_stub.append(gpid->buf, gpid->len); 

#endif

    //writeLog("recv from socket %d, length %d", bufferevent_getfd(m_bev), data.length());
    Json::Value root;
    Json::Reader reader(Json::Features::strictMode());
    while (!m_stub.empty())
    {
        size_t pos = 0;
        std::string part;
        // multi-part message ? try split
        pos = m_stub.find("}{", 0);
        if (pos != std::string::npos)
        {
            part = m_stub.substr(0, pos + 1);
#ifdef GEVENT_DUMP
            writeLog("multi-part message detected, split..");
            //LOG("multi-part message detected, split..");
#endif
        }
        else
            part = m_stub;

        if (!reader.parse(part, root, false))
        {
            writeLog("json parse error: %s\n%s", reader.getFormatedErrorMessages().c_str(), part.c_str());
            //LOG("json parse error: %s", part.c_str());
            break;
        }

        // remove processed data
        if (pos != std::string::npos)
            m_stub = m_stub.substr(pos + 1);
        else
            m_stub.clear();

#ifdef GEVENT_DUMP
        writeLog("recv [%d]: %s", part.size(), part.c_str());
        //LOG("recv [%d]: %s", part.size(), part.c_str()); 
#endif
        on_read_msg(root);
    }

    return true; 
}

void GJsonEventHandler::cleanup(bool terminal)
{
#ifdef HAS_ET
#  ifdef GEVENT_DUMP
    writeLog("queue for lock, reset"); 
#  endif
    std::unique_lock <std::mutex> guard (m_mutex); 
#  ifdef GEVENT_DUMP
    writeLog("enter lock"); 
#  endif
    m_stub.clear (); 
#endif
}

bool GJsonEventHandler::on_timeout(GEV_PER_TIMER_DATA *gptd)
{
    //writeLog("event %d for timer %p, id = %d", type, m_ev, m_id);
    return true; 
}
