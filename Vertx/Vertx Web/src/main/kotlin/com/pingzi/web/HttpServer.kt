package com.pingzi.web

import io.vertx.core.Vertx

// Direct Server
fun main() {
    val vertx = Vertx.vertx()
    val server = vertx.createHttpServer()

    server.requestHandler{request ->
        println(Thread.currentThread().name)
        request.response()
            .putHeader("content-type", "text/plain")
            .end("Hello Direct Server")
    }.listen()
}
