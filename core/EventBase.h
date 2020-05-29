#pragma once


#include "EventHandler.h" 
#include <string>
#include <map>
#include <mutex> 
#include <condition_variable>
#include "thread_group.hpp"

//#define HAS_ET
#define GEV_MAX_BUF_SIZE 65536

class GEventBase : public IEventBase
{
public:
    GEventBase();
    ~GEventBase();

#ifdef WIN32
    virtual HANDLE iocp () const; 
#else
    virtual int epfd () const; 
#endif
    virtual bool post_timer(GEV_PER_TIMER_DATA *gptd); 
    virtual GEventHandler* create_handler() = 0; 

    // thr_num : 
    //  =0 - no default thread pool, user provide thread and call run
    //  <0 - use max(|thr_num|, processer_num)
    //  >0 - use thr_num
    bool init(int thr_num = -8, int blksize = GEV_MAX_BUF_SIZE
#ifndef WIN32
              , int timer_sig = SIGUSR1
#endif
              ); 

    bool listen(unsigned short port, unsigned short backup = 10);
    GEventHandler* connect(unsigned short port, GEventHandler* exist_handler = NULL); 
    // PARAM
    // due_msec: first timeout milliseconds
    // period_msec: later periodically milliseconds
    // arg: user provied argument
    // exist_handler: reuse the timer handler
    //
    // RETURN
    //   NULL: failed
    void* timeout(int due_msec, int period_msec, void *arg, GEventHandler *exist_handler);
    bool cancel_timer(void* tid); 
    void fini();  
    void run(); 
    void exit(int extra_notify = 0); 
    void cleanup(); 

protected:
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

    virtual bool on_accept(GEV_PER_HANDLE_DATA *gphd);
    virtual bool on_read(GEventHandler *h, GEV_PER_IO_DATA *gpid); 
    virtual void on_error(GEventHandler *h);
	virtual bool on_timeout (GEV_PER_TIMER_DATA *gptd); 
    

protected:
    volatile bool m_running = false;
    int m_thrnum = 0; 
    int m_blksize = GEV_MAX_BUF_SIZE; 
    std::thread_group m_grp; 
    SOCKET m_listener = INVALID_SOCKET;

    std::mutex m_mutex;  // protect m_map
    std::mutex m_tlock; // protect m_tmap
    // timer_t may conflict when new timer created after old timer closed
    //std::map <timer_t, GEventHandler *> m_tmap; 
    std::map <GEV_PER_TIMER_DATA*, GEventHandler *> m_tmap; 

#ifdef WIN32
    LPFN_ACCEPTEX m_acceptex = nullptr; 
    LPFN_GETACCEPTEXSOCKADDRS m_getacceptexsockaddrs = nullptr; 
    HANDLE m_iocp = NULL; 
    HANDLE m_timerque = NULL; 

    std::map<GEV_PER_HANDLE_DATA*, GEventHandler*> m_map; 
#else
    int m_ep = -1; 
    int m_pp[2]; 
    int m_tsig = 0; // signal number for timer

    std::mutex m_lock;   // protect epoll
    pthread_t m_leader = -1; 
    std::map<conn_key_t, GEventHandler*> m_map; 
#  ifdef HAS_SIGTHR
    // special thread only cares about signal
    std::thread *m_sigthr = nullptr; 
#  endif
#endif
};

