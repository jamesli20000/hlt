# hlt
an http-load-test tool utilizing epoll and epollet.
It supports http GET and POST.

How to Compile:   gcc main.c

usage:
  http_load_test [option][value] </br>
 -c concurrent </br>
 -n total_request </br>
 -s server </br>
 -p port n  </br>
 -u url </br>
 -v verbose  </br>
 -t method type 0 GET/1 POST </br>
 -f external file </br>

example:
  ./a.out -c 2000 -n 2000 -s 127.0.0.1 -p 8080 -u /index.html   

more detail could be found at:
 http://www.jamesli20000.com/wordpress/a-smart-tool-to-test-http-load
