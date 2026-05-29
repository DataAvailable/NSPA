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
  --project-root vim-master \
  --project-name vim \
  --output ./outputs/nspa_vim_memory_candidates.json \
  --llm-jsonl ./outputs/nspa_vim_memory_candidates.jsonl \
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
  --input ./outputs/nspa_vim_memory_candidates.jsonl \
  --output ./outputs/nspa_vim_validated_memory_functions.json \
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
  --input ./outputs/nspa_vim_memory_candidates.jsonl \
  --output ./outputs/nspa_vim_validated_memory_functions.json \
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
