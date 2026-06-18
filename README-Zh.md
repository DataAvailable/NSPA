# NSPA
Neuro-Symbolic Augmented Fine-Grained Pointer Analysis


## 第一阶段：自定义内存函数候选检测

NSPA提供了一个C/C++项目扫描器，用于构建候选函数记录（CFR）、保守过滤潜在的自定义内存分配/释放/销毁函数，并输出可交给LLM语义验证的轻量级JSONL。

推荐安装依赖后运行以获得更精确的Tree-sitter解析：

```bash
python3 -m pip install -r requirements.txt
```

如果当前Python环境缺少Tree-sitter依赖，扫描器会自动切换到无第三方依赖的`regex_fallback`模式继续运行，并在`--summary`和输出JSON的`metadata.parser_mode`中标明解析模式。

### 步骤1：CFR构建和过滤

```bash
python3 -m nspa.memory_function_detector \
  --project-root ./open-source-soft/vim-master \
  --project-name vim \
  --output ./outputs/vim/nspa_vim_memory_candidates.json \
  --llm-jsonl ./outputs/vim/nspa_vim_memory_candidates.jsonl \
  --summary \
  --min-confidence 0.5 \
  --exclude runtime \
  --exclude testdir
```

输出JSON包含过滤后的CFR、证据列表、原始过滤分数`score`和归一化过滤分数`confidence`；JSONL文件每行是一个紧凑CFR，可作为LLM验证阶段的输入。

过滤阈值有两种：

- `--min-score`：原始证据加权分，默认值为`2.0`。
- `--min-confidence`：归一化后的过滤分数，范围为`0`到`1`。例如`--min-confidence 0.5`会保留`filter_confidence >= 0.5`的候选。

`macro_value`只对函数式宏有意义，表示宏参数列表之后的宏体。例如`#define VIM_CLEAR(p) do { vim_free(p); (p) = NULL; } while (0)`中，`macro_value`就是`do { vim_free(p); (p) = NULL; } while (0)`；普通函数的`macro_value`为空字符串。


### 步骤2：CFR语义验证

步骤2读取步骤1生成的JSONL，调用OpenAI-compatible的LLM API进行语义验证，默认只保留最终识别为自定义分配/释放/销毁接口的函数。

```bash
export OPENAI_API_KEY="你的API Key"

python3 -m nspa.llm_semantic_validator \
  --input ./outputs/vim/nspa_vim_memory_candidates.jsonl \
  --output ./outputs/vim/nspa_vim_validated_memory_functions.json \
  --model gpt-4o-mini \
  --batch-size 4 \
  --max-retries 6 \
  --request-delay 0.5 \
  --min-llm-confidence 0.5 \
  --progress \
  --summary
```

如果使用其他OpenAI-compatible服务，可以指定：

```bash
python3 -m nspa.llm_semantic_validator \
  --input ./outputs/vim/nspa_vim_memory_candidates.jsonl \
  --output ./outputs/vim/nspa_vim_validated_memory_functions.json \
  --base-url https://你的服务地址/v1 \
  --model 你的模型名 \
  --api-key-env OPENAI_API_KEY \
  --batch-size 4 \
  --max-retries 6 \
  --request-delay 0.5 \
  --progress \
  --summary
```

默认请求地址为`{--base-url}/chat/completions`。例如`--base-url https://api.gpt.ge/v1`会请求`https://api.gpt.ge/v1/chat/completions`。如果服务商使用不同路径，可通过`--chat-path`覆盖。若返回404，优先检查`--base-url`、`--chat-path`和`--model`是否与服务商文档一致。


验证器会自动创建`输出文件名.checkpoint.jsonl`作为断点续跑文件。若任务中断，重新运行同一命令会跳过已经完成验证的CFR。

长时间验证时，远端网关可能偶发断开连接。验证器会对`RemoteDisconnected`、超时、连接重置、429和5xx错误自动重试；如果一个批次反复失败，会自动拆成更小批次继续验证。若服务商不稳定，建议使用较小的`--batch-size`，提高`--max-retries`，并设置`--request-delay`。

输出类别包括：

- `allocator`：向调用者返回或传出新动态内存/拥有权对象。
- `releaser`：释放调用者传入的内存、字段、引用或句柄。
- `destroyer`：销毁整个对象/容器/资源生命周期，通常包含释放内部字段。
- `non_memory`：不是自定义内存管理接口。

默认输出会过滤掉`non_memory`。如果需要审计所有LLM判断结果，添加`--include-non-memory`。


## 第二阶段：细粒度可达性分析

第二阶段把 LLM 验证得到的自定义内存管理函数接入 SVF/Saber，并在目标项目 bitcode 上运行细粒度检查。

### 步骤1：注入自定义内存函数

脚本会读取 `outputs/nspa_curl_validated_memory_functions.json`，过滤 `allocator/releaser/destroyer`，跳过函数式宏，并把结果插入 `SVF/svf/lib/SABER/SaberCheckerAPI.cpp` 的 `ei_pairs[]`。`allocator` 映射为 `CK_ALLOC`，`releaser/destroyer` 映射为 `CK_FREE`。插入位置会保持 Saber 要求的类型分组，避免触发 `ei_pairs not grouped by type`。

仅更新 Saber 源码，不编译、不运行：

```bash
python3 -m nspa.fine_grained_reachability \
  --validated-json outputs/curl/nspa_curl_validated_memory_functions.json \
  --project curl \
  --saber-api-cpp SVF/svf/lib/SABER/SaberCheckerAPI.cpp \
  --skip-rebuild \
  --skip-saber \
  --summary
```

### 步骤2：编译目标项目 bitcode

* `scripts/build_bc.sh` 一键构建全部项目;
* `build_common.sh`：公共 clang/clang++ bitcode wrapper、收集和 llvm-link 逻辑;
* 单项目脚本：bash、curl、ffmpeg、git、openssl、sqlite、tmux、vim

```bash
bash scripts/build_all_bc.sh
# 或单独编译运行
bash scripts/build_ffmpeg_bc.sh
```

输出都放在 `NSPA/workspace/<project>-bc/`，每个项目目录包含：

* objects/：每个源代码对应的 .bc
* project.bc：项目完整 LLVM bitcode
* manifest.tsv：源文件/归档成员到 bitcode 的映射
* logs/：构建日志

脚本会用 clang wrapper 生成 LLVM bitcode object，递归收集`.o/.bc`中的 bitcode，并从 libtool `.libs/*.a` 静态库中提取 bitcode 成员。


### 步骤3：重编译 SVF/Saber

本项目基于SVF/Saber工具构建，在进行该步骤之前请先按照官方文档[安装SVF](https://github.com/SVF-tools/SVF)。

```bash
bash scripts/rebuild_and_check_saber.sh \
  /NSPA/ \
  /NSPA/SVF/Release-build \
  /NSPA/workspace/curl-bc \
  /NSPA/outputs/curl/nspa_curl_validated_memory_functions.json
```


### 步骤4：运行 Saber 检测 bitcode

完整运行会对 `workspace/curl-bc` 下的每个 `.bc` 分别执行：

```bash
saber -leak       -extapi="$SVF_EXTAPI" file.bc
saber -dfree      -extapi="$SVF_EXTAPI" file.bc
saber -fileck     -extapi="$SVF_EXTAPI" file.bc
saber -null-deref -extapi="$SVF_EXTAPI" file.bc
```

自动化命令：

```bash
python3 -m nspa.fine_grained_reachability \
  --validated-json outputs/curl/nspa_curl_validated_memory_functions.json \
  --project curl \
  --saber-api-cpp SVF/svf/lib/SABER/SaberCheckerAPI.cpp \
  --svf-build-dir SVF/Release-build \
  --bc-dir workspace/curl-bc/src \
  --output-dir outputs/saber/curl \
  --skip-rebuild \
  --timeout 120 \
  --summary
```

运行过程中会在控制台打印每个 `.bc + checker` 的进度。默认输出采用稀疏保存策略：没有 stderr 内容且返回码为 0 的正常结果不保存 per-run 文件；有 stderr 内容或非 0 返回码的结果会保存 `stderr` 文件。`stdout` 默认丢弃，如需保存非空 stdout，添加 `--save-stdout`。如需关闭进度打印，添加 `--quiet`。

第二阶段结束时会输出 `第二阶段：细粒度可达性分析运行时间`；使用 `--summary` 时，汇总 JSON 也会包含 `stage_elapsed_seconds` 和 `stage_elapsed`。

输出目录会生成：

- `manifest.json`：结构化运行结果。
- `manifest.tsv`：便于表格查看的结果索引。

批量统计增强后 SVF/Saber 在每个项目上的步骤4运行时间：

```bash
python3 scripts/run_saber_timing_all_projects.py \
  --projects bash,curl,ffmpeg,git,openssl,sqlite,tmux,vim \
  --svf-build-dir SVF/Release-build \
  --bc-scope objects \
  --timeout 120
```

该脚本会对每个项目依次注入 `outputs/<project>/nspa_<project>_validated_memory_functions.json` 中的自定义内存分配/释放函数，重编译 `saber`，然后运行 Saber 检测 bitcode。汇总结果写入 `outputs/saber_timing_summary.json` 和 `outputs/saber_timing_summary.csv`；每个项目的步骤4耗时字段为 `saber_elapsed_seconds` / `saber_elapsed`。快速冒烟测试可添加 `--bc-limit 1 --checkers leak`。

## 第三阶段：漏洞验证

第三阶段读取第二阶段的 Saber 报告，根据告警中的 `memory allocation/file open` 位置和 `conditional free/double free/conditional file close` 路径行号，从原始源代码中抽取程序切片，然后调用 LLM 判断告警是否为真实漏洞。

调用 OpenAI-compatible API 进行最终验证：

```bash
export OPENAI_API_KEY="你的API Key"

python3 -m nspa.vulnerability_verifier_multi \
  --saber-output-dir outputs/saber/curl \
  --source-root open-source-soft/curl-master \
  --output outputs/curl/nspa_curl_verified_vulnerabilities.json \
  --checkpoint-jsonl outputs/curl/nspa_curl_verified_vulnerabilities.json.checkpoint.json \
  --workers 1 \
  --parallel-backend thread \
  --model 你的模型名 \
  --base-url https://你的服务地址/v1 \
  --api-key-env OPENAI_API_KEY \
  --timeout 120 \
  --max-retries 3 \  
  --api-error-policy unknown \ 
  --progress \  
  --summary
```

如果服务商不支持 JSON mode，可以添加：

```bash
--no-json-mode
```

第三阶段也会自动创建`输出文件名.checkpoint.jsonl`作为断点续跑文件。长时间验证时，如果远端网关在重试耗尽后仍然断开，默认`--api-error-policy unknown`会把该条告警记录为`unknown`并继续验证后续告警，避免整轮任务中止；如果希望遇到 API 错误立刻失败，设置`--api-error-policy stop`。之后若想重新验证这些 API 失败的`unknown`项，可删除对应 checkpoint 行或换一个新的`--checkpoint-jsonl`重新运行。

第三阶段会写出两个结果文件：

- `--output` 指定的全量文件，例如 `outputs/nspa_curl_verified_vulnerabilities.json`：`results`包含每条 Saber 告警的 LLM 验证结果、原始告警、源码切片；`confirmed_vulnerabilities`包含其中判定为`true_positive`的最终漏洞。
- 自动生成的 TP-only 文件，例如 `outputs/nspa_curl_verified_vulnerabilities_TP.json`：`results`只包含判定结果为`true_positive`的验证结果。可通过`--tp-output`指定其他路径。

LLM 判定类别：

- `true_positive`：切片显示存在可行漏洞路径。
- `false_positive`：切片显示资源已正确释放、所有权已转移或路径不可行。
- `unknown`：切片不足以确认，需要人工审计。

程序切片会优先抽取包含告警行号的完整函数；如果函数过大或无法识别，则退化为告警行号附近的上下文窗口。重要行以 `>>` 标记。