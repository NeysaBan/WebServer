#ifndef PRINT_TIME_H
#define PRINT_TIME_H

#include <iostream>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <mutex>
#include <pthread.h>
#include <thread> 
#include <unistd.h> 


class PrintTime{
	public:

		std::mutex g_mtx;

		void printfTime()
		{
			char buf[32] = {0};
			struct timeval tv;
			struct tm      tm;
			size_t         len = 28;
			memset(&tv, 0, sizeof(tv));
			memset(&tm, 0, sizeof(tm));
			gettimeofday(&tv, NULL);
			localtime_r(&tv.tv_sec, &tm);
			strftime(buf, len, "%Y-%m-%d %H:%M:%S", &tm);
			len = strlen(buf);
			sprintf(buf + len, ".%-4.3d", (int)(tv.tv_usec/1000)); 
			printf("%s",buf);
		}
		void printXX()
		{
			std::cout<<std::endl;
		}

		template <typename T,typename... types>

		void printXX(const T& firstArg,const types&... arges)
		{
			std::cout<<firstArg;
			printXX(arges...);
		}
		template <typename T,typename... types>

		void mvPrintf(const T& firstArg,const types&... arges)
		{
			std::lock_guard<std::mutex> lock(g_mtx);
			printfTime();
			printXX(firstArg,arges...);
		}
};


#endif