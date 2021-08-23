# gevent
cross-platform event driven framework (made in cpp)， to get detail: [一个工业级、跨平台、轻量级的 tcp 网络服务框架：gevent ](https://www.cnblogs.com/goodcitizen/p/12349909.html)
# build
## win32
cd demo
cmake -G "Visual Studio 14 2015" -S .
# then using VS2015 open epoll_svc.sln and build all
## linux / mac
mkdir demo/build
cd demo/build
cmake ..
make
# output
goes into $gevent/bin
