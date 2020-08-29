package com.pingzi.web

import io.vertx.core.AsyncResult
import io.vertx.core.Future
import io.vertx.core.Handler
import io.vertx.core.Promise
import java.util.function.Function
import kotlin.concurrent.thread

fun main() {
    hiAsync("Begin")
        .compose {result ->
            println("${Thread.currentThread().name}, End Actual")
            Future.succeededFuture<String>()
        }
}

private fun hiAsync(name: String): Future<String> {
    val promise = Promise.promise<String>()
    hiAsync(name, promise)
    return promise.future()
}

private fun hiAsync(name: String, handler: Handler<AsyncResult<String>>) {
    thread(name = name) {
        println("${Thread.currentThread().name}, $name")
        Thread.sleep(1000)
        handler.handle(Future.succeededFuture("Hi, $name"))
    }
}
