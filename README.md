# NSPA

NSPA provides a C/C++ project scanner for constructing Candidate Function Records (CFRs), conservatively filtering potential custom memory allocation, release, and destruction functions, and exporting lightweight JSONL files that can be passed to an LLM for semantic validation.

The workflow consists of three stages:

1. **Custom memory-function candidate detection**
   Detect candidate allocation, release, and destruction interfaces from C/C++ projects.

2. **Fine-grained reachability analysis**
   Inject validated custom memory functions into SVF/Saber and run enhanced static checks on LLVM bitcode.

3. **Vulnerability verification**
   Extract source-level slices from Saber reports and use an LLM to verify whether warnings correspond to real vulnerabilities.

## Stage 1: Candidate Detection for Custom Memory Functions

It is recommended to install the dependencies before running the scanner to obtain more accurate Tree-sitter parsing:

```bash
python3 -m pip install -r requirements.txt
```

If the current Python environment lacks Tree-sitter dependencies, the scanner will automatically fall back to the dependency-free `regex_fallback` mode and continue running. The parsing mode will be reported in `--summary` and in `metadata.parser_mode` of the output JSON.

### Step 1: CFR Construction and Filtering

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

The output JSON contains the filtered CFRs, evidence lists, the raw filtering score `score`, and the normalized filtering score `confidence`. Each line of the JSONL file is a compact CFR that can be used as input for the LLM validation stage.

There are two filtering thresholds:

* `--min-score`: the raw evidence-weighted score. The default value is `2.0`.
* `--min-confidence`: the normalized filtering score, ranging from `0` to `1`. For example, `--min-confidence 0.5` retains candidates whose `filter_confidence >= 0.5`.

`macro_value` is meaningful only for function-like macros and denotes the macro body after the macro parameter list. For example, in `#define VIM_CLEAR(p) do { vim_free(p); (p) = NULL; } while (0)`, `macro_value` is `do { vim_free(p); (p) = NULL; } while (0)`. For ordinary functions, `macro_value` is an empty string.

### Step 2: CFR Semantic Validation

Step 2 reads the JSONL file generated in Step 1 and invokes an OpenAI-compatible LLM API for semantic validation. By default, it only retains functions that are finally identified as custom allocation, release, or destruction interfaces.

```bash
export OPENAI_API_KEY="your API key"

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

If you use another OpenAI-compatible service, specify the following options:

```bash
python3 -m nspa.llm_semantic_validator \
  --input ./outputs/vim/nspa_vim_memory_candidates.jsonl \
  --output ./outputs/vim/nspa_vim_validated_memory_functions.json \
  --base-url https://your-service-endpoint/v1 \
  --model your-model-name \
  --api-key-env OPENAI_API_KEY \
  --batch-size 4 \
  --max-retries 6 \
  --request-delay 0.5 \
  --progress \
  --summary
```

The default request endpoint is `{--base-url}/chat/completions`. For example, `--base-url https://api.gpt.ge/v1` sends requests to `https://api.gpt.ge/v1/chat/completions`. If the service provider uses a different path, it can be overridden with `--chat-path`. If a 404 error is returned, first check whether `--base-url`, `--chat-path`, and `--model` are consistent with the provider’s documentation.

The validator automatically creates `output_filename.checkpoint.jsonl` as a checkpoint file for resumable execution. If the task is interrupted, rerunning the same command skips CFRs that have already been validated.

During long validation runs, the remote gateway may occasionally disconnect. The validator automatically retries `RemoteDisconnected`, timeout, connection reset, 429, and 5xx errors. If a batch repeatedly fails, it is automatically split into smaller batches and validation continues. If the service provider is unstable, it is recommended to use a smaller `--batch-size`, increase `--max-retries`, and set `--request-delay`.

The output categories include:

* `allocator`: returns or passes newly allocated dynamic memory or owned objects to the caller.
* `releaser`: releases memory, fields, references, or handles passed in by the caller.

## Stage 2: Fine-Grained Reachability Analysis

Stage 2 integrates the LLM-validated custom memory management functions into SVF/Saber and runs fine-grained checks on the target project bitcode.

### Step 1: Inject Custom Memory Functions

The script reads `outputs/nspa_curl_validated_memory_functions.json`, filters `allocator/releaser/destroyer` entries, skips function-like macros, and inserts the results into `ei_pairs[]` in `SVF/svf/lib/SABER/SaberCheckerAPI.cpp`. `allocator` is mapped to `CK_ALLOC`, while `releaser/destroyer` is mapped to `CK_FREE`. The insertion preserves the type grouping required by Saber to avoid triggering `ei_pairs not grouped by type`.

Update only the Saber source code without compiling or running Saber:

```bash
python3 -m nspa.fine_grained_reachability \
  --validated-json outputs/curl/nspa_curl_validated_memory_functions.json \
  --project curl \
  --saber-api-cpp SVF/svf/lib/SABER/SaberCheckerAPI.cpp \
  --skip-rebuild \
  --skip-saber \
  --summary
```

### Step 2: Compile Target Project Bitcode

* `scripts/build_bc.sh`: one-click build for all projects.
* `build_common.sh`: common clang/clang++ bitcode wrappers, collection logic, and llvm-link logic.
* Single-project scripts: bash, curl, ffmpeg, git, openssl, sqlite, tmux, vim.

```bash
bash scripts/build_all_bc.sh
# Or build and run a single project separately
bash scripts/build_ffmpeg_bc.sh
```

All outputs are placed under `NSPA/workspace/<project>-bc/`. Each project directory contains:

* `objects/`: `.bc` files corresponding to each source file.
* `project.bc`: complete LLVM bitcode of the project.
* `manifest.tsv`: mapping from source files or archive members to bitcode files.
* `logs/`: build logs.

The scripts use clang wrappers to generate LLVM bitcode objects, recursively collect bitcode from `.o/.bc` files, and extract bitcode members from libtool `.libs/*.a` static libraries.

### Step 3: Rebuild SVF/Saber

This project is built on SVF/Saber. Before performing this step, install SVF according to the official documentation: [SVF Installation](https://github.com/SVF-tools/SVF).

```bash
bash scripts/rebuild_and_check_saber.sh \
  /NSPA/ \
  /NSPA/SVF/Release-build \
  /NSPA/workspace/curl-bc \
  /NSPA/outputs/curl/nspa_curl_validated_memory_functions.json
```

### Step 4: Run Saber on Bitcode

A full run executes the following commands for each `.bc` file under `workspace/curl-bc`:

```bash
saber -leak       -extapi="$SVF_EXTAPI" file.bc
saber -dfree      -extapi="$SVF_EXTAPI" file.bc
saber -fileck     -extapi="$SVF_EXTAPI" file.bc
saber -null-deref -extapi="$SVF_EXTAPI" file.bc
```

Automated command:

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

During execution, the console prints the progress for each `.bc + checker` pair. By default, the output uses a sparse saving strategy: normal results with no stderr content and a return code of 0 do not generate per-run files. Results with stderr content or a non-zero return code save a `stderr` file. `stdout` is discarded by default. To save non-empty stdout, add `--save-stdout`. To disable progress printing, add `--quiet`.

At the end of Stage 2, the tool reports `Stage 2: fine-grained reachability analysis runtime`. When `--summary` is used, the summary JSON also includes `stage_elapsed_seconds` and `stage_elapsed`.

The output directory generates:

* `manifest.json`: structured execution results.
* `manifest.tsv`: result index for convenient table viewing.

## Stage 3: Vulnerability Verification

Stage 3 reads the Saber reports generated in Stage 2. Based on the `memory allocation/file open` locations and the path line numbers of `conditional free/double free/conditional file close` in the alerts, it extracts program slices from the original source code and then invokes an LLM to determine whether each alert corresponds to a real vulnerability.

Invoke an OpenAI-compatible API for final verification:

```bash
export OPENAI_API_KEY="your API key"

python3 -m nspa.vulnerability_verifier_multi \
  --saber-output-dir outputs/saber/curl \
  --source-root open-source-soft/curl-master \
  --output outputs/curl/nspa_curl_verified_vulnerabilities.json \
  --checkpoint-jsonl outputs/curl/nspa_curl_verified_vulnerabilities.json.checkpoint.json \
  --workers 1 \
  --parallel-backend thread \
  --model your-model-name \
  --base-url https://your-service-endpoint/v1 \
  --api-key-env OPENAI_API_KEY \
  --timeout 120 \
  --max-retries 3 \
  --api-error-policy unknown \
  --progress \
  --summary
```

Stage 3 also automatically creates `output_filename.checkpoint.jsonl` as a checkpoint file for resumable execution. During long validation runs, if the remote gateway still disconnects after all retries are exhausted, the default `--api-error-policy unknown` records the corresponding alert as `unknown` and continues verifying subsequent alerts, preventing the entire run from being aborted. If you want the process to fail immediately when an API error occurs, set `--api-error-policy stop`. To re-verify `unknown` entries caused by API failures later, delete the corresponding checkpoint lines or rerun with a new `--checkpoint-jsonl`.

Stage 3 writes two result files:

* The full result file specified by `--output`, for example `outputs/nspa_curl_verified_vulnerabilities.json`: `results` contains the LLM verification result, original alert, and source-code slice for each Saber alert; `confirmed_vulnerabilities` contains the final vulnerabilities judged as `true_positive`.
* An automatically generated TP-only file, for example `outputs/nspa_curl_verified_vulnerabilities_TP.json`: `results` contains only verification results whose judgment is `true_positive`. A different path can be specified with `--tp-output`.

LLM judgment categories:

* `true_positive`: the slice shows a feasible vulnerability path.
* `false_positive`: the slice shows that the resource is correctly released, ownership has been transferred, or the path is infeasible.
* `unknown`: the slice is insufficient for confirmation and requires manual audit.
