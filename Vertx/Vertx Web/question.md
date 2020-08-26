## 用来记录需要做出的 Verticle 的功能

1. 使用 router, 客户端在访问 localhost:80 的时候, 网页输出 hello world
2. 使用多个 router, 客户端在访问 localhost/some/path/ 的时候, 网页可以同时输出多个 router 的 handler 响应.
3. 从 path 中提取参数, 从 get 方法的请求中提取参数
4. 限定router处理的请求的 content-type 为 text/html 的请求
5. 限定 router 可以提供 application/json 的响应
6. 将当前的请求转发给另一个 router, 带参数
7. 使用 Event Bus 在不同 Verticle 之间传递信息
