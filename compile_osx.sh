export LDFLAGS="-L/usr/local/lib -L/usr/local/Cellar/readline/6.3.8/lib"
export CFLAGS="-I/usr/local/include -I/usr/local/Cellar/readline/6.3.8/include"

./configure --with-openssl=/usr/local/opt/openssl --disable-liblua
make
