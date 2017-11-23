// 网络连接相关

#include <string.h>
#include <errno.h>

#include "app.h"
#include "msgbuf.h"

extern appServer app;

/* =========================== c call lua =========================== */
void cl_handle_msg(int fd, msgPack * pkt){
	// lua_handleMsg(fd,cmd,fromTy,fromId,toTy,toId,pkt);
	lua_State *L = app.L;
	lua_getglobal(L, "cl_handleMsg");
	lua_pushinteger(L, fd);
	lua_pushinteger(L, pkt->cmd);
	lua_pushinteger(L, pkt->fromType);
	lua_pushinteger(L, pkt->fromId);
	lua_pushinteger(L, pkt->toType);
	lua_pushinteger(L, pkt->toId);
	lua_pushlstring(L, pkt->buf, pkt->len - sizeof(msgPack));
	lua_pcall(L,7,0,0);
}

void cl_accpeted(int fd){

}

/* =========================== static func =========================== */
static int readMsgbuff(netSession * session){
	int readlen = 0;
	msgBuf * input = session->input;
	unsigned char * buf = getFreeBuf(input, &readlen);
	assert(buf);

	int nread = read(session->fd, buf, readlen);
	if (nread < 0) {
		if (errno == EAGAIN) {
			return 0;
		} else {
			serverLog(LL_VERBOSE, "Reading from session: %s",strerror(errno));
			return -1;
		}
	} else if (nread == 0) {
		serverLog(LL_VERBOSE, "Client closed connection");
		return -1;
	}
	input->size += nread;
	return nread;
}

static int writeMsgbuff(netSession * session){
	int fd = session->fd;
	if (fd<0) return -1;

	int left = session->output->size;
	unsigned char * head = session->output->buf;
	int wlen = 0;
	while(left>0){
		int nwritten = anetWrite(fd, head+wlen, left);
		if (nwritten>0){
			left -= nwritten;
			wlen += nwritten;
		}else if (nwritten<0){
			if(errno==EAGAIN){
				break;
			}else if(errno==EINTR){
				continue;
			}else{
				// socket有问题了
				return -1;
			}
		}else{
			return -2;
		}
	}
	return left;
}

/* =========================== file event callback =========================== */
static void readFromSession(aeEventLoop *el, int fd, void *privdata, int mask) {
	netSession * session = getSession(fd);
	if (session == NULL) {
		serverLog(LL_VERBOSE, "no session");
		return;
	}

	// 处理buf
	int nread = readMsgbuff(session);
	if (nread>0){
		int headLen = sizeof(msgPack);
		msgBuf * input = session->input;
		unsigned char * head = input->buf;
		int left = input->size;
		while(left >= headLen){
			msgPack * pkt = (msgPack *)head;
			assert(pkt->len >= headLen); // 检查len是否合法
			if (pkt->len > input->size){
				/* 不够一个包 */
				break;
			}else{
				head += pkt->len;
				left -= pkt->len;
			}
			cl_handle_msg(fd, pkt);
		}
		readBuf(input, input->size - left);
	}else if (nread<0){
		closeSession(session);
	}
}

static void writeToSession(aeEventLoop *el, int fd, void *privdata, int mask) {
	netSession * session = getSession(fd);
	if (session){
		flushSession(session);
	}
}

static void acceptTcpHandler(aeEventLoop *el, int fd, void *privdata, int mask) {
	int cport;
	char cip[64];

	int cfd = anetTcpAccept(app.err, fd, cip, sizeof(cip), &cport);
	if (cfd==ANET_ERR){
		if (errno != EWOULDBLOCK){
			serverLog(LL_WARNING, "Accepting session connection: %s", app.err);
		}
		return;
	}

	printf("Accepted %s:%d \n", cip, cport);
	// 处理接收到fd
	netSession * session = createSession(cfd, cip, cport);
	if (session==NULL){
		serverLog(LL_WARNING, "create session fail: %d", cfd);
		return;
	}

	// 告诉lua层
	cl_accpeted(fd);
}

/* =========================== net api =========================== */
netSession * createSession(int fd, const char *ip, short port){
	if (fd>=app.maxSize){
		serverLog(0, "create session fail, fd = %d, maxSize = %s", fd, app.maxSize);
		return NULL;
	}

	netSession * session = app.session[fd];
	if (session==NULL || session->fd>=0){
		serverLog(0, "create session fail, session = %p", session);
		return NULL;
	}

	if (fd != -1) {
		anetNonBlock(NULL,fd);	// 设置为非阻塞
		anetEnableTcpNoDelay(NULL,fd);

		if (app.tcpkeepalive){
			anetKeepAlive(NULL,fd,app.tcpkeepalive);
		}

		if (aeCreateFileEvent(app.pEl, fd, AE_READABLE, readFromSession, NULL) == AE_ERR){
			closeSession(session);
			return NULL;
		}
	}

	session->fd = fd;
	session->ip = ip;
	session->port = port;
	return session;
}

netSession * getSession(int fd){
	if (fd>=app.maxSize) return NULL;

	netSession * session = app.session[fd];
	if (session->fd != fd) return NULL;
	return session;
}

void flushSession(netSession * session){
	int left = writeMsgbuff(session);
	if (left<0){
		/* close socket */
		closeSession(session);
		return;
	}

	readBuf(session->output, session->output->size - left);
	int mask = aeGetFileEvents(app.pEl, session->fd);
	if (left==0){
		if (mask & AE_WRITABLE){
			aeDeleteFileEvent(app.pEl, session->fd, AE_WRITABLE);
		}
	}else if (left>0){
		if (!(mask & AE_WRITABLE)){
			aeCreateFileEvent(app.pEl, session->fd, AE_WRITABLE, writeToSession, NULL);
		}
	}
}

void closeSession(netSession * session){
	// 清空
	int fd = session->fd;
	if (fd>=0){
		aeDeleteFileEvent(app.pEl, fd, AE_READABLE|AE_WRITABLE);
	}

	session->port = 0;
	session->ip = NULL;
	session->fd = -1;

	cleanBuf(session->input);
	cleanBuf(session->output);
}

int netListen(int port, char * addr){
	int fd = anetTcpServer(app.err, port, addr, 511); // 监听的socket
	if (fd == ANET_ERR) {
		serverLog(LL_WARNING, "Creating Server TCP listening socket %s:%d: %s", addr ? addr : "*", port, app.err);
		return -1;
	}else{
		anetNonBlock(NULL, fd);

		// listen
		if (aeCreateFileEvent(app.pEl, fd, AE_READABLE, acceptTcpHandler, NULL) == -1){
			serverLog(LL_WARNING, "Create Listen File Event Fail...");
			return -1;
		}
		return fd;
	}
}

int netConnect(char * addr, int port){
	int fd = anetTcpConnect(app.err, addr, port);
	if (fd==ANET_ERR){
		serverLog(0, "net connect fail");
		return -1;
	}

	// event
	netSession * session = createSession(fd, addr, port);
	if (session==NULL){
		serverLog(LL_WARNING, "session create fail...");
		return -1;
	}
	return fd;
}

void netWrite(int fd, msgPack * pkt){
	netSession * session = getSession(fd);
	if(session==NULL){
		serverLog(0,"session not found,fd = %d", fd);
		return;
	}

	writeToBuf(session->output, (unsigned char *)pkt, pkt->len);
	flushSession(session);
}