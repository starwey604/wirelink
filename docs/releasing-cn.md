# 冻结提交配对并发布

先完成两个候选分支。`.github/release-pair.json` 固定 WLC 的完整 SHA，Wirelink
自身 SHA 由运行发布工作流的提交提供；WLC 的测试基线不需要反向引用最终 Wirelink
提交。演练时显式传入最终 Wirelink SHA，归档内的元数据记录实际验证的 `(Wirelink, WLC)`。

## 两边演练通过后再创建发布标签

在 Wirelink 工作区中读取候选配对；两个候选分支应仍指向这些提交：

```sh
WIRELINK_SOURCE=$(git rev-parse HEAD)
SOURCE=$(python -c 'import json; print(json.load(open(".github/release-pair.json"))["wlc"]["source_sha"])')
COMPILER_BRANCH=fix/release-pair-013
CORE_BRANCH=fix/release-pair-013

gh workflow run release.yml --repo starwey604/wlc --ref "$COMPILER_BRANCH" \
  -f source_ref="$SOURCE" -f wirelink_ref="$WIRELINK_SOURCE" -F dry_run=true
gh workflow run release.yml --repo starwey604/wirelink --ref "$CORE_BRANCH" -F dry_run=true
```

WLC 演练验证五个平台归档，解压后调用其中的编译器，检查 contract 0.13、binding API 3，
对比 checked-in 产物，并在精确配对的 installed core 上编译运行 RPC 和 C/C++/COBS 消费者。
Wirelink 演练运行完整 Host、SDK、Zephyr 矩阵，校验 40 个 wheel 的包名、版本、ABI 和
平台组合，收集两个经过 manylinux 测试的 sdist。两者均生成来源清单与 `SHA256SUMS`。

记录两次成功 Release run 的 ID，分别为 `WLC_RUN` 和 `CORE_RUN`。保持两个候选分支不动；
可以快进合入 main，但不能把后续源码修改混入待发布配对。

## 按 WLC → Wirelink 顺序提升原始产物

```sh
gh workflow run release.yml --repo starwey604/wlc --ref "$COMPILER_BRANCH" \
  -f source_ref="$SOURCE" -f wirelink_ref="$WIRELINK_SOURCE" \
  -f tag=v0.13.0 -f promote_run="$WLC_RUN" -F dry_run=false
# 等 WLC 发布成功，再提升 Wirelink。
gh workflow run release.yml --repo starwey604/wirelink --ref "$CORE_BRANCH" \
  -f tag=v0.13.0 -f promote_run="$CORE_RUN" -F dry_run=false
```

提升校验原始 run 成功、源码 SHA、完整库存、配对与校验和，复用原始 bundle，不重新构建。
Wirelink 还会读取已公开 WLC Release 的来源清单，确认两边是同一精确配对。缺少标签时，
只有经过验证的提升可以创建对应标签；普通 tag-push 发布入口继续保留。

重复提升完全一致的资产不会修改已有 Release；资产、标签或配对不一致则失败，保留已有资产。
失败时先查看对应步骤和原始 bundle，修复新候选后重新演练；不要移动已发布标签或覆盖旧资产。

WLC bundle 包含五个归档、`wlc-release.json`、`SHA256SUMS`；Wirelink bundle 包含
40 个 wheel、两个 sdist、`wirelink-release.json`、`SHA256SUMS`。完成两边发布之前保留 CI 产物。
