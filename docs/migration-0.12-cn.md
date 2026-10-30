# 迁移到 0.12 开发合同

端点初始化现在显式接收按值传递的 `wl_link_defaults_t`：
`*_endpoint_config_defaults(&config, environment, wl_link_defaults())` 和
`*_endpoint_init(&endpoint, environment, wl_link_defaults())` 选择 CRC16；
USB 接入从 `<wirelink/usb_bulk_defaults.h>` 传入
`wl_usb_bulk_link_defaults()`，选择 NONE。两端的 integrity 必须一致，没有协商或回退。
用户在 defaults 后、`*_endpoint_init_config()`／`*_endpoint_create()` 前显式覆盖
`config.link.integrity`，完整 config 的最终值始终优先。envelope 仍由生成 profile 决定
（`any` 默认 native packet），存储仍预留最大 CRC32C trailer。
默认 `multi` 支持这些选择；固定 `cobs-crc16` 对 USB NONE 返回
`WL_ERR_NOT_SUPPORTED`。裸 core 可使用
`wl_config_apply_link_defaults(&config, defaults)`，它不重置其他字段。
新签名替代旧的双参数入口，当前生成物须由匹配的 WLC 重新生成；协议版本和 schema identity 不变。

使用匹配的 Wirelink/WLC 0.12.0 本地源码，重新生成全部 codec、runtime 和 C++/Python SDK，再重建原生包和 wheel。`wlc codegen-contract` 必须输出 `0.12`。Binding source API revision 2 是独立标识，不能代替 C 生成契约检查。

目前没有固定的 0.12 compiler/source 发布归档配对。安装消费者通过 `WIRELINK_WLC_EXECUTABLE` 指定本地 compiler，并使用 `find_package(Wirelink 0.12 CONFIG REQUIRED)`。1.0 前的生成契约要求 minor 精确匹配；保留历史发布 manifest 和 frozen-v1 向量。

## 客户端结果与生命周期

| 已删除路径 | 替代方式 |
| --- | --- |
| 客户端 `response_storage`、字节容量及 encoded response 池 | 每个调用槽对应真实 `*_rpc_result_value_t` union 对象 |
| 客户端 raw `response_data` / `response_length` 及旧接收入口 | `wl_rpc_client_response_prepare()` / `wl_rpc_client_response_commit()` |
| 生成的 `client_decode()` / `decode_owned_response()` | typed response 访问及 completion copy |
| 完成阶段的第二次业务解码 | 成功响应在 RX 中一次解码到 owned 字段 |
| 私有 schema clone、重生成及标识符改写 | 共享 codec plan 和显式 symbol policy |
| 普通消息的私有 codec 副本、私有 canonical hash | 仅生成 managed 消息及嵌套依赖，使用公开 Schema-v1 canonical hash |

core 只管理关联、deadline、owner、generation 和 TX 生命周期，生成 C 管理业务类型。自定义 runtime storage 必须在 byte arena 之外提供对齐的真实 typed result 数组。优先使用生成的 requirements/default descriptor，不能强转字节数组代替业务对象。

普通 async 先把结果复制到独立 completion union，再释放容量，最后通知；回调可立即提交新调用。sync 复制 completion。advanced `*_inspect()` 返回的 `const *_value_t *` 借用到 release/close 为止；复制 `*response` 后可独立保存。

高级 codec view 和 deferred handler 的请求字段仍有借用期，需要延后使用的字节应在回调内复制。服务器 encoded 响应、请求快照及 detached link TX 回收仍有用途，不能随客户端旧池一起删除。

## 分别选择保证与编码

默认 managed RPC 继续使用 tagged/replay。显式 `guarantee = correlate;` 允许每次合法到达独立执行，不保留完成历史和 RPC 重复抑制。两者都在 handler 前预留 pending/响应容量，保留 deferred token generation、业务拒绝、session 校验和投递背压。取消和超时只结束本地等待，无法撤销远端副作用。

```text
rpc Setpoint {
  request = SetpointRequest;
  response = SetpointResponse;
  guarantee = replay;
  codec = positional_v1;
}
latest Sample { delivery = unreliable; codec = schema_v1; }
```

RPC 可选 `managed_tagged`（默认）或 `positional_v1`；普通 route 独立选择 `schema_v1`（默认）或 `positional_v1`。两类生成命令都传匹配 profile，普通消息使用位置格式时也要给 CMake codec target 传 `PROFILE`。

managed wire v4 保留 BE64 原客户端 session 和完整 operation32。普通元数据长 9–13 字节，拒绝为 13–17 字节；不回退到被替换的固定 20 字节格式。默认 tagged 业务体不变。

位置格式改变业务体及演进规则。部署时校验 schema/profile identity，两端配套生成，具体规则见 [positional-v1](positional-v1.md)。没有格式协商或嗅探；错配的 body 偶尔也能解码成功。不支持的位置类型在写产物前拒绝。

latest 在一次 owner pass 内也可能覆盖多次 publication。必须消费每条发布时，应在结果 observer 中读取/release，或选用合适的 FIFO；仅在 pass 末读取一次不能保证完整序列。

详见 [RPC 生命周期](rpc-runtime-cn.md)、[存储](endpoint-storage.md)、[兼容性](compatibility-cn.md)和[第二轮结果](codec-rpc-refactor-round-2-result-cn.md)。
