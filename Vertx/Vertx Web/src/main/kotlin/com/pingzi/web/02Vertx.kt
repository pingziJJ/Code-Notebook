package com.pingzi.web

import io.netty.buffer.Unpooled
import io.vertx.core.AbstractVerticle
import io.vertx.core.AsyncResult
import io.vertx.core.buffer.Buffer
import io.vertx.core.eventbus.Message
import io.vertx.core.json.JsonObject
import io.vertx.core.shareddata.impl.ClusterSerializable
import java.nio.CharBuffer
import java.nio.charset.Charset

// standard 用于接收请求. worker 用于执行阻塞任务. 二者使用 EventBus 通信
// 如果是少量阻塞任务, 可以考虑使用 executeBlocking 方法代替 worker

// 前端拦截器 和 前端过滤器
//  - 使用 API WEB Contract 做基础的数据验证和数据合约验证
//  - 使用 Vertx Web 做前端验证和前端过滤. 设置 Router 中的 order 绑定优先级更高的 Handler.
//  - 自己开发基于 vertx 的过滤器和拦截器

// 发布过程中, worker 类型的 verticle 和 standard 类型的不必须对等
// event bus 传输数据的时候必要的情况下可以自定义 codec

val strings: Array<String> = arrayOf(
    "A random string value",
    "The product of an infinite number of monkeys",
    "Hey hey we're the Monkees",
    "Opening act for the Monkees: Jimi Hendix",
    "'Scuse me while I kiss this fly'",
    "Help Me! Help Me!"
)

var index = 0

fun drainBuffer(buffer: CharBuffer) {
    while (buffer.hasRemaining()) {
        println(buffer.get())
    }

    println("  ")
}

fun fillBuffer(buffer: CharBuffer): Boolean {
    if (index >= strings.size) {
        return false
    }

    val string = strings[index++]
    for (element in string) {
        buffer.put(element)
    }

    return true
}

fun main() {
    /*val buffer = Unpooled.buffer(16)
    for (i in 0..15) {
        buffer.writeByte(i)
    }

    val result = buffer.readBytes(5)
    println(result)

    for (i in 0..4) {
        print("" + result.getByte(i) + ",")
    }
    println()
    println(result)
    println("" + buffer.readerIndex() + ", " + buffer.writerIndex())*/

    // buffer discard
/*    val buffer = Unpooled.buffer(16)
    for (i in 0..15) {
        buffer.writeByte(i)
    }

    val subBuffer = buffer.readBytes(7)

    println(buffer)
    val created = buffer.discardReadBytes()
    println("Buffer: " + buffer)
    println("Sub: " + subBuffer)
    println("Discard: " + created)*/

    // vertx buffer
    // 创建空 Buffer
    val buffer = Buffer.buffer()
    println("空 Buffer: " + buffer.toString() + ", " + buffer.length())
    // 字节数组的创建
    val helloWorld = "hello world"
    val byteBuffer = Buffer.buffer(helloWorld.toByteArray())
    println("使用字节数组创建: " + byteBuffer.toString() + ", " + byteBuffer.length())
    // 字符串创建
    val stringBuffer = Buffer.buffer(helloWorld)
    println("使用字符串创建: " + stringBuffer.toString() + ", " + stringBuffer.length())
    // 带编码的字符串创建
    val stringEncodeBuffer = Buffer.buffer(helloWorld, "utf-8")
    println("使用带编码的字符串Buffer: " + stringEncodeBuffer.toString() + ", " + stringEncodeBuffer.length())
    // 直接使用 byteBuffer
    val nettyBuffer = Unpooled.buffer(16)
    for (i in 0..15) {
        nettyBuffer.writeByte(i)
    }
    val defaultBuffer = Buffer.buffer(nettyBuffer)
    println("使用 nettyBuffer: " + defaultBuffer.toString() + ", " + defaultBuffer.length())

    // 直接初始化带尺寸的
    val sizeBuffer = Buffer.buffer(20)
    println("直接使用带尺寸的: " + sizeBuffer.toString() + ", " + sizeBuffer.length())
}

class FirstVerticle : AbstractVerticle() {
    override fun start() {
        println("Hello verticle: ${Thread.currentThread().name}")
    }
}

class AcceptorVerticle : AbstractVerticle() {
    override fun start() {
        val server = vertx.createHttpServer()
        println("start acceptor")

        server.requestHandler { request ->
            val event = vertx.eventBus()
            println("Accept request...")

            event.send(
                "MSG://EVENT/BUS",
                JsonObject().put("message", "Event Communication")
            ) { reply: AsyncResult<Message<JsonObject>> ->
                if (reply.succeeded()) {
                    println("${Thread.currentThread().name} reply message")
                    println("message: ${reply.result().body()}")

                    request.response().end(reply.result().body().encode())
                }
            }
        }

        server.listen()
    }
}

class WorkerVerticle : AbstractVerticle() {
    override fun start() {
        println("start worker")
        val event = vertx.eventBus()
        event.consumer<JsonObject>("MSG://EVENT/BUS") { reply ->
            println("${Thread.currentThread().name}: Consume Message...")
            val message = reply.body()
            println("message: ${message.encode()}")

            reply.reply(JsonObject().put("worker", "worker message"))
        }
    }
}

class Buffalo {
    companion object {
        fun write(buffer: Buffer, data: Array<String>) {
            for (item in data) {
                val bytes = item.toByteArray(Charset.defaultCharset())
                buffer.appendInt(bytes.size)
                buffer.appendBytes(bytes)
            }
        }

        fun read(start: Int, buffer: Buffer, reference: Array<String>) : Int{
            var pos = start
            for (index in 0 until reference.size) {
                val len = buffer.getInt(pos)
                pos += 4
                val bytes = buffer.getBytes(pos, pos + len)
                reference[index] = String(bytes, Charset.defaultCharset())
                pos += len
            }

            return pos
        }
    }
}

class BasicUser: ClusterSerializable {

    @Transient
    private var id: String = ""
    @Transient
    private var username: String = ""
    @Transient
    private var password: String = ""

    override fun readFromBuffer(pos: Int, buffer: Buffer): Int {
        val strings = Array(3) {""}
        val result = Buffalo.read(pos, buffer, strings)

        this.id = strings[0]
        this.username = strings[1]
        this.password = strings[2]

        return result
    }

    override fun writeToBuffer(buffer: Buffer) {
        Buffalo.write(buffer, arrayOf(id, username, password))
    }

}
