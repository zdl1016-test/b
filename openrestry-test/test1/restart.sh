#PATH=/usr/local/openresty/nginx/sbin:$PATH
#export PATH

openresty -s reload -p `pwd`/ -c conf/nginx.conf
