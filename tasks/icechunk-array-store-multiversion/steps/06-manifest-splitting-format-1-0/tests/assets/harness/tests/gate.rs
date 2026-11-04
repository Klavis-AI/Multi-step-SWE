// Verifier-owned end-to-end driver for milestone 06 — the `gate-endtoend`
// scored group.
//
// One path through this step's headline capability (manifest splitting, and
// the explicit virtual-chunk authorisation that ships with it), plus one path
// through each milestone this step inherits:
//
//   01  the object layout and the two kinds of reference
//   02  sessions, commit conflicts and rebase
//   03  expiration and garbage collection
//   04  the FlatBuffers format behind its 39-byte header
//   05  the `icechunk` command-line interface
//
// It is one ordinary scored group: it multiplies nothing and stops nothing.
// Compiled as its own target, so a conformance driver that does not typecheck
// cannot take it down, and vice versa.
//
// No network, no cloud, no server, no wall-clock sleep.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use std::{collections::HashMap, path::PathBuf, sync::Arc};

use bytes::Bytes;
use chrono::Utc;
use futures::StreamExt;

use icechunk::{
    ObjectStoreConfig, Repository, RepositoryConfig, Storage,
    config::{
        Credentials, ManifestConfig, ManifestPreloadConfig, ManifestSplittingConfig,
    },
    conflicts::basic_solver::BasicConflictSolver,
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::{ArrayShape, Snapshot},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::{
        gc::{GCConfig, garbage_collect},
        manifests::rewrite_manifests,
    },
    repository::VersionInfo,
    session::get_chunk,
    virtual_chunks::VirtualChunkContainer,
};

type TestResult = Result<(), Box<dyn std::error::Error>>;
type AnyResult<T> = Result<T, Box<dyn std::error::Error>>;

async fn mem() -> AnyResult<Arc<dyn Storage + Send + Sync>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(storage)
}

fn config_with_splitting(splitting: Option<ManifestSplittingConfig>) -> RepositoryConfig {
    RepositoryConfig {
        manifest: Some(ManifestConfig {
            preload: Some(ManifestPreloadConfig {
                max_total_refs: Some(0),
                preload_if: None,
            }),
            splitting,
        }),
        ..Default::default()
    }
}

fn p(path: &str) -> Path {
    Path::try_from(path).unwrap()
}

async fn read_1d(repo: &Repository, array: &Path, len: u32) -> AnyResult<Vec<Bytes>> {
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let mut out = Vec::with_capacity(len as usize);
    for i in 0..len {
        out.push(
            get_chunk(
                session
                    .get_chunk_reader(array, &ChunkIndices(vec![i]), &ByteRange::ALL)
                    .await?,
            )
            .await?
            .ok_or("chunk is missing")?,
        );
    }
    Ok(out)
}

// ---------------------------------------------------------------------------
// This step's new capability.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_manifest_splitting_end_to_end() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    for i in 0..10u32 {
        session
            .set_chunk_ref(
                array.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(Bytes::from(format!("{i}")))),
            )
            .await?;
    }
    let first = session.commit("write the array", None).await?;

    // 10 chunks at split size 3 land in four manifests, not one.
    let settings = storage.default_settings();
    assert_eq!(storage.list_manifests(&settings).await?.count().await, 4);
    assert_eq!(repo.lookup_snapshot(&first).await?.manifests.len(), 4);

    let expected: Vec<Bytes> = (0..10).map(|i| Bytes::from(format!("{i}"))).collect();
    assert_eq!(read_1d(&repo, &array, 10).await?, expected);

    // Re-laying-out the manifests changes the layout and nothing else.
    let policy = ManifestSplittingConfig::with_size(12);
    let consolidating = repo.reopen(Some(config_with_splitting(Some(policy))), None)?;
    let rewritten =
        rewrite_manifests(&consolidating, "main", "consolidate manifests", None).await?;
    assert_eq!(repo.lookup_snapshot(&rewritten).await?.manifests.len(), 1);
    assert!(
        repo.lookup_snapshot(&rewritten).await?.metadata.contains_key("splitting_config")
    );

    let plain = repo.reopen(Some(config_with_splitting(None)), None)?;
    assert_eq!(read_1d(&plain, &array, 10).await?, expected);
    Ok(())
}

#[tokio::test]
async fn gate_virtual_chunk_authorization() -> TestResult {
    let chunks = tempfile::TempDir::new()?;
    let chunk_file = chunks.path().join("chunk-1");
    std::fs::write(&chunk_file, b"hello virtual")?;
    let prefix = format!("file://{}", chunks.path().to_str().ok_or("bad temp path")?);

    let container = VirtualChunkContainer::new(
        prefix.clone(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    )?;
    let containers: HashMap<String, VirtualChunkContainer> =
        [(container.url_prefix().to_string(), container)].into_iter().collect();
    let config = RepositoryConfig {
        virtual_chunk_containers: Some(containers),
        ..Default::default()
    };

    let storage = mem().await?;
    let authorized: HashMap<String, Option<Credentials>> =
        [(prefix, None)].into_iter().collect();
    let repo = Repository::create(
        Some(config.clone()),
        Arc::clone(&storage),
        authorized.clone(),
    )
    .await?;

    let array = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(1, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path(&format!(
                    "file://{}",
                    chunk_file.to_str().ok_or("bad chunk path")?
                ))?,
                offset: 0,
                length: 13,
                checksum: None,
            })),
        )
        .await?;
    session.commit("one virtual chunk", None).await?;

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let got = get_chunk(
        session.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?
    .ok_or("the authorised virtual chunk did not resolve")?;
    assert_eq!(got, Bytes::from_static(b"hello virtual"));

    // The same repository, opened without authorising that prefix, refuses.
    let unauthorized =
        Repository::open(Some(config), Arc::clone(&storage), HashMap::new()).await?;
    let session = unauthorized
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    let res = get_chunk(
        session.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await;
    assert!(res.is_err(), "an unauthorised container must not resolve chunks");
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 01 — the object layout and the two kinds of reference.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_object_layout_and_references() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&root).await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("first", None).await?;

    assert!(root.join("refs").join("branch.main").join("ref.json").is_file());
    assert!(root.join("snapshots").is_dir());
    assert!(!root.join("attributes").exists());

    repo.create_branch("release", &snap).await?;
    repo.create_tag("v1", &snap).await?;
    assert_eq!(repo.lookup_tag("v1").await?, snap);
    assert_eq!(repo.lookup_branch("release").await?, snap);
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        vec!["main".to_string(), "release".to_string()],
    );

    // The first snapshot is the well-known one.
    assert_eq!(
        String::from(&Snapshot::INITIAL_SNAPSHOT_ID),
        "1CECHNKREP0F1RSTCMT0",
    );
    assert_eq!(
        repo.lookup_snapshot(&Snapshot::INITIAL_SNAPSHOT_ID).await?.message,
        Snapshot::INITIAL_COMMIT_MESSAGE,
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 02 — sessions, commit conflicts and rebase.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_sessions_conflicts_and_rebase() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut setup = repo.writable_session("main").await?;
    setup.add_group(Path::root(), Bytes::new()).await?;
    setup.commit("first", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.add_group(p("/a"), Bytes::new()).await?;
    b.add_group(p("/b"), Bytes::new()).await?;
    a.commit("a", None).await?;
    assert!(b.commit("b", None).await.is_err(), "a stale session cannot commit");

    b.rebase(&BasicConflictSolver::default()).await?;
    b.commit("b after rebase", None).await?;

    let reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(reader.get_group(&p("/a")).await.is_ok());
    assert!(reader.get_group(&p("/b")).await.is_ok());
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 03 — expiration and garbage collection.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_garbage_collection() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..config_with_splitting(None)
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/array");
    let settings = storage.default_settings();

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..4u32 {
        let payload = session.get_chunk_writer()(Bytes::from(format!("keep{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    let keep: SnapshotId = session.commit("keep", None).await?;

    let mut session = repo.writable_session("main").await?;
    for i in 0..4u32 {
        let payload = session.get_chunk_writer()(Bytes::from(format!("drop{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    session.commit("drop", None).await?;
    assert_eq!(storage.list_chunks(&settings).await?.count().await, 8);

    repo.reset_branch("main", &keep).await?;
    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert_eq!(summary.chunks_deleted, 4);
    assert_eq!(storage.list_chunks(&settings).await?.count().await, 4);

    let expected: Vec<Bytes> = (0..4).map(|i| Bytes::from(format!("keep{i}"))).collect();
    assert_eq!(read_1d(&repo, &array, 4).await?, expected);
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 04 — the FlatBuffers format behind its fixed header.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_flatbuffers_objects() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&root).await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let array = p("/temperature");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"0"))),
        )
        .await?;
    let snap = session.commit("first", None).await?;

    let magic = "ICE\u{1f9ca}CHUNK".as_bytes();
    for (prefix, file_type) in
        [("snapshots", 1u8), ("manifests", 2u8), ("transactions", 4u8)]
    {
        let mut seen = 0usize;
        for entry in std::fs::read_dir(root.join(prefix))? {
            let bytes = std::fs::read(entry?.path())?;
            assert!(bytes.len() > 39);
            assert_eq!(&bytes[0..12], magic);
            assert_eq!(bytes[36], 1u8, "the on-disk spec version is still 1");
            assert_eq!(bytes[37], file_type);
            seen += 1;
        }
        assert!(seen > 0, "{prefix}/ must hold at least one object");
    }

    // And the same objects decode through the public API.
    let info = repo.lookup_snapshot(&snap).await?;
    assert_eq!(info.message, "first");
    assert_eq!(info.manifests.len(), 1);
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 05 — the command-line interface.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_command_line_interface() -> TestResult {
    let bin = PathBuf::from(
        std::env::var("ICECHUNK_TEST_CLI_BIN")
            .expect("ICECHUNK_TEST_CLI_BIN is set by the verifier"),
    );
    let home = tempfile::TempDir::new()?;
    let repos = tempfile::TempDir::new()?;
    let repo_path = repos.path().join("local1");
    std::fs::create_dir_all(&repo_path)?;

    let config_dir = home.path().join("icechunk");
    std::fs::create_dir_all(&config_dir)?;
    std::fs::write(
        config_dir.join("cli-config.yaml"),
        format!(
            "repos:\n  local1: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_path.display()
        ),
    )?;

    let run = |args: &[&str]| {
        std::process::Command::new(&bin)
            .args(args)
            .env("XDG_CONFIG_HOME", home.path())
            .env("HOME", home.path())
            .output()
            .expect("the icechunk binary must be executable")
    };

    let out = run(&["repo", "create", "local1"]);
    assert!(
        out.status.success(),
        "repo create failed: {}",
        String::from_utf8_lossy(&out.stderr),
    );
    assert!(repo_path.join("refs").join("branch.main").join("ref.json").is_file());

    let out = run(&["snapshot", "list", "local1"]);
    assert!(
        out.status.success(),
        "snapshot list failed: {}",
        String::from_utf8_lossy(&out.stderr),
    );
    let text = String::from_utf8(out.stdout)?;
    assert_eq!(text.lines().count(), 1);
    assert!(text.contains(Snapshot::INITIAL_COMMIT_MESSAGE));

    // An alias the configuration does not name is an error, not a silent pass.
    assert!(!run(&["snapshot", "list", "nosuch"]).status.success());
    Ok(())
}
