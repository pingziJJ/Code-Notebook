package com.pingzi.web

import io.vertx.core.*
import io.vertx.core.eventbus.DeliveryOptions
import io.vertx.core.eventbus.Message
import io.vertx.core.http.HttpMethod
import io.vertx.core.json.JsonObject
import io.vertx.ext.auth.shiro.ShiroAuth
import io.vertx.ext.auth.shiro.ShiroAuthRealmType
import io.vertx.ext.web.Router
import io.vertx.ext.web.handler.*
import io.vertx.ext.web.sstore.LocalSessionStore
import io.vertx.ext.web.sstore.SessionStore


fun main() {
    val vertx = Vertx.vertx()
    vertx.deployVerticle(RerouterVerticle())
}

class HelloWorldVerticle : AbstractVerticle() {

    override fun start(startPromise: Promise<Void>?) {

        val server = vertx.createHttpServer()

        server.requestHandler { request ->
            val response = request.response()
            response.putHeader("content-type", "text/plain")

            response.end("Hello world")
        }

        server.listen(8080)
    }
}

class SimpleRouterVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        router.route().handler { context ->
            val response = context.response()
            response.putHeader("content-type", "text/plain")
            response.end("Hello from simple router verticle")
        }

        server.requestHandler(router).listen()
    }
}

class MutilRouterVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        router.route("/some/path/").handler { context ->
            val response = context.response()
            response.isChunked = true

            response.write("route1\n")

            context.vertx().setTimer(5000) { _ -> context.next() }
        }

        router.route("/some/path/").handler { context ->
            val response = context.response()
            response.write("router2")

            context.vertx().setTimer(5000) { _ -> context.next() }
        }

        router.route("/some/path/").handler { context ->
            val response = context.response()
            response.write("router3")

            response.end()
        }

        server.requestHandler(router).listen { result ->
            if (result.succeeded()) {
                println("succeed!")
            } else {
                println("failed: ${result.cause()}")
            }
        }
    }
}

class BlockingVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        var router = Router.router(vertx)

        router.route().blockingHandler { context ->
            Thread.sleep(5000)
            context.response().end("Hello world")
        }

        server.requestHandler(router).listen()
    }
}

class PathVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        // 访问 /some/path /some/path/ 或者 /some/path// 都会走这个 Handler
        router.route().path("/some/path").handler { context ->
            context.response().end("Hello world")
        }

        server.requestHandler(router).listen()
    }
}

class PrefixVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        // 所有 /some/path/ 开头的请求都会调用这个处理器
        router.route().path("/some/path/*").handler { context ->
            context.response().putHeader("content-type", "text/paint")
            context.response().end("Prefix Verticle")
        }

        server.requestHandler(router).listen()
    }
}

class PathParametersVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        router.route(HttpMethod.GET, "/catalogue/products/:producttype/:productid")
            .handler {
                it.response()
                    .putHeader("content-type", "text/paint")
                    .end(
                        """
                        type: ${it.request().getParam("producttype")}
                        id: ${it.request().getParam("productid")}
                    """.trimIndent()
                    )
            }
        server.requestHandler(router).listen {
            if (it.succeeded()) {
                println("Succeeded")
            } else {
                println("error: ${it.cause()}")
            }
        }
    }
}

class MIMETypeRouterVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        router.route().consumes("text/html").handler { context ->
            // 只能处理 content-type 头为 text/html 的请求
        }

        router.route().consumes("text/*").handler { context ->
            // 会处理所有 content-type 头以 text 开头的请求
        }

        router.route().consumes("*/json").handler { context ->
            // 会处理所有 Json
        }

        server.requestHandler(router).listen()
    }
}

class ProducesVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        // 可以提供 application/json 的 content-type 类型
        router.route().produces("application/json").handler { }

        server.requestHandler(router).listen()
    }
}

class RerouterVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val server = vertx.createHttpServer()
        val router = Router.router(vertx)

        router.route("/some").handler { context ->
            println("Handler1")
            context.put("param", "value")
            context.reroute("/some/path")
        }

        router.route("/some/path").handler { context ->
            val value = context.get<String>("param")
            println("Handler2: $value")
            context.response()
                .putHeader("content-type", "text/paint")
                .end("param: $value")
        }

        server.requestHandler(router).listen()
    }
}

class FailureHandlerVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val router = Router.router(vertx)

        router.get("/somepath/path1").handler { context ->
            println("somepath 1")
            throw RuntimeException("something happened!") // 这里直接出异常的 code 不是 500, 而是没有设置的 -1
        }

        router.get("/somepath/path2").handler { context ->
            println("somepath 2")
            context.fail(403)
        }

        router.get("/somepath/*").failureHandler { context ->
            val code = context.statusCode()
            println("failure handler $code")
            context.response().setStatusCode(code).end("Sorry, Not today")
        }

        vertx.createHttpServer().requestHandler(router).listen()
    }
}

class UploadVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val router = Router.router(vertx)

        router.route().handler(BodyHandler.create())
        router.post("/some/path/uploads").handler { context ->
            val fileUploads = context.fileUploads()
            // 执行处理上传
            fileUploads.forEach { upload ->
            }
        }

        vertx.createHttpServer().requestHandler(router).listen()
    }
}

class CookieVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val router = Router.router(vertx)

        router.route().handler(CookieHandler.create()) // 在新版本的 vertx 中, 默认提供 cookie 支持, 这句话已经没有效果了

        // 操作 cookie
        router.route("/some/path").handler { routingContext ->
            val cookie = routingContext.getCookie("myCookie")
            // 执行某些操作
            routingContext.addCookie(io.vertx.core.http.Cookie.cookie("name", "value"))
        }

        vertx.createHttpServer().requestHandler(router).listen()
    }
}

class SessionVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val router = Router.router(vertx)

        router.route().handler(SessionHandler.create(SessionStore.create(vertx)))
        router.route().handler { context ->
            val session = context.session()

            session.put("foo", "bar")
            session.get<String>("age")

            session.remove("obj")
        }

        vertx.createHttpServer().requestHandler(router).listen()
    }
}

class HttpAuthVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val router = Router.router(vertx)

        router.route().handler(BodyHandler.create())
        router.route().handler(SessionHandler.create(LocalSessionStore.create(vertx)))

        // 配置一个 user session handler, 实现了用户认证并存储 session 的过程
        val authProvider = ShiroAuth.create(vertx, ShiroAuthRealmType.PROPERTIES, JsonObject())
        router.route().handler(UserSessionHandler.create(authProvider))

        // 设置 /private 目录下的所有资源都需要认证, 如果用户未登录就跳转到 loginpath.html.
        // 设置 /private 目录下的所有资源都是静态资源, vertx 不缓存请求头, 并且目录地址是 private 目录
        router.route("/private/*").handler(RedirectAuthHandler.create(authProvider, "/loginpath.html"))
        router.route("/private/*").handler(StaticHandler.create().setCachingEnabled(false).setWebRoot("private"))

        // 登录的 handler
        router.route("/loginhandler").handler(FormLoginHandler.create(authProvider))

        router.route("/logout").handler { context ->
            context.clearUser()
            context.response().putHeader("location", "/")
                .setStatusCode(302)
                .end()
        }

        router.route().handler(StaticHandler.create())
        vertx.createHttpServer().requestHandler(router).listen()
    }
}

class EventBusVerticle : AbstractVerticle() {
    override fun start(startPromise: Promise<Void>?) {
        val eventBus = vertx.eventBus()
        eventBus.consumer<String>("news.uk.support") { message ->
            println("I have receive a message: ${message.body()}")
        }.completionHandler { result ->
            if (result.succeeded()) {
                println("组件注册完成")
            } else {
                println("组件注册失败")
            }
        }

        eventBus.publish("news.uk.support", "所有的处理器都能收到这个消息")
        eventBus.send("news.uk.support", "只有一个处理器会受到这个消息")
        val options = DeliveryOptions()
        options.addHeader("some-header", "some-value")
        eventBus.send("news.uk.support", "只有一个处理器能收到消息", options)
        eventBus.publish("news.uk.support", "所有处理器都能收到消息", options)

        // 应答
        eventBus.consumer<String>("news.uk.support") { message ->
            val body = message.body()
            message.reply("我收到了 ${message.body()}")
        }
        eventBus.send("news.uk.support", "所有处理器都能收到") { result:AsyncResult<Message<String>> ->
            if (result.succeeded()) {
                println(result.result().body())
            }
        }
    }
}
