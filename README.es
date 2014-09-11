## Telegram messenger CLI [![Build Status](https://travis-ci.org/vysheng/tg.png)](https://travis-ci.org/vysheng/tg)

!!!! May be outdated. Please refer to english README as more actual. 


Interfaz de línea de comandos para: [Telegram](http://telegram.org). Usa interfaz readline.

### Documentación del API y el protocolo

La documentación del APi de Telegram está disponible aquí: http://core.telegram.org/api

La documentación del protocolo MTproto está disponible aquí: http://core.telegram.org/mtproto

### Instalación

Clona el Repositorio GitHub

    $ git clone https://github.com/vysheng/tg.git && cd tg
    
o descarga y descomprime el zip

    $ wget https://github.com/vysheng/tg/archive/master.zip -O tg-master.zip
    $ unzip tg-master.zip && cd tg-master
    
#### Linux

Librerías requeridas: readline openssl y (si desea usar config) libconfig y liblua.
Si no deseas usarlo, pasa las siguientes opciones --disable-libconfig y --disable-liblua respectivamente.

En Ubuntu usa:

    $ sudo apt-get install libreadline-dev libconfig-dev libssl-dev lua5.2 liblua5.2-dev

En gentoo:
    
    $ sudo emerge -av sys-libs/readline dev-libs/libconfig dev-libs/openssl dev-lang/lua
    
En Fedora:

    $ sudo yum install lua-devel openssl-devel libconfig-devel readline-devel

Por defecto Makefile utiliza liblua5.2 de ubuntu. Si utilizas una versión diferente de liblua o linux, tienes que ejecutar el script ./configure o recibirás un error de compilación extraño.

Entonces

    $ ./configure
    $ make


#### Mac OS X

El cliente depende de [librería readline](http://cnswww.cns.cwru.edu/php/chet/readline/rltop.html) y [libconfig](http://www.hyperrealm.com/libconfig/), las cuales no están incluídas en OS X por defecto. Debes instalar estas librerías de forma manual, usando por ejemplo [Homebrew](http://brew.sh/).

    $ brew install libconfig
    $ brew install readline
    $ brew install lua
    $ export CFLAGS="-I/usr/local/include -I/usr/local/Cellar/readline/6.2.4/include"
    $ export LDFLAGS="-L/usr/local/lib -L/usr/local/Cellar/readline/6.2.4/lib"
    $ ./configure && make
    
Gracias a [@jfontan](https://github.com/vysheng/tg/issues/3#issuecomment-28293731) por esta solución.

#### FreeBSD

Instalar estos puertos:

* devel/libconfig
* devel/libexecinfo

Entonces construir:

    $ env CC=clang CFLAGS=-I/usr/local/include LDFLAGS=-L/usr/local/lib ./configure
    $ make
    
#### Otros UNIX

Si logras ejecutarlo en otros UNIX, por favor házmelo saber.


### Uso

    ./telegram -k <clave-servidor-público>
    

Por defecto la clave pública se almacena en la misma carpeta con el nombre tg-server.pub o en /etc/telegram/server.pub, si no lo es, indica dónde encontrarlo:

    ./telegram -k tg-server.pub

El Cliente soporta completamiento con TAB e historial de comandos.

Peer se refiere al nombre del contacto o de diálogo y se puede acceder por completamiento con Tab.
Para los contactos de usuario el peer es el Nombre <guión bajo> Apellido con todos los espacios cambiados a guiones bajos.
Para los chats es su título con todos los espacios cambiados a guiones bajos.
Para los chats encriptados es <marca de exclamación> <guión bajo> Nombre <guión bajo> Apellido con todos los espacios cambiados a guiones bajos. 

Si dos o más peers tienen el mismo nombre, una almohadilla y un número es añadido al nombre. (por ejemplo A_B,A_B#1,A_B#2 y así sucesivamente).
  
### Comandos soportados

#### Mensajería


* **msg** \<peer\> texto - envía el mensaje a este usuario.
* **fwd** \<usuario\> \<numero-mensaje\> - reenviar un mensaje al usuario. Puedes ver los número de mensajes iniciando el Cliente con -N.
* **chat_with_peer** \<peer\> - inicia un chat con este usuario. /exit o /quit para salir de este modo.
* **add_contact** \<numero-teléfono\> \<nombre\> \<apellido\> - intenta añadir este contacto a la lista de contactos.
* **rename_contact** \<usuario\> \<nombre\> \<apellido\> - intenta renombrar el contacto. Si tienes otro dispositivo será una pelea.
* **mark_read** \<peer\> - marca todos los mensajes como recibidos de ese usuario.

#### Multimedia

* **send_photo** \<peer\> \<nombre-archivo-foto\> - manda una foto al usuario.
* **send_video** \<peer\> \<nombre-archivo-video\> - envia un video al usuario.
* **send_text** \<peer\> \<nombre-archivo-texto> - envia un archivo de texto como un mensaje en plano.
* **load_photo**/load_video/load_video_thumb \<numero-mensaje\> - carga foto/video indicado del directorio de descarga.
* **view_photo**/view_video/view_video_thumb \<numero-mensaje\> - carga foto/video indicado del directorio de descarga y lo abre con el visor por defecto del sistema.


#### Opciones de chat de grupo

* **chat_info** \<chat\> - imprime información del chat.
* **chat_add_user** \<chat\> \<usuario\> - agrega un usuario al chat.
* **chat_del_user** \<chat\> \<usuario\> - elimina un usuario del chat.
* **rename_chat** \<chat\> \<nuevo-nombre\> - cambia el nombre al chat.

#### Search

* **search** \<peer\> patrón - busca el patrón indicado en los mensajes con ese usuario.
* **global_search** patrón - busca el patrón indicado en todos los mensajes.

#### Chat secreto

* **create_secret_chat** \<user\> - crea un chat secreto con el usuario indicado.
* **visualize_key** \<secret_chat\> - Muestra la clave de cifrado. Debes compararla con la del otro usuario.

#### Estadísticas e información varia.

* **user_info** \<user\> - muestra información sobre el usuario.
* **history** \<peer\> [limit] - muestra el historial (y la marca como leído). Limite por defecto = 40.
* **dialog_list** - muestra información acerca del dialogo
* **contact_list** - muestra información acerca de tu lista de contactos.
* **suggested_contacts** - muestra información sobre sus contactos, tiene un máximo de amigos comunes.
* **stats** - solo para depuración.
* **show_license** - muestra la licencia GPLv2.
* **help** - imprime esta ayuda.
