[English](https://github.com/DataAvailable/NSPA/README.md)|[中文简体](https://github.com/DataAvailable/NSPA/README-Zh.md)


# NSPA

**Neuro-Symbolic Augmented Fine-Grained Pointer Analysis**

NSPA is a neuro-symbolic static-analysis framework for improving fine-grained pointer and resource analysis in large-scale C/C++ projects. It combines lightweight static candidate extraction, LLM-based semantic validation, SVF/Saber integration, LLVM bitcode analysis, and LLM-assisted vulnerability verification.

The workflow consists of three stages:

1. **Custom memory-function candidate detection**
   Detect candidate allocation, release, and destruction interfaces from C/C++ projects.

2. **Fine-grained reachability analysis**
   Inject validated custom memory functions into SVF/Saber and run enhanced static checks on LLVM bitcode.

3. **Vulnerability verification**
   Extract source-level slices from Saber reports and use an LLM to verify whether warnings correspond to real vulnerabilities.

---

## Stage 1: Custom Memory-Function Candidate Detection

NSPA provides a C/C++ project scanner that builds Candidate Function Records (CFRs), conservatively filters potential custom memory allocation/release/destruction functions, and exports compact JSONL records for LLM-based semantic validation.

Install dependencies first for more accurate Tree-sitter parsing:

```bash
python3 -m pip install -r requirements.txt
```

If Tree-sitter dependencies are not available in the current Python environment, the scanner automatically falls back to the dependency-free `regex_fallback` mode. The selected parser mode is reported in `--summary` and in the output JSON field `metadata.parser_mode`.

---

### Step 1: Build and Filter CFRs

Example command:

```bash
python3 -m nspa.memory_function_detector \
  --project-root ./open-source-soft/vim-master \
  --project-name vim \
  --output ./outputs/nspa_vim_memory_candidates.json \
  --llm-jsonl ./outputs/nspa_vim_memory_candidates.jsonl \
  --summary \
  --min-confidence 0.5 \
  --exclude runtime \
  --exclude testdir
```

The output JSON contains filtered CFRs, evidence lists, raw filtering scores (`score`), and normalized filtering confidence values (`confidence`). The JSONL file contains one compact CFR per line and can be used as the input to the LLM validation stage.

Two filtering thresholds are supported:

* `--min-score`: the raw weighted evidence score. The default value is `2.0`.
* `--min-confidence`: the normalized filtering confidence in the range `[0, 1]`. For example, `--min-confidence 0.5` keeps candidates with `filter_confidence >= 0.5`.

The field `macro_value` is meaningful only for function-like macros. It represents the macro body after the macro parameter list. For example, in:

```c
#define VIM_CLEAR(p) do { vim_free(p); (p) = NULL; } while (0)
```

the `macro_value` is:

```c
do { vim_free(p); (p) = NULL; } while (0)
```

For normal functions, `macro_value` is an empty string.

---

### Step 2: Semantic Validation of CFRs

This step reads the JSONL file generated in Step 1 and calls an OpenAI-compatible LLM API for semantic validation. By default, it only keeps functions that are finally identified as custom allocation, release, or destruction interfaces.

```bash
export OPENAI_API_KEY="your_api_key"

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

For other OpenAI-compatible services, specify `--base-url`, `--model`, and the API key environment variable:

```bash
python3 -m nspa.llm_semantic_validator \
  --input ./outputs/nspa_vim_memory_candidates.jsonl \
  --output ./outputs/nspa_vim_validated_memory_functions.json \
  --base-url https://your-service-endpoint/v1 \
  --model your-model-name \
  --api-key-env OPENAI_API_KEY \
  --batch-size 4 \
  --max-retries 6 \
  --request-delay 0.5 \
  --progress \
  --summary
```

By default, the request URL is:

```text
{--base-url}/chat/completions
```

For example, `--base-url https://api.example.com/v1` sends requests to:

```text
https://api.example.com/v1/chat/completions
```

If your provider uses a different path, override it with `--chat-path`. If a `404` error occurs, first check whether `--base-url`, `--chat-path`, and `--model` are consistent with the service provider's documentation.

The validator automatically creates a checkpoint file named:

```text
<output_file>.checkpoint.jsonl
```

If validation is interrupted, re-running the same command skips already validated CFRs.

For long-running validation tasks, remote gateways may occasionally disconnect. The validator automatically retries `RemoteDisconnected`, timeout, connection reset, `429`, and `5xx` errors. If a batch repeatedly fails, it is automatically split into smaller batches. For unstable service providers, use a smaller `--batch-size`, increase `--max-retries`, and set a larger `--request-delay`.

The output categories are:

* `allocator`: returns or passes out newly allocated memory or ownership objects.
* `releaser`: releases memory, fields, references, or handles passed by the caller.
* `destroyer`: destroys an object, container, or resource lifecycle, usually including internal field release.
* `non_memory`: not a custom memory-management interface.

By default, `non_memory` results are filtered out. To audit all LLM decisions, add:

```bash
--include-non-memory
```

---

## Stage 2: Fine-Grained Reachability Analysis

Stage 2 integrates LLM-validated custom memory-management functions into SVF/Saber and runs fine-grained static checks on target-project LLVM bitcode.

---

### Step 1: Inject Custom Memory Functions

The script reads validated memory functions, filters `allocator`, `releaser`, and `destroyer` entries, skips function-like macros, and inserts the results into `ei_pairs[]` in:

```text
SVF/svf/lib/SABER/SaberCheckerAPI.cpp
```

The mapping is:

* `allocator` -> `CK_ALLOC`
* `releaser` / `destroyer` -> `CK_FREE`

The insertion preserves Saber’s required type grouping to avoid the error:

```text
ei_pairs not grouped by type
```

To update the Saber source file only, without rebuilding or running Saber:

```bash
python3 -m nspa.fine_grained_reachability \
  --validated-json outputs/nspa_curl_validated_memory_functions.json \
  --project curl \
  --saber-api-cpp SVF/svf/lib/SABER/SaberCheckerAPI.cpp \
  --skip-rebuild \
  --skip-saber \
  --summary
```

---

### Step 2: Build LLVM Bitcode for Target Projects

The repository provides scripts for building LLVM bitcode:

* `scripts/build_all_bc.sh`: build all supported projects.
* `build_common.sh`: common clang/clang++ bitcode wrapper, collection, and `llvm-link` logic.
* Per-project scripts: `bash`, `curl`, `ffmpeg`, `git`, `openssl`, `sqlite`, `tmux`, and `vim`.

Build all projects:

```bash
bash scripts/build_all_bc.sh
```

Or build a single project:

```bash
bash scripts/build_ffmpeg_bc.sh
```

The output is stored under:

```text
NSPA/workspace/<project>-bc/
```

Each project directory contains:

```text
objects/       Per-source LLVM bitcode files
project.bc     Linked whole-project LLVM bitcode
manifest.tsv   Mapping from source/archive members to bitcode files
logs/          Build logs
```

The build scripts use clang wrappers to generate LLVM bitcode objects, recursively collect bitcode from `.o` and `.bc` files, and extract bitcode members from libtool `.libs/*.a` static archives.

---

### Step 3: Rebuild SVF/Saber

NSPA is built on top of SVF/Saber. Before this step, install SVF according to the official SVF documentation:

```text
https://github.com/SVF-tools/SVF
```

Rebuild and check Saber:

```bash
bash scripts/rebuild_and_check_saber.sh \
  /NSPA \
  /NSPA/SVF/Release-build \
  /NSPA/workspace/curl-bc \
  /NSPA/outputs/curl/nspa_curl_validated_memory_functions.json
```

---

### Step 4: Run Saber on LLVM Bitcode

A full run executes the following Saber checks for each `.bc` file under `workspace/curl-bc`:

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

During execution, the tool prints progress for each `.bc + checker` pair.

By default, the output uses a sparse-saving strategy:

* If `stderr` is empty and the return code is `0`, no per-run file is saved.
* If `stderr` is non-empty or the return code is non-zero, the `stderr` file is saved.
* `stdout` is discarded by default.

To save non-empty stdout, add:

```bash
--save-stdout
```

To disable progress printing, add:

```bash
--quiet
```

At the end of Stage 2, the tool reports the elapsed time of the fine-grained reachability analysis. When `--summary` is enabled, the summary JSON also includes:

```text
stage_elapsed_seconds
stage_elapsed
```

The output directory contains:

```text
manifest.json   Structured run results
manifest.tsv    Tabular result index
```

---

### Batch Timing for All Projects

To measure the Step 4 runtime of the enhanced SVF/Saber analysis across multiple projects:

```bash
python3 scripts/run_saber_timing_all_projects.py \
  --projects bash,curl,ffmpeg,git,openssl,sqlite,tmux,vim \
  --svf-build-dir SVF/Release-build \
  --bc-scope objects \
  --timeout 120
```

This script sequentially injects custom allocation/release functions from:

```text
outputs/<project>/nspa_<project>_validated_memory_functions.json
```

then rebuilds `saber` and runs Saber checks on the project bitcode.

The summary results are written to:

```text
outputs/saber_timing_summary.json
outputs/saber_timing_summary.csv
```

The Step 4 runtime fields are:

```text
saber_elapsed_seconds
saber_elapsed
```

For a quick smoke test, use:

```bash
--bc-limit 1 --checkers leak
```

---

## Stage 3: Vulnerability Verification

Stage 3 reads Saber reports from Stage 2, extracts source-level program slices according to warning locations, and uses an LLM to determine whether each warning corresponds to a real vulnerability.

The verifier uses the following information from Saber reports:

* `memory allocation` / `file open` locations
* `conditional free`
* `double free`
* `conditional file close`
* path line numbers

It then extracts source slices from the original project and asks the LLM to classify each warning.

---

### Run LLM-Based Vulnerability Verification

```bash
export OPENAI_API_KEY="your_api_key"

python3 -m nspa.vulnerability_verifier \
  --saber-output-dir outputs/saber/curl \
  --source-root open-source-soft/curl-master \
  --output outputs/nspa_curl_verified_vulnerabilities.json \
  --base-url https://your-service-endpoint/v1 \
  --model your-model-name \
  --api-key-env OPENAI_API_KEY \
  --max-retries 8 \
  --request-delay 0.5 \
  --api-error-policy unknown \
  --api-error-cooldown 10 \
  --progress \
  --summary
```

If the service provider does not support JSON mode, add:

```bash
--no-json-mode
```

Stage 3 also creates a checkpoint file:

```text
<output_file>.checkpoint.jsonl
```

If the remote gateway still disconnects after all retries, the default policy:

```bash
--api-error-policy unknown
```

marks the corresponding warning as `unknown` and continues validating subsequent warnings. This prevents the entire run from being interrupted by a single API failure.

To fail immediately on API errors, use:

```bash
--api-error-policy stop
```

To re-validate `unknown` cases caused by API failures, delete the corresponding checkpoint lines or specify a new checkpoint file with:

```bash
--checkpoint-jsonl
```

---

### Stage 3 Outputs

Stage 3 writes two result files.

The full output file specified by `--output`, for example:

```text
outputs/nspa_curl_verified_vulnerabilities.json
```

contains:

```text
results                     All Saber warnings with LLM verification results, raw warnings, and source slices
confirmed_vulnerabilities   Warnings classified as true positives
```

A TP-only file is generated automatically, for example:

```text
outputs/nspa_curl_verified_vulnerabilities_TP.json
```

This file contains only results classified as:

```text
true_positive
```

A custom TP-only output path can be specified with:

```bash
--tp-output
```

The LLM classification labels are:

* `true_positive`: the slice shows a feasible vulnerability path.
* `false_positive`: the slice shows that the resource is properly released, ownership is transferred, or the path is infeasible.
* `unknown`: the slice is insufficient for confirmation and requires manual auditing.

Source slicing first tries to extract the complete function containing the warning line. If the function is too large or cannot be identified, the verifier falls back to a context window around the warning line. Important lines are marked with:

```text
>>
```

---

## Notes

* The framework is designed for C/C++ projects.
* OpenAI-compatible APIs are supported in both semantic validation and vulnerability verification.
* Checkpoint files are generated automatically for long-running LLM-based stages.
* SVF/Saber must be installed and built before running the fine-grained analysis stage.
* Large generated artifacts under `workspace/` may be expensive to store in Git. Consider using Git LFS or excluding unnecessary build artifacts when publishing the repository.