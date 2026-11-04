//! The shell. This is the only thing the verifier ever runs.
//!
//! It is invoked as
//!
//! ```text
//! limbo -m <mode> -q <database-path>
//! ```
//!
//! with SQL on standard input, and it must print each result row to standard
//! output in the requested mode. See /app/TEST_INTERFACE.md for the exact
//! contract, including the meaning of every option and exit status.

fn main() -> std::process::ExitCode {
    eprintln!("limbo: not implemented");
    std::process::ExitCode::FAILURE
}
