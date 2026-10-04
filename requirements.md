# Reproduction Environment

## Local WSL Environment

| Property | Measured value |
| --- | --- |
| Platform | WSL 2 on Windows; default distribution: Ubuntu-22.04 |
| Operating system | Ubuntu 22.04.5 LTS, x86_64 |
| Kernel | `5.15.153.1-microsoft-standard-WSL2` |
| CPU | Intel Core i5-13600KF |
| Logical CPUs available to WSL | 20 (`nproc`) |
| Total memory visible to WSL | Approximately 15 GiB (`free -h`) |
| Swap | 4.0 GiB |

## Configuration

| Tool / stage | configuration |
| --- | --- |
| NSPA | Built on LLVM 21.1.0 and SVF 3.3; generates LLVM SSA IR with `-O0` and extends Saber |
| SVF / Saber | 3.3.0; LLVM 21.1.0; `-O0`; default SVF pointer analysis as invoked by Saber, sparse value-flow graph construction, and memory-checking configuration |
| Stage 1 / Stage 3 | Claude Opus 4.8, temperature = 0; CMMF identification and alert validation, respectively |
| CppCheck | 2.21.0; `--enable=all --max-ctu-depth=4 --check-level=exhaustive` |
| CodeQL | 2.25.6; `codeql/cpp-queries`; selected queries for memory leaks, null-pointer dereferences, and `cpp/file-never-closed` |
| LLMDFA / RepoAudit | Their respective official workflows, both using Claude Opus 4.8 with temperature = 0 |
