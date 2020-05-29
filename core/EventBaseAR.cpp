#include "EventBaseAR.h"
#include "log.h" 

#ifdef WIN32
#else
#  include <fcntl.h>
#  include <signal.h>
#  include <sstream>
#endif


GEventBaseWithAutoReconnect::GEventBaseWithAutoReconnect(int reconn_min, int reconn_max)
    : m_port(0)
    , m_app(nullptr)
    , m_timer(nullptr)
    , m_reconn_min(reconn_min)
    , m_reconn_max(reconn_max)
    , m_reconn_curr(m_reconn_min)
{
    m_htimer = create_handler (); 
}


GEventBaseWithAutoReconnect::~GEventBaseWithAutoReconnect()
{
    delete m_htimer; 
}

GEventHandler* GEventBaseWithAutoReconnect::connector()
{
    return m_app; 
}

void GEventBaseWithAutoReconnect::on_connect_break()
{
    writeLog("connection break detected");
    // don't clear m_app to avoid crash when other thread hold this pointer and using...
    //m_app = nullptr;
}

bool GEventBaseWithAutoReconnect::on_connected(GEventHandler *app)
{
    // sub class should do this if don't call base ...
    writeLog("reconnected, reinit..");
    // 0. assign to null
    // 1. self assign 
    m_app = app; 
    //m_app->arg(arg); 
    return true; 
}

bool GEventBaseWithAutoReconnect::do_connect(unsigned short port, void *arg)
{
    m_port = port;
    if (!m_app || !m_app->connected())
    {
        GEventHandler *app = connect(port, m_app);
        if (app == nullptr)
        {
            writeLog("connect to server failed, port %d, errno %d", port, WSAGetLastError());
            // re-connect even for first in!!
            do_reconnect(arg);
            return false;
        }
        else
        {
            app->arg(arg); 
            writeLog("(re)connect to server ok");
            m_reconn_curr = m_reconn_min; // reset timeout
            if (!on_connected(app))
                return false;
        }
    }
    else
        writeLog("connection keeps well, ignore reconnect timer!");

    return true;
}

void GEventBaseWithAutoReconnect::do_reconnect(void *arg)
{
    //m_app = nullptr;  // clean pointer, instance will be auto deleted.

    // start a timer here
    int conntime = m_reconn_curr;
    m_reconn_curr *= 2;
    if (m_reconn_curr > m_reconn_max)
        m_reconn_curr = m_reconn_max;

    if (m_timer)
    {
        //int ret = cancel_timer(m_timer);
        writeLog("warning, old timer %p still existing", m_timer);
    }

    m_timer = timeout(conntime * 1000, 0, arg, m_htimer);
    if (m_timer == nullptr)
        writeLog("start timer to do reconnect failed");
    else
        writeLog("start timer %d seconds later to do connect...", conntime);

    // necessary if no.
    //do_loop();
}

void GEventBaseWithAutoReconnect::on_error(GEventHandler *h)
{
    bool notify = m_app && m_app == h; 
    // first call HANLDER::on_error
    GEventBase::on_error(h);
    if (notify)
    {
        // then do notify
        on_connect_break();
        if (m_app->auto_reconnect()) 
        {
            // last start timer to do reconnect
            do_reconnect(nullptr);
        }
    }
}

bool GEventBaseWithAutoReconnect::on_timeout(GEV_PER_TIMER_DATA *gptd)
{
    // note gptd will deleted during on_timeout, so here need to remember it !
    //timer_t timer = gptd->timer; 
    void *arg = gptd->user_arg; 
    // first call HANLDER::on_timeout and clean one shot timer.
    GEventBase::on_timeout(gptd);
    if (m_timer == gptd)
    {
        // then try reconnect and setup new timer if failed.
        writeLog("timeout for reconnect %d, timer %p cleared", m_port, m_timer);
        // will auto delete one shot timer, so here just clearing..
        m_timer = nullptr; 
        do_connect(m_port, arg);
    }

    return true; 
}

