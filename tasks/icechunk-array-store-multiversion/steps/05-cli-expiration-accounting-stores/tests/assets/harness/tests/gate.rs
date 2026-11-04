// Verifier-owned end-to-end driver for milestone 05 (the command-line
// interface, expiration accounting and object-store configuration).
//
// One path through this milestone's headline capability at proof-of-life
// depth - including one invocation of the compiled `icechunk` binary - plus
// one end-to-end path through each of the four milestones this step inherits.
// The harder variants, corners and negative cases are scored by
// tests/conformance.rs, which still runs when this group fails.
//
// Nothing here reaches the network or reads a cloud credential.

#![allow(non_snake_case)]

use std::collections::{BTreeSet, HashMap};
use std::path::{Path as StdPath, PathBuf};
use std::sync::Arc;

use bytes::Bytes;
use chrono::{TimeDelta, Utc};
use futures::TryStreamExt;
use tokio::sync::RwLock;

use icechunk::{
    Repository, RepositoryConfig, Storage, Store,
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        snapshot::{ArrayShape, SnapshotInfo, SnapshotProperties},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{ExpireRefResult, ExpiredRefAction, GCConfig, expire, expire_ref, garbage_collect},
    ops::stats::repo_chunks_storage,
    refs::Ref,
    repository::VersionInfo,
    session::{Session, get_chunk},
    conflicts::basic_solver::BasicConflictSolver,
};

type GateResult = Result<(), Box<dyn std::error::Error>>;
type BoxErr = Box<dyn std::error::Error>;

const GROUP_META: &[u8] = br#"{"zarr_format":3,"node_type":"group"}"#;
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"]}"#;

async fn mem() -> Result<Arc<dyn Storage + Send + Sync>, BoxErr> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(storage)
}

async fn local(p: &StdPath) -> Result<Arc<dyn Storage + Send + Sync>, BoxErr> {
    let storage: Arc<dyn Storage + Send + Sync> = new_local_filesystem_storage(p).await?;
    Ok(storage)
}

fn path(p: &str) -> Path {
    Path::try_from(p).expect("a valid node path")
}

fn array_shape(len: u64) -> ArrayShape {
    ArrayShape::new(vec![(len, 1)]).expect("a one-dimensional array shape")
}

async fn native_repo(
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

async fn write_chunk(
    session: &mut Session,
    array: &Path,
    index: u32,
    bytes: &[u8],
) -> GateResult {
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

async fn ancestry(
    repo: &Repository,
    version: &VersionInfo,
) -> Result<Vec<SnapshotInfo>, BoxErr> {
    Ok(repo.ancestry(version).await?.try_collect::<Vec<_>>().await?)
}

// ---------------------------------------------------------------------------
// This milestone's headline capability
// ---------------------------------------------------------------------------

/// Configure a repository, create it through the `icechunk` binary, commit
/// through the library, and list the result back through the binary.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__command_line_creates_and_lists_a_repository() -> GateResult {
    let binary = PathBuf::from(std::env::var("ICECHUNK_TEST_CLI_BIN").expect(
        "ICECHUNK_TEST_CLI_BIN must name the candidate's `icechunk` binary",
    ));
    let tmp = tempfile::TempDir::new()?;
    let config_home = tmp.path().join("config-home");
    let repo_dir = tmp.path().join("repo");
    std::fs::create_dir_all(config_home.join("icechunk"))?;
    std::fs::create_dir_all(&repo_dir)?;
    std::fs::write(
        config_home.join("icechunk").join("cli-config.yaml"),
        format!(
            "repos:\n  r1: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_dir.display()
        ),
    )?;

    let run = |args: &[&str]| -> Result<(i32, String), BoxErr> {
        let out = std::process::Command::new(&binary)
            .env("XDG_CONFIG_HOME", &config_home)
            .args(args)
            .output()?;
        Ok((
            out.status.code().unwrap_or(-1),
            String::from_utf8_lossy(&out.stdout).into_owned(),
        ))
    };

    let (code, stdout) = run(&["--help"])?;
    assert_eq!(code, 0, "`icechunk --help` exits 0");
    assert!(stdout.contains("repo") && stdout.contains("snapshot"));

    let (code, stdout) = run(&["config", "list"])?;
    assert_eq!(code, 0);
    assert!(stdout.contains("r1"), "the configured alias is listed: {stdout}");

    let (code, _stdout) = run(&["repo", "create", "r1"])?;
    assert_eq!(code, 0, "`icechunk repo create r1` exits 0");
    assert!(repo_dir.join("refs").is_dir());

    // Add history through the library, then read it back through the binary.
    let storage = local(&repo_dir).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("gate commit", None).await?;

    let (code, stdout) = run(&["snapshot", "list", "r1"])?;
    assert_eq!(code, 0);
    let lines: Vec<&str> = stdout.lines().filter(|l| !l.trim().is_empty()).collect();
    assert_eq!(lines.len(), 2, "one commit plus the initial snapshot: {lines:?}");
    assert!(lines[0].starts_with("SnapshotInfo"));
    assert!(lines[0].contains("manifests"), "manifest information is reported");
    assert!(lines[0].contains("gate commit"), "newest first: {lines:?}");

    let (code, _stdout) = run(&["repo", "create", "r1"])?;
    assert_eq!(code, 1, "creating the same repository twice is a runtime error");
    Ok(())
}

/// Look a snapshot up, measure the repository's committed chunk storage,
/// expire the history, collect the released objects and read the byte
/// accounting back.
#[tokio::test(flavor = "multi_thread")]
async fn gate02__expiration_and_accounting_end_to_end() -> GateResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let mut repo = native_repo(&storage).await?;
    let array = path("/array");

    let mut defaults = SnapshotProperties::default();
    defaults.insert("author".to_string(), serde_json::Value::String("gate".into()));
    repo.set_default_commit_metadata(defaults.clone());

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[1u8; 64]).await?;
    let first = session.commit("first", None).await?;

    for i in 1..3u32 {
        let mut session = repo.writable_session("main").await?;
        write_chunk(&mut session, &array, i, &[i as u8; 64]).await?;
        session.commit(&format!("commit {i}"), None).await?;
    }

    // Repository-level metadata reached every commit, and lookup_snapshot
    // reports the snapshot record including its manifests.
    let info = repo.lookup_snapshot(&first).await?;
    assert_eq!(info.metadata, defaults);
    assert_eq!(info.manifests.len(), 1);
    assert!(info.manifests[0].size_bytes > 0);

    // Three 64-byte native chunks are committed.
    assert_eq!(
        repo_chunks_storage(
            storage.as_ref(),
            &settings,
            repo.asset_manager().clone()
        )
        .await?,
        3 * 64
    );

    // Expire everything: the history collapses onto the root.
    let result = expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        Utc::now() + TimeDelta::days(1),
    )
    .await?;
    assert!(matches!(result, ExpireRefResult::Done { ref_is_expired: true, .. }));
    let history = ancestry(&repo, &VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(history.len(), 2, "tip and root only: {history:?}");

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert!(summary.snapshots_deleted > 0, "the expired snapshots were collected");
    assert!(summary.bytes_deleted > 0, "the collection reports the bytes it freed");

    // The tip's data is untouched and the repository is still usable.
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(read_chunk(&session, &array, 2).await?.as_deref(), Some(&[2u8; 64][..]));
    Ok(())
}

// ---------------------------------------------------------------------------
// One path through each inherited milestone
// ---------------------------------------------------------------------------

/// Milestone 01 - the repository format: initialise, write metadata and a
/// chunk through the Zarr store, commit, tag, and read everything back from a
/// fresh handle on a local-filesystem backend.
#[tokio::test(flavor = "multi_thread")]
async fn gate03__milestone01_repository_and_store() -> GateResult {
    let tmp = tempfile::TempDir::new()?;
    let snapshot = {
        let storage = local(tmp.path()).await?;
        let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
        let session = repo.writable_session("main").await?;
        let store = Store::from_session(Arc::new(RwLock::new(session))).await;

        store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
        store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
        store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;
        assert_eq!(
            store.get("array/c/0/0/0", &ByteRange::ALL).await?,
            Bytes::copy_from_slice(b"hello")
        );

        let session = store.session();
        let id = session.write().await.commit("store commit", None).await?;
        repo.create_tag("v1", &id).await?;
        assert_eq!(repo.lookup_branch("main").await?, id);
        id
    };

    let storage = local(tmp.path()).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.lookup_tag("v1").await?, snapshot);
    assert_eq!(repo.list_branches().await?, BTreeSet::from(["main".to_string()]));

    let session = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
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

/// Milestone 02 - sessions and conflicts: two writers race on one branch, the
/// loser rebases with the basic solver and lands its change.
#[tokio::test(flavor = "multi_thread")]
async fn gate04__milestone02_sessions_and_rebase() -> GateResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.commit("base", None).await?;

    let mut winner = repo.writable_session("main").await?;
    let mut loser = repo.writable_session("main").await?;
    winner.add_group(path("/winner"), Bytes::new()).await?;
    loser.add_group(path("/loser"), Bytes::new()).await?;

    winner.commit("winner", None).await?;
    assert!(loser.commit("loser", None).await.is_err(), "exactly one writer wins");

    loser.rebase(&BasicConflictSolver::default()).await?;
    loser.commit("loser", None).await?;

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(session.get_group(&path("/winner")).await.is_ok());
    assert!(session.get_group(&path("/loser")).await.is_ok());

    let reader = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    assert!(reader.read_only());
    Ok(())
}

/// Milestone 03 - expiration and garbage collection: a stranded commit is
/// released by expiration and then deleted, while the reachable data stays.
#[tokio::test(flavor = "multi_thread")]
async fn gate05__milestone03_expiration_and_collection() -> GateResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    let repo = native_repo(&storage).await?;
    let array = path("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[7u8; 64]).await?;
    let keep = session.commit("keep", None).await?;

    let mut session = repo.writable_session("main").await?;
    write_chunk(&mut session, &array, 1, &[9u8; 64]).await?;
    session.commit("strand", None).await?;
    repo.reset_branch("main", &keep).await?;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        Utc::now() - TimeDelta::days(1),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    assert!(result.deleted_refs.is_empty(), "nothing expired, nothing deleted");

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert_eq!(summary.chunks_deleted, 1, "only the stranded chunk goes");
    assert_eq!(summary.snapshots_deleted, 1);

    let session = repo.readonly_session(&VersionInfo::SnapshotId(keep)).await?;
    assert_eq!(read_chunk(&session, &array, 0).await?.as_deref(), Some(&[7u8; 64][..]));
    Ok(())
}

/// Milestone 04 - the FlatBuffers format: a committed snapshot and its
/// transaction log decode field by field, and a session marshals and revives.
#[tokio::test(flavor = "multi_thread")]
async fn gate06__milestone04_format_objects() -> GateResult {
    let storage = mem().await?;
    let repo = native_repo(&storage).await?;
    let array = path("/array");

    let parent = repo.lookup_branch("main").await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(array.clone(), array_shape(4), None, Bytes::new()).await?;
    write_chunk(&mut session, &array, 0, &[4u8; 64]).await?;

    let revived = Session::from_bytes(session.as_bytes()?)?;
    assert!(revived.has_uncommitted_changes(), "the change set survives marshalling");

    let id: SnapshotId = session.commit("format", None).await?;

    let snapshot = repo.asset_manager().fetch_snapshot(&id).await?;
    assert_eq!(snapshot.id(), id);
    assert_eq!(snapshot.parent_id(), Some(parent));
    assert_eq!(snapshot.message(), "format");
    assert_eq!(snapshot.manifest_files().count(), 1);
    assert!(snapshot.get_node(&array).is_ok());

    let log = repo.asset_manager().fetch_transaction_log(&id).await?;
    assert_eq!(log.new_arrays().count(), 1);
    assert_eq!(log.new_groups().count(), 1);
    Ok(())
}
