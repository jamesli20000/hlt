# hlt
an http-load-test tool utilizing epoll and epollet.
It supports http GET and POST.

How to Compile:   gcc main.c

usage:
  http_load_test [option][value]
 -c concurrent
 -n total_request
 -s server
 -p port n 
 -u url
 -v verbose 
 -t method type 0 GET/1 POST
 -f external file

example:
  ./a.out -c 2000 -n 2000 -s 127.0.0.1 -p 8080 -u /index.html   
