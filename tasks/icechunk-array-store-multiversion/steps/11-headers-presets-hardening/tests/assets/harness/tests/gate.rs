// Verifier-owned end-to-end gate for milestone 11 — "headers, presets and
// hardening" (earth-mover/icechunk v2.2.0).
//
// One scored group, `gate-endtoend`: this milestone's headline capabilities at
// proof-of-life depth, plus one end-to-end path through each milestone the step
// inherits. It compiles and runs SEPARATELY from `conformance.rs`, so neither
// driver can take the other down.
//
// Everything traces to /app/TEST_INTERFACE.md and /app/icechunk-spec/. Nothing
// reaches the network or a cloud credential: storage is in-memory or a
// local-filesystem `tempfile::TempDir`, and the object-store presets are only
// constructed and read back through `Storage::storage_info()`.

#![allow(clippy::expect_used, clippy::unwrap_used, clippy::panic)]

use std::{
    collections::HashMap,
    num::{NonZeroU16, NonZeroUsize},
    path::{Path as FsPath, PathBuf},
    process::Command,
    sync::Arc,
};

use bytes::Bytes;
use futures::TryStreamExt as _;
use serde_json::Value;
use tokio::sync::RwLock;

use icechunk::{
    Repository, Storage, Store,
    config::{ManifestConfig, RepositoryConfig, S3Credentials, S3Options},
    format::{
        ByteRange, ChunkId, ChunkIndices, ManifestId, Path, SnapshotId,
        format_constants::{self, CompressionAlgorithmBin, FileTypeBin, SpecVersionBin},
        manifest::{ChunkPayload, ChunkRef, VirtualChunkLocation, VirtualChunkRef},
        repo_info::UpdateType,
        snapshot::{ArrayShape, Snapshot},
    },
    inspect::{manifest_json, repo_info_json, snapshot_json, transaction_log_json},
    new_in_memory_storage,
    ops::{
        gc::{ExpiredRefAction, GCConfig, expire, garbage_collect},
        stats::repo_chunks_storage,
    },
    repository::VersionInfo,
    session::{SessionError, SessionErrorKind},
    // `new_hf_storage` is NEW at this release and is imported inside the one
    // case that needs it, keeping this file-level prelude resolvable against
    // the previous checkpoint's workspace (see conformance.rs for why).
    storage::new_local_filesystem_storage,
    store::{StoreError, StoreErrorKind},
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

const GROUP_META: &[u8] = br#"{"zarr_format":3, "node_type":"group"}"#;
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"bytes","configuration":{"endian":"little"}}]}"#;

async fn mem_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(Repository::create(None, storage, HashMap::new(), Some(SpecVersionBin::V2), true)
        .await?)
}

/// A committed repository holding a root group and one `/array` with a single
/// inline chunk, so a snapshot, a manifest and a transaction log all exist.
async fn fixture() -> Result<(Repository, SnapshotId), Box<dyn std::error::Error>> {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(4, 2)]).expect("a shape"),
            None,
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            apath,
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"abcd"))),
        )
        .await?;
    let snap = session.commit("first").execute().await?;
    Ok((repo, snap))
}

async fn commit_group(
    repo: &Repository,
    branch: &str,
    path: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session(branch).await?;
    let path = if path == "/" { Path::root() } else { Path::try_from(path)? };
    session.add_group(path, Bytes::new()).await?;
    Ok(session.commit(branch).execute().await?)
}

fn header_of(json: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let v: Value = serde_json::from_str(json)?;
    Ok(v.get("header").ok_or("the inspect document has no `header` member")?.clone())
}

/// Assert one `inspect_*` header: the writer identity, the spec version, the
/// expected file type and a compression the format defines.
fn assert_header(h: &Value, file_type: &str) {
    assert_eq!(h["app_name"], "ic");
    assert_eq!(h["app_version"], format_constants::ICECHUNK_LIB_VERSION);
    assert_eq!(h["written_by"], format_constants::ICECHUNK_CLIENT_NAME.as_str());
    assert_eq!(h["spec_version"], SpecVersionBin::V2.to_string());
    assert_eq!(h["file_type"], file_type);
    assert_eq!(h["compression"], format_constants::ICECHUNK_COMPRESSION_ZSTD);
}

fn info_fields(storage: &(dyn Storage + Send + Sync)) -> HashMap<String, String> {
    storage.storage_info().fields.into_iter().map(|(k, v)| (k.to_string(), v)).collect()
}

// ---------------------------------------------------------------------------
// Command-line helper: the candidate's own `icechunk` binary, built by the
// runner with `--features cli --bin icechunk` and exported as
// ICECHUNK_TEST_CLI_BIN, driven against a local-filesystem repository.
// ---------------------------------------------------------------------------

struct Cli {
    dir: tempfile::TempDir,
}

impl Cli {
    fn new() -> Result<Self, Box<dyn std::error::Error>> {
        let dir = tempfile::tempdir()?;
        std::fs::create_dir_all(dir.path().join("xdg/icechunk"))?;
        std::fs::create_dir_all(dir.path().join("repo"))?;
        let cli = Cli { dir };
        std::fs::write(
            cli.dir.path().join("xdg/icechunk/cli-config.yaml"),
            // `RepositoryDefinition` is an externally tagged enum, and
            // serde_yaml_ng renders that as a YAML tag: `!LocalFileSystem`.
            format!(
                "repos:\n  demo: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
                cli.repo_path().display()
            ),
        )?;
        Ok(cli)
    }

    fn repo_path(&self) -> PathBuf {
        self.dir.path().join("repo")
    }

    fn run(&self, args: &[&str]) -> Result<(i32, String, String), Box<dyn std::error::Error>> {
        let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
            .map_err(|_| "ICECHUNK_TEST_CLI_BIN is not set")?;
        assert!(FsPath::new(&bin).is_file(), "no icechunk binary at {bin}");
        let out = Command::new(&bin)
            .args(args)
            .env("XDG_CONFIG_HOME", self.dir.path().join("xdg"))
            .env("HOME", self.dir.path())
            .output()?;
        Ok((
            out.status.code().unwrap_or(-1),
            String::from_utf8_lossy(&out.stdout).into_owned(),
            String::from_utf8_lossy(&out.stderr).into_owned(),
        ))
    }

    fn ok(&self, args: &[&str]) -> Result<String, Box<dyn std::error::Error>> {
        let (code, stdout, stderr) = self.run(args)?;
        assert_eq!(code, 0, "`icechunk {}` failed\n{stdout}\n{stderr}", args.join(" "));
        Ok(stdout)
    }
}

// ===========================================================================
// This milestone's headline capabilities, at proof-of-life depth
// ===========================================================================

#[tokio::test]
async fn gate_every_inspect_entry_point_reports_the_file_header() -> TestResult {
    let (repo, snap) = fixture().await?;
    let am = repo.asset_manager();

    assert_header(&header_of(&snapshot_json(am, &snap, true).await?)?, "snapshot");
    assert_header(&header_of(&repo_info_json(am, true).await?)?, "repo-info");
    assert_header(
        &header_of(&transaction_log_json(am, &snap, true).await?)?,
        "transaction-log",
    );

    let snap_doc: Value = serde_json::from_str(&snapshot_json(am, &snap, false).await?)?;
    let mid = ManifestId::try_from(
        snap_doc["manifests"][0]["id"].as_str().ok_or("no manifest in the snapshot")?,
    )?;
    assert_header(&header_of(&manifest_json(am, &mid, true).await?)?, "manifest");

    // The reported header describes the file that was read.
    let file = am.fetch_snapshot_header(&snap).await?;
    assert_eq!(file.file_type, FileTypeBin::Snapshot);
    assert_eq!(file.spec_version, SpecVersionBin::V2);
    assert_eq!(file.compression, CompressionAlgorithmBin::Zstd);
    Ok(())
}

#[tokio::test]
async fn gate_cli_creates_a_repository_and_manages_branches_tags_and_ancestry()
-> TestResult {
    let cli = Cli::new()?;
    cli.ok(&["repo", "create", "demo"])?;

    let branches = cli.ok(&["branch", "list", "demo"])?;
    let first: Vec<&str> = branches.lines().filter(|l| !l.trim().is_empty()).collect();
    assert_eq!(first.len(), 1);
    assert!(first[0].ends_with("main"), "got {:?}", first[0]);
    let root = first[0].split_whitespace().next().expect("a snapshot id").to_string();
    assert_eq!(root.len(), 20);

    cli.ok(&["branch", "create", "demo", "feature", "main"])?;
    assert_eq!(cli.ok(&["branch", "list", "demo"])?.lines().count(), 2);
    cli.ok(&["branch", "delete", "demo", "feature"])?;

    cli.ok(&["tag", "create", "demo", "v1", "main"])?;
    let tags = cli.ok(&["tag", "list", "demo"])?;
    assert!(tags.contains("v1") && tags.contains(&root));
    cli.ok(&["tag", "delete", "demo", "v1"])?;
    assert!(cli.ok(&["tag", "list", "demo"])?.trim().is_empty());

    let ancestry = cli.ok(&["ancestry", "demo"])?;
    assert_eq!(ancestry.matches("Snapshot: ").count(), 1);
    assert!(ancestry.contains("Date: "));
    assert!(!ancestry.trim_start().starts_with('{'), "the CLI never emits JSON");

    // `snapshot list` was removed: clap reports a usage error.
    let (code, stdout, _) = cli.run(&["snapshot", "list", "demo"])?;
    assert_eq!(code, 2);
    assert!(stdout.is_empty());
    // A reference that resolves to nothing is a runtime failure, not a usage one.
    let (code, _, stderr) = cli.run(&["ancestry", "demo", "nope"])?;
    assert_eq!(code, 1);
    assert!(stderr.contains("nope"));
    Ok(())
}

#[tokio::test]
async fn gate_impossible_chunk_deletes_are_noops_and_zero_length_writes_are_rejected()
-> TestResult {
    let repo = mem_repo().await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let data = Bytes::from_static(b"hello");
    store.set("array/c/1/1/1", data.clone()).await?;

    // Three keys that cannot exist: outside the grid, a missing node, a group.
    store.delete("array/c/10/1/1").await?;
    store.delete("no/such/node/c/0/0/0").await?;
    store.delete("c/0").await?;
    assert_eq!(store.get("array/c/1/1/1", &ByteRange::ALL).await?, data);

    // Writing one of them is still an error.
    let err = store
        .set("array/c/10/1/1", data.clone())
        .await
        .expect_err("a write outside the grid must be rejected");
    assert!(
        matches!(
            err,
            StoreError {
                kind: StoreErrorKind::SessionError(SessionErrorKind::InvalidIndex { .. }),
                ..
            }
        ),
        "got {err:?}"
    );

    // A zero-length payload is rejected for all three reference kinds.
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array1".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(5, 3), (5, 3)]).expect("a shape"),
            None,
            Bytes::new(),
        )
        .await?;
    let coords = ChunkIndices(vec![0, 0]);
    for payload in [
        ChunkPayload::Inline(Bytes::new()),
        ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_url("s3://bucket/foo.nc")?,
            offset: 0,
            length: 0,
            checksum: None,
        }),
        ChunkPayload::Ref(ChunkRef { id: ChunkId::random(), offset: 0, length: 0 }),
    ] {
        assert_eq!(payload.length(), 0);
        let res = session.set_chunk_ref(apath.clone(), coords.clone(), Some(payload)).await;
        assert!(
            matches!(
                res,
                Err(SessionError {
                    kind: SessionErrorKind::ZeroLengthChunk { .. },
                    ..
                })
            ),
            "got {res:?}"
        );
    }
    // Deleting is how "not stored at all" is recorded, and it is accepted.
    session.set_chunk_ref(apath.clone(), coords.clone(), None).await?;
    Ok(())
}

#[tokio::test]
async fn gate_hugging_face_preset_and_portable_metadata_keys() -> TestResult {
    use icechunk::storage::new_hf_storage;
    let storage = new_hf_storage(
        S3Options::default(),
        "my-bucket".to_string(),
        Some("some/prefix".to_string()),
        "my-namespace",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(storage.as_ref());
    assert_eq!(f["bucket"], "my-bucket");
    assert_eq!(f["endpoint_url"], "https://s3.hf.co/my-namespace");
    assert_eq!(f["region"], "us-east-1");
    assert_eq!(f["force_path_style"], "True");
    assert_eq!(storage.storage_info().backend_type, "S3 (native)");

    use format_constants::{
        ICECHUNK_CLIENT_NAME_METADATA_KEY, ICECHUNK_CLIENT_NAME_METADATA_KEY_DEPRECATED,
        ICECHUNK_COMPRESSION_METADATA_KEY, ICECHUNK_COMPRESSION_METADATA_KEY_DEPRECATED,
        ICECHUNK_FILE_TYPE_METADATA_KEY, ICECHUNK_FILE_TYPE_METADATA_KEY_DEPRECATED,
        LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY,
        LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY_DEPRECATED,
    };
    assert_eq!(LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY, "icspecver");
    assert_eq!(ICECHUNK_CLIENT_NAME_METADATA_KEY, "icclient");
    assert_eq!(ICECHUNK_FILE_TYPE_METADATA_KEY, "icfiletype");
    assert_eq!(ICECHUNK_COMPRESSION_METADATA_KEY, "iccompalg");
    assert_eq!(LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY_DEPRECATED, "ic_spec_ver");
    assert_eq!(ICECHUNK_CLIENT_NAME_METADATA_KEY_DEPRECATED, "ic_client");
    assert_eq!(ICECHUNK_FILE_TYPE_METADATA_KEY_DEPRECATED, "ic_file_type");
    assert_eq!(ICECHUNK_COMPRESSION_METADATA_KEY_DEPRECATED, "ic_comp_alg");
    Ok(())
}

// ===========================================================================
// One end-to-end path through each inherited milestone
// ===========================================================================

#[tokio::test]
async fn gate_inherited_repository_sessions_and_time_travel() -> TestResult {
    // Milestones 01 and 02: a versioned repository, the Zarr-facing store, and
    // the session/rebase split.
    let repo = mem_repo().await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::from_static(b"first")).await?;
    let first = {
        let session = store.session();
        let mut guard = session.write().await;
        guard.commit("first").execute().await?
    };
    repo.create_tag("v1", &first).await?;

    let second = commit_group(&repo, "main", "/later").await?;
    assert_eq!(repo.lookup_branch("main").await?, second);
    assert_eq!(repo.lookup_tag("v1").await?, first);

    let ancestry = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect::<Vec<_>>()
        .await?;
    assert_eq!(ancestry.first().map(|s| s.id.clone()), Some(second));
    assert_eq!(ancestry.len(), 3);

    let old = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert!(old.get_node(&"/later".try_into()?).await.is_err());
    let old_store = Store::from_session(Arc::new(RwLock::new(old))).await;
    assert_eq!(
        old_store.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::from_static(b"first")
    );

    // Two sessions over the same branch: the stale one must conflict.
    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.add_group("/a".try_into()?, Bytes::new()).await?;
    b.add_group("/b".try_into()?, Bytes::new()).await?;
    a.commit("a").execute().await?;
    let conflict = b.commit("b").execute().await;
    assert!(
        matches!(conflict, Err(SessionError { kind: SessionErrorKind::Conflict { .. }, .. })),
        "got {conflict:?}"
    );
    b.rebase(&icechunk::conflicts::basic_solver::BasicConflictSolver::default()).await?;
    b.commit("b, rebased").execute().await?;
    Ok(())
}

#[tokio::test]
async fn gate_inherited_expiration_gc_and_the_pruned_transaction_log_ancestry()
-> TestResult {
    // Milestones 03 and 10.
    let repo = mem_repo().await?;
    let a = commit_group(&repo, "main", "/a").await?;
    repo.create_branch("feat", &a).await?;
    let b = commit_group(&repo, "feat", "/b").await?;
    let c = commit_group(&repo, "feat", "/c").await?;
    let am = Arc::clone(repo.asset_manager());

    let result = expire(
        Arc::clone(&am),
        chrono::Utc::now() + chrono::Duration::days(1),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert_eq!(result.released_snapshots.len(), 1);
    assert!(result.edited_snapshots.contains(&c));

    let (info, _) = am.fetch_repo_info().await?;
    assert_eq!(info.find_snapshot(&c)?.pruned_ancestor_tx_logs, vec![a, b]);

    let cutoff = chrono::Utc::now() + chrono::Duration::days(1);
    let summary = garbage_collect(
        Arc::clone(&am),
        &GCConfig::clean_all(
            cutoff,
            cutoff,
            None,
            NonZeroU16::new(50).expect("nonzero"),
            NonZeroUsize::new(512 * 1024 * 1024).expect("nonzero"),
            NonZeroU16::new(500).expect("nonzero"),
            false,
        ),
        None,
        100,
    )
    .await?;
    assert!(summary.snapshots_deleted >= 1);
    assert_eq!(repo.lookup_branch("feat").await?, c);
    Ok(())
}

#[tokio::test]
async fn gate_inherited_metadata_format_and_manifest_splitting() -> TestResult {
    // Milestones 04 and 06.
    let (repo, snap) = fixture().await?;
    let snapshot = repo.asset_manager().fetch_snapshot(&snap).await?;
    let bytes = snapshot.bytes().to_vec();
    let parsed = Snapshot::from_buffer(SpecVersionBin::V2, bytes.clone())?;
    assert_eq!(parsed.id(), snap);
    assert!(
        Snapshot::from_buffer(SpecVersionBin::V2, bytes[..bytes.len() / 3].to_vec())
            .is_err(),
        "a truncated buffer is rejected before any access"
    );

    let mut config = RepositoryConfig::default();
    config.manifest = Some(ManifestConfig {
        splitting: Some(icechunk::config::ManifestSplittingConfig {
            split_sizes: Some(vec![(
                icechunk::config::ManifestSplitCondition::AnyArray,
                vec![icechunk::config::ManifestSplitDim {
                    condition: icechunk::config::ManifestSplitDimCondition::Any,
                    num_chunks: 2,
                }],
            )]),
        }),
        ..ManifestConfig::default()
    });
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let split_repo =
        Repository::create(Some(config), storage, HashMap::new(), None, true).await?;
    let mut session = split_repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(8, 8)]).expect("a shape"),
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..8u32 {
        session
            .set_chunk_ref(
                apath.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(Bytes::from_static(b"x"))),
            )
            .await?;
    }
    let split_snap = session.commit("split").execute().await?;
    assert!(
        split_repo.lookup_manifest_files(&split_snap).await?.count() > 1,
        "the configured split size must produce several manifests"
    );
    Ok(())
}

#[tokio::test]
async fn gate_inherited_table_of_contents_cli_config_and_storage_accounting()
-> TestResult {
    // Milestones 05, 08 and 09.
    let cli = Cli::new()?;
    cli.ok(&["repo", "create", "demo"])?;
    let listed = cli.ok(&["config", "list"])?;
    assert!(listed.contains("repos:") && listed.contains("LocalFileSystem"));

    let storage = new_local_filesystem_storage(&cli.repo_path()).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let (stream, _info, _version) = repo.ops_log().await?;
    let entries = stream.try_collect::<Vec<_>>().await?;
    assert!(
        entries.iter().any(|(_, u, _)| matches!(u, UpdateType::RepoInitializedUpdate)),
        "the table of contents records how the repository was created"
    );

    let mut config = RepositoryConfig::default();
    config.inline_chunk_threshold_bytes = Some(0);
    let mem: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo = Repository::create(Some(config), mem, HashMap::new(), None, true).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(4, 4)]).expect("a shape"),
            None,
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"1234"))),
        )
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Ref(ChunkRef {
                id: ChunkId::random(),
                offset: 0,
                length: 100,
            })),
        )
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![2]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_url("s3://bucket/f.nc")?,
                offset: 0,
                length: 1000,
                checksum: None,
            })),
        )
        .await?;
    session.commit("three kinds").execute().await?;

    let stats = repo_chunks_storage(
        Arc::clone(repo.asset_manager()),
        NonZeroU16::new(50).expect("nonzero"),
        NonZeroUsize::new(512 * 1024 * 1024).expect("nonzero"),
        NonZeroU16::new(500).expect("nonzero"),
    )
    .await?;
    assert_eq!(stats.inlined_bytes, 4);
    assert_eq!(stats.native_bytes, 100);
    assert_eq!(stats.virtual_bytes, 1000);
    Ok(())
}
