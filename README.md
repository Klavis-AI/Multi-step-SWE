<h1 align="center">Multi-step SWE</h1>

<p align="center"><em>Can coding agents keep building on their own code over long horizons?</em></p>

## Links

- [Website](https://multistepswe.com/)
- [Paper](https://multistepswe.com/paper.pdf)
- [Leaderboard](https://multistepswe.com/#leaderboard)
- [Tasks](https://multistepswe.com/#tasks)

## Benchmark

Multi-step SWE contains **69 tasks, 574 steps, and 9 languages**. Each task chains **5–15 dependent engineering requests** on one codebase, carrying the agent's own design decisions and errors forward. A step is a complete engineering request, not an agent turn or a tool call, with **2–4 hours per step** and **10–60 hours per task**.

The benchmark covers four families of engineering work:

- **Feature implementation:** [Build a database in Rust](tasks/turso-engine-multiversion) across 15 turso releases.
- **Version upgrade:** [Implement Ethereum's EVM in C](tasks/evm-fork-chain) across 14 hard forks.
- **Migration:** [Migrate a React 15 dashboard to React 19](tasks/react-classes-to-19) through 8 stages.
- **Optimization:** [Accelerate BoringSSL's cryptographic kernels](tasks/boringssl-crypto-asm) while preserving outputs.

Requirements and verifiers share an existing source of truth. Every task passed two rounds of expert review, and steps are graded in separate verifier containers.

Across nine frontier models, the strongest passes **56.8% of steps** but solves only **18.5% of task rollouts**. **38 of 69 tasks remain unsolved** by any evaluated model. A task is solved only when every step receives full credit; see the [leaderboard](https://multistepswe.com/#leaderboard) for results and model–harness configurations.

## Getting Started

Install [Harbor](https://github.com/harbor-framework/harbor):

```bash
uv tool install harbor
```

You also need:

- Docker running.
- An Anthropic API key set as `ANTHROPIC_API_KEY` for the example below.

Clone the benchmark and run a task:

```bash
git clone https://github.com/Klavis-AI/Multi-step-SWE.git
cd Multi-step-SWE

harbor run --path tasks/lua-vm-version-chain \
  --agent claude-code --model anthropic/claude-opus-5-5 \
  --effort max --allow-agent-host api.anthropic.com
```

Use `--path tasks --n-attempts 4` for a full evaluation. Resource requirements and time budgets are specified in each `task.toml`.

Tasks follow [Harbor's multi-step format](https://github.com/harbor-framework/harbor/blob/main/docs-mintlify/tasks/multi-step.mdx), including environments, instructions, verifiers, and reference solutions. Evaluate all steps, including those after a failure. **Compute task solve rate from all step rewards**; Harbor's mean reward is not the task solve rate.

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

## License

[MIT License](LICENSE). Bundled upstream code retains its original license.
