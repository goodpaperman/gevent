#include "JsonEventHandler.h"
#include "log.h"


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
