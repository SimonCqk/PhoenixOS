#pragma once

#include <iostream>
#include <thread>
#include <map>

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#include "pos/common.h"
#include "pos/log.h"

template<class T_POSTransport, class T_POSClient>
class POSWorkspace;

template<class T_POSTransport>
class POSAgent;

template<class T_POSTransport, class T_POSClient>
class POSOobServer;

template<class T_POSTransport>
class POSOobClient;

/*!
 *  \brief  metadata of a out-of-band client
 */
typedef struct POSOobClientMeta {
    // ip address
    in_addr_t ipv4;

    // udp port
    uint16_t port;

    // process id on the host
    __pid_t pid;

    // uuid of the client on the server
    pos_client_uuid_t uuid;
} POSOobClientMeta_t;

/*!
 *  \brief  out-of-band message type id
 */
enum pos_oob_msg_typeid_t {
    kPOS_Oob_Register_Client=0,
    kPOS_Oob_Unregister_Client,
    kPOS_Oob_Connect_Transport,
    kPOS_Oob_Mock_Api_Call,
};

/*!
 *  \brief  out-of-band message content
 */
typedef struct POSOobMsg {
    // type of the message
    pos_oob_msg_typeid_t msg_type;

    // meta data of a out-of-band client
    POSOobClientMeta_t client_meta;
    
    // out-of-band message payload
#define POS_OOB_MSG_MAXLEN 1024
    uint8_t payload[POS_OOB_MSG_MAXLEN];
} POSOobMsg_t;

/*!
 *  \brief  default endpoint config of OOB server
 */
#define POS_OOB_SERVER_DEFAULT_IP   "0.0.0.0"
#define POS_OOB_SERVER_DEFAULT_PORT 5213
#define POS_OOB_CLIENT_DEFAULT_PORT 12123

/*!
 *  \brief  prototype of the server-side function
 */
template<class T_POSTransport, class T_POSClient>
using oob_server_function_t = pos_retval_t(*)(
    int, struct sockaddr_in*, POSOobMsg_t*, POSWorkspace<T_POSTransport, T_POSClient>*, POSOobServer<T_POSTransport, T_POSClient>*
);

/*!
 *  \brief  prototype of the client-side function
 */
template<class T_POSTransport> 
using oob_client_function_t = pos_retval_t(*)(
    int, struct sockaddr_in*, POSOobMsg_t*, POSAgent<T_POSTransport>*, POSOobClient<T_POSTransport>*, void*
);

/*!
 *  \brief  macro of the definition of the server-side OOB RPC callback function
 */
#define POS_OOB_FUNC_S()                                                                                    \
template<class T_POSTransport, class T_POSClient>                                                           \
pos_retval_t sv(                                                                                            \
    int fd, struct sockaddr_in* remote, POSOobMsg_t* msg,                                                   \
    POSWorkspace<T_POSTransport, T_POSClient>* ws, POSOobServer<T_POSTransport, T_POSClient>* oob_server    \
)

/*!
 *  \brief  macro for the definition of the client-side OOB RPC request function
 */
#define POS_OOB_FUNC_C()                                                        \
template<class T_POSTransport>                                                  \
pos_retval_t clnt(                                                              \
    int fd, struct sockaddr_in* remote, POSOobMsg_t* msg,                       \
    POSAgent<T_POSTransport>* agent, POSOobClient<T_POSTransport>* oob_clnt,    \
    void *call_data                                                             \
)

/*!
 *  \brief  macro for sending OOB message between client & server
 */
#define __POS_OOB_SEND()                                                                                                \
{                                                                                                                       \
    if(unlikely(sendto(fd, msg, sizeof(POSOobMsg_t), 0, (struct sockaddr*)remote, sizeof(struct sockaddr_in)) < 0)){    \
        POS_WARN_DETAIL("failed oob sending: %s", strerror(errno));                                                     \
        return POS_FAILED;                                                                                              \
    }                                                                                                                   \
}

/*!
 *  \brief  macro for receiving OOB message between client & server
 */
#define __POS_OOB_RECV()                                                                                                \
{                                                                                                                       \
    socklen_t __socklen__ = sizeof(struct sockaddr_in);                                                                 \
    if(unlikely(recvfrom(fd, msg, sizeof(POSOobMsg_t), 0, (struct sockaddr*)remote, &__socklen__) < 0)){                \
        POS_WARN_DETAIL("failed oob sending: %s", strerror(errno));                                                     \
        return POS_FAILED;                                                                                              \
    }                                                                                                                   \
}

/*!
 *  \brief  function prototypes for all out-of-band message types
 */
namespace oob_functions {
#define POS_OOB_DECLARE_FUNCTIONS(oob_type) namespace oob_type { POS_OOB_FUNC_S(); POS_OOB_FUNC_C(); }
    POS_OOB_DECLARE_FUNCTIONS(register_client);
    POS_OOB_DECLARE_FUNCTIONS(unregister_client);
    POS_OOB_DECLARE_FUNCTIONS(connect_transport);
    POS_OOB_DECLARE_FUNCTIONS(mock_api_call);
}; // namespace oob_functions

/*!
 *  \brief  UDP-based out-of-band RPC server
 */
template<class T_POSTransport, class T_POSClient>
class POSOobServer {
 public:
    /*!
     *  \brief  constructor
     *  \param  ws      the workspace that include current oob server
     *  \param  ip_str  ip address to bind
     *  \param  port    udp port to bind
     */
    POSOobServer(
        POSWorkspace<T_POSTransport, T_POSClient>* ws,
        const char *ip_str=POS_OOB_SERVER_DEFAULT_IP, uint16_t port=POS_OOB_SERVER_DEFAULT_PORT
    ) : _ws(ws), _stop_flag(false) {
        POS_CHECK_POINTER(ws);

        // step 1: insert oob callback map
        _callback_map.insert({
            {   kPOS_Oob_Register_Client,   oob_functions::register_client::sv      },
            {   kPOS_Oob_Unregister_Client, oob_functions::unregister_client::sv    },
            {   kPOS_Oob_Connect_Transport, oob_functions::connect_transport::sv    },
            {   kPOS_Oob_Mock_Api_Call,     oob_functions::mock_api_call::sv        }
        });

        // step 2: create server socket
        _listen_fd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (_listen_fd < 0) {
            POS_ERROR_C_DETAIL(
                "failed to create listen_fd for out-of-band UDP server: %s",
                strerror(errno)
            );
        }
        _local_addr.sin_family = AF_INET;
        _local_addr.sin_addr.s_addr = inet_addr(ip_str);
        _local_addr.sin_port = htons(port);
        if(bind(_listen_fd, (struct sockaddr*)&_local_addr, sizeof(_local_addr)) < 0){
            close(_listen_fd);
            POS_ERROR_C_DETAIL(
                "failed to bind out-of-band UDP server to \"%s:%u\": %s",
                ip_str, port, strerror(errno)
            );
        }
        POS_DEBUG_C("out-of-band UDP server is binded to %s:%u", ip_str, port);

        // step 3: start daemon thread
        _daemon_thread = new std::thread(&daemon, this);
        POS_CHECK_POINTER(_daemon_thread);
    }

    /*!
     *  \brief  processing daemon of the OOB UDP server
     */
    void daemon(){
        int sock_retval;
        struct sockaddr_in remote_addr;
        socklen_t len = sizeof(remote_addr);
        uint8_t recvbuf[sizeof(POSOobMsg)] = {0};
        POSOobMsg *recvmsg;

        POS_DEBUG_C("daemon of the out-of-band UDP server start running");

        while(!_stop_flag){
            memset(recvbuf, 0, sizeof(recvbuf));
            sock_retval = recvfrom(_listen_fd, recvbuf, sizeof(recvbuf), 0, (struct sockaddr*)&remote_addr, &len);
            if(sock_retval < 0){ continue; }
            
            recvmsg = (POSOobMsg*)recvbuf;
            POS_DEBUG_C(
                "oob recv info: recvmsg.msg_type(%lu), recvmsg.client(ip: %u, port: %u, pid: %d)",
                recvmsg->msg_type, recvmsg->client_meta.ipv4, recvmsg->client_meta.port, recvmsg->client_meta.pid
            );

            // invoke corresponding callback function
            if(unlikely(_callback_map.count(recvmsg->msg_type)) == 0){
                POS_ERROR_C_DETAIL(
                    "no callback function register for oob msg type %lu, this is a bug",
                    recvmsg->msg_type
                )
            }
            (*(_callback_map[recvmsg->msg_type]))(_listen_fd, &remote_addr, recvmsg, _ws, this);
        }

        POS_DEBUG_C("oob daemon shutdown");
        return;
    }

    /*!
     *  \brief  raise the shutdown signal to stop the daemon
     */
    inline void shutdown(){ 
        _stop_flag = true;
        if(_daemon_thread != nullptr){
            _daemon_thread->join();
            delete _daemon_thread;
            _daemon_thread = nullptr;
            POS_DEBUG_C("OOB daemon thread shutdown");
        }
    }

    /*!
     *  \brief  deconstructor
     */
    ~POSOobServer(){
        shutdown();
        if(_listen_fd > 0){ close(_listen_fd); }
    }

 private:
    // UDP socket
    int _listen_fd;

    // local network address
    struct sockaddr_in _local_addr;

    // stop flag to indicate the daemon thread to stop
    bool _stop_flag;

    // the daemon thread for receiving and processing OOB request
    std::thread *_daemon_thread;

    // map of callback functions
    std::map<pos_oob_msg_typeid_t, oob_server_function_t<T_POSTransport,T_POSClient>> _callback_map;

    // pointer to the server-side workspace
    POSWorkspace<T_POSTransport, T_POSClient> *_ws;
};

/*!
 *  \brief  UDP-based out-of-band RPC client
 */
template<class T_POSTransport>
class POSOobClient {
 public:
    /*!
     *  \brief  constructor
     *  \param  agent       pointer to the client-side agent
     *  \param  local_port  expected local port to bind
     *  \param  local_ip    exepected local ip to bind
     *  \param  server_port destination server port
     *  \param  server_ip   destination server ipv4
     */
    POSOobClient(
        POSAgent<T_POSTransport> *agent,
        uint16_t local_port, const char* local_ip="0.0.0.0",
        uint16_t server_port=POS_OOB_SERVER_DEFAULT_PORT, const char* server_ip="127.0.0.1"
    ) : _agent(agent) {
        uint8_t retry_time = 1;

        // step 1: insert oob request map
        _request_map.insert({
            {   kPOS_Oob_Register_Client,   oob_functions::register_client::clnt    },
            {   kPOS_Oob_Unregister_Client, oob_functions::unregister_client::clnt  },
            {   kPOS_Oob_Connect_Transport, oob_functions::connect_transport::clnt  },
            {   kPOS_Oob_Mock_Api_Call,     oob_functions::mock_api_call::clnt      }
        });

        // step 2: obtain the process id
        _msg.client_meta.pid = getpid();

        // step 3: create socket
        _fd = socket(AF_INET, SOCK_DGRAM, 0);
        if (_fd < 0) {
            POS_ERROR_C_DETAIL(
                "failed to create _fd for out-of-band UDP client: %s",
                strerror(errno)
            );
        }
        _port = local_port;
        _local_addr.sin_family = AF_INET;
        _local_addr.sin_addr.s_addr = inet_addr(local_ip);
        _local_addr.sin_port = htons(_port);
        while(bind(_fd, (struct sockaddr*)&_local_addr, sizeof(_local_addr)) < 0){
            if(retry_time == 100){
                POS_ERROR_C_DETAIL("failed to bind oob client to local port, too many clients? try increase the threashold");
            }
            POS_WARN_C(
                "failed to bind out-of-band UDP client to \"%s:%u\": %s, try %uth time to switch port to %u",
                local_ip, _port, strerror(errno), retry_time, _port+1
            );
            retry_time += 1;
            _port += 1;
            _local_addr.sin_port = htons(_port);
        }
        POS_DEBUG_C("out-of-band UDP client is binded to %s:%u", local_ip, _port);
        _msg.client_meta.ipv4 = inet_addr(local_ip);
        _msg.client_meta.port = _port;

        // setup server addr
        _remote_addr.sin_family = AF_INET;
        _remote_addr.sin_addr.s_addr = inet_addr(server_ip);
        _remote_addr.sin_port = htons(server_port);
    }

    /*!
     *  \brief  call OOB RPC request procedure according to OOB message type
     *  \param  id          the OOB message type
     *  \param  call_data   calling payload, coule bd null
     *  \return POS_SUCCESS for successfully requesting
     */
    inline pos_retval_t call(pos_oob_msg_typeid_t id, void *call_data){
        if(unlikely(_request_map.count(id) == 0)){
            POS_ERROR_C_DETAIL("no request function for type %lu is registered, this is a bug", id);
        }
        return (*(_request_map[id]))(_fd, &_remote_addr, &_msg, _agent, this, call_data);
    }

    /*!
     *  \brief  deconstrutor
     */
    ~POSOobClient(){
        if(_fd > 0){ close(_fd); }
    }

    /*!
     *  \brief  set the uuid of the client
     *  \note   this function is invoked during the registeration process 
     *          (i.e., register_client oob type)
     */
    inline void set_uuid(pos_client_uuid_t id){ _msg.client_meta.uuid = id; }

 private:
    // UDP socket
    int _fd;

    // local-used port
    uint16_t _port;

    // local and remote address
    struct sockaddr_in _local_addr, _remote_addr;

    // the one-and-only oob message instance
    POSOobMsg_t _msg;

    // pointer to the client-side POS agent
    POSAgent<T_POSTransport> *_agent;

    // map of request functions
    std::map<pos_oob_msg_typeid_t, oob_client_function_t<T_POSTransport>> _request_map;
};

#include "pos/oob.c"