// Verifier-owned conformance driver for milestone 05 of
// earth-mover/icechunk: the command-line interface, expiration and
// garbage-collection accounting, and the object-store configuration surface.
//
// Every assertion here traces either to /app/TEST_INTERFACE.md - which states
// the public Rust surface, the binary's argument grammar, its exit statuses
// and its stdout shape - or to a section of /app/icechunk-spec/ named from
// instruction.md's reading list.
//
// Group = the `<prefix>__` part of a test function's name. The runner invokes
// one `cargo test` per group and asserts the executed count, so every case
// below is independent and leaves no global state behind.
//
// Test form follows the artifact, not a house style:
//   * the library groups call the crate's public API directly;
//   * the `cli_*` groups drive the compiled `icechunk` binary as a PROCESS,
//     through `std::process::Command`, asserting argv grammar, exit status and
//     the shape of stdout. The binary's path arrives in ICECHUNK_TEST_CLI_BIN.
//
// Nothing here reaches the network, reads a cloud credential, starts a server
// or sleeps on a wall clock. Storage is `new_in_memory_storage()` or
// `new_local_filesystem_storage()` over a `tempfile::TempDir`. The S3, R2 and
// Tigris constructors are exercised for their CONFIGURATION behaviour only -
// they build a lazy client and never open a socket.

#![allow(clippy::bool_assert_comparison)]
#![allow(non_snake_case)]

use std::collections::{BTreeSet, HashMap, HashSet};
use std::path::{Path as StdPath, PathBuf};
use std::sync::Arc;

use bytes::Bytes;
use chrono::{TimeDelta, Utc};
use futures::TryStreamExt;
use tokio::sync::RwLock;

use icechunk::{
    Repository, RepositoryConfig, Storage, Store,
    asset_manager::AssetManager,
    config::{ObjectStoreConfig, S3Credentials, S3Options, S3StaticCredentials},
    conflicts::{
        Conflict,
        basic_solver::{BasicConflictSolver, VersionSelection},
    },
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::{ArrayShape, SnapshotInfo, SnapshotProperties},
    },
    new_in_memory_storage, new_local_filesystem_storage, new_s3_storage,
    ops::gc::{
        Action, ExpireRefResult, ExpiredRefAction, GCConfig, GCSummary, expire,
        expire_ref, garbage_collect,
    },
    refs::Ref,
    repository::{RepositoryErrorKind, VersionInfo},
    session::{Session, SessionErrorKind, get_chunk},
    // `ops::stats::repo_chunks_storage`, `ops::{all_roots, pointed_snapshots}`
    // and `storage::new_r2_storage` are NEW at this release, and
    // `S3Options::force_path_style` is a NEW field. They are imported - and the
    // struct literal naming that field is written - inside the case bodies that
    // need them rather than at file scope, so that this prelude still resolves
    // against the PREVIOUS milestone's workspace. The verifier's per-group
    // attribution slice removes case functions but never `use` items or
    // module-level helpers, so a file-scope name that does not exist yet would
    // fail to compile EVERY group - the inherited-regression ones included -
    // instead of only the groups that need this release's new surface.
    storage::{self, new_s3_object_store_storage, new_tigris_storage},
    virtual_chunks::VirtualChunkContainer,
};

type TestResult = Result<(), Box<dyn std::error::Error>>;
type BoxErr = Box<dyn std::error::Error>;

// ---------------------------------------------------------------------------
// Shared library helpers
// ---------------------------------------------------------------------------

/// A fresh, empty in-memory backend.
async fn mem() -> Result<Arc<dyn Storage + Send + Sync>, BoxErr> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(storage)
}

/// A fresh, empty local-filesystem backend rooted at `path`.
async fn local(path: &StdPath) -> Result<Arc<dyn Storage + Send + Sync>, BoxErr> {
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(path).await?;
    Ok(storage)
}

/// A repository whose chunks are never inlined, so every chunk becomes its own
/// object and is therefore visible to byte accounting and to GC.
async fn repo_with_native_chunks(
    storage: &Arc<dyn Storage + Send + Sync>,
) -> Result<Repository, BoxErr> {
    Ok(Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..Default::default()
        }),
        Arc::clone(storage),
        HashMap::new(),
    )
    .await?)
}

fn array_shape(len: u64) -> ArrayShape {
    ArrayShape::new(vec![(len, 1)]).expect("a one-dimensional array shape")
}

fn path(p: &str) -> Path {
    Path::try_from(p).expect("a valid node path")
}

/// Write one chunk at `index` of `array`, materialised through the session's
/// own chunk writer so the repository's inlining threshold decides its form.
async fn write_chunk(
    session: &mut Session,
    array: &Path,
    index: u32,
    bytes: &[u8],
) -> TestResult {
    let payload = session.get_chunk_writer()(Bytes::copy_from_slice(bytes)).await?;
    session
        .set_chunk_ref(array.clone(), ChunkIndices(vec![index]), Some(payload))
        .await?;
    Ok(())
}

async fn read_chunk(
    session: &Session,
    array: &Path,
    index: u32,
) -> Result<Option<Bytes>, BoxErr> {
    Ok(get_chunk(
        session
            .get_chunk_reader(array, &ChunkIndices(vec![index]), &ByteRange::ALL)
            .await?,
    )
    .await?)
}

/// The commit messages reachable from `version`, newest first.
async fn messages(repo: &Repository, version: &VersionInfo) -> Result<Vec<String>, BoxErr> {
    let infos: Vec<SnapshotInfo> =
        repo.ancestry(version).await?.try_collect::<Vec<_>>().await?;
    Ok(infos.into_iter().map(|i| i.message).collect())
}

/// The snapshots reachable from `version`, newest first.
async fn ancestry(
    repo: &Repository,
    version: &VersionInfo,
) -> Result<Vec<SnapshotInfo>, BoxErr> {
    Ok(repo.ancestry(version).await?.try_collect::<Vec<_>>().await?)
}

/// Commit `n` groups onto `main`, returning the snapshot id of each commit in
/// the order they were made.
async fn commit_chain(repo: &Repository, names: &[&str]) -> Result<Vec<SnapshotId>, BoxErr> {
    let mut out = Vec::new();
    for (i, name) in names.iter().enumerate() {
        let mut session = repo.writable_session("main").await?;
        if i == 0 {
            session.add_group(Path::root(), Bytes::new()).await?;
        }
        session.add_group(path(&format!("/n{i}")), Bytes::new()).await?;
        out.push(session.commit(name, None).await?);
    }
    Ok(out)
}

fn asset_manager(storage: &Arc<dyn Storage + Send + Sync>) -> Arc<AssetManager> {
    let settings = storage.default_settings();
    Arc::new(AssetManager::new_no_cache(Arc::clone(storage), settings, 1))
}

// ---------------------------------------------------------------------------
// Shared command-line helpers
//
// The binary is the candidate's own `icechunk` target, built by the verifier
// with `--features cli`; its path is in ICECHUNK_TEST_CLI_BIN. Every
// invocation gets its own XDG_CONFIG_HOME inside a TempDir, so the CLI's
// configuration file is entirely under the driver's control and no invocation
// can see another's.
// ---------------------------------------------------------------------------

fn cli_binary() -> PathBuf {
    let raw = std::env::var("ICECHUNK_TEST_CLI_BIN").expect(
        "ICECHUNK_TEST_CLI_BIN must name the candidate's `icechunk` binary; \
         the verifier exports it after building with --features cli",
    );
    PathBuf::from(raw)
}

struct Cli {
    tmp: tempfile::TempDir,
    config_home: PathBuf,
}

struct Run {
    code: i32,
    stdout: String,
    stderr: String,
}

impl Run {
    /// stdout split into non-empty lines, in order.
    fn lines(&self) -> Vec<&str> {
        self.stdout.lines().filter(|l| !l.trim().is_empty()).collect()
    }
}

impl Cli {
    fn new() -> Result<Self, BoxErr> {
        let tmp = tempfile::TempDir::new()?;
        let config_home = tmp.path().join("config-home");
        std::fs::create_dir_all(config_home.join("icechunk"))?;
        Ok(Cli { tmp, config_home })
    }

    /// An empty directory for a repository, ready for `icechunk repo create`.
    fn repo_dir(&self, name: &str) -> Result<PathBuf, BoxErr> {
        let p = self.tmp.path().join(name);
        std::fs::create_dir_all(&p)?;
        Ok(p)
    }

    fn config_file(&self) -> PathBuf {
        self.config_home.join("icechunk").join("cli-config.yaml")
    }

    fn write_config(&self, body: &str) -> Result<(), BoxErr> {
        std::fs::write(self.config_file(), body)?;
        Ok(())
    }

    /// Write a configuration naming one or more local-filesystem repositories,
    /// in the externally tagged form the CLI itself writes.
    fn write_local_config(&self, entries: &[(&str, &StdPath)]) -> Result<(), BoxErr> {
        let mut body = String::from("repos:\n");
        for (alias, dir) in entries {
            body.push_str(&format!(
                "  {}: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
                alias,
                dir.display()
            ));
        }
        self.write_config(&body)
    }

    fn run(&self, args: &[&str]) -> Result<Run, BoxErr> {
        let out = std::process::Command::new(cli_binary())
            .env("XDG_CONFIG_HOME", &self.config_home)
            .args(args)
            .output()?;
        Ok(Run {
            code: out.status.code().unwrap_or(-1),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        })
    }

    /// The `$HOME/.config` fallback: `XDG_CONFIG_HOME` is removed from the
    /// child's environment entirely and `HOME` names a directory the driver
    /// owns.
    fn run_with_home(&self, home: &StdPath, args: &[&str]) -> Result<Run, BoxErr> {
        let out = std::process::Command::new(cli_binary())
            .env_remove("XDG_CONFIG_HOME")
            .env("HOME", home)
            .args(args)
            .output()?;
        Ok(Run {
            code: out.status.code().unwrap_or(-1),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        })
    }

    /// Copy the configuration this `Cli` already wrote into
    /// `<home>/.config/icechunk/cli-config.yaml` and return `<home>`.
    fn as_home_config(&self) -> Result<PathBuf, BoxErr> {
        let home = self.tmp.path().join("home");
        let dir = home.join(".config").join("icechunk");
        std::fs::create_dir_all(&dir)?;
        std::fs::copy(self.config_file(), dir.join("cli-config.yaml"))?;
        Ok(home)
    }
}

// ===========================================================================
// cli_help_and_usage - the argument grammar and its exit statuses
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn cli_help_and_usage__help_exits_zero_and_lists_every_subcommand() -> TestResult {
    let cli = Cli::new()?;
    let run = cli.run(&["--help"])?;
    assert_eq!(run.code, 0, "`icechunk --help` must exit 0; stderr: {}", run.stderr);
    for sub in ["repo", "snapshot", "config", "help"] {
        assert!(
            run.stdout.contains(sub),
            "`--help` must list the `{sub}` subcommand on stdout, got: {}",
            run.stdout
        );
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_help_and_usage__subcommand_help_documents_its_options() -> TestResult {
    let cli = Cli::new()?;
    let run = cli.run(&["snapshot", "list", "--help"])?;
    assert_eq!(run.code, 0, "subcommand help must exit 0; stderr: {}", run.stderr);
    assert!(
        run.stdout.contains("-n"),
        "`snapshot list --help` must document the -n option: {}",
        run.stdout
    );
    assert!(
        run.stdout.contains("--branch"),
        "`snapshot list --help` must document the --branch option: {}",
        run.stdout
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_help_and_usage__missing_subcommand_is_a_usage_error() -> TestResult {
    let cli = Cli::new()?;
    let run = cli.run(&[])?;
    assert_eq!(run.code, 2, "a bare `icechunk` is a usage error; stdout: {}", run.stdout);
    // Section 5.3's table gives the stream as well as the status: a usage
    // error is reported on stderr. What it says there is not asserted.
    assert!(
        !run.stderr.trim().is_empty(),
        "a usage error must be reported on stderr; stdout was: {}",
        run.stdout
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_help_and_usage__unknown_subcommand_is_a_usage_error() -> TestResult {
    let cli = Cli::new()?;
    let run = cli.run(&["definitely-not-a-subcommand"])?;
    assert_eq!(run.code, 2, "an unknown subcommand is a usage error");
    let run = cli.run(&["repo", "definitely-not-a-subcommand"])?;
    assert_eq!(run.code, 2, "an unknown sub-subcommand is a usage error");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_help_and_usage__unknown_option_is_a_usage_error() -> TestResult {
    let cli = Cli::new()?;
    let run = cli.run(&["--definitely-not-an-option"])?;
    assert_eq!(run.code, 2, "an unrecognised global option is a usage error");
    let run = cli.run(&["config", "list", "--definitely-not-an-option"])?;
    assert_eq!(run.code, 2, "an unrecognised subcommand option is a usage error");

    // Section 5.2: `-f, --force` belongs to `config init` only, so it is not
    // an option of any other subcommand.
    let run = cli.run(&["snapshot", "list", "some-alias", "-f"])?;
    assert_eq!(
        run.code, 2,
        "-f belongs to `config init` only; stdout: {}",
        run.stdout
    );

    // Section 5.2: `config list` takes no arguments.
    let run = cli.run(&["config", "list", "an-unexpected-argument"])?;
    assert_eq!(
        run.code, 2,
        "`config list` takes no arguments; stdout: {}",
        run.stdout
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_help_and_usage__there_is_no_version_option() -> TestResult {
    let cli = Cli::new()?;
    let run = cli.run(&["--version"])?;
    assert_eq!(
        run.code, 2,
        "this binary has no `--version` option, so it is a usage error; stdout: {}",
        run.stdout
    );
    Ok(())
}

// ===========================================================================
// cli_repo_create - `icechunk repo create <alias>`
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn cli_repo_create__creates_a_local_filesystem_repository() -> TestResult {
    let cli = Cli::new()?;
    let dir = cli.repo_dir("repo")?;
    cli.write_local_config(&[("r1", &dir)])?;

    let run = cli.run(&["repo", "create", "r1"])?;
    assert_eq!(run.code, 0, "creating a repository must exit 0; stderr: {}", run.stderr);
    assert!(!run.stdout.trim().is_empty(), "success is reported on stdout");
    assert!(dir.join("refs").is_dir(), "the repository must have a refs/ prefix");
    assert!(dir.join("snapshots").is_dir(), "the repository must have a snapshots/ prefix");
    assert!(
        std::fs::read_dir(dir.join("snapshots"))?.next().is_some(),
        "the initial snapshot must have been written"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_repo_create__refuses_an_existing_repository() -> TestResult {
    let cli = Cli::new()?;
    let dir = cli.repo_dir("repo")?;
    cli.write_local_config(&[("r1", &dir)])?;

    assert_eq!(cli.run(&["repo", "create", "r1"])?.code, 0);
    let again = cli.run(&["repo", "create", "r1"])?;
    assert_eq!(again.code, 1, "creating over an existing repository is a runtime error");
    assert!(!again.stderr.trim().is_empty(), "the failure is reported on stderr");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_repo_create__refuses_an_alias_that_is_not_configured() -> TestResult {
    let cli = Cli::new()?;
    let dir = cli.repo_dir("repo")?;
    cli.write_local_config(&[("r1", &dir)])?;

    let run = cli.run(&["repo", "create", "not-configured"])?;
    assert_eq!(run.code, 1, "an unknown alias is a runtime error, not a usage error");
    assert!(
        std::fs::read_dir(&dir)?.next().is_none(),
        "nothing may be written for an unknown alias"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_repo_create__result_opens_through_the_library() -> TestResult {
    let cli = Cli::new()?;
    let dir = cli.repo_dir("repo")?;
    cli.write_local_config(&[("r1", &dir)])?;
    assert_eq!(cli.run(&["repo", "create", "r1"])?.code, 0);

    let storage = local(&dir).await?;
    assert!(Repository::exists(storage.as_ref()).await?);
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.list_branches().await?, BTreeSet::from(["main".to_string()]));
    let tip = repo.lookup_branch("main").await?;
    assert!(repo.lookup_snapshot(&tip).await?.is_initial());
    Ok(())
}

// ===========================================================================
// cli_snapshot_list - `icechunk snapshot list <REPO> [-n N] [-b BRANCH]`
// ===========================================================================

/// Create a repository through the CLI and add two commits through the
/// library, so that the listing has three snapshots to show.
async fn cli_repo_with_history(cli: &Cli) -> Result<PathBuf, BoxErr> {
    let dir = cli.repo_dir("repo")?;
    cli.write_local_config(&[("r1", &dir)])?;
    assert_eq!(cli.run(&["repo", "create", "r1"])?.code, 0);

    let storage = local(&dir).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    for (i, message) in ["alpha-commit", "omega-commit"].iter().enumerate() {
        let mut session = repo.writable_session("main").await?;
        if i == 0 {
            session.add_group(Path::root(), Bytes::new()).await?;
        }
        session.add_group(path(&format!("/g{i}")), Bytes::new()).await?;
        session.commit(message, None).await?;
    }
    Ok(dir)
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_snapshot_list__prints_one_line_per_snapshot() -> TestResult {
    let cli = Cli::new()?;
    cli_repo_with_history(&cli).await?;

    let run = cli.run(&["snapshot", "list", "r1"])?;
    assert_eq!(run.code, 0, "listing snapshots must exit 0; stderr: {}", run.stderr);
    let lines = run.lines();
    assert_eq!(lines.len(), 3, "two commits plus the initial snapshot: {:?}", lines);
    for line in &lines {
        assert!(
            line.starts_with("SnapshotInfo"),
            "each line is the rendering of one SnapshotInfo: {line}"
        );
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_snapshot_list__reports_manifest_information() -> TestResult {
    let cli = Cli::new()?;
    cli_repo_with_history(&cli).await?;

    let run = cli.run(&["snapshot", "list", "r1"])?;
    assert_eq!(run.code, 0);
    for line in run.lines() {
        assert!(
            line.contains("manifests"),
            "a listed snapshot must report its manifests: {line}"
        );
        assert!(
            line.contains("parent_id"),
            "a listed snapshot must report its parent: {line}"
        );
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_snapshot_list__caps_the_listing_at_n() -> TestResult {
    let cli = Cli::new()?;
    cli_repo_with_history(&cli).await?;

    let run = cli.run(&["snapshot", "list", "r1", "-n", "1"])?;
    assert_eq!(run.code, 0, "stderr: {}", run.stderr);
    assert_eq!(run.lines().len(), 1, "-n 1 shows exactly one snapshot");

    let run = cli.run(&["snapshot", "list", "r1", "-n", "2"])?;
    assert_eq!(run.code, 0);
    assert_eq!(run.lines().len(), 2, "-n 2 shows exactly two snapshots");

    let run = cli.run(&["snapshot", "list", "r1", "-n", "99"])?;
    assert_eq!(run.code, 0);
    assert_eq!(run.lines().len(), 3, "-n beyond the history shows all of it");

    // `-n` is a short option only; there is no `--n`.
    let run = cli.run(&["snapshot", "list", "r1", "--n", "1"])?;
    assert_eq!(run.code, 2, "`-n` has no long form, so `--n` is a usage error");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_snapshot_list__walks_the_named_branch_newest_first() -> TestResult {
    let cli = Cli::new()?;
    cli_repo_with_history(&cli).await?;

    let run = cli.run(&["snapshot", "list", "r1", "-b", "main"])?;
    assert_eq!(run.code, 0, "stderr: {}", run.stderr);
    let lines = run.lines();
    assert_eq!(lines.len(), 3);
    assert!(
        lines[0].contains("omega-commit"),
        "the newest snapshot comes first: {:?}",
        lines
    );
    assert!(
        lines[1].contains("alpha-commit"),
        "then its parent: {:?}",
        lines
    );
    assert!(
        !lines[2].contains("omega-commit") && !lines[2].contains("alpha-commit"),
        "the initial snapshot comes last: {:?}",
        lines
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_snapshot_list__reports_a_missing_branch_or_alias_at_runtime() -> TestResult {
    let cli = Cli::new()?;
    cli_repo_with_history(&cli).await?;

    let missing_branch = cli.run(&["snapshot", "list", "r1", "-b", "no-such-branch"])?;
    assert_eq!(missing_branch.code, 1, "an unknown branch is a runtime error");
    assert!(
        missing_branch.stdout.trim().is_empty(),
        "nothing is listed for an unknown branch"
    );

    let missing_alias = cli.run(&["snapshot", "list", "not-configured"])?;
    assert_eq!(missing_alias.code, 1, "an unknown alias is a runtime error");
    assert!(missing_alias.stdout.trim().is_empty());

    // An unparsable -n, by contrast, never reaches the repository at all.
    let bad_option = cli.run(&["snapshot", "list", "r1", "-n", "not-a-number"])?;
    assert_eq!(bad_option.code, 2, "an ill-typed option value is a usage error");
    Ok(())
}

// ===========================================================================
// cli_config_file - the configuration file and `icechunk config list`
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn cli_config_file__lists_the_configured_repositories() -> TestResult {
    let cli = Cli::new()?;
    let dir = cli.repo_dir("repo")?;
    cli.write_local_config(&[("r1", &dir)])?;

    let run = cli.run(&["config", "list"])?;
    assert_eq!(run.code, 0, "`config list` must exit 0; stderr: {}", run.stderr);
    assert!(run.stdout.contains("r1"), "the alias must be listed: {}", run.stdout);
    assert!(
        run.stdout.contains(&dir.display().to_string()),
        "the repository location must be listed: {}",
        run.stdout
    );
    assert!(
        run.stdout.contains("LocalFileSystem"),
        "the backend kind must be listed: {}",
        run.stdout
    );

    // The same document reached through the $HOME/.config fallback, with
    // XDG_CONFIG_HOME absent from the environment.
    let home = cli.as_home_config()?;
    let run = cli.run_with_home(&home, &["config", "list"])?;
    assert_eq!(
        run.code, 0,
        "$HOME/.config/icechunk/cli-config.yaml is the documented fallback; stderr: {}",
        run.stderr
    );
    assert!(
        run.stdout.contains("r1"),
        "the configuration must be found under $HOME/.config: {}",
        run.stdout
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_config_file__is_empty_when_no_configuration_exists() -> TestResult {
    let cli = Cli::new()?;
    // No configuration file is written at all.
    let run = cli.run(&["config", "list"])?;
    assert_eq!(run.code, 0, "a missing configuration is not an error; stderr: {}", run.stderr);
    assert!(
        run.stdout.contains("repos"),
        "an empty repository map is still printed: {}",
        run.stdout
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_config_file__unreadable_configuration_is_treated_as_empty() -> TestResult {
    let cli = Cli::new()?;
    let dir = cli.repo_dir("repo")?;
    cli.write_config("this is: [not, valid, icechunk, configuration\n")?;

    let run = cli.run(&["config", "list"])?;
    assert_eq!(run.code, 0, "an unparseable configuration is not a usage error");
    assert!(!run.stdout.contains("r1"), "no repository is configured: {}", run.stdout);

    let create = cli.run(&["repo", "create", "r1"])?;
    assert_eq!(create.code, 1, "and no alias resolves");
    assert!(
        std::fs::read_dir(&dir)?.next().is_none(),
        "nothing may be written when the configuration did not parse"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn cli_config_file__resolves_each_alias_to_its_own_location() -> TestResult {
    let cli = Cli::new()?;
    let first = cli.repo_dir("first")?;
    let second = cli.repo_dir("second")?;
    cli.write_local_config(&[("r1", &first), ("r2", &second)])?;

    assert_eq!(cli.run(&["repo", "create", "r2"])?.code, 0);
    assert!(second.join("refs").is_dir(), "the named alias is the one created");
    assert!(
        std::fs::read_dir(&first)?.next().is_none(),
        "the other alias is untouched"
    );

    assert_eq!(cli.run(&["repo", "create", "r1"])?.code, 0);
    assert!(first.join("refs").is_dir());
    Ok(())
}

/// Every one of the five backend variant tags of TEST_INTERFACE.md section 5.5
/// must parse. Nothing here contacts a backend: `config list` only reads the
/// document and prints it back, and a document that fails to parse is treated
/// as empty, so a tag the candidate cannot read shows up as a missing alias.
#[tokio::test(flavor = "multi_thread")]
async fn cli_config_file__parses_every_backend_variant() -> TestResult {
    const EVERY_VARIANT: &str = r#"repos:
  local1: !LocalFileSystem
    path: __PATH__
    config: {}
  s3repo: !S3
    location: { bucket: my-bucket, prefix: my-prefix }
    object_store_config: { region: us-west-2, endpoint_url: null, anonymous: false, allow_http: false, force_path_style: false }
    credentials: { s3_credential_type: from_env }
    config: {}
  tigrisrepo: !Tigris
    location: { bucket: b, prefix: p }
    object_store_config: { region: r, endpoint_url: "https://fly.storage.tigris.dev", anonymous: false, allow_http: false, force_path_style: false }
    credentials: { s3_credential_type: from_env }
    config: {}
  gcsrepo: !GCS
    location: { bucket: b, prefix: p }
    object_store_config: {}
    credentials: { gcs_credential_type: from_env }
    config: {}
  azrepo: !Azure
    location: { account: a, container: c, prefix: p }
    object_store_config: {}
    credentials: { az_credential_type: from_env }
    config: {}
"#;

    let cli = Cli::new()?;
    let dir = cli.repo_dir("local")?;
    cli.write_config(&EVERY_VARIANT.replace("__PATH__", &dir.display().to_string()))?;

    let run = cli.run(&["config", "list"])?;
    assert_eq!(run.code, 0, "`config list` must exit 0; stderr: {}", run.stderr);
    for alias in ["local1", "s3repo", "tigrisrepo", "gcsrepo", "azrepo"] {
        assert!(
            run.stdout.contains(alias),
            "the `{alias}` entry did not survive the round trip, so its variant tag \
             was not parsed: {}",
            run.stdout
        );
    }
    for kind in ["LocalFileSystem", "S3", "Tigris", "GCS", "Azure"] {
        assert!(
            run.stdout.contains(kind),
            "the `{kind}` backend kind must be named: {}",
            run.stdout
        );
    }
    Ok(())
}

// ===========================================================================
// ops_lookup_snapshot - Repository::lookup_snapshot
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn ops_lookup_snapshot__describes_a_committed_snapshot() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["a", "b"]).await?;

    let info = repo.lookup_snapshot(&ids[1]).await?;
    assert_eq!(info.id, ids[1]);
    assert_eq!(info.message, "b");
    assert_eq!(info.parent_id.as_ref(), Some(&ids[0]));
    assert!(!info.is_initial());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_lookup_snapshot__unknown_id_is_an_error() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_chain(&repo, &["a"]).await?;

    assert!(
        repo.lookup_snapshot(&SnapshotId::random()).await.is_err(),
        "a snapshot that was never written cannot be looked up"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_lookup_snapshot__finds_the_initial_snapshot() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let root = repo.lookup_branch("main").await?;

    let info = repo.lookup_snapshot(&root).await?;
    assert_eq!(info.id, root);
    assert!(info.parent_id.is_none());
    assert!(info.is_initial());
    assert!(info.manifests.is_empty(), "the initial snapshot holds no data");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_lookup_snapshot__unreachable_snapshots_are_still_stored() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["a", "b"]).await?;

    repo.reset_branch("main", &ids[0]).await?;
    assert_eq!(repo.lookup_branch("main").await?, ids[0]);

    // `b` is no longer reachable from any reference, but it has not been
    // deleted, so looking it up still succeeds.
    assert_eq!(repo.lookup_snapshot(&ids[1]).await?.id, ids[1]);
    assert!(
        repo.readonly_session(&VersionInfo::SnapshotId(ids[1].clone())).await.is_ok()
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_lookup_snapshot__reports_the_absence_left_by_collection() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["a", "b"]).await?;
    repo.reset_branch("main", &ids[0]).await?;

    let settings = storage.default_settings();
    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1, "only the stranded snapshot goes");

    assert!(repo.lookup_snapshot(&ids[1]).await.is_err(), "the object is gone");
    assert_eq!(repo.lookup_snapshot(&ids[0]).await?.id, ids[0], "the reachable one stays");
    Ok(())
}

/// `resolve_version` answers the question every reader-facing entry point asks
/// first: which snapshot does this `VersionInfo` name? It became part of the
/// public surface at this milestone (section 6).
#[tokio::test(flavor = "multi_thread")]
async fn ops_lookup_snapshot__resolves_every_version_reference() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["one", "two"]).await?;
    repo.create_tag("release", &ids[0]).await?;
    repo.create_branch("side", &ids[0]).await?;

    assert_eq!(
        repo.resolve_version(&VersionInfo::BranchTipRef("main".to_string())).await?,
        ids[1],
        "a branch reference resolves to that branch's tip"
    );
    assert_eq!(
        repo.resolve_version(&VersionInfo::BranchTipRef("side".to_string())).await?,
        ids[0],
        "and each branch to its own"
    );
    assert_eq!(
        repo.resolve_version(&VersionInfo::TagRef("release".to_string())).await?,
        ids[0],
        "a tag resolves to the snapshot it was created at"
    );
    assert_eq!(
        repo.resolve_version(&VersionInfo::SnapshotId(ids[1].clone())).await?,
        ids[1],
        "a snapshot id resolves to itself"
    );
    assert!(
        repo.resolve_version(&VersionInfo::BranchTipRef("no-such-branch".to_string()))
            .await
            .is_err(),
        "a reference that does not exist cannot be resolved"
    );

    // What it hands back is the id `lookup_snapshot` describes.
    let tip = repo.resolve_version(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(repo.lookup_snapshot(&tip).await?.message, "two");
    Ok(())
}

// ===========================================================================
// ops_chunks_storage - ops::stats::repo_chunks_storage
// ===========================================================================

/// A repository whose inlining threshold is 5 bytes: six-byte chunks become
/// their own objects, one-byte chunks are inlined into the manifest.
async fn stats_repo(
    storage: &Arc<dyn Storage + Send + Sync>,
) -> Result<(Repository, Path), BoxErr> {
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(5),
            ..Default::default()
        }),
        Arc::clone(storage),
        HashMap::new(),
    )
    .await?;
    let array = path("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(100), None, Bytes::new()).await?;
    session.commit("create array", None).await?;
    Ok((repo, array))
}

const SIX: &[u8] = &[0, 1, 2, 3, 4, 5];

#[tokio::test(flavor = "multi_thread")]
async fn ops_chunks_storage__is_zero_before_the_first_commit() -> TestResult {
    use icechunk::ops::stats::repo_chunks_storage;
    let storage = mem().await?;
    let settings = storage.default_settings();
    let am = asset_manager(&storage);
    let (repo, array) = stats_repo(&storage).await?;

    let mut session = repo.writable_session("main").await?;
    for idx in 0..10 {
        write_chunk(&mut session, &array, idx, SIX).await?;
    }
    assert_eq!(
        repo_chunks_storage(storage.as_ref(), &settings, Arc::clone(&am)).await?,
        0,
        "uncommitted chunks are not committed storage"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_chunks_storage__counts_every_committed_native_chunk() -> TestResult {
    use icechunk::ops::stats::repo_chunks_storage;
    let storage = mem().await?;
    let settings = storage.default_settings();
    let am = asset_manager(&storage);
    let (repo, array) = stats_repo(&storage).await?;

    let mut session = repo.writable_session("main").await?;
    for idx in 0..50 {
        write_chunk(&mut session, &array, idx, SIX).await?;
    }
    session.commit("first", None).await?;
    assert_eq!(
        repo_chunks_storage(storage.as_ref(), &settings, Arc::clone(&am)).await?,
        50 * 6
    );

    let mut session = repo.writable_session("main").await?;
    for idx in 50..60 {
        write_chunk(&mut session, &array, idx, SIX).await?;
    }
    session.commit("second", None).await?;
    assert_eq!(
        repo_chunks_storage(storage.as_ref(), &settings, Arc::clone(&am)).await?,
        60 * 6,
        "a second commit adds its own chunks"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_chunks_storage__excludes_inline_and_virtual_chunks() -> TestResult {
    use icechunk::ops::stats::repo_chunks_storage;
    let storage = mem().await?;
    let settings = storage.default_settings();
    let am = asset_manager(&storage);
    let (repo, array) = stats_repo(&storage).await?;

    let mut session = repo.writable_session("main").await?;
    for idx in 0..20 {
        write_chunk(&mut session, &array, idx, SIX).await?;
    }
    // Below the threshold: stored inside the manifest, not as an object.
    for idx in 20..30 {
        write_chunk(&mut session, &array, idx, &[7]).await?;
    }
    // Virtual: the bytes live outside the repository entirely.
    for idx in 30..40 {
        let payload = ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_absolute_path("s3://bucket/object")?,
            offset: idx as u64,
            length: 1024,
            checksum: None,
        });
        session
            .set_chunk_ref(array.clone(), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }
    session.commit("mixed", None).await?;

    assert_eq!(
        repo_chunks_storage(storage.as_ref(), &settings, Arc::clone(&am)).await?,
        20 * 6,
        "only native chunks count"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn ops_chunks_storage__respects_the_repositorys_inlining_threshold() -> TestResult {
    use icechunk::ops::stats::repo_chunks_storage;
    let storage = mem().await?;
    let settings = storage.default_settings();
    let am = asset_manager(&storage);

    // The default threshold is large enough that these chunks are inlined, so
    // the repository holds no chunk objects at all.
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array = path("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(16), None, Bytes::new()).await?;
    for idx in 0..16 {
        write_chunk(&mut session, &array, idx, SIX).await?;
    }
    session.commit("inlined", None).await?;
    assert_eq!(
        repo_chunks_storage(storage.as_ref(), &settings, Arc::clone(&am)).await?,
        0,
        "under the default threshold every small chunk is inline"
    );
    Ok(())
}

// ===========================================================================
// refs_need_existing_snapshot - create_branch / create_tag validation
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn refs_need_existing_snapshot__tag_at_an_unknown_snapshot_is_refused() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    assert!(repo.create_tag("foo", &SnapshotId::random()).await.is_err());
    assert_eq!(repo.list_tags().await?, BTreeSet::new(), "no tag may be left behind");
    assert!(repo.lookup_tag("foo").await.is_err());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn refs_need_existing_snapshot__branch_at_an_unknown_snapshot_is_refused() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    assert!(repo.create_branch("bar", &SnapshotId::random()).await.is_err());
    assert_eq!(
        repo.list_branches().await?,
        BTreeSet::from(["main".to_string()]),
        "no branch may be left behind"
    );
    assert!(repo.lookup_branch("bar").await.is_err());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn refs_need_existing_snapshot__an_existing_snapshot_is_accepted() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["a"]).await?;

    repo.create_branch("branch1", &ids[0]).await?;
    repo.create_tag("tag1", &ids[0]).await?;
    assert_eq!(repo.lookup_branch("branch1").await?, ids[0]);
    assert_eq!(repo.lookup_tag("tag1").await?, ids[0]);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn refs_need_existing_snapshot__a_refused_name_stays_available() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["a"]).await?;

    assert!(repo.create_branch("later", &SnapshotId::random()).await.is_err());
    assert!(repo.create_tag("later", &SnapshotId::random()).await.is_err());

    // The refused attempts wrote nothing, so the same names are still free.
    repo.create_branch("later", &ids[0]).await?;
    repo.create_tag("later", &ids[0]).await?;
    assert_eq!(repo.lookup_branch("later").await?, ids[0]);
    assert_eq!(repo.lookup_tag("later").await?, ids[0]);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn refs_need_existing_snapshot__reset_branch_validates_its_target() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["a", "b"]).await?;
    assert_eq!(repo.lookup_branch("main").await?, ids[1]);

    assert!(
        repo.reset_branch("main", &SnapshotId::random()).await.is_err(),
        "reset_branch validates its target the same way create_branch does"
    );
    assert_eq!(
        repo.lookup_branch("main").await?,
        ids[1],
        "a refused reset must leave the branch where it was"
    );

    // The same call against a snapshot that does exist is accepted.
    repo.reset_branch("main", &ids[0]).await?;
    assert_eq!(repo.lookup_branch("main").await?, ids[0]);
    Ok(())
}

// ===========================================================================
// commit_default_metadata - repository-level commit metadata
// ===========================================================================

fn props(entries: &[(&str, &str)]) -> SnapshotProperties {
    let mut p = SnapshotProperties::default();
    for (k, v) in entries {
        p.insert((*k).to_string(), serde_json::Value::String((*v).to_string()));
    }
    p
}

#[tokio::test(flavor = "multi_thread")]
async fn commit_default_metadata__is_empty_until_it_is_set() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert!(repo.default_commit_metadata().is_empty());

    let ids = commit_chain(&repo, &["a"]).await?;
    assert!(
        repo.lookup_snapshot(&ids[0]).await?.metadata.is_empty(),
        "with no repository defaults a commit carries no metadata"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn commit_default_metadata__round_trips_through_the_accessor() -> TestResult {
    let storage = mem().await?;
    let mut repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let defaults = props(&[("author", "John Doe"), ("project", "My Project")]);
    repo.set_default_commit_metadata(defaults.clone());
    assert_eq!(repo.default_commit_metadata(), &defaults);

    repo.set_default_commit_metadata(SnapshotProperties::default());
    assert!(repo.default_commit_metadata().is_empty(), "the defaults can be cleared");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn commit_default_metadata__applies_to_a_commit_with_no_properties() -> TestResult {
    let storage = mem().await?;
    let mut repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let defaults = props(&[("author", "John Doe"), ("project", "My Project")]);
    repo.set_default_commit_metadata(defaults.clone());

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let id = session.commit("commit", None).await?;

    assert_eq!(
        repo.lookup_snapshot(&id).await?.metadata,
        defaults,
        "passing no properties yields exactly the repository defaults"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn commit_default_metadata__user_properties_win_key_by_key() -> TestResult {
    let storage = mem().await?;
    let mut repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    repo.set_default_commit_metadata(props(&[
        ("author", "John Doe"),
        ("project", "My Project"),
    ]));

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let id = session
        .commit("commit", Some(props(&[("author", "Jane Doe"), ("id", "ideded")])))
        .await?;

    let metadata = repo.lookup_snapshot(&id).await?.metadata;
    assert_eq!(
        metadata,
        props(&[
            ("author", "Jane Doe"),
            ("project", "My Project"),
            ("id", "ideded"),
        ]),
        "the user's value replaces the default for a key it sets, and the \
         defaults survive for the keys it does not"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn commit_default_metadata__applies_to_every_later_commit() -> TestResult {
    let storage = mem().await?;
    let mut repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let before = session.commit("before", None).await?;

    let defaults = props(&[("author", "John Doe")]);
    repo.set_default_commit_metadata(defaults.clone());

    let mut ids = Vec::new();
    for i in 0..3 {
        let mut session = repo.writable_session("main").await?;
        session.add_group(path(&format!("/g{i}")), Bytes::new()).await?;
        ids.push(session.commit("after", None).await?);
    }

    assert!(
        repo.lookup_snapshot(&before).await?.metadata.is_empty(),
        "a commit made before the defaults were set is unaffected"
    );
    for id in ids {
        assert_eq!(repo.lookup_snapshot(&id).await?.metadata, defaults);
    }
    Ok(())
}

// ===========================================================================
// gc_byte_accounting - garbage_collect's summary
// ===========================================================================

/// A repository with two commits, the second of which overwrites one chunk,
/// then reset so the second commit is unreachable. Returns the repository, the
/// array path and the snapshot that is still reachable.
async fn stranded_commit(
    storage: &Arc<dyn Storage + Send + Sync>,
) -> Result<(Repository, Path, SnapshotId), BoxErr> {
    let repo = repo_with_native_chunks(storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[7u8; 64]).await?;
    let first = session.commit("first", None).await?;

    let mut session = repo.writable_session("main").await?;
    write_chunk(&mut session, &array, 0, &[9u8; 64]).await?;
    session.commit("second", None).await?;

    repo.reset_branch("main", &first).await?;
    Ok((repo, array, first))
}

#[tokio::test(flavor = "multi_thread")]
async fn gc_byte_accounting__reports_objects_and_bytes_freed() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, _array, _first) = stranded_commit(&storage).await?;

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;

    assert_eq!(summary.chunks_deleted, 1);
    assert_eq!(summary.manifests_deleted, 1);
    assert_eq!(summary.snapshots_deleted, 1);
    assert!(
        summary.bytes_deleted >= 64,
        "the freed bytes must at least cover the 64-byte chunk, got {}",
        summary.bytes_deleted
    );
    assert!(
        summary.bytes_deleted > summary.chunks_deleted,
        "bytes_deleted counts bytes, not objects: {} vs {}",
        summary.bytes_deleted,
        summary.chunks_deleted
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn gc_byte_accounting__keeps_everything_that_is_reachable() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[7u8; 64]).await?;
    session.commit("only", None).await?;

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert_eq!(summary, GCSummary::default(), "nothing is dangling, so nothing goes");

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        read_chunk(&session, &array, 0).await?.as_deref(),
        Some(&[7u8; 64][..]),
        "the data survives"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn gc_byte_accounting__deletes_nothing_when_every_class_is_kept() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, _array, _first) = stranded_commit(&storage).await?;

    let keep_everything = GCConfig::new(
        HashSet::new(),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
    );
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &keep_everything,
    )
    .await?;
    assert_eq!(summary, GCSummary::default());
    assert_eq!(keep_everything.deletes_chunks(), false);
    assert_eq!(keep_everything.deletes_manifests(), false);
    assert_eq!(keep_everything.deletes_attributes(), false);
    assert_eq!(keep_everything.deletes_transaction_logs(), false);
    assert_eq!(keep_everything.deletes_snapshots(), false);

    // Each predicate reports its own class: a configuration that deletes only
    // dangling chunks reports `deletes_chunks` and nothing else.
    let chunks_only = GCConfig::new(
        HashSet::new(),
        Action::DeleteIfCreatedBefore(Utc::now()),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
    );
    assert_eq!(chunks_only.deletes_chunks(), true);
    assert_eq!(chunks_only.deletes_manifests(), false);
    assert_eq!(chunks_only.deletes_attributes(), false);
    assert_eq!(chunks_only.deletes_transaction_logs(), false);
    assert_eq!(chunks_only.deletes_snapshots(), false);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn gc_byte_accounting__leaves_the_repository_usable() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, array, first) = stranded_commit(&storage).await?;

    let now = Utc::now();
    garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;

    // The same `Repository` object - whose asset manager the collection just
    // mutated - must still read, and must still be able to commit.
    assert_eq!(repo.lookup_branch("main").await?, first);
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        read_chunk(&session, &array, 0).await?.as_deref(),
        Some(&[7u8; 64][..])
    );

    let mut session = repo.writable_session("main").await?;
    write_chunk(&mut session, &array, 1, &[3u8; 64]).await?;
    let after = session.commit("after gc", None).await?;
    assert_eq!(repo.lookup_snapshot(&after).await?.message, "after gc");
    Ok(())
}

/// The reachability the collector starts from, read directly: `all_roots`
/// yields what the references point at plus the caller's extra roots, and
/// `pointed_snapshots` expands those into every snapshot a reference reaches.
#[tokio::test(flavor = "multi_thread")]
async fn gc_byte_accounting__reachability_is_reported_by_the_ops_module() -> TestResult {
    use icechunk::ops::{all_roots, pointed_snapshots};
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["one", "two"]).await?;
    repo.create_tag("release", &ids[0]).await?;
    let root = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .pop()
        .expect("an initial snapshot")
        .id;

    let extra = SnapshotId::random();
    let extra_roots = HashSet::from([extra.clone()]);
    let roots: HashSet<SnapshotId> = all_roots(storage.as_ref(), &settings, &extra_roots)
        .await?
        .try_collect::<Vec<_>>()
        .await?
        .into_iter()
        .collect();
    assert!(roots.contains(&ids[1]), "the branch tip is a root: {roots:?}");
    assert!(roots.contains(&ids[0]), "the tag's snapshot is a root: {roots:?}");
    assert!(roots.contains(&extra), "a caller's extra root is a root: {roots:?}");

    let no_extra: HashSet<SnapshotId> = HashSet::new();
    let pointed: HashSet<SnapshotId> = pointed_snapshots(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &no_extra,
    )
    .await?
    .try_collect::<Vec<_>>()
    .await?
    .into_iter()
    .collect();
    assert_eq!(
        pointed,
        HashSet::from([ids[0].clone(), ids[1].clone(), root.clone()]),
        "every snapshot a reference reaches, and nothing else"
    );
    Ok(())
}

// ===========================================================================
// expire_ref_accounting - ops::gc::expire_ref
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn expire_ref_accounting__does_nothing_when_nothing_is_old_enough() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_chain(&repo, &["first", "second", "third"]).await?;

    let result = expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        Utc::now() - TimeDelta::days(10),
    )
    .await?;

    match result {
        ExpireRefResult::NothingToDo { ref_is_expired } => assert!(
            !ref_is_expired,
            "a threshold older than every commit leaves the ref unexpired"
        ),
        other => panic!("expected NothingToDo, got {other:?}"),
    }
    assert_eq!(
        messages(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?.len(),
        4,
        "the history is untouched"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_ref_accounting__flags_and_squashes_a_fully_expired_ref() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["first", "second", "third"]).await?;
    let root = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .pop()
        .expect("an initial snapshot")
        .id;

    let result = expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        Utc::now() + TimeDelta::days(10),
    )
    .await?;

    match result {
        ExpireRefResult::Done { ref_is_expired, released_snapshots, edited_snapshot } => {
            assert!(ref_is_expired, "every commit is past the threshold");
            assert_eq!(edited_snapshot, ids[2], "the tip is the snapshot rewritten");
            assert!(released_snapshots.contains(&ids[0]));
            assert!(released_snapshots.contains(&ids[1]));
            assert!(
                !released_snapshots.contains(&root),
                "the first snapshot of a repository is never expired"
            );
        }
        other => panic!("expected Done, got {other:?}"),
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_ref_accounting__leaves_a_well_formed_shorter_history() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_chain(&repo, &["first", "second", "third"]).await?;

    expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        Utc::now() + TimeDelta::days(10),
    )
    .await?;

    let history = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(history.len(), 2, "only the tip and the root remain: {history:?}");
    assert_eq!(history[0].message, "third");
    assert!(history[1].is_initial(), "the root is still the root");
    assert_eq!(history[0].parent_id.as_ref(), Some(&history[1].id));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_ref_accounting__reports_expiry_for_a_tag_without_rewriting_it() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["first"]).await?;
    repo.create_tag("release", &ids[0]).await?;

    // The tag's snapshot is the child of the root, so there is nothing to
    // squash - but the ref itself is older than the threshold and is reported
    // as expired all the same.
    let result = expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Tag("release".to_string()),
        Utc::now() + TimeDelta::days(10),
    )
    .await?;
    match result {
        ExpireRefResult::NothingToDo { ref_is_expired } => assert!(ref_is_expired),
        other => panic!("expected NothingToDo, got {other:?}"),
    }
    assert_eq!(repo.lookup_tag("release").await?, ids[0], "the tag is not deleted here");
    Ok(())
}

/// The third `NothingToDo` case of section 4.10.2: the snapshot that would be
/// rewritten IS the repository's initial snapshot, because `main` has never
/// moved off it.
#[tokio::test(flavor = "multi_thread")]
async fn expire_ref_accounting__nothing_to_do_at_the_initial_snapshot() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let result = expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        Utc::now() + TimeDelta::days(10),
    )
    .await?;
    match result {
        ExpireRefResult::NothingToDo { ref_is_expired } => assert!(
            ref_is_expired,
            "the tip is older than the threshold, so the reference is reported expired"
        ),
        other => panic!("expected NothingToDo, got {other:?}"),
    }

    let history = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(history.len(), 1, "nothing was rewritten or released: {history:?}");
    assert!(history[0].is_initial(), "the initial snapshot is never expired");
    Ok(())
}

// ===========================================================================
// expire_then_collect - ops::gc::expire followed by garbage_collect
// ===========================================================================

/// A repository with four commits on `main` and a tag on the second.
async fn expirable_repo(
    storage: &Arc<dyn Storage + Send + Sync>,
) -> Result<(Repository, Vec<SnapshotId>), BoxErr> {
    let repo = Repository::create(None, Arc::clone(storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["one", "two", "three", "four"]).await?;
    repo.create_tag("release", &ids[1]).await?;
    Ok((repo, ids))
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_then_collect__ignored_refs_are_left_in_place() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, ids) = expirable_repo(&storage).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now() + TimeDelta::days(10),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;

    assert!(!result.released_snapshots.is_empty(), "snapshots were released");
    assert!(
        result.edited_snapshots.contains(&ids[3]),
        "the snapshot each expired reference had rewritten is aggregated here: {:?}",
        result.edited_snapshots
    );
    assert!(result.deleted_refs.is_empty(), "Ignore deletes no reference");
    assert_eq!(repo.list_tags().await?, BTreeSet::from(["release".to_string()]));
    assert_eq!(repo.list_branches().await?, BTreeSet::from(["main".to_string()]));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_then_collect__expired_tags_can_be_deleted() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, _ids) = expirable_repo(&storage).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now() + TimeDelta::days(10),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Delete,
    )
    .await?;

    assert!(
        result.deleted_refs.contains(&Ref::Tag("release".to_string())),
        "the expired tag is reported as deleted: {:?}",
        result.deleted_refs
    );
    assert_eq!(repo.list_tags().await?, BTreeSet::new());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_then_collect__the_default_branch_is_never_deleted() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, ids) = expirable_repo(&storage).await?;
    repo.create_branch("side", &ids[2]).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now() + TimeDelta::days(10),
        ExpiredRefAction::Delete,
        ExpiredRefAction::Ignore,
    )
    .await?;

    assert!(
        !result.deleted_refs.contains(&Ref::Branch("main".to_string())),
        "main is never deleted: {:?}",
        result.deleted_refs
    );
    assert!(repo.list_branches().await?.contains("main"));
    assert!(
        result.deleted_refs.contains(&Ref::Branch("side".to_string())),
        "an expired non-default branch is deleted: {:?}",
        result.deleted_refs
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn expire_then_collect__collection_removes_the_released_snapshots() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, _ids) = expirable_repo(&storage).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now() + TimeDelta::days(10),
        ExpiredRefAction::Delete,
        ExpiredRefAction::Delete,
    )
    .await?;
    let released: Vec<SnapshotId> = result.released_snapshots.iter().cloned().collect();
    assert!(!released.is_empty());

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert!(summary.snapshots_deleted > 0, "expired snapshots are collectable");

    // A released snapshot that a reference still points at survives; every
    // other released snapshot is gone.
    let reachable: HashSet<SnapshotId> =
        ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string()))
            .await?
            .into_iter()
            .map(|i| i.id)
            .collect();
    let mut checked = 0u32;
    for id in &released {
        if reachable.contains(id) {
            // Section 4.10.2: releasing a snapshot does not delete it - a
            // reference still pointing at it keeps it alive through the
            // collection that follows.
            assert!(
                repo.lookup_snapshot(id).await.is_ok(),
                "a released snapshot a reference still points at must survive collection"
            );
            continue;
        }
        checked += 1;
        assert!(
            repo.lookup_snapshot(id).await.is_err(),
            "an unreachable released snapshot must be collected"
        );
    }
    assert!(checked > 0, "the expiration released at least one collectable snapshot");
    // The branch tip still resolves and the history is well formed.
    let history = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(history[0].message, "four");
    assert!(history.last().expect("a root").is_initial());
    Ok(())
}

// ===========================================================================
// snapshot_manifests - SnapshotInfo::manifests
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn snapshot_manifests__are_empty_for_a_metadata_only_commit() -> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(path("/a"), array_shape(4), None, Bytes::new()).await?;
    let id = session.commit("no chunks", None).await?;

    assert!(
        repo.lookup_snapshot(&id).await?.manifests.is_empty(),
        "an array with no chunks needs no manifest"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn snapshot_manifests__describe_the_written_manifest() -> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/a");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[1u8; 64]).await?;
    write_chunk(&mut session, &array, 1, &[2u8; 64]).await?;
    let id = session.commit("chunks", None).await?;

    let info = repo.lookup_snapshot(&id).await?;
    assert_eq!(info.manifests.len(), 1, "one array, one manifest");
    assert_eq!(info.manifests[0].num_chunk_refs, 2, "both chunk references are counted");
    assert!(info.manifests[0].size_bytes > 0, "the manifest has a recorded size");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn snapshot_manifests__are_carried_into_later_snapshots() -> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/a");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[1u8; 64]).await?;
    let with_chunks = session.commit("chunks", None).await?;
    let first_manifest = repo.lookup_snapshot(&with_chunks).await?.manifests;

    // A later commit that touches no chunk still references the same manifest.
    let mut session = repo.writable_session("main").await?;
    session.add_group(path("/later"), Bytes::new()).await?;
    let later = session.commit("a group", None).await?;

    let info = repo.lookup_snapshot(&later).await?;
    assert_eq!(info.manifests.len(), 1);
    assert_eq!(info.manifests[0].id, first_manifest[0].id, "the same manifest object");
    assert_eq!(info.manifests[0].num_chunk_refs, first_manifest[0].num_chunk_refs);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn snapshot_manifests__accompany_the_rest_of_the_snapshot_record() -> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/a");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let groups_only = session.commit("groups", None).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[5u8; 64]).await?;
    let with_chunks = session.commit("chunks", None).await?;

    let info = repo.lookup_snapshot(&with_chunks).await?;
    assert_eq!(info.id, with_chunks);
    assert_eq!(info.parent_id.as_ref(), Some(&groups_only));
    assert_eq!(info.message, "chunks");
    assert_eq!(info.manifests.len(), 1);
    assert!(repo.lookup_snapshot(&groups_only).await?.manifests.is_empty());
    Ok(())
}

// ===========================================================================
// distributed_write_merge - marshal, merge, commit once
// ===========================================================================

/// A repository with one array ready to receive chunks from several writers.
async fn distributed_repo(
    storage: &Arc<dyn Storage + Send + Sync>,
) -> Result<(Repository, Path), BoxErr> {
    let repo = repo_with_native_chunks(storage).await?;
    let array = path("/a");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(8), None, Bytes::new()).await?;
    session.commit("create array", None).await?;
    Ok((repo, array))
}

#[tokio::test(flavor = "multi_thread")]
async fn distributed_write_merge__folds_many_sessions_into_one_commit() -> TestResult {
    let storage = mem().await?;
    let (repo, array) = distributed_repo(&storage).await?;

    let mut marshalled: Vec<Vec<u8>> = Vec::new();
    for i in 0..3u32 {
        let mut worker = repo.writable_session("main").await?;
        write_chunk(&mut worker, &array, i, &[i as u8; 8]).await?;
        marshalled.push(worker.as_bytes()?);
    }

    let mut root = repo.writable_session("main").await?;
    for bytes in marshalled {
        let remote = Session::from_bytes(bytes)?;
        root.merge(remote.into()).await?;
    }
    let snapshot = root.commit("distributed commit", None).await?;

    let history = ancestry(&repo, &VersionInfo::SnapshotId(snapshot.clone())).await?;
    assert_eq!(history[0].message, "distributed commit");
    assert_eq!(
        history.len(),
        3,
        "the three workers produced ONE commit, not three: {:?}",
        history.iter().map(|i| i.message.as_str()).collect::<Vec<_>>()
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn distributed_write_merge__every_merged_chunk_survives() -> TestResult {
    let storage = mem().await?;
    let (repo, array) = distributed_repo(&storage).await?;

    let mut marshalled: Vec<Vec<u8>> = Vec::new();
    for i in 0..3u32 {
        let mut worker = repo.writable_session("main").await?;
        write_chunk(&mut worker, &array, i, &[i as u8; 8]).await?;
        marshalled.push(worker.as_bytes()?);
    }
    let mut root = repo.writable_session("main").await?;
    for bytes in marshalled {
        root.merge(Session::from_bytes(bytes)?.into()).await?;
    }
    root.commit("distributed commit", None).await?;

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    for i in 0..3u32 {
        assert_eq!(
            read_chunk(&session, &array, i).await?.as_deref(),
            Some(&[i as u8; 8][..]),
            "chunk {i} written by worker {i} must be readable"
        );
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn distributed_write_merge__a_read_only_session_refuses_a_merge() -> TestResult {
    let storage = mem().await?;
    let (repo, array) = distributed_repo(&storage).await?;

    let mut worker = repo.writable_session("main").await?;
    write_chunk(&mut worker, &array, 0, &[1u8; 8]).await?;
    let changes = worker.discard_changes();

    let mut reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(reader.read_only());
    let err = reader.merge(changes).await.expect_err("a read-only session cannot merge");
    assert!(
        matches!(err.kind, SessionErrorKind::ReadOnlySession),
        "expected a read-only-session failure, got {:?}",
        err.kind
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn distributed_write_merge__discard_changes_hands_the_work_over() -> TestResult {
    let storage = mem().await?;
    let (repo, array) = distributed_repo(&storage).await?;

    let mut worker = repo.writable_session("main").await?;
    write_chunk(&mut worker, &array, 4, &[42u8; 8]).await?;
    assert!(worker.has_uncommitted_changes());

    let changes = worker.discard_changes();
    assert!(
        !worker.has_uncommitted_changes(),
        "discard_changes takes the change set out of the session"
    );

    let mut coordinator = repo.writable_session("main").await?;
    coordinator.merge(changes).await?;
    assert!(coordinator.has_uncommitted_changes());
    coordinator.commit("handed over", None).await?;

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        read_chunk(&session, &array, 4).await?.as_deref(),
        Some(&[42u8; 8][..])
    );
    Ok(())
}

/// `Repository::as_bytes` / `from_bytes` are NEW at this milestone
/// (section 4.3). They are the other half of the marshalling this reduction
/// needs: a coordinator in another process revives the repository handle
/// before it can revive a writer's session against it.
#[tokio::test(flavor = "multi_thread")]
async fn distributed_write_merge__a_repository_marshals_for_its_writers() -> TestResult {
    // A local-filesystem backend, because a marshalled repository carries a
    // description of its backend rather than the backend's contents: an
    // in-memory one revives empty by construction.
    let tmp = tempfile::TempDir::new()?;
    let storage = local(tmp.path()).await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[9u8; 64]).await?;
    let id = session.commit("marshalled", None).await?;

    let revived = Repository::from_bytes(repo.as_bytes()?)?;
    assert_eq!(
        revived.config().inline_chunk_threshold_bytes(),
        repo.config().inline_chunk_threshold_bytes(),
        "the configuration survives the round trip"
    );
    assert_eq!(revived.lookup_branch("main").await?, id, "and so does the backend");
    let reader = revived.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert_eq!(
        read_chunk(&reader, &array, 0).await?.as_deref(),
        Some(&[9u8; 64][..]),
        "a revived repository reads the data the original wrote"
    );
    Ok(())
}

// ===========================================================================
// storage_settings_surface - storage::Settings and its unsafe_* switches
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn storage_settings_surface__defaults_leave_every_switch_on() -> TestResult {
    let settings = storage::Settings::default();
    assert_eq!(settings.unsafe_use_conditional_create, None);
    assert_eq!(settings.unsafe_use_conditional_update, None);
    assert_eq!(settings.unsafe_use_metadata, None);
    assert_eq!(settings.unsafe_use_conditional_create(), true);
    assert_eq!(settings.unsafe_use_conditional_update(), true);
    assert_eq!(settings.unsafe_use_metadata(), true);
    // `concurrency` is the fourth field, and like the three switches its
    // accessor answers with a value even where the option itself is unset:
    // section 4.8 declares it as returning a `&ConcurrencySettings`, not an
    // `Option`. What that value contains is not fixed by any document here.
    assert_eq!(settings.concurrency, None);
    let _concurrency: &storage::ConcurrencySettings = settings.concurrency();
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_settings_surface__explicit_switches_are_read_back() -> TestResult {
    let settings = storage::Settings {
        concurrency: None,
        unsafe_use_conditional_update: Some(false),
        unsafe_use_conditional_create: Some(false),
        unsafe_use_metadata: Some(false),
    };
    assert_eq!(settings.unsafe_use_conditional_create(), false);
    assert_eq!(settings.unsafe_use_conditional_update(), false);
    assert_eq!(settings.unsafe_use_metadata(), false);

    let on = storage::Settings {
        concurrency: None,
        unsafe_use_conditional_update: Some(true),
        unsafe_use_conditional_create: Some(true),
        unsafe_use_metadata: Some(true),
    };
    assert_eq!(on.unsafe_use_conditional_create(), true);
    assert_eq!(on.unsafe_use_conditional_update(), true);
    assert_eq!(on.unsafe_use_metadata(), true);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_settings_surface__merge_takes_the_other_value() -> TestResult {
    let mine = storage::Settings {
        concurrency: None,
        unsafe_use_conditional_update: Some(true),
        unsafe_use_conditional_create: Some(true),
        unsafe_use_metadata: Some(true),
    };
    let theirs = storage::Settings {
        concurrency: None,
        unsafe_use_conditional_update: Some(false),
        unsafe_use_conditional_create: Some(false),
        unsafe_use_metadata: Some(false),
    };
    let merged = mine.merge(theirs);
    assert_eq!(merged.unsafe_use_conditional_update(), false);
    assert_eq!(merged.unsafe_use_conditional_create(), false);
    assert_eq!(merged.unsafe_use_metadata(), false);

    // A value the other side does not set is kept.
    let merged = mine.merge(storage::Settings::default());
    assert_eq!(merged.unsafe_use_conditional_update(), true);
    assert_eq!(merged.unsafe_use_metadata(), true);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_settings_surface__a_local_filesystem_backend_turns_them_off() -> TestResult {
    let tmp = tempfile::TempDir::new()?;
    let local_storage = local(tmp.path()).await?;
    let local_settings = local_storage.default_settings();
    assert_eq!(
        local_settings.unsafe_use_conditional_update(),
        false,
        "a local filesystem has no conditional update"
    );
    assert_eq!(
        local_settings.unsafe_use_metadata(),
        false,
        "a local filesystem stores no object metadata"
    );

    let in_memory = mem().await?;
    let memory_settings = in_memory.default_settings();
    assert_eq!(memory_settings.unsafe_use_conditional_update(), true);
    assert_eq!(memory_settings.unsafe_use_metadata(), true);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_settings_surface__a_repository_adopts_its_configured_settings() -> TestResult {
    // Over a LOCAL-FILESYSTEM backend, so the configured value and the
    // backend's own default disagree and the override is observable: a
    // repository over an in-memory backend whose defaults are already on
    // cannot tell an applied configuration from an ignored one.
    let tmp = tempfile::TempDir::new()?;
    let storage = local(tmp.path()).await?;
    assert_eq!(
        storage.default_settings().unsafe_use_conditional_create(),
        true,
        "the backend's own default for the switch the configuration overrides below"
    );

    let wanted = storage::Settings {
        concurrency: None,
        unsafe_use_conditional_update: Some(false),
        unsafe_use_conditional_create: Some(false),
        unsafe_use_metadata: Some(false),
    };
    let repo = Repository::create(
        Some(RepositoryConfig {
            storage: Some(wanted.clone()),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    assert_eq!(repo.config().storage(), Some(&wanted));
    assert_eq!(
        repo.storage_settings().unsafe_use_conditional_create(),
        false,
        "the caller's configured value overrides the backend's default"
    );
    assert_eq!(repo.storage_settings().unsafe_use_metadata(), false);

    // The same backend with no `storage` in its configuration keeps the
    // backend's own defaults, which is the other half of the rule.
    let other_tmp = tempfile::TempDir::new()?;
    let other = local(other_tmp.path()).await?;
    let defaulted = Repository::create(None, Arc::clone(&other), HashMap::new()).await?;
    assert_eq!(defaulted.config().storage(), None);
    assert_eq!(
        defaulted.storage_settings().unsafe_use_conditional_create(),
        true,
        "with nothing configured a repository reports what the backend declares"
    );
    assert_eq!(defaulted.storage_settings().unsafe_use_metadata(), false);

    // And the repository still works with them.
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let id = session.commit("configured", None).await?;
    assert_eq!(repo.lookup_snapshot(&id).await?.message, "configured");
    Ok(())
}

// ===========================================================================
// storage_backend_config - per-backend object-store configuration
//
// These constructors build a lazily initialised client; none of them opens a
// socket, and no case here contacts a network or reads a credential from the
// environment.
// ===========================================================================

// `s3_options` is deliberately NOT a module-level helper: its struct literal
// names `force_path_style`, which is NEW at this release, and a module-level
// item survives every attribution slice (see the note on the `use` block
// above). It is written out inside each of the five cases that need it.

const SECRET: &str = "a-secret-that-must-never-be-printed";

fn static_credentials() -> S3Credentials {
    S3Credentials::Static(S3StaticCredentials {
        access_key_id: "AKIAEXAMPLE".to_string(),
        secret_access_key: SECRET.to_string(),
        session_token: None,
        expires_after: None,
    })
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__s3_options_carry_a_path_style_switch() -> TestResult {
    fn s3_options(endpoint: Option<&str>, region: Option<&str>) -> S3Options {
        S3Options {
            region: region.map(|r| r.to_string()),
            endpoint_url: endpoint.map(|e| e.to_string()),
            anonymous: false,
            allow_http: false,
            force_path_style: false,
        }
    }
    let mut options = s3_options(Some("http://localhost:9000"), Some("us-east-1"));
    assert_eq!(options.force_path_style, false, "virtual-hosted addressing by default");
    options.force_path_style = true;
    assert_eq!(options.force_path_style, true);

    let json = serde_json::to_string(&options)?;
    let back: S3Options = serde_json::from_str(&json)?;
    assert_eq!(back, options, "the flag survives a serialisation round trip");

    // A document written before the flag existed still deserialises.
    let legacy: S3Options = serde_json::from_str(
        r#"{"region":"us-east-1","endpoint_url":null,"anonymous":false,"allow_http":true}"#,
    )?;
    assert_eq!(legacy.force_path_style, false, "the flag defaults when it is absent");
    assert_eq!(legacy.allow_http, true);

    // S3Options renders itself; the wording is not scored, only that the
    // rendering exists and is not empty.
    assert!(!format!("{}", options).is_empty(), "S3Options must implement Display");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__s3_storage_never_renders_its_credentials() -> TestResult {
    fn s3_options(endpoint: Option<&str>, region: Option<&str>) -> S3Options {
        S3Options {
            region: region.map(|r| r.to_string()),
            endpoint_url: endpoint.map(|e| e.to_string()),
            anonymous: false,
            allow_http: false,
            force_path_style: false,
        }
    }
    let storage =
        new_s3_storage(
            s3_options(None, Some("us-east-1")),
            "my-bucket".to_string(),
            Some("my-prefix".to_string()),
            Some(static_credentials()),
        )?;

    let rendered = format!("{}", storage);
    assert!(!rendered.is_empty(), "a Storage renders a description of itself");
    assert!(
        !rendered.contains(SECRET),
        "a secret access key must never be rendered: {rendered}"
    );
    assert!(
        !rendered.contains("AKIAEXAMPLE"),
        "an access key id must never be rendered: {rendered}"
    );
    assert_eq!(storage.can_write(), true);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__the_s3_constructor_refuses_a_tigris_endpoint() -> TestResult {
    fn s3_options(endpoint: Option<&str>, region: Option<&str>) -> S3Options {
        S3Options {
            region: region.map(|r| r.to_string()),
            endpoint_url: endpoint.map(|e| e.to_string()),
            anonymous: false,
            allow_http: false,
            force_path_style: false,
        }
    }
    let result = new_s3_storage(
        s3_options(Some("https://fly.storage.tigris.dev"), Some("auto")),
        "bucket".to_string(),
        None,
        Some(static_credentials()),
    );
    assert!(
        result.is_err(),
        "a Tigris endpoint must go through the Tigris constructor, not the S3 one"
    );

    // The same options without that endpoint are accepted.
    assert!(
        new_s3_storage(
            s3_options(Some("https://s3.example.com"), Some("auto")),
            "bucket".to_string(),
            None,
            Some(static_credentials()),
        )
        .is_ok()
    );

    // The object-store-backed constructor refuses it on the same rule.
    assert!(
        new_s3_object_store_storage(
            s3_options(Some("https://fly.storage.tigris.dev"), Some("auto")),
            "bucket".to_string(),
            None,
            Some(static_credentials()),
        )
        .await
        .is_err(),
        "new_s3_object_store_storage must refuse a Tigris endpoint as well"
    );
    assert!(
        new_s3_object_store_storage(
            s3_options(Some("https://s3.example.com"), Some("auto")),
            "bucket".to_string(),
            None,
            Some(static_credentials()),
        )
        .await
        .is_ok(),
        "and must accept the same options without it, without any I/O"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__tigris_trades_a_region_for_write_access() -> TestResult {
    fn s3_options(endpoint: Option<&str>, region: Option<&str>) -> S3Options {
        S3Options {
            region: region.map(|r| r.to_string()),
            endpoint_url: endpoint.map(|e| e.to_string()),
            anonymous: false,
            allow_http: false,
            force_path_style: false,
        }
    }
    // Strong consistency is the default and requires a region.
    assert!(
        new_tigris_storage(
            s3_options(None, None),
            "bucket".to_string(),
            None,
            Some(static_credentials()),
            false,
        )
        .is_err(),
        "without a region the strongly consistent Tigris backend is refused"
    );

    let strong = new_tigris_storage(
        s3_options(None, Some("iad")),
        "bucket".to_string(),
        None,
        Some(static_credentials()),
        false,
    )?;
    assert_eq!(strong.can_write(), true, "the consistent Tigris backend is writable");

    // The weakly consistent backend needs no region and is read-only.
    let weak = new_tigris_storage(
        s3_options(None, None),
        "bucket".to_string(),
        None,
        Some(static_credentials()),
        true,
    )?;
    assert_eq!(
        weak.can_write(),
        false,
        "an eventually consistent Tigris backend must not accept writes"
    );
    Ok(())
}

/// A backend whose `can_write()` is false is refused by the repository's write
/// entry points *before* any I/O, so this contacts nothing. The weakly
/// consistent Tigris backend is the read-only one this milestone introduces.
#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__a_read_only_backend_cannot_host_a_repository()
-> TestResult {
    fn s3_options(endpoint: Option<&str>, region: Option<&str>) -> S3Options {
        S3Options {
            region: region.map(|r| r.to_string()),
            endpoint_url: endpoint.map(|e| e.to_string()),
            anonymous: false,
            allow_http: false,
            force_path_style: false,
        }
    }
    let weak: Arc<dyn Storage + Send + Sync> = new_tigris_storage(
        s3_options(None, None),
        "bucket".to_string(),
        None,
        Some(static_credentials()),
        true,
    )?;
    assert_eq!(weak.can_write(), false, "the premise: this backend is read-only");

    let err = Repository::create(None, Arc::clone(&weak), HashMap::new())
        .await
        .expect_err("a read-only backend cannot host a new repository");
    assert!(
        matches!(err.kind, RepositoryErrorKind::ReadonlyStorage(_)),
        "expected ReadonlyStorage, got {:?}",
        err.kind
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__r2_needs_an_endpoint_or_an_account() -> TestResult {
    use icechunk::storage::new_r2_storage;
    fn s3_options(endpoint: Option<&str>, region: Option<&str>) -> S3Options {
        S3Options {
            region: region.map(|r| r.to_string()),
            endpoint_url: endpoint.map(|e| e.to_string()),
            anonymous: false,
            allow_http: false,
            force_path_style: false,
        }
    }
    let no_endpoint = new_r2_storage(
        s3_options(None, None),
        Some("bucket".to_string()),
        None,
        None,
        Some(static_credentials()),
    )
    .expect_err("neither an endpoint nor an account id was supplied");
    assert!(
        matches!(no_endpoint.kind, storage::StorageErrorKind::R2ConfigurationError(_)),
        "an R2 misconfiguration is reported as R2ConfigurationError: {:?}",
        no_endpoint.kind
    );

    let no_bucket = new_r2_storage(
        s3_options(None, None),
        None,
        None,
        Some("an-account".to_string()),
        Some(static_credentials()),
    )
    .expect_err("neither a bucket nor a prefix was supplied");
    assert!(
        matches!(no_bucket.kind, storage::StorageErrorKind::R2ConfigurationError(_)),
        "an R2 misconfiguration is reported as R2ConfigurationError: {:?}",
        no_bucket.kind
    );

    let from_account = new_r2_storage(
        s3_options(None, None),
        Some("bucket".to_string()),
        None,
        Some("an-account".to_string()),
        Some(static_credentials()),
    )?;
    assert_eq!(from_account.can_write(), true);

    let from_endpoint = new_r2_storage(
        s3_options(Some("https://example.r2.cloudflarestorage.com"), None),
        Some("bucket".to_string()),
        None,
        None,
        Some(static_credentials()),
    )?;
    assert_eq!(from_endpoint.can_write(), true);
    Ok(())
}

/// The credential enums of section 4.7 reach a repository through
/// `Repository::create`'s third argument, one entry per virtual chunk
/// container. Nothing here contacts a backend: no virtual chunk is read.
#[tokio::test(flavor = "multi_thread")]
async fn storage_backend_config__virtual_chunk_credentials_match_their_container()
-> TestResult {
    use icechunk::config::{AzureCredentials, Credentials, GcsCredentials};

    let s3_store = ObjectStoreConfig::S3(S3Options {
        region: Some("us-east-1".to_string()),
        endpoint_url: None,
        anonymous: false,
        allow_http: false,
        force_path_style: false,
    });
    let mut config = RepositoryConfig::default();
    for (name, store) in [
        ("driver-s3", s3_store),
        ("driver-gcs", ObjectStoreConfig::Gcs(HashMap::new())),
        ("driver-az", ObjectStoreConfig::Azure(HashMap::new())),
    ] {
        config.set_virtual_chunk_container(VirtualChunkContainer {
            name: name.to_string(),
            url_prefix: format!("{name}://bucket/"),
            store,
        });
    }

    let credentials = HashMap::from([
        ("driver-s3".to_string(), Credentials::S3(S3Credentials::Anonymous)),
        ("driver-gcs".to_string(), Credentials::Gcs(GcsCredentials::Anonymous)),
        ("driver-az".to_string(), Credentials::Azure(AzureCredentials::FromEnv)),
    ]);

    let storage = mem().await?;
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), credentials).await?;
    for name in ["driver-s3", "driver-gcs", "driver-az"] {
        let container = repo
            .config()
            .get_virtual_chunk_container(name)
            .unwrap_or_else(|| panic!("the {name} container survives into the repository"));
        assert_eq!(container.name, name);
    }
    // The repository is fully usable afterwards: declaring a cloud container
    // and its credentials opens nothing.
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let id = session.commit("with cloud containers declared", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, id);
    Ok(())
}

// ===========================================================================
// inh01_repository_and_store - milestone 01, the repository format and the
// Zarr store, expressed in this release's API.
// ===========================================================================

const GROUP_META: &[u8] = br#"{"zarr_format":3,"node_type":"group"}"#;
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"]}"#;

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__initialises_and_commits() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert!(Repository::exists(storage.as_ref()).await?);
    assert_eq!(repo.list_branches().await?, BTreeSet::from(["main".to_string()]));

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_group(path("/group"), Bytes::new()).await?;
    let id = session.commit("first commit", None).await?;

    assert_eq!(repo.lookup_branch("main").await?, id);
    assert_eq!(
        messages(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?.len(),
        2,
        "the commit plus the initial snapshot"
    );
    let reader = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert!(reader.get_group(&path("/group")).await.is_ok());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__round_trips_through_the_zarr_store() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let data = Bytes::copy_from_slice(b"hello");
    store.set("array/c/0/0/0", data.clone()).await?;

    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, data);
    assert!(store.exists("array/zarr.json").await?);
    assert!(!store.exists("array/c/1/1/1").await?);

    let mut keys = store.list().await?.try_collect::<Vec<_>>().await?;
    keys.sort();
    assert_eq!(
        keys,
        vec![
            "array/c/0/0/0".to_string(),
            "array/zarr.json".to_string(),
            "zarr.json".to_string(),
        ]
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__branches_and_tags_address_snapshots() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["base"]).await?;

    repo.create_branch("feature", &ids[0]).await?;
    let mut session = repo.writable_session("feature").await?;
    session.add_group(path("/only-on-feature"), Bytes::new()).await?;
    let on_feature = session.commit("feature commit", None).await?;

    assert_eq!(repo.lookup_branch("main").await?, ids[0], "main did not move");
    assert_eq!(repo.lookup_branch("feature").await?, on_feature);

    repo.create_tag("v1", &ids[0]).await?;
    let tagged = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert!(
        tagged.get_group(&path("/only-on-feature")).await.is_err(),
        "the tag points at the base commit"
    );
    assert_eq!(repo.list_tags().await?, BTreeSet::from(["v1".to_string()]));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__persists_on_a_local_filesystem() -> TestResult {
    let tmp = tempfile::TempDir::new()?;
    let array = path("/array");
    let id = {
        let storage = local(tmp.path()).await?;
        let repo = repo_with_native_chunks(&storage).await?;
        let mut session = repo.writable_session("main").await?;
        session.add_group(Path::root(), Bytes::new()).await?;
        session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
        write_chunk(&mut session, &array, 0, b"persisted").await?;
        session.commit("persisted", None).await?
    };

    // A completely fresh handle on the same directory sees the same data.
    let storage = local(tmp.path()).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.lookup_branch("main").await?, id);
    let session = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert_eq!(
        read_chunk(&session, &array, 0).await?.as_deref(),
        Some(&b"persisted"[..])
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__deletes_references_but_not_main() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["base"]).await?;
    repo.create_branch("scratch", &ids[0]).await?;
    repo.create_tag("scratch", &ids[0]).await?;

    repo.delete_branch("scratch").await?;
    assert_eq!(repo.list_branches().await?, BTreeSet::from(["main".to_string()]));
    repo.delete_tag("scratch").await?;
    assert_eq!(repo.list_tags().await?, BTreeSet::new());

    let err = repo.delete_branch("main").await.expect_err("main cannot be deleted");
    assert!(
        matches!(err.kind, RepositoryErrorKind::CannotDeleteMain),
        "expected CannotDeleteMain, got {:?}",
        err.kind
    );
    assert!(repo.list_branches().await?.contains("main"));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__nodes_report_their_type_shape_and_names() -> TestResult {
    use icechunk::format::snapshot::{DimensionName, NodeData, NodeType};

    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    // `Path::new` and `Path::try_from` name the same node.
    let array = Path::new("/array").expect("an absolute, canonical node path");
    assert_eq!(array, path("/array"));
    assert!(Path::new("relative").is_err(), "a node path is absolute");
    assert!(
        Path::new("/a/../b").is_err(),
        "a node path is canonic: a `.` or `..` component is refused"
    );

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_group(path("/group"), Bytes::copy_from_slice(b"g")).await?;
    let shape = ArrayShape::new(vec![(4u64, 2u64)]).expect("a valid array shape");
    session
        .add_array(
            array.clone(),
            shape.clone(),
            Some(vec![DimensionName::from("x")]),
            Bytes::copy_from_slice(b"a"),
        )
        .await?;

    // A group is a group and an array is an array, whichever reader asks.
    let group = session.get_group(&path("/group")).await?;
    assert_eq!(group.node_type(), NodeType::Group);
    assert_eq!(group.node_data, NodeData::Group);
    assert_eq!(group.user_data, Bytes::copy_from_slice(b"g"));

    let node = session.get_array(&array).await?;
    assert_eq!(node.node_type(), NodeType::Array);
    assert_eq!(node.path, array);
    assert_eq!(session.get_node(&array).await?.id, node.id);
    match &node.node_data {
        NodeData::Array { shape: recorded, dimension_names, .. } => {
            assert_eq!(recorded, &shape, "the shape the array was created with");
            match dimension_names.as_deref() {
                Some([DimensionName::Name(name)]) => assert_eq!(name, "x"),
                _ => panic!("the array must report its one named dimension"),
            }
        }
        NodeData::Group => panic!("an array must carry NodeData::Array"),
    }
    // An array is not a group and a group is not an array.
    assert!(session.get_group(&array).await.is_err());
    assert!(session.get_array(&path("/group")).await.is_err());
    assert!(session.get_node(&path("/nowhere")).await.is_err());

    let listed = session.list_nodes().await?.collect::<Result<Vec<_>, _>>()?;
    assert_eq!(listed.len(), 3, "the root group, /group and /array");

    // The shape decides which chunk coordinates exist: (4, 2) is two chunks.
    assert!(shape.valid_chunk_coord(&ChunkIndices(vec![1])));
    assert!(!shape.valid_chunk_coord(&ChunkIndices(vec![2])));

    // The metadata mutators replace what `add_*` recorded; the delete mutators
    // remove the node.
    session.update_group(&path("/group"), Bytes::copy_from_slice(b"g2")).await?;
    assert_eq!(
        session.get_group(&path("/group")).await?.user_data,
        Bytes::copy_from_slice(b"g2")
    );
    session.update_array(&array, shape, None, Bytes::copy_from_slice(b"a2")).await?;
    assert_eq!(session.get_array(&array).await?.user_data, Bytes::copy_from_slice(b"a2"));
    session.delete_array(array.clone()).await?;
    assert!(session.get_array(&array).await.is_err(), "a deleted array is gone");
    session.delete_group(path("/group")).await?;
    assert!(session.get_group(&path("/group")).await.is_err());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__the_store_lists_directories_and_sizes() -> TestResult {
    use icechunk::store::{ListDirItem, StoreErrorKind};

    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;

    // The directory view of the root: the root's own metadata key, and the
    // array below it as a prefix rather than a key.
    let mut keys: Vec<String> = Vec::new();
    let mut prefixes: Vec<String> = Vec::new();
    for item in store.list_dir_items("").await?.try_collect::<Vec<_>>().await? {
        match item {
            ListDirItem::Key(k) => keys.push(k),
            ListDirItem::Prefix(p) => prefixes.push(p.trim_end_matches('/').to_string()),
        }
    }
    assert!(keys.contains(&"zarr.json".to_string()), "the root metadata key: {keys:?}");
    assert!(
        prefixes.contains(&"array".to_string()),
        "the array is a directory below the root: {prefixes:?}"
    );

    // `getsize` answers with the size of what is stored under the key.
    assert_eq!(store.getsize("array/c/0/0/0").await?, 5);

    // A bounded byte range reads a slice of the chunk.
    assert_eq!(
        store.get("array/c/0/0/0", &ByteRange::Bounded(1..3)).await?,
        Bytes::copy_from_slice(b"el")
    );

    // A key nothing was written at is NotFound - not an empty value.
    let err = store
        .get("array/c/1/1/1", &ByteRange::ALL)
        .await
        .expect_err("no chunk was written at that coordinate");
    assert!(
        matches!(err.kind, StoreErrorKind::NotFound(_)),
        "expected NotFound, got {:?}",
        err.kind
    );

    // A store over a read-only session refuses every write.
    let reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let read_only = Store::from_session(Arc::new(RwLock::new(reader))).await;
    assert!(read_only.read_only().await);
    let err = read_only
        .set("zarr.json", Bytes::copy_from_slice(GROUP_META))
        .await
        .expect_err("a read-only store cannot be written");
    assert!(
        matches!(err.kind, StoreErrorKind::ReadOnly),
        "expected ReadOnly, got {:?}",
        err.kind
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__opens_or_creates_the_same_repository() -> TestResult {
    let tmp = tempfile::TempDir::new()?;
    let storage = local(tmp.path()).await?;
    assert!(!Repository::exists(storage.as_ref()).await?);

    // The first call creates; the second opens what the first made.
    let repo = Repository::open_or_create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert!(Repository::exists(storage.as_ref()).await?);
    let ids = commit_chain(&repo, &["base"]).await?;
    let again = Repository::open_or_create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        again.lookup_branch("main").await?,
        ids[0],
        "the second call opened the repository, it did not re-create it"
    );

    // A repository hands back the backend it was built on, and it is the same
    // one the repository is on disk at.
    let reported = again.storage();
    assert!(Repository::exists(reported.as_ref()).await?);
    assert_eq!(
        again.asset_manager().fetch_snapshot(&ids[0]).await?.message(),
        "base",
        "and the asset manager it hands back reads that backend"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh01_repository_and_store__reports_and_merges_its_configuration() -> TestResult {
    // The accessors answer on an all-default configuration ...
    let defaults = RepositoryConfig::default();
    assert_eq!(defaults.inline_chunk_threshold_bytes(), 512);
    assert_eq!(defaults.get_partial_values_concurrency(), 10);
    assert!(defaults.storage().is_none(), "no storage settings were supplied");
    // ... including the three sub-configurations, each of which has a default
    // of its own even where the field is unset.
    let _compression = defaults.compression();
    let _caching = defaults.caching();
    let _manifest = defaults.manifest();

    // `merge` takes the other side's value for every field the other side
    // sets, and keeps its own everywhere else.
    let mine = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(1),
        get_partial_values_concurrency: Some(2),
        ..Default::default()
    };
    let theirs = RepositoryConfig {
        get_partial_values_concurrency: Some(7),
        ..Default::default()
    };
    let merged = mine.merge(theirs);
    assert_eq!(
        merged.get_partial_values_concurrency(),
        7,
        "the other side's value wins where it sets one"
    );
    assert_eq!(
        merged.inline_chunk_threshold_bytes(),
        1,
        "a field the other side leaves unset keeps this side's value"
    );

    // A repository reports the configuration it was created with, and its
    // caching configuration is what an AssetManager is built from.
    let storage = mem().await?;
    let repo =
        Repository::create(Some(mine.clone()), Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.config().inline_chunk_threshold_bytes(), 1);
    let am = AssetManager::new_with_config(
        Arc::clone(&storage),
        storage.default_settings(),
        repo.config().caching(),
        1,
    );
    let tip = repo.lookup_branch("main").await?;
    assert_eq!(am.fetch_snapshot(&tip).await?.id(), tip);
    Ok(())
}

// ===========================================================================
// inh02_sessions_and_conflicts - milestone 02, the session model, conflict
// detection and rebase, and virtual chunk containers.
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__read_only_sessions_cannot_write() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["base"]).await?;

    let mut reader = repo.readonly_session(&VersionInfo::SnapshotId(ids[0].clone())).await?;
    assert!(reader.read_only());
    assert!(reader.branch().is_none());
    let err = reader.commit("nope", None).await.expect_err("a reader cannot commit");
    assert!(matches!(err.kind, SessionErrorKind::ReadOnlySession));

    let writer = repo.writable_session("main").await?;
    assert!(!writer.read_only());
    assert_eq!(writer.branch(), Some("main"));
    assert_eq!(writer.snapshot_id(), &ids[0]);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__a_second_commit_on_the_same_tip_conflicts() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_chain(&repo, &["base"]).await?;

    let mut one = repo.writable_session("main").await?;
    let mut two = repo.writable_session("main").await?;
    one.add_group(path("/one"), Bytes::new()).await?;
    two.add_group(path("/two"), Bytes::new()).await?;

    one.commit("one", None).await?;
    let err = two.commit("two", None).await.expect_err("the loser must not commit");
    assert!(
        matches!(err.kind, SessionErrorKind::Conflict { .. }),
        "expected a branch-update conflict, got {:?}",
        err.kind
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__rebase_lets_the_loser_land() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_chain(&repo, &["base"]).await?;

    let mut one = repo.writable_session("main").await?;
    let mut two = repo.writable_session("main").await?;
    one.add_group(path("/one"), Bytes::new()).await?;
    two.add_group(path("/two"), Bytes::new()).await?;
    one.commit("one", None).await?;
    assert!(two.commit("two", None).await.is_err());

    two.rebase(&BasicConflictSolver::default()).await?;
    two.commit("two", None).await?;

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(session.get_group(&path("/one")).await.is_ok());
    assert!(session.get_group(&path("/two")).await.is_ok());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__an_unsolvable_rebase_is_classified() -> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    session.commit("create", None).await?;

    let mut one = repo.writable_session("main").await?;
    let mut two = repo.writable_session("main").await?;
    write_chunk(&mut one, &array, 0, &[1u8; 32]).await?;
    write_chunk(&mut two, &array, 0, &[2u8; 32]).await?;
    one.commit("one", None).await?;
    assert!(two.commit("two", None).await.is_err());

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::Fail,
        ..BasicConflictSolver::default()
    };
    let err = two.rebase(&solver).await.expect_err("the same chunk was written twice");
    match err.kind {
        SessionErrorKind::RebaseFailed { ref conflicts, .. } => assert!(
            conflicts
                .iter()
                .any(|c| matches!(c, Conflict::ChunkDoubleUpdate { .. })),
            "expected a ChunkDoubleUpdate in {conflicts:?}"
        ),
        ref other => panic!("expected RebaseFailed, got {other:?}"),
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__virtual_chunks_resolve_through_a_container() -> TestResult {
    let tmp = tempfile::TempDir::new()?;
    let chunk_file = tmp.path().join("payload.bin");
    std::fs::write(&chunk_file, b"0123456789")?;

    let mut config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        ..Default::default()
    };
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "file".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    });
    assert!(config.get_virtual_chunk_container("file").is_some());

    let storage = mem().await?;
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    let location =
        VirtualChunkLocation::from_absolute_path(&format!("file://{}", chunk_file.display()))?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: location.clone(),
                offset: 2,
                length: 4,
                checksum: None,
            })),
        )
        .await?;
    session.commit("virtual", None).await?;

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        read_chunk(&session, &array, 0).await?.as_deref(),
        Some(&b"2345"[..]),
        "the virtual reference resolves to a slice of the external file"
    );
    assert!(session.matching_container(&location).is_some());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__session_state_and_status_report_the_changes()
-> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");
    let base = repo.lookup_branch("main").await?;

    let mut session = repo.writable_session("main").await?;
    assert_eq!(session.branch(), Some("main"));
    assert_eq!(session.read_only(), false);
    assert_eq!(session.snapshot_id(), &base, "a new session starts at the branch tip");
    assert!(!session.has_uncommitted_changes());
    assert!(session.changes().is_empty(), "a fresh session has an empty change set");
    assert_eq!(
        session.config().inline_chunk_threshold_bytes(),
        0,
        "the session reports the repository's configuration"
    );

    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[7u8; 64]).await?;
    assert!(session.has_uncommitted_changes());
    assert!(!session.changes().is_empty());

    // `status` describes exactly those changes.
    let diff = session.status().await?;
    assert!(diff.new_groups.contains(&Path::root()), "the root group is new: {diff:?}");
    assert!(diff.new_arrays.contains(&array), "the array is new: {diff:?}");
    assert!(
        diff.updated_chunks
            .get(&array)
            .map(|coords| coords.contains(&ChunkIndices(vec![0])))
            .unwrap_or(false),
        "the chunk write is reported: {diff:?}"
    );
    assert!(diff.deleted_groups.is_empty() && diff.deleted_arrays.is_empty());
    assert!(diff.updated_groups.is_empty() && diff.updated_arrays.is_empty());

    // A chunk at or above the repository's inlining threshold is stored by
    // reference, and the reference records its length.
    match session.get_chunk_ref(&array, &ChunkIndices(vec![0])).await? {
        Some(ChunkPayload::Ref(chunk_ref)) => {
            assert_eq!(chunk_ref.length, 64);
            assert_eq!(chunk_ref.offset, 0);
        }
        _ => panic!("a 64-byte chunk under a zero threshold is its own object"),
    }

    // The same write under the default 512-byte threshold is carried inline.
    let inline_storage = mem().await?;
    let inline_repo =
        Repository::create(None, Arc::clone(&inline_storage), HashMap::new()).await?;
    let mut inline_session = inline_repo.writable_session("main").await?;
    inline_session.add_group(Path::root(), Bytes::new()).await?;
    inline_session
        .add_array(array.clone(), array_shape(4), None, Bytes::new())
        .await?;
    write_chunk(&mut inline_session, &array, 0, b"small").await?;
    match inline_session.get_chunk_ref(&array, &ChunkIndices(vec![0])).await? {
        Some(ChunkPayload::Inline(bytes)) => {
            assert_eq!(bytes, Bytes::copy_from_slice(b"small"))
        }
        _ => panic!("a chunk below the inlining threshold is carried in the manifest"),
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh02_sessions_and_conflicts__default_containers_describe_themselves() -> TestResult {
    use icechunk::virtual_chunks::mk_default_containers;

    let defaults = mk_default_containers();
    assert!(!defaults.is_empty(), "there is at least one default container");
    for (name, container) in &defaults {
        assert_eq!(name, &container.name, "a container is keyed by its own name");
        assert!(
            !container.url_prefix.is_empty(),
            "a container matches virtual locations by prefix, so it must have one"
        );
    }

    // A configuration carries containers of the caller's own and gives them
    // back by name.
    let mut config = RepositoryConfig::default();
    let container = VirtualChunkContainer {
        name: "driver".to_string(),
        url_prefix: "file:///driver/".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    };
    config.set_virtual_chunk_container(container.clone());
    assert_eq!(config.get_virtual_chunk_container("driver"), Some(&container));
    assert!(config.virtual_chunk_containers().any(|c| c.name == "driver"));

    config.clear_virtual_chunk_containers();
    assert!(config.get_virtual_chunk_container("driver").is_none());
    assert_eq!(config.virtual_chunk_containers().count(), 0);
    Ok(())
}

// ===========================================================================
// inh03_expiration_and_gc - milestone 03, expiration, collection and the
// asset manager.
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn inh03_expiration_and_gc__commit_metadata_survives_the_round_trip() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let id = session
        .commit("with metadata", Some(props(&[("ticket", "ICE-1"), ("who", "me")])))
        .await?;

    // Read back through the inherited surface - `ancestry` - rather than this
    // milestone's `lookup_snapshot`, which `ops_lookup_snapshot` scores.
    let info = ancestry(&repo, &VersionInfo::SnapshotId(id))
        .await?
        .into_iter()
        .next()
        .expect("the commit is the newest entry of its own ancestry");
    assert_eq!(info.metadata, props(&[("ticket", "ICE-1"), ("who", "me")]));
    assert_eq!(info.message, "with metadata");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh03_expiration_and_gc__expiration_shortens_the_history() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_chain(&repo, &["one", "two", "three", "four"]).await?;

    let before = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(before.len(), 5);
    // Everything strictly older than the newest commit is expirable.
    let cutoff = before[0].flushed_at;

    expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;

    let after = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(after.len() < before.len(), "the history got shorter: {after:?}");
    assert_eq!(after[0].message, "four", "the tip is unchanged");
    assert!(after.last().expect("a root").is_initial(), "the root is still there");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh03_expiration_and_gc__collection_removes_unreachable_objects() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let (repo, array, first) = stranded_commit(&storage).await?;

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert!(summary.chunks_deleted > 0);
    assert!(summary.snapshots_deleted > 0);

    assert_eq!(repo.lookup_branch("main").await?, first);
    let session = repo.readonly_session(&VersionInfo::SnapshotId(first)).await?;
    assert_eq!(
        read_chunk(&session, &array, 0).await?.as_deref(),
        Some(&[7u8; 64][..]),
        "a reachable chunk is never collected"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh03_expiration_and_gc__a_tag_keeps_its_snapshots_alive() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["one", "two"]).await?;
    repo.create_tag("keep", &ids[1]).await?;
    repo.reset_branch("main", &ids[0]).await?;

    let now = Utc::now();
    let config = GCConfig::clean_all(now, now, None);
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &config,
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 0, "the tag is a GC root");
    // Observed through the inherited asset manager, with caching off, so the
    // answer comes from storage rather than from this milestone's
    // `lookup_snapshot`.
    assert!(asset_manager(&storage).fetch_snapshot(&ids[1]).await.is_ok());

    repo.delete_tag("keep").await?;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &config,
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1, "now nothing points at it");
    assert!(asset_manager(&storage).fetch_snapshot(&ids[1]).await.is_err());
    Ok(())
}

// ===========================================================================
// inh04_format_and_errors - milestone 04, the FlatBuffers-backed format
// objects and the split error types.
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn inh04_format_and_errors__snapshot_objects_decode_field_by_field() -> TestResult {
    use icechunk::format::{IcechunkFormatErrorKind, ManifestId};

    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[1u8; 64]).await?;
    let parent = repo.lookup_branch("main").await?;
    let id = session.commit("decoded", Some(props(&[("k", "v")]))).await?;

    let snapshot = repo.asset_manager().fetch_snapshot(&id).await?;
    assert_eq!(snapshot.id(), id);
    assert_eq!(snapshot.parent_id(), Some(parent));
    assert_eq!(snapshot.message(), "decoded");
    assert_eq!(snapshot.metadata()?, props(&[("k", "v")]));
    assert_eq!(snapshot.manifest_files().count(), 1);
    assert!(snapshot.flushed_at()? <= Utc::now());
    assert!(snapshot.get_node(&array).is_ok());
    assert!(!snapshot.bytes().is_empty(), "the snapshot has a serialised form");

    // Every node is reachable by iteration as well as by path, and `len`
    // counts them.
    let nodes = snapshot.iter().collect::<Result<Vec<_>, _>>()?;
    assert_eq!(nodes.len(), 2, "the root group and the array");
    assert_eq!(snapshot.len(), nodes.len());
    assert!(nodes.iter().any(|n| n.path == array));

    // The manifest the array's chunks live in is described by its id.
    let file = snapshot.manifest_files().next().expect("one manifest file");
    assert_eq!(
        snapshot.manifest_info(&file.id).expect("described by id"),
        file,
        "manifest_info answers with the same record manifest_files yields"
    );
    assert!(snapshot.manifest_info(&ManifestId::random()).is_none());

    // Asking a snapshot for a node it does not carry is a format error, and it
    // carries a matchable kind like every other error in this crate.
    let err = snapshot
        .get_node(&path("/no-such-node"))
        .expect_err("the snapshot has no such node");
    let _kind: &IcechunkFormatErrorKind = &err.kind;
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh04_format_and_errors__the_transaction_log_records_the_change_set() -> TestResult {
    let storage = mem().await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[1u8; 64]).await?;
    let id = session.commit("logged", None).await?;

    let log = repo.asset_manager().fetch_transaction_log(&id).await?;
    assert_eq!(log.new_groups().count(), 1, "the root group was created");
    assert_eq!(log.new_arrays().count(), 1, "one array was created");
    let array_id = repo
        .readonly_session(&VersionInfo::SnapshotId(id.clone()))
        .await?
        .get_array(&array)
        .await?
        .id;
    assert!(log.array_created(&array_id));
    assert!(log.chunks_updated(&array_id));

    // Nothing was deleted or updated in this commit, and the remaining
    // accessors say so.
    assert_eq!(log.deleted_groups().count(), 0);
    assert_eq!(log.deleted_arrays().count(), 0);
    assert_eq!(log.updated_groups().count(), 0);
    assert_eq!(log.updated_arrays().count(), 0);
    let root_id = repo
        .readonly_session(&VersionInfo::SnapshotId(id))
        .await?
        .get_group(&Path::root())
        .await?
        .id;
    assert!(log.group_created(&root_id), "the root group is recorded as created");
    assert!(!log.array_created(&root_id), "and it is not an array");
    assert!(!log.chunks_updated(&root_id), "a group has no chunks");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh04_format_and_errors__failures_carry_a_matchable_kind() -> TestResult {
    let storage = mem().await?;
    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .expect_err("there is no repository at an empty prefix");
    assert!(
        matches!(err.kind, RepositoryErrorKind::RepositoryDoesntExist),
        "expected RepositoryDoesntExist, got {:?}",
        err.kind
    );

    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let err = Repository::create(None, Arc::clone(&storage), HashMap::new())
        .await
        .expect_err("the prefix is no longer clean");
    assert!(
        matches!(err.kind, RepositoryErrorKind::ParentDirectoryNotClean),
        "expected ParentDirectoryNotClean, got {:?}",
        err.kind
    );

    let mut session = repo.writable_session("main").await?;
    let err = session
        .commit("nothing changed", None)
        .await
        .expect_err("an empty session has nothing to commit");
    assert!(
        matches!(err.kind, SessionErrorKind::NoChangesToCommit),
        "expected NoChangesToCommit, got {:?}",
        err.kind
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh04_format_and_errors__sessions_marshal() -> TestResult {
    // A local-filesystem backend, because a marshalled repository carries a
    // description of its backend rather than the backend's contents: an
    // in-memory one revives empty by construction.
    let tmp = tempfile::TempDir::new()?;
    let storage = local(tmp.path()).await?;
    let repo = repo_with_native_chunks(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[8u8; 64]).await?;

    let revived = Session::from_bytes(session.as_bytes()?)?;
    assert!(revived.has_uncommitted_changes());
    assert_eq!(revived.branch(), Some("main"));
    assert_eq!(revived.snapshot_id(), session.snapshot_id());

    // `Repository::as_bytes` / `from_bytes` are NEW at this milestone, so they
    // are scored by `distributed_write_merge`, not here.
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn inh04_format_and_errors__ancestry_streams_newest_first() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let ids = commit_chain(&repo, &["one", "two", "three"]).await?;

    let infos = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(infos.len(), 4, "three commits and the initial snapshot");
    assert_eq!(infos[0].id, ids[2], "the tip comes first");
    assert!(
        infos.last().expect("an initial snapshot").is_initial(),
        "and the ancestry ends at the snapshot with no parent"
    );
    for pair in infos.windows(2) {
        assert!(
            pair[0].flushed_at >= pair[1].flushed_at,
            "ancestry is ordered by decreasing flushed_at: {infos:?}"
        );
        assert_eq!(
            pair[0].parent_id.as_ref(),
            Some(&pair[1].id),
            "and each entry names the next as its parent"
        );
    }

    // `snapshot_ancestry` walks from a snapshot id rather than a reference and
    // yields the same history.
    let by_id: Vec<SnapshotInfo> =
        repo.snapshot_ancestry(&ids[2]).await?.try_collect::<Vec<_>>().await?;
    assert_eq!(
        by_id.iter().map(|i| i.id.clone()).collect::<Vec<_>>(),
        infos.iter().map(|i| i.id.clone()).collect::<Vec<_>>()
    );

    // Starting one commit back yields exactly that commit and its ancestors.
    let from_middle: Vec<SnapshotInfo> =
        repo.snapshot_ancestry(&ids[0]).await?.try_collect::<Vec<_>>().await?;
    assert_eq!(from_middle.len(), 2, "the first commit and the initial snapshot");
    assert_eq!(from_middle[0].id, ids[0]);
    Ok(())
}
