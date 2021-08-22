#pragma once

#include "EventHandler.h" 
#include <string>
#include <map>
#include <mutex> 
#include <condition_variable>
#include <functional>
#include "thread_group.hpp"

#define GEV_MAX_BUF_SIZE 65536

/**
 * @brief event base, the core of the gevent
 * 
 * setup, cleanup, listening, connecting, event looping, thread pool,
 * handler management and etc.
 */
class GEventBase : public IEventBase
{
public:
    GEventBase();
    ~GEventBase();

#ifdef WIN32
    /** @see IEventBase::mpfd */
    virtual HANDLE mpfd () const; 
#else
    /** @see IEventBase::mpfd */
    virtual int mpfd () const; 
#endif

    /** @see IEventBase::post_timer */
    virtual bool post_timer(GEV_PER_TIMER_DATA *gptd); 

    /**
     * @brief return handler for new connection
     * @return inherit class of GEventHandler
     * @note user must implement this method to provide their own handler classes
     */
    virtual GEventHandler* create_handler() = 0; 

    /**
     * @brief initialize event base
     * @param thr_num
     * - =0 - no default thread pool, user provide thread and call run
     * - <0 - use max(|thr_num|, processer_num)
     * - >0 - use thr_num
     * @param blksize buffer size for single event
     * @param timer_sig signal number for timer on linux
     * @retval true - initialize ok
     * @retval false - failed
     * @note user must call this method before others
     */
    bool init(int thr_num = -8, int blksize = GEV_MAX_BUF_SIZE
#ifdef __linux__
              , int timer_sig = SIGUSR1
#endif
              ); 

    /**
     * @brief setup local listener for accept new connection
     * @param port listen port 
     * @param backup listen queue size
     * @retval true - listen ok
     * @retval false - failed
     * @note user only call this method when to be a server
     */
    bool listen(unsigned short port, unsigned short backup = 10);

    /**
     * @brief setup connection to remote server
     * @param port listen port
     * @param host server host
     * @param exist_handler handler reuse if there a one
     * @return new handler binding to that connection, nullptr if connection setup failed
     * @note user only call this method when to be a client
     */
    GEventHandler* connect(unsigned short port, char const* host = "127.0.0.1", GEventHandler* exist_handler = NULL);

    /**
     * @brief setup timer for once or periodic
     * @param due_msec once timer timeout milli-second
     * @param period_msec periodic timer timeout milli-second
     * @param arg user special data
     * @exist_handler handler reuse if there a one
     * @retval timer-id if success
     * @retval nullptr if failed
     * @note user only call this method when having timer task
     */
    void* timeout(int due_msec, int period_msec, void *arg, GEventHandler *exist_handler);

    /**
     * @brief cancel timer
     * @param tid return value of timeout
     * @note once timer after timeout will be cancelled automatically
     */
    bool cancel_timer(void* tid); 

    /** @brief finish the whole thing */
    void fini();  

    /**
     * @brief main entry for event loop
     *
     * after setup, user may call run to block on main, 
     * or start thread pool to do multi-thread handling,
     * on that case, run will be called by each thread in pool.
     */
    void run(); 

    /**
     * @brief exit event loop
     * @param extra_notify some thread may call run but not in \n
     *        our thread pool, use this to do exit notify, \n
     *        how many thread runs, how many notification needs.
     * @note after exit, all thread stopped, but event base status not cleaned
     */
    void exit(int extra_notify = 0); 

    /** @brief close all the handle and clean event handler */
    void cleanup(); 

    /** @brief break all the connections in this event base */
    void disconnect(); 

    /**
     * @brief notify all the client by sending msg on connections
     * @param msg things you want to say
     * @return number connections successfully sent
     * @note it depends on foreach to traverse handlers
     */
    int broadcast(std::string const& msg); 

    /**
     * @brief traverse the handlers and call func for each handler
     * @param func user defined procedure that applies on each handler
     * @param arg passing to the procedure 2nd parameter
     * @return number handlers successfully handled
     * @note handlers user don't want traverse can filtered by filter_handler
     */
    int foreach(std::function<int(GEventHandler *h, void *arg)> func, void *arg);

protected:
    /**
     * @brief called when accepting a new connection
     * @param gphd data binding to new connection
     * @retval true - dispatch accept event ok
     * @retval false - failed
     */
    virtual bool on_accept(GEV_PER_HANDLE_DATA *gphd);

    /**
     * @brief called when receiving data on a connection
     * @param h handler binding to that connection
     * @param gpid data binding to that action
     * @retval true - dispatch read event ok
     * @retval false - failed
     */
    virtual bool on_read(GEventHandler *h, GEV_PER_IO_DATA *gpid); 

    /**
     * @brief called when detecting error on a conection
     * @param h handler binding to that connection
     */
    virtual void on_error(GEventHandler *h);

    /** 
     * @brief called when timer due
     * @param gptd data binding to that timer
     * @retval true - dispatch timer event ok
     * @retval false - failed
     */
	virtual bool on_timeout (GEV_PER_TIMER_DATA *gptd); 

    /**
     * @brief called when for_each traverse handlers
     * @param h handler will traverse
     * @retval true - allow access
     * @retval false - skip
     */
    virtual bool filter_handler(GEventHandler *h); 

private:
#ifdef WIN32
    bool do_accept(GEV_PER_IO_DATA *gpid); 
    bool do_recv(GEV_PER_HANDLE_DATA *gphd, GEV_PER_IO_DATA *gpid); 
    void do_error(GEV_PER_HANDLE_DATA *gphd); 

    int init_socket();
    bool issue_accept(); 
    bool issue_read(GEV_PER_HANDLE_DATA *gphd);
    bool post_completion(DWORD bytes, ULONG_PTR key, LPOVERLAPPED ol); 

#else
    bool do_accept(int fd); 
    bool do_recv(conn_key_t key); 
    void do_error(conn_key_t key); 

    bool init_pipe(); 
    void close_pipe(); 
    bool post_notify (char ch, void* ptr = nullptr); 
    void promote_leader (std::unique_lock<std::mutex> &guard); 

    GEventHandler* find_by_key (conn_key_t key, bool erase); 
    GEventHandler* find_by_fd (int fd, conn_key_t &key, bool erase); 

#  ifdef HAS_SIGTHR
    void sig_proc (); 
#  endif
#endif

    bool do_timeout(GEV_PER_TIMER_DATA *gptd); 

protected:
    volatile bool m_running = false;      /**< if loop is running, set false leads all thread exit */
    int m_thrnum = 0;                     /**< total running thread in pool */
    int m_blksize = GEV_MAX_BUF_SIZE;     /**< default receiving buffer size */
    std::thread_group m_grp;              /**< thread group */
    SOCKET m_listener = INVALID_SOCKET;   /**< listen socket */

    std::recursive_mutex m_mutex;         /**< lock to protect m_map */
    std::mutex m_tlock;                   /**< lock to protect m_tmap */
    // timer_t may conflict when new timer created after old timer closed
    //std::map <timer_t, GEventHandler *> m_tmap; 
    std::map <GEV_PER_TIMER_DATA*, GEventHandler *> m_tmap;  /**< timer map, key is data address binding to timer, value is timer handler */

#ifdef WIN32
    LPFN_ACCEPTEX m_acceptex = nullptr;    /**< address of AcceptEx on win32 */
    LPFN_GETACCEPTEXSOCKADDRS m_getacceptexsockaddrs = nullptr;  /**< address of GetAcceptExSockAddrs on win32 */
    HANDLE m_mp = NULL;  nn                /**< IO completion port handle on win32 */
    HANDLE m_timerque = NULL;              /**< timer queue handle on win32 */

    std::map<GEV_PER_HANDLE_DATA*, GEventHandler*> m_map;  /**< handler map, key is data address binding to handler, value is event handler */
#else
    int m_mp = -1;                         /**< epoll/kqueue file descriptor on unix like */
    int m_pp[2];                           /**< self-notify pipe on unix like */
#  if defined (__linux__)
    int m_tsig = 0;                        /**< signal number for timer on linux */
#  else
    // timer facility in mac & freebsd is integrated into m_mp
#  endif

    std::mutex m_lock;                     /**< lock to protect epoll */
#  if defined (__APPLE__) || defined (__FreeBSD__)
    pthread_t m_leader = NULL;               /**< current leader thread (the one do epoll) */
#  else
    pthread_t m_leader = -1;               /**< current leader thread (the one do epoll) */
#  endif
    std::map<conn_key_t, GEventHandler*> m_map;  /**< handler map, key is the combine of fd/lport/rport, data is event handler */
#  ifdef HAS_SIGTHR
    std::thread *m_sigthr = nullptr;       /**< thread handling signal only */
#  endif
#endif
};

