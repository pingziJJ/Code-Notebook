package com.pingzi.web

import io.vertx.config.ConfigRetriever
import io.vertx.config.ConfigRetrieverOptions
import io.vertx.config.ConfigStoreOptions
import io.vertx.config.spi.ConfigProcessor
import io.vertx.config.spi.ConfigStore
import io.vertx.config.spi.ConfigStoreFactory
import io.vertx.core.AsyncResult
import io.vertx.core.Future
import io.vertx.core.Handler
import io.vertx.core.Vertx
import io.vertx.core.buffer.Buffer
import io.vertx.core.json.JsonObject
import java.util.*

fun main() {
   /* val vertx = Vertx.vertx()

    val storeOption = ConfigStoreOptions()
        .setType("file")
        .setConfig(JsonObject().put("path", "data.json"))

    val options = ConfigRetrieverOptions()
        .addStore(storeOption)

    val retriever = ConfigRetriever.create(vertx, options)
    retriever.getConfig{handler ->
        if (handler.succeeded()) {
            val result = handler.result()
            println(result.encodePrettily())
        }
    }*/

    val vertx = Vertx.vertx()
    val options = ConfigStoreOptions()
        .setType("test")
        .setFormat("test")
        .setConfig(JsonObject().put("node", "store"))
    val retrieverOptions = ConfigRetrieverOptions()
        .addStore(options)

    val retriever = ConfigRetriever.create(vertx, retrieverOptions)
    retriever.getConfig {
        if (it.succeeded()) {
            val result = it.result()
            println(result.encodePrettily())
        }
    }
}

class TestProcessor: ConfigProcessor{
    override fun name() = "test"

    override fun process(
        vertx: Vertx,
        configuration: JsonObject,
        input: Buffer,
        handler: Handler<AsyncResult<JsonObject>>
    ) {
        val data = input.toJsonObject()
        data.put("processor", "执行 Processor")
        handler.handle(Future.succeededFuture(data))
    }

}

class TestStore(configuration: JsonObject) : ConfigStore{

    private val configuration: JsonObject

    init {
        if (Objects.isNull(configuration)) {
            this.configuration = JsonObject()
        } else {
            this.configuration = configuration
        }
    }

    override fun get(completionHandler: Handler<AsyncResult<Buffer>>) {
        println("传入配置: ${configuration.encodePrettily()}")
        val output = JsonObject()
        val node = configuration.getString("node")
        if (Objects.nonNull(node)) {
            output.put(node, "测试配置输出")
        }
        completionHandler.handle(Future.succeededFuture(output.toBuffer()))
    }
}

class TestStoreFactory: ConfigStoreFactory {
    override fun name() = "test"

    override fun create(vertx: Vertx, configuration: JsonObject): ConfigStore {
        return TestStore(configuration)
    }
}
