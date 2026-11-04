// Verifier-owned end-to-end driver for step 03 (`icechunk` 0.1.0).
//
// `gate-endtoend` is one ordinary scored group among the others. It is the
// shallow, wide check: one end-to-end path through this step's headline
// capability, and one through each milestone this step inherits, so a
// regression in an earlier milestone still costs score even though Harbor
// never re-runs that milestone's verifier.
//
// Public `icechunk::` API only; no network, no cloud, no emulator.
#![allow(clippy::expect_used, clippy::unwrap_used, clippy::panic)]
#![allow(non_snake_case)]

use std::{collections::HashMap, num::NonZeroU64, path::PathBuf, sync::Arc};

use bytes::Bytes;
use futures::{StreamExt, TryStreamExt};
use icechunk::{
    asset_manager::AssetManager,
    conflicts::basic_solver::BasicConflictSolver,
    format::{
        format_constants::{
            CompressionAlgorithmBin, FileTypeBin, SpecVersionBin,
            ICECHUNK_FORMAT_MAGIC_BYTES,
        },
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::ZarrArrayMetadata,
        ByteRange, ChunkIndices, Path,
    },
    metadata::{ChunkKeyEncoding, ChunkShape, DataType, FillValue},
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{expire, garbage_collect, GCConfig, GCSummary},
    repository::VersionInfo,
    session::get_chunk,
    storage::ObjectStorage,
    ObjectStoreConfig, Repository, RepositoryConfig, Storage,
};
use tokio::io::AsyncReadExt;

type R = Result<(), Box<dyn std::error::Error>>;

fn array_meta(shape: Vec<u64>, chunk: Vec<u64>) -> ZarrArrayMetadata {
    ZarrArrayMetadata {
        shape,
        data_type: DataType::Int32,
        chunk_shape: ChunkShape(
            chunk.into_iter().map(|c| NonZeroU64::new(c).expect("non-zero")).collect(),
        ),
        chunk_key_encoding: ChunkKeyEncoding::Slash,
        fill_value: FillValue::Int32(0),
        codecs: vec![],
        storage_transformers: None,
        dimension_names: None,
    }
}

async fn commit_group(repo: &Repository, branch: &str, name: &str) -> icechunk::format::SnapshotId {
    let mut session = repo.writable_session(branch).await.expect("writable session");
    session
        .add_group(Path::try_from(format!("/{name}").as_str()).expect("path"))
        .await
        .expect("add group");
    session.commit(name, None).await.expect("commit")
}

async fn branch_messages(repo: &Repository, branch: &str) -> Vec<String> {
    repo.ancestry(&VersionInfo::BranchTipRef(branch.to_string()))
        .await
        .expect("ancestry")
        .map_ok(|info| info.message)
        .try_collect::<Vec<_>>()
        .await
        .expect("ancestry stream")
}

// ---------------------------------------------------------------------------
// This step's headline capability
// ---------------------------------------------------------------------------

#[tokio::test]
/// Build a history, expire everything older than a cutoff, then collect the
/// garbage that expiration released, and check the counts.
async fn gate__expire_then_garbage_collect() -> R {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage()?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.commit("one", None).await?;
    commit_group(&repo, "main", "two").await;
    let keep_from = commit_group(&repo, "main", "three").await;
    // the cutoff comes from icechunk's own record of when "three" was flushed
    let cutoff = repo
        .ancestry(&VersionInfo::SnapshotId(keep_from.clone()))
        .await?
        .try_collect::<Vec<_>>()
        .await?
        .first()
        .expect("commit three")
        .flushed_at;
    commit_group(&repo, "main", "four").await;

    let expired = expire(
        storage.as_ref(),
        &settings,
        Arc::new(AssetManager::new_no_cache(Arc::clone(&storage), settings.clone(), 1)),
        cutoff,
    )
    .await?;
    assert_eq!(expired.len(), 2, "expected 'one' and 'two' to leave history");

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        branch_messages(&reopened, "main").await,
        ["four", "three", "Repository initialized"]
    );

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        Arc::new(AssetManager::new_no_cache(Arc::clone(&storage), settings.clone(), 1)),
        &GCConfig::clean_all(chrono::Utc::now(), chrono::Utc::now(), None),
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 2);
    assert_eq!(summary.attributes_deleted, 0);
    assert_ne!(summary, GCSummary::default());
    assert_eq!(storage.list_snapshots(&settings).await?.count().await, 3);
    Ok(())
}

#[tokio::test]
/// The 39-byte metadata header on a real object, and tag deletion burning the
/// name for good.
async fn gate__file_header_and_tag_deletion() -> R {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage()?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let snap = commit_group(&repo, "main", "first").await;

    let mut reader = storage.fetch_snapshot(&settings, &snap).await?;
    let mut header = [0u8; 39];
    reader.read_exact(&mut header).await?;
    assert_eq!(&header[0..12], ICECHUNK_FORMAT_MAGIC_BYTES);
    assert!(std::str::from_utf8(&header[12..36])?.starts_with("ic-"));
    assert_eq!(header[36], SpecVersionBin::current() as u8);
    assert_eq!(header[37], FileTypeBin::Snapshot as u8);
    assert_eq!(header[38], CompressionAlgorithmBin::Zstd as u8);

    repo.create_tag("v1", &snap).await?;
    assert_eq!(repo.lookup_tag("v1").await?, snap);
    repo.delete_tag("v1").await?;
    assert!(repo.list_tags().await?.is_empty());
    assert!(repo.lookup_tag("v1").await.is_err());
    assert!(repo.create_tag("v1", &snap).await.is_err(), "the name must stay burned");
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 1 — the spec-1 repository format
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__repository_format_round_trip() -> R {
    let object_storage = Arc::new(ObjectStorage::new_in_memory()?);
    let storage: Arc<dyn Storage + Send + Sync> = Arc::clone(&object_storage) as _;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..Default::default()
        }),
        storage,
        HashMap::new(),
    )
    .await?;

    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    let writer = session.get_chunk_writer();
    let payload = writer(Bytes::copy_from_slice(b"0123456789abcdef")).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    let snap = session.commit("write", None).await?;
    repo.create_tag("v1", &snap).await?;

    let keys = object_storage.all_keys().await?;
    for prefix in ["refs/", "snapshots/", "manifests/", "chunks/", "transactions/"] {
        assert!(keys.iter().any(|k| k.contains(prefix)), "no object under {prefix}");
    }
    assert!(keys.iter().any(|k| k.contains("refs/branch.main/ZZZZZZZZ.json")));
    assert!(keys.iter().any(|k| k.contains("refs/tag.v1/ref.json")));

    for version in [
        VersionInfo::BranchTipRef("main".to_string()),
        VersionInfo::TagRef("v1".to_string()),
        VersionInfo::SnapshotId(snap.clone()),
    ] {
        let session = repo.readonly_session(&version).await?;
        let reader = session
            .get_chunk_reader(&array, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
            .await?;
        assert_eq!(
            get_chunk(reader).await?,
            Some(Bytes::copy_from_slice(b"0123456789abcdef"))
        );
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 2 — sessions, conflicts and rebase, virtual chunk containers
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__sessions_and_version_history() -> R {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage()?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    let first = session.commit("first", None).await?;
    assert!(session.read_only(), "a committed session becomes read only");

    repo.create_branch("dev", &first).await?;
    let on_dev = commit_group(&repo, "dev", "dev-only").await;
    assert_eq!(repo.lookup_branch("dev").await?, on_dev);
    assert_eq!(repo.lookup_branch("main").await?, first);
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        ["dev", "main"]
    );

    let at_main = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    assert!(at_main.get_group(&"/dev-only".try_into()?).await.is_err());
    let at_dev = repo.readonly_session(&VersionInfo::BranchTipRef("dev".to_string())).await?;
    assert!(at_dev.get_group(&"/dev-only".try_into()?).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn gate__conflict_then_rebase_then_commit() -> R {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage()?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array: Path = "/array".try_into()?;

    let mut setup = repo.writable_session("main").await?;
    setup.add_group(Path::root()).await?;
    setup.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    setup.commit("setup", None).await?;

    let mut first = repo.writable_session("main").await?;
    let mut second = repo.writable_session("main").await?;

    first
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0, 0]),
            Some(ChunkPayload::Inline("ours".into())),
        )
        .await?;
    first.commit("first", None).await?;

    second
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![1, 1]),
            Some(ChunkPayload::Inline("theirs".into())),
        )
        .await?;
    assert!(second.commit("second", None).await.is_err(), "the parent is stale");

    second.rebase(&BasicConflictSolver::default()).await?;
    let merged = second.commit("second", None).await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(merged)).await?;
    for (coord, expected) in
        [(vec![0u32, 0u32], "ours"), (vec![1, 1], "theirs")]
    {
        match session.get_chunk_ref(&array, &ChunkIndices(coord.clone())).await? {
            Some(ChunkPayload::Inline(bytes)) => {
                assert_eq!(bytes, Bytes::from(expected))
            }
            other => panic!("chunk {coord:?} was {other:?}"),
        }
    }
    Ok(())
}

#[tokio::test]
async fn gate__virtual_chunk_container() -> R {
    let chunk_dir = tempfile::tempdir()?;
    let chunk_path = chunk_dir.path().join("data.bin");
    tokio::fs::write(&chunk_path, b"0123456789").await?;

    let repo_dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;

    let container = icechunk::virtual_chunks::VirtualChunkContainer {
        name: "file".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    };
    let mut containers = HashMap::new();
    containers.insert(container.name.clone(), container);
    let repo = Repository::create(
        Some(RepositoryConfig {
            virtual_chunk_containers: Some(containers),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    let url = format!("file://{}", chunk_path.to_str().expect("utf-8 path"));
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0, 0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path(&url)?,
                offset: 3,
                length: 4,
                checksum: None,
            })),
        )
        .await?;
    let snap = session.commit("virtual", None).await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let reader = session
        .get_chunk_reader(&array, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
        .await?;
    assert_eq!(get_chunk(reader).await?, Some(Bytes::copy_from_slice(b"3456")));
    Ok(())
}
