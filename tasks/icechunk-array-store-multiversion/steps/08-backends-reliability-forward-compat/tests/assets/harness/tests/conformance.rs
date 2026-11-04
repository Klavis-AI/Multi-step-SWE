// Verifier-owned conformance driver for milestone 08 of
// earth-mover/icechunk — "backends, reliability and forward compatibility"
// (upstream tag v1.1.21, Rust crate `icechunk` 0.3.24).
//
// Every assertion here traces to /app/TEST_INTERFACE.md, which states the
// public Rust surface this milestone must expose, or to the pinned
// specification corpus under /app/icechunk-spec/ (whose PROFILE.md resolves the
// places the shipped documents and the contract disagree).
//
// Group = the `<prefix>__` part of the test function name. The runner invokes
// one `cargo test` per group and asserts the executed count.
//
// Everything here is offline and deterministic: storage is either
// `new_in_memory_storage()` or `new_local_filesystem_storage()` over a
// `tempfile::TempDir`. No socket is opened, no cloud credential is read, no
// virtual chunk reference is ever dereferenced, and nothing sleeps on a wall
// clock. The retry behaviour this release adds (HTTP 408/429/499, connection
// resets, stalled streams) needs a fault-injecting proxy and is explicitly NOT
// scored — see TEST_INTERFACE.md §"What is not scored". Only the retry
// *configuration surface* is exercised here.

#![allow(non_snake_case)]
#![allow(clippy::bool_assert_comparison)]

use std::{
    collections::{HashMap, HashSet},
    num::{NonZeroU16, NonZeroU64, NonZeroUsize},
    path::{Path as FsPath, PathBuf},
    process::Command,
    sync::Arc,
};

use bytes::Bytes;
use chrono::Utc;
use futures::TryStreamExt;
use tempfile::TempDir;

// Everything named at MODULE level - every `use` item, type alias and helper
// below - survives in every per-group attribution slice, because the slice
// removes other groups' case FUNCTIONS and nothing else. So a module-level name
// that exists only at this release would fail to compile *every* group,
// including the inherited-regression ones, instead of only the groups that
// actually need the new surface. `DEFAULT_MAX_CONCURRENT_REQUESTS`,
// `icechunk::inspect` and `ops::stats::ChunkStorageStats` are new here and are
// therefore imported inside the case bodies that need them; the two helpers
// that touch a changed signature (`AssetManager::new_no_cache`'s fourth
// argument, `S3Options`' two new fields) are handled the same way, below.
use icechunk::{
    ObjectStoreConfig, Repository, RepositoryConfig, Storage,
    asset_manager::AssetManager,
    config::{
        CachingConfig, CompressionConfig, Credentials, GcsCredentials, ManifestConfig,
        ManifestPreloadCondition, ManifestPreloadConfig, ManifestSplittingConfig,
        S3Credentials, S3Options,
    },
    conflicts::{
        Conflict,
        basic_solver::{BasicConflictSolver, VersionSelection},
        detector::ConflictDetector,
    },
    format::{
        ChunkIndices, IcechunkFormatErrorKind, Path, SnapshotId,
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::{ArrayShape, NodeSnapshot, Snapshot, SnapshotProperties},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::{
        gc::{
            Action, ExpireResult, ExpiredRefAction, GCConfig, GCSummary, expire,
            garbage_collect,
        },
        stats::repo_chunks_storage,
    },
    repository::{RepositoryErrorKind, VersionInfo},
    session::{Session, SessionErrorKind},
    storage::{ConcurrencySettings, RetriesSettings, Settings},
    virtual_chunks::{VirtualChunkContainer, VirtualChunkResolver},
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

/// The storage handle every entry point in this crate takes. The constructors
/// hand back `Arc<dyn Storage>`; the repository API asks for the `Send + Sync`
/// spelling, so the widening happens once, here.
type DynStorage = Arc<dyn Storage + Send + Sync>;

// ---------------------------------------------------------------------------
// Shared helpers. None of these contains a `__`, so the runner's per-group
// failure attribution never mistakes one for a scored case.
// ---------------------------------------------------------------------------

/// A node path. Panics on a malformed literal, which is a driver bug, not a
/// candidate failure.
fn p(s: &str) -> Path {
    match Path::try_from(s) {
        Ok(path) => path,
        Err(err) => panic!("driver bug: bad path literal {s:?}: {err:?}"),
    }
}

/// A one-dimensional array shape with `len` chunks of one element each.
fn shape(len: u64) -> ArrayShape {
    match ArrayShape::new(vec![(len, 1)]) {
        Some(s) => s,
        None => panic!("driver bug: bad shape"),
    }
}

/// The S3 object-store options this driver uses wherever a virtual chunk
/// container needs an S3 store.
///
/// Built through `Deserialize` rather than as a struct literal because this is a
/// module-level helper: it survives in every attribution slice, so it has to
/// resolve against the previous milestone's `S3Options` too. Both fields this
/// release adds are named as JSON keys, so a candidate that has them gets them
/// set explicitly. The full seven-field **struct literal** TEST_INTERFACE.md
/// §3.5 promises lives in
/// `g13_retry_and_stream_config__stream_timeout_field_and_backend_defaults`,
/// which is one of this milestone's own groups.
fn s3_options(region: &str) -> S3Options {
    let value = serde_json::json!({
        "region": region,
        "endpoint_url": null,
        "anonymous": false,
        "allow_http": false,
        "force_path_style": false,
        "network_stream_timeout_seconds": null,
        "requester_pays": false
    });
    match serde_json::from_value(value) {
        Ok(options) => options,
        Err(err) => panic!("driver bug: S3Options did not deserialize: {err}"),
    }
}

/// A virtual chunk container over S3. Panics on rejection, so a driver typo
/// never reads as a candidate failure; the rejection paths are asserted
/// explicitly in `i06_virtual_container_rules`.
fn vcc(url_prefix: &str, store: ObjectStoreConfig) -> VirtualChunkContainer {
    match VirtualChunkContainer::new(url_prefix.to_string(), store) {
        Ok(c) => c,
        Err(err) => panic!("driver bug: container {url_prefix:?} rejected: {err}"),
    }
}

/// A fresh in-memory object store.
async fn mem_storage() -> Result<DynStorage, Box<dyn std::error::Error>> {
    let storage: DynStorage = new_in_memory_storage().await?;
    Ok(storage)
}

/// An object store rooted at `path`.
async fn fs_storage(path: &FsPath) -> Result<DynStorage, Box<dyn std::error::Error>> {
    let storage: DynStorage = new_local_filesystem_storage(path).await?;
    Ok(storage)
}

/// A fresh, empty in-memory repository with the default configuration.
async fn mem_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    let storage = mem_storage().await?;
    Ok(Repository::create(None, storage, HashMap::new()).await?)
}

/// A fresh in-memory repository whose inline-chunk threshold is `threshold`
/// bytes, together with the storage it was built on.
async fn mem_repo_with_threshold(
    threshold: u16,
) -> Result<(DynStorage, Repository), Box<dyn std::error::Error>> {
    let storage = mem_storage().await?;
    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(threshold),
        ..Default::default()
    };
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;
    Ok((storage, repo))
}

/// Root group plus a one-dimensional array of `len` chunks at `/array`.
async fn seed_array(session: &mut Session, len: u64) -> TestResult {
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(p("/array"), shape(len), None, Bytes::new()).await?;
    Ok(())
}

// The two statistics helpers are `macro_rules!` rather than functions on
// purpose: a macro definition is not type-checked until it is expanded, and the
// expansions live only inside `g08`/`g09` case bodies, which the attribution
// slice removes when it scores any other group. Written as functions they would
// name `AssetManager::new_no_cache`'s new fourth argument and the new
// `ChunkStorageStats` return type at module level, and so would break every
// group's slice against the previous milestone. Neither name contains a `__`,
// so the runner never mistakes one for a scored case.

/// An uncached asset manager over `storage`, the shape `repo_chunks_storage`
/// expects (TEST_INTERFACE.md §6.1).
macro_rules! stats_asset_manager {
    ($storage:expr, $settings:expr) => {
        Arc::new(AssetManager::new_no_cache(
            Arc::clone($storage),
            ($settings).clone(),
            1,
            icechunk::config::DEFAULT_MAX_CONCURRENT_REQUESTS,
        ))
    };
}

/// `repo_chunks_storage` with the tuning arguments fixed, so a case reads as
/// one call. Expands to the future; the caller awaits it.
macro_rules! chunk_stats {
    ($storage:expr, $settings:expr, $asset_manager:expr) => {
        repo_chunks_storage(
            ($storage).as_ref(),
            $settings,
            Arc::clone($asset_manager),
            NonZeroU16::new(5).expect("driver bug: nonzero"),
            NonZeroUsize::MAX,
            NonZeroU16::new(10).expect("driver bug: nonzero"),
        )
    };
}

/// A virtual chunk reference at `url`, `offset`, `length`. Never dereferenced.
fn virtual_payload(
    url: &str,
    offset: u64,
    length: u64,
) -> Result<ChunkPayload, Box<dyn std::error::Error>> {
    Ok(ChunkPayload::Virtual(VirtualChunkRef {
        location: VirtualChunkLocation::from_absolute_path(url)?,
        offset,
        length,
        checksum: None,
    }))
}

/// The commit messages of a branch's ancestry, newest first.
async fn ancestry_messages(
    repo: &Repository,
    branch: &str,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let all: Vec<_> = repo
        .ancestry(&VersionInfo::BranchTipRef(branch.to_string()))
        .await?
        .try_collect()
        .await?;
    Ok(all.into_iter().map(|s| s.message).collect())
}

/// A local-filesystem repository under a fresh temporary directory.
async fn local_repo(
    dir: &TempDir,
    config: Option<RepositoryConfig>,
) -> Result<(DynStorage, Repository), Box<dyn std::error::Error>> {
    let storage = fs_storage(dir.path()).await?;
    let repo = Repository::create(config, Arc::clone(&storage), HashMap::new()).await?;
    Ok((storage, repo))
}

/// Mark this repository as written by a newer format generation, the only way
/// the guard can be reached from outside the crate: put an object at the key
/// `repo` directly under the repository prefix. Its contents are never read.
fn write_v2_repo_object(dir: &TempDir, body: &[u8]) -> TestResult {
    std::fs::write(dir.path().join("repo"), body)?;
    Ok(())
}

/// Read the raw bytes of a metadata object out of a local-filesystem
/// repository, e.g. `snapshots/<id>`.
fn read_object(dir: &TempDir, key: &str) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
    Ok(std::fs::read(dir.path().join(key))?)
}

/// Write raw bytes back over a metadata object.
fn write_object(dir: &TempDir, key: &str, bytes: &[u8]) -> TestResult {
    std::fs::write(dir.path().join(key), bytes)?;
    Ok(())
}

/// The candidate's `icechunk` command-line binary, as the runner exposes it.
fn cli_binary() -> String {
    match std::env::var("ICECHUNK_TEST_CLI_BIN") {
        Ok(path) if !path.is_empty() => path,
        _ => panic!("driver bug: ICECHUNK_TEST_CLI_BIN is not set"),
    }
}

/// Run the command-line interface with `XDG_CONFIG_HOME` and `HOME` pointed at
/// `home`, so it reads only the configuration this driver wrote.
fn run_cli(home: &FsPath, args: &[&str]) -> (i32, String, String) {
    let binary = cli_binary();
    if !FsPath::new(&binary).exists() {
        panic!(
            "the candidate's `icechunk` binary is not at {binary}: the \
             `--features cli --bin icechunk` build did not produce it. See \
             build-cli.log; this is a build failure, not a behaviour failure."
        );
    }
    let out = Command::new(binary)
        .args(args)
        .env("XDG_CONFIG_HOME", home)
        .env("HOME", home)
        .env("NO_COLOR", "1")
        .output();
    let out = match out {
        Ok(o) => o,
        Err(err) => panic!("could not run the icechunk binary: {err}"),
    };
    (
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    )
}

/// Run the command-line interface with **`XDG_CONFIG_HOME` removed** and `HOME`
/// pointed at `home`, so the configuration can only be found through the
/// documented `$HOME/.config` fallback.
fn run_cli_home_only(home: &FsPath, args: &[&str]) -> (i32, String, String) {
    let binary = cli_binary();
    if !FsPath::new(&binary).exists() {
        panic!(
            "the candidate's `icechunk` binary is not at {binary}: the \
             `--features cli --bin icechunk` build did not produce it. See \
             build-cli.log; this is a build failure, not a behaviour failure."
        );
    }
    let out = Command::new(binary)
        .args(args)
        .env_remove("XDG_CONFIG_HOME")
        .env("HOME", home)
        .env("NO_COLOR", "1")
        .output();
    let out = match out {
        Ok(o) => o,
        Err(err) => panic!("could not run the icechunk binary: {err}"),
    };
    (
        out.status.code().unwrap_or(-1),
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    )
}

/// Write a command-line configuration file naming one local-filesystem
/// repository into `home`, which is then used as `XDG_CONFIG_HOME`. The
/// repository definition is an externally tagged enum, which this YAML dialect
/// spells as a `!Variant` tag.
fn write_cli_config(home: &FsPath, alias: &str, repo_path: &FsPath) -> TestResult {
    write_cli_config_at(&home.join("icechunk"), alias, repo_path)
}

/// The same file, written into an arbitrary configuration directory.
fn write_cli_config_at(dir: &FsPath, alias: &str, repo_path: &FsPath) -> TestResult {
    std::fs::create_dir_all(dir)?;
    let body = format!(
        "repos:\n  {alias}: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
        repo_path.display()
    );
    std::fs::write(dir.join("cli-config.yaml"), body)?;
    Ok(())
}

// ---------------------------------------------------------------------------
// g01_config_merge — TEST_INTERFACE.md §3.1 "RepositoryConfig::merge".
// `self` is the base, the argument is the overlay, and the overlay wins.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g01_config_merge__scalar_fields_take_the_overlay() -> TestResult {
    let base = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(128),
        get_partial_values_concurrency: Some(7),
        ..Default::default()
    };
    let overlay = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(512),
        ..Default::default()
    };

    let merged = base.merge(overlay);
    assert_eq!(merged.inline_chunk_threshold_bytes, Some(512));
    // the overlay left this one unset, so the base's value survives
    assert_eq!(merged.get_partial_values_concurrency, Some(7));
    assert_eq!(merged.inline_chunk_threshold_bytes(), 512);
    assert_eq!(merged.get_partial_values_concurrency(), 7);
    Ok(())
}

#[tokio::test]
async fn g01_config_merge__sub_configs_merge_field_by_field() -> TestResult {
    let base = RepositoryConfig {
        caching: Some(CachingConfig {
            num_chunk_refs: Some(11),
            num_bytes_chunks: Some(1024),
            ..Default::default()
        }),
        compression: Some(CompressionConfig { algorithm: None, level: Some(9) }),
        storage: Some(Settings {
            retries: Some(RetriesSettings {
                max_tries: NonZeroU16::new(3),
                initial_backoff_ms: Some(11),
                max_backoff_ms: None,
            }),
            ..Default::default()
        }),
        manifest: Some(ManifestConfig {
            preload: Some(ManifestPreloadConfig {
                max_total_refs: Some(111),
                preload_if: None,
            }),
            splitting: None,
        }),
        ..Default::default()
    };
    let overlay = RepositoryConfig {
        caching: Some(CachingConfig {
            num_snapshot_nodes: Some(77),
            num_bytes_chunks: Some(4096),
            ..Default::default()
        }),
        compression: Some(CompressionConfig { algorithm: None, level: None }),
        storage: Some(Settings {
            retries: Some(RetriesSettings {
                max_tries: None,
                initial_backoff_ms: Some(22),
                max_backoff_ms: Some(33),
            }),
            concurrency: Some(ConcurrencySettings {
                max_concurrent_requests_for_object: NonZeroU16::new(4),
                ideal_concurrent_request_size: None,
            }),
            ..Default::default()
        }),
        manifest: Some(ManifestConfig {
            preload: Some(ManifestPreloadConfig {
                max_total_refs: None,
                preload_if: Some(ManifestPreloadCondition::True),
            }),
            splitting: Some(ManifestSplittingConfig::with_size(3)),
        }),
        ..Default::default()
    };

    let merged = base.merge(overlay);

    let caching = merged.caching.as_ref().ok_or("caching")?;
    assert_eq!(caching.num_chunk_refs, Some(11)); // only the base set it
    assert_eq!(caching.num_snapshot_nodes, Some(77)); // only the overlay set it
    assert_eq!(caching.num_bytes_chunks, Some(4096)); // both set it: overlay wins

    // an overlay sub-config whose own fields are all unset changes nothing
    assert_eq!(merged.compression.as_ref().ok_or("compression")?.level, Some(9));

    let storage = merged.storage.as_ref().ok_or("storage")?;
    let retries = storage.retries.as_ref().ok_or("retries")?;
    assert_eq!(retries.max_tries, NonZeroU16::new(3));
    assert_eq!(retries.initial_backoff_ms, Some(22));
    assert_eq!(retries.max_backoff_ms, Some(33));
    assert_eq!(
        storage
            .concurrency
            .as_ref()
            .ok_or("concurrency")?
            .max_concurrent_requests_for_object,
        NonZeroU16::new(4)
    );

    // `manifest` is §3.1's last row: both sides are `Some`, so
    // `ManifestConfig::merge` runs, per sub-field `other.or(self)`. `preload` is
    // one such sub-field, so the overlay's *whole* value replaces the base's —
    // the base's `max_total_refs` does not survive underneath it.
    let manifest = merged.manifest.as_ref().ok_or("manifest")?;
    let preload = manifest.preload.as_ref().ok_or("preload")?;
    assert_eq!(preload.max_total_refs, None, "the overlay's whole `preload` wins");
    assert_eq!(preload.preload_if, Some(ManifestPreloadCondition::True));
    assert_eq!(
        manifest.splitting,
        Some(ManifestSplittingConfig::with_size(3)),
        "only the overlay set `splitting`"
    );
    Ok(())
}

#[tokio::test]
async fn g01_config_merge__is_direction_sensitive() -> TestResult {
    let a = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(128),
        caching: Some(CachingConfig { num_chunk_refs: Some(11), ..Default::default() }),
        ..Default::default()
    };
    let b = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(512),
        caching: Some(CachingConfig { num_chunk_refs: Some(22), ..Default::default() }),
        ..Default::default()
    };

    let ab = a.merge(b.clone());
    let ba = b.merge(a);
    assert_eq!(ab.inline_chunk_threshold_bytes, Some(512));
    assert_eq!(ba.inline_chunk_threshold_bytes, Some(128));
    assert_eq!(ab.caching.as_ref().ok_or("caching")?.num_chunk_refs, Some(22));
    assert_eq!(ba.caching.as_ref().ok_or("caching")?.num_chunk_refs, Some(11));
    Ok(())
}

#[tokio::test]
async fn g01_config_merge__absent_stays_absent_and_present_survives() -> TestResult {
    // (None, None) => None, for every field
    let empty = RepositoryConfig::default().merge(RepositoryConfig::default());
    assert_eq!(empty, RepositoryConfig::default());
    assert_eq!(empty.inline_chunk_threshold_bytes, None);
    assert_eq!(empty.max_concurrent_requests, None);
    assert_eq!(empty.caching, None);
    assert_eq!(empty.storage, None);
    assert_eq!(empty.compression, None);
    assert_eq!(empty.manifest, None);
    assert_eq!(empty.virtual_chunk_containers, None);

    let only = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(64),
        caching: Some(CachingConfig {
            num_snapshot_nodes: Some(5),
            ..Default::default()
        }),
        ..Default::default()
    };
    // (None, Some) => Some, and (Some, None) => Some
    assert_eq!(RepositoryConfig::default().merge(only.clone()), only);
    assert_eq!(only.merge(RepositoryConfig::default()), only);
    Ok(())
}

// ---------------------------------------------------------------------------
// g02_config_vcc_replace — TEST_INTERFACE.md §3.2. At this release
// `virtual_chunk_containers` REPLACES on merge; it used to be unioned, which is
// what made `clear_virtual_chunk_containers()` ineffective.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g02_config_vcc_replace__a_cleared_overlay_wipes_the_base() -> TestResult {
    let mut base = RepositoryConfig::default();
    base.set_virtual_chunk_container(vcc(
        "s3://bucket1/",
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    ));
    assert_eq!(base.virtual_chunk_containers().count(), 1);

    let mut cleared = RepositoryConfig::default();
    cleared.clear_virtual_chunk_containers();
    assert_eq!(cleared.virtual_chunk_containers, Some(HashMap::new()));

    let merged = base.merge(cleared);
    assert_eq!(merged.virtual_chunk_containers, Some(HashMap::new()));
    assert_eq!(merged.virtual_chunk_containers().count(), 0);
    assert!(merged.get_virtual_chunk_container("s3://bucket1/").is_none());
    Ok(())
}

#[tokio::test]
async fn g02_config_vcc_replace__a_non_empty_overlay_replaces_rather_than_unions()
-> TestResult {
    let mut base = RepositoryConfig::default();
    base.set_virtual_chunk_container(vcc(
        "s3://bucket1/",
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    ));

    let mut overlay = RepositoryConfig::default();
    overlay.set_virtual_chunk_container(vcc(
        "s3://bucket2/",
        ObjectStoreConfig::S3(s3_options("us-west-2")),
    ));

    let merged = base.merge(overlay);
    let vccs = merged.virtual_chunk_containers.as_ref().ok_or("containers")?;
    assert_eq!(vccs.len(), 1, "the overlay replaces, it does not union");
    assert!(vccs.contains_key("s3://bucket2/"));
    assert!(!vccs.contains_key("s3://bucket1/"));

    let kept = merged.get_virtual_chunk_container("s3://bucket2/").ok_or("bucket2")?;
    assert_eq!(kept.url_prefix(), "s3://bucket2/");
    Ok(())
}

#[tokio::test]
async fn g02_config_vcc_replace__an_unset_overlay_keeps_the_base() -> TestResult {
    let mut base = RepositoryConfig::default();
    base.set_virtual_chunk_container(vcc(
        "s3://bucket1/",
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    ));

    // the overlay never mentions containers at all: the base's set survives
    let merged = base.clone().merge(RepositoryConfig::default());
    let vccs = merged.virtual_chunk_containers.as_ref().ok_or("containers")?;
    assert_eq!(vccs.len(), 1);
    assert!(vccs.contains_key("s3://bucket1/"));

    // and the reverse direction installs the base's set on an empty config
    let merged = RepositoryConfig::default().merge(base);
    assert_eq!(merged.virtual_chunk_containers().count(), 1);
    Ok(())
}

#[tokio::test]
async fn g02_config_vcc_replace__reopening_with_a_cleared_overlay_drops_them()
-> TestResult {
    let storage = mem_storage().await?;
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(vcc(
        "s3://bucket1/",
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    ));
    let creds: HashMap<String, Option<Credentials>> =
        [("s3://bucket1/".to_string(), Some(Credentials::S3(S3Credentials::Anonymous)))]
            .into();

    let repo = Repository::create(Some(config), Arc::clone(&storage), creds).await?;
    assert_eq!(repo.config().virtual_chunk_containers().count(), 1);

    let mut cleared = RepositoryConfig::default();
    cleared.clear_virtual_chunk_containers();
    let reopened = repo.reopen(Some(cleared), Some(HashMap::new()))?;
    assert_eq!(
        reopened.config().virtual_chunk_containers().count(),
        0,
        "clearing the containers must survive the merge performed by reopen"
    );
    assert!(reopened.authorized_virtual_container_prefixes().is_empty());

    // section 3.2 applies the rule "wherever `merge` is used", and `open` with
    // an overlay is the other call site it names: the cleared map replaces the
    // persisted one there too, rather than unioning with it
    let mut cleared_again = RepositoryConfig::default();
    cleared_again.clear_virtual_chunk_containers();
    let opened =
        Repository::open(Some(cleared_again), Arc::clone(&storage), HashMap::new())
            .await?;
    assert_eq!(
        opened.config().virtual_chunk_containers().count(),
        0,
        "clearing the containers must survive the merge performed by open"
    );
    assert!(opened.config().get_virtual_chunk_container("s3://bucket1/").is_none());
    Ok(())
}

// ---------------------------------------------------------------------------
// g03_concurrency_config — TEST_INTERFACE.md §3.3, the new
// `max_concurrent_requests` field, its constant and its accessor.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g03_concurrency_config__the_default_limit_is_256() -> TestResult {
    use icechunk::config::DEFAULT_MAX_CONCURRENT_REQUESTS;

    assert_eq!(DEFAULT_MAX_CONCURRENT_REQUESTS, 256u16);
    assert_eq!(RepositoryConfig::default().max_concurrent_requests, None);
    assert_eq!(
        RepositoryConfig::default().max_concurrent_requests(),
        DEFAULT_MAX_CONCURRENT_REQUESTS
    );

    let explicit =
        RepositoryConfig { max_concurrent_requests: Some(10), ..Default::default() };
    assert_eq!(explicit.max_concurrent_requests(), 10);
    Ok(())
}

#[tokio::test]
async fn g03_concurrency_config__merge_replaces_the_whole_value() -> TestResult {
    let base =
        RepositoryConfig { max_concurrent_requests: Some(8), ..Default::default() };
    let overlay =
        RepositoryConfig { max_concurrent_requests: Some(32), ..Default::default() };

    assert_eq!(base.merge(overlay.clone()).max_concurrent_requests, Some(32));
    assert_eq!(
        base.merge(RepositoryConfig::default()).max_concurrent_requests,
        Some(8),
        "an unset overlay must not erase the base's value"
    );
    assert_eq!(
        RepositoryConfig::default().merge(overlay).max_concurrent_requests,
        Some(32)
    );
    assert_eq!(
        RepositoryConfig::default()
            .merge(RepositoryConfig::default())
            .max_concurrent_requests,
        None
    );
    Ok(())
}

#[tokio::test]
async fn g03_concurrency_config__survives_a_repository_round_trip() -> TestResult {
    let storage = mem_storage().await?;
    let config =
        RepositoryConfig { max_concurrent_requests: Some(1), ..Default::default() };
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.config().max_concurrent_requests(), 1);

    // a repository that allows a single request at a time must still work
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("hello".into())),
        )
        .await?;
    session.commit("with a limit of one", None).await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        reopened.config().max_concurrent_requests(),
        1,
        "the persisted configuration must carry max_concurrent_requests"
    );
    assert_eq!(
        ancestry_messages(&reopened, "main").await?.first().map(String::as_str),
        Some("with a limit of one")
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// g04_caching_defaults — TEST_INTERFACE.md §3.4. The snapshot-node cache
// default was raised to 500,000 at this release; it is the public, observable
// consequence of the raised on-disk size budgets.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g04_caching_defaults__snapshot_nodes_default_to_500_000() -> TestResult {
    assert_eq!(CachingConfig::default().num_snapshot_nodes(), 500_000);
    assert_eq!(RepositoryConfig::default().caching().num_snapshot_nodes(), 500_000);

    let explicit =
        CachingConfig { num_snapshot_nodes: Some(17), ..Default::default() };
    assert_eq!(explicit.num_snapshot_nodes(), 17);
    Ok(())
}

#[tokio::test]
async fn g04_caching_defaults__the_other_cache_budgets() -> TestResult {
    let d = CachingConfig::default();
    assert_eq!(d.num_chunk_refs(), 15_000_000);
    assert_eq!(d.num_transaction_changes(), 0);
    assert_eq!(d.num_bytes_attributes(), 0);
    assert_eq!(d.num_bytes_chunks(), 0);
    assert_eq!(d, CachingConfig { ..Default::default() });
    Ok(())
}

#[tokio::test]
async fn g04_caching_defaults__caching_config_survives_a_repository_round_trip()
-> TestResult {
    let storage = mem_storage().await?;
    let config = RepositoryConfig {
        caching: Some(CachingConfig {
            num_snapshot_nodes: Some(1_234),
            num_chunk_refs: Some(5_678),
            ..Default::default()
        }),
        ..Default::default()
    };
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 2).await?;
    session.commit("seed", None).await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let caching = reopened.config().caching();
    assert_eq!(caching.num_snapshot_nodes(), 1_234);
    assert_eq!(caching.num_chunk_refs(), 5_678);
    // an unset sub-field still falls back to this release's default
    assert_eq!(caching.num_transaction_changes(), 0);
    Ok(())
}

// ---------------------------------------------------------------------------
// g05_v2_repo_open — TEST_INTERFACE.md §4 "Forward compatibility". An object at
// the key `repo` means the repository was written by a newer format
// generation; this library refuses to open it. The object is never read.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g05_v2_repo_open__an_object_at_the_repo_key_refuses_the_open() -> TestResult {
    let dir = TempDir::new()?;
    write_v2_repo_object(&dir, b"fake v2 repo info")?;

    let storage = fs_storage(dir.path()).await?;
    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .err()
        .ok_or("opening a newer-generation repository must fail")?;
    assert!(
        matches!(err.kind, RepositoryErrorKind::RepositoryIsV2),
        "expected RepositoryIsV2, got: {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn g05_v2_repo_open__the_object_contents_are_never_read() -> TestResult {
    // Three unrelated payloads, including an empty one and one that is not
    // valid UTF-8: the guard is existence, not content.
    for body in [b"".as_slice(), b"fake v2 repo info".as_slice(), &[0xff, 0x00, 0x7f]] {
        let dir = TempDir::new()?;
        write_v2_repo_object(&dir, body)?;
        let storage = fs_storage(dir.path()).await?;
        let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
            .await
            .err()
            .ok_or("expected RepositoryIsV2")?;
        assert!(
            matches!(err.kind, RepositoryErrorKind::RepositoryIsV2),
            "payload {body:?} gave: {err:?}"
        );
    }

    // The guard runs before the repository is even looked for: a prefix that
    // holds only `repo` reports RepositoryIsV2, not RepositoryDoesntExist.
    let dir = TempDir::new()?;
    write_v2_repo_object(&dir, b"x")?;
    let storage = fs_storage(dir.path()).await?;
    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .err()
        .ok_or("expected RepositoryIsV2")?;
    assert!(
        matches!(err.kind, RepositoryErrorKind::RepositoryIsV2),
        "expected the guard to precede the existence check, got: {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn g05_v2_repo_open__a_real_repository_upgraded_underneath_is_refused()
-> TestResult {
    let dir = TempDir::new()?;
    let (storage, repo) = local_repo(&dir, None).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("hello".into())),
        )
        .await?;
    session.commit("first", None).await?;
    drop(repo);

    // reopening is fine while the repository is still this generation
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(ancestry_messages(&reopened, "main").await?.len(), 2);
    drop(reopened);

    write_v2_repo_object(&dir, b"upgraded")?;
    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .err()
        .ok_or("expected RepositoryIsV2")?;
    assert!(
        matches!(err.kind, RepositoryErrorKind::RepositoryIsV2),
        "expected RepositoryIsV2, got: {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn g05_v2_repo_open__the_storage_probe_and_a_clean_prefix() -> TestResult {
    let dir = TempDir::new()?;
    let storage = fs_storage(dir.path()).await?;
    assert_eq!(storage.has_v2_repo_info().await?, false);
    assert_eq!(storage.has_object(&storage.default_settings(), "repo").await?, false);

    // a repository can be created in the clean prefix, and the probe still
    // says nothing marks it as a newer generation
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);
    assert_eq!(storage.has_v2_repo_info().await?, false);

    write_v2_repo_object(&dir, b"")?;
    assert_eq!(storage.has_v2_repo_info().await?, true);
    assert_eq!(storage.has_object(&storage.default_settings(), "repo").await?, true);

    // creating into a prefix that already holds objects is refused, so a
    // newer-generation repository cannot be silently overwritten
    let other = TempDir::new()?;
    write_v2_repo_object(&other, b"x")?;
    let other_storage = fs_storage(other.path()).await?;
    let err = Repository::create(None, other_storage, HashMap::new())
        .await
        .err()
        .ok_or("creating in a dirty prefix must fail")?;
    assert!(
        matches!(err.kind, RepositoryErrorKind::ParentDirectoryNotClean),
        "expected ParentDirectoryNotClean, got: {err:?}"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// g06_v2_repo_commit — TEST_INTERFACE.md §4.2. The same guard runs on the
// commit path, after the snapshot has been flushed and before the branch moves.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g06_v2_repo_commit__a_commit_after_the_upgrade_is_refused() -> TestResult {
    let dir = TempDir::new()?;
    let (_storage, repo) = local_repo(&dir, None).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;

    // someone migrates the repository while this session is open
    write_v2_repo_object(&dir, b"v2")?;

    let err = session
        .commit("should fail", None)
        .await
        .err()
        .ok_or("committing into a newer-generation repository must fail")?;
    assert!(
        matches!(
            err.kind,
            SessionErrorKind::RepositoryError(RepositoryErrorKind::RepositoryIsV2)
        ),
        "expected SessionErrorKind::RepositoryError(RepositoryIsV2), got: {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn g06_v2_repo_commit__the_branch_is_not_moved_by_the_refused_commit()
-> TestResult {
    let dir = TempDir::new()?;
    let (_storage, repo) = local_repo(&dir, None).await?;

    let mut first = repo.writable_session("main").await?;
    seed_array(&mut first, 4).await?;
    let committed = first.commit("first", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, committed);

    let mut session = repo.writable_session("main").await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Inline("more".into())),
        )
        .await?;
    write_v2_repo_object(&dir, b"v2")?;

    assert!(session.commit("should fail", None).await.is_err());
    assert_eq!(
        repo.lookup_branch("main").await?,
        committed,
        "the branch must be left where it was"
    );
    assert_eq!(session.snapshot_id(), &committed);
    assert_eq!(
        ancestry_messages(&repo, "main").await?,
        vec!["first".to_string(), Snapshot::INITIAL_COMMIT_MESSAGE.to_string()]
    );
    Ok(())
}

#[tokio::test]
async fn g06_v2_repo_commit__reads_through_an_open_repository_are_unaffected()
-> TestResult {
    let dir = TempDir::new()?;
    let (_storage, repo) = local_repo(&dir, None).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![2]),
            Some(ChunkPayload::Inline("kept".into())),
        )
        .await?;
    let committed = session.commit("first", None).await?;

    write_v2_repo_object(&dir, b"v2")?;

    // the guard is on `open` and on `commit`; an already-open handle still reads
    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(committed)).await?;
    assert!(matches!(
        readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![2])).await?,
        Some(ChunkPayload::Inline(_))
    ));
    assert_eq!(readonly.list_nodes(&Path::root()).await?.count(), 2);
    Ok(())
}

// ---------------------------------------------------------------------------
// g07_chunk_ref_states — TEST_INTERFACE.md §5 "Chunk reference states". The
// four states a chunk reference can be in are exactly the four results of
// `Session::get_chunk_ref`: `None` (uninitialised), `Some(Ref)` (native),
// `Some(Inline)`, `Some(Virtual)`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g07_chunk_ref_states__a_materialised_chunk_reads_back_as_a_native_ref()
-> TestResult {
    let (_storage, repo) = mem_repo_with_threshold(5).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;

    // six bytes is above the five-byte inline threshold
    let payload =
        session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5])).await?;
    assert!(matches!(payload, ChunkPayload::Ref(_)));
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(payload)).await?;

    match session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await? {
        Some(ChunkPayload::Ref(chunk_ref)) => assert_eq!(chunk_ref.length, 6),
        other => panic!("expected a native reference, got {other:?}"),
    }

    // and it is still native after a commit, read through a fresh session
    let snapshot = session.commit("native", None).await?;
    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    assert!(matches!(
        readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Ref(_))
    ));

    // §5: the same reference is reachable by walking the whole node with
    // `Session::all_node_chunks`
    let nodes: Vec<NodeSnapshot> =
        readonly.list_nodes(&Path::root()).await?.collect::<Result<Vec<_>, _>>()?;
    let array_node =
        nodes.iter().find(|n| n.path == p("/array")).ok_or("the array must be listed")?;
    let walked: Vec<_> =
        readonly.all_node_chunks(array_node).await.try_collect().await?;
    assert_eq!(walked.len(), 1, "exactly one reference was written: {walked:?}");
    assert_eq!(walked[0].0, p("/array"));
    assert_eq!(walked[0].1.coord, ChunkIndices(vec![0]));
    assert_eq!(walked[0].1.node, array_node.id);
    assert!(matches!(walked[0].1.payload, ChunkPayload::Ref(_)));
    Ok(())
}

#[tokio::test]
async fn g07_chunk_ref_states__a_small_chunk_reads_back_as_inline() -> TestResult {
    let (_storage, repo) = mem_repo_with_threshold(5).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;

    // one byte is at or below the threshold, so it is stored in the manifest
    let payload = session.get_chunk_writer()(Bytes::copy_from_slice(&[7])).await?;
    assert!(matches!(payload, ChunkPayload::Inline(_)));
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![1]), Some(payload)).await?;

    match session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await? {
        Some(ChunkPayload::Inline(bytes)) => assert_eq!(bytes.as_ref(), &[7u8]),
        other => panic!("expected an inline payload, got {other:?}"),
    }

    let snapshot = session.commit("inline", None).await?;
    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    assert!(matches!(
        readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Inline(_))
    ));
    Ok(())
}

#[tokio::test]
async fn g07_chunk_ref_states__a_virtual_reference_reads_back_as_virtual()
-> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;

    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(virtual_payload("s3://bucket/file.dat", 4096, 100)?),
        )
        .await?;

    match session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await? {
        Some(ChunkPayload::Virtual(r)) => {
            assert_eq!(r.offset, 4096);
            assert_eq!(r.length, 100);
            assert_eq!(r.location.url(), "s3://bucket/file.dat");
            assert!(r.checksum.is_none());
        }
        other => panic!("expected a virtual reference, got {other:?}"),
    }

    let snapshot = session.commit("virtual", None).await?;
    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    assert!(matches!(
        readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Virtual(_))
    ));
    Ok(())
}

#[tokio::test]
async fn g07_chunk_ref_states__unset_and_out_of_bounds_coordinates_are_uninitialised()
-> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 10).await?;

    // in bounds but never written
    assert!(session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![3])).await?.is_none());
    // outside the array's chunk grid
    assert!(
        session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![999])).await?.is_none()
    );

    // written, then cleared again
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![3]),
            Some(ChunkPayload::Inline("x".into())),
        )
        .await?;
    assert!(session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![3])).await?.is_some());
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![3]), None).await?;
    assert!(session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![3])).await?.is_none());

    // asking a group for a chunk reference is an error, not a state
    assert!(session.get_chunk_ref(&Path::root(), &ChunkIndices(vec![0])).await.is_err());

    // and a grid with a zero chunk length has no coordinates at all, so there
    // is no shape to build: `ArrayShape::new` answers `None` (section 12.2)
    assert!(ArrayShape::new(vec![(10u64, 0u64)]).is_none());
    assert!(ArrayShape::new(vec![(10u64, 2u64), (8u64, 0u64)]).is_none());
    assert!(ArrayShape::new(vec![(10u64, 2u64), (8u64, 4u64)]).is_some());
    Ok(())
}

// ---------------------------------------------------------------------------
// g08_chunk_storage_stats — TEST_INTERFACE.md §6 "Storage statistics".
// `repo_chunks_storage` now reports native, virtual and inline bytes
// separately instead of one total.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g08_chunk_storage_stats__nothing_committed_counts_as_nothing() -> TestResult {
    use icechunk::ops::stats::ChunkStorageStats;

    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;
    for idx in 0..5u32 {
        let payload =
            session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5]))
                .await?;
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }

    // uncommitted work is not repository storage
    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    assert_eq!(stats, ChunkStorageStats::default());
    assert_eq!(stats.native_bytes, 0);
    assert_eq!(stats.virtual_bytes, 0);
    assert_eq!(stats.inlined_bytes, 0);
    assert_eq!(stats.total_bytes(), 0);
    Ok(())
}

#[tokio::test]
async fn g08_chunk_storage_stats__native_inline_and_virtual_are_counted_apart()
-> TestResult {
    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;
    for idx in 0..50u32 {
        let payload =
            session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5]))
                .await?;
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }
    for idx in 50..60u32 {
        let payload = session.get_chunk_writer()(Bytes::copy_from_slice(&[0])).await?;
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }
    for idx in 60..70u32 {
        session
            .set_chunk_ref(
                p("/array"),
                ChunkIndices(vec![idx]),
                Some(virtual_payload("s3://foo/bar", idx as u64, 100)?),
            )
            .await?;
    }
    session.commit("first", None).await?;

    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    assert_eq!(stats.native_bytes, 50 * 6);
    assert_eq!(stats.inlined_bytes, 10);
    assert_eq!(stats.virtual_bytes, 10 * 100);
    Ok(())
}

#[tokio::test]
async fn g08_chunk_storage_stats__the_derived_totals() -> TestResult {
    use icechunk::ops::stats::ChunkStorageStats;

    let stats = ChunkStorageStats::new(300, 1000, 10);
    assert_eq!(stats.native_bytes, 300);
    assert_eq!(stats.virtual_bytes, 1000);
    assert_eq!(stats.inlined_bytes, 10);
    assert_eq!(stats.non_virtual_bytes(), 310);
    assert_eq!(stats.total_bytes(), 1310);

    let sum = stats + ChunkStorageStats::new(1, 2, 3);
    assert_eq!(sum, ChunkStorageStats::new(301, 1002, 13));
    assert_eq!(ChunkStorageStats::default(), ChunkStorageStats::new(0, 0, 0));

    // and the same numbers arrive from a real repository
    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 10).await?;
    let payload =
        session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5])).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(payload)).await?;
    let payload = session.get_chunk_writer()(Bytes::copy_from_slice(&[9])).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![1]), Some(payload)).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![2]),
            Some(virtual_payload("s3://foo/bar", 0, 64)?),
        )
        .await?;
    session.commit("mixed", None).await?;

    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    assert_eq!(stats, ChunkStorageStats::new(6, 64, 1));
    assert_eq!(stats.non_virtual_bytes(), 7);
    assert_eq!(stats.total_bytes(), 71);
    Ok(())
}

#[tokio::test]
async fn g08_chunk_storage_stats__inline_bytes_are_recounted_for_every_manifest()
-> TestResult {
    use icechunk::ops::stats::ChunkStorageStats;

    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;
    for idx in 0..50u32 {
        let payload =
            session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5]))
                .await?;
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }
    for idx in 50..60u32 {
        let payload = session.get_chunk_writer()(Bytes::copy_from_slice(&[0])).await?;
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }
    session.commit("first", None).await?;

    let mut session = repo.writable_session("main").await?;
    for idx in 0..10u32 {
        let payload =
            session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5]))
                .await?;
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload))
            .await?;
    }
    session.commit("second", None).await?;

    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    // native chunks are deduplicated across manifests by their chunk id
    assert_eq!(stats.native_bytes, (50 + 10) * 6);
    // inline chunks are physically rewritten into the second manifest too, so
    // both copies count
    assert_eq!(stats.inlined_bytes, (10 + 10) * 1);
    assert_eq!(stats.virtual_bytes, 0);
    Ok(())
}

// ---------------------------------------------------------------------------
// g09_stats_dedup — TEST_INTERFACE.md §6.2, the deduplication keys.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g09_stats_dedup__virtual_chunks_dedup_on_location_offset_and_length()
-> TestResult {
    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;
    for idx in 0..10u32 {
        session
            .set_chunk_ref(
                p("/array"),
                ChunkIndices(vec![idx]),
                Some(virtual_payload("s3://bucket/file.dat", idx as u64 * 100, 100)?),
            )
            .await?;
    }
    // five more references to exactly the same byte range
    for idx in 10..15u32 {
        session
            .set_chunk_ref(
                p("/array"),
                ChunkIndices(vec![idx]),
                Some(virtual_payload("s3://bucket/file.dat", 0, 100)?),
            )
            .await?;
    }
    session.commit("first", None).await?;

    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    assert_eq!(stats.virtual_bytes, 10 * 100, "15 references, 10 distinct ranges");
    assert_eq!(stats.native_bytes, 0);
    assert_eq!(stats.inlined_bytes, 0);
    Ok(())
}

#[tokio::test]
async fn g09_stats_dedup__a_different_url_offset_or_length_is_a_different_chunk()
-> TestResult {
    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;
    // same length, everything else varied one axis at a time
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(virtual_payload("s3://bucket/a.dat", 0, 10)?),
        )
        .await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![1]),
            Some(virtual_payload("s3://bucket/b.dat", 0, 10)?),
        )
        .await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![2]),
            Some(virtual_payload("s3://bucket/a.dat", 5, 10)?),
        )
        .await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![3]),
            Some(virtual_payload("s3://bucket/a.dat", 0, 20)?),
        )
        .await?;
    // an exact repeat of the first one
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![4]),
            Some(virtual_payload("s3://bucket/a.dat", 0, 10)?),
        )
        .await?;
    session.commit("first", None).await?;

    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    assert_eq!(stats.virtual_bytes, 10 + 10 + 10 + 20);
    Ok(())
}

#[tokio::test]
async fn g09_stats_dedup__native_chunks_dedup_on_their_chunk_id() -> TestResult {
    let (storage, repo) = mem_repo_with_threshold(5).await?;
    let settings = storage.default_settings();
    let asset_manager = stats_asset_manager!(&storage, &settings);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 100).await?;

    // one materialised chunk, referenced from four coordinates
    let payload =
        session.get_chunk_writer()(Bytes::copy_from_slice(&[0, 1, 2, 3, 4, 5, 6, 7]))
            .await?;
    for idx in 0..4u32 {
        session
            .set_chunk_ref(p("/array"), ChunkIndices(vec![idx]), Some(payload.clone()))
            .await?;
    }
    // plus a second, independent one
    let other =
        session.get_chunk_writer()(Bytes::copy_from_slice(&[9, 9, 9, 9, 9, 9])).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![4]), Some(other)).await?;
    session.commit("first", None).await?;

    let stats = chunk_stats!(&storage, &settings, &asset_manager).await?;
    assert_eq!(stats.native_bytes, 8 + 6, "the shared chunk is counted once");
    assert_eq!(stats.inlined_bytes, 0);
    assert_eq!(stats.virtual_bytes, 0);
    Ok(())
}

// ---------------------------------------------------------------------------
// g10_rebase_delete_recreate — TEST_INTERFACE.md §7 "Rebase". Deleting a node
// and recreating it at the same path is no longer reported as a conflict with
// the node that is still there; a concurrent *update* of that node still is.
// ---------------------------------------------------------------------------

/// Two writable sessions on one branch, over a repository that already holds
/// the group `/foo/bar` and the array `/foo/bar/some-array`.
async fn conflict_sessions()
-> Result<(Repository, Session, Session), Box<dyn std::error::Error>> {
    let repo = mem_repo().await?;
    let mut setup = repo.writable_session("main").await?;
    setup.add_group(p("/foo"), Bytes::new()).await?;
    setup.add_group(p("/foo/bar"), Bytes::new()).await?;
    setup.add_array(p("/foo/bar/some-array"), shape(5), None, Bytes::new()).await?;
    setup.commit("create directory", None).await?;

    let one = repo.writable_session("main").await?;
    let two = repo.writable_session("main").await?;
    Ok((repo, one, two))
}

#[tokio::test]
async fn g10_rebase_delete_recreate__a_recreated_group_rebases_cleanly() -> TestResult {
    let (repo, mut one, mut two) = conflict_sessions().await?;

    one.add_group(p("/foo/quux"), Bytes::new()).await?;
    one.commit("add sibling group", None).await?;

    two.delete_group(p("/foo/bar")).await?;
    two.add_group(p("/foo/bar"), Bytes::from_static(b"recreated")).await?;
    let err = two
        .commit("delete and re-add group", None)
        .await
        .err()
        .ok_or("the branch moved, so this commit must conflict")?;
    assert!(
        matches!(err.kind, SessionErrorKind::Conflict { .. }),
        "expected a branch conflict, got {err:?}"
    );

    two.rebase(&ConflictDetector).await?;
    let snapshot = two.commit("delete and re-add group", None).await?;

    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    assert!(readonly.get_group(&p("/foo/bar")).await.is_ok());
    assert!(readonly.get_group(&p("/foo/quux")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn g10_rebase_delete_recreate__a_recreated_array_rebases_cleanly() -> TestResult {
    let (repo, mut one, mut two) = conflict_sessions().await?;

    one.add_group(p("/foo/quux"), Bytes::new()).await?;
    one.commit("add sibling group", None).await?;

    two.delete_array(p("/foo/bar/some-array")).await?;
    two.add_array(p("/foo/bar/some-array"), shape(9), None, Bytes::new()).await?;
    assert!(two.commit("delete and re-add array", None).await.is_err());

    two.rebase(&ConflictDetector).await?;
    let snapshot = two.commit("delete and re-add array", None).await?;

    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    assert!(readonly.get_array(&p("/foo/bar/some-array")).await.is_ok());
    // the recreated array has the new shape, so the old coordinates are gone
    assert!(
        readonly
            .get_chunk_ref(&p("/foo/bar/some-array"), &ChunkIndices(vec![8]))
            .await?
            .is_none()
    );
    Ok(())
}

#[tokio::test]
async fn g10_rebase_delete_recreate__still_conflicts_when_the_node_was_updated()
-> TestResult {
    let (_repo, mut one, mut two) = conflict_sessions().await?;

    let path = p("/foo/bar");
    one.update_group(&path, Bytes::from_static(b"updated")).await?;
    one.commit("update group metadata", None).await?;

    let node = two.get_node(&path).await?;
    two.delete_group(path.clone()).await?;
    two.add_group(path.clone(), Bytes::new()).await?;
    assert!(two.commit("delete and re-add group", None).await.is_err());

    let err = two
        .rebase(&ConflictDetector)
        .await
        .err()
        .ok_or("deleting a concurrently updated group must still conflict")?;
    match err.kind {
        SessionErrorKind::RebaseFailed { ref conflicts, .. } => {
            assert!(
                conflicts.contains(&Conflict::DeleteOfUpdatedGroup {
                    path,
                    node_id: node.id
                }),
                "expected DeleteOfUpdatedGroup, got {conflicts:?}"
            );
        }
        other => panic!("expected RebaseFailed, got {other:?}"),
    }

    // section 7 states the same rule for an array, with the array-flavoured
    // conflict carrying the node's identity as it was before the delete
    let (_repo, mut one, mut two) = conflict_sessions().await?;
    let apath = p("/foo/bar/some-array");
    one.update_array(&apath, shape(5), None, Bytes::from_static(b"updated")).await?;
    one.commit("update array metadata", None).await?;

    let anode = two.get_node(&apath).await?;
    two.delete_array(apath.clone()).await?;
    two.add_array(apath.clone(), shape(5), None, Bytes::new()).await?;
    assert!(two.commit("delete and re-add array", None).await.is_err());

    let err = two
        .rebase(&ConflictDetector)
        .await
        .err()
        .ok_or("deleting a concurrently updated array must still conflict")?;
    match err.kind {
        SessionErrorKind::RebaseFailed { ref conflicts, .. } => {
            assert!(
                conflicts.contains(&Conflict::DeleteOfUpdatedArray {
                    path: apath,
                    node_id: anode.id
                }),
                "expected DeleteOfUpdatedArray, got {conflicts:?}"
            );
        }
        other => panic!("expected RebaseFailed, got {other:?}"),
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// g11_session_flush — TEST_INTERFACE.md §8. `Session::flush` writes a snapshot
// without moving any branch; `Repository::reset_branch` takes the expected
// current tip so the move can be made conditional.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g11_session_flush__writes_a_snapshot_without_moving_the_branch() -> TestResult {
    let repo = mem_repo().await?;
    let before = repo.lookup_branch("main").await?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 5).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("hello".into())),
        )
        .await?;
    let properties: SnapshotProperties = [("test".to_string(), 42.into())].into();
    let flushed = session.flush("flush", Some(properties.clone())).await?;

    assert_ne!(flushed, before);
    assert_eq!(
        repo.lookup_branch("main").await?,
        before,
        "flush must not move the branch"
    );

    // the snapshot is nonetheless durable and complete
    let info = repo.lookup_snapshot(&flushed).await?;
    assert_eq!(info.message, "flush");
    assert_eq!(info.metadata, properties);
    let readonly =
        repo.readonly_session(&VersionInfo::SnapshotId(flushed.clone())).await?;
    assert!(matches!(
        readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline(_))
    ));

    // §8: a flushed snapshot is permanent, and `create_tag` and `create_branch`
    // accept it just as `reset_branch` does
    repo.create_tag("flushed", &flushed).await?;
    assert_eq!(repo.lookup_tag("flushed").await?, flushed);
    repo.create_branch("from-flush", &flushed).await?;
    assert_eq!(repo.lookup_branch("from-flush").await?, flushed);
    assert_eq!(repo.lookup_branch("main").await?, before, "still not moved");

    // §8: the repository's `default_commit_metadata` is merged *underneath* the
    // properties the flush is given — the flush's own keys win, the defaults it
    // does not mention survive
    let mut repo = mem_repo().await?;
    repo.set_default_commit_metadata(
        [
            ("author".to_string(), serde_json::json!("the repository")),
            ("test".to_string(), serde_json::json!(0)),
        ]
        .into(),
    );
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 2).await?;
    let flushed = session.flush("with defaults", Some(properties.clone())).await?;
    let info = repo.lookup_snapshot(&flushed).await?;
    assert_eq!(
        info.metadata.get("test"),
        Some(&serde_json::json!(42)),
        "the flush's own property wins over the repository default: {:?}",
        info.metadata
    );
    assert_eq!(
        info.metadata.get("author"),
        Some(&serde_json::json!("the repository")),
        "a default the flush does not mention survives: {:?}",
        info.metadata
    );
    Ok(())
}

#[tokio::test]
async fn g11_session_flush__demotes_the_session_to_read_only() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    assert_eq!(session.branch(), Some("main"));
    seed_array(&mut session, 5).await?;
    assert!(session.has_uncommitted_changes());

    let flushed = session.flush("flush", None).await?;

    assert!(session.branch().is_none(), "the session is no longer on a branch");
    assert!(session.read_only());
    assert!(session.changes().is_empty());
    assert!(!session.has_uncommitted_changes());
    assert_eq!(session.snapshot_id(), &flushed);

    let err = session
        .commit("not allowed", None)
        .await
        .err()
        .ok_or("a flushed session must be read only")?;
    assert!(
        matches!(err.kind, SessionErrorKind::ReadOnlySession),
        "expected ReadOnlySession, got {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn g11_session_flush__reset_branch_is_conditional_on_the_expected_tip()
-> TestResult {
    let repo = mem_repo().await?;
    let initial = repo.lookup_branch("main").await?;
    assert_eq!(initial, Snapshot::INITIAL_SNAPSHOT_ID);

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 5).await?;
    let flushed = session.flush("flush", None).await?;

    // a stale expectation is refused and changes nothing
    assert!(repo.reset_branch("main", &flushed, Some(&flushed)).await.is_err());
    assert_eq!(repo.lookup_branch("main").await?, initial);

    // the correct expectation moves the branch
    repo.reset_branch("main", &flushed, Some(&Snapshot::INITIAL_SNAPSHOT_ID)).await?;
    assert_eq!(repo.lookup_branch("main").await?, flushed);
    assert_eq!(
        ancestry_messages(&repo, "main").await?,
        vec!["flush".to_string(), Snapshot::INITIAL_COMMIT_MESSAGE.to_string()]
    );

    // and passing no expectation resets against whatever the tip is now
    repo.reset_branch("main", &initial, None).await?;
    assert_eq!(repo.lookup_branch("main").await?, initial);
    Ok(())
}

// ---------------------------------------------------------------------------
// g12_anonymous_credentials — TEST_INTERFACE.md §9. Anonymous credentials are
// constructed and read back; nothing here contacts a bucket. Azure has no
// anonymous variant at this release and is not asserted — see
// TEST_INTERFACE.md §"What is not scored".
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g12_anonymous_credentials__s3_and_gcs_expose_an_anonymous_variant()
-> TestResult {
    assert!(matches!(S3Credentials::default(), S3Credentials::FromEnv));
    assert!(matches!(GcsCredentials::default(), GcsCredentials::FromEnv));

    let s3 = Credentials::S3(S3Credentials::Anonymous);
    let gcs = Credentials::Gcs(GcsCredentials::Anonymous);
    assert!(matches!(&s3, Credentials::S3(S3Credentials::Anonymous)));
    assert!(matches!(&gcs, Credentials::Gcs(GcsCredentials::Anonymous)));

    // and both survive serialization, which is how a repository persists them
    let back: Credentials = serde_json::from_str(&serde_json::to_string(&s3)?)?;
    assert!(matches!(back, Credentials::S3(S3Credentials::Anonymous)));
    let back: Credentials = serde_json::from_str(&serde_json::to_string(&gcs)?)?;
    assert!(matches!(back, Credentials::Gcs(GcsCredentials::Anonymous)));
    Ok(())
}

#[tokio::test]
async fn g12_anonymous_credentials__s3_options_carry_an_independent_flag()
-> TestResult {
    let mut options = s3_options("us-east-1");
    options.anonymous = true;
    assert_eq!(options.anonymous, true);

    let store = ObjectStoreConfig::S3(options.clone());
    let back: ObjectStoreConfig = serde_json::from_str(&serde_json::to_string(&store)?)?;
    assert_eq!(back, store);

    // an S3 container accepts anonymous S3 credentials, and no credentials,
    // but not credentials for another backend
    let container = vcc("s3://public-bucket/", store);
    assert!(
        container
            .validate_credentials(Some(&Credentials::S3(S3Credentials::Anonymous)))
            .is_ok()
    );
    assert!(container.validate_credentials(None).is_ok());
    assert!(
        container
            .validate_credentials(Some(&Credentials::Gcs(GcsCredentials::Anonymous)))
            .is_err()
    );
    Ok(())
}

#[tokio::test]
async fn g12_anonymous_credentials__authorise_a_public_virtual_container()
-> TestResult {
    let storage = mem_storage().await?;
    let mut options = s3_options("us-east-1");
    options.anonymous = true;

    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(vcc(
        "s3://earthmover-sample-data/",
        ObjectStoreConfig::S3(options),
    ));
    let creds: HashMap<String, Option<Credentials>> = [(
        "s3://earthmover-sample-data/".to_string(),
        Some(Credentials::S3(S3Credentials::Anonymous)),
    )]
    .into();

    let repo = Repository::create(Some(config), Arc::clone(&storage), creds).await?;
    let prefixes = repo.authorized_virtual_container_prefixes();
    assert_eq!(
        prefixes,
        HashSet::from(["s3://earthmover-sample-data/".to_string()])
    );

    // the container resolves for a location under its prefix and for nothing else
    let session = repo.writable_session("main").await?;
    let inside =
        VirtualChunkLocation::from_absolute_path("s3://earthmover-sample-data/a/b.dat")?;
    let outside = VirtualChunkLocation::from_absolute_path("s3://another-bucket/b.dat")?;
    assert_eq!(
        session.matching_container(&inside).map(|c| c.url_prefix().to_string()),
        Some("s3://earthmover-sample-data/".to_string())
    );
    assert!(session.matching_container(&outside).is_none());

    // A key naming a container whose credentials do not validate makes
    // `create`, `open` and `reopen` fail. The container is an S3 one, so GCS
    // credentials for its prefix are refused wherever they are supplied.
    let bad: HashMap<String, Option<Credentials>> = [(
        "s3://earthmover-sample-data/".to_string(),
        Some(Credentials::Gcs(GcsCredentials::Anonymous)),
    )]
    .into();
    assert!(repo.reopen(None, Some(bad.clone())).is_err(), "reopen must refuse");
    assert!(
        Repository::open(None, Arc::clone(&storage), bad.clone()).await.is_err(),
        "open must refuse"
    );

    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(vcc(
        "s3://earthmover-sample-data/",
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    ));
    let fresh = mem_storage().await?;
    assert!(
        Repository::create(Some(config), fresh, bad).await.is_err(),
        "create must refuse"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// g13_retry_and_stream_config — TEST_INTERFACE.md §10. The retry and
// stalled-stream *configuration* is scored; the retry *behaviour* is not, and
// cannot be, because it needs a fault-injecting proxy.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g13_retry_and_stream_config__retry_defaults_and_accessors() -> TestResult {
    // the one accessor in section 3.1's table that substitutes NO default:
    // an unset `storage` reads back as `None`, never as `Settings::default()`
    assert!(RepositoryConfig::default().storage().is_none());

    let defaults = RetriesSettings::default();
    assert_eq!(defaults.max_tries.is_none(), true);
    assert_eq!(defaults.max_tries().get(), 10);
    assert_eq!(defaults.initial_backoff_ms(), 100);
    assert_eq!(defaults.max_backoff_ms(), 3 * 60 * 1000);

    let explicit = RetriesSettings {
        max_tries: NonZeroU16::new(4),
        initial_backoff_ms: Some(25),
        max_backoff_ms: Some(2_500),
    };
    assert_eq!(explicit.max_tries().get(), 4);
    assert_eq!(explicit.initial_backoff_ms(), 25);
    assert_eq!(explicit.max_backoff_ms(), 2_500);

    // merge is per field, with the argument winning
    let merged = defaults.merge(explicit.clone());
    assert_eq!(merged, explicit);
    let merged = explicit.merge(RetriesSettings::default());
    assert_eq!(merged, explicit);

    // §10: every other accessor substitutes its documented default when the
    // field it reads is unset
    let unset = Settings::default();
    assert_eq!(unset.concurrency().max_concurrent_requests_for_object().get(), 18);
    assert_eq!(
        unset.concurrency().ideal_concurrent_request_size().get(),
        12 * 1024 * 1024
    );
    assert_eq!(unset.unsafe_use_conditional_update(), true);
    assert_eq!(unset.unsafe_use_conditional_create(), true);
    assert_eq!(unset.unsafe_use_metadata(), true);
    assert_eq!(unset.minimum_size_for_multipart_upload(), 100 * 1024 * 1024);
    assert_eq!(unset.metadata_storage_class(), None);
    assert_eq!(unset.chunks_storage_class(), None);

    // the two per-kind storage classes fall back to the general one
    let classed =
        Settings { storage_class: Some("STANDARD".to_string()), ..Default::default() };
    assert_eq!(classed.metadata_storage_class().map(String::as_str), Some("STANDARD"));
    assert_eq!(classed.chunks_storage_class().map(String::as_str), Some("STANDARD"));

    // and an explicit per-kind class wins over the general one
    let split = Settings {
        storage_class: Some("STANDARD".to_string()),
        chunks_storage_class: Some("GLACIER_IR".to_string()),
        ..Default::default()
    };
    assert_eq!(split.metadata_storage_class().map(String::as_str), Some("STANDARD"));
    assert_eq!(split.chunks_storage_class().map(String::as_str), Some("GLACIER_IR"));

    // `ConcurrencySettings::merge` is per field, the argument winning
    let base = ConcurrencySettings {
        max_concurrent_requests_for_object: NonZeroU16::new(5),
        ideal_concurrent_request_size: None,
    };
    let merged = base.merge(ConcurrencySettings {
        max_concurrent_requests_for_object: None,
        ideal_concurrent_request_size: NonZeroU64::new(4096),
    });
    assert_eq!(merged.max_concurrent_requests_for_object, NonZeroU16::new(5));
    assert_eq!(merged.ideal_concurrent_request_size, NonZeroU64::new(4096));
    Ok(())
}

#[tokio::test]
async fn g13_retry_and_stream_config__storage_settings_merge_recursively()
-> TestResult {
    // every one of §10's nine fields is set here, as a full struct literal, so a
    // missing or renamed field is a compile error rather than a silent pass
    let base = Settings {
        concurrency: Some(ConcurrencySettings {
            max_concurrent_requests_for_object: NonZeroU16::new(9),
            ideal_concurrent_request_size: None,
        }),
        retries: Some(RetriesSettings {
            max_tries: NonZeroU16::new(3),
            initial_backoff_ms: Some(11),
            max_backoff_ms: None,
        }),
        unsafe_use_conditional_update: Some(false),
        unsafe_use_conditional_create: Some(false),
        unsafe_use_metadata: Some(false),
        storage_class: Some("STANDARD".to_string()),
        metadata_storage_class: Some("STANDARD_IA".to_string()),
        chunks_storage_class: Some("GLACIER_IR".to_string()),
        minimum_size_for_multipart_upload: Some(1024),
    };
    let overlay = Settings {
        retries: Some(RetriesSettings {
            max_tries: None,
            initial_backoff_ms: Some(22),
            max_backoff_ms: Some(33),
        }),
        unsafe_use_conditional_update: Some(true),
        storage_class: Some("ONEZONE_IA".to_string()),
        ..Default::default()
    };

    let merged = base.merge(overlay);
    let retries = merged.retries.as_ref().ok_or("retries")?;
    assert_eq!(retries.max_tries, NonZeroU16::new(3));
    assert_eq!(retries.initial_backoff_ms, Some(22));
    assert_eq!(retries.max_backoff_ms, Some(33));
    assert_eq!(
        merged.concurrency.as_ref().ok_or("concurrency")?.max_concurrent_requests_for_object,
        NonZeroU16::new(9)
    );
    assert_eq!(merged.minimum_size_for_multipart_upload, Some(1024));

    // the six scalar fields merge the same way: the overlay wins where it is
    // `Some`, the base survives where it is not
    assert_eq!(merged.unsafe_use_conditional_update, Some(true), "both sides set it");
    assert_eq!(merged.unsafe_use_conditional_create, Some(false), "only the base");
    assert_eq!(merged.unsafe_use_metadata, Some(false), "only the base");
    assert_eq!(merged.storage_class.as_deref(), Some("ONEZONE_IA"), "both sides");
    assert_eq!(merged.metadata_storage_class.as_deref(), Some("STANDARD_IA"));
    assert_eq!(merged.chunks_storage_class.as_deref(), Some("GLACIER_IR"));

    // and the whole thing round-trips through serde unchanged
    let back: Settings = serde_json::from_str(&serde_json::to_string(&merged)?)?;
    assert_eq!(back, merged);
    Ok(())
}

#[tokio::test]
async fn g13_retry_and_stream_config__stream_timeout_field_and_backend_defaults()
-> TestResult {
    // the stalled-stream grace period is an S3Options field; 0 means the
    // detector is off, absent means this release's own default applies
    for value in [None, Some(0u32), Some(30u32)] {
        let mut options = s3_options("us-west-2");
        options.network_stream_timeout_seconds = value;
        assert_eq!(options.network_stream_timeout_seconds, value);
        let store = ObjectStoreConfig::S3(options);
        let back: ObjectStoreConfig =
            serde_json::from_str(&serde_json::to_string(&store)?)?;
        assert_eq!(back, store);
    }

    // §3.5: the contract promises a full seven-field struct literal, so every
    // field must be present, public and spelled exactly as documented
    let full = S3Options {
        region: Some("us-east-1".to_string()),
        endpoint_url: Some("https://example.invalid".to_string()),
        anonymous: true,
        allow_http: true,
        force_path_style: true,
        network_stream_timeout_seconds: Some(45),
        requester_pays: true,
    };
    assert_eq!(full.region.as_deref(), Some("us-east-1"));
    assert_eq!(full.endpoint_url.as_deref(), Some("https://example.invalid"));
    assert_eq!(full.anonymous, true);
    assert_eq!(full.allow_http, true);
    assert_eq!(full.force_path_style, true);
    assert_eq!(full.network_stream_timeout_seconds, Some(45));
    assert_eq!(full.requester_pays, true);
    let back: S3Options = serde_json::from_str(&serde_json::to_string(&full)?)?;
    assert_eq!(back, full);

    // §3.5: every one of the eight `ObjectStoreConfig` variants round-trips
    let variants = vec![
        ObjectStoreConfig::InMemory,
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/tmp/icechunk-verifier")),
        ObjectStoreConfig::Http(HashMap::from([(
            "timeout".to_string(),
            "30".to_string(),
        )])),
        ObjectStoreConfig::S3Compatible(s3_options("us-east-1")),
        ObjectStoreConfig::S3(full.clone()),
        ObjectStoreConfig::Gcs(HashMap::from([(
            "project".to_string(),
            "a-project".to_string(),
        )])),
        ObjectStoreConfig::Azure(HashMap::from([(
            "account".to_string(),
            "an-account".to_string(),
        )])),
        ObjectStoreConfig::Tigris(s3_options("iad")),
    ];
    assert_eq!(variants.len(), 8);
    for variant in &variants {
        let back: ObjectStoreConfig =
            serde_json::from_str(&serde_json::to_string(variant)?)?;
        assert_eq!(&back, variant, "every ObjectStoreConfig variant round-trips");
    }

    // a user-supplied storage configuration overrides only the fields it sets;
    // the backend's own defaults survive for the rest
    let storage = mem_storage().await?;
    let backend_defaults = storage.default_settings();
    let config = RepositoryConfig {
        storage: Some(Settings {
            retries: Some(RetriesSettings {
                max_tries: NonZeroU16::new(7),
                initial_backoff_ms: None,
                max_backoff_ms: None,
            }),
            ..Default::default()
        }),
        ..Default::default()
    };
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;

    assert_eq!(repo.storage_settings().retries().max_tries().get(), 7);
    assert_eq!(
        repo.storage_settings().retries().initial_backoff_ms(),
        backend_defaults.retries().initial_backoff_ms(),
        "an unset retry field must keep the backend default"
    );
    assert_eq!(repo.storage_settings().concurrency(), backend_defaults.concurrency());

    // and the user's own settings are what the repository persists
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        reopened.config().storage().ok_or("storage")?.retries().max_tries().get(),
        7
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// g14_inspect_snapshot_json — TEST_INTERFACE.md §11. The new `icechunk::inspect`
// module exposes exactly one item: a JSON rendering of one snapshot.
// ---------------------------------------------------------------------------

/// A repository holding one committed snapshot with a group, an array and one
/// chunk, plus the id of that snapshot.
async fn repo_with_one_commit()
-> Result<(Repository, SnapshotId), Box<dyn std::error::Error>> {
    let (_storage, repo) = mem_repo_with_threshold(512).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 8).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("hello".into())),
        )
        .await?;
    let properties: SnapshotProperties = [("author".to_string(), "verifier".into())].into();
    let snapshot = session.commit("a documented message", Some(properties)).await?;
    Ok((repo, snapshot))
}

#[tokio::test]
async fn g14_inspect_snapshot_json__reports_the_snapshot_identity() -> TestResult {
    use icechunk::inspect::snapshot_json;

    let (repo, snapshot) = repo_with_one_commit().await?;

    let json = snapshot_json(repo.asset_manager(), &snapshot, false).await?;
    let value: serde_json::Value = serde_json::from_str(&json)?;

    assert_eq!(value["id"], serde_json::json!(snapshot.to_string()));
    assert_eq!(value["commit_message"], serde_json::json!("a documented message"));
    assert_eq!(value["metadata"]["author"], serde_json::json!("verifier"));
    assert_eq!(
        value["parent_id"],
        serde_json::json!(Snapshot::INITIAL_SNAPSHOT_ID.to_string())
    );
    assert!(value["flushed_at"].is_string(), "flushed_at must be a timestamp string");

    // the root snapshot has no parent, and the key is then absent
    let json =
        snapshot_json(repo.asset_manager(), &Snapshot::INITIAL_SNAPSHOT_ID, false)
            .await?;
    let root: serde_json::Value = serde_json::from_str(&json)?;
    assert_eq!(root["id"], serde_json::json!(Snapshot::INITIAL_SNAPSHOT_ID.to_string()));
    assert!(root.get("parent_id").is_none(), "an absent parent is omitted: {root}");
    Ok(())
}

#[tokio::test]
async fn g14_inspect_snapshot_json__lists_every_node_with_its_type() -> TestResult {
    use icechunk::inspect::snapshot_json;

    let (repo, snapshot) = repo_with_one_commit().await?;
    let json = snapshot_json(repo.asset_manager(), &snapshot, false).await?;
    let value: serde_json::Value = serde_json::from_str(&json)?;

    let nodes = value["nodes"].as_array().ok_or("nodes must be an array")?;
    assert_eq!(nodes.len(), 2, "the root group and the array: {value}");

    let mut seen: Vec<(String, String)> = nodes
        .iter()
        .map(|n| {
            (
                n["path"].as_str().unwrap_or_default().to_string(),
                n["node_type"].as_str().unwrap_or_default().to_string(),
            )
        })
        .collect();
    seen.sort();
    assert_eq!(
        seen,
        vec![
            ("/".to_string(), "group".to_string()),
            ("/array".to_string(), "array".to_string()),
        ]
    );

    for node in nodes {
        assert!(node["id"].is_string());
        if node["node_type"] == serde_json::json!("array") {
            let refs =
                node["manifest_refs"].as_array().ok_or("an array carries manifest_refs")?;
            assert_eq!(refs.len(), 1);
            assert!(refs[0]["id"].is_string());
            assert!(refs[0]["extents"].is_array());
        } else {
            assert!(
                node.get("manifest_refs").is_none(),
                "a group must not carry manifest_refs: {node}"
            );
        }
    }

    // §11: the same node list is reachable from the snapshot object itself
    let snap = repo.asset_manager().fetch_snapshot(&snapshot).await?;
    let root_index = snap.get_node_index(&Path::root())?;
    let array_index = snap.get_node_index(&p("/array"))?;
    assert_ne!(root_index, array_index, "two nodes have two positions");
    assert!(root_index < nodes.len() && array_index < nodes.len());
    assert!(
        snap.get_node_index(&p("/missing")).is_err(),
        "a path the snapshot does not hold has no index"
    );
    Ok(())
}

#[tokio::test]
async fn g14_inspect_snapshot_json__lists_manifests_and_pretty_prints_the_same_value()
-> TestResult {
    use icechunk::inspect::snapshot_json;

    let (repo, snapshot) = repo_with_one_commit().await?;

    let compact = snapshot_json(repo.asset_manager(), &snapshot, false).await?;
    let pretty = snapshot_json(repo.asset_manager(), &snapshot, true).await?;
    assert!(!compact.contains('\n'), "the compact rendering is one line");
    assert!(pretty.contains('\n'), "the pretty rendering is indented");
    assert_eq!(
        serde_json::from_str::<serde_json::Value>(&compact)?,
        serde_json::from_str::<serde_json::Value>(&pretty)?
    );

    let value: serde_json::Value = serde_json::from_str(&compact)?;
    let manifests = value["manifests"].as_array().ok_or("manifests must be an array")?;
    assert_eq!(manifests.len(), 1);
    assert!(manifests[0]["id"].is_string());
    assert!(manifests[0]["size_bytes"].as_u64().unwrap_or(0) > 0);
    assert_eq!(manifests[0]["num_chunk_refs"], serde_json::json!(1));
    Ok(())
}

// ---------------------------------------------------------------------------
// g15_storage_has_object — TEST_INTERFACE.md §4.3. The forward-compatibility
// guard rests on one new storage primitive: an existence probe for a single
// key, and the named probe for the `repo` key built on top of it.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g15_storage_has_object__reports_presence_of_an_arbitrary_key() -> TestResult {
    let dir = TempDir::new()?;
    let storage = fs_storage(dir.path()).await?;
    let settings = storage.default_settings();

    assert_eq!(storage.has_object(&settings, "marker").await?, false);
    std::fs::write(dir.path().join("marker"), b"present")?;
    assert_eq!(storage.has_object(&settings, "marker").await?, true);
    // a missing key is `false`, not an error
    assert_eq!(storage.has_object(&settings, "no-such-key").await?, false);
    Ok(())
}

#[tokio::test]
async fn g15_storage_has_object__has_v2_repo_info_tracks_the_repo_key() -> TestResult {
    let dir = TempDir::new()?;
    let storage = fs_storage(dir.path()).await?;

    assert_eq!(storage.has_v2_repo_info().await?, false);
    write_v2_repo_object(&dir, b"")?;
    assert_eq!(
        storage.has_v2_repo_info().await?,
        true,
        "a zero-length object at `repo` still marks the repository"
    );
    assert_eq!(
        storage.has_object(&storage.default_settings(), "repo").await?,
        true,
        "has_v2_repo_info is the `repo` key probe"
    );
    Ok(())
}

#[tokio::test]
async fn g15_storage_has_object__answers_over_in_memory_storage_too() -> TestResult {
    let storage = mem_storage().await?;
    let settings = storage.default_settings();
    assert_eq!(storage.has_v2_repo_info().await?, false);

    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    let snapshot = session.commit("first", None).await?;

    assert_eq!(
        storage.has_object(&settings, &format!("snapshots/{snapshot}")).await?,
        true
    );
    assert_eq!(
        storage.has_object(&settings, "snapshots/not-a-snapshot").await?,
        false
    );
    assert_eq!(storage.has_v2_repo_info().await?, false);
    Ok(())
}

// ===========================================================================
// Inherited regressions. One end-to-end path through each milestone this step
// carries forward; the contract each one restates is in TEST_INTERFACE.md
// §12 "Inherited contract".
// ===========================================================================

// ---------------------------------------------------------------------------
// i01_repo_refs_ancestry — milestones 01 to 03: repository creation, branches,
// tags with permanent retirement, ancestry with commit metadata, time travel.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i01_repo_refs_ancestry__creation_and_the_default_branch() -> TestResult {
    let storage = mem_storage().await?;
    assert_eq!(Repository::exists(storage.as_ref()).await?, false);

    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .err()
        .ok_or("opening a repository that was never created must fail")?;
    assert!(
        matches!(err.kind, RepositoryErrorKind::RepositoryDoesntExist),
        "expected RepositoryDoesntExist, got {err:?}"
    );

    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(Repository::exists(storage.as_ref()).await?, true);
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), ["main"]);
    assert_eq!(repo.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);
    assert_eq!(
        ancestry_messages(&repo, "main").await?,
        vec![Snapshot::INITIAL_COMMIT_MESSAGE.to_string()]
    );

    // creating twice over the same prefix is refused
    assert!(Repository::create(None, Arc::clone(&storage), HashMap::new()).await.is_err());
    Ok(())
}

#[tokio::test]
async fn i01_repo_refs_ancestry__branches_and_retired_tags() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    let first = session.commit("first", None).await?;

    repo.create_branch("dev", &first).await?;
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        ["dev", "main"]
    );
    assert_eq!(repo.lookup_branch("dev").await?, first);
    repo.delete_branch("dev").await?;
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), ["main"]);
    assert!(repo.delete_branch("main").await.is_err(), "main cannot be deleted");

    repo.create_tag("v1", &first).await?;
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), ["v1"]);
    assert_eq!(repo.lookup_tag("v1").await?, first);
    assert!(repo.create_tag("v1", &first).await.is_err(), "a tag is immutable");

    repo.delete_tag("v1").await?;
    assert!(repo.lookup_tag("v1").await.is_err());
    assert!(repo.list_tags().await?.is_empty());
    assert!(
        repo.create_tag("v1", &first).await.is_err(),
        "a deleted tag name stays retired"
    );
    Ok(())
}

#[tokio::test]
async fn i01_repo_refs_ancestry__ancestry_carries_messages_and_metadata() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    let properties: SnapshotProperties = [("run".to_string(), 1.into())].into();
    let first = session.commit("first", Some(properties.clone())).await?;

    let mut session = repo.writable_session("main").await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("a".into())),
        )
        .await?;
    let second = session.commit("second", None).await?;

    assert_eq!(
        ancestry_messages(&repo, "main").await?,
        vec![
            "second".to_string(),
            "first".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string(),
        ]
    );

    let info = repo.lookup_snapshot(&first).await?;
    assert_eq!(info.id, first);
    assert_eq!(info.metadata, properties);
    assert_eq!(info.parent_id, Some(Snapshot::INITIAL_SNAPSHOT_ID));
    assert_eq!(repo.lookup_snapshot(&second).await?.parent_id, Some(first));
    Ok(())
}

#[tokio::test]
async fn i01_repo_refs_ancestry__time_travel_by_snapshot_tag_and_branch() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    let first = session.commit("first", None).await?;
    repo.create_tag("before", &first).await?;

    let mut session = repo.writable_session("main").await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("later".into())),
        )
        .await?;
    session.commit("second", None).await?;

    let at_tag = repo.readonly_session(&VersionInfo::TagRef("before".to_string())).await?;
    assert!(at_tag.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?.is_none());

    let at_snapshot = repo.readonly_session(&VersionInfo::SnapshotId(first)).await?;
    assert!(
        at_snapshot.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?.is_none()
    );

    let at_tip =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(matches!(
        at_tip.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline(_))
    ));
    assert!(at_tip.read_only());
    Ok(())
}

// ---------------------------------------------------------------------------
// i02_sessions_conflicts — milestone 02: the session is the unit of work, two
// sessions on one branch can conflict, and a rebase resolves what it can.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i02_sessions_conflicts__two_sessions_on_one_branch_conflict() -> TestResult {
    let (_repo, mut one, mut two) = conflict_sessions().await?;

    one.add_group(p("/foo/quux"), Bytes::new()).await?;
    one.commit("first in", None).await?;

    two.add_group(p("/foo/other"), Bytes::new()).await?;
    let err = two
        .commit("second in", None)
        .await
        .err()
        .ok_or("the second session must see a moved branch")?;
    match err.kind {
        SessionErrorKind::Conflict { expected_parent, actual_parent } => {
            assert_ne!(expected_parent, actual_parent);
        }
        other => panic!("expected a branch conflict, got {other:?}"),
    }

    // rebasing over an unrelated change succeeds and the commit then lands
    two.rebase(&ConflictDetector).await?;
    two.commit("second in", None).await?;
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__the_basic_solver_picks_a_chunk_version() -> TestResult {
    let repo = mem_repo().await?;
    let mut setup = repo.writable_session("main").await?;
    seed_array(&mut setup, 8).await?;
    setup.commit("create array", None).await?;

    let mut one = repo.writable_session("main").await?;
    let mut two = repo.writable_session("main").await?;

    one.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("theirs".into())),
    )
    .await?;
    one.commit("theirs", None).await?;

    two.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    assert!(two.commit("ours", None).await.is_err());

    // the bare detector refuses a double update of the same chunk
    let err = two
        .rebase(&ConflictDetector)
        .await
        .err()
        .ok_or("a chunk double update must be reported")?;
    assert!(matches!(err.kind, SessionErrorKind::RebaseFailed { .. }), "got {err:?}");

    // the basic solver keeps our version when asked to
    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseOurs,
        ..Default::default()
    };
    two.rebase(&solver).await?;
    let snapshot = two.commit("ours", None).await?;

    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    match readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await? {
        Some(ChunkPayload::Inline(bytes)) => assert_eq!(bytes.as_ref(), b"ours"),
        other => panic!("expected our inline chunk, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__a_new_node_at_an_occupied_path_is_a_conflict()
-> TestResult {
    let (_repo, mut one, mut two) = conflict_sessions().await?;
    let path = p("/foo/bar/conflict");

    one.add_group(path.clone(), Bytes::new()).await?;
    one.commit("create group", None).await?;

    two.add_array(path.clone(), shape(5), None, Bytes::new()).await?;
    assert!(two.commit("create array", None).await.is_err());

    let err = two.rebase(&ConflictDetector).await.err().ok_or("expected a conflict")?;
    match err.kind {
        SessionErrorKind::RebaseFailed { ref conflicts, .. } => assert!(
            conflicts.contains(&Conflict::NewNodeConflictsWithExistingNode(path)),
            "got {conflicts:?}"
        ),
        other => panic!("expected RebaseFailed, got {other:?}"),
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// i03_file_header_specversion — milestones 03 and 04: every metadata object
// starts with the documented 39-byte header, and the spec-version byte at
// offset 36 is the per-file forward-compatibility guard.
// ---------------------------------------------------------------------------

/// A local-filesystem repository with one commit, returning the directory, the
/// snapshot id and the id of its single manifest.
async fn local_repo_with_manifest(
    dir: &TempDir,
) -> Result<(SnapshotId, String), Box<dyn std::error::Error>> {
    let (_storage, repo) = local_repo(
        dir,
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..Default::default()
        }),
    )
    .await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 8).await?;
    let payload = session.get_chunk_writer()(Bytes::from_static(b"payload")).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(payload)).await?;
    let snapshot = session.commit("first", None).await?;
    let info = repo.lookup_snapshot(&snapshot).await?;
    let manifest = info.manifests.first().ok_or("the commit must write a manifest")?;
    Ok((snapshot, manifest.id.to_string()))
}

#[tokio::test]
async fn i03_file_header_specversion__metadata_objects_carry_the_documented_header()
-> TestResult {
    use icechunk::format::format_constants::{
        CompressionAlgorithmBin, FileTypeBin, ICECHUNK_FORMAT_MAGIC_BYTES,
        SpecVersionBin,
    };

    let dir = TempDir::new()?;
    let (snapshot, manifest) = local_repo_with_manifest(&dir).await?;

    for (key, expected) in [
        (format!("snapshots/{snapshot}"), FileTypeBin::Snapshot),
        (format!("manifests/{manifest}"), FileTypeBin::Manifest),
        (format!("transactions/{snapshot}"), FileTypeBin::TransactionLog),
    ] {
        let bytes = read_object(&dir, &key)?;
        assert!(bytes.len() > 39, "{key} is too short to hold a header");
        assert_eq!(&bytes[0..12], ICECHUNK_FORMAT_MAGIC_BYTES, "magic bytes of {key}");
        assert_eq!(
            bytes[36],
            SpecVersionBin::current() as u8,
            "spec version byte of {key}"
        );
        assert_eq!(bytes[36], 1u8);
        assert_eq!(bytes[37], expected as u8, "file type byte of {key}");
        assert!(
            bytes[38] == CompressionAlgorithmBin::None as u8
                || bytes[38] == CompressionAlgorithmBin::Zstd as u8,
            "compression byte of {key}"
        );
    }

    // bytes 12-35 name the writing client and are IGNORED on read, so an
    // object stamped by another implementation still reads back
    let key = format!("snapshots/{snapshot}");
    let mut bytes = read_object(&dir, &key)?;
    bytes[12..36].copy_from_slice(b"another-implementation  ");
    write_object(&dir, &key, &bytes)?;
    let storage = fs_storage(dir.path()).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let _ = repo.lookup_snapshot(&snapshot).await?;
    Ok(())
}

#[tokio::test]
async fn i03_file_header_specversion__a_newer_spec_version_byte_is_refused()
-> TestResult {
    let dir = TempDir::new()?;
    let (snapshot, _manifest) = local_repo_with_manifest(&dir).await?;

    let key = format!("snapshots/{snapshot}");
    let mut bytes = read_object(&dir, &key)?;
    bytes[36] = 2;
    write_object(&dir, &key, &bytes)?;

    let storage = fs_storage(dir.path()).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let err = repo
        .lookup_snapshot(&snapshot)
        .await
        .err()
        .ok_or("a newer spec version must be refused")?;
    assert!(
        matches!(
            err.kind,
            RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidSpecVersion
            )
        ),
        "expected InvalidSpecVersion, got {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn i03_file_header_specversion__corrupt_magic_bytes_are_refused() -> TestResult {
    let dir = TempDir::new()?;
    let (snapshot, _manifest) = local_repo_with_manifest(&dir).await?;

    let key = format!("snapshots/{snapshot}");
    let mut bytes = read_object(&dir, &key)?;
    bytes[0] ^= 0xff;
    write_object(&dir, &key, &bytes)?;

    let storage = fs_storage(dir.path()).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let err = repo
        .lookup_snapshot(&snapshot)
        .await
        .err()
        .ok_or("a corrupt header must be refused")?;
    assert!(
        matches!(
            err.kind,
            RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidMagicNumbers
            )
        ),
        "expected InvalidMagicNumbers, got {err:?}"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// i04_cli_snapshot_list — milestone 05: the `icechunk` command-line interface.
// Driven as a process, over argv and exit status, against the configuration
// file this driver writes into a private XDG_CONFIG_HOME.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i04_cli_snapshot_list__help_lists_the_documented_subcommands() -> TestResult {
    let home = TempDir::new()?;
    let (code, stdout, _stderr) = run_cli(home.path(), &["--help"]);
    assert_eq!(code, 0, "`icechunk --help` must succeed");
    for expected in ["repo", "snapshot", "config"] {
        assert!(stdout.contains(expected), "`--help` must mention {expected}: {stdout}");
    }

    let (code, _out, _err) = run_cli(home.path(), &["definitely-not-a-subcommand"]);
    assert_ne!(code, 0, "an unknown subcommand must not succeed");

    let (code, _out, _err) = run_cli(home.path(), &["repo", "create"]);
    assert_ne!(code, 0, "a missing required argument must not succeed");

    // `config list` prints the current configuration, so with one repository
    // configured it succeeds and names that alias
    let repo_dir = TempDir::new()?;
    write_cli_config(home.path(), "demo", repo_dir.path())?;
    let (code, stdout, stderr) = run_cli(home.path(), &["config", "list"]);
    assert_eq!(code, 0, "`icechunk config list` must succeed: {stderr}");
    assert!(stdout.contains("demo"), "`config list` must show the alias: {stdout}");

    // §12.4: with `XDG_CONFIG_HOME` unset the file is read from
    // `$HOME/.config/icechunk/cli-config.yaml` instead
    let fallback_home = TempDir::new()?;
    let (code, stdout, _err) = run_cli_home_only(fallback_home.path(), &["config", "list"]);
    assert!(
        !stdout.contains("fallback-demo"),
        "nothing is configured under the fallback path yet: {stdout} (exit {code})"
    );
    write_cli_config_at(
        &fallback_home.path().join(".config").join("icechunk"),
        "fallback-demo",
        repo_dir.path(),
    )?;
    let (code, stdout, stderr) =
        run_cli_home_only(fallback_home.path(), &["config", "list"]);
    assert_eq!(code, 0, "`config list` through the $HOME/.config fallback: {stderr}");
    assert!(
        stdout.contains("fallback-demo"),
        "the $HOME/.config fallback must be read: {stdout}"
    );
    Ok(())
}

#[tokio::test]
async fn i04_cli_snapshot_list__repo_create_initialises_a_local_repository()
-> TestResult {
    let home = TempDir::new()?;
    let repo_dir = TempDir::new()?;
    write_cli_config(home.path(), "demo", repo_dir.path())?;

    let (code, _out, stderr) = run_cli(home.path(), &["repo", "create", "demo"]);
    assert_eq!(code, 0, "`repo create demo` failed: {stderr}");
    assert!(repo_dir.path().join("refs").is_dir());
    assert!(repo_dir.path().join("snapshots").is_dir());

    // and the library agrees the repository is there
    let storage = fs_storage(repo_dir.path()).await?;
    assert_eq!(Repository::exists(storage.as_ref()).await?, true);

    // an alias that is not in the configuration is an error, not a new repository
    let (code, _out, _err) = run_cli(home.path(), &["repo", "create", "missing"]);
    assert_ne!(code, 0, "an unknown alias must fail");

    // A configuration file that cannot be parsed is treated as empty, so every
    // alias resolves to "not found" and the invocation fails. A separate home
    // and a separate repository directory, so that the only difference from the
    // successful run above is the unreadable file.
    let broken_home = TempDir::new()?;
    let broken_repo = TempDir::new()?;
    let dir = broken_home.path().join("icechunk");
    std::fs::create_dir_all(&dir)?;
    // an unterminated YAML flow sequence: the file exists and is readable, and
    // no YAML parser accepts it
    std::fs::write(dir.join("cli-config.yaml"), b"repos: [demo: !LocalFileSystem\n")?;
    let (code, _out, _err) = run_cli(broken_home.path(), &["repo", "create", "demo"]);
    assert_ne!(code, 0, "an unparsable configuration file must not resolve an alias");
    assert!(
        !broken_repo.path().join("refs").exists(),
        "and no repository is created from it"
    );
    let (code, _out, _err) = run_cli(broken_home.path(), &["snapshot", "list", "demo"]);
    assert_ne!(code, 0, "an unparsable configuration file must not resolve an alias");
    Ok(())
}

#[tokio::test]
async fn i04_cli_snapshot_list__prints_one_line_per_snapshot() -> TestResult {
    let home = TempDir::new()?;
    let repo_dir = TempDir::new()?;
    write_cli_config(home.path(), "demo", repo_dir.path())?;

    let (code, _out, stderr) = run_cli(home.path(), &["repo", "create", "demo"]);
    assert_eq!(code, 0, "`repo create demo` failed: {stderr}");

    let (code, stdout, stderr) = run_cli(home.path(), &["snapshot", "list", "demo"]);
    assert_eq!(code, 0, "`snapshot list demo` failed: {stderr}");
    assert_eq!(stdout.lines().count(), 1, "a fresh repository has one snapshot");

    // add two commits through the library, and the interface reports them
    let storage = fs_storage(repo_dir.path()).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    let older = session.commit("from the library", None).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/later"), Bytes::new()).await?;
    let newer = session.commit("and one more", None).await?;

    let (code, stdout, stderr) = run_cli(home.path(), &["snapshot", "list", "demo"]);
    assert_eq!(code, 0, "`snapshot list demo` failed: {stderr}");
    assert_eq!(stdout.lines().count(), 3);

    // the ancestry is printed newest first: the first line is the branch tip
    // and the last is the repository's initial commit. Each line carries its
    // own snapshot's id; nothing else about a line's layout is asserted.
    let lines: Vec<&str> = stdout.lines().collect();
    assert!(
        lines[0].contains(newer.to_string().as_str()),
        "`snapshot list` prints newest first, so the first line is the tip: {stdout}"
    );
    assert!(
        lines[lines.len() - 1]
            .contains(Snapshot::INITIAL_SNAPSHOT_ID.to_string().as_str()),
        "`snapshot list` prints newest first, so the last line is the initial \
         commit: {stdout}"
    );
    assert!(
        lines[1].contains(older.to_string().as_str()),
        "the middle line is the commit between them: {stdout}"
    );

    // §12.4: `-n` caps the listing (default 10) and `-b` chooses the branch
    // (default `main`)
    let (code, stdout, stderr) =
        run_cli(home.path(), &["snapshot", "list", "demo", "-n", "2"]);
    assert_eq!(code, 0, "`snapshot list demo -n 2` failed: {stderr}");
    assert_eq!(stdout.lines().count(), 2, "`-n 2` caps the listing at two lines");
    let lines: Vec<&str> = stdout.lines().collect();
    assert!(
        lines[0].contains(newer.to_string().as_str()),
        "`-n` keeps the newest snapshots: {stdout}"
    );

    repo.create_branch("side", &older).await?;
    let (code, stdout, stderr) =
        run_cli(home.path(), &["snapshot", "list", "demo", "-b", "side"]);
    assert_eq!(code, 0, "`snapshot list demo -b side` failed: {stderr}");
    assert_eq!(stdout.lines().count(), 2, "`side` holds two snapshots");
    let lines: Vec<&str> = stdout.lines().collect();
    assert!(
        lines[0].contains(older.to_string().as_str()),
        "`-b side` lists that branch's tip first: {stdout}"
    );
    assert!(
        !stdout.contains(newer.to_string().as_str()),
        "`-b side` must not report a snapshot only `main` holds: {stdout}"
    );

    let (code, _out, _err) =
        run_cli(home.path(), &["snapshot", "list", "demo", "-b", "no-such-branch"]);
    assert_ne!(code, 0, "listing a branch that does not exist must fail");

    let (code, _out, _err) = run_cli(home.path(), &["snapshot", "list", "missing"]);
    assert_ne!(code, 0, "an unknown alias must fail");
    Ok(())
}

// ---------------------------------------------------------------------------
// i05_manifest_splitting — milestone 06: a configured split size cuts an
// array's references into several manifests.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i05_manifest_splitting__a_split_size_produces_several_manifests()
-> TestResult {
    let storage = mem_storage().await?;
    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        manifest: Some(ManifestConfig {
            splitting: Some(ManifestSplittingConfig::with_size(3)),
            ..Default::default()
        }),
        ..Default::default()
    };
    let repo = Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 10).await?;
    for idx in 0..10u32 {
        session
            .set_chunk_ref(
                p("/array"),
                ChunkIndices(vec![idx]),
                Some(virtual_payload("s3://foo/bar", idx as u64, 8)?),
            )
            .await?;
    }
    let snapshot = session.commit("split", None).await?;

    let info = repo.lookup_snapshot(&snapshot).await?;
    assert_eq!(info.manifests.len(), 4, "10 references in groups of 3");
    assert_eq!(
        info.manifests.iter().map(|m| m.num_chunk_refs).sum::<u32>(),
        10
    );

    // every reference is still reachable through the split manifests
    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    for idx in 0..10u32 {
        assert!(
            matches!(
                readonly.get_chunk_ref(&p("/array"), &ChunkIndices(vec![idx])).await?,
                Some(ChunkPayload::Virtual(_))
            ),
            "chunk {idx} went missing"
        );
    }
    Ok(())
}

#[tokio::test]
async fn i05_manifest_splitting__without_a_split_size_there_is_one_manifest()
-> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 10).await?;
    for idx in 0..10u32 {
        session
            .set_chunk_ref(
                p("/array"),
                ChunkIndices(vec![idx]),
                Some(virtual_payload("s3://foo/bar", idx as u64, 8)?),
            )
            .await?;
    }
    let snapshot = session.commit("unsplit", None).await?;

    let info = repo.lookup_snapshot(&snapshot).await?;
    assert_eq!(info.manifests.len(), 1);
    assert_eq!(info.manifests[0].num_chunk_refs, 10);
    Ok(())
}

#[tokio::test]
async fn i05_manifest_splitting__manifest_configuration_survives_a_round_trip()
-> TestResult {
    let storage = mem_storage().await?;
    let splitting = ManifestSplittingConfig::with_size(7);
    let preload = ManifestPreloadConfig {
        max_total_refs: Some(123),
        preload_if: Some(ManifestPreloadCondition::NameMatches {
            regex: "^t$".to_string(),
        }),
    };
    let config = RepositoryConfig {
        manifest: Some(ManifestConfig {
            splitting: Some(splitting.clone()),
            preload: Some(preload.clone()),
        }),
        ..Default::default()
    };
    let repo = Repository::create(Some(config), Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, 4).await?;
    session.commit("seed", None).await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let manifest = reopened.config().manifest();
    assert_eq!(manifest.splitting(), &splitting);
    assert_eq!(manifest.preload().max_total_refs(), 123);
    assert_eq!(
        manifest.preload().preload_if(),
        &ManifestPreloadCondition::NameMatches { regex: "^t$".to_string() }
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// i06_virtual_container_rules — milestone 07: a virtual chunk container's URL
// prefix must end in `/`, so that authorisation is prefix-exact rather than
// substring-based.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i06_virtual_container_rules__a_prefix_must_end_in_a_separator() -> TestResult {
    for prefix in ["s3://bucket", "s3://bucket/path"] {
        let err = VirtualChunkContainer::new(
            prefix.to_string(),
            ObjectStoreConfig::S3(s3_options("us-east-1")),
        )
        .err()
        .ok_or("a prefix without a trailing separator must be refused")?;
        assert!(
            err.contains("must end in a / character"),
            "unexpected rejection for {prefix}: {err}"
        );
    }

    // with the separator the same prefixes are accepted and preserved verbatim
    for prefix in ["s3://bucket/", "s3://bucket/path/"] {
        let container =
            vcc(prefix, ObjectStoreConfig::S3(s3_options("us-east-1")));
        assert_eq!(container.url_prefix(), prefix);
        assert!(container.name.is_none());
    }
    Ok(())
}

#[tokio::test]
async fn i06_virtual_container_rules__the_scheme_must_match_the_store() -> TestResult {
    // a scheme the store does not serve is refused
    assert!(
        VirtualChunkContainer::new(
            "gcs://bucket/".to_string(),
            ObjectStoreConfig::S3(s3_options("us-east-1")),
        )
        .is_err()
    );
    // a host is required
    assert!(
        VirtualChunkContainer::new(
            "s3:///".to_string(),
            ObjectStoreConfig::S3(s3_options("us-east-1")),
        )
        .is_err()
    );
    // and the credentials have to match the store as well
    let container = vcc("s3://bucket/", ObjectStoreConfig::S3(s3_options("us-east-1")));
    assert!(container.validate_credentials(None).is_ok());
    assert!(
        container
            .validate_credentials(Some(&Credentials::S3(S3Credentials::Anonymous)))
            .is_ok()
    );
    assert!(
        container
            .validate_credentials(Some(&Credentials::Gcs(GcsCredentials::Anonymous)))
            .is_err()
    );
    Ok(())
}

#[tokio::test]
async fn i06_virtual_container_rules__the_resolver_repairs_and_drops_containers()
-> TestResult {
    let valid = vcc("s3://bucket/", ObjectStoreConfig::S3(s3_options("us-east-1")));
    let serialized = serde_json::to_string(&valid)?;

    // a persisted container whose prefix lost its separator is repaired
    let unslashed: VirtualChunkContainer =
        serde_json::from_str(&serialized.replace("s3://bucket/", "s3://bucket"))?;
    assert_eq!(unslashed.url_prefix(), "s3://bucket");
    let resolver = VirtualChunkResolver::new(
        vec![unslashed].into_iter(),
        HashMap::new(),
        Settings::default(),
    );
    let matched = resolver
        .matching_container("s3://bucket/a/b.dat")
        .ok_or("the resolver must repair a missing trailing separator")?;
    assert_eq!(matched.url_prefix(), "s3://bucket/");

    // a persisted container that cannot be revalidated is dropped, not fatal
    let bogus: VirtualChunkContainer =
        serde_json::from_str(&serialized.replace("s3://bucket/", "ftp://bucket/"))?;
    let resolver = VirtualChunkResolver::new(
        vec![bogus].into_iter(),
        HashMap::new(),
        Settings::default(),
    );
    assert!(resolver.matching_container("ftp://bucket/a").is_none());
    Ok(())
}

#[tokio::test]
async fn i06_virtual_container_rules__authorisation_is_prefix_exact() -> TestResult {
    let storage = mem_storage().await?;
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(vcc(
        "s3://bucket/data/",
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    ));
    let creds: HashMap<String, Option<Credentials>> = [(
        "s3://bucket/data/".to_string(),
        Some(Credentials::S3(S3Credentials::Anonymous)),
    )]
    .into();
    let repo = Repository::create(Some(config), Arc::clone(&storage), creds).await?;

    // the repository carries exactly the one container it was given (§3). The
    // *authorised* view of the same thing,
    // `Repository::authorized_virtual_container_prefixes`, is additive at this
    // release and is asserted twice over in `g02_config_vcc_replace` and
    // `g12_anonymous_credentials`; this group scores milestone 07's inherited
    // prefix rule, so it stays on the surface that milestone already had.
    assert_eq!(repo.config().virtual_chunk_containers().count(), 1);
    assert_eq!(
        repo.config()
            .get_virtual_chunk_container("s3://bucket/data/")
            .map(|c| c.url_prefix().to_string()),
        Some("s3://bucket/data/".to_string())
    );

    let session = repo.writable_session("main").await?;
    let inside = VirtualChunkLocation::from_absolute_path("s3://bucket/data/x.dat")?;
    assert_eq!(
        session.matching_container(&inside).map(|c| c.url_prefix().to_string()),
        Some("s3://bucket/data/".to_string())
    );

    // a sibling whose name merely starts with the same characters is NOT inside
    let sibling =
        VirtualChunkLocation::from_absolute_path("s3://bucket/database/x.dat")?;
    assert!(
        session.matching_container(&sibling).is_none(),
        "the trailing separator is what makes the prefix exact"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// i07_expiration_gc — milestones 03 and 05: expiration and garbage collection,
// TEST_INTERFACE.md §12.3. Driven directly here rather than only inside the
// end-to-end gate, so a regression in either operation localises to this group.
// Nothing sleeps and nothing compares a stored timestamp to a clock: every
// threshold is an instant captured *after* the commits it is applied to, so
// every object in play is unambiguously older than it.
// ---------------------------------------------------------------------------

/// A repository over in-memory storage whose chunks are always materialised,
/// together with its storage and resolved settings.
async fn gc_repo()
-> Result<(DynStorage, Settings, Repository), Box<dyn std::error::Error>> {
    let storage = mem_storage().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    Ok((storage, settings, repo))
}

/// Commit one native chunk at `coord` of `/array` on `branch`, seeding the array
/// first when `seed` is set. Returns the new snapshot id.
async fn gc_commit(
    repo: &Repository,
    branch: &str,
    seed: bool,
    coord: u32,
    body: &'static [u8],
    message: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session(branch).await?;
    if seed {
        seed_array(&mut session, 8).await?;
    }
    let payload = session.get_chunk_writer()(Bytes::from_static(body)).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![coord]), Some(payload)).await?;
    Ok(session.commit(message, None).await?)
}

/// Leave exactly one commit reachable from no reference: branch away from
/// `from`, commit on the scratch branch, then delete the branch. `main` is never
/// touched. Written with the branch API of TEST_INTERFACE.md §12.1 rather than
/// with `reset_branch`, because `reset_branch`'s third argument is new at this
/// release and this group scores inherited behaviour.
async fn gc_dangling_commit(
    repo: &Repository,
    from: &SnapshotId,
    coord: u32,
    body: &'static [u8],
    message: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    repo.create_branch("scratch", from).await?;
    let dangling = gc_commit(repo, "scratch", false, coord, body, message).await?;
    repo.delete_branch("scratch").await?;
    Ok(dangling)
}

/// `GCConfig::clean_all` with the tuning arguments fixed.
fn gc_clean_all(at: chrono::DateTime<chrono::Utc>) -> Result<GCConfig, &'static str> {
    Ok(GCConfig::clean_all(
        at,
        at,
        None,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        false,
    ))
}

/// `GCConfig::new` with every dangling class set to `Action::Keep`.
fn gc_keep_everything() -> Result<GCConfig, &'static str> {
    Ok(GCConfig::new(
        HashSet::new(),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        false,
    ))
}

#[tokio::test]
async fn i07_expiration_gc__nothing_dangling_is_collected_and_keep_collects_nothing()
-> TestResult {
    let (storage, settings, repo) = gc_repo().await?;
    let _first = gc_commit(&repo, "main", true, 0, b"first", "first").await?;
    let second = gc_commit(&repo, "main", false, 1, b"second", "second").await?;
    assert_eq!(repo.lookup_branch("main").await?, second);

    assert_eq!(
        GCSummary::default(),
        GCSummary {
            bytes_deleted: 0,
            chunks_deleted: 0,
            manifests_deleted: 0,
            snapshots_deleted: 0,
            attributes_deleted: 0,
            transaction_logs_deleted: 0,
        }
    );

    // everything is reachable from `main`, so a full collection deletes nothing
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &gc_clean_all(Utc::now())?,
    )
    .await?;
    assert_eq!(summary, GCSummary::default());

    // now leave a third commit dangling, but ask for `Action::Keep` everywhere:
    // still nothing is deleted
    let _dangling = gc_dangling_commit(&repo, &second, 2, b"third", "third").await?;
    assert_eq!(repo.lookup_branch("main").await?, second, "`main` is where it was");
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &gc_keep_everything()?,
    )
    .await?;
    assert_eq!(summary, GCSummary::default());

    // and the dangling commit is still there to be collected later
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &gc_clean_all(Utc::now())?,
    )
    .await?;
    assert!(summary.snapshots_deleted > 0, "the dangling commit must be collectable");
    Ok(())
}

#[tokio::test]
async fn i07_expiration_gc__a_dangling_commit_is_collected_and_history_still_reads()
-> TestResult {
    let (storage, settings, repo) = gc_repo().await?;
    let first = gc_commit(&repo, "main", true, 0, b"first", "first").await?;
    let _dangling = gc_dangling_commit(&repo, &first, 1, b"second", "second").await?;

    assert_eq!(repo.lookup_branch("main").await?, first);

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &gc_clean_all(Utc::now())?,
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1);
    assert_eq!(summary.chunks_deleted, 1);
    assert_eq!(summary.manifests_deleted, 1);
    assert_eq!(summary.transaction_logs_deleted, 1);
    assert_eq!(summary.attributes_deleted, 0);
    assert!(summary.bytes_deleted > 0);

    // the surviving history still reads, through a fresh handle
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let at_tip = reopened
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    let payload = at_tip.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?;
    assert!(matches!(payload, Some(ChunkPayload::Ref(_))));
    assert!(
        at_tip.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await?.is_none(),
        "the collected commit's reference is gone from the branch"
    );
    Ok(())
}

#[tokio::test]
async fn i07_expiration_gc__expiration_reports_and_optionally_deletes_expired_refs()
-> TestResult {
    assert_eq!(
        ExpireResult::default(),
        ExpireResult {
            released_snapshots: HashSet::new(),
            edited_snapshots: HashSet::new(),
            deleted_refs: HashSet::new(),
        }
    );

    // `Ignore` leaves every reference where it is
    let (storage, settings, repo) = gc_repo().await?;
    let first = gc_commit(&repo, "main", true, 0, b"first", "first").await?;
    gc_commit(&repo, "main", false, 1, b"second", "second").await?;
    let tip = gc_commit(&repo, "main", false, 2, b"third", "third").await?;
    repo.create_tag("v1", &first).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now(),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    assert!(result.deleted_refs.is_empty(), "Ignore must delete no reference");
    assert!(!result.released_snapshots.is_empty());
    assert!(!result.edited_snapshots.is_empty());
    assert_eq!(repo.lookup_branch("main").await?, tip, "no reference is moved");
    assert!(repo.list_tags().await?.contains("v1"));

    // `Delete` retires a reference whose ancestry has wholly expired, but never
    // the default branch
    let (storage, settings, repo) = gc_repo().await?;
    let first = gc_commit(&repo, "main", true, 0, b"first", "first").await?;
    gc_commit(&repo, "main", false, 1, b"second", "second").await?;
    repo.create_tag("v1", &first).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now(),
        ExpiredRefAction::Delete,
        ExpiredRefAction::Delete,
    )
    .await?;
    assert!(!result.deleted_refs.is_empty(), "Delete must retire the expired tag");
    assert!(!repo.list_tags().await?.contains("v1"));
    assert!(repo.list_branches().await?.contains("main"), "`main` is never deleted");
    Ok(())
}
