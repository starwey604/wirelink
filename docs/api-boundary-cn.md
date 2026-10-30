# 当前 API 边界

公开 C surface 是 `kernel.h`、`kernel_wire.h`、`packet_port.h` 及选定 connector/adapter。旧 ctx/event/pump/reliable/deferred/mapped 接口已删除。单 C owner 执行 admission/step/cancel/close、codec 和 callback；生成 C 提供静态 typed route 与有界容量，不再生成另一套执行引擎。

C 持有 packet/FIFO 原子存储，C++ 使用 opaque pointer，可持有不含 C atomic 的生成 kernel。可选 `Wirelink::host` 提供有界外部命令与等待，`Wirelink::cpp_host` 提供 owning UDP Session。基础 C 包不需要 C++。包装接口前核对安装头与可编译示例。

[迁移清单](migration-0.13-cn.md) · [内核 API](static-endpoint-kernel-api.md) · [开发验证](development-current.md)
