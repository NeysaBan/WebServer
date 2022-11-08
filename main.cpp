#include "config.h"

int main(int argc, char *argv[])
{

    Config config; // 解析命令行

    config.parse_arg(argc, argv);

    WebServer server;

    PrintTime PT;
    PT.mvPrintf("[info]  Init Server···");
    server.init(config.PORT, config.OPT_LINGER, config.TRIGMode, config.thread_num, config.LOGWrite, config.close_log);
    PT.mvPrintf("Init Server success!\n");

    PT.mvPrintf("Init Write····");
    server.log_write();
    PT.mvPrintf("Init Write success!\n");

    PT.mvPrintf("Init Threadpool····");
    server.thread_pool();
    PT.mvPrintf("Init Threadpool success!\n");

    PT.mvPrintf("Init Trigmode····");
    server.trig_mode();
    PT.mvPrintf("Init Trigmode success!\n");

    PT.mvPrintf("Event Listen····");
    server.eventListen();
    PT.mvPrintf("Event Listen success!\n");

    PT.mvPrintf("Event Loop····");
    server.eventLoop();
    PT.mvPrintf("Event Loop success!\n");

    return 0;
}