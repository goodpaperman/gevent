#! /bin/sh
sudo apt-get install doxygen
sudo apt-get install graphviz

# to generate documents:
# doxygen gevent.doxygen

# after generate, insert a line:
#
#   MANDATORY_MANPATH			$PWD/man
#
# into /etc/manpath.config
# then you can see man pages by:
#
# man GEventBase
# man GEventHandler
# ...
