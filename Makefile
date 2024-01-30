COPT:=-g
server: server.c
	gcc ${COPT} -o $@ $^ -lpthread

epoll_server:
	gcc ${COPT} epoll_server.c -o epoll_server 

clean: 
	rm epoll_server