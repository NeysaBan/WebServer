#ifndef CONFIG_H
#define CONFIG_H

#include "./server/server.h"

using namespace std;

class Config{

    public:
        Config();
        ~Config(){};

        void parse_arg(int argc, char * argv[]);

        int PORT;

        int TRIGMode;

        int LISTENTrigmode; // listenfd 触发模式

        int CONNTrigmode;   // connfd触发模式

        int OPT_LINGER;

        int thread_num;

        int LOGWrite;   // 日志写入方式

        int close_log;

    private:
};

#endif