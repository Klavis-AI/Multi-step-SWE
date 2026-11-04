//! `wild` — an ELF linker.
//!
//! The grader builds this workspace with `cargo build --release --bin wild` and
//! runs the resulting executable as a linker. Replace this stub.

fn main() -> std::process::ExitCode {
    let args: Vec<String> = std::env::args().skip(1).collect();
    eprintln!("wild: error: not implemented (args: {})", args.join(" "));
    std::process::ExitCode::from(255)
}
