#pragma once

/**
 * @file log facilities
 * @brief log utility functions
 *
 * output log to file
 */

/**
 * @brief open log file
 * @param filename path to log file
 * @return none
 */
void openLog (char const* filename); 

/**
 * @brief write one line log
 * @param fmt printf like format
 * @param ... variable parameter for @param fmt
 * @return none
 */
void writeLog(const char *fmt, ...); 

/**
 * @brief close log file
 * @return none
 */
void closeLog (); 

