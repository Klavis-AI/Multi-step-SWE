// Verifier-owned end-to-end driver for step 10 (`gate-endtoend`).
//
// One scored group among the others: this step's headline capability at
// proof-of-life depth, plus one end-to-end path through every milestone the
// step inherits. It compiles as its own target so a conformance driver that
// does not typecheck cannot take it down, and vice versa.
//
// Public `icechunk::` API only; in-memory and temporary-directory storage only;
// no network, no cloud, no MinIO.

#![allow(non_snake_case)]

use std::{
    collections::{BTreeSet, HashMap, HashSet},
    num::{NonZeroU16, NonZeroUsize},
    sync::Arc,
};

use bytes::Bytes;
use chrono::{DateTime, Utc};
use futures::TryStreamExt as _;
use icechunk::{
    ObjectStoreConfig, Repository, Storage, Store,
    config::{
        Credentials, HttpConfig, ManifestConfig, ManifestSplittingConfig,
        RepositoryConfig, S3Credentials, S3Options, S3StaticCredentials,
    },
    conflicts::detector::ConflictDetector,
    error::ICError,
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        format_constants::{
            CompressionAlgorithmBin, FileTypeBin, ICECHUNK_FILE_HEADER_LEN,
            ICECHUNK_FORMAT_MAGIC_BYTES, ICECHUNK_IMPL_NAME_LEN, SpecVersionBin,
            parse_file_header,
        },
        manifest::{ChunkPayload, VirtualChunkLocation},
        repo_info::UpdateType,
        snapshot::{ArrayShape, DimensionName},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{ExpiredRefAction, GCConfig, expire, garbage_collect},
    repository::{RepositoryErrorKind, VersionInfo},
    session::SessionErrorKind,
    storage::{RepositoryCreation, s3_storage},
    virtual_chunks::VirtualChunkContainer,
};
use tokio::sync::RwLock;

type Res = Result<(), Box<dyn std::error::Error>>;

fn path_of(p: &str) -> Path {
    if p == "/" { Path::root() } else { Path::try_from(p).unwrap() }
}

fn paths(items: &[&str]) -> BTreeSet<Path> {
    items.iter().map(|p| path_of(p)).collect()
}

async fn memory_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(Repository::create(None, storage, HashMap::new(), None, true).await?)
}

async fn commit_group(
    repo: &Repository,
    branch: &str,
    path: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session(branch).await?;
    session.add_group(path_of(path), Bytes::new()).await?;
    Ok(session.commit(path).execute().await?)
}

async fn threshold_between_commits() -> DateTime<Utc> {
    tokio::time::sleep(std::time::Duration::from_millis(2)).await;
    let t = Utc::now();
    tokio::time::sleep(std::time::Duration::from_millis(2)).await;
    t
}

fn clean_all_now() -> GCConfig {
    let now = Utc::now();
    GCConfig::clean_all(
        now,
        now,
        None,
        NonZeroU16::new(50).unwrap(),
        NonZeroUsize::new(512 * 1024 * 1024).unwrap(),
        NonZeroU16::new(500).unwrap(),
        false,
    )
}

async fn add_small_array(
    session: &mut icechunk::session::Session,
    path: &str,
    chunks: u32,
) -> Res {
    let shape = ArrayShape::new(vec![(chunks as u64, chunks)]).unwrap();
    session
        .add_array(
            path_of(path),
            shape,
            Some(vec![DimensionName::from(Some("t"))]),
            Bytes::new(),
        )
        .await?;
    for i in 0..chunks {
        session
            .set_chunk_ref(
                path_of(path),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(Bytes::copy_from_slice(&[i as u8; 4]))),
            )
            .await?;
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// This step's new capability.
// ---------------------------------------------------------------------------

/// Expiration records the pruned transaction-log ancestry, GC keeps the logs it
/// references, and diff, amend and inspect all stay correct across the boundary.
#[tokio::test]
async fn gate__expiration_records_pruned_ancestry_end_to_end() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let initial = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".into()))
        .await?
        .snapshot_id()
        .clone();

    let g0 = commit_group(&repo, "main", "/").await?;
    let a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let threshold = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;

    let result = expire(
        Arc::clone(&am),
        threshold,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert_eq!(result.released_snapshots.len(), 3);
    assert!(result.edited_snapshots.contains(&c));

    // The boundary snapshot records the collapsed run, oldest first.
    let history: Vec<_> = repo
        .ancestry(&VersionInfo::SnapshotId(c.clone()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(history[0].pruned_ancestor_tx_logs, vec![g0.clone(), a.clone(), b.clone()]);

    // GC deletes the expired snapshot files but keeps their transaction logs.
    let summary = garbage_collect(Arc::clone(&am), &clean_all_now(), None, 100).await?;
    assert_eq!(summary.snapshots_deleted, 3);
    assert_eq!(summary.transaction_logs_deleted, 0);

    // Diff across the boundary is still complete.
    let diff = repo
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    assert_eq!(diff.new_groups, paths(&["/", "/a", "/b", "/c"]));

    // Inspecting the edited boundary returns a flagged synthetic composite.
    let json = icechunk::inspect::transaction_log_json(am.as_ref(), &c, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&json)?;
    let merged: Vec<String> = parsed
        .get("synthetic_composite")
        .and_then(|v| v.get("merged_pruned_ancestor_tx_logs"))
        .and_then(|v| v.as_array())
        .expect("an expiration-edited snapshot renders a synthetic composite")
        .iter()
        .map(|v| v.as_str().unwrap_or_default().to_string())
        .collect();
    assert_eq!(merged, vec![g0.to_string(), a.to_string(), b.to_string()]);
    Ok(())
}

/// A metadata file's header is readable without decoding its body, and spec 2.1
/// still flags the on-disk format as version byte `2`.
#[tokio::test]
async fn gate__header_inspection_reports_spec_two() -> Res {
    // From raw bytes: the body is ignored entirely.
    let mut buf = Vec::with_capacity(ICECHUNK_FILE_HEADER_LEN);
    buf.extend_from_slice(ICECHUNK_FORMAT_MAGIC_BYTES);
    let mut name = b"ic-2.1.0".to_vec();
    name.resize(ICECHUNK_IMPL_NAME_LEN, b' ');
    buf.extend_from_slice(&name);
    buf.push(SpecVersionBin::V2 as u8);
    buf.push(FileTypeBin::Snapshot as u8);
    buf.push(CompressionAlgorithmBin::Zstd as u8);
    buf.extend_from_slice(b"a compressed body that must never be decoded");
    let header = parse_file_header(&buf)?;
    assert_eq!(header.spec_version, SpecVersionBin::V2);
    assert_eq!(header.file_type, FileTypeBin::Snapshot);
    assert_eq!(header.app_name, "ic");

    // From storage: before and after the 2.1 feature is used, the byte is `2`.
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    commit_group(&repo, "main", "/").await?;
    let before = commit_group(&repo, "main", "/a").await?;
    assert_eq!(am.fetch_repo_info_header().await?.file_type, FileTypeBin::RepoInfo);
    assert_eq!(
        am.fetch_snapshot_header(&before).await?.spec_version,
        SpecVersionBin::V2
    );

    let threshold = threshold_between_commits().await;
    let after = commit_group(&repo, "main", "/b").await?;
    expire(
        Arc::clone(&am),
        threshold,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert_eq!(am.fetch_repo_info_header().await?.spec_version, SpecVersionBin::V2);
    assert_eq!(
        am.fetch_transaction_log_header(&after).await?.file_type,
        FileTypeBin::TransactionLog
    );
    Ok(())
}

/// Static HTTP headers are part of the configuration surface, and the
/// no-credential sentinels authorize exactly their own backend.
#[tokio::test]
async fn gate__http_headers_and_credential_sentinels() -> Res {
    let cfg = HttpConfig {
        opts: HashMap::from([("allow_http".to_string(), "true".to_string())]),
        headers: HashMap::from([(
            "authorization".to_string(),
            "Bearer token123".to_string(),
        )]),
    };
    let store = ObjectStoreConfig::Http(cfg.clone());
    let round_tripped: ObjectStoreConfig =
        serde_json::from_str(&serde_json::to_string(&store)?)?;
    assert_eq!(round_tripped, store);
    // A 2.0.x document that carried only transport options still loads.
    let legacy: ObjectStoreConfig = serde_json::from_str(r#"{"http":{"allow_http":"true"}}"#)?;
    match legacy {
        ObjectStoreConfig::Http(c) => {
            assert_eq!(c.opts.get("allow_http").map(String::as_str), Some("true"));
            assert!(c.headers.is_empty());
        }
        other => panic!("expected ObjectStoreConfig::Http, got {other:?}"),
    }

    let dir = tempfile::tempdir()?;
    let fs = VirtualChunkContainer::new(
        format!("file://{}/", dir.path().to_str().unwrap()),
        ObjectStoreConfig::LocalFileSystem(dir.path().to_path_buf()),
    )?;
    let http = VirtualChunkContainer::new(
        "http://example.com/".to_string(),
        ObjectStoreConfig::Http(cfg),
    )?;
    assert!(fs.validate_credentials(Some(&Credentials::LocalFileSystemAccess)).is_ok());
    assert!(http.validate_credentials(Some(&Credentials::HttpAccess)).is_ok());
    assert!(fs.validate_credentials(Some(&Credentials::HttpAccess)).is_err());
    assert!(http.validate_credentials(Some(&Credentials::LocalFileSystemAccess)).is_err());
    // `None` is deprecated but still validates.
    assert!(fs.validate_credentials(None).is_ok());
    assert!(http.validate_credentials(None).is_ok());
    Ok(())
}

/// Repository creation at an object store's root is refused before any I/O, and
/// a virtual chunk location keeps every part of its URL.
#[tokio::test]
async fn gate__empty_prefix_refused_and_url_parts_preserved() -> Res {
    let creds = S3Credentials::Static(S3StaticCredentials {
        access_key_id: "key".to_string(),
        secret_access_key: "secret".to_string(),
        session_token: None,
        expires_after: None,
    });
    let root: Arc<dyn Storage + Send + Sync> = Arc::new(s3_storage(
        S3Options::default().with_region("us-east-1"),
        "testbucket".to_string(),
        Some(String::new()),
        Some(creds),
        None,
    )?);
    assert_eq!(
        root.can_create_repository().await?,
        RepositoryCreation::RefusedEmptyPrefix
    );
    let err = Repository::create(None, root, HashMap::new(), None, true)
        .await
        .unwrap_err();
    assert!(matches!(
        err,
        ICError { kind: RepositoryErrorKind::EmptyPrefixCreation, .. }
    ));

    // The non-cloud backends are unaffected.
    assert_eq!(
        new_in_memory_storage().await?.can_create_repository().await?,
        RepositoryCreation::Allowed
    );

    let verbatim = "https://user:pass@host.com:8443/a//b/../c.bin?versionId=42#frag";
    assert_eq!(VirtualChunkLocation::from_url(verbatim)?.url(), verbatim);
    assert_eq!(
        VirtualChunkLocation::from_url("http://127.0.0.1:8080/x")?.url(),
        "http://127.0.0.1:8080/x"
    );
    // `file://` remains normalized, which is what confines a container.
    assert_eq!(VirtualChunkLocation::from_url("file:///a/b/../c")?.url(), "file:///a/c");
    Ok(())
}

// ---------------------------------------------------------------------------
// One end-to-end path through each inherited milestone.
// ---------------------------------------------------------------------------

/// Steps 01-02: repositories, sessions, branches, tags, the Zarr store and
/// conflict detection on rebase.
#[tokio::test]
async fn gate__inherited_repository_sessions_and_conflicts() -> Res {
    let repo = memory_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::from_static(b"{}")).await?;
    add_small_array(&mut session, "/array", 2).await?;
    let first = session.commit("initial").execute().await?;

    repo.create_branch("feature", &first).await?;
    repo.create_tag("v1", &first).await?;
    assert!(repo.list_branches().await?.contains("feature"));
    assert_eq!(repo.lookup_tag("v1").await?, first);

    let read = repo.readonly_session(&VersionInfo::TagRef("v1".into())).await?;
    let store = Store::from_session(Arc::new(RwLock::new(read))).await;
    assert_eq!(
        store.get("array/c/1", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(&[1u8; 4])
    );

    let mut losing = repo.writable_session("main").await?;
    losing.add_group(path_of("/x"), Bytes::new()).await?;
    let mut winning = repo.writable_session("main").await?;
    winning.add_group(path_of("/x"), Bytes::new()).await?;
    winning.commit("winner").execute().await?;
    let err = losing.commit("loser").execute().await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::Conflict { .. }));
    let err = losing.rebase(&ConflictDetector).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RebaseFailed { .. }));
    Ok(())
}

/// Steps 03-05: the on-disk metadata header, expiration and garbage collection
/// accounting, and snapshot lookup.
#[tokio::test]
async fn gate__inherited_file_headers_expiration_and_gc() -> Res {
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true)
            .await?;
    let am = Arc::clone(repo.asset_manager());

    commit_group(&repo, "main", "/").await?;
    let pinned = commit_group(&repo, "main", "/a").await?;
    let threshold = threshold_between_commits().await;
    let tip = commit_group(&repo, "main", "/b").await?;

    // Every written metadata object starts with the documented header.
    let bytes = std::fs::read(dir.path().join("snapshots").join(tip.to_string()))?;
    assert!(bytes.starts_with(ICECHUNK_FORMAT_MAGIC_BYTES));
    assert_eq!(parse_file_header(&bytes)?.file_type, FileTypeBin::Snapshot);

    assert_eq!(repo.lookup_snapshot(&pinned).await?.id, pinned);

    let result = expire(
        Arc::clone(&am),
        threshold,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert_eq!(result.released_snapshots.len(), 2);
    let summary = garbage_collect(Arc::clone(&am), &clean_all_now(), None, 100).await?;
    assert_eq!(summary.snapshots_deleted, 2);

    let remaining: Vec<_> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".into()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(remaining.len(), 2);
    Ok(())
}

/// Steps 06-08: manifest splitting policy, virtual chunk containers and the
/// inspect module.
#[tokio::test]
async fn gate__inherited_manifest_splitting_and_virtual_containers() -> Res {
    let config = RepositoryConfig {
        manifest: Some(ManifestConfig {
            splitting: Some(ManifestSplittingConfig::with_size(2)),
            ..ManifestConfig::default()
        }),
        ..RepositoryConfig::default()
    };
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo =
        Repository::create(Some(config), storage, HashMap::new(), None, true).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    add_small_array(&mut session, "/array", 8).await?;
    let snap = session.commit("array").execute().await?;
    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert!(manifests.len() > 1, "a split policy must produce several manifests");

    // Reads are unchanged by the policy.
    let read = repo.readonly_session(&VersionInfo::SnapshotId(snap.clone())).await?;
    let store = Store::from_session(Arc::new(RwLock::new(read))).await;
    for i in 0..8u32 {
        assert_eq!(
            store.get(&format!("array/c/{i}"), &ByteRange::ALL).await?,
            Bytes::copy_from_slice(&[i as u8; 4])
        );
    }

    // A container url prefix must end in a separator.
    let dir = tempfile::tempdir()?;
    let prefix = format!("file://{}/", dir.path().to_str().unwrap());
    assert!(
        VirtualChunkContainer::new(
            prefix.trim_end_matches('/').to_string(),
            ObjectStoreConfig::LocalFileSystem(dir.path().to_path_buf()),
        )
        .is_err()
    );
    assert!(
        VirtualChunkContainer::new(
            prefix,
            ObjectStoreConfig::LocalFileSystem(dir.path().to_path_buf()),
        )
        .is_ok()
    );

    let am = Arc::clone(repo.asset_manager());
    let json = icechunk::inspect::snapshot_json(am.as_ref(), &snap, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&json)?;
    assert_eq!(
        parsed.get("id").and_then(|v| v.as_str()),
        Some(snap.to_string().as_str())
    );
    Ok(())
}

/// Step 05: the command-line interface. The runner builds the candidate's own
/// `icechunk` binary with `--features cli --bin icechunk` and names it in
/// ICECHUNK_TEST_CLI_BIN; this is the gate's one path through that milestone,
/// and it is why a failed `--features cli` build is attributed to this group as
/// well as to the CLI conformance group.
#[tokio::test]
async fn gate__inherited_command_line_interface() -> Res {
    let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
        .map_err(|_| "ICECHUNK_TEST_CLI_BIN is not set")?;
    let home = tempfile::tempdir()?;
    let repo_dir = home.path().join("repo");
    std::fs::create_dir_all(&repo_dir)?;
    std::fs::create_dir_all(home.path().join("icechunk"))?;
    // `RepositoryDefinition` is an externally tagged enum, which serde_yaml_ng
    // renders as a YAML tag.
    std::fs::write(
        home.path().join("icechunk/cli-config.yaml"),
        format!(
            "repos:\n  demo: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_dir.to_str().unwrap()
        ),
    )?;
    let run = |args: &[&str]| {
        std::process::Command::new(&bin)
            .args(args)
            .env("XDG_CONFIG_HOME", home.path())
            .env("HOME", home.path())
            .env("ICECHUNK_NO_LOGS", "1")
            .output()
            .expect("the icechunk binary must be executable")
    };

    let help = run(&["--help"]);
    assert!(help.status.success(), "`icechunk --help` must exit 0");
    let text = String::from_utf8_lossy(&help.stdout).to_lowercase();
    for group in ["repo", "snapshot", "config"] {
        assert!(text.contains(group), "`--help` must name the `{group}` command group");
    }

    assert!(
        run(&["repo", "create", "demo"]).status.success(),
        "`repo create demo` must create the repository named in the config"
    );
    let listed = run(&["snapshot", "list", "demo"]);
    assert!(listed.status.success(), "`snapshot list demo` must exit 0");
    let lines = String::from_utf8_lossy(&listed.stdout)
        .lines()
        .filter(|l| !l.trim().is_empty())
        .count();
    assert!(lines >= 1, "`snapshot list` prints one line per snapshot, got {lines}");
    assert!(
        !run(&["snapshot", "list", "no-such-alias"]).status.success(),
        "an alias absent from the config must exit non-zero"
    );
    Ok(())
}

/// Step 09: the Icechunk 2 repository table of contents, its operations log and
/// feature flags.
#[tokio::test]
async fn gate__inherited_table_of_contents_and_operations_log() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let first = commit_group(&repo, "main", "/").await?;
    repo.create_branch("feature", &first).await?;
    repo.create_tag("v1", &first).await?;

    let (info, _version) = am.fetch_repo_info().await?;
    assert_eq!(info.spec_version()?, SpecVersionBin::V2);
    let branches: HashSet<String> =
        info.branches()?.map(|(name, _)| name.to_string()).collect();
    assert!(branches.contains("main") && branches.contains("feature"));
    assert_eq!(info.resolve_tag("v1")?, first);
    assert_eq!(info.find_snapshot(&first)?.id, first);

    let (stream, _info, _version) = repo.ops_log().await?;
    let entries: Vec<(DateTime<Utc>, UpdateType, Option<String>)> =
        stream.try_collect().await?;
    assert!(entries.iter().any(|(_, k, _)| matches!(k, UpdateType::RepoInitializedUpdate)));
    assert!(entries.iter().any(
        |(_, k, _)| matches!(k, UpdateType::BranchCreatedUpdate { name } if name == "feature")
    ));

    repo.set_feature_flag("move_node", Some(true)).await?;
    let enabled: HashSet<String> =
        repo.enabled_feature_flags().await?.map(|f| f.name().to_string()).collect();
    assert!(enabled.contains("move_node"));
    Ok(())
}
