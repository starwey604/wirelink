# 发布前 benchmark checklist

[English](RELEASE.md).

必须在 release commit 上完成本 checklist，之后才能创建发布 tag。本比较回答
candidate 相对上一版本是否退化；它不是正确性测试，也不是性能承诺。

## 政策

- 普通 push 与 PR 不运行性能测量。
- base 使用上一个 release tag，candidate 使用干净、已提交的 release
  commit。
- base 与 candidate 必须在同一机器、工具链、电源策略和开发板上运行；不
  比较来自不同 testbed 的结果。
- smoke 回归或退出码 `2` 必须在打 tag 前排查。主机 timing 与 API/UDP 在
  固定专用 testbed 完成阈值标定前只作诊断。
- 保存完整输出目录，不能只保存 Markdown 摘要。

## 推荐的本地运行方式

权威结果应在空闲、稳定的主机上生成。先配置固定版本的 WLC 与 Google
Benchmark 源码：

```sh
export WIRELINK_WLC_EXECUTABLE=/absolute/path/to/wlc
export WIRELINK_GOOGLE_BENCHMARK_SOURCE_DIR=/absolute/path/to/google-benchmark

python3 benchmarks/bench.py doctor --preset full
```

确认 candidate 已提交，并选择上一个发布 tag：

```sh
git status --short
base=v0.8.0
candidate=$(git rev-parse HEAD)
release_out=out/release-candidate
```

以下三次比较覆盖当前全部 8 个主机套件，且不会重复运行套件：

```sh
python3 benchmarks/bench.py pair --preset smoke --order ab \
  --base "$base" --candidate "$candidate" --out "$release_out/smoke"

python3 benchmarks/bench.py pair --preset quick \
  --base "$base" --candidate "$candidate" --out "$release_out/quick"

python3 benchmarks/bench.py pair --preset full --suite api \
  --base "$base" --candidate "$candidate" --out "$release_out/api"
```

每次尝试都要使用新的 `release_out`；证据目录采用独占创建，绝不覆盖。

## 可选的 GitHub 运行

`Benchmarks` workflow 只支持手工触发。在 release commit 上选择 `release`
模式，并把 `base` 设置为上一个 tag；留空时 workflow 会选择 candidate 可达
的最新旧 tag。它在一个 job 中运行 smoke、quick 与 API，并把全部证据保留
30 天。

GitHub 托管 runner 的 timing 可以提供诊断性的第二意见，但不能代替固定的
本地或 self-hosted testbed 结果。

## 审核与归档

创建 tag 前：

1. 确认每次比较都有效；退出码 `2` 表示必须重跑或修复 testbed。
2. 排查每个 smoke 回归。修复回归，或记录为什么该变化是有意的产品取舍。
3. 审核同时超过 10% 与 5 ns 的 quick timing 行，以及 API 内存和 UDP
   分布；在非专用主机上这些结果只作建议。
4. 归档三次比较的 `manifest.json`、`raw/`、`artifacts/`、
   `comparison.json`、`summary.md` 与 `junit.xml`。
5. 在 release notes 或发布跟踪 issue 中记录 base tag、candidate commit、
   testbed ID，以及每个被接受回归的处理结论。
6. 完成上述审核后才能创建 release tag。

硬件 benchmark/HIL 尚未统一自动化。发布需要固件测量时，按对应的
`benchmarks/zephyr/` 套件说明运行，并把 ELF、map、config、原始串口日志和
开发板身份与主机证据一起归档。
