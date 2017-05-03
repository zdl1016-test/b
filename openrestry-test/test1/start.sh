#PATH=/usr/local/openresty/nginx/sbin:$PATH
#export PATH

openresty -p `pwd`/ -c conf/nginx.conf
