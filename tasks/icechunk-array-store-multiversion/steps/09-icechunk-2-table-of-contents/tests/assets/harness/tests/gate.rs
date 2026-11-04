// Verifier-owned end-to-end driver for milestone 09 — the `gate-endtoend`
// group.
//
// This is this milestone's headline capability at proof-of-life depth, plus one
// end-to-end path through each milestone the step inherits. It is one ordinary
// scored group among the others: it multiplies nothing and stops nothing.
//
// It is compiled as its own target, so a conformance driver that does not
// typecheck cannot take it down with it, and vice versa.
//
// Nothing here reaches the network, reads a cloud credential or starts a
// daemon.

use std::{collections::HashMap, path::Path as StdPath, sync::Arc};

use bytes::Bytes;
use chrono::Utc;
use futures::{StreamExt as _, TryStreamExt as _};
use tokio::sync::RwLock;

use icechunk::{
    Repository, RepositoryConfig, Storage, Store,
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        format_constants::SpecVersionBin,
        manifest::ChunkPayload,
        repo_info::{RepoAvailability, RepoStatus, UpdateType},
        snapshot::{ArrayShape, Snapshot, SnapshotProperties},
    },
    migrations::migrate_1_to_2,
    new_in_memory_storage, new_local_filesystem_storage,
    repository::{RepositoryErrorKind, VersionInfo},
    session::{Session, SessionErrorKind, get_chunk},
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

/// The verifier-owned, checksum-verified spec-version-1 repository fixture.
const V1_FIXTURE: &str = "/tests/assets/fixtures/test-repo-v1";

fn p(path: &str) -> Path {
    Path::new(path).expect("canonical absolute path")
}

fn tip(branch: &str) -> VersionInfo {
    VersionInfo::BranchTipRef(branch.to_string())
}

async fn mem_repo() -> Repository {
    let storage: Arc<dyn Storage + Send + Sync> =
        new_in_memory_storage().await.expect("in-memory storage");
    Repository::create(None, storage, HashMap::new(), None, true)
        .await
        .expect("create repository")
}

/// A root group plus a one-dimensional array of `n` inline chunks, committed.
async fn seed(
    repo: &Repository,
    path: &Path,
    n: u32,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path.clone(),
            ArrayShape::new(vec![(u64::from(n) * 2, n)]).ok_or("shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..n {
        session
            .set_chunk_ref(
                path.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(i.to_string().into())),
            )
            .await?;
    }
    Ok(session.commit("seed").max_concurrent_nodes(4).execute().await?)
}

async fn chunk_bytes(
    session: &Session,
    path: &Path,
    coord: u32,
) -> Result<Option<Bytes>, Box<dyn std::error::Error>> {
    let reader = session
        .get_chunk_reader(path, &ChunkIndices(vec![coord]), &ByteRange::ALL)
        .await?;
    Ok(get_chunk(reader).await?)
}

async fn messages(
    repo: &Repository,
    version: &VersionInfo,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    Ok(repo
        .ancestry(version)
        .await?
        .map_ok(|info| info.message)
        .try_collect::<Vec<_>>()
        .await?)
}

async fn op_kinds(repo: &Repository) -> Result<Vec<UpdateType>, Box<dyn std::error::Error>> {
    let (stream, _, _) = repo.ops_log().await?;
    Ok(stream.map_ok(|(_, kind, _)| kind).try_collect::<Vec<_>>().await?)
}

fn copy_tree(src: &StdPath, dst: &StdPath) -> std::io::Result<()> {
    std::fs::create_dir_all(dst)?;
    for entry in std::fs::read_dir(src)? {
        let entry = entry?;
        let to = dst.join(entry.file_name());
        if entry.file_type()?.is_dir() {
            copy_tree(&entry.path(), &to)?;
        } else {
            std::fs::copy(entry.path(), &to)?;
        }
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// This milestone's headline capability.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_creates_a_second_generation_repository() -> TestResult {
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);

    seed(&repo, &p("/array"), 2).await?;

    // the second-generation layout: one mutable `repo` object, no refs prefix,
    // no configuration file
    assert!(dir.path().join("repo").is_file());
    assert!(!dir.path().join("refs").exists());
    assert!(!dir.path().join("config.yaml").exists());
    assert!(dir.path().join("snapshots").is_dir());
    assert!(dir.path().join("transactions").is_dir());

    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(reopened.spec_version(), SpecVersionBin::V2);
    assert_eq!(messages(&reopened, &tip("main")).await?.len(), 2);
    Ok(())
}

#[tokio::test]
async fn gate_table_of_contents_and_operations_log() -> TestResult {
    let repo = mem_repo().await;
    let snap = seed(&repo, &p("/array"), 2).await?;
    repo.create_branch("dev", &snap).await?;
    repo.create_tag("v1", &snap).await?;
    repo.delete_branch("dev").await?;

    let (toc, version) = repo.asset_manager().fetch_repo_info().await?;
    assert_eq!(toc.spec_version()?, SpecVersionBin::V2);
    assert_eq!(toc.branch_names()?.collect::<Vec<_>>(), vec!["main"]);
    assert_eq!(toc.tag_names()?.collect::<Vec<_>>(), vec!["v1"]);
    assert_eq!(toc.resolve_tag("v1")?, snap);
    assert_eq!(toc.all_snapshots()?.count(), 2);
    assert!(!version.is_create());

    let kinds = op_kinds(&repo).await?;
    assert!(matches!(&kinds[0], UpdateType::BranchDeletedUpdate { name, .. } if name == "dev"));
    assert!(matches!(&kinds[1], UpdateType::TagCreatedUpdate { name } if name == "v1"));
    assert!(matches!(&kinds[2], UpdateType::BranchCreatedUpdate { name } if name == "dev"));
    assert!(matches!(&kinds[3], UpdateType::NewCommitUpdate { branch, .. } if branch == "main"));
    assert!(matches!(kinds[4], UpdateType::RepoInitializedUpdate));
    Ok(())
}

#[tokio::test]
async fn gate_amend_and_anonymous_snapshot() -> TestResult {
    let repo = mem_repo().await;
    seed(&repo, &p("/array"), 2).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/added"), Bytes::new()).await?;
    session.commit("amended").max_concurrent_nodes(4).amend().execute().await?;
    assert_eq!(
        messages(&repo, &tip("main")).await?,
        vec!["amended".to_string(), Snapshot::INITIAL_COMMIT_MESSAGE.to_string()]
    );

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/detached"), Bytes::new()).await?;
    let anonymous =
        session.commit("anonymous").max_concurrent_nodes(4).anonymous().execute().await?;
    assert!(session.branch().is_none());
    assert_eq!(repo.lookup_branch("main").await?, repo.resolve_version(&tip("main")).await?);
    assert_ne!(repo.lookup_branch("main").await?, anonymous);

    let detached = repo.readonly_session(&VersionInfo::SnapshotId(anonymous)).await?;
    assert!(detached.get_group(&p("/detached")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn gate_fork_merge_round_trip() -> TestResult {
    let repo = mem_repo().await;
    let path = p("/array");
    seed(&repo, &path, 4).await?;

    let mut base = repo.writable_session("main").await?;
    base.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(ChunkPayload::Inline("base".into())))
        .await?;
    let fork = base.fork().await?;
    assert!(fork.is_fork());

    let mut worker = fork.clone();
    worker
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Inline("worker".into())),
        )
        .await?;
    assert!(matches!(
        worker.commit("nope").execute().await.unwrap_err().kind,
        SessionErrorKind::CommitNotAllowed
    ));
    base.merge(worker).await?;
    base.commit("merged").max_concurrent_nodes(4).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &path, 0).await?, Some(Bytes::from_static(b"base")));
    assert_eq!(chunk_bytes(&session, &path, 1).await?, Some(Bytes::from_static(b"worker")));
    Ok(())
}

#[tokio::test]
async fn gate_move_node_and_shift_array() -> TestResult {
    let repo = mem_repo().await;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_group(p("/old"), Bytes::new()).await?;
    let path = p("/old/array");
    session
        .add_array(path.clone(), ArrayShape::new(vec![(8u64, 4u32)]).ok_or("shape")?, None, Bytes::new())
        .await?;
    for i in 0..4u32 {
        session
            .set_chunk_ref(
                path.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(i.to_string().into())),
            )
            .await?;
    }
    session.commit("first").max_concurrent_nodes(4).execute().await?;

    // a move needs a rearrange session and copies no bytes
    let mut writable = repo.writable_session("main").await?;
    assert!(matches!(
        writable.move_node(p("/old"), p("/new")).await.unwrap_err().kind,
        SessionErrorKind::NonRearrangeSession
    ));
    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/old"), p("/new")).await?;
    assert!(matches!(
        rearrange.add_group(p("/nope"), Bytes::new()).await.unwrap_err().kind,
        SessionErrorKind::RearrangeSessionOnly
    ));
    rearrange.commit("moved").max_concurrent_nodes(4).execute().await?;

    let moved = p("/new/array");
    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(chunk_bytes(&session, &moved, i).await?, Some(Bytes::from(i.to_string())));
    }

    // a shift is an ordinary edit, and its offset is in chunks
    let mut session = repo.writable_session("main").await?;
    session.shift_array(&moved, &[-1]).await?;
    session.commit("shifted").max_concurrent_nodes(4).execute().await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &moved, 0).await?, Some(Bytes::from_static(b"1")));
    assert_eq!(chunk_bytes(&session, &moved, 3).await?, None);
    Ok(())
}

#[tokio::test]
async fn gate_status_flags_and_metadata() -> TestResult {
    let repo = mem_repo().await;
    seed(&repo, &p("/array"), 2).await?;

    // repository-level metadata
    repo.set_metadata(&SnapshotProperties::from([(
        "team".to_string(),
        serde_json::json!("science"),
    )]))
    .await?;
    let merged = repo
        .update_metadata(&SnapshotProperties::from([(
            "run".to_string(),
            serde_json::json!(7),
        )]))
        .await?;
    assert_eq!(merged.len(), 2);
    assert_eq!(repo.get_metadata().await?["team"], serde_json::json!("science"));

    // feature flags
    assert_eq!(repo.feature_flags().await?.count(), 3);
    repo.set_feature_flag("move_node", Some(false)).await?;
    assert!(repo.rearrange_session("main").await.is_err());
    repo.set_feature_flag("move_node", None).await?;
    assert!(repo.rearrange_session("main").await.is_ok());

    // repository status
    assert_eq!(repo.get_status().await?.availability, RepoAvailability::Online);
    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("maintenance".to_string()),
    })
    .await?;
    assert!(matches!(
        repo.writable_session("main").await.unwrap_err().kind,
        RepositoryErrorKind::ReadonlyRepository(_)
    ));
    assert!(repo.readonly_session(&tip("main")).await.is_ok());
    repo.set_status(&RepoStatus {
        availability: RepoAvailability::Online,
        set_at: Utc::now(),
        limited_availability_reason: None,
    })
    .await?;
    assert!(repo.writable_session("main").await.is_ok());
    Ok(())
}

#[tokio::test]
async fn gate_migration_from_the_first_generation() -> TestResult {
    let dir = tempfile::tempdir()?;
    let root = dir.path().join("test-repo-v1");
    copy_tree(StdPath::new(V1_FIXTURE), &root)?;
    let storage = new_local_filesystem_storage(root.as_path()).await?;

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V1);
    let before = messages(&repo, &tip("main")).await?;
    assert!(!before.is_empty());

    // a dry run changes nothing
    migrate_1_to_2(repo, true, true, None).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V1);

    migrate_1_to_2(repo, false, true, None).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert_eq!(messages(&repo, &tip("main")).await?, before);

    let kinds = op_kinds(&repo).await?;
    assert!(matches!(
        &kinds[0],
        UpdateType::RepoMigratedUpdate {
            from_version: SpecVersionBin::V1,
            to_version: SpecVersionBin::V2
        }
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// One end-to-end path through the milestones this step inherits.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate_inherited_versioning_and_maintenance_path() -> TestResult {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..RepositoryConfig::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
        None,
        true,
    )
    .await?;

    // write through the Zarr store interface
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store
        .set("zarr.json", Bytes::from_static(br#"{"zarr_format":3,"node_type":"group"}"#))
        .await?;
    store
        .set(
            "array/zarr.json",
            Bytes::from_static(
                br#"{"zarr_format":3,"node_type":"array","shape":[4],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[2]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"bytes","configuration":{"endian":"little"}}]}"#,
            ),
        )
        .await?;
    store.set("array/c/0", Bytes::from_static(b"0011")).await?;
    store.set("array/c/1", Bytes::from_static(b"2233")).await?;
    let first =
        session.write().await.commit("wrote").max_concurrent_nodes(4).execute().await?;

    // branches, tags and time travel
    repo.create_tag("v1", &first).await?;
    repo.create_branch("dev", &first).await?;
    let mut session = repo.writable_session("main").await?;
    session
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("4455".into())),
        )
        .await?;
    let second = session.commit("second").max_concurrent_nodes(4).execute().await?;

    let then = repo.readonly_session(&VersionInfo::TagRef("v1".into())).await?;
    assert_eq!(chunk_bytes(&then, &p("/array"), 0).await?, Some(Bytes::from_static(b"0011")));
    let now = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&now, &p("/array"), 0).await?, Some(Bytes::from_static(b"4455")));

    // the on-disk format: a snapshot with manifest files behind it
    assert!(repo.lookup_manifest_files(&second).await?.count() >= 1);
    assert!(repo.asset_manager().list_chunks().await?.count().await >= 2);

    // maintenance: abandon the second commit, then collect the garbage
    repo.reset_branch("main", &first, Some(&second)).await?;
    let now = Utc::now();
    let config = icechunk::ops::gc::GCConfig::clean_all(
        now,
        now,
        None,
        std::num::NonZeroU16::new(50).ok_or("nonzero")?,
        std::num::NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        std::num::NonZeroU16::new(50).ok_or("nonzero")?,
        false,
    );
    let summary =
        icechunk::ops::gc::garbage_collect(Arc::clone(repo.asset_manager()), &config, None, 100)
            .await?;
    assert!(summary.snapshots_deleted >= 1, "{summary:?}");

    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &p("/array"), 0).await?, Some(Bytes::from_static(b"0011")));
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), vec!["v1"]);
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        vec!["dev".to_string(), "main".to_string()]
    );
    Ok(())
}

#[tokio::test]
async fn gate_inherited_command_line_interface() -> TestResult {
    let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
        .expect("ICECHUNK_TEST_CLI_BIN must name the candidate's icechunk binary");
    let home = tempfile::tempdir()?;
    let repo_dir = tempfile::tempdir()?;
    let root = repo_dir.path().join("repo");
    std::fs::create_dir_all(&root)?;

    let config_dir = home.path().join("icechunk");
    std::fs::create_dir_all(&config_dir)?;
    std::fs::write(
        config_dir.join("cli-config.yaml"),
        format!(
            "repos:\n  demo: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            root.display()
        ),
    )?;

    let run = |args: &[&str]| -> std::io::Result<(Option<i32>, String)> {
        let out = std::process::Command::new(&bin)
            .args(args)
            .env("XDG_CONFIG_HOME", home.path())
            .env("HOME", home.path())
            .output()?;
        Ok((out.status.code(), String::from_utf8_lossy(&out.stdout).into_owned()))
    };

    let (code, stdout) = run(&["--help"])?;
    assert_eq!(code, Some(0));
    assert!(stdout.contains("repo") && stdout.contains("snapshot"));

    let (code, _) = run(&["repo", "create", "demo"])?;
    assert_eq!(code, Some(0));

    let storage = new_local_filesystem_storage(&root).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("from the library").max_concurrent_nodes(2).execute().await?;

    let (code, stdout) = run(&["snapshot", "list", "demo"])?;
    assert_eq!(code, Some(0));
    assert!(stdout.contains("from the library"), "{stdout}");

    let (code, _) = run(&["snapshot", "list", "no-such-alias"])?;
    assert!(matches!(code, Some(c) if c != 0));
    Ok(())
}
