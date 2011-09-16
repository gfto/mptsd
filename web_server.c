#include <stdlib.h>
#include <regex.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "libfuncs/libfuncs.h"

#include "web_pages.h"
#include "web_server.h"

typedef struct req_info {
	int clientsock;
	struct sockaddr_in client;
} request_info;

extern int keep_going;

#define NEXT_CLIENT { FREE(path); FREE(buf); pthread_exit(0); }
#define SHUTDOWN_CLIENT { FREE(path); FREE(buf); shutdown_fd(&clientsock); pthread_exit(0); }
#define BUF_SIZE 1024

void *process_web_request(void *);

void *web_server_thread(void *data) {
	CONFIG *conf = data;
	while (keep_going) {
		struct sockaddr_in client;
		unsigned int clientlen = sizeof(client);
		int clientsock;
		clientsock = accept(conf->server_socket, (struct sockaddr *) &client, &clientlen);
		if (clientsock < 0) {
			if (conf->server_socket > -1)	// The server_socket is closed on exit, so do not report errors
				LOGf("ERROR : Failed to accept client fd: %i err: %s\n", clientsock, strerror(errno));
			if (errno==EMFILE || errno==ENFILE) /* No more FDs */
				break;
		} else {
			request_info *req;
			pthread_t req_thread;
			req = malloc(sizeof(request_info));
			if (!req) {
				log_perror("Can't allocate request_info", errno);
				continue;
			}
			req->clientsock = clientsock;
			req->client = client;
			if (pthread_create(&req_thread, NULL, (void *)&process_web_request, (void *)req)) {
				log_perror("Error creating request processing thread.", errno);
				exit(1);
			}
			pthread_detach(req_thread);
		}
	}

	pthread_exit(0);
}

void web_server_start(CONFIG *conf) {
	if (conf->server_socket > -1)
		pthread_create(&conf->server_thread, NULL, &web_server_thread, conf);
}

void web_server_stop(CONFIG *conf) {
	if (conf->server_socket > -1) {
		shutdown_fd(&conf->server_socket);
		pthread_join(conf->server_thread, NULL);
	}
}

void *process_web_request(void *in_req) {
	request_info *req = (request_info *)in_req;
	int clientsock = req->clientsock;
	regmatch_t res[3];
	char *path=NULL, *buf=NULL;
	FREE(req);

	signal(SIGPIPE, SIG_IGN);

	if (!keep_going)
		pthread_exit(0);

	buf = malloc(BUF_SIZE);
	if (!buf) {
		log_perror("Can't allocate buffer", errno);
		SHUTDOWN_CLIENT;
	}

	if (fdgetline(clientsock,buf,BUF_SIZE)<=0) {
		SHUTDOWN_CLIENT;
	}

	regex_t request_get;
	regcomp(&request_get, "^GET /([^ ]*) HTTP/1.*$", REG_EXTENDED);
	if (regexec(&request_get,buf,2,res,0)==REG_NOMATCH) {
		send_501_not_implemented(clientsock);
		SHUTDOWN_CLIENT;
	}

	buf[res[1].rm_eo]=0;
	chomp(buf+res[1].rm_so);
	if (buf[res[1].rm_eo-1]=='/') buf[res[1].rm_eo-1]=0;
	path = strdup(buf+res[1].rm_so);
	regfree(&request_get);

	while (fdgetline(clientsock,buf,BUF_SIZE) > 0) {
		if (buf[0] == '\n' || buf[0] == '\r') // End of headers
			break;
	}

	if (strlen(path) == 0) {
		cmd_index(clientsock);
	} else if (strstr(path,"reconnect")==path) {
		cmd_reconnect(clientsock);
	} else {
		send_404_not_found(clientsock);
	}

	SHUTDOWN_CLIENT;
}

