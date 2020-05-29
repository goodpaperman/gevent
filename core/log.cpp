
#include "log.h"
#include <stdio.h> 
#include <stdarg.h> 
#include <memory.h>
#ifdef WIN32
#include <windows.h> // for OutputDebugString
#else
#include <syslog.h>
#endif

#define LOG_BUF_SIZE 1024

FILE* g_log = NULL; 
void openLog (char const* filename)
{
    if (g_log)
        return; 

    g_log = fopen (filename, "w+"); 
#ifndef WIN32
    openlog (filename, LOG_PID, LOG_USER); 
#endif
}

void writeLog(const char *fmt, ...)
{
    // can dump to DebugView even if log open failed !
    //if (!log_)
    //    return; 

    char buf[LOG_BUF_SIZE] = { 0 }; 

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, LOG_BUF_SIZE - 1, fmt, ap);
    va_end(ap);

#ifdef WIN32
    OutputDebugStringA (buf); 
#else
    syslog (LOG_INFO, buf, strlen (buf)); 
#endif

    if (g_log)
    {
        fputs (buf, g_log); 
        fflush (g_log); 
    }
}


void closeLog ()
{
    if (g_log)
    {
        fclose (g_log); 
        g_log = NULL; 
    }

#ifndef WIN32
    closelog (); 
#endif
}

