#include "prelude.h"
extern "C" {
#include STANDALONE_INC
}
#include <iostream>

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    if (argc < 2) return 2;
    if (argc > 2) vcn_ipc_set_token_c(argv[2]);
    if (vcn_ipc_listen_c(argv[1]) != 1) return 3;
    std::cout << "ready" << std::endl;
    for (int i = 0; i < 2000; i++)
    {
        if (vcn_ipc_poll() == 1)
        {
            vcn_ipc_line();
            std::string line = g_buf1;
            if (line == "ping") vcn_ipc_reply_c("pong");
            else if (line == "subscribe") { if (!vcn_ipc_subscribe_c()) vcn_ipc_reply_c("error: limit"); }
            else if (line.rfind("publish ", 0) == 0) { vcn_ipc_publish_c(line.substr(8).c_str()); vcn_ipc_reply_c("ok"); }
            else if (line == "quit") { vcn_ipc_reply_c("bye"); break; }
            else vcn_ipc_reply_c("error: nieznane polecenie");
        }
        usleep(5000);
    }
    vcn_ipc_close();
    return 0;
}
