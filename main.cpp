#include "config.h"

int main(int argc, char *argv[])
{
    //需要修改的数据库信息,登录名,密码,库名
    //数据库账号从环境变量读取，避免把密码硬编码进源码（凭据不进代码库是安全底线）
    //启动示例：TWS_DB_USER=appuser TWS_DB_PASS=xxx ./server -p 8888
    const char *env_user = getenv("TWS_DB_USER");
    const char *env_pass = getenv("TWS_DB_PASS");
    string user = env_user ? env_user : "appuser";
    string passwd = env_pass ? env_pass : "";
    string databasename = "qgydb";

    //命令行解析
    Config config;
    config.parse_arg(argc, argv);

    WebServer server;

    //初始化
    server.init(config.PORT, user, passwd, databasename, config.LOGWrite, 
                config.OPT_LINGER, config.TRIGMode,  config.sql_num,  config.thread_num, 
                config.close_log, config.actor_model);
    

    //日志
    server.log_write();

    //数据库
    server.sql_pool();

    //线程池
    server.thread_pool();

    //触发模式
    server.trig_mode();

    //监听
    server.eventListen();

    //运行
    server.eventLoop();

    return 0;
}