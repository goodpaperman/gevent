#pragma once
#include "EventHandler.h"

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
    // protect m_stub to prevent multi-entry when HAS_ET
#ifdef HAS_ET
    std::mutex m_mutex;              /**< lock to protect m_stub */
#endif

    std::string m_stub;              /**< things left by last read (part of whole message) */
};
