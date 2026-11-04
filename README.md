# Multi-step SWE

**Can coding agents keep building on their own code over long horizons?**

[Paper](https://multistepswe.com/paper.pdf) · [Leaderboard](https://multistepswe.com/#leaderboard) · [Explore tasks](https://multistepswe.com/#tasks) · [Website](https://multistepswe.com/)

Multi-step SWE evaluates sustained software engineering on an evolving codebase. Each task chains **5–15 dependent steps**, with every step starting from the code the agent produced earlier. Design decisions and errors carry forward.

A step is a complete engineering request, not an agent turn or a tool call: **2–4 hours per step, 10–60 hours per task**.

**69 tasks · 574 steps · 9 languages · 4 task families**

## Results

The strongest model passes **56.8% of individual steps** but solves only **18.5% of task rollouts**. **38 of 69 tasks remain unsolved** by any evaluated model.

| Model | Harness | Tasks solved (95% CI) | Steps passed |
|---|---|---:|---:|
| Claude Opus 5.5 | Claude Code | **18.5%** [11.6, 25.7] | **56.8%** |
| Claude Fable 5.1 | Claude Code | 17.8% [9.8, 26.1] | 49.1% |
| Gemini 4 Argon | mini-swe-agent | 10.1% [5.8, 15.2] | 40.1% |
| GPT-6.1 Sol | Codex CLI | 10.1% [5.1, 15.9] | 37.7% |
| GPT-6 Astra | Codex CLI | 6.9% [2.9, 12.0] | 36.7% |
| DeepSeek V4.1 Flash | Claude Code | 6.5% [2.9, 10.9] | 35.8% |
| Qwen 3.8 Max | Claude Code | 4.0% [1.1, 7.6] | 24.4% |
| MiMo 2.6 Pro | Claude Code | 2.9% [0.4, 6.5] | 22.1% |
| Grok 4.7 | Grok Build | 1.4% [0.0, 4.0] | 13.9% |

A rollout solves a task only if **every step receives full credit**. Intervals are bootstrapped over tasks. Results reflect each model–harness combination; see the [paper](https://multistepswe.com/paper.pdf) for configurations and protocol.

## Tasks

| Family | Tasks | Example |
|---|---:|---|
| Feature implementation | 18 | [Build a database in Rust](tasks/turso-engine-multiversion), then add indexes, views, and encryption across 15 turso releases. |
| Version upgrade | 20 | [Implement Ethereum's EVM in C](tasks/evm-fork-chain) across 14 hard forks, from Frontier to Osaka. |
| Migration | 13 | [Migrate a React 15 dashboard to React 19](tasks/react-classes-to-19) through 8 stages while preserving behavior. |
| Optimization | 18 | [Accelerate BoringSSL](tasks/boringssl-crypto-asm): AES → AES-GCM → GCM-SIV → Montgomery multiplication → P-256, preserving outputs. |

Requirements and verifiers share an existing source of truth: specifications, upstream releases, or reference behavior. Each task passed two rounds of expert review. Steps are graded in separate verifier containers, and evaluation continues after failures on the agent's cumulative workspace.

## Run a task

Tasks use [Harbor's multi-step format](https://github.com/harbor-framework/harbor/blob/main/docs-mintlify/tasks/multi-step.mdx). With Docker running and an Anthropic API key set:

```bash
git clone https://github.com/Klavis-AI/Multi-step-SWE.git
cd Multi-step-SWE
uv tool install harbor

harbor run --path tasks/lua-vm-version-chain \
  --agent claude-code --model anthropic/claude-opus-5-5 \
  --effort max --allow-agent-host api.anthropic.com
```

Set `ANTHROPIC_API_KEY` in your environment. Use `--path tasks --n-attempts 4` for a full evaluation; compute and time budgets are specified in each `task.toml`.

Each task includes its environment, ordered steps, instructions, verifiers, and reference solutions. **Compute task solve rate from all step rewards**; Harbor's mean reward is not the task solve rate.

We welcome new model evaluations, verifier improvements, and additional tasks via [issues](https://github.com/Klavis-AI/Multi-step-SWE/issues) and pull requests.

## Citation

```bibtex
@misc{klavis2026multistepswe,
  title  = {{Multi-step SWE: Can Coding Agents Keep Building on
             Their Own Code Over Long Horizons?}},
  author = {{Klavis AI}},
  year   = {2026},
  url    = {https://multistepswe.com/}
}
```

[MIT License](LICENSE). Bundled upstream code retains its original license.
