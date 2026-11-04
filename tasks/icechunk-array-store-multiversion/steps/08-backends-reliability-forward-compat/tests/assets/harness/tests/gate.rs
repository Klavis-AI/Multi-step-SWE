// Verifier-owned end-to-end driver for milestone 08 of earth-mover/icechunk
// (upstream tag v1.1.21, Rust crate `icechunk` 0.3.24).
//
// This is the scored group `gate-endtoend`: this milestone's headline
// capability at proof-of-life depth, plus one end-to-end path through every
// milestone the step inherits. It is compiled and reported separately from
// `conformance.rs`, so neither can take the other down.
//
// Contract: /app/TEST_INTERFACE.md. Offline and deterministic, exactly like the
// conformance driver: in-memory or temporary-directory storage only, no
// network, no cloud credential, no virtual chunk ever dereferenced.

#![allow(non_snake_case)]
#![allow(clippy::bool_assert_comparison)]

use std::{
    collections::HashMap,
    num::{NonZeroU16, NonZeroUsize},
    path::Path as FsPath,
    process::Command,
    sync::Arc,
};

use bytes::Bytes;
use chrono::Utc;
use futures::TryStreamExt;
use tempfile::TempDir;

use icechunk::{
    ObjectStoreConfig, Repository, RepositoryConfig, Storage,
    config::{
        CachingConfig, Credentials, DEFAULT_MAX_CONCURRENT_REQUESTS, ManifestConfig,
        ManifestSplittingConfig, S3Credentials, S3Options,
    },
    conflicts::detector::ConflictDetector,
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::{ArrayShape, Snapshot},
    },
    inspect::snapshot_json,
    new_in_memory_storage, new_local_filesystem_storage,
    ops::{
        gc::{ExpiredRefAction, GCConfig, GCSummary, expire, garbage_collect},
        stats::repo_chunks_storage,
    },
    repository::{RepositoryErrorKind, VersionInfo},
    session::{SessionErrorKind, get_chunk},
    virtual_chunks::VirtualChunkContainer,
};

type TestResult = Result<(), Box<dyn std::error::Error>>;
type DynStorage = Arc<dyn Storage + Send + Sync>;

fn p(s: &str) -> Path {
    match Path::try_from(s) {
        Ok(path) => path,
        Err(err) => panic!("driver bug: bad path literal {s:?}: {err:?}"),
    }
}

fn shape(len: u64) -> ArrayShape {
    match ArrayShape::new(vec![(len, 1)]) {
        Some(s) => s,
        None => panic!("driver bug: bad shape"),
    }
}

fn s3_options(region: &str) -> S3Options {
    S3Options {
        region: Some(region.to_string()),
        endpoint_url: None,
        anonymous: true,
        allow_http: false,
        force_path_style: false,
        network_stream_timeout_seconds: None,
        requester_pays: false,
    }
}

async fn mem_storage() -> Result<DynStorage, Box<dyn std::error::Error>> {
    let storage: DynStorage = new_in_memory_storage().await?;
    Ok(storage)
}

async fn fs_storage(path: &FsPath) -> Result<DynStorage, Box<dyn std::error::Error>> {
    let storage: DynStorage = new_local_filesystem_storage(path).await?;
    Ok(storage)
}

fn run_cli(home: &FsPath, args: &[&str]) -> (i32, String, String) {
    let binary = match std::env::var("ICECHUNK_TEST_CLI_BIN") {
        Ok(path) if !path.is_empty() => path,
        _ => panic!("driver bug: ICECHUNK_TEST_CLI_BIN is not set"),
    };
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

// ---------------------------------------------------------------------------
// 1. The workspace builds and every public entry point this milestone names is
//    reachable and behaves at proof-of-life depth.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__the_new_public_surface_is_reachable() -> TestResult {
    // configuration: the new field, its constant and its accessor
    assert_eq!(DEFAULT_MAX_CONCURRENT_REQUESTS, 256u16);
    assert_eq!(RepositoryConfig::default().max_concurrent_requests(), 256);
    assert_eq!(CachingConfig::default().num_snapshot_nodes(), 500_000);

    // statistics: the new struct and its derived totals
    let stats = icechunk::ops::stats::ChunkStorageStats::new(1, 2, 3);
    assert_eq!(stats.non_virtual_bytes(), 4);
    assert_eq!(stats.total_bytes(), 6);

    // a repository, a session, a chunk of each kind, a commit, and the
    // snapshot rendered as JSON
    let storage = mem_storage().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(5),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(p("/array"), shape(16), None, Bytes::new()).await?;
    let native =
        session.get_chunk_writer()(Bytes::from_static(b"materialised")).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(native)).await?;
    let snapshot = session.commit("gate", None).await?;

    let json = snapshot_json(repo.asset_manager(), &snapshot, false).await?;
    let value: serde_json::Value = serde_json::from_str(&json)?;
    assert_eq!(value["id"], serde_json::json!(snapshot.to_string()));

    // the storage existence probe the forward-compatibility guard rests on
    assert_eq!(storage.has_v2_repo_info().await?, false);
    Ok(())
}

// ---------------------------------------------------------------------------
// 2. Forward compatibility, end to end: a repository written by a newer format
//    generation cannot be opened, and cannot be committed into.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__a_newer_generation_repository_is_refused() -> TestResult {
    let dir = TempDir::new()?;
    let storage = fs_storage(dir.path()).await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(p("/array"), shape(8), None, Bytes::new()).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("kept".into())),
        )
        .await?;
    let committed = session.commit("before the upgrade", None).await?;

    // another writer migrates the repository to the next format generation
    let mut pending = repo.writable_session("main").await?;
    pending
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Inline("lost".into())),
        )
        .await?;
    std::fs::write(dir.path().join("repo"), b"icechunk v2 repository")?;

    // opening is refused
    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .err()
        .ok_or("opening a newer-generation repository must fail")?;
    assert!(
        matches!(err.kind, RepositoryErrorKind::RepositoryIsV2),
        "expected RepositoryIsV2, got {err:?}"
    );

    // and so is committing through the session that was already open
    let err = pending
        .commit("after the upgrade", None)
        .await
        .err()
        .ok_or("committing into a newer-generation repository must fail")?;
    assert!(
        matches!(
            err.kind,
            SessionErrorKind::RepositoryError(RepositoryErrorKind::RepositoryIsV2)
        ),
        "expected RepositoryIsV2 on the commit path, got {err:?}"
    );

    // the branch is exactly where it was
    assert_eq!(repo.lookup_branch("main").await?, committed);
    assert_eq!(storage.has_v2_repo_info().await?, true);
    Ok(())
}

// ---------------------------------------------------------------------------
// 3. Chunk reference states and storage statistics over one repository.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__chunk_states_and_storage_statistics() -> TestResult {
    let storage = mem_storage().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(5),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let asset_manager = Arc::new(icechunk::asset_manager::AssetManager::new_no_cache(
        Arc::clone(&storage),
        settings.clone(),
        1,
        DEFAULT_MAX_CONCURRENT_REQUESTS,
    ));

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(p("/array"), shape(16), None, Bytes::new()).await?;

    let native =
        session.get_chunk_writer()(Bytes::copy_from_slice(&[1, 2, 3, 4, 5, 6])).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(native)).await?;
    let inline = session.get_chunk_writer()(Bytes::copy_from_slice(&[7])).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![1]), Some(inline)).await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![2]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path(
                    "s3://bucket/file.dat",
                )?,
                offset: 0,
                length: 500,
                checksum: None,
            })),
        )
        .await?;

    // the four states, before any commit
    assert!(matches!(
        session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Ref(_))
    ));
    assert!(matches!(
        session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Inline(_))
    ));
    assert!(matches!(
        session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![2])).await?,
        Some(ChunkPayload::Virtual(_))
    ));
    assert!(session.get_chunk_ref(&p("/array"), &ChunkIndices(vec![3])).await?.is_none());

    session.commit("mixed", None).await?;

    let stats = repo_chunks_storage(
        storage.as_ref(),
        &settings,
        Arc::clone(&asset_manager),
        NonZeroU16::new(5).ok_or("nonzero")?,
        NonZeroUsize::MAX,
        NonZeroU16::new(10).ok_or("nonzero")?,
    )
    .await?;
    assert_eq!(stats.native_bytes, 6);
    assert_eq!(stats.inlined_bytes, 1);
    assert_eq!(stats.virtual_bytes, 500);
    assert_eq!(stats.non_virtual_bytes(), 7);
    assert_eq!(stats.total_bytes(), 507);
    Ok(())
}

// ---------------------------------------------------------------------------
// 4. Configuration merge, end to end through a real repository.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__configuration_merge_through_a_repository() -> TestResult {
    let storage = mem_storage().await?;
    let mut base = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(128),
        max_concurrent_requests: Some(16),
        caching: Some(CachingConfig { num_chunk_refs: Some(99), ..Default::default() }),
        ..Default::default()
    };
    base.set_virtual_chunk_container(
        VirtualChunkContainer::new(
            "s3://bucket-one/".to_string(),
            ObjectStoreConfig::S3(s3_options("us-east-1")),
        )
        .map_err(|e| format!("container rejected: {e}"))?,
    );

    let creds: HashMap<String, Option<Credentials>> = [(
        "s3://bucket-one/".to_string(),
        Some(Credentials::S3(S3Credentials::Anonymous)),
    )]
    .into();
    let repo = Repository::create(Some(base), Arc::clone(&storage), creds).await?;
    assert_eq!(repo.config().max_concurrent_requests(), 16);
    assert_eq!(repo.config().virtual_chunk_containers().count(), 1);

    // reopening with an overlay: scalars are replaced, sub-configs merge, and
    // the container set is replaced rather than unioned
    let mut overlay = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(512),
        caching: Some(CachingConfig {
            num_snapshot_nodes: Some(77),
            ..Default::default()
        }),
        ..Default::default()
    };
    overlay.set_virtual_chunk_container(
        VirtualChunkContainer::new(
            "s3://bucket-two/".to_string(),
            ObjectStoreConfig::S3(s3_options("us-west-2")),
        )
        .map_err(|e| format!("container rejected: {e}"))?,
    );

    let reopened = repo.reopen(Some(overlay), Some(HashMap::new()))?;
    let config = reopened.config();
    assert_eq!(config.inline_chunk_threshold_bytes, Some(512));
    assert_eq!(config.max_concurrent_requests, Some(16));
    assert_eq!(config.caching().num_chunk_refs(), 99);
    assert_eq!(config.caching().num_snapshot_nodes(), 77);
    assert!(config.get_virtual_chunk_container("s3://bucket-two/").is_some());
    assert!(
        config.get_virtual_chunk_container("s3://bucket-one/").is_none(),
        "the overlay's container set replaces the base's"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// 5. Rebase over a delete-then-recreate, flush, and a conditional branch reset.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__rebase_flush_and_conditional_reset() -> TestResult {
    let storage = mem_storage().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut setup = repo.writable_session("main").await?;
    setup.add_group(p("/foo"), Bytes::new()).await?;
    setup.add_group(p("/foo/bar"), Bytes::new()).await?;
    let base = setup.commit("create directory", None).await?;

    let mut one = repo.writable_session("main").await?;
    let mut two = repo.writable_session("main").await?;

    one.add_group(p("/foo/quux"), Bytes::new()).await?;
    one.commit("sibling", None).await?;

    two.delete_group(p("/foo/bar")).await?;
    two.add_group(p("/foo/bar"), Bytes::from_static(b"recreated")).await?;
    assert!(two.commit("recreate", None).await.is_err());
    two.rebase(&ConflictDetector).await?;
    let after_rebase = two.commit("recreate", None).await?;

    let readonly = repo.readonly_session(&VersionInfo::SnapshotId(after_rebase)).await?;
    assert!(readonly.get_group(&p("/foo/bar")).await.is_ok());
    assert!(readonly.get_group(&p("/foo/quux")).await.is_ok());

    // flush writes a snapshot on a side branch of history without moving `main`
    let tip = repo.lookup_branch("main").await?;
    let mut flushing = repo.writable_session("main").await?;
    flushing.add_group(p("/flushed"), Bytes::new()).await?;
    let flushed = flushing.flush("flushed", None).await?;
    assert!(flushing.branch().is_none());
    assert_eq!(repo.lookup_branch("main").await?, tip);

    // a stale expectation refuses the reset; the right one performs it
    assert!(repo.reset_branch("main", &flushed, Some(&base)).await.is_err());
    repo.reset_branch("main", &flushed, Some(&tip)).await?;
    assert_eq!(repo.lookup_branch("main").await?, flushed);
    Ok(())
}

// ---------------------------------------------------------------------------
// 6. One end-to-end path through milestones 01 to 05: repository, hierarchy,
//    chunks, commits, tags, time travel, expiration and garbage collection.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__inherited_repository_lifecycle() -> TestResult {
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
    assert_eq!(Repository::exists(storage.as_ref()).await?, true);
    assert_eq!(repo.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(p("/array"), shape(8), None, Bytes::new()).await?;
    let payload = session.get_chunk_writer()(Bytes::from_static(b"first")).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(payload)).await?;
    let first = session.commit("first", None).await?;
    repo.create_tag("v1", &first).await?;

    let mut session = repo.writable_session("main").await?;
    let payload = session.get_chunk_writer()(Bytes::from_static(b"second")).await?;
    session.set_chunk_ref(p("/array"), ChunkIndices(vec![1]), Some(payload)).await?;
    let second = session.commit("second", None).await?;

    // time travel: the tag still sees only the first chunk
    let at_tag = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert!(at_tag.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await?.is_none());
    let bytes = get_chunk(
        at_tag
            .get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL)
            .await?,
    )
    .await?;
    assert_eq!(bytes.as_deref(), Some(b"first".as_slice()));

    let ancestry: Vec<String> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect::<Vec<_>>()
        .await?
        .into_iter()
        .map(|s| s.message)
        .collect();
    assert_eq!(
        ancestry,
        vec![
            "second".to_string(),
            "first".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string(),
        ]
    );

    // garbage collection is a no-op while everything is reachable
    let now = Utc::now();
    let gc_config = GCConfig::clean_all(
        now,
        now,
        None,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        false,
    );
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &gc_config,
    )
    .await?;
    assert_eq!(summary, GCSummary::default());

    // leave the second commit dangling, then collect it
    repo.delete_tag("v1").await?;
    repo.reset_branch("main", &first, Some(&second)).await?;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &gc_config,
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1);
    assert_eq!(summary.chunks_deleted, 1);
    assert!(summary.bytes_deleted > 0);

    // the surviving history still reads
    let at_tip =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let bytes = get_chunk(
        at_tip
            .get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL)
            .await?,
    )
    .await?;
    assert_eq!(bytes.as_deref(), Some(b"first".as_slice()));

    // and expiration runs over the remaining ancestry
    let expired = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now(),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    assert!(expired.deleted_refs.is_empty());
    Ok(())
}

// ---------------------------------------------------------------------------
// 7. One end-to-end path through milestones 05 to 07: the command-line
//    interface, manifest splitting, and the virtual-chunk-container prefix
//    rule.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__inherited_cli_splitting_and_virtual_containers() -> TestResult {
    // --- milestone 05: the command-line interface -------------------------
    let home = TempDir::new()?;
    let repo_dir = TempDir::new()?;
    let config_dir = home.path().join("icechunk");
    std::fs::create_dir_all(&config_dir)?;
    std::fs::write(
        config_dir.join("cli-config.yaml"),
        format!(
            "repos:\n  demo: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_dir.path().display()
        ),
    )?;

    let (code, _out, stderr) = run_cli(home.path(), &["repo", "create", "demo"]);
    assert_eq!(code, 0, "`icechunk repo create demo` failed: {stderr}");
    assert!(repo_dir.path().join("refs").is_dir());

    let (code, stdout, stderr) = run_cli(home.path(), &["snapshot", "list", "demo"]);
    assert_eq!(code, 0, "`icechunk snapshot list demo` failed: {stderr}");
    assert_eq!(stdout.lines().count(), 1);

    // --- milestone 06: manifest splitting ---------------------------------
    let storage = mem_storage().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            manifest: Some(ManifestConfig {
                splitting: Some(ManifestSplittingConfig::with_size(4)),
                ..Default::default()
            }),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(p("/array"), shape(12), None, Bytes::new()).await?;
    for idx in 0..12u32 {
        session
            .set_chunk_ref(
                p("/array"),
                ChunkIndices(vec![idx]),
                Some(ChunkPayload::Virtual(VirtualChunkRef {
                    location: VirtualChunkLocation::from_absolute_path(
                        "s3://bucket/data/file.dat",
                    )?,
                    offset: idx as u64 * 8,
                    length: 8,
                    checksum: None,
                })),
            )
            .await?;
    }
    let snapshot: SnapshotId = session.commit("split", None).await?;
    assert_eq!(repo.lookup_snapshot(&snapshot).await?.manifests.len(), 3);

    // --- milestone 07: the container prefix must end in a separator -------
    assert!(
        VirtualChunkContainer::new(
            "s3://bucket/data".to_string(),
            ObjectStoreConfig::S3(s3_options("us-east-1")),
        )
        .is_err(),
        "a prefix without a trailing separator must be refused"
    );
    let container = VirtualChunkContainer::new(
        "s3://bucket/data/".to_string(),
        ObjectStoreConfig::S3(s3_options("us-east-1")),
    )
    .map_err(|e| format!("container rejected: {e}"))?;
    assert_eq!(container.url_prefix(), "s3://bucket/data/");
    Ok(())
}
