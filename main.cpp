#include "config.h"


int main(int argc, char* argv[]){ 

    Config config;  // 解析命令行

    config.parse_arg(argc, argv);

    WebServer server;

    printf("\n#######################Init Server····##########################\n");
    server.init(config.PORT, config.OPT_LINGER, config.TRIGMode, config.thread_num);
    printf("######Init Server success!######\n\n");


    printf("\n#######################Init Threadpool····##########################\n");
    server.thread_pool();
    printf("######Init Threadpool success!######\n\n");

    printf("\n#######################Init Trigmode····##########################\n");
    server.trig_mode();
    printf("######Init Trigmode success!######\n\n");

    printf("\n#######################Event Listen····##########################\n");
    server.eventListen();
    printf("######Event Listen success!######\n\n");

    printf("\n#######################Event Loop····##########################\n");
    server.eventLoop();
    printf("######Event Loop success!######\n\n");

    return 0;
}