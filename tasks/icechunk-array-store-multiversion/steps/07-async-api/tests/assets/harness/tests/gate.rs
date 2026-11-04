// Verifier-owned end-to-end driver for milestone 07 of earth-mover/icechunk.
//
// This is the `gate-endtoend` group: one ordinary scored group, reported from
// its own compile target. It carries this milestone's headline capability - the
// virtual-chunk-container prefix-exactness flip - at end-to-end depth, plus one
// whole path through each milestone this step inherits (01 through 06), so a
// candidate that breaks a predecessor while landing this one is visible here
// even if every conformance group happens to pass.
//
// Every assertion traces to /app/TEST_INTERFACE.md or /app/icechunk-spec/.
// No network, no cloud endpoint, no MinIO, no credential from the environment.
// Repository storage is in-memory or a `tempfile::TempDir`; virtual chunks are
// `file://` URLs inside a `TempDir`; the only process started is the
// candidate's own `icechunk` binary, named by ICECHUNK_TEST_CLI_BIN.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use std::{collections::HashMap, num::NonZeroU16, num::NonZeroUsize, sync::Arc};

use bytes::Bytes;
use chrono::Utc;
use futures::{StreamExt, TryStreamExt};
use tokio::sync::RwLock;

use icechunk::{
    ObjectStoreConfig, Repository, RepositoryConfig, Storage, Store,
    config::{Credentials, ManifestConfig, ManifestSplittingConfig},
    conflicts::basic_solver::BasicConflictSolver,
    format::{
        ByteRange, ChunkIndices, Path,
        manifest::{VirtualChunkLocation, VirtualChunkRef},
        snapshot::{ArrayShape, Snapshot},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{ExpiredRefAction, GCConfig, expire, garbage_collect},
    repository::VersionInfo,
    session::get_chunk,
    store::{StoreError, StoreErrorKind},
    virtual_chunks::VirtualChunkContainer,
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

const GROUP_META: &[u8] = br#"{"zarr_format":3,"node_type":"group"}"#;

/// An age threshold comfortably later than every object these fixtures write,
/// so "older than this" never depends on clock resolution.
fn after_everything() -> chrono::DateTime<Utc> {
    Utc::now() + chrono::Duration::seconds(60)
}

fn materialising() -> RepositoryConfig {
    RepositoryConfig { inline_chunk_threshold_bytes: Some(0), ..Default::default() }
}

// ---------------------------------------------------------------------------
// This milestone: the prefix-exactness flip, end to end
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_virtual_prefix_exactness_end_to_end() -> TestResult {
    let data = tempfile::TempDir::new()?;
    let allowed = data.path().join("foo");
    let sibling = data.path().join("foo-production");
    std::fs::create_dir_all(&allowed)?;
    std::fs::create_dir_all(&sibling)?;
    std::fs::write(allowed.join("chunk.bin"), b"ALLOWED-PAYLOAD")?;
    std::fs::write(sibling.join("secret.bin"), b"SECRET-PAYLOAD!")?;

    let prefix = format!("file://{}/", allowed.display());
    // A prefix that does not end in a separator is refused outright.
    let err = VirtualChunkContainer::new(
        prefix.trim_end_matches('/').to_string(),
        ObjectStoreConfig::LocalFileSystem(allowed.clone()),
    )
    .expect_err("an unseparated prefix must be refused");
    assert!(err.contains("must end in a / character"), "unexpected: {err}");

    let mut config = materialising();
    config.set_virtual_chunk_container(VirtualChunkContainer::new(
        prefix.clone(),
        ObjectStoreConfig::LocalFileSystem(allowed.clone()),
    )?);
    // An authorisation map written before this release keys the container by
    // its unseparated prefix; it must keep working.
    let creds: HashMap<String, Option<Credentials>> =
        [(prefix.trim_end_matches('/').to_string(), None)].into_iter().collect();
    let repo = Repository::create(
        Some(config),
        new_in_memory_storage().await?,
        creds,
    )
    .await?;

    let mut session = repo.writable_session("main").await?;
    let array: Path = "/array".try_into()?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(array.clone(), ArrayShape::new(vec![(2, 1)]).unwrap(), None, Bytes::new())
        .await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    // Inside the authorised prefix: accepted with validation on, and readable.
    let inside = VirtualChunkLocation::from_absolute_path(&format!(
        "file://{}",
        allowed.join("chunk.bin").display()
    ))?;
    store
        .set_virtual_ref(
            "array/c/0",
            VirtualChunkRef {
                location: inside.clone(),
                offset: 0,
                length: 15,
                checksum: None,
            },
            true,
        )
        .await?;
    assert_eq!(
        store.get("array/c/0", &ByteRange::ALL).await?,
        Bytes::from_static(b"ALLOWED-PAYLOAD")
    );

    // The sibling directory merely shares a textual prefix: refused.
    let outside = VirtualChunkLocation::from_absolute_path(&format!(
        "file://{}",
        sibling.join("secret.bin").display()
    ))?;
    match store
        .set_virtual_ref(
            "array/c/1",
            VirtualChunkRef {
                location: outside.clone(),
                offset: 0,
                length: 14,
                checksum: None,
            },
            true,
        )
        .await
    {
        Err(StoreError {
            kind: StoreErrorKind::InvalidVirtualChunkContainer { chunk_location },
            ..
        }) => assert_eq!(chunk_location, outside.url()),
        other => panic!("the sibling prefix must not be covered, got {other:?}"),
    }

    // Commit, reopen, and read the virtual chunk back through a fresh session.
    let handle = store.session();
    let mut session = handle.write().await;
    let locations: Vec<String> =
        session.all_virtual_chunk_locations().await?.try_collect().await?;
    assert_eq!(locations, vec![inside.url().to_string()]);
    let head = session.commit("virtual", None).await?;
    drop(session);

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(head)).await?;
    let got = get_chunk(
        ro.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::bounded(0, 7))
            .await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"ALLOWED"));
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 01 — the repository format on a real local-filesystem backend
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_repository_format_round_trip() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage = new_local_filesystem_storage(&root).await?;
    let repo = Repository::create(Some(materialising()), storage, HashMap::new()).await?;

    let array: Path = "/group/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::copy_from_slice(GROUP_META)).await?;
    session.add_group("/group".try_into()?, Bytes::copy_from_slice(GROUP_META)).await?;
    session
        .add_array(array.clone(), ArrayShape::new(vec![(4, 1)]).unwrap(), None, Bytes::new())
        .await?;
    for i in 0..4u32 {
        let payload = session.get_chunk_writer()(Bytes::from(format!("chunk-{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    let first = session.commit("first", None).await?;
    repo.create_tag("v1", &first).await?;

    let mut session = repo.writable_session("main").await?;
    let payload = session.get_chunk_writer()(Bytes::from_static(b"overwritten")).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0]), Some(payload)).await?;
    let second = session.commit("second", None).await?;

    assert!(root.join("refs").is_dir());
    assert!(root.join("snapshots").is_dir());
    assert_eq!(repo.lookup_branch("main").await?, second);

    // Time travel: the tag still sees the original chunk.
    let old = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    let got = get_chunk(
        old.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"chunk-0"));

    let tip = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let got = get_chunk(
        tip.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"overwritten"));

    let history: Vec<_> = repo.snapshot_ancestry(&second).await?.try_collect().await?;
    assert_eq!(
        history.iter().map(|i| i.message.clone()).collect::<Vec<_>>(),
        vec![
            "second".to_string(),
            "first".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string()
        ]
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 02 — sessions, a real conflict, and a rebase that resolves it
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_sessions_conflict_and_rebase() -> TestResult {
    let repo = Repository::create(
        Some(materialising()),
        new_in_memory_storage().await?,
        HashMap::new(),
    )
    .await?;
    let array: Path = "/arr".try_into()?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(array.clone(), ArrayShape::new(vec![(4, 1)]).unwrap(), None, Bytes::new())
        .await?;
    base.commit("base", None).await?;

    let mut ours = repo.writable_session("main").await?;
    let mut theirs = repo.writable_session("main").await?;

    let p = theirs.get_chunk_writer()(Bytes::from_static(b"theirs")).await?;
    theirs.set_chunk_ref(array.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    theirs.commit("theirs", None).await?;

    let p = ours.get_chunk_writer()(Bytes::from_static(b"ours00")).await?;
    ours.set_chunk_ref(array.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    assert!(ours.commit("ours", None).await.is_err(), "the tip moved");

    ours.rebase(&BasicConflictSolver::default()).await?;
    let head = ours.commit("ours after rebase", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, head);

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(head)).await?;
    let got = get_chunk(
        ro.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"ours00"), "the solver keeps our version");
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 03 — expiration and garbage collection over a live repository
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_expiration_and_garbage_collection() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo =
        Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?;
    let array: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(array.clone(), ArrayShape::new(vec![(8, 1)]).unwrap(), None, Bytes::new())
        .await?;
    for i in 0..8u32 {
        let p = session.get_chunk_writer()(Bytes::from_static(b"original")).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(p)).await?;
    }
    session.commit("first", None).await?;

    let mut session = repo.writable_session("main").await?;
    for i in 0..4u32 {
        let p = session.get_chunk_writer()(Bytes::from_static(b"replaced")).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(p)).await?;
    }
    let head = session.commit("second", None).await?;
    let before = storage.list_chunks(&settings).await?.count().await;

    let expired = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        after_everything(),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    assert!(!expired.released_snapshots.is_empty());

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(
            after_everything(),
            after_everything(),
            None,
            NonZeroU16::new(50).unwrap(),
            NonZeroUsize::new(64 * 1024 * 1024).unwrap(),
            NonZeroU16::new(100).unwrap(),
            false,
        ),
    )
    .await?;
    assert!(summary.chunks_deleted > 0, "the superseded chunks are unreachable");
    let after = storage.list_chunks(&settings).await?.count().await;
    assert_eq!(after as u64, before as u64 - summary.chunks_deleted);

    // The surviving tip still reads correctly.
    assert_eq!(repo.lookup_branch("main").await?, head);
    let ro = repo.readonly_session(&VersionInfo::SnapshotId(head)).await?;
    for i in 0..8u32 {
        let want: &[u8] = if i < 4 { b"replaced" } else { b"original" };
        let got = get_chunk(
            ro.get_chunk_reader(&array, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?;
        assert_eq!(got.unwrap(), Bytes::copy_from_slice(want), "chunk {i}");
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 04 — the FlatBuffers metadata format and verbatim Zarr bytes
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_flatbuffers_format_and_diff() -> TestResult {
    let repo = Repository::create(None, new_in_memory_storage().await?, HashMap::new())
        .await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    let meta = Bytes::from_static(
        br#"{"zarr_format":3,"node_type":"group","attributes":{"a":1},"future_extension":{"x":[1,2]}}"#,
    );
    store.set("zarr.json", meta.clone()).await?;
    assert_eq!(
        store.get("zarr.json", &ByteRange::ALL).await?,
        meta,
        "unknown Zarr metadata survives byte for byte"
    );

    let base = store.session().write().await.commit("base", None).await?;

    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("g/zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    let head = store.session().write().await.commit("added a group", None).await?;

    let diff = repo
        .diff(&VersionInfo::SnapshotId(base), &VersionInfo::SnapshotId(head.clone()))
        .await?;
    assert!(diff.new_groups.contains(&"/g".try_into()?));

    let snapshot = repo.asset_manager().fetch_snapshot(&head).await?;
    let bytes = snapshot.bytes().to_vec();
    assert_eq!(&bytes[4..8], b"Ichk", "the FlatBuffers file identifier");
    let reloaded = Snapshot::from_buffer(bytes)?;
    assert_eq!(reloaded.id(), head);
    assert_eq!(reloaded.message(), "added a group");
    assert!(Snapshot::from_buffer(vec![0u8; 16]).is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 05 — the icechunk command-line interface, as a process
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_cli_creates_and_lists_a_repository() -> TestResult {
    let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
        .expect("ICECHUNK_TEST_CLI_BIN must name the candidate's icechunk binary");
    let home = tempfile::TempDir::new()?;
    let repo_root = tempfile::TempDir::new()?;
    let repo_dir = repo_root.path().join("repo");

    let cfg_dir = home.path().join("icechunk");
    std::fs::create_dir_all(&cfg_dir)?;
    std::fs::write(
        cfg_dir.join("cli-config.yaml"),
        format!(
            "repos:\n  local1: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_dir.display()
        ),
    )?;

    let run = |args: &[&str]| {
        std::process::Command::new(&bin)
            .args(args)
            .env("XDG_CONFIG_HOME", home.path())
            .output()
            .expect("the icechunk binary must be executable")
    };

    let listed = run(&["config", "list"]);
    assert_eq!(listed.status.code(), Some(0));
    assert!(String::from_utf8_lossy(&listed.stdout).contains("local1"));

    let created = run(&["repo", "create", "local1"]);
    assert_eq!(
        created.status.code(),
        Some(0),
        "stderr: {}",
        String::from_utf8_lossy(&created.stderr)
    );
    assert!(repo_dir.join("refs").is_dir());
    assert!(repo_dir.join("snapshots").is_dir());

    let snapshots = run(&["snapshot", "list", "local1"]);
    assert_eq!(
        snapshots.status.code(),
        Some(0),
        "stderr: {}",
        String::from_utf8_lossy(&snapshots.stderr)
    );
    let out = String::from_utf8_lossy(&snapshots.stdout);
    assert!(out.contains("SnapshotInfo {"), "stdout: {out}");
    assert!(out.contains(Snapshot::INITIAL_COMMIT_MESSAGE), "stdout: {out}");

    // The same repository is readable through the library.
    let storage = new_local_filesystem_storage(&repo_dir).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(repo.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);

    assert_eq!(run(&["repo", "create", "no-such-alias"]).status.code(), Some(1));
    assert_eq!(run(&["definitely-not-a-command"]).status.code(), Some(2));
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 06 — manifest splitting, invisible to readers
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_manifest_splitting_end_to_end() -> TestResult {
    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        manifest: Some(ManifestConfig {
            splitting: Some(ManifestSplittingConfig::with_size(3)),
            ..Default::default()
        }),
        ..Default::default()
    };
    let repo =
        Repository::create(Some(config), new_in_memory_storage().await?, HashMap::new())
            .await?;
    let array: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(array.clone(), ArrayShape::new(vec![(8, 1)]).unwrap(), None, Bytes::new())
        .await?;
    for i in 0..8u32 {
        let p = session.get_chunk_writer()(Bytes::from(format!("value-{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(p)).await?;
    }
    let head = session.commit("split", None).await?;

    // The root snapshot id is the fixed constant this milestone introduced.
    let history: Vec<_> = repo.snapshot_ancestry(&head).await?.try_collect().await?;
    assert_eq!(history.last().map(|i| i.id.clone()), Some(Snapshot::INITIAL_SNAPSHOT_ID));

    let snapshot = repo.asset_manager().fetch_snapshot(&head).await?;
    let manifests: Vec<_> = snapshot.manifest_files().collect();
    assert_eq!(manifests.len(), 3, "eight chunks split three at a time");
    assert_eq!(manifests.iter().map(|m| m.num_chunk_refs).sum::<u32>(), 8);

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(head)).await?;
    for i in 0..8u32 {
        let got = get_chunk(
            ro.get_chunk_reader(&array, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?;
        assert_eq!(got.unwrap(), Bytes::from(format!("value-{i}")), "chunk {i}");
    }
    Ok(())
}
