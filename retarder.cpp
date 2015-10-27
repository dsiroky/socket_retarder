/*
 * @author David Siroky (siroky@dasir.cz)
 */

#include <stdio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdarg.h>
#include <assert.h>

#include <map>

//===========================================================================

#define DNS_PORT 53
#define PROXY_PORT_START 20000
#define PROXY_PORT_STOP 20500

#define DISTRIB_NORMAL 0
#define DISTRIB_UNIFORM 1

#define NORMALDIST_MEAN 1000 // ms
#define NORMALDIST_VARIANCE 500 // ms
#define NORMALDIST_SUMS 8

#define UNIFORMDIST_A 500
#define UNIFORMDIST_B 1500

#define DEBUG_LEVEL 0

//===========================================================================

typedef int (*pconnect_t)(int sockfd, const sockaddr *addr,
                          socklen_t addrlen);
typedef ssize_t (*psendto_t)(int sockfd, const void *buf, size_t len, int flags,
                      const sockaddr *dest_addr, socklen_t addrlen);
typedef ssize_t (*psendmsg_t)(int sockfd, const msghdr *msg, int flags);
typedef ssize_t (*psend_t)(int sockfd, const void *buf, size_t len, int flags);

typedef int (*pclose_t)(int fd);

typedef struct { int sockfd; sockaddr *addr; socklen_t addrlen; }
    connect_params_t;

typedef struct { int sockfd; int flags; sockaddr_in addr; int addrlen;
                  int buf_len; char buf[64 * 1024]; }
    sendto_params_t;

typedef struct { int count; pthread_cond_t cv; }
    fd_pending_t;

//===========================================================================

static pconnect_t realconnect;
static psendto_t realsendto;
static psendmsg_t realsendmsg;
static psend_t realsend;
static pclose_t realclose;

static int g_tcp_proxy_port = 0;

static bool g_retard_dns = false;
static int g_distribution = DISTRIB_NORMAL;

static int g_normaldist_mean = NORMALDIST_MEAN;
static int g_normaldist_variance = NORMALDIST_VARIANCE;

static int g_uniformdist_a = UNIFORMDIST_A;
static int g_uniformdist_b = UNIFORMDIST_B;

static int g_debug_level = DEBUG_LEVEL;
static pthread_mutex_t g_log_mutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_mutex_t g_fd_pending_mutex = PTHREAD_MUTEX_INITIALIZER;
static std::map<int, fd_pending_t> g_fd_pending;

//===========================================================================

inline void log(int level, const char *fmt, ...)
{
  va_list ap;
  if (g_debug_level >= level) 
  {
    pthread_mutex_lock(&g_log_mutex);
    fprintf(stderr, "socket_retarder: ");
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    pthread_mutex_unlock(&g_log_mutex);
  }
}

//--------------------------------------------------------------------------

void fd_pending_insert(int fd)
{
  fd_pending_t data;

  pthread_mutex_lock(&g_fd_pending_mutex);
  data.count = 0;
  pthread_cond_init(&data.cv, NULL);
  g_fd_pending[fd] = data;
  log(3, "fd_pending_insert fd=%i", fd);
  pthread_mutex_unlock(&g_fd_pending_mutex);
}

//--------------------------------------------------------------------------

void fd_pending_increase(int fd, int count)
{
  fd_pending_t data;

  pthread_mutex_lock(&g_fd_pending_mutex);
  if (g_fd_pending.count(fd) == 0) 
  {
    data.count = count;
    pthread_cond_init(&data.cv, NULL);
    g_fd_pending[fd] = data;
  } else {
    g_fd_pending[fd].count += count;
  }
  log(3, "fd_pending_increase fd=%i +%i =%i", fd, count, g_fd_pending[fd].count);
  pthread_mutex_unlock(&g_fd_pending_mutex);
}

//--------------------------------------------------------------------------

void fd_pending_increase_present(int fd, int count)
{
  pthread_mutex_lock(&g_fd_pending_mutex);
  if (g_fd_pending.count(fd) > 0) 
  {
    g_fd_pending[fd].count += count;
    log(3, "fd_pending_increase_present fd=%i +%i =%i",
                fd, count, g_fd_pending[fd].count);
  }
  pthread_mutex_unlock(&g_fd_pending_mutex);
}

//--------------------------------------------------------------------------

void fd_pending_decrease(int fd, int count)
{
  pthread_mutex_lock(&g_fd_pending_mutex);
  if (g_fd_pending.count(fd) > 0) 
  {
    g_fd_pending[fd].count -= count;
    if (g_fd_pending[fd].count == 0) pthread_cond_signal(&g_fd_pending[fd].cv);
    log(3, "fd_pending_decrease fd=%i -%i =%i", fd, count, g_fd_pending[fd].count);
  }
  pthread_mutex_unlock(&g_fd_pending_mutex);
}

//--------------------------------------------------------------------------

void fd_pending_wait_remove(int fd)
{
  pthread_mutex_lock(&g_fd_pending_mutex);
  if (g_fd_pending.count(fd) > 0)
  {
    log(3, "fd_pending_wait_remove fd=%i", fd);
    while (g_fd_pending[fd].count > 0)
      pthread_cond_wait(&g_fd_pending[fd].cv, &g_fd_pending_mutex);
    g_fd_pending.erase(g_fd_pending.find(fd));
  }
  pthread_mutex_unlock(&g_fd_pending_mutex);
}

//--------------------------------------------------------------------------

int random_normal()
{
  int sleep_time = 0, i;
  float r;
  for (i = 0; i < NORMALDIST_SUMS; i++)
  {
    r = (float)random() / (float)RAND_MAX;
    sleep_time += r * g_normaldist_variance;
  }
  sleep_time /= NORMALDIST_SUMS;
  sleep_time += g_normaldist_mean - g_normaldist_variance / 2;
  return sleep_time;
}

//--------------------------------------------------------------------------

int random_uniform()
{
  return (float)random() / (float)RAND_MAX * 
          (g_uniformdist_b - g_uniformdist_a) +
          g_uniformdist_a;
}

//--------------------------------------------------------------------------

void random_sleep()
{
  int sleep_time;
  if (g_distribution == DISTRIB_NORMAL)
    sleep_time = random_normal();
  else
    sleep_time = random_uniform();
  if (sleep_time <= 0) return;
  log(2, "sleeping for %i ms", sleep_time);
  usleep(sleep_time * 1000);
}

//--------------------------------------------------------------------------

#define err(msg) _err(msg, __FILE__, __LINE__)

void _err(const char *msg, const char *filename, int linenum)
{
  fprintf(stderr, "error (%s:%i, errno:%i, %s): %s\n", filename, linenum,
            errno, strerror(errno), msg);
  exit(1);
}

//--------------------------------------------------------------------------

bool should_retard(int sockfd, const sockaddr *addr)
{
  int sock_type = -1;
  socklen_t opt_size = sizeof(sock_type);

  // retard only */IPv4
  // don't retard DNS
  if ((addr->sa_family != AF_INET) || 
      ((ntohs(((sockaddr_in*)addr)->sin_port) == DNS_PORT) && !g_retard_dns))
    return false;

  // retard only TCP/*
  getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &sock_type, &opt_size);
  return sock_type == SOCK_STREAM;
}

//--------------------------------------------------------------------------

void wait_for_data(int sock)
{
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  if (select(sock + 1, &fds, NULL, NULL, NULL) == -1)
    err("wait_for_data select");
}

//--------------------------------------------------------------------------

int transfer(int src_sock, int dst_sock)
{
  char buf[64 * 1024];
  int counter = 0, received;

  for (;;)
  {
    received = recv(src_sock, buf, sizeof(buf), MSG_DONTWAIT);
    if (received == 0) break;
    if (received < 0)
    {
      if (errno != EAGAIN) counter = 0; // maybe closed
      break;
    }
    counter += received;
    send(dst_sock, buf, received, 0);
  }

  return counter;
}

//===========================================================================

void *tcp_proxy_retarder(void *param)
{
  int sock = *static_cast<int*>(param);
  free(param);

  int dst_sock, conn_result, transfered;
  connect_params_t orig_conn_params;
  fd_set fds, working_fds;
  int fdmax;

  // receive original connect parameters
  wait_for_data(sock);
  recv(sock, (void*)&orig_conn_params, sizeof(orig_conn_params), 0);

  dst_sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  log(1, "realconnect fd=%i", orig_conn_params.sockfd);
  conn_result = realconnect(dst_sock, orig_conn_params.addr,
                            orig_conn_params.addrlen);

  // send the result back to the proxy master
  send(sock, (void*)&conn_result, sizeof(conn_result), 0);

  if (conn_result < 0)
  {
    realclose(sock);
    return NULL;
  }

  FD_ZERO(&fds);
  FD_SET(sock, &fds);
  FD_SET(dst_sock, &fds);
  if (sock > dst_sock) fdmax = sock; else fdmax = dst_sock;
  for(;;)
  {
    memcpy(&working_fds, &fds, sizeof(fds));
    if (select(fdmax + 1, &working_fds, NULL, NULL, NULL) == -1)
      err("tcp_proxy_retarder");

    // TODO allow asynchonous transfer/delays
    if (FD_ISSET(sock, &working_fds))
    {
      log(2, "tcp data cli->srv fd=%i", orig_conn_params.sockfd);
      random_sleep();
      transfered = transfer(sock, dst_sock);
      fd_pending_decrease(orig_conn_params.sockfd, transfered);
    } else {
      transfered = transfer(dst_sock, sock);
    }

    // something happened (socket closed, armagedon, ...)
    if (transfered == 0)
      break;
  }
  
  realclose(sock);
  realclose(dst_sock);
  return NULL;
}

//---------------------------------------------------------------------------

void *run_retarding_tcp_proxy(void *)
{
  pthread_t thread;
  int sock, incoming_sock, port;
  int optval, bind_res;
  sockaddr_in addr, incoming_addr;
  unsigned int incoming_addr_len;

  sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (sock < 0) err("run_retarding_tcp_proxy socket");

  optval = 1;
  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);

  // find free port
  for (port = PROXY_PORT_START; port <= PROXY_PORT_STOP; port++)
  {
    addr.sin_port = htons(port);
    bind_res = bind(sock, (sockaddr*)&addr, sizeof(addr));
    if (bind_res == 0) break;
    if (errno != EADDRINUSE) err("run_retarding_tcp_proxy bind");
  }
  if (bind_res < 0) err("run_retarding_tcp_proxy bind no free port");

  if (listen(sock, 10) < 0) err("run_retarding_tcp_proxy listen");
  g_tcp_proxy_port = port;
  log(1, "g_tcp_proxy_port=%hi", port);

  for (;;)
  {
    incoming_addr_len = sizeof(incoming_addr);
    log(1, "tcp proxy accept fd=%i", sock);
    incoming_sock = accept(sock, (sockaddr*)&incoming_addr, 
                      &incoming_addr_len);
    if (incoming_sock < 0)
    {
      realclose(sock);
      err("run_retarding_tcp_proxy accept");
    }

    int *p_incoming_sock = new int(incoming_sock);
    assert(p_incoming_sock != NULL);
    pthread_create(&thread, NULL, tcp_proxy_retarder,
                    static_cast<void*>(p_incoming_sock));
  }

  return NULL;
}

//---------------------------------------------------------------------------

void *delay_sendto(void *_params)
{
  sendto_params_t *params = (sendto_params_t *)_params;
  log(2, "sendto fd=%i waiting", params->sockfd);
  random_sleep();
  realsendto(params->sockfd, &(params->buf), params->buf_len, params->flags, 
        (sockaddr*)&(params->addr), params->addrlen);
  fd_pending_decrease(params->sockfd, params->buf_len);
  log(2, "sendto fd=%i performed", params->sockfd);
  free(params);
  return NULL;
}

//===========================================================================
#ifdef __cplusplus
extern "C" {
#endif

int connect(int sockfd, const sockaddr *addr, socklen_t addrlen)
{
  sockaddr_in proxy_addr;
  connect_params_t params;
  int remote_conn_result;
  int orig_flags;

  int sock_type = -1;
  socklen_t opt_size = sizeof(sock_type);
  getsockopt(sockfd, SOL_SOCKET, SO_TYPE, &sock_type, &opt_size);

  log(1, "connect fd=%i SO_TYPE:%i %s:%i", sockfd, sock_type,
      inet_ntoa(((sockaddr_in*)addr)->sin_addr),
      ntohs(((sockaddr_in*)addr)->sin_port));

  if (!should_retard(sockfd, addr))
  {
    log(1, "connect no retarding fd=%i", sockfd);
    return realconnect(sockfd, addr, addrlen);
  }

  log(1, "connecting to %s:%i",
              inet_ntoa(((sockaddr_in*)addr)->sin_addr),
              ntohs(((sockaddr_in*)addr)->sin_port));

  random_sleep();

  // wait for the proxy listen
  while (g_tcp_proxy_port == 0) {}

  // store nonblocking flag and set temporarily to blocking for
  // the realconnect() to the proxy
  orig_flags = fcntl(sockfd, F_GETFL, 0);
  fcntl(sockfd, F_SETFL, orig_flags & ~O_NONBLOCK);

  memset(&proxy_addr, 0, sizeof(proxy_addr));
  proxy_addr.sin_family = AF_INET;
  proxy_addr.sin_port = htons(g_tcp_proxy_port);
  inet_pton(AF_INET, "127.0.0.1", &(proxy_addr.sin_addr));
  if (realconnect(sockfd, (sockaddr*)&proxy_addr, sizeof(proxy_addr)) < 0)
    err("connect to proxy");

  // restore flags
  fcntl(sockfd, F_SETFL, orig_flags);

  // pass the original parameters to the proxy and wait for result
  params.sockfd = sockfd;
  params.addr = (sockaddr*)addr;
  params.addrlen = addrlen;
  send(sockfd, (void*)&params, sizeof(params), 0);

  fd_pending_insert(sockfd);

  wait_for_data(sockfd);
  if (recv(sockfd, (void*)&remote_conn_result, sizeof(remote_conn_result), 0) < 0)
    err("connect recv remote_conn_result");
  return remote_conn_result;
}

//---------------------------------------------------------------------------

ssize_t sendto(int sockfd, const void *buf, size_t len, int flags,
                      const sockaddr *addr, socklen_t addrlen)
{
  sendto_params_t *params;
  pthread_t thread;

  // handle only UDP/IPv4
  if ((addr->sa_family != AF_INET) ||
      (addr == NULL) ||
      ((ntohs(((sockaddr_in*)addr)->sin_port) == DNS_PORT) && !g_retard_dns))
  {
    return realsendto(sockfd, buf, len, flags, addr, addrlen);
  }

  // TODO make it more like the TCP proxy because this will fail
  // if the sockfd is destroyed before the realsendto is called

  params = (sendto_params_t *)malloc(sizeof(sendto_params_t) + len);
  params->sockfd = sockfd;
  params->flags = flags;
  memcpy(&(params->addr), addr, sizeof(sockaddr_in));
  params->addrlen = addrlen;
  params->buf_len = len;
  memcpy(params->buf, buf, len);
  fd_pending_increase(sockfd, len);
  pthread_create(&thread, NULL, delay_sendto, params);

  return len;
}

//---------------------------------------------------------------------------

ssize_t sendmsg(int sockfd, const msghdr *msg, int flags)
{
  // TODO make this function non-blocking
  log(2, "sendmsg fd=%i waiting", sockfd);
  random_sleep();
  log(2, "sendmsg fd=%i performed", sockfd);
  return realsendmsg(sockfd, msg, flags);
}

//---------------------------------------------------------------------------

ssize_t send(int sockfd, const void *buf, size_t len, int flags)
{
  fd_pending_increase_present(sockfd, len);
  return realsend(sockfd, buf, len, flags);
}

//---------------------------------------------------------------------------

int close(int fd)
{
  fd_pending_wait_remove(fd);
  return realclose(fd);
}

#ifdef __cplusplus
}
#endif
//===========================================================================

void load_params()
{
  // load params from environment variables
  char *buf;

  buf = getenv("SOCKET_RETARDER_DNS");
  if (buf != NULL) g_retard_dns = strcmp(buf, "1") == 0;

  buf = getenv("SOCKET_RETARDER_DISTRIBUTION");
  if (buf != NULL)
  {
    if (strcmp(buf, "uniform") == 0)
      g_distribution = DISTRIB_UNIFORM;
  }

  buf = getenv("SOCKET_RETARDER_NORMALDIST_MEAN");
  if (buf != NULL) g_normaldist_mean = atoi(buf);

  buf = getenv("SOCKET_RETARDER_NORMALDIST_VARIANCE");
  if (buf != NULL) g_normaldist_variance = atoi(buf);

  buf = getenv("SOCKET_RETARDER_UNIFORMDIST_A");
  if (buf != NULL) g_uniformdist_a = atoi(buf);

  buf = getenv("SOCKET_RETARDER_UNIFORMDIST_B");
  if (buf != NULL) g_uniformdist_b = atoi(buf);

  buf = getenv("SOCKET_RETARDER_DEBUG");
  if (buf != NULL) g_debug_level = atoi(buf);

  log(1, "g_retard_dns=%i", g_retard_dns);

  if (g_distribution == DISTRIB_NORMAL)
      log(1, "normal distribution: g_normaldist_mean=%i g_normaldist_variance=%i",
              g_normaldist_mean, g_normaldist_variance);
  else
      log(1, "uniform distribution: g_uniformdist_a=%i g_uniformdist_b=%i",
              g_uniformdist_a, g_uniformdist_b);
}

//---------------------------------------------------------------------------

static void wrap_init(void) __attribute__((constructor));
static void wrap_init(void)
{
  pthread_t thread;

  realconnect = (pconnect_t)dlsym(RTLD_NEXT, "connect");
  realsendto = (psendto_t)dlsym(RTLD_NEXT, "sendto");
  realsendmsg = (psendmsg_t)dlsym(RTLD_NEXT, "sendmsg");
  realsend = (psend_t)dlsym(RTLD_NEXT, "send");
  realclose = (pclose_t)dlsym(RTLD_NEXT, "close");

  load_params();
  pthread_create(&thread, NULL, run_retarding_tcp_proxy, NULL);
}
