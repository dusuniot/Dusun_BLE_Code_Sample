#ifndef DUSUN_LOG_H
#define DUSUN_LOG_H

#include <syslog.h>

#define  INIT_LOG(tag) \
	do 			\
	{			\
		openlog(tag, LOG_PERROR | LOG_PID, LOG_DAEMON);	\
	} while(0) ;



#define LOG(fmt, args...)  syslog(LOG_DEBUG, fmt, ##args)


#endif