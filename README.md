# Apache2 grpc proxy plugin
This plugin allows proxying grpc-web requests to a grpc backend server directly inside apache2 without the need to run an additional
proxy server like nginx or envoy.

While I did my best to make it stable and correct, I am neither an experienced apache2 plugin writer nor a grpc core developer.
I do use it on a couple of low traffic servers without any problems, however I definitly do not recommend using it in production
without a proper check of the source.

If you find any issues feel free to open an issue or create a Pull Request.

## Balancing
The plugin supports running in a balancing config, checkout `httpd-balancer.conf` for a minimal example.

## TLS/ALTS
Encryption is not yet supported for backend servers.

## Building
The plugin uses the cmake build system and relies on FetchContent to import and compile grpc.
It links both libstdc++ and grpc statically into the shared object. While this increases the size a bit it prevents issues due to missmatched grpc libraries and allows deployment to servers without libgrpc installed. It also makes it easier to get a consistent build on different operating system version. From a user perspective, the build is the same as every cmake build.

```
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j `nprocs`
```

After the build is done you will have mod_proxy_grpc.so in your build folder.

## Installing

`make install`