#include "EventBase.h"

#ifdef WIN32
#else
//#  define LG(format, args...) do {printf("%lu ", pthread_self ()); printf(format"\n", ##args);} while(0) 
//#  define LG(format, ...) printf(format"\n", ##__VA_ARGS__)
#  include <fcntl.h>
#  include <signal.h>
#  include <sstream>
#endif

#include "log.h" 
#define LG writeLog

#define MAX_ERROR_COUNT 32
#define TIMER_LOG_THRESHOLD (3 * 60 * 1000) // in miliseconds

GEventBase::GEventBase()
{
#ifdef WIN32
    init_socket();
#else
    m_pp[0] = m_pp[1] = -1; 
#endif
}


GEventBase::~GEventBase()
{
#ifdef WIN32
    WSACleanup();
#endif
}

// give it a definition to avoid link error : can not find GEventBaseWithAutoReconnect::create_handler
GEventHandler* GEventBase::create_handler()
{
    return nullptr;
}

bool GEventBase::post_timer(GDP_PER_TIMER_DATA *gptd)
{
#ifdef WIN32
    return post_completion (0, 0, &gptd->ol); 
#else
    return post_notify ('t', gptd); 
#endif
}

#ifdef WIN32
HANDLE GEventBase::iocp () const
{
    return m_iocp; 
}

int GEventBase::init_socket()
{
    WORD wVersionRequested;
    WSADATA wsaData;
    int err;

    /* Use the MAKEWORD(lowbyte, highbyte) macro declared in Windef.h */
    wVersionRequested = MAKEWORD(2, 2);
    err = WSAStartup(wVersionRequested, &wsaData);
    if (err != 0) {
        /* Tell the user that we could not find a usable */
        /* Winsock DLL.                                  */
        LG("WSAStartup failed with error: %d", err);
        return -1;
    }

    /* Confirm that the WinSock DLL supports 2.2.*/
    /* Note that if the DLL supports versions greater    */
    /* than 2.2 in addition to 2.2, it will still return */
    /* 2.2 in wVersion since that is the version we      */
    /* requested.                                        */

    if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
        /* Tell the user that we could not find a usable */
        /* WinSock DLL.                                  */
        LG("Could not find a usable version of Winsock.dll");
        WSACleanup();
        return -1;
    }
    else
        LG("The Winsock 2.2 dll was found okay");

    return 0;
}

bool GEventBase::issue_accept()
{
    SOCKET acceptor = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
    if (INVALID_SOCKET == acceptor)
    {
        LG("create acceptor socket failed, errno %d", WSAGetLastError());
        return false; 
    }

    DWORD bytes = 0;
    // api needs 16 bytes extra space
    int addr_len = sizeof(SOCKADDR_IN) + 16;
    GDP_PER_IO_DATA *gpid = new GDP_PER_IO_DATA(OP_ACCEPT, acceptor, 2 * addr_len);
    bool ret = (*m_acceptex)(m_listener, acceptor, gpid->wsa.buf, 0/*gpid->wsa.len*/, // just receive addresses 
        addr_len, addr_len, &bytes, &gpid->ol);

    if (ret)
    {
        LG("AcceptEx successed immediately, fd = %d, do nothing to wait iocp callbacks", acceptor);
        //ret = do_accept(gpid);
    }
    else if (WSAGetLastError() != WSA_IO_PENDING)
    {
        LG("accept asynchronizely failed, errno %d", WSAGetLastError());
        closesocket(acceptor);
        delete gpid;
        return false;
    }
    else
    {
        LG("issue next accept ok"); 
        ret = true;
    }

    return ret; 
}

bool GEventBase::issue_read(GDP_PER_HANDLE_DATA *gphd)
{
    DWORD flags = 0; 
    GDP_PER_IO_DATA* gpid = new GDP_PER_IO_DATA(OP_RECV, gphd->so, m_blksize);
    int ret = WSARecv(gpid->so, &gpid->wsa, 1, &gpid->bytes, &flags, &gpid->ol, NULL); 
    if (ret == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            LG("issue next read on socket failed, errno %d", WSAGetLastError());
            delete gpid;
            // leave gphd handled by upper layer.
            //delete gphd; 
            return false;
        }

#  ifdef IOCP_DUMP
        LG("issue next read on %d ok", gphd->so);
#  endif
    }
    else
    {
#  ifdef IOCP_DUMP
        // immediately recevied ?
        LG("recevie completed immediately after issue read, do nothing to wait iocp callbacks");
#  endif
        //if (gpid->bytes == 0)
        //{
        //    // will delete gphd automatically
        //    do_error(gphd);
        //}
        //else
        //{
        //    // on error, do_recv automatically remove handler & delete gphd
        //    // so here ignore result and return true always..
        //    ret = do_recv(gphd, gpid);
        //}
    }
    
    return true; 
}

bool GEventBase::post_completion(DWORD bytes, ULONG_PTR key, LPOVERLAPPED ol)
{
    return PostQueuedCompletionStatus(m_iocp, bytes, key, ol); 
}

static void CALLBACK timeout_proc(LPVOID param, BOOLEAN unused)
{
    GDP_PER_TIMER_DATA *gptd = (GDP_PER_TIMER_DATA *)param; 
    if (gptd == NULL)
        return; 

    IEventBase *base = gptd->base; 
    if (base == NULL)
        return; 

    // do notify
    if (!base->post_timer(gptd))
    {
        LG("post timer notify failed, timer %p, errno %d", gptd, GetLastError());
        if (gptd->period_msec == 0)
            // non-periodically timer, destroy
            delete gptd;
    }
#  ifdef IOCP_DUMP
    // prevent too frequency logs, 
    // only log once timer or periodical timer that equal or more than 3 minutes.
    else if (gptd->period_msec == 0 || gptd->period_msec >= TIMER_LOG_THRESHOLD)
        LG("timer %p timeout, post notify..", gptd);
#  endif
}

#else // WIN32

static void setnonblocking (int fd)
{
    int opts = fcntl (fd, F_GETFL); 
    if (opts < 0)
    {
        LG("get sock %d flag failed, errno %d", fd, errno); 
        return; 
    }

    opts |= O_NONBLOCK; 
    if (fcntl (fd, F_SETFL, opts) < 0)
        LG("set sock %d flag failed, flag 0x%08x, errno %d", fd, opts, errno); 
}

static void setreuseaddr (int fd)
{
    int opt = 1;  
    if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof (opt)) < 0)
        LG("setsockopt %d failed, errno %d", fd, errno); 
}

static conn_key_t get_key_from_fd (int fd, bool verbose)
{
    // getsockname doesn't work if fd is closed by other thread in system; 
    // getpeername dones't work if connection breaks, 
    // so we provide a method find_by_fd to do this...
    SOCKADDR_IN local, remote;
    socklen_t locallen, remotelen;
    locallen = remotelen = sizeof(SOCKADDR_IN);
    if (getsockname(fd, (sockaddr *)&local, &locallen) == SOCKET_ERROR)
    {
        LG("get socket local address failed, errno %d", WSAGetLastError());
        memset(&local, 0, sizeof(local));
        locallen = 0;
        // not fatal
    }
    else if (verbose)
        LG("socket local address %s:%d", inet_ntoa(local.sin_addr), ntohs(local.sin_port));

    if (getpeername(fd, (sockaddr *)&remote, &remotelen) == SOCKET_ERROR)
    {
        LG("get socket remote address failed, errno %d", WSAGetLastError());
        memset(&remote, 0, sizeof(remote));
        remotelen = 0;
        // not fatal
    }
    else if (verbose)
        LG("socket remote address %s:%d", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

    return conn_key_t (fd, ntohs(local.sin_port), ntohs(remote.sin_port)); 
}

int GEventBase::epfd () const
{
    return m_ep; 
}

bool GEventBase::init_pipe()
{
    // create self connected pipe and add into epoll to do notify
    if (pipe (m_pp) < 0)
    {
        LG("create pipe failed, errno %d", errno); 
        return false; 
    }
    else 
        LG("create pipe, in %d, out %d", m_pp[0], m_pp[1]); 

    struct epoll_event ev; 
    ev.events = EPOLLIN; // no EPOLLET
    ev.data.fd = m_pp[0]; 
    if (epoll_ctl (m_ep, EPOLL_CTL_ADD, m_pp[0], &ev) < 0)
    {
        LG("add notify pipe into epoll failed, errno %d", errno); 
        return false; 
    }
    else 
        LG("add notify pipe into epoll ok"); 

    return true; 
}

void GEventBase::close_pipe()
{
    if (m_pp[0] != -1)
        close (m_pp[0]); 

    if (m_pp[1] != -1)
        close (m_pp[1]); 

    m_pp[0] = m_pp[1] = -1; 
}

bool GEventBase::post_notify(char ch, void *ptr)
{
    if (m_pp[1] == -1)
        return false; 

    char buf[sizeof(char) + sizeof (void*)] = { 0 }; 
    int n = 0; 
    buf[n++] = ch; 
    if (ptr)
    {
        memcpy (&buf[1], &ptr, sizeof (void*)); 
        n += sizeof (void *); 
    }

    int ret = write (m_pp[1], buf, n); 
    if (ret <= 0)
    {
        LG("send notify to epoll failed, ret %d, errno %d", ret, errno);
        return false; 
    }

    return true; 
}

GEventHandler* GEventBase::find_by_key(conn_key_t key, bool erase)
{
    GEventHandler *h = nullptr;
    std::lock_guard<std::mutex> guard(m_mutex); 
    auto it = m_map.find(key); 
    if (it != m_map.end())
    {
        h = it->second; 
        if (erase)
            m_map.erase (it); 
    }

    return h; 
}

GEventHandler* GEventBase::find_by_fd(int fd, conn_key_t &key, bool erase)
{
    GEventHandler *h = nullptr;
    std::lock_guard<std::mutex> guard(m_mutex); 
    for (auto it = m_map.begin (); it != m_map.end (); ++ it)
    {
        if (it->first.fd == fd)
        {
            h = it->second; 
            // query out key too to prevent handler has closed the fd and set it to -1
            key = it->first; 
            if (erase)
                m_map.erase (it); 

            break; 
        }
    }

    return h; 
}

void sig_timer (int sig, siginfo_t *si, void *uc)
{
    LG("%d caught", sig); 
    GDP_PER_TIMER_DATA *gptd = (GDP_PER_TIMER_DATA *)si->si_value.sival_ptr; 
    if (gptd == NULL)
        return; 

    IEventBase *base = gptd->base; 
    if (base == NULL)
        return; 

    if (!base->post_timer (gptd))
    {
        LG("post timer notify failed, timer %p, errno %d", gptd, GetLastError());
        if (gptd->period_msec == 0)
            // non-periodically timer, destroy
            delete gptd;
    }
#  ifdef IOCP_DUMP
    // prevent too frequency logs, 
    // only log once timer or periodical timer that equal or more than 3 minutes.
    else if (gptd->period_msec == 0 || gptd->period_msec >= TIMER_LOG_THRESHOLD)
        LG("timer %p timeout, post notify..", gptd);
#  endif
}

#  ifdef HAS_SIGTHR
void GEventBase::sig_proc()
{
    int signo = 0; 
    siginfo_t si; 

    sigset_t mask; 
    sigaddset (&mask, m_tsig); 
    sigaddset (&mask, SIGINT); 
    
    LG("signal thread running"); 
    while (m_running) {
        signo = sigwaitinfo (&mask, &si); 
        if (signo < 0) { 
            LG ("sigwaitinfo failed, errno %d", errno); 
            break; 
        }

        if (signo == m_tsig)
        {
            // time out signal
            LG ("wait timer signal %d", m_tsig); 
            sig_timer (signo, &si, NULL); 
            continue; 
        }
        else if (signo == SIGINT)
            LG ("quit detected"); 
        else
            LG ("unexpected signal %d\n", signo); 

#  if 0
        switch (signo) { 
            case SIGINT:
            case SIGTERM:
                LG ("quit detected, signal %d", signo); 
                break; 
            default:
                LG ("unexpected signal %d", signo); 
                break; 
        }
#  endif
    }

    LG("signal thread exiting"); 
}

pthread_t get_std_thread_id (std::thread *pthr)
{
    std::stringstream sin;
    sin << pthr->get_id ();
    return stoull(sin.str());
}
#  endif

#endif

bool GEventBase::on_accept(GDP_PER_HANDLE_DATA *gphd)
{
    //LG("accept a client %d", gphd->so);
    GEventHandler *h = create_handler ();
    h->reset(gphd, nullptr, this);

#ifdef WIN32
    std::lock_guard<std::mutex> guard(m_mutex);
    //m_map.insert(std::make_pair(gphd, h));
    m_map[gphd] = h; 
#else
    //m_map.insert(std::make_pair(gphd->key(), h));
    m_map[gphd->key()] = h; 
#endif
    return true; 
}


bool GEventBase::on_read(GEventHandler *h, GDP_PER_IO_DATA *gpid)
{
    return h->on_read(gpid); 
}


void GEventBase::on_error(GEventHandler *h)
{
    h->on_error(h->gphd ());
    h->close(true);
    if (!h->reuse())
        delete h;
}

bool GEventBase::init(int thr_num, int blksize
#ifndef WIN32
                                , int timer_sig
#endif
                                )
{
    m_running = true; 
    m_blksize = blksize; 
    if (thr_num < 0)
    {
#ifdef WIN32
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        thr_num = max ((int)si.dwNumberOfProcessors, -thr_num);  
#else
        int ncore = sysconf(_SC_NPROCESSORS_ONLN); 
        LG("ncore %d", ncore); 
        thr_num = std::max (ncore, -thr_num); 
#endif
    }

    m_thrnum = thr_num; 
#ifdef WIN32
    /* for 0, use as many thread as iocp can support */
    m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, thr_num);
    if (m_iocp == NULL)
    {
        LG("create io completion port failed, errno %d", GetLastError()); 
        return false; 
    }

    m_timerque = CreateTimerQueue(); 
    if (m_timerque == NULL)
    {
        LG("create timer queue failed, errno %d", GetLastError()); 
        //return false; 
        // not fatal as we can use default timer queue...
    }

#else // WIN32
    m_ep = epoll_create (1/*just a hint*/); 
    if (m_ep < 0)
    {
        LG("create epoll instance failed, errno %d", errno); 
        return false; 
    }
    else 
        LG("create epoll fd %d", m_ep); 

    if (!init_pipe())
        return false; 

    struct sigaction act; 
    // install SIGUSR1 handler to do timer callback
    act.sa_sigaction = sig_timer; 
    act.sa_flags = SA_RESTART | SA_SIGINFO; 
    sigemptyset (&act.sa_mask); 
    if (sigaction (timer_sig, &act, NULL) < 0)
    {
        LG("install sig %d for timer failed, errno %d", timer_sig, errno); 
        return false; 
    }
    else
        LG("install sig %d for timer ok", timer_sig); 

    m_tsig = timer_sig; 

    // installl SIGPIPE handler to avoid exit on broken pipe/socket
    act.sa_handler = SIG_IGN; 
    act.sa_flags = SA_RESTART; 
    sigemptyset (&act.sa_mask); 
    if (sigaction (SIGPIPE, &act, NULL) < 0)
    {
        LG("ignore SIGPIPE failed, errno %d", errno); 
        return false; 
    }
    else
        LG("ignore SIGPIPE ok"); 

#  ifdef HAS_SIGTHR
    sigset_t mask; 
    // ignore all signal for worker thread
    // note: this step must set before spawning worker threads.
    sigaddset(&mask, m_tsig); 
    if (pthread_sigmask (SIG_BLOCK, &mask, NULL) != 0)
    {
        LG("block all signal for work thread failed, errno %d", errno); 
        return false; 
    }

    m_sigthr = new std::thread(&GEventBase::sig_proc, this); 
#  endif
#endif

    std::thread *thr = nullptr; 
    for (int i = 0; i < thr_num; ++i)
    {
        thr = new std::thread(&GEventBase::run, this);
        m_grp.add_thread(thr);
    }

    LG("start %d threads to run event loop", thr_num); 
    return true; 
}

bool GEventBase::listen(unsigned short port, unsigned short backup)
{
    int ret = 0;

    do
    {
#ifdef WIN32
        m_listener = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
        m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP); 
#endif
        if (m_listener == INVALID_SOCKET)
        {
            LG("create listener socket failed, errno %d", WSAGetLastError());
            break; 
        }

        // Associate SOCKET with IOCP  
        LG("create listener %d", m_listener); 
#ifdef WIN32
        if (CreateIoCompletionPort((HANDLE)m_listener, m_iocp, 0, 0) == NULL)
        {
            LG("associate listener to iocp failed, errno %d", GetLastError());
            break; 
        }

        LG("associate listener to iocp ok"); 
#endif

        SOCKADDR_IN addr;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        if (::bind(m_listener, (SOCKADDR*)&addr, sizeof(SOCKADDR)) == SOCKET_ERROR)
        {
            LG("bind listener to local port %d failed, errno %d", port, WSAGetLastError()); 
            break; 
        }

#ifndef WIN32
        setnonblocking (m_listener); 
        setreuseaddr (m_listener); 
#endif

        if (::listen(m_listener, backup) == SOCKET_ERROR)
        {
            LG("start listen on port %d with backup %d failed, errno %d", port, backup, WSAGetLastError()); 
            break;
        }

#ifdef WIN32
        int n = 0; 
        DWORD bytes = 0;
        // api needs 16 bytes extra space
        int addr_len = sizeof(SOCKADDR_IN) + 16; 
        //LPFN_ACCEPTEX lpfnAcceptEx = NULL;//AcceptEx
        //LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs = NULL; // GetAcceptExSockAddrs
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        GUID guidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS;
        if (WSAIoctl(m_listener, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &m_acceptex,
            sizeof(m_acceptex), &bytes, NULL, NULL) == SOCKET_ERROR)
        {
            LG("get function ptr for AcceptEx failed, errno %d", WSAGetLastError());
            break;
        }

        if (WSAIoctl(m_listener, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidGetAcceptExSockAddrs,
            sizeof(guidGetAcceptExSockAddrs), &m_getacceptexsockaddrs, sizeof(m_getacceptexsockaddrs),
            &bytes, NULL, NULL) == SOCKET_ERROR)
        {
            LG("get function ptr for GetAcceptExSockAddrs failed, errno %d", WSAGetLastError());
            // not fatal
            m_getacceptexsockaddrs = nullptr;
        }

        for (int i = 0; i < backup; ++i)
        {
            if (issue_accept())
                n++; 
        }

        if (n == 0)
        {
            LG("no acceptor prepared, fatal error"); 
            break;
        }

        LG("prepare %d acceptors for listener", n); 
#else
        struct epoll_event ev; 
        ev.events = EPOLLIN; 
#  ifdef HAS_ET
        ev.events |= EPOLLET; 
#  endif
        ev.data.fd = m_listener; 
        if (epoll_ctl (m_ep, EPOLL_CTL_ADD, m_listener, &ev) < 0)
        {
            LG("epoll ctl %d failed, errno %d", m_listener, errno); 
            break; 
        }

        LG("associate listener to epoll ok"); 
#endif

        return true; 
    } while (0); 

    if (m_listener != INVALID_SOCKET)
    {
        closesocket(m_listener);
        m_listener = INVALID_SOCKET;
    }

    return false; 
}


GEventHandler* GEventBase::connect(unsigned short port, GEventHandler *exist_handler)
{
    int ret = 0;
    GDP_PER_HANDLE_DATA* gphd = nullptr;
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1"); //INADDR_ANY;


    do
    {
#if 0
        SOCKET connector = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (connector == INVALID_SOCKET)
        {
            LG("create socket to do connect failed, errno %d", WSAGetLastError());
            return NULL;
        }

        ret = WSAConnect(connector, (const sockaddr *)&server_addr, sizeof(server_addr), NULL, NULL, NULL, NULL);
        if (ret == SOCKET_ERROR)
        {
            if (WSAGetLastError() != WSAEWOULDBLOCK)
            {
                LG("ansychronous connect to server failed, errno %d", WSAGetLastError());
                return NULL;
            }
        }
#else 
        SOCKET fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == INVALID_SOCKET)
        {
            LG("create socket failed, errno %d", WSAGetLastError());
            break;
        }

        ret = ::connect(fd, (const struct sockaddr *)&server_addr, sizeof(server_addr));
        if (ret == SOCKET_ERROR)
        {
            LG("connect to server failed, errno %d", WSAGetLastError());
            break;
        }
#endif

        SOCKADDR_IN local, remote;
        socklen_t locallen, remotelen;
        locallen = remotelen = sizeof(SOCKADDR_IN);
        if (getsockname(fd, (sockaddr *)&local, &locallen) == SOCKET_ERROR)
        {
            LG("get connector local address failed, errno %d", WSAGetLastError());
            //memset(&local, 0, sizeof(local));
            locallen = 0;
            // not fatal
        }
        else
            LG("connector local address %s:%d", inet_ntoa(local.sin_addr), ntohs(local.sin_port));

        if (getpeername(fd, (sockaddr *)&remote, &remotelen) == SOCKET_ERROR)
        {
            LG("get connector remote address failed, errno %d", WSAGetLastError());
            //memset(&remote, 0, sizeof(remote));
            remotelen = 0;
            // not fatal
        }
        else
            LG("connector remote address %s:%d", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

        gphd = new GDP_PER_HANDLE_DATA(fd, locallen > 0 ? &local : 0, remotelen > 0 ? &remote : 0);

#ifdef WIN32
#  if 0
        // if needed ?
        DWORD yes = 1;
        ioctlsocket(fd, FIONBIO, &yes);
#  endif 

        if (CreateIoCompletionPort((HANDLE)fd, m_iocp, (ULONG_PTR)gphd, 0) == NULL)
        {
            LG("associate newly connected socket to iocp failed, errno %d", GetLastError());
            break;
        }

        LG("bind newly connected socket %d to iocp ok", fd);
        // add lock before issue_read to make sure on_read use the handler after we add it!
        std::lock_guard<std::mutex> guard(m_mutex);
        if (!issue_read(gphd))
            break;

#else
        setnonblocking (fd); 

        // reuse this event, we don't use it later..
        struct epoll_event ev; 
        ev.events = EPOLLIN; 
#  ifdef HAS_ET
        ev.events |= EPOLLET; 
#  endif
        ev.data.fd = fd; 
        if (epoll_ctl (m_ep, EPOLL_CTL_ADD, fd, &ev) < 0)
        {
            LG("epoll ctl %d failed, errno %d", fd, errno); 
            break; 
        }

        LG("bind newly connected socket %d to epoll ok", fd);
        std::lock_guard<std::mutex> guard(m_mutex);
#endif 

        GEventHandler *h = nullptr; 
        if (exist_handler == nullptr)
            h = create_handler ();
        else
            h = exist_handler; 

        h->reset(gphd, nullptr, this);

#ifdef WIN32
        //std::lock_guard<std::mutex> guard(m_mutex);
        //m_map.insert(std::make_pair(gphd, h));
        m_map[gphd] = h; 
#else
        conn_key_t key = gphd->key (); 
        auto res = m_map.insert(std::make_pair(key, h));
        if (!res.second)
        {
            LG("insert fd (%d.%d.%d) failed, old fd still in the pill (%d.%d.%d), try replace it ...", key.fd, key.lport, key.rport, res.first->first.fd, res.first->first.lport, res.first->first.rport); 
            GEventHandler *oldh = res.first->second; 
            if (oldh != h)
            {
                // for reused handler, may the same object, so...
                res.first->second = h; 

                // clear old handler
                oldh->cleanup (true); 

                // close old fd will close new fd (old fd == new fd)
                // so clear fd before close...
                oldh->clear (); 
                oldh->close (true); 
                if (!oldh->reuse ())
                    delete oldh;
            }
        }
#endif
        return h;
    } while (0);

    if (gphd)
        delete gphd;

    return nullptr;
}


#ifdef WIN32
bool GEventBase::do_accept(GDP_PER_IO_DATA *gpid)
{
    GDP_PER_HANDLE_DATA* gphd = nullptr; 

    do
    {
        int addr_len = sizeof(SOCKADDR_IN) + 16;
        SOCKADDR_IN* remote = NULL, *local = NULL;
        int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);
        if (m_getacceptexsockaddrs)
            (*m_getacceptexsockaddrs)(gpid->wsa.buf, 0/* no user data */, addr_len, addr_len, (LPSOCKADDR*)&local, &localLen, (LPSOCKADDR*)&remote, &remoteLen);

        if (remote || local)
        {
            LG("accept a client %d", gpid->so);
            if (remote)
                LG("    remote addr %s:%d", inet_ntoa(remote->sin_addr), ntohs(remote->sin_port));
            if (local)
                LG("    local addr %s:%d", inet_ntoa(local->sin_addr), ntohs(local->sin_port)); 
        }
        else
            LG("accept a client %d, address can not got", gpid->so);

        // copy socket settings for AcceptEx (accept does not need to do this)
        if (setsockopt(gpid->so, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&m_listener, sizeof(m_listener)) == SOCKET_ERROR)
        {
            LG("let new accepted socket inherit listen socket failed, some setting on listen socket may not copy to new connection, warning..");
            // note fatal
        }

        issue_accept(); // ignore error; 
        gphd = new GDP_PER_HANDLE_DATA(gpid->so, local, remote);
        if (CreateIoCompletionPort((HANDLE)gpid->so, m_iocp, (ULONG_PTR)gphd, 0) == NULL)
        {
            LG("associate newly accepted socket to iocp failed, errno %d", GetLastError());
            break; 
        }

        LG("bind newly accepted socket %d to iocp ok", gpid->so); 
        // insert handler into map before issue any read, 
        // to avoid read callback can NOT find handler error !
        if (!on_accept(gphd))
            break;

        if (!issue_read(gphd))
            break;

        delete gpid; // unused
        return true; 
    } while (0);

    if (gpid)
        delete gpid; 

    if (gphd)
        delete gphd;

    return false; 
}

#else // WIN32

bool GEventBase::do_accept(int listener)
{
    int ret = 0; 
    int accept_cnt = 0; 
    GDP_PER_HANDLE_DATA* gphd = nullptr; 

    while(1)
    {
        // note listener fd is set non-blocking, ET trigger, 
        // so we should accept as many client as we can once.
        SOCKADDR_IN remote, local;
        socklen_t addr_len = sizeof(SOCKADDR_IN);
        int fd = accept (listener, (struct sockaddr *)&remote, &addr_len); 
        if (fd < 0)
        {
            if (errno != EAGAIN)
                LG("accept failed, turn %d, errno %d", accept_cnt + 1, errno); 

            break; 
        }

        LG("accept a client %d", fd);
        LG("    remote addr %s:%d", inet_ntoa(remote.sin_addr), ntohs(remote.sin_port));

        addr_len = sizeof(SOCKADDR_IN);
        ret = getsockname (fd, (struct sockaddr *)&local, &addr_len);
        if (ret == 0)
        {
            LG("    local addr %s:%d", inet_ntoa(local.sin_addr), ntohs(local.sin_port)); 
        }

        setnonblocking (fd); 
        gphd = new GDP_PER_HANDLE_DATA(fd, ret == 0 ? &local : 0, &remote);

        struct epoll_event ev; 
        ev.events = EPOLLIN; 
#  ifdef HAS_ET
        ev.events |= EPOLLET; 
#  endif
        ev.data.fd = fd; 
        // add lock before fd added into epoll to prevent 
        // io event arrives earlier than we insert handler into map
        std::lock_guard<std::mutex> guard(m_mutex);
        if (epoll_ctl (m_ep, EPOLL_CTL_ADD, fd, &ev) < 0)
        {
            LG("epoll ctl %d failed, errno %d", fd, errno); 
            break; 
        }

        LG("associate fd to epoll ok"); 
        if (!on_accept(gphd))
            break;

        accept_cnt ++; 
        gphd = nullptr; 
    }

    if (gphd)
        delete gphd;

    LG("total accepted %d client", accept_cnt); 
    return accept_cnt > 0 ? true : false; 
}
#endif

#ifdef WIN32
bool GEventBase::do_recv(GDP_PER_HANDLE_DATA *gphd, GDP_PER_IO_DATA *gpid)
{
    do
    {
        if (gpid == NULL)
        {
            LG("invalid data per io, ignore read complete event");
            break; 
        }

#  ifdef IOCP_DUMP
        LG("read %d on socket %d", gpid->bytes, gpid->so);
#  endif
        if (gphd == NULL)
        {
            LG("invalid data per handle, using fd %d from data per IO", gpid->so);
            // not fatal
        }
        else if (gpid->so != gphd->so)
        {
            LG("per handle fd %d != per io fd %d, using last one", gphd->so, gpid->so);
            gphd->so = gpid->so;
        }

        GEventHandler *h = NULL;

        {
            std::lock_guard<std::mutex> guard(m_mutex); 
            auto it = m_map.find(gphd); 
            if (it != m_map.end())
                h = it->second; 
        }

        if (h == NULL)
        {
            LG("can not find handler for socket %d on read", gphd->so); 
            return false; 
        }

        if (!on_read(h, gpid))
            break; 

        if (!issue_read(gphd))
            break; 

        delete gpid;  // unused
        return true; 
    } while (0); 

    if (gpid)
        delete gpid;

    if (gphd)
    {
        // will delete gphd automatically
        do_error(gphd); 
    }

    return false; 
}

#else // WIN32

bool GEventBase::do_recv(conn_key_t key)
{
    GEventHandler *h = find_by_key (key, false); 
    if (h == NULL)
    {
        LG("can not find handler for socket (%d.%d.%d) on read, try find by fd", key.fd, key.lport, key.rport); 
        // will update key, too 
        h = find_by_fd (key.fd, key, false); 
        if (h == NULL)
        {
            LG("can not find handler"); 
            return false; 
        }

        LG("find a handler with fd (%d.%d.%d)", key.fd, key.lport, key.rport); 
    }

    int ret = 0; 
    int total = 0; 
    while (1)
    {
        // do the dirty work
        GDP_PER_IO_DATA gpid(key.fd, m_blksize);
        ret = read (key.fd, gpid.buf, gpid.len); 
        if (ret < 0)
        {
            if (errno != EAGAIN)
                LG("read %d failed, ret %d, errno %d", key.fd, ret, errno); 

            break;
        }
        else if (ret == 0)
        {
            // no more data
            LG("connection break detected, fd %d", key.fd); 
            break;
        }

#  ifdef IOCP_DUMP
        LG("read %d on socket %d", ret, key.fd);
#  endif

        gpid.len = ret; 
        if (!on_read(h, &gpid))
            break; 

        total += ret; 
    } 

    // has complete some read...
    if (total > 0)
        return true; 

    if (h)
    {
        // do error will earse hander from map and pass heavy work to on_error, 
        // so here don't call on_error directly
        //on_error(h); 
        // will delete h automatically
        do_error (key); 
    }

    return false; 
}
#endif

#ifdef WIN32
void GEventBase::do_error(GDP_PER_HANDLE_DATA *gphd)
{
    LG("got error %d on socket %d", WSAGetLastError (), gphd->so); 
    GEventHandler *h = NULL;

    {
        std::lock_guard<std::mutex> guard(m_mutex);
        auto it = m_map.find(gphd);
        if (it != m_map.end())
        {
            h = it->second;
            m_map.erase(it); 
        }
    }

    if (h == NULL)
    {
        LG("can not find handler for socket %d on error", gphd->so);
        return;
    }

    on_error(h); 
    // on_error will delete gphd
    //delete gphd; 
}

#else // WIN32

void GEventBase::do_error(conn_key_t key)
{
    LG("got error on socket (%d.%d.%d)", key.fd, key.lport, key.rport); 
    GEventHandler *h = find_by_key (key, true); 
    if (h == NULL)
    {
        LG("can not find handler for socket (%d.%d.%d) on error, try find by fd", key.fd, key.lport, key.rport); 
        h = find_by_fd (key.fd, key, true); 
        if (h == NULL)
        {
            LG("can not find handler"); 
            return; 
        }

        key = h->gphd ()->key (); 
        LG("find a handler with fd (%d.%d.%d)", key.fd, key.lport, key.rport); 
    }

    on_error(h); 
}
#endif



void* GEventBase::timeout(int due_msec, int period_msec, void *arg, GEventHandler *exist_handler)
{
    // setup timer
    int ret = 0; 
    if (due_msec == 0 && period_msec == 0)
    {
        LG("both timeout can not be zero!"); 
        return nullptr;  
    }

#ifdef WIN32
    GDP_PER_TIMER_DATA *gptd = new GDP_PER_TIMER_DATA(this, due_msec, period_msec, arg, m_timerque);
    // may lost timer event ?
    std::lock_guard<std::mutex> guard(m_tlock);
    // use WT_EXECUTEINTIMERTHREAD to tell system we don't want other thread to be created..
    ret = CreateTimerQueueTimer(&gptd->timer, m_timerque, timeout_proc, gptd, due_msec, period_msec, WT_EXECUTEINTIMERTHREAD);
    if (!ret)
    {
        LG("create timer queue timer failed, errno %d", GetLastError()); 
        delete gptd; 
        return nullptr; 
    }

    LG("create timer %p", gptd); 
    GEventHandler *h; 
    if (exist_handler == NULL)
        h = create_handler ();
    else
        h = exist_handler; 

    h->reset(nullptr, gptd, this);
    // add lock before timer take effect (short timers) to make sure on_timeout use the handler after we add it!
    //std::lock_guard<std::mutex> guard(m_tlock);
    //m_tmap.insert(std::make_pair(gptd, h));
    m_tmap[gptd] = h; 
    return gptd;

#else // WIN32

    GDP_PER_TIMER_DATA *gptd = new GDP_PER_TIMER_DATA(this, due_msec, period_msec, arg, NULL); 

    timer_t timer = NULL; 
    struct sigevent sev; 
    sev.sigev_notify = SIGEV_SIGNAL; 
    sev.sigev_signo = m_tsig; 
    sev.sigev_value.sival_ptr = gptd; 
    ret = timer_create (CLOCK_MONOTONIC, &sev, &timer); 
    if (ret < 0)
    {
        LG("create timer failed, errno %d", errno); 
        delete gptd; 
        return nullptr; 
    }


    gptd->timer = timer; 
    LG("create timer %p", gptd); 

    GEventHandler *h; 
    if (exist_handler == NULL)
        h = create_handler ();
    else
        h = exist_handler; 

    // lock before timer take effect to avoid handler missing..
    std::lock_guard <std::mutex> guard (m_tlock); 

    // start the timer
    struct itimerspec its; 
    its.it_value.tv_sec = due_msec / 1000; 
    its.it_value.tv_nsec = due_msec % 1000 * 1000000; 
    its.it_interval.tv_sec = period_msec / 1000; 
    its.it_interval.tv_nsec = period_msec % 1000 * 1000000; 
    ret = timer_settime (timer, 0, &its, NULL); 
    if (ret < 0)
    {
        LG("start timer %p failed, errno %d", gptd, errno); 
        delete gptd; 
        return nullptr; 
    }

    h->reset(nullptr, gptd, this);
    //m_tmap.insert(std::make_pair(gptd, h));
    m_tmap[gptd] = h; 
    return gptd;
#endif

}


bool GEventBase::cancel_timer(void* tid)
{
    GEventHandler *h = NULL;
    {
        std::lock_guard<std::mutex> guard(m_tlock);
        auto it = m_tmap.find((GDP_PER_TIMER_DATA *)tid);
        if (it != m_tmap.end())
        {
            h = it->second;
            //LG("cancel timer %p", tid); 
            // set cancelled flag in lock guard to avoid thread competition
            h->close(false); 
            // erase it later on timeout to avoid crash
            //m_tmap.erase(it);
        }
    }

    if (h == NULL)
    {
        LG("can not find handler for timer %p", tid);
        return false;
    }

    LG("cancel timer %p", tid); 
    //GDP_PER_TIMER_DATA *gptd = h->gptd (); 
    // no handler delete to avoid crash 
    // when another thread dispatch timer event on this handler
    // h->close (); 
    //if (!h->reuse())
    //    delete h; 

    // will be deleted later in map erase.
    //delete gptd; 
    return true; 
}

bool GEventBase::on_timeout(GDP_PER_TIMER_DATA *gptd)
{
    bool erase = false; 
    GEventHandler *h = NULL;
    //LG("%p timeout", gptd);

    {
        std::lock_guard<std::mutex> guard(m_tlock);
        auto it = m_tmap.find(gptd);
        if (it != m_tmap.end())
        {
            h = it->second;
            if (gptd->cancelled || gptd->period_msec == 0)
            {
                // remove do once & cancelled timer
                erase = true; 
                m_tmap.erase(it); 
            }
        }
    }

    if (h == NULL)
    {
        LG("can not find handler for timer %p", gptd);
        return false;
    }

    bool ret = true; 
    if (!gptd->cancelled)
    {
        // only dispatch last timeout event when no cancelled
        ret = h->on_timeout(gptd);
    }

    if (gptd->cancelled)
    {
        // user cancel during timeout callback?
        std::lock_guard<std::mutex> guard(m_tlock);
        auto it = m_tmap.find(gptd);
        if (it != m_tmap.end())
        {
            erase = true; 
            m_tmap.erase (it); 
        }
    }

    if (erase)
    {
        h->close(true); 
        if (!h->reuse())
            delete h; 
    }

    return ret; 
}

bool GEventBase::do_timeout(GDP_PER_TIMER_DATA *gptd)
{
    if (gptd == nullptr)
    {
        LG("dynamic down cast failed? skip timeout event"); 
        return false; 
    }
    
#ifdef IOCP_DUMP
    // prevent too frequency logs, 
    // only log once timer or periodical timer that equal or more than 3 minutes.
    if (gptd->period_msec == 0 || gptd->period_msec >= TIMER_LOG_THRESHOLD)
        LG("time out, due %.0f, period %.0f", gptd->due_msec / 1000.0, gptd->period_msec / 1000.0);
#endif

    on_timeout(gptd); 
    return true; 
}

void GEventBase::run()
{
    LG("loop running");
    int ret = false; 
#ifdef WIN32
    int error_count = 0; 
    DWORD bytes = 0; 
    OVERLAPPED *ol = NULL; 
    GDP_PER_HANDLE_DATA *gphd = NULL; 
    GDP_PER_IO_DATA *gpid = NULL; 
    while (1)
    {
        // clear pointers
        gphd = nullptr; 
        ol = nullptr; 
        if (!GetQueuedCompletionStatus(m_iocp, &bytes, (PULONG_PTR)&gphd, &ol, INFINITE))
        {
            ret = GetLastError (); 
            gpid = (GDP_PER_IO_DATA*)CONTAINING_RECORD(ol, GDP_PER_IO_DATA, ol);
            LG("get queued completion status failed, errno %d, gpid.so %d, gphd.so %d", ret, ol ? gpid->so : 0, gphd ? gphd->so : 0);
            if (ret == ERROR_INVALID_HANDLE /*|| ret == ERROR_INVALID_FUNCTION*/)
            {
                LG("fatal error, quiting.."); 
                break;
            }

            if (!m_running)
            {
                LG("service quiting on error %d...", ret); 
                // post event back into completion queue to pumping it later.
                //PostQueuedCompletionStatus(m_iocp, bytes, (ULONG_PTR)gphd, ol); 
                break; 
            }

            // sometimes we will receive socket errors on GetQueuedCompletionStatus (995, 64, 1236... eg.), 
            // if we break here, thread in poll will run over some times, 
            // so just ignore it and waiting the exit notify ...

            if (ol && gphd)
            {
                //gpid = (GDP_PER_IO_DATA*)CONTAINING_RECORD(ol, GDP_PER_IO_DATA, ol);
                //LG("error detected on %d (%d per io), dispatching on_error...", gphd->so, gpid->so); 
                if (gphd->so == gpid->so)
                    do_error(gphd);
                else
                    // some error occurs, just recylce the memory, no notify..
                    delete gphd; 

                delete gpid; 
            }

            // avoid run into dead loop
            if (++error_count > MAX_ERROR_COUNT)
            {
                LG("retry %d times, reach max error count %d, quiting..", error_count, MAX_ERROR_COUNT); 
                break; 
            }

            continue; 
        }

        //if (key == GDP_KEY_EXIT)
        //{
        //    LG("got exit notify, quiting..."); 
        //    break; 
        //}

        // on end, we post null overlapped pointers
        if (ol == NULL)
        {
            LG("no overlapped param provided, quiting..."); 
            break; 
        }

        if (!m_running)
        {
            LG("service quiting...");
            // post event back into completion queue to pumping it later.
            //PostQueuedCompletionStatus(m_iocp, bytes, (ULONG_PTR)gphd, ol);

            // if this is a timer, we will crash in pumping by deleting gptd twice...
            //delete gpid; 
            break;
        }

		// reset
        error_count = 0; 
        gpid = (GDP_PER_IO_DATA*)CONTAINING_RECORD(ol, GDP_PER_IO_DATA, ol);
        //LG("got io event, handle %p, io %p", gphd, gpid); 
        // always need to do this
        gpid->bytes = bytes;
        switch (gpid->op)
        {
        case OP_TIMEOUT:
            ret = do_timeout(dynamic_cast<GDP_PER_TIMER_DATA *>(gpid)); 
            break; 
        case OP_ACCEPT:
            ret = do_accept(gpid); 
            break; 
        case OP_RECV:
            if (bytes == 0)
            {
                // will delete gphd automatically
                do_error(gphd);
                delete gpid; 
                ret = true; 
            }
            else
            {
                //if (gpid->wsa.len != bytes)
                //{
                //    LG("length not updated, %d != %d", gpid->wsa.len, bytes); 
                //    gpid->wsa.len = bytes; 
                //}

                ret = do_recv(gphd, gpid);
            }
            break; 
        default:
            LG("unknown operation %d when iocp completed, ignore", gpid->op); 
            break; 
        }
    }

#else // WIN32

    while (1)
    {
        std::unique_lock<std::mutex> guard(m_lock); 
        if (m_leader != pthread_self ())
        {
            LG("became leader"); 
            m_leader = pthread_self (); 
        }

        if (!m_running)
        {
            LG("service quiting...");
            break;
        }

        struct epoll_event eps; 
        // can handle only one request a time 
        // -1: all timer goes into pipe, no timeout here! 
        ret = epoll_wait (m_ep, &eps, 1, -1); 
        //guard.unlock (); 
        if (ret < 0)
        {
            LG("epoll_wait failed with %d", errno); 
            continue; 
        }


        // always 1 or 0 events
        //LG("%d events detected", ret); 
        if (ret == 0)
        {
            LG("should never timeout !"); 
            //do_timeout (nullptr); 
            continue; 
        }

        bool readd = true; 
        int fd = eps.data.fd; 
#  ifndef HAS_ET
        // remove fd temporaryly from epoll to prevent duplicate notify
        eps.events = EPOLLIN; 
        eps.data.fd = fd; 
        ret = epoll_ctl (m_ep, EPOLL_CTL_DEL, fd, &eps); 
        if (ret < 0)
        {
            LG("remove %d from epoll failed, errno %d", fd, errno); 
        }
#    ifdef IOCP_DUMP
        else 
            LG("del %d from epoll temporary", fd); 
#    endif
#  endif

        // then allow other thead in..
        guard.unlock (); 
        if (fd == m_listener)
        {
            if (eps.events & EPOLLIN)
            {
                // new client arrives
                do_accept (fd); 
            }
            else 
                LG("unexpect events 0x%08x on listener", eps.events); 
        }
        else if (fd == m_pp[0])
        {
            // notify message
            bool reinit = false;
            if (eps.events & EPOLLIN)
            {
                // clear trigger flag
                char ch = 0; 
                void *ptr = nullptr;   
                ret = read (m_pp[0], &ch, 1); 
                LG("got notify %d: '%c'", ret, ch); 
                if (ret != 1)
                    reinit = true; 
                else 
                {
                    switch (ch)
                    {
                        case 'e':
                            break; 
                        case 't':
                            // timer
                            ret = read (m_pp[0], &ptr, sizeof (ptr)); 
                            if (ret != sizeof (ptr))
                            {
                                reinit = true; 
                                LG("read timer pointer failed on pipe"); 
                            }
                            else 
                            {
                                // before doing work, add pipe handle into epoll to receive next timer...
                                eps.events = EPOLLIN; 
                                eps.data.fd = fd; 
                                ret = epoll_ctl (m_ep, EPOLL_CTL_ADD, fd, &eps); 
                                if (ret < 0)
                                    LG("re-add %d to epoll failed, errno %d, fatal error", fd, errno); 
#    ifdef IOCP_DUMP
                                else 
                                    LG("re-add %d to epoll ok", fd); 
#    endif
                                readd = false; 
                                do_timeout ((GDP_PER_TIMER_DATA *)ptr); 
                            }
                            break; 
                        default:
                            LG("unknown pipe type"); 
                            break; 
                    }
                }
            }
            
            if (eps.events & EPOLLERR || eps.events & EPOLLHUP)
                reinit = true; 

            if (eps.events & ~(EPOLLIN | EPOLLERR | EPOLLHUP))
                LG("unexpect events 0x%08x on pipe", eps.events); 

            if (reinit)
            {
                LG("pipe break detected, start reinit"); 
                close_pipe (); 
                init_pipe (); 
                readd = false;
            }
        }
        else 
        {
            // common connection
            conn_key_t key = get_key_from_fd (fd, false); 
            if (eps.events & EPOLLIN)
            {
                // data arrive on connections
                // if recv failled, not added it to epoll anymore
                readd = do_recv (key); 
            }

            if (eps.events & EPOLLERR || eps.events & EPOLLHUP)
            {
                // exception on connections
                do_error (key); 
                readd = false; 
            }

            // to see any other flag left?
            if (eps.events & ~(EPOLLIN | EPOLLERR | EPOLLHUP))
                LG("unexpect events flag 0x%08x, fd = %d", eps.events, fd); 
        }

#  ifndef HAS_ET
        if (readd && m_running)
        {
            eps.events = EPOLLIN; 
            eps.data.fd = fd; 
            ret = epoll_ctl (m_ep, EPOLL_CTL_ADD, fd, &eps); 
            if (ret < 0)
                LG("re-add %d to epoll failed, errno %d, fatal error", fd, errno); 
#    ifdef IOCP_DUMP
            else 
                LG("re-add %d to epoll ok", fd); 
#    endif
        }
#  endif
    }
#endif

    LG("loop prepare to exit"); 
}

void GEventBase::exit(int extra_notify)
{
    // notify thread that running loop to exit.
    m_running = false; 

#ifdef WIN32
#  if 1
    if (m_iocp != NULL)
    {
        // notify all thread to exit
        if (m_thrnum != m_grp.size())
        {
            // maybe some thread exit halfway
            LG("actual thread num %d != expected %d, using previous one", m_grp.size(), m_thrnum); 
            m_thrnum = m_grp.size(); 
        }

        for (int i = 0; i < m_thrnum + extra_notify; ++i)
        {
            if (!post_completion(0, 0, NULL))
                LG("post notify status to iocp failed, errno %d", GetLastError());
        }

        LG("notify %d thread (%d extra) to exit..", m_thrnum, extra_notify);

        m_grp.join_all();
        LG("join all working thread");
    }
#  else
    // let we see what error returned from GetQueuedCompletionPort when we close iocp directly...
    // result is: 6
#  endif

    if (m_timerque != NULL)
    {
        BOOL ret = DeleteTimerQueue(m_timerque);
        m_timerque = NULL;
        LG("delete timer queue return %d", ret);
    }

#else // WIN32

    if (m_ep != -1)
    {
        // notify all thread to exit
        if (m_thrnum != m_grp.size())
        {
            // maybe some thread exit halfway
            LG("actual thread num %d != expected %d, using previous one", m_grp.size(), m_thrnum); 
            m_thrnum = m_grp.size(); 
        }

        // in case signal does not break epoll_wait, using pipe instead...
        for (int i=0; i<m_thrnum + extra_notify; ++ i)
            post_notify ('e'/* means exit */);

        LG("notify %d thread (%d extra) to exit..", m_thrnum, extra_notify);
    }

#  ifdef HAS_SIGTHR
    if (m_sigthr)
    {
        pthread_t tid = get_std_thread_id (m_sigthr); 
        pthread_kill (tid, SIGINT); 
        LG ("notify signal thread to exit by killing %llu a SIGINT", tid); 
    }
#  endif
#endif

    if (m_listener != INVALID_SOCKET)
    {
        closesocket(m_listener);
        m_listener = INVALID_SOCKET;
        LG("shutdown listening..");
    }
}

void GEventBase::cleanup()
{
    std::unique_lock <std::mutex> guard(m_mutex);
    LG("%d socket handlers total", m_map.size());
    for (auto it = m_map.begin(); it != m_map.end();)
    {
        it->second->cleanup(true);
        it->second->close(true); 
        if (!it->second->reuse())
        {
            delete it->second;
        }

        //LG("destroy handler %p", it->second);
        it = m_map.erase(it);
    }

    guard.unlock (); 
    std::lock_guard<std::mutex> lock(m_tlock);
    LG("%d timer handlers total", m_tmap.size());
    for (auto it = m_tmap.begin(); it != m_tmap.end();)
    {
        it->second->cleanup(true);
        it->second->close(true); 
        if (!it->second->reuse())
            delete it->second;

        //LG("destroy handler %p", it->second);
        it = m_tmap.erase(it);
    }
}

void GEventBase::fini()
{
#ifdef WIN32
    if (m_iocp != NULL)
    {
        // pump left events
        DWORD bytes = 0;
        OVERLAPPED *ol = NULL;
        GDP_PER_HANDLE_DATA *gphd = NULL;
        GDP_PER_IO_DATA *gpid = NULL;

        // unduplicated
        //std::set <GDP_PER_HANDLE_DATA *> gphds; 
        while (true)
        {
            if (!GetQueuedCompletionStatus(m_iocp, &bytes, (PULONG_PTR)&gphd, &ol, 0) && ol == NULL)
            {
                LG("no more event, quiting...");
                break;
            }

            //if (ol == NULL)
            //{
            //    LG("pumping empty notify, ignoring..");
            //    continue; 
            //}

            gpid = (GDP_PER_IO_DATA*)CONTAINING_RECORD(ol, GDP_PER_IO_DATA, ol);
#  if 0
            LG("pump and discard io event on fd %d, op %d, length %d, errno %d", gpid->so, gpid->op, bytes, GetLastError ()); 
            if (gpid->op == OP_ACCEPT)
            {
                // ~GDP_PER_IO_DATA does not close the socket, so...
                LG("destroying socket %d for accepting", gpid->so); 
                closesocket(gpid->so);
            }
            else if (gphd)
            {
                // avoid crash (gphd is deleted during cleanup)
                LG("leave gphd %p", gphd); 
            }

            delete gpid; 

            // maybe appear many times (more than 1 events) , so takecare..
            // delete gphd; 

            //if (gphd)
            //    // timer has no gphd
            //    gphds.insert(gphd);
#  else
            // just discard them on terminal (it is not worth to do cleaup with crash risk)
            LG("pump and discard io event %p, length %d, errno %d", gpid, bytes, GetLastError ()); 
#  endif
        }

        CloseHandle(m_iocp);
        m_iocp = NULL;
    }

#else // WIN32

    // call exit first!
    m_grp.join_all();
    LG("join all working thread");

#  ifdef HAS_SIGTHR
    if (m_sigthr)
    {
        m_sigthr->join (); 
        delete m_sigthr; 
        m_sigthr = nullptr; 
    }
#  endif

    close_pipe (); 
    if (m_ep != -1)
    {
        close (m_ep);
        m_ep = -1; 
    }
#endif

    m_running = false; 
}
