// Verifier-owned gate for step 02 of `icechunk-array-store-multiversion`.
//
// The gate is proof of life, not depth: one end-to-end path through this
// step's headline capability - the Repository / Session / Store split, commit
// conflicts and rebase, persistent configuration and virtual chunk containers
// - plus one end-to-end path through the milestone this step inherits, the
// spec-1 repository format of `icechunk-v0.1.0-alpha.1` as this release spells
// it. Harder variants live in tests/conformance.rs and still run when this
// fails; `gate-endtoend` is one ordinary scored group among the others.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
#![allow(non_snake_case)]

use std::{
    collections::{HashMap, HashSet},
    error::Error,
    num::NonZeroU64,
    path::PathBuf,
    sync::Arc,
};

use bytes::Bytes;
use futures::TryStreamExt;
use tokio::sync::RwLock;

use icechunk::{
    conflicts::basic_solver::{BasicConflictSolver, VersionSelection},
    format::{
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::ZarrArrayMetadata,
        ByteRange, ChunkIndices, Path,
    },
    metadata::{ChunkKeyEncoding, ChunkShape, DataType, FillValue, UserAttributes},
    repository::VersionInfo,
    session::{get_chunk, SessionError},
    storage::{new_in_memory_storage, new_local_filesystem_storage},
    store::StoreError,
    virtual_chunks::VirtualChunkContainer,
    ObjectStoreConfig, Repository, RepositoryConfig, Storage, Store,
};

const GROUP_META: &[u8] = br#"{"zarr_format":3, "node_type":"group"}"#;
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[4,4,4],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"]}"#;

fn p(s: &str) -> Path {
    s.try_into().expect("valid path")
}

fn basic_meta() -> ZarrArrayMetadata {
    ZarrArrayMetadata {
        shape: vec![5],
        data_type: DataType::Int32,
        chunk_shape: ChunkShape(vec![NonZeroU64::new(1).unwrap()]),
        chunk_key_encoding: ChunkKeyEncoding::Slash,
        fill_value: FillValue::Int32(0),
        codecs: vec![],
        storage_transformers: None,
        dimension_names: None,
    }
}

async fn mem_repo() -> Repository {
    let storage: Arc<dyn Storage + Send + Sync> =
        new_in_memory_storage().expect("in-memory storage");
    Repository::create(None, storage, HashMap::new()).await.expect("create repository")
}

// ---------------------------------------------------------------------------
// This step's headline capability.
// ---------------------------------------------------------------------------

/// The three entities exist and compose: a repository hands out a writable
/// session on a branch, a store is a view over that session, the commit
/// publishes to the branch and demotes the session, and a read-only session at
/// the resulting snapshot sees the data.
#[tokio::test]
async fn gate02__repository_session_store_split() -> Result<(), Box<dyn Error>> {
    let repo = mem_repo().await;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    assert_eq!(session.read().await.branch(), Some("main"));
    let store = Store::from_session(Arc::clone(&session)).await;
    assert!(!store.read_only().await);

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;

    let snapshot = session.write().await.commit("first commit", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, snapshot);
    // a successful commit demotes the session it was made from
    assert!(session.read().await.read_only());
    assert_eq!(session.read().await.branch(), None);

    let readonly = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?,
    )))
    .await;
    assert!(readonly.read_only().await);
    assert_eq!(
        readonly.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(b"hello")
    );
    assert!(matches!(
        readonly.set("array/c/0/0/1", Bytes::copy_from_slice(b"nope")).await,
        Err(StoreError::ReadOnly)
    ));
    Ok(())
}

/// A commit whose branch moved underneath it is refused, the conflict is
/// classified, a rebase with a policy resolves it, and the retried commit
/// succeeds with both writers' data present.
#[tokio::test]
async fn gate02__conflict_then_rebase_then_commit() -> Result<(), Box<dyn Error>> {
    let repo = mem_repo().await;
    let mut seed = repo.writable_session("main").await?;
    seed.add_group(p("/")).await?;
    seed.add_array(p("/array"), basic_meta()).await?;
    seed.commit("seed", None).await?;

    let mut ours = repo.writable_session("main").await?;
    let mut theirs = repo.writable_session("main").await?;

    theirs
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("theirs".into())),
        )
        .await?;
    theirs.commit("theirs", None).await?;

    ours.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ours.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Inline("only ours".into())),
    )
    .await?;
    assert!(matches!(
        ours.commit("ours", None).await,
        Err(SessionError::Conflict { .. })
    ));

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseTheirs,
        ..Default::default()
    };
    ours.rebase(&solver).await?;
    let snapshot = ours.commit("ours after rebase", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, snapshot);

    let r = repo.readonly_session(&VersionInfo::SnapshotId(snapshot)).await?;
    assert_eq!(
        r.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline("theirs".into()))
    );
    assert_eq!(
        r.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Inline("only ours".into()))
    );
    Ok(())
}

/// Configuration is persisted in the repository, a caller-supplied config
/// overrides it for one instance, and `save_config` makes the override the new
/// stored default.
#[tokio::test]
async fn gate02__persistent_configuration() -> Result<(), Box<dyn Error>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage()?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.config(), &RepositoryConfig::default());
    assert!(Repository::fetch_config(storage.as_ref()).await?.is_some());

    let overridden = Repository::open(
        Some(RepositoryConfig { inline_chunk_threshold_bytes: 42, ..Default::default() }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    assert_eq!(overridden.config().inline_chunk_threshold_bytes, 42);
    overridden.save_config().await?;

    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(reopened.config().inline_chunk_threshold_bytes, 42);
    Ok(())
}

/// A virtual chunk container authorises a URL prefix; a reference into a real
/// file under it reads back through the store, and every virtual location the
/// repository holds is reportable.
#[tokio::test(flavor = "multi_thread")]
async fn gate02__virtual_chunk_container() -> Result<(), Box<dyn Error>> {
    let chunk_dir = tempfile::TempDir::new()?;
    let chunk_path = chunk_dir.path().join("chunk-1");
    std::fs::write(&chunk_path, b"first")?;
    let chunk_path = chunk_path.to_str().expect("utf8 temp path").to_owned();

    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "file".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    });
    let repo = Repository::create(Some(config), storage, HashMap::new()).await?;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let location =
        VirtualChunkLocation::from_absolute_path(&format!("file://{}", chunk_path))?;
    store
        .set_virtual_ref(
            "array/c/0/0/0",
            VirtualChunkRef {
                location: location.clone(),
                offset: 0,
                length: 5,
                checksum: None,
            },
        )
        .await?;

    assert_eq!(
        store.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(b"first")
    );
    let locations: HashSet<String> = session
        .read()
        .await
        .all_virtual_chunk_locations()
        .await?
        .try_collect()
        .await?;
    assert_eq!(locations, HashSet::from([location.0]));
    Ok(())
}

// ---------------------------------------------------------------------------
// The milestone this step inherits: the spec-1 repository format.
// ---------------------------------------------------------------------------

/// Branches and tags reference snapshots, a commit records a snapshot with its
/// message, the ancestry walks back to the repository's first snapshot, and all
/// three chunk reference kinds round-trip through a commit.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__spec1_repository_format() -> Result<(), Box<dyn Error>> {
    let chunk_dir = tempfile::TempDir::new()?;
    let chunk_path = chunk_dir.path().join("virtual-chunk");
    std::fs::write(&chunk_path, b"virtual!")?;
    let chunk_path = chunk_path.to_str().expect("utf8 temp path").to_owned();

    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let mut config = RepositoryConfig {
        inline_chunk_threshold_bytes: 8,
        ..Default::default()
    };
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "file".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    });
    let repo = Repository::create(Some(config), storage, HashMap::new()).await?;
    let root = repo.lookup_branch("main").await?;

    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/")).await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_user_attributes(p("/"), Some(UserAttributes::try_new(br#"{"n":1}"#)?)).await?;

    // 1. an inline chunk reference
    let inline = ds.get_chunk_writer()(Bytes::copy_from_slice(b"inline")).await?;
    assert!(matches!(inline, ChunkPayload::Inline(_)));
    ds.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(inline)).await?;
    // 2. a materialised chunk reference
    let materialised =
        ds.get_chunk_writer()(Bytes::copy_from_slice(b"materialised")).await?;
    assert!(matches!(materialised, ChunkPayload::Ref(_)));
    ds.set_chunk_ref(p("/array"), ChunkIndices(vec![1]), Some(materialised)).await?;
    // 3. a virtual chunk reference
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![2]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_absolute_path(&format!(
                "file://{}",
                chunk_path
            ))?,
            offset: 0,
            length: 8,
            checksum: None,
        })),
    )
    .await?;

    let snapshot = ds.commit("one of each", None).await?;
    repo.create_tag("v1", &snapshot).await?;
    assert_eq!(repo.lookup_tag("v1").await?, snapshot);
    assert_eq!(repo.lookup_branch("main").await?, snapshot);

    let ancestry: Vec<_> = repo.ancestry(&snapshot).await?.try_collect().await?;
    assert_eq!(ancestry[0].id, snapshot);
    assert_eq!(ancestry[0].message, "one of each");
    assert_eq!(ancestry.last().expect("a first snapshot").id, root);

    let r = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    for (coord, expected) in [
        (0u32, Bytes::copy_from_slice(b"inline")),
        (1, Bytes::copy_from_slice(b"materialised")),
        (2, Bytes::copy_from_slice(b"virtual!")),
    ] {
        let got = get_chunk(
            r.get_chunk_reader(&p("/array"), &ChunkIndices(vec![coord]), &ByteRange::ALL)
                .await?,
        )
        .await?;
        assert_eq!(got, Some(expected));
    }
    assert!(r.get_node(&p("/")).await?.user_attributes.is_some());
    Ok(())
}

/// The Zarr-facing key/value store still speaks Zarr v3 keys, lists them one
/// level at a time, and reads a historical version.
#[tokio::test]
async fn gate01__zarr_store_over_versions() -> Result<(), Box<dyn Error>> {
    let repo = mem_repo().await;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"first")).await?;
    let first = session.write().await.commit("first", None).await?;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"second")).await?;
    let second = session.write().await.commit("second", None).await?;

    let mut keys: Vec<String> = store.list().await?.try_collect().await?;
    keys.sort();
    assert_eq!(
        keys,
        vec![
            "array/c/0/0/0".to_string(),
            "array/zarr.json".to_string(),
            "zarr.json".to_string(),
        ]
    );
    let mut dir: Vec<String> = store.list_dir("/").await?.try_collect().await?;
    dir.sort();
    assert_eq!(dir, vec!["array".to_string(), "zarr.json".to_string()]);

    let at_second = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::SnapshotId(second)).await?,
    )))
    .await;
    assert_eq!(
        at_second.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(b"second")
    );

    let at_first = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::SnapshotId(first)).await?,
    )))
    .await;
    assert_eq!(
        at_first.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(b"first")
    );
    Ok(())
}
