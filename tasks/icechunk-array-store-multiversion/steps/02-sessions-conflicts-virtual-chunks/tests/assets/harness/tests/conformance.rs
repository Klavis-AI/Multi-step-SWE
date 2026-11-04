// Verifier-owned conformance driver for step 02 of
// `icechunk-array-store-multiversion`: sessions, commit conflicts and rebase,
// persistent repository configuration, and virtual chunk containers.
//
// Every assertion traces either to a document in /app/icechunk-spec/ (the
// pinned upstream corpus for icechunk 0.1.0-alpha.8 plus its design documents
// and the alpha.1 specification in archive/) or to /app/TEST_INTERFACE.md,
// which states the public Rust surface this milestone must expose. Where a
// behaviour was derived from upstream's own suite the case comment names the
// upstream test it was adapted from.
//
// Group = the `<group>__` prefix of the test function name. The runner invokes
// one `cargo test` per group and asserts the executed case count, so a group's
// cases must be independent of every other group's.
//
// Rules this file follows:
//   * public `icechunk::` API only - nothing from a private module, nothing
//     from `icechunk::strategies` (which is `#[cfg(test)]`);
//   * no network, no cloud, no MinIO: in-memory storage or a `tempfile`
//     directory, and `file://` URLs for virtual chunk references;
//   * deterministic: no sleeps, no unsynchronised concurrency, every
//     collection sorted before it is compared;
//   * error *categories* only - never a `Display` string, never a `Debug`
//     rendering, never a private path.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
#![allow(non_snake_case)]
#![allow(unused_imports, dead_code, unused_macros)]

use std::{
    collections::{HashMap, HashSet},
    error::Error,
    num::NonZeroU64,
    path::PathBuf,
    sync::Arc,
    time::UNIX_EPOCH,
};

use bytes::Bytes;
use futures::TryStreamExt;
use tokio::sync::RwLock;

// ---------------------------------------------------------------------------
// The `icechunk` prelude and the shared fixtures, as macros each case expands.
//
// NOTHING at file scope names anything from the candidate crate, and the reason
// is scoring rather than style. When the whole `conformance` target fails to
// compile, tests/test.sh attributes the failure group by group by removing the
// CASE FUNCTIONS of every other group - and nothing else. Every file-scope
// `use` item, const, struct and helper survives in every slice, so one
// file-scope name a candidate has not implemented would fail every group's
// slice, including the groups that never touch it. A `macro_rules!` body is
// name-resolved only where it is expanded, so here each case pulls in exactly
// the modules and fixtures it uses and a group's slice answers for that group's
// own surface alone.
//
// `ic_*!` are the import macros, one per module of section 4 of
// TEST_INTERFACE.md. `fx_*!` are the shared fixtures: each builds a fresh
// repository on its own storage, and no case observes state produced by
// another case. A fixture macro is expanded beside the import macros it needs.
// ---------------------------------------------------------------------------

macro_rules! ic_storage {
    () => {
        use icechunk::{
            storage::{new_in_memory_storage, new_local_filesystem_storage},
            Storage, StorageError,
        };
    };
}

macro_rules! ic_storage_ctors {
    () => {
        use icechunk::{new_s3_storage, ObjectStorage};
    };
}

macro_rules! ic_repository {
    () => {
        use icechunk::{
            repository::{RepositoryError, VersionInfo},
            Repository,
        };
    };
}

macro_rules! ic_session {
    () => {
        use icechunk::session::{get_chunk, Session, SessionError};
    };
}

macro_rules! ic_session_helpers {
    () => {
        use icechunk::session::{construct_valid_byte_range, is_prefix_match};
    };
}

macro_rules! ic_conflicts {
    () => {
        use icechunk::conflicts::{
            basic_solver::{BasicConflictSolver, VersionSelection},
            detector::ConflictDetector,
            Conflict,
        };
    };
}

macro_rules! ic_config {
    () => {
        use icechunk::{
            config::{
                Credentials, GcsCredentials, GcsStaticCredentials, S3Credentials,
                S3Options, S3StaticCredentials,
            },
            ObjectStoreConfig, RepositoryConfig,
        };
    };
}

macro_rules! ic_virtual_chunks {
    () => {
        use icechunk::virtual_chunks::VirtualChunkContainer;
    };
}

macro_rules! ic_default_containers {
    () => {
        use icechunk::virtual_chunks::mk_default_containers;
    };
}

macro_rules! ic_store {
    () => {
        use icechunk::{store::StoreError, Store};
    };
}

macro_rules! ic_list_dir_item {
    () => {
        use icechunk::store::ListDirItem;
    };
}

macro_rules! ic_gc {
    () => {
        use icechunk::ops::gc::{garbage_collect, Action, GCConfig, GCSummary};
    };
}

macro_rules! ic_checksum {
    () => {
        use icechunk::format::manifest::{Checksum, SecondsSinceEpoch};
    };
}

macro_rules! ic_manifest {
    () => {
        use icechunk::format::manifest::{
            ChunkPayload, VirtualChunkLocation, VirtualChunkRef, VirtualReferenceError,
        };
    };
}

macro_rules! ic_snapshot {
    () => {
        use icechunk::format::snapshot::{NodeData, NodeType, ZarrArrayMetadata};
    };
}

macro_rules! ic_format {
    () => {
        use icechunk::format::{ByteRange, ChunkIndices, Path};
    };
}

macro_rules! ic_change_set {
    () => {
        use icechunk::change_set::ChangeSet;
    };
}

macro_rules! ic_refs {
    () => {
        use icechunk::refs::Ref;
    };
}

macro_rules! ic_metadata {
    () => {
        use icechunk::metadata::{
            ChunkKeyEncoding, ChunkShape, DataType, FillValue, UserAttributes,
        };
    };
}

macro_rules! fx_mem {
    () => {
        fn mem_storage() -> Arc<dyn Storage + Send + Sync> {
            new_in_memory_storage().expect("in-memory storage")
        }

        /// Adapted from `icechunk/src/session.rs :: tests::create_memory_store_repository`.
        async fn mem_repo() -> Repository {
            Repository::create(None, mem_storage(), HashMap::new())
                .await
                .expect("create repository")
        }
    };
}

macro_rules! fx_meta {
    () => {
        /// Adapted from `icechunk/src/session.rs :: tests::basic_meta`.
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

        /// A three-dimensional array whose Zarr key form is `array/c/i/j/k`.
        fn meta_3d() -> ZarrArrayMetadata {
            ZarrArrayMetadata {
                shape: vec![4, 4, 4],
                data_type: DataType::Int32,
                chunk_shape: ChunkShape(vec![
                    NonZeroU64::new(1).unwrap(),
                    NonZeroU64::new(1).unwrap(),
                    NonZeroU64::new(1).unwrap(),
                ]),
                chunk_key_encoding: ChunkKeyEncoding::Slash,
                fill_value: FillValue::Int32(0),
                codecs: vec![],
                storage_transformers: None,
                dimension_names: None,
            }
        }
    };
}

macro_rules! fx_path {
    () => {
        fn p(s: &str) -> Path {
            s.try_into().expect("valid path")
        }
    };
}

macro_rules! fx_conflict_repo {
    () => {
        /// Adapted from `icechunk/src/session.rs :: tests::get_repo_for_conflict`:
        /// group `/foo/bar` and array `/foo/bar/some-array`, already committed.
        async fn repo_for_conflict() -> Result<Repository, Box<dyn Error>> {
            let repo = mem_repo().await;
            let mut ds = repo.writable_session("main").await?;
            ds.add_group(p("/foo/bar")).await?;
            ds.add_array(p("/foo/bar/some-array"), basic_meta()).await?;
            ds.commit("create directory", None).await?;
            Ok(repo)
        }
    };
}

macro_rules! fx_zarr_meta {
    () => {
        const GROUP_META: &[u8] = br#"{"zarr_format":3, "node_type":"group"}"#;

        /// A Zarr v3 array metadata document for a 4x4x4 int32 array with 1x1x1 chunks.
        const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[4,4,4],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"]}"#;

        /// Compare two JSON documents by value, so the order in which a metadata
        /// document's members are emitted is never asserted (PROFILE P4).
        fn assert_same_json(actual: &Bytes, expected: &[u8]) {
            let a: serde_json::Value =
                serde_json::from_slice(actual.as_ref()).expect("the store returned valid JSON");
            let b: serde_json::Value =
                serde_json::from_slice(expected).expect("the fixture is valid JSON");
            assert_eq!(a, b);
        }
    };
}

macro_rules! fx_mtime {
    () => {
        /// The referenced object's **own** last-modified time, in seconds since the
        /// epoch. PROFILE P5: only a checksum's relation to this is ever observed, so
        /// no scored assertion reads a wall clock (TEST_INTERFACE section 7).
        fn object_mtime_secs(path: &str) -> u32 {
            std::fs::metadata(path)
                .expect("the fixture chunk file exists")
                .modified()
                .expect("the platform reports a last-modified time")
                .duration_since(UNIX_EPOCH)
                .expect("a last-modified time after the epoch")
                .as_secs() as u32
        }
    };
}

macro_rules! fx_virtual {
    () => {
        /// A repository on a temp directory that authorises the `file://` prefix, plus
        /// three real chunk files written next to it.
        ///
        /// Adapted from `icechunk/tests/test_virtual_refs.rs :: create_local_repository`
        /// and `write_chunks_to_local_fs`.
        struct LocalVirtualFixture {
            _chunk_dir: tempfile::TempDir,
            _repo_dir: tempfile::TempDir,
            repo: Repository,
            chunk_paths: Vec<String>,
            chunk_bytes: Vec<Bytes>,
        }

        async fn local_virtual_fixture(
            extra_containers: Vec<VirtualChunkContainer>,
            clear_defaults: bool,
        ) -> Result<LocalVirtualFixture, Box<dyn Error>> {
            let chunk_dir = tempfile::TempDir::new()?;
            let contents: Vec<Bytes> = vec![
                Bytes::copy_from_slice(b"first"),
                Bytes::copy_from_slice(b"second0000"),
                Bytes::copy_from_slice(b"modified"),
            ];
            let mut chunk_paths = Vec::new();
            for (i, bytes) in contents.iter().enumerate() {
                let path = chunk_dir.path().join(format!("chunk-{}", i + 1));
                std::fs::write(&path, bytes.as_ref())?;
                chunk_paths.push(path.to_str().expect("utf8 temp path").to_owned());
            }

            let repo_dir = tempfile::TempDir::new()?;
            let storage: Arc<dyn Storage + Send + Sync> =
                new_local_filesystem_storage(repo_dir.path())?;

            let mut config = RepositoryConfig::default();
            if clear_defaults {
                config.clear_virtual_chunk_containers();
            }
            config.set_virtual_chunk_container(VirtualChunkContainer {
                name: "file".to_string(),
                url_prefix: "file://".to_string(),
                store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
            });
            for cont in extra_containers {
                config.set_virtual_chunk_container(cont);
            }

            let repo = Repository::create(Some(config), storage, HashMap::new()).await?;
            Ok(LocalVirtualFixture {
                _chunk_dir: chunk_dir,
                _repo_dir: repo_dir,
                repo,
                chunk_paths,
                chunk_bytes: contents,
            })
        }

        fn file_url(path: &str) -> String {
            format!("file://{}", path)
        }
    };
}

// ===========================================================================
// repo_open_create - Repository::{create, open, open_or_create, exists}
// Sources: design/003-python-instantiation-api.md; guide/icechunk-python-configuration.md
//          "Creating a new repo" / "Opening an existing repo";
//          upstream `icechunk/src/repository.rs :: test_repository_persistent_config`.
// ===========================================================================

/// CFG-06: a repository must be created before it can be opened, and `exists`
/// reports whether the default branch is there.
#[tokio::test]
async fn repo_open_create__open_before_create_fails() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    fx_mem!();
    let storage = mem_storage();
    assert!(!Repository::exists(storage.as_ref()).await?);
    let err = Repository::open(None, Arc::clone(&storage), HashMap::new())
        .await
        .expect_err("opening a repository that does not exist must fail");
    assert!(matches!(err, RepositoryError::RepositoryDoesntExist));

    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert!(Repository::exists(storage.as_ref()).await?);
    assert_eq!(repo.list_branches().await?, HashSet::from(["main".to_string()]));
    Ok(())
}

/// CFG-06: "Icechunk repos cannot be created in the same location where another
/// store already exists."
#[tokio::test]
async fn repo_open_create__creating_twice_is_refused() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    fx_mem!();
    let storage = mem_storage();
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let err = Repository::create(None, Arc::clone(&storage), HashMap::new())
        .await
        .expect_err("a second create on the same location must fail");
    assert!(matches!(err, RepositoryError::AlreadyInitialized));
    Ok(())
}

/// CFG-06: `open_or_create` succeeds whether or not the repository is there,
/// and the second call opens the very same repository.
#[tokio::test]
async fn repo_open_create__open_or_create_is_idempotent() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    fx_mem!();
    let storage = mem_storage();
    let first =
        Repository::open_or_create(None, Arc::clone(&storage), HashMap::new()).await?;
    let tip = first.lookup_branch("main").await?;
    let second =
        Repository::open_or_create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(second.lookup_branch("main").await?, tip);
    assert_eq!(second.list_branches().await?, HashSet::from(["main".to_string()]));
    Ok(())
}

/// A freshly created repository has an empty `main` whose snapshot is readable
/// and carries no nodes; creating it wrote the config object too.
#[tokio::test]
async fn repo_open_create__fresh_repository_is_empty() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    fx_mem!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert!(Repository::fetch_config(storage.as_ref()).await?.is_some());

    let tip = repo.lookup_branch("main").await?;
    let session = repo.readonly_session(&VersionInfo::SnapshotId(tip.clone())).await?;
    assert_eq!(session.snapshot_id(), &tip);
    assert_eq!(session.list_nodes().await?.count(), 0);
    assert_eq!(repo.list_tags().await?, HashSet::new());
    Ok(())
}

/// `add_in_mem_asset_caching` wraps a storage and the wrapped handle still
/// opens the same repository.
#[tokio::test]
async fn repo_open_create__asset_caching_wraps_a_storage() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    fx_mem!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let tip = repo.lookup_branch("main").await?;

    let cached = Repository::add_in_mem_asset_caching(Arc::clone(&storage));
    assert!(Repository::exists(cached.as_ref()).await?);
    let repo2 = Repository::open(None, cached, HashMap::new()).await?;
    assert_eq!(repo2.lookup_branch("main").await?, tip);

    // the repository hands back the storage handle it was opened on, and that
    // handle still reaches the same repository
    assert!(Repository::exists(repo2.storage().as_ref()).await?);
    Ok(())
}

// ===========================================================================
// repo_config - persisted configuration, defaults, wholesale override
// Sources: design/001-virtual-chunk-improvements.md "Global config";
//          design/003-python-instantiation-api.md "Discussion";
//          guide/icechunk-python-configuration.md "Writing chunks inline";
//          upstream `icechunk/src/repository.rs :: test_repository_persistent_config`.
// ===========================================================================

/// CFG-05 / VCC-04: the documented defaults.
#[tokio::test]
async fn repo_config__defaults_are_documented() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    ic_default_containers!();
    fx_mem!();
    let repo = mem_repo().await;
    let cfg = repo.config();
    assert_eq!(cfg, &RepositoryConfig::default());
    assert_eq!(cfg.inline_chunk_threshold_bytes, 512);
    assert!(!cfg.unsafe_overwrite_refs);
    assert_eq!(cfg.get_partial_values_concurrency, 10);

    let mut names: Vec<&str> =
        cfg.virtual_chunk_containers().map(|c| c.name.as_str()).collect();
    names.sort_unstable();
    assert_eq!(names, vec!["az", "file", "gcs", "s3", "tigris"]);
    for cont in cfg.virtual_chunk_containers() {
        assert_eq!(cont.url_prefix, cont.name);
    }
    // and they are exactly the set `mk_default_containers` reports
    assert_eq!(cfg.virtual_chunk_containers, mk_default_containers());
    Ok(())
}

/// CFG-01 / CFG-02: creating a repository persists exactly one configuration
/// object, and it round-trips through `fetch_config`.
#[tokio::test]
async fn repo_config__is_persisted_on_create() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    fx_mem!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let (persisted, etag) = Repository::fetch_config(storage.as_ref())
        .await?
        .expect("create must write the config object");
    assert_eq!(persisted, RepositoryConfig::default());
    assert_ne!(etag, "");
    assert_eq!(repo.config(), &persisted);
    Ok(())
}

/// CFG-03: a caller-supplied config overrides the persisted one for that
/// instance only. CFG-04: `save_config` is what makes the override persistent.
#[tokio::test]
async fn repo_config__caller_override_then_save() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    fx_mem!();
    let storage = mem_storage();
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let overridden = Repository::open(
        Some(RepositoryConfig { inline_chunk_threshold_bytes: 42, ..Default::default() }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    assert_eq!(overridden.config().inline_chunk_threshold_bytes, 42);
    // the override alone changes nothing on storage
    assert_eq!(
        Repository::fetch_config(storage.as_ref()).await?.unwrap().0,
        RepositoryConfig::default()
    );

    let etag = overridden.save_config().await?;
    assert_ne!(etag, "");
    assert_eq!(
        Repository::fetch_config(storage.as_ref()).await?.unwrap().0.inline_chunk_threshold_bytes,
        42
    );

    // a later open with no caller config now reads the saved value
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(reopened.config().inline_chunk_threshold_bytes, 42);
    Ok(())
}

/// The override is **wholesale**, not per field: every other field of the
/// supplied config is what reaches the instance, not the persisted value.
#[tokio::test]
async fn repo_config__override_replaces_the_whole_config() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_config!();
    fx_mem!();
    let storage = mem_storage();
    let saved = RepositoryConfig {
        inline_chunk_threshold_bytes: 11,
        get_partial_values_concurrency: 7,
        ..Default::default()
    };
    let repo =
        Repository::create(Some(saved), Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.config().get_partial_values_concurrency, 7);

    // supply a config that mentions only one field; the rest come from
    // `Default`, not from the persisted object.
    let reopened = Repository::open(
        Some(RepositoryConfig { inline_chunk_threshold_bytes: 99, ..Default::default() }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    assert_eq!(reopened.config().inline_chunk_threshold_bytes, 99);
    assert_eq!(
        reopened.config().get_partial_values_concurrency,
        RepositoryConfig::default().get_partial_values_concurrency
    );
    Ok(())
}

/// CFG-01: the configuration object is written conditionally, so a writer
/// holding a stale view of it cannot clobber a newer one.
#[tokio::test]
async fn repo_config__stale_save_is_refused() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    fx_mem!();
    let storage = mem_storage();
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    // Both instances read the same persisted configuration and each carries a
    // different override, so whichever saves first genuinely changes the
    // stored bytes. What is scored is the conditional write, never how an
    // implementation derives an etag - a content-derived etag is as valid as a
    // generated one (section 4.5, PROFILE P3).
    let first = Repository::open(
        Some(RepositoryConfig {
            get_partial_values_concurrency: 3,
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let second = Repository::open(
        Some(RepositoryConfig { inline_chunk_threshold_bytes: 7, ..Default::default() }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    // `first` saves, which advances the stored etag under `second`'s feet.
    first.save_config().await?;
    let err = second
        .save_config()
        .await
        .expect_err("a save from a stale instance must be refused");
    assert!(matches!(
        err,
        RepositoryError::StorageError(StorageError::ConfigUpdateConflict)
    ));

    // and the refused write left the configuration `first` stored in place
    let stored = Repository::fetch_config(storage.as_ref()).await?.unwrap().0;
    assert_eq!(stored.get_partial_values_concurrency, 3);
    assert_eq!(
        stored.inline_chunk_threshold_bytes,
        RepositoryConfig::default().inline_chunk_threshold_bytes
    );
    Ok(())
}

// ===========================================================================
// repo_reset_branch - Repository::reset_branch
// Sources: guide/spec.md "Reference Files"; VER-01 (Changelog 0.1.0a4);
//          upstream `icechunk/src/store.rs :: test_branch_reset`.
// ===========================================================================

/// VER-01: `reset_branch` repoints a branch head at an earlier snapshot and the
/// data committed after it stops being visible on that branch.
#[tokio::test]
async fn repo_reset_branch__rewinds_the_branch_head() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;

    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/")).await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    let base = ds.commit("root", None).await?;

    let mut ds = repo.writable_session("main").await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("hello".into())),
    )
    .await?;
    let after = ds.commit("write a chunk", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, after);

    repo.reset_branch("main", &base).await?;
    assert_eq!(repo.lookup_branch("main").await?, base);

    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(r.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?, None);
    Ok(())
}

/// The snapshot a branch is reset to must exist.
#[tokio::test]
async fn repo_reset_branch__unknown_snapshot_is_refused() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;
    let tip = ds.commit("a group", None).await?;

    // a well-formed but unwritten snapshot id
    let other = mem_repo().await;
    let mut ods = other.writable_session("main").await?;
    ods.add_group(p("/g")).await?;
    let foreign = ods.commit("elsewhere", None).await?;

    let err = repo
        .reset_branch("main", &foreign)
        .await
        .expect_err("resetting to an unknown snapshot must fail");
    assert!(matches!(err, RepositoryError::SnapshotNotFound { .. }));
    assert_eq!(repo.lookup_branch("main").await?, tip);
    Ok(())
}

/// Resetting forward again restores the later snapshot: the operation is a
/// repoint, not a deletion of the data.
#[tokio::test]
async fn repo_reset_branch__can_move_forward_again() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/")).await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    let base = ds.commit("root", None).await?;

    let mut ds = repo.writable_session("main").await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Inline("world".into())),
    )
    .await?;
    let after = ds.commit("a chunk", None).await?;

    repo.reset_branch("main", &base).await?;
    repo.reset_branch("main", &after).await?;
    assert_eq!(repo.lookup_branch("main").await?, after);
    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        r.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Inline("world".into()))
    );
    Ok(())
}

// ===========================================================================
// session_roles - the Repository / Session / Store split
// Sources: SESS-01..SESS-05 (Changelog 0.1.0a8, guide/icechunk-python-quickstart.md);
//          upstream `icechunk/src/store.rs :: test_access_mode`.
// ===========================================================================

/// SESS-02: a writable session is anchored at the tip of the branch it names.
#[tokio::test]
async fn session_roles__writable_session_is_anchored_to_a_branch(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    ic_change_set!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let tip = repo.lookup_branch("main").await?;
    let mut w = repo.writable_session("main").await?;

    assert_eq!(w.branch(), Some("main"));
    assert!(!w.read_only());
    assert_eq!(w.snapshot_id(), &tip);
    assert!(!w.has_uncommitted_changes());
    // the session carries the repository's configuration, and starts empty
    assert_eq!(w.config(), repo.config());
    assert_eq!(w.changes(), &ChangeSet::default());

    w.add_group(p("/g")).await?;
    assert!(w.has_uncommitted_changes());
    Ok(())
}

/// SESS-05: a read-only session can be opened by branch tip, by tag and by
/// snapshot id, and each reads exactly that version.
#[tokio::test]
async fn session_roles__readonly_session_by_branch_tag_and_snapshot(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/first")).await?;
    let first = ds.commit("first", None).await?;
    repo.create_tag("v1", &first).await?;

    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/second")).await?;
    let second = ds.commit("second", None).await?;

    for (version, expect) in [
        (VersionInfo::SnapshotId(first.clone()), &first),
        (VersionInfo::TagRef("v1".to_string()), &first),
        (VersionInfo::BranchTipRef("main".to_string()), &second),
    ] {
        let s = repo.readonly_session(&version).await?;
        assert!(s.read_only());
        assert_eq!(s.branch(), None);
        assert_eq!(s.snapshot_id(), expect);
    }

    let at_first = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert!(at_first.get_node(&p("/first")).await.is_ok());
    assert!(at_first.get_node(&p("/second")).await.is_err());

    // a well-formed snapshot id this repository never stored is refused
    let other = mem_repo().await;
    let mut ods = other.writable_session("main").await?;
    ods.add_group(p("/elsewhere")).await?;
    let foreign = ods.commit("elsewhere", None).await?;
    assert!(matches!(
        repo.readonly_session(&VersionInfo::SnapshotId(foreign)).await,
        Err(RepositoryError::SnapshotNotFound { .. })
    ));
    Ok(())
}

/// A read-only session can never publish anything: `merge`, `commit` and
/// `rebase` all report the read-only category, and the branch never moves. The
/// per-node recorders are in-memory bookkeeping and are not the boundary the
/// guarantee is enforced at - see TEST_INTERFACE section 4.3.
#[tokio::test]
async fn session_roles__readonly_session_cannot_publish() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_change_set!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;
    ds.add_array(p("/g/a"), basic_meta()).await?;
    let tip = ds.commit("seed", None).await?;

    let mut r =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(r.read_only());
    assert!(matches!(
        r.merge(ChangeSet::default()).await,
        Err(SessionError::ReadOnlySession)
    ));
    assert!(matches!(
        r.commit("nope", None).await,
        Err(SessionError::ReadOnlySession)
    ));
    assert!(matches!(
        r.rebase(&ConflictDetector).await,
        Err(SessionError::ReadOnlySession)
    ));

    // Recording a change locally cannot change that. Whether the recorder
    // itself refuses is deliberately not scored (PROFILE section D6), so its
    // result is ignored; what must hold is that the commit is still refused
    // and that nothing reached storage.
    let _ = r.add_group(p("/h")).await;
    assert!(matches!(
        r.commit("still nope", None).await,
        Err(SessionError::ReadOnlySession)
    ));
    assert_eq!(repo.lookup_branch("main").await?, tip);
    let fresh =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(fresh.get_node(&p("/h")).await.is_err());
    Ok(())
}

/// SESS-01: a `Store` is a view over one session; the session it wraps is the
/// single source of truth and `Store::session` hands it back.
#[tokio::test]
async fn session_roles__store_is_a_view_over_one_session() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_path!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;

    assert!(!store.read_only().await);
    assert!(store.is_empty("").await?);
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    assert!(!store.is_empty("").await?);

    // `Store` is `Clone`, and a clone is one more view over the same session
    let twin = store.clone();
    assert!(!twin.is_empty("").await?);

    // the write is visible on the session the store was built from, and the
    // session handed back by `store.session()` is that same one
    assert!(session.read().await.get_group(&p("/")).await.is_ok());
    let handed_back = store.session();
    assert!(handed_back.read().await.has_uncommitted_changes());

    // committing through the handed-back session is what advances the branch
    let snap = handed_back.write().await.commit("through the session", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

/// SESS-08: a session opened at a tag or a snapshot produces a read-only store,
/// and every mutating store method reports the read-only category.
#[tokio::test]
async fn session_roles__readonly_store_refuses_writes() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_manifest!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    let snap = session.write().await.commit("root group", None).await?;
    repo.create_tag("tagged", &snap).await?;

    let ro = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::TagRef("tagged".to_string())).await?,
    )))
    .await;
    assert!(ro.read_only().await);
    // reads still work
    assert!(ro.exists("zarr.json").await?);
    assert!(matches!(
        ro.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await,
        Err(StoreError::ReadOnly)
    ));
    assert!(matches!(ro.delete("zarr.json").await, Err(StoreError::ReadOnly)));
    assert!(matches!(
        ro.set_if_not_exists("other/zarr.json", Bytes::copy_from_slice(GROUP_META)).await,
        Err(StoreError::ReadOnly)
    ));
    assert!(matches!(
        ro.set_virtual_ref(
            "array/c/0/0/0",
            VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path("s3://b/k")?,
                offset: 0,
                length: 1,
                checksum: None,
            }
        )
        .await,
        Err(StoreError::ReadOnly)
    ));
    Ok(())
}

// ===========================================================================
// session_commit - commit, its conflict pre-check and the self-demotion
// Sources: SESS-03, SESS-04 (guide/icechunk-python-quickstart.md);
//          CONF-01, CONF-02 (design/002-rebase-commit-improvements.md);
//          upstream `icechunk/src/session.rs :: test_no_double_commit`,
//          `test_commit_and_refs`.
// ===========================================================================

/// SESS-03: `commit` returns the new snapshot id and moves the branch to it.
#[tokio::test]
async fn session_commit__returns_the_new_snapshot_id() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let parent = repo.lookup_branch("main").await?;

    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;
    let snap = ds.commit("a group", None).await?;

    assert_ne!(snap, parent);
    assert_eq!(repo.lookup_branch("main").await?, snap);

    let ancestry: Vec<_> = repo.ancestry(&snap).await?.try_collect().await?;
    assert_eq!(ancestry[0].id, snap);
    assert_eq!(ancestry[0].message, "a group");
    assert_eq!(ancestry[1].id, parent);
    Ok(())
}

/// SESS-04: "Once a writable `Session` has been successfully committed to, it
/// becomes read only." The session clears its change set, advances to the new
/// snapshot, and drops its branch.
#[tokio::test]
async fn session_commit__self_demotes_after_success() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut w = repo.writable_session("main").await?;
    w.add_group(p("/g")).await?;
    assert!(w.has_uncommitted_changes());

    let snap = w.commit("root group", None).await?;

    assert!(w.read_only());
    assert_eq!(w.branch(), None);
    assert!(!w.has_uncommitted_changes());
    assert_eq!(w.snapshot_id(), &snap);
    // a demoted session still reads what it committed
    assert!(w.get_group(&p("/g")).await.is_ok());
    Ok(())
}

/// Adapted from upstream `test_no_double_commit`: the same session cannot
/// commit twice, because the first success demoted it.
#[tokio::test]
async fn session_commit__cannot_commit_twice() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut w = repo.writable_session("main").await?;
    w.add_group(p("/g")).await?;
    w.commit("first", None).await?;

    assert!(matches!(
        w.commit("second", None).await,
        Err(SessionError::ReadOnlySession)
    ));
    // and the branch still holds exactly one user commit
    let tip = repo.lookup_branch("main").await?;
    let ancestry: Vec<_> = repo.ancestry(&tip).await?.try_collect().await?;
    assert_eq!(ancestry.len(), 2);
    Ok(())
}

/// CONF-01 / CONF-02: when the branch tip moved, `commit` reports the conflict
/// carrying the expected parent and the actual parent, and writes nothing.
#[tokio::test]
async fn session_commit__conflicting_commit_reports_both_parents(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let base = repo.lookup_branch("main").await?;

    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.add_group(p("/foo/bar/one")).await?;
    let theirs = ds1.commit("theirs", None).await?;

    ds2.add_group(p("/foo/bar/two")).await?;
    match ds2.commit("ours", None).await {
        Err(SessionError::Conflict { expected_parent, actual_parent }) => {
            assert_eq!(expected_parent, Some(base));
            assert_eq!(actual_parent, Some(theirs.clone()));
        }
        other => panic!("expected a commit conflict, got {:?}", other.is_ok()),
    }

    // nothing was written: the branch is still at their commit and our session
    // kept its changes and its branch
    assert_eq!(repo.lookup_branch("main").await?, theirs);
    assert!(!ds2.read_only());
    assert!(ds2.has_uncommitted_changes());
    Ok(())
}

/// A commit with no changes at all is refused, and the branch does not move.
#[tokio::test]
async fn session_commit__empty_commit_is_refused() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    fx_mem!();
    let repo = mem_repo().await;
    let tip = repo.lookup_branch("main").await?;
    let mut ds = repo.writable_session("main").await?;
    assert!(!ds.has_uncommitted_changes());

    assert!(matches!(
        ds.commit("nothing to see", None).await,
        Err(SessionError::NoChangesToCommit)
    ));
    assert_eq!(repo.lookup_branch("main").await?, tip);
    // the failed commit did not demote the session
    assert!(!ds.read_only());
    assert_eq!(ds.branch(), Some("main"));
    Ok(())
}

// ===========================================================================
// session_merge_distributed - the distributed write path
// Sources: DIST-01 (Changelog 0.1.0a5), DIST-02 (guide/icechunk-python-dask.md);
//          upstream `icechunk/tests/test_distributed_writes.rs :: test_distributed_writes`,
//          rewritten for local storage.
// ===========================================================================

/// DIST-01: `merge` folds another session's changes in without committing.
#[tokio::test]
async fn session_merge_distributed__merge_does_not_commit() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut seed = repo.writable_session("main").await?;
    seed.add_group(p("/")).await?;
    seed.add_array(p("/array"), basic_meta()).await?;
    let base = seed.commit("seed", None).await?;

    let mut worker = repo.writable_session("main").await?;
    worker
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("w0".into())),
        )
        .await?;

    let mut main = repo.writable_session("main").await?;
    main.merge(worker.into()).await?;
    assert!(main.has_uncommitted_changes());
    // merging alone moved nothing on storage
    assert_eq!(repo.lookup_branch("main").await?, base);

    let after = main.commit("merged", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, after);
    Ok(())
}

/// The real distributed shape: several writable sessions write disjoint chunks,
/// are marshalled to bytes as a remote worker would, and are merged into one
/// coordinating session that commits once.
#[tokio::test]
async fn session_merge_distributed__marshalled_sessions_merge_into_one_commit(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let repo = Repository::create(None, storage, HashMap::new()).await?;

    let mut seed = repo.writable_session("main").await?;
    seed.add_group(p("/")).await?;
    seed.add_array(p("/array"), basic_meta()).await?;
    seed.commit("seed", None).await?;

    let mut marshalled: Vec<Vec<u8>> = Vec::new();
    for coord in [0u32, 1, 2] {
        let mut s = repo.writable_session("main").await?;
        let payload = s.get_chunk_writer()(Bytes::from(format!("c{}", coord))).await?;
        s.set_chunk_ref(p("/array"), ChunkIndices(vec![coord]), Some(payload)).await?;
        marshalled.push(s.as_bytes()?);
    }

    let mut main = repo.writable_session("main").await?;
    for raw in marshalled {
        let remote = Session::from_bytes(raw)?;
        main.merge(remote.into()).await?;
    }
    let after = main.commit("distributed commit", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, after);

    let r = repo.readonly_session(&VersionInfo::SnapshotId(after)).await?;
    for coord in [0u32, 1, 2] {
        let got =
            get_chunk(r.get_chunk_reader(&p("/array"), &ChunkIndices(vec![coord]), &ByteRange::ALL).await?)
                .await?;
        assert_eq!(got, Some(Bytes::from(format!("c{}", coord))));
    }
    Ok(())
}

/// `discard_changes` empties the session and hands the change set back, so it
/// can be merged somewhere else instead.
#[tokio::test]
async fn session_merge_distributed__discard_changes_hands_them_back(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut seed = repo.writable_session("main").await?;
    seed.add_group(p("/")).await?;
    seed.add_array(p("/array"), basic_meta()).await?;
    seed.commit("seed", None).await?;

    let mut donor = repo.writable_session("main").await?;
    donor
        .set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![3]),
            Some(ChunkPayload::Inline("moved".into())),
        )
        .await?;
    assert!(donor.has_uncommitted_changes());

    let changes = donor.discard_changes();
    assert!(!donor.has_uncommitted_changes());

    let mut receiver = repo.writable_session("main").await?;
    receiver.merge(changes).await?;
    let snap = receiver.commit("adopted", None).await?;

    let r = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert_eq!(
        r.get_chunk_ref(&p("/array"), &ChunkIndices(vec![3])).await?,
        Some(ChunkPayload::Inline("moved".into()))
    );
    Ok(())
}

/// A session survives a msgpack round trip: its branch, its snapshot and its
/// pending changes all come back.
#[tokio::test]
async fn session_merge_distributed__session_round_trips_through_bytes(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let repo = Repository::create(None, storage, HashMap::new()).await?;

    let mut seed = repo.writable_session("main").await?;
    seed.add_group(p("/")).await?;
    seed.add_array(p("/array"), basic_meta()).await?;
    let base = seed.commit("seed", None).await?;

    let mut ds = repo.writable_session("main").await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![2]),
        Some(ChunkPayload::Inline("kept".into())),
    )
    .await?;

    let revived = Session::from_bytes(ds.as_bytes()?)?;
    assert_eq!(revived.branch(), Some("main"));
    assert!(!revived.read_only());
    assert_eq!(revived.snapshot_id(), &base);
    assert!(revived.has_uncommitted_changes());
    assert_eq!(
        revived.get_chunk_ref(&p("/array"), &ChunkIndices(vec![2])).await?,
        Some(ChunkPayload::Inline("kept".into()))
    );
    Ok(())
}

// ===========================================================================
// conflict_new_node - "node created twice"
// Sources: CONF-06 (conflict taxonomy); upstream
//          `icechunk/src/session.rs :: test_conflict_detection_node_conflict_with_existing_node`
//          and `test_conflict_detection_node_conflict_in_path`.
// ===========================================================================

#[tokio::test]
async fn conflict_new_node__same_path_created_twice() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/conflict");
    ds1.add_group(path.clone()).await?;
    ds1.commit("create group", None).await?;

    ds2.add_array(path.clone(), basic_meta()).await?;
    assert!(matches!(
        ds2.commit("create array", None).await,
        Err(SessionError::Conflict { .. })
    ));

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts.contains(&Conflict::NewNodeConflictsWithExistingNode(path)));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

#[tokio::test]
async fn conflict_new_node__new_node_lands_inside_a_new_array(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let conflict_path = p("/foo/bar/conflict");
    ds1.add_array(conflict_path.clone(), basic_meta()).await?;
    ds1.commit("create array", None).await?;

    ds2.add_array(p("/foo/bar/conflict/inner"), basic_meta()).await?;
    ds2.commit("create inner array", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts.contains(&Conflict::NewNodeInInvalidGroup(conflict_path)));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

/// Neither node-creation conflict is recoverable by policy: the basic solver
/// fails on both no matter how it is configured.
#[tokio::test]
async fn conflict_new_node__is_never_recoverable() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/conflict");
    ds1.add_group(path.clone()).await?;
    ds1.commit("create group", None).await?;

    ds2.add_array(path, basic_meta()).await?;
    ds2.commit("create array", None).await.unwrap_err();

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseTheirs,
        on_user_attributes_conflict: VersionSelection::UseTheirs,
        fail_on_delete_of_updated_array: false,
        fail_on_delete_of_updated_group: false,
    };
    assert!(matches!(
        ds2.rebase(&solver).await,
        Err(SessionError::RebaseFailed { .. })
    ));
    Ok(())
}

// ===========================================================================
// conflict_metadata - "array metadata updated concurrently"
// Sources: CONF-06; upstream
//          `icechunk/src/session.rs :: test_conflict_detection_double_zarr_metadata_edit`.
// ===========================================================================

#[tokio::test]
async fn conflict_metadata__double_zarr_metadata_update() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.update_array(path.clone(), basic_meta()).await?;
    ds1.commit("update array", None).await?;

    ds2.update_array(path.clone(), basic_meta()).await?;
    ds2.commit("update array again", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts.contains(&Conflict::ZarrMetadataDoubleUpdate(path)));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

/// A metadata double update can never be repaired by the basic solver.
#[tokio::test]
async fn conflict_metadata__is_never_recoverable() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.update_array(path.clone(), basic_meta()).await?;
    ds1.commit("update array", None).await?;
    ds2.update_array(path, basic_meta()).await?;
    ds2.commit("update array again", None).await.unwrap_err();

    assert!(matches!(
        ds2.rebase(&BasicConflictSolver::default()).await,
        Err(SessionError::RebaseFailed { .. })
    ));
    // the failed rebase left the session's own changes in place
    assert!(ds2.has_uncommitted_changes());
    Ok(())
}

/// The mirror direction: we delete an array whose metadata they updated.
#[tokio::test]
async fn conflict_metadata__delete_of_an_updated_array() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.update_array(path.clone(), basic_meta()).await?;
    ds1.commit("update array", None).await?;

    let node_id = ds2.get_node(&path).await?.id;
    ds2.delete_array(path.clone()).await?;
    ds2.commit("delete array", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .contains(&Conflict::DeleteOfUpdatedArray { path, node_id }));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

// ===========================================================================
// conflict_deleted - "node deleted while written"
// Sources: CONF-06; upstream
//          `icechunk/src/session.rs :: test_conflict_detection_metadata_edit_of_deleted`,
//          `test_conflict_detection_user_atts_edit_of_deleted`,
//          `test_conflict_detection_delete_when_chunks_updated`,
//          `test_conflict_detection_delete_when_group_user_atts_updated`.
// ===========================================================================

#[tokio::test]
async fn conflict_deleted__metadata_update_of_a_deleted_array(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.delete_array(path.clone()).await?;
    ds1.commit("delete array", None).await?;

    ds2.update_array(path.clone(), basic_meta()).await?;
    ds2.commit("update array", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .contains(&Conflict::ZarrMetadataUpdateOfDeletedArray(path)));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

#[tokio::test]
async fn conflict_deleted__user_attributes_update_of_a_deleted_node(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.delete_array(path.clone()).await?;
    ds1.commit("delete array", None).await?;

    ds2.set_user_attributes(
        path.clone(),
        Some(UserAttributes::try_new(br#"{"foo":"bar"}"#)?),
    )
    .await?;
    ds2.commit("update user attributes", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .contains(&Conflict::UserAttributesUpdateOfDeletedNode(path)));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

#[tokio::test]
async fn conflict_deleted__delete_of_an_array_whose_chunks_they_wrote(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("hello".into())),
    )
    .await?;
    ds1.commit("update chunks", None).await?;

    let node_id = ds2.get_node(&path).await?.id;
    ds2.delete_array(path.clone()).await?;
    ds2.commit("delete array", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .contains(&Conflict::DeleteOfUpdatedArray { path, node_id }));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

#[tokio::test]
async fn conflict_deleted__delete_of_an_updated_group() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar");
    ds1.set_user_attributes(
        path.clone(),
        Some(UserAttributes::try_new(br#"{"foo":"bar"}"#)?),
    )
    .await?;
    ds1.commit("update user attributes", None).await?;

    let node_id = ds2.get_node(&path).await?.id;
    ds2.delete_group(path.clone()).await?;
    ds2.commit("delete group", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .contains(&Conflict::DeleteOfUpdatedGroup { path, node_id }));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

/// The two delete-side conflicts are recoverable by policy: with the default
/// solver the delete wins and the rebase succeeds.
#[tokio::test]
async fn conflict_deleted__delete_conflicts_are_policy_recoverable(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    ds1.set_user_attributes(
        path.clone(),
        Some(UserAttributes::try_new(br#"{"foo":"bar"}"#)?),
    )
    .await?;
    ds1.commit("update user attributes", None).await?;

    ds2.delete_array(path.clone()).await?;
    ds2.commit("delete array", None).await.unwrap_err();

    // default policy does not fail on a delete of an updated array
    let solver = BasicConflictSolver::default();
    assert!(!solver.fail_on_delete_of_updated_array);
    ds2.rebase(&solver).await?;
    ds2.commit("delete array after rebase", None).await?;

    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(r.get_node(&path).await.is_err());
    Ok(())
}

/// Chunks written into an array the intervening commit deleted are their own
/// class, and no configuration of the basic solver repairs it.
#[tokio::test]
async fn conflict_deleted__chunks_written_into_a_deleted_array(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    let path = p("/foo/bar/some-array");
    let node_id = ds2.get_node(&path).await?.id;

    ds1.delete_array(path.clone()).await?;
    ds1.commit("delete array", None).await?;

    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ds2.commit("write a chunk", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts.contains(&Conflict::ChunksUpdatedInDeletedArray {
                path,
                node_id,
            }));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    // the most permissive basic solver still fails on it
    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseTheirs,
        on_user_attributes_conflict: VersionSelection::UseTheirs,
        fail_on_delete_of_updated_array: false,
        fail_on_delete_of_updated_group: false,
    };
    assert!(matches!(
        ds2.rebase(&solver).await,
        Err(SessionError::RebaseFailed { .. })
    ));
    Ok(())
}

/// The two `fail_on_delete_of_*` flags turn a delete-side conflict the default
/// policy repairs into a failure, one flag per node kind.
#[tokio::test]
async fn conflict_deleted__fail_on_delete_flags_refuse_the_repair(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    // an array whose metadata they updated, refused because the flag is set
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;
    ds1.update_array(path.clone(), basic_meta()).await?;
    ds1.commit("update array", None).await?;
    ds2.delete_array(path.clone()).await?;
    ds2.commit("delete array", None).await.unwrap_err();

    let strict = BasicConflictSolver {
        fail_on_delete_of_updated_array: true,
        ..Default::default()
    };
    match ds2.rebase(&strict).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .iter()
                .any(|c| matches!(c, Conflict::DeleteOfUpdatedArray { .. })));
        }
        other => panic!("expected a rebase failure, got ok={}", other.is_ok()),
    }

    // a group whose attributes they updated: refused with the flag set, and
    // repaired - the delete wins - with the documented default of `false`
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;
    ds.commit("a group", None).await?;

    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;
    ds1.set_user_attributes(p("/g"), Some(UserAttributes::try_new(br#"{"n":1}"#)?))
        .await?;
    ds1.commit("their attributes", None).await?;
    ds2.delete_group(p("/g")).await?;
    ds2.commit("delete group", None).await.unwrap_err();

    let strict = BasicConflictSolver {
        fail_on_delete_of_updated_group: true,
        ..Default::default()
    };
    match ds2.rebase(&strict).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .iter()
                .any(|c| matches!(c, Conflict::DeleteOfUpdatedGroup { .. })));
        }
        other => panic!("expected a rebase failure, got ok={}", other.is_ok()),
    }

    let lenient = BasicConflictSolver::default();
    assert!(!lenient.fail_on_delete_of_updated_group);
    ds2.rebase(&lenient).await?;
    ds2.commit("delete group after rebase", None).await?;
    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(r.get_node(&p("/g")).await.is_err());
    Ok(())
}

// ===========================================================================
// conflict_chunks - "same chunk written twice"
// Sources: CONF-05 (conflicted chunk indices are inspectable); upstream
//          `icechunk/src/session.rs :: test_rebase_without_fast_forward`.
// ===========================================================================

#[tokio::test]
async fn conflict_chunks__same_chunk_written_twice() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("theirs".into())),
    )
    .await?;
    let conflicting_snap = ds1.commit("write chunk 0", None).await?;

    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    assert!(matches!(
        ds2.commit("write chunk 0", None).await,
        Err(SessionError::Conflict { .. })
    ));

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { snapshot, conflicts }) => {
            // CONF-04: the report names the commit the conflict arose at
            assert_eq!(snapshot, conflicting_snap);
            assert_eq!(conflicts.len(), 1);
            // CONF-05: and the conflicted chunk coordinates
            assert!(conflicts.contains(&Conflict::ChunkDoubleUpdate {
                path,
                node_id: ds2.get_node(&p("/foo/bar/some-array")).await?.id,
                chunk_coordinates: HashSet::from([ChunkIndices(vec![0])]),
            }));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    Ok(())
}

/// Writing *different* chunks of the same array is not a conflict: the rebase
/// fast-forwards and both writes survive.
#[tokio::test]
async fn conflict_chunks__disjoint_chunks_do_not_conflict() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("theirs".into())),
    )
    .await?;
    ds1.commit("write chunk 0", None).await?;

    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ds2.commit("write chunk 1", None).await.unwrap_err();

    ds2.rebase(&ConflictDetector).await?;
    ds2.commit("write chunk 1 after rebase", None).await?;

    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        r.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline("theirs".into()))
    );
    assert_eq!(
        r.get_chunk_ref(&path, &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Inline("ours".into()))
    );
    Ok(())
}

/// Chunks written into an array whose metadata they updated is its own class,
/// and it is never recoverable.
#[tokio::test]
async fn conflict_chunks__chunks_in_an_updated_array() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.update_array(path.clone(), basic_meta()).await?;
    ds1.commit("update metadata", None).await?;

    let node_id = ds2.get_node(&path).await?.id;
    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ds2.commit("write a chunk", None).await.unwrap_err();

    match ds2.rebase(&ConflictDetector).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts.contains(&Conflict::ChunksUpdatedInUpdatedArray {
                path,
                node_id,
            }));
        }
        other => panic!("expected a rebase conflict, got ok={}", other.is_ok()),
    }
    assert!(matches!(
        ds2.rebase(&BasicConflictSolver::default()).await,
        Err(SessionError::RebaseFailed { .. })
    ));
    Ok(())
}

// ===========================================================================
// rebase_success - a rebase that resolves every conflict
// Sources: CONF-03, CONF-07, CONF-09 (design/002-rebase-commit-improvements.md);
//          upstream `icechunk/src/session.rs ::
//          test_conflict_resolution_success_through_multiple_commits`,
//          `test_conflict_resolution_double_user_atts_edit_with_theirs`.
// ===========================================================================

/// CONF-07: rebase is a no-op when the branch tip is already the session's
/// parent, and the following commit succeeds.
#[tokio::test]
async fn rebase_success__is_a_noop_when_nothing_moved() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds = repo.writable_session("main").await?;
    ds.set_chunk_ref(
        p("/foo/bar/some-array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("only writer".into())),
    )
    .await?;

    ds.rebase(&ConflictDetector).await?;
    let snap = ds.commit("after a no-op rebase", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

/// CONF-03 / CONF-07: `UseTheirs` resolves a chunk conflict, and the rebase
/// fast-forwards over several intervening commits, oldest first.
#[tokio::test]
async fn rebase_success__use_theirs_over_multiple_commits() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds2 = repo.writable_session("main").await?;

    for coord in [0u32, 1, 2] {
        let mut ds1 = repo.writable_session("main").await?;
        ds1.set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![coord]),
            Some(ChunkPayload::Inline("repo 1".into())),
        )
        .await?;
        ds1.commit(&format!("update chunk {}", coord), None).await?;
    }
    for coord in [0u32, 1, 2] {
        ds2.set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![coord]),
            Some(ChunkPayload::Inline("repo 2".into())),
        )
        .await?;
    }
    // One coordinate nobody else touched, so that after `UseTheirs` drops the
    // three conflicting writes the session still carries a real change. This
    // case is about the resolution, not about what `commit` does with an empty
    // change set - that question belongs to the NoChangesToCommit rule stated
    // in section 4, and nothing here should depend on how an implementation
    // bookkeeps a per-node bucket the solver has just emptied.
    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![3]),
        Some(ChunkPayload::Inline("repo 2".into())),
    )
    .await?;
    ds2.commit("update chunks on repo 2", None).await.unwrap_err();

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseTheirs,
        ..Default::default()
    };
    ds2.rebase(&solver).await?;
    ds2.commit("after conflict", None).await?;

    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    for coord in [0u32, 1, 2] {
        assert_eq!(
            r.get_chunk_ref(&path, &ChunkIndices(vec![coord])).await?,
            Some(ChunkPayload::Inline("repo 1".into()))
        );
    }
    Ok(())
}

/// CONF-09: `UseOurs` is the default for a chunk conflict and keeps our value.
#[tokio::test]
async fn rebase_success__use_ours_keeps_our_chunk() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("theirs".into())),
    )
    .await?;
    ds1.commit("theirs", None).await?;

    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ds2.commit("ours", None).await.unwrap_err();

    let solver = BasicConflictSolver::default();
    assert_eq!(solver.on_chunk_conflict, VersionSelection::UseOurs);
    ds2.rebase(&solver).await?;
    ds2.commit("ours after rebase", None).await?;

    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        r.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline("ours".into()))
    );
    Ok(())
}

/// CONF-09: the same two policies apply to a user-attributes conflict. Under
/// `UseTheirs` the repair *undoes* our update, so the session must carry some
/// other change or there would be nothing left to commit - upstream's own test
/// adds exactly such a change for the same reason.
#[tokio::test]
async fn rebase_success__user_attributes_policy() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    for (selection, expected) in [
        (VersionSelection::UseOurs, br#"{"repo":2}"#.as_slice()),
        (VersionSelection::UseTheirs, br#"{"repo":1}"#.as_slice()),
    ] {
        let repo = repo_for_conflict().await?;
        let path = p("/foo/bar/some-array");
        let mut ds1 = repo.writable_session("main").await?;
        let mut ds2 = repo.writable_session("main").await?;

        ds1.set_user_attributes(
            path.clone(),
            Some(UserAttributes::try_new(br#"{"repo":1}"#)?),
        )
        .await?;
        ds1.commit("theirs", None).await?;

        ds2.add_group(p("/baz")).await?;
        ds2.set_user_attributes(
            path.clone(),
            Some(UserAttributes::try_new(br#"{"repo":2}"#)?),
        )
        .await?;
        ds2.commit("ours", None).await.unwrap_err();

        let solver = BasicConflictSolver {
            on_user_attributes_conflict: selection,
            ..Default::default()
        };
        ds2.rebase(&solver).await?;
        ds2.commit("after rebase", None).await?;

        let r =
            repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
        let node = r.get_node(&path).await?;
        let atts = node.user_attributes.expect("user attributes are set");
        assert_eq!(
            atts,
            icechunk::format::snapshot::UserAttributesSnapshot::Inline(
                UserAttributes::try_new(expected)?
            )
        );
        // the unrelated change the session also carried survived the rebase
        assert!(r.get_group(&p("/baz")).await.is_ok());
    }
    Ok(())
}

// ===========================================================================
// rebase_failure - a rebase that reports a classified conflict
// Sources: CONF-04, CONF-07, CONF-08; upstream `icechunk/src/session.rs ::
//          test_conflict_resolution_failure_in_multiple_commits`.
// ===========================================================================

/// CONF-04 / CONF-07: the rebase fast-forwards what it can, then reports the
/// commit it stopped at; the session is left ready to commit on top of the last
/// commit it did fast-forward over.
#[tokio::test]
async fn rebase_failure__partial_fast_forward_then_report() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds2 = repo.writable_session("main").await?;

    let mut ds1 = repo.writable_session("main").await?;
    ds1.set_user_attributes(
        path.clone(),
        Some(UserAttributes::try_new(br#"{"repo":1}"#)?),
    )
    .await?;
    let non_conflicting_snap = ds1.commit("update user atts", None).await?;

    let mut ds1 = repo.writable_session("main").await?;
    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("repo 1".into())),
    )
    .await?;
    let conflicting_snap = ds1.commit("update chunk ref", None).await?;

    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("repo 2".into())),
    )
    .await?;
    ds2.commit("update chunk ref", None).await.unwrap_err();

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::Fail,
        ..Default::default()
    };
    match ds2.rebase(&solver).await {
        Err(SessionError::RebaseFailed { snapshot, conflicts }) => {
            assert_eq!(snapshot, conflicting_snap);
            assert_eq!(conflicts.len(), 1);
            assert!(matches!(conflicts[0], Conflict::ChunkDoubleUpdate { .. }));
        }
        other => panic!("expected a rebase failure, got ok={}", other.is_ok()),
    }
    assert_eq!(ds2.snapshot_id(), &non_conflicting_snap);
    Ok(())
}

/// CONF-08: `ConflictDetector` only detects. It never patches, so it fails even
/// on a conflict class the basic solver would have repaired.
#[tokio::test]
async fn rebase_failure__detector_never_patches() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("theirs".into())),
    )
    .await?;
    ds1.commit("theirs", None).await?;
    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ds2.commit("ours", None).await.unwrap_err();

    assert!(matches!(
        ds2.rebase(&ConflictDetector).await,
        Err(SessionError::RebaseFailed { .. })
    ));
    // the very same situation is repairable by the basic solver
    ds2.rebase(&BasicConflictSolver::default()).await?;
    ds2.commit("ours after rebase", None).await?;
    Ok(())
}

/// `VersionSelection::Fail` turns an otherwise repairable chunk conflict into a
/// failure, and the session keeps its unmodified change set.
#[tokio::test]
async fn rebase_failure__fail_policy_keeps_our_changes() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("theirs".into())),
    )
    .await?;
    ds1.commit("theirs", None).await?;
    ds2.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("ours".into())),
    )
    .await?;
    ds2.commit("ours", None).await.unwrap_err();

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::Fail,
        ..Default::default()
    };
    assert!(matches!(
        ds2.rebase(&solver).await,
        Err(SessionError::RebaseFailed { .. })
    ));
    assert!(ds2.has_uncommitted_changes());
    assert_eq!(
        ds2.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline("ours".into()))
    );
    // a second rebase with a policy that can repair it still succeeds
    ds2.rebase(&BasicConflictSolver::default()).await?;
    ds2.commit("ours after rebase", None).await?;
    Ok(())
}

/// `VersionSelection::Fail` on the user-attributes knob refuses a conflict the
/// default policy repairs, and leaves the session able to try again.
#[tokio::test]
async fn rebase_failure__user_attributes_fail_policy() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let path = p("/foo/bar/some-array");
    let mut ds1 = repo.writable_session("main").await?;
    let mut ds2 = repo.writable_session("main").await?;

    ds1.set_user_attributes(
        path.clone(),
        Some(UserAttributes::try_new(br#"{"repo":1}"#)?),
    )
    .await?;
    ds1.commit("theirs", None).await?;

    ds2.set_user_attributes(
        path.clone(),
        Some(UserAttributes::try_new(br#"{"repo":2}"#)?),
    )
    .await?;
    ds2.commit("ours", None).await.unwrap_err();

    let strict = BasicConflictSolver {
        on_user_attributes_conflict: VersionSelection::Fail,
        ..Default::default()
    };
    match ds2.rebase(&strict).await {
        Err(SessionError::RebaseFailed { conflicts, .. }) => {
            assert!(conflicts
                .iter()
                .any(|c| matches!(c, Conflict::UserAttributesDoubleUpdate { .. })));
        }
        other => panic!("expected a rebase failure, got ok={}", other.is_ok()),
    }

    // the default keeps our version and the rebase then succeeds
    let lenient = BasicConflictSolver::default();
    assert_eq!(lenient.on_user_attributes_conflict, VersionSelection::UseOurs);
    ds2.rebase(&lenient).await?;
    ds2.commit("ours after rebase", None).await?;
    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(
        r.get_node(&path).await?.user_attributes,
        Some(icechunk::format::snapshot::UserAttributesSnapshot::Inline(
            UserAttributes::try_new(br#"{"repo":2}"#)?
        ))
    );
    Ok(())
}

/// A rebase of a session that is already committed is refused, because that
/// session is read-only.
#[tokio::test]
async fn rebase_failure__committed_session_cannot_rebase() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_conflicts!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    fx_conflict_repo!();
    let repo = repo_for_conflict().await?;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/foo/bar/extra")).await?;
    ds.commit("extra", None).await?;

    assert!(matches!(
        ds.rebase(&BasicConflictSolver::default()).await,
        Err(SessionError::ReadOnlySession)
    ));
    Ok(())
}

// ===========================================================================
// virtual_containers - virtual chunk containers and URL-prefix authorisation
// Sources: VCC-01..VCC-06, CRED-02 (design/001-virtual-chunk-improvements.md);
//          upstream `icechunk/tests/test_virtual_refs.rs ::
//          test_repository_with_local_virtual_refs`.
// ===========================================================================

/// VCC-01 / VCC-05 / CHK-03: a container authorising `file://` makes a virtual
/// reference into a temp directory resolvable, with and without a byte range.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_containers__file_prefix_container_resolves(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let mut ds = fx.repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_absolute_path(&file_url(
                &fx.chunk_paths[0],
            ))?,
            offset: 0,
            length: 5,
            checksum: None,
        })),
    )
    .await?;

    let got = get_chunk(
        ds.get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL)
            .await?,
    )
    .await?;
    assert_eq!(got, Some(fx.chunk_bytes[0].clone()));

    for range in [
        ByteRange::bounded(0, 3),
        ByteRange::from_offset(2),
        ByteRange::to_offset(4),
        ByteRange::Last(2),
    ] {
        let sliced = get_chunk(
            ds.get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &range).await?,
        )
        .await?;
        assert_eq!(sliced, Some(range.slice(fx.chunk_bytes[0].clone())));
    }
    Ok(())
}

/// VCC-06: a reference stores only its location; the offset and length are
/// applied at read time, so a slice of a larger file is what comes back.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_containers__offset_and_length_apply_at_read_time(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let mut ds = fx.repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_absolute_path(&file_url(
                &fx.chunk_paths[1],
            ))?,
            offset: 1,
            length: 5,
            checksum: None,
        })),
    )
    .await?;

    let got = get_chunk(
        ds.get_chunk_reader(&p("/array"), &ChunkIndices(vec![1]), &ByteRange::ALL)
            .await?,
    )
    .await?;
    assert_eq!(got, Some(Bytes::copy_from_slice(&fx.chunk_bytes[1][1..6])));

    // the stored payload is the reference itself, not the bytes
    match ds.get_chunk_ref(&p("/array"), &ChunkIndices(vec![1])).await? {
        Some(ChunkPayload::Virtual(r)) => {
            assert_eq!(r.offset, 1);
            assert_eq!(r.length, 5);
            assert_eq!(r.checksum, None);
            assert!(r.location.0.starts_with("file://"));
        }
        other => panic!("expected a virtual payload, got some={}", other.is_some()),
    }
    Ok(())
}

/// VCC-02: containers are matched longest prefix first, so the most specific
/// container wins. Here only the more specific container is usable, and the
/// generic one would have sent the read somewhere unimplemented.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_containers__longest_prefix_wins() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    fx_virtual!();
    let chunk_dir = tempfile::TempDir::new()?;
    let inner = chunk_dir.path().join("inner");
    std::fs::create_dir_all(&inner)?;
    let specific_path = inner.join("chunk").to_str().expect("utf8").to_owned();
    std::fs::write(&specific_path, b"deep!")?;

    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;

    let mut config = RepositoryConfig::default();
    config.clear_virtual_chunk_containers();
    // the generic `file://` container points at an in-memory store, whose
    // virtual chunk backend does not exist at this release; the specific one
    // is the local filesystem. Only longest-prefix-first matching can read the
    // chunk.
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "generic".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::InMemory,
    });
    let specific_prefix = file_url(inner.to_str().expect("utf8"));
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "specific".to_string(),
        url_prefix: specific_prefix.clone(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    });
    let repo = Repository::create(Some(config), storage, HashMap::new()).await?;

    let mut ds = repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_absolute_path(&file_url(
                &specific_path,
            ))?,
            offset: 0,
            length: 5,
            checksum: None,
        })),
    )
    .await?;

    let got = get_chunk(
        ds.get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL)
            .await?,
    )
    .await?;
    assert_eq!(got, Some(Bytes::copy_from_slice(b"deep!")));
    Ok(())
}

/// VCC-03: with no container whose prefix matches, the fetch fails and says so.
/// The reference itself was stored: resolution happens at read time (VCC-06).
#[tokio::test(flavor = "multi_thread")]
async fn virtual_containers__unmatched_prefix_fails_at_read(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], true).await?;
    let mut ds = fx.repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;

    // authorised: the fixture always installs a `file://` container
    let unauthorised = VirtualChunkLocation::from_absolute_path(
        "s3://some-bucket/path/to/a/chunk",
    )?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location: unauthorised.clone(),
            offset: 0,
            length: 5,
            checksum: None,
        })),
    )
    .await?;
    // storing it succeeded
    assert!(matches!(
        ds.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Virtual(_))
    ));

    let err = get_chunk(
        ds.get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL)
            .await?,
    )
    .await
    .expect_err("no container authorises this url");
    assert!(matches!(
        err,
        SessionError::VirtualReferenceError(VirtualReferenceError::NoContainerForUrl(_))
    ));
    Ok(())
}

/// CRED-02: credentials are supplied per container name and are validated
/// against the container's store type when the repository is opened. A
/// local-filesystem container accepts none.
#[tokio::test]
async fn virtual_containers__credentials_are_validated_per_container(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    ic_virtual_chunks!();
    fx_mem!();
    let storage = mem_storage();
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "file".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    });

    let s3_creds = Credentials::S3(S3Credentials::Static(S3StaticCredentials {
        access_key_id: "not-used".to_string(),
        secret_access_key: "not-used".to_string(),
        session_token: None,
        expires_after: None,
    }));

    // a container backed by the local filesystem takes no credentials at all
    let err = Repository::create(
        Some(config.clone()),
        storage,
        HashMap::from([("file".to_string(), s3_creds.clone())]),
    )
    .await
    .expect_err("a local-filesystem container must reject credentials");
    assert!(matches!(err, RepositoryError::StorageError(StorageError::Other(_))));

    // the S3 container in the default set does take S3 credentials
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "s3".to_string(),
        url_prefix: "s3://".to_string(),
        store: ObjectStoreConfig::S3(S3Options {
            region: Some("us-east-1".to_string()),
            endpoint_url: None,
            anonymous: false,
            allow_http: false,
        }),
    });
    Repository::create(
        Some(config.clone()),
        mem_storage(),
        HashMap::from([("s3".to_string(), s3_creds.clone())]),
    )
    .await?;

    // a GCS-backed container takes GCS credentials. The container is declared
    // here rather than taken from the default set, because what backs each
    // default container is not part of this milestone's contract - only the
    // five names and prefixes are (section 4.6, PROFILE D5).
    config.set_virtual_chunk_container(VirtualChunkContainer {
        name: "gcs".to_string(),
        url_prefix: "gcs".to_string(),
        store: ObjectStoreConfig::Gcs(HashMap::new()),
    });
    let gcs_creds = Credentials::Gcs(GcsCredentials::Static(
        GcsStaticCredentials::ServiceAccountKey("{}".to_string()),
    ));
    Repository::create(
        Some(config.clone()),
        mem_storage(),
        HashMap::from([("gcs".to_string(), gcs_creds.clone())]),
    )
    .await?;

    // a mismatched pair is refused: GCS credentials on the S3-backed container
    let err = Repository::create(
        Some(config.clone()),
        mem_storage(),
        HashMap::from([("s3".to_string(), gcs_creds.clone())]),
    )
    .await
    .expect_err("gcs credentials on an s3 container must be refused");
    assert!(matches!(err, RepositoryError::StorageError(StorageError::Other(_))));

    // a container backed by an in-memory store takes no credentials either
    let mut in_mem = config.clone();
    in_mem.set_virtual_chunk_container(VirtualChunkContainer {
        name: "inmem".to_string(),
        url_prefix: "memory://".to_string(),
        store: ObjectStoreConfig::InMemory,
    });
    let err = Repository::create(
        Some(in_mem),
        mem_storage(),
        HashMap::from([("inmem".to_string(), s3_creds.clone())]),
    )
    .await
    .expect_err("an in-memory container must reject credentials");
    assert!(matches!(err, RepositoryError::StorageError(StorageError::Other(_))));

    // credentials named for a container the configuration does not have are
    // ignored - even credentials that would not have validated anywhere
    Repository::create(
        Some(config),
        mem_storage(),
        HashMap::from([("no-such-container".to_string(), s3_creds)]),
    )
    .await?;
    Ok(())
}

// ===========================================================================
// virtual_checksums - optional ETag / last-modified validation
// Sources: CHK-01..CHK-03, SESS-06 (design/001-virtual-chunk-improvements.md
//          "Checksum"; Changelog 0.1.0a8); upstream
//          `icechunk/tests/test_virtual_refs.rs ::
//          test_zarr_store_with_multiple_virtual_chunk_containers`.
// ===========================================================================

/// CHK-01 / CHK-02: a last-modified checksum that the object still satisfies
/// serves the bytes; one the object no longer satisfies refuses them, naming
/// the reference that changed.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_checksums__last_modified_pass_and_fail() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_store!();
    ic_checksum!();
    ic_manifest!();
    ic_format!();
    fx_zarr_meta!();
    fx_mtime!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let session = fx.repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let location =
        VirtualChunkLocation::from_absolute_path(&file_url(&fx.chunk_paths[0]))?;

    // recorded as unmodified since 600 s AFTER the object's own last-modified
    // time: the object still satisfies the checksum, so the fetch is allowed
    let mtime = object_mtime_secs(&fx.chunk_paths[0]);
    store
        .set_virtual_ref(
            "array/c/0/0/0",
            VirtualChunkRef {
                location: location.clone(),
                offset: 0,
                length: 5,
                checksum: Some(Checksum::LastModified(SecondsSinceEpoch(mtime + 600))),
            },
        )
        .await?;
    assert_eq!(
        store.get("array/c/0/0/0", &ByteRange::ALL).await?,
        fx.chunk_bytes[0].clone()
    );

    // recorded as unmodified since 600 s BEFORE the object's own last-modified
    // time: the object is newer than the reference records, so it is refused
    store
        .set_virtual_ref(
            "array/c/0/0/1",
            VirtualChunkRef {
                location,
                offset: 0,
                length: 5,
                checksum: Some(Checksum::LastModified(SecondsSinceEpoch(mtime - 600))),
            },
        )
        .await?;
    let err = store
        .get("array/c/0/0/1", &ByteRange::ALL)
        .await
        .expect_err("a stale last-modified checksum must refuse the chunk");
    assert!(matches!(
        err,
        StoreError::SessionError(SessionError::VirtualReferenceError(
            VirtualReferenceError::ObjectModified(_)
        ))
    ));
    Ok(())
}

/// CHK-02: an ETag that no longer matches refuses the chunk.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_checksums__etag_mismatch_is_refused() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_store!();
    ic_checksum!();
    ic_manifest!();
    ic_format!();
    fx_zarr_meta!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let session = fx.repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    store
        .set_virtual_ref(
            "array/c/1/0/1",
            VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path(&file_url(
                    &fx.chunk_paths[2],
                ))?,
                offset: 0,
                length: 5,
                checksum: Some(Checksum::ETag("invalid etag".to_string())),
            },
        )
        .await?;

    let err = store
        .get("array/c/1/0/1", &ByteRange::ALL)
        .await
        .expect_err("a mismatched ETag must refuse the chunk");
    assert!(matches!(
        err,
        StoreError::SessionError(SessionError::VirtualReferenceError(
            VirtualReferenceError::ObjectModified(_)
        ))
    ));
    Ok(())
}

/// CHK-03: with no checksum recorded the fetch is always attempted.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_checksums__absent_checksum_always_fetches() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_config!();
    ic_virtual_chunks!();
    ic_store!();
    ic_manifest!();
    ic_format!();
    fx_zarr_meta!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let session = fx.repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    store
        .set_virtual_ref(
            "array/c/0/0/2",
            VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path(&file_url(
                    &fx.chunk_paths[2],
                ))?,
                offset: 0,
                length: 5,
                checksum: None,
            },
        )
        .await?;
    assert_eq!(
        store.get("array/c/0/0/2", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(&fx.chunk_bytes[2][..5])
    );
    Ok(())
}

/// SESS-06: `all_virtual_chunk_locations` reports every location the repository
/// holds virtual data at, and only those - inline chunks are not locations.
#[tokio::test(flavor = "multi_thread")]
async fn virtual_checksums__all_virtual_chunk_locations() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    ic_virtual_chunks!();
    ic_store!();
    ic_manifest!();
    fx_zarr_meta!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let session = fx.repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let mut expected = HashSet::new();
    for (i, key) in ["array/c/0/0/0", "array/c/0/0/1"].iter().enumerate() {
        let location =
            VirtualChunkLocation::from_absolute_path(&file_url(&fx.chunk_paths[i]))?;
        expected.insert(location.0.clone());
        store
            .set_virtual_ref(
                key,
                VirtualChunkRef { location, offset: 0, length: 5, checksum: None },
            )
            .await?;
    }
    // an ordinary inline chunk, which must not appear
    store.set("array/c/0/0/2", Bytes::copy_from_slice(b"inline")).await?;

    let locations: HashSet<String> = store
        .session()
        .read()
        .await
        .all_virtual_chunk_locations()
        .await?
        .try_collect()
        .await?;
    assert_eq!(locations, expected);
    Ok(())
}

// ===========================================================================
// store_sessions - the Zarr-facing store built on a session
// Sources: TEST_INTERFACE section 4; upstream `icechunk/src/store.rs ::
//          test_get_partial_values`, `test_chunk_set_and_get`, `test_clear`.
// ===========================================================================

/// `set_virtual_ref` accepts only chunk keys; a metadata key is refused and
/// nothing is stored.
#[tokio::test]
async fn store_sessions__set_virtual_ref_only_accepts_chunk_keys(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_manifest!();
    ic_format!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let reference = VirtualChunkRef {
        location: VirtualChunkLocation::from_absolute_path("s3://bucket/key")?,
        offset: 0,
        length: 5,
        checksum: None,
    };
    assert!(matches!(
        store.set_virtual_ref("array/zarr.json", reference.clone()).await,
        Err(StoreError::NotAllowed(_))
    ));
    assert!(matches!(
        store.set_virtual_ref("zarr.json", reference.clone()).await,
        Err(StoreError::NotAllowed(_))
    ));
    // ... and so is a Zarr-v2 key, in both its bare and its `<path>/` form
    assert!(matches!(
        store.set_virtual_ref(".zarray", reference.clone()).await,
        Err(StoreError::NotAllowed(_))
    ));
    assert!(matches!(
        store.set_virtual_ref("array/.zarray", reference.clone()).await,
        Err(StoreError::NotAllowed(_))
    ));
    // the metadata key is untouched
    assert_same_json(&store.get("array/zarr.json", &ByteRange::ALL).await?, ARRAY_META);
    // a chunk key is accepted
    store.set_virtual_ref("array/c/0/0/0", reference).await?;
    Ok(())
}

/// The capability matrix this release advertises, and the one operation it
/// does not implement.
#[tokio::test]
async fn store_sessions__capability_matrix() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    let repo = mem_repo().await;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;

    assert!(store.supports_writes()?);
    assert!(store.supports_deletes()?);
    assert!(store.supports_listing()?);
    assert!(!store.supports_partial_writes()?);
    assert!(matches!(
        store.set_partial_values([("array/c/0/0/0", 0u64, Bytes::new())]).await,
        Err(StoreError::Unimplemented(_))
    ));

    // Zarr v2 keys: a read finds nothing, a write is not implemented
    assert!(matches!(
        store.get(".zgroup", &ByteRange::ALL).await,
        Err(StoreError::NotFound(_))
    ));
    assert!(matches!(
        store.set(".zgroup", Bytes::copy_from_slice(b"{}")).await,
        Err(StoreError::Unimplemented(_))
    ));

    // a key that does not parse is refused as such
    assert!(matches!(
        store.get("not-a-zarr-key", &ByteRange::ALL).await,
        Err(StoreError::InvalidKey { .. })
    ));
    Ok(())
}

/// `get_partial_values` answers in the order the key ranges were given, with a
/// per-key result, and its concurrency comes from the repository config.
#[tokio::test]
async fn store_sessions__get_partial_values_preserves_order(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_config!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_zarr_meta!();
    let storage = mem_storage();
    let repo = Repository::create(
        Some(RepositoryConfig {
            get_partial_values_concurrency: 3,
            ..Default::default()
        }),
        storage,
        HashMap::new(),
    )
    .await?;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"abcdef")).await?;
    store.set("array/c/0/0/1", Bytes::copy_from_slice(b"ghijkl")).await?;

    let results = store
        .get_partial_values([
            ("array/c/0/0/0".to_string(), ByteRange::bounded(0, 2)),
            ("array/c/0/0/1".to_string(), ByteRange::from_offset(3)),
            ("array/c/0/0/9".to_string(), ByteRange::ALL),
            ("array/c/0/0/0".to_string(), ByteRange::Last(1)),
        ])
        .await?;
    assert_eq!(results.len(), 4);
    assert_eq!(results[0].as_ref().unwrap(), &Bytes::copy_from_slice(b"ab"));
    assert_eq!(results[1].as_ref().unwrap(), &Bytes::copy_from_slice(b"jkl"));
    assert!(matches!(results[2], Err(StoreError::NotFound(_))));
    assert_eq!(results[3].as_ref().unwrap(), &Bytes::copy_from_slice(b"f"));
    Ok(())
}

/// `clear` empties the store through the session, and `is_empty` reports it.
#[tokio::test]
async fn store_sessions__clear_empties_the_store() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;
    assert!(!store.is_empty("").await?);

    store.clear().await?;
    assert!(store.is_empty("").await?);
    assert!(!store.exists("zarr.json").await?);
    assert_eq!(store.list().await?.try_collect::<Vec<_>>().await?.len(), 0);
    Ok(())
}

/// `exists` and `delete` agree with `get`, and a delete of an absent key is not
/// an error.
#[tokio::test]
async fn store_sessions__exists_and_delete() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;

    assert!(store.exists("array/c/0/0/0").await?);
    store.delete("array/c/0/0/0").await?;
    assert!(!store.exists("array/c/0/0/0").await?);
    // deleting again is a no-op, not a failure
    store.delete("array/c/0/0/0").await?;

    store.delete("array/zarr.json").await?;
    assert!(matches!(
        store.get("array/zarr.json", &ByteRange::ALL).await,
        Err(StoreError::NotFound(_))
    ));
    Ok(())
}

// ===========================================================================
// store_pickle - the msgpack round trip a Python pickle goes through
// Sources: SESS-07 (Changelog 0.1.0a5), SESS-04; upstream
//          `icechunk/src/store.rs :: test_serialize`.
// ===========================================================================

/// A store serialised after its session committed comes back read-only, and it
/// still reads everything the commit contains.
#[tokio::test]
async fn store_pickle__committed_store_round_trips_read_only(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_zarr_meta!();
    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let repo = Repository::create(None, storage, HashMap::new()).await?;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;
    session.write().await.commit("first", None).await?;

    let revived = Store::from_bytes(store.as_bytes().await?)?;
    assert!(revived.read_only().await);
    assert_eq!(
        revived.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(b"hello")
    );
    assert!(matches!(
        revived.set("array/c/0/0/1", Bytes::copy_from_slice(b"nope")).await,
        Err(StoreError::ReadOnly)
    ));
    Ok(())
}

/// The round trip carries the session's state: an uncommitted writable store
/// comes back writable, at the same snapshot, with its pending changes.
#[tokio::test]
async fn store_pickle__uncommitted_store_keeps_its_changes() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_zarr_meta!();
    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let repo = Repository::create(None, storage, HashMap::new()).await?;
    let base = repo.lookup_branch("main").await?;

    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let revived = Store::from_bytes(store.as_bytes().await?)?;
    assert!(!revived.read_only().await);
    {
        let s = revived.session();
        let guard = s.read().await;
        assert_eq!(guard.branch(), Some("main"));
        assert_eq!(guard.snapshot_id(), &base);
        assert!(guard.has_uncommitted_changes());
    }
    assert_same_json(&revived.get("array/zarr.json", &ByteRange::ALL).await?, ARRAY_META);
    // and it can go on to commit
    let snap = revived.session().write().await.commit("from the revived store", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

/// `from_session_and_config` pins the partial-values concurrency explicitly,
/// independently of the repository config.
#[tokio::test]
async fn store_pickle__from_session_and_config() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let store = Store::from_session_and_config(
        Arc::new(RwLock::new(repo.writable_session("main").await?)),
        1,
    );
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"abc")).await?;

    let results = store
        .get_partial_values([("array/c/0/0/0".to_string(), ByteRange::ALL)])
        .await?;
    assert_eq!(results.len(), 1);
    assert_eq!(results[0].as_ref().unwrap(), &Bytes::copy_from_slice(b"abc"));
    Ok(())
}

// ===========================================================================
// inherited_refs_snapshots - the spec-1 reference and snapshot model, reached
// through this release's `Repository`.
// Sources: archive/spec-alpha1.md "Reference Files", "Snapshot Files";
//          upstream `icechunk/src/repository.rs :: test_manage_refs`.
// ===========================================================================

/// Branches are mutable references, tags are immutable ones, and a repository
/// always has `main`.
#[tokio::test]
async fn inherited_refs_snapshots__branch_and_tag_crud() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_refs!();
    fx_mem!();
    let repo = mem_repo().await;
    // `main` is the default branch the `refs` module names
    assert_eq!(Ref::DEFAULT_BRANCH, "main");
    assert_eq!(
        repo.list_branches().await?,
        HashSet::from([Ref::DEFAULT_BRANCH.to_string()])
    );
    assert_eq!(repo.list_tags().await?, HashSet::new());
    assert_ne!(Ref::Branch("x".to_string()), Ref::Tag("x".to_string()));

    let initial = repo.lookup_branch("main").await?;
    repo.create_branch("branch1", &initial).await?;
    repo.create_branch("branch2", &initial).await?;
    assert_eq!(
        repo.list_branches().await?,
        HashSet::from([
            "main".to_string(),
            "branch1".to_string(),
            "branch2".to_string()
        ])
    );
    assert_eq!(repo.lookup_branch("branch1").await?, initial);

    repo.delete_branch("branch1").await?;
    assert_eq!(
        repo.list_branches().await?,
        HashSet::from(["main".to_string(), "branch2".to_string()])
    );

    repo.create_tag("tag1", &initial).await?;
    assert_eq!(repo.list_tags().await?, HashSet::from(["tag1".to_string()]));
    assert_eq!(repo.lookup_tag("tag1").await?, initial);
    // a tag is immutable: creating it again is refused
    assert!(repo.create_tag("tag1", &initial).await.is_err());

    // spec.md, "Creating and Updating Branches" and "Tags": a branch name and a
    // tag name may not contain the `/` character, and a refused name creates
    // nothing.
    assert!(repo.create_branch("has/slash", &initial).await.is_err());
    assert!(repo.create_tag("has/slash", &initial).await.is_err());
    assert_eq!(
        repo.list_branches().await?,
        HashSet::from(["main".to_string(), "branch2".to_string()])
    );
    assert_eq!(repo.list_tags().await?, HashSet::from(["tag1".to_string()]));
    Ok(())
}

/// Two writers on one branch: exactly one commit wins, and the loser is told
/// the branch moved.
#[tokio::test]
async fn inherited_refs_snapshots__one_commit_wins_per_branch(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;

    a.add_group(p("/a")).await?;
    b.add_group(p("/b")).await?;
    let winner = a.commit("a", None).await?;
    assert!(matches!(b.commit("b", None).await, Err(SessionError::Conflict { .. })));
    assert_eq!(repo.lookup_branch("main").await?, winner);
    Ok(())
}

/// Branches diverge: a commit on one branch is invisible from the other.
#[tokio::test]
async fn inherited_refs_snapshots__branches_are_independent(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/common")).await?;
    let base = ds.commit("common", None).await?;

    repo.create_branch("side", &base).await?;
    let mut side = repo.writable_session("side").await?;
    side.add_group(p("/only-on-side")).await?;
    let side_tip = side.commit("side", None).await?;

    assert_eq!(repo.lookup_branch("main").await?, base);
    assert_eq!(repo.lookup_branch("side").await?, side_tip);

    let on_main =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(on_main.get_group(&p("/common")).await.is_ok());
    assert!(on_main.get_node(&p("/only-on-side")).await.is_err());

    let on_side =
        repo.readonly_session(&VersionInfo::BranchTipRef("side".to_string())).await?;
    assert!(on_side.get_group(&p("/only-on-side")).await.is_ok());
    Ok(())
}

/// Every commit records a snapshot with its message, and the ancestry walks
/// them newest first back to the repository's first snapshot.
#[tokio::test]
async fn inherited_refs_snapshots__ancestry_is_newest_first(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let root = repo.lookup_branch("main").await?;

    let mut ids = Vec::new();
    for i in 0..3 {
        let mut ds = repo.writable_session("main").await?;
        ds.add_group(p(&format!("/g{}", i))).await?;
        ids.push(ds.commit(&format!("commit {}", i), None).await?);
    }

    let tip = repo.lookup_branch("main").await?;
    assert_eq!(&tip, ids.last().unwrap());
    let ancestry: Vec<_> = repo.ancestry(&tip).await?.try_collect().await?;
    let messages: Vec<&str> = ancestry.iter().map(|m| m.message.as_str()).collect();
    assert_eq!(messages[0], "commit 2");
    assert_eq!(messages[1], "commit 1");
    assert_eq!(messages[2], "commit 0");
    assert_eq!(ancestry.len(), 4);
    assert_eq!(ancestry[3].id, root);
    Ok(())
}

// ===========================================================================
// inherited_chunk_kinds - the three chunk reference kinds of the spec-1 chunk
// manifest, reached through `Session`.
// Sources: archive/spec-alpha1.md "Chunk Manifest Files" (CM-2, CM-3);
//          guide/icechunk-python-configuration.md "Writing chunks inline".
// ===========================================================================

/// CFG-05: the writer inlines a payload at or below the threshold and
/// materialises anything larger; both read back identically.
#[tokio::test]
async fn inherited_chunk_kinds__inline_and_materialised() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let storage = mem_storage();
    let repo = Repository::create(
        Some(RepositoryConfig { inline_chunk_threshold_bytes: 8, ..Default::default() }),
        storage,
        HashMap::new(),
    )
    .await?;
    let mut ds = repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;

    let small = Bytes::copy_from_slice(b"12345678");
    let large = Bytes::copy_from_slice(b"123456789");

    let small_payload = ds.get_chunk_writer()(small.clone()).await?;
    assert!(matches!(small_payload, ChunkPayload::Inline(_)));
    let large_payload = ds.get_chunk_writer()(large.clone()).await?;
    assert!(matches!(large_payload, ChunkPayload::Ref(_)));

    ds.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(small_payload)).await?;
    ds.set_chunk_ref(p("/array"), ChunkIndices(vec![1]), Some(large_payload)).await?;

    for (coord, expected) in [(0u32, small), (1, large)] {
        let got = get_chunk(
            ds.get_chunk_reader(&p("/array"), &ChunkIndices(vec![coord]), &ByteRange::ALL)
                .await?,
        )
        .await?;
        assert_eq!(got, Some(expected));
    }
    Ok(())
}

/// A virtual reference is the third payload kind and survives a commit.
#[tokio::test(flavor = "multi_thread")]
async fn inherited_chunk_kinds__virtual_reference_survives_commit(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_virtual_chunks!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_meta!();
    fx_path!();
    fx_virtual!();
    let fx = local_virtual_fixture(vec![], false).await?;
    let mut ds = fx.repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_absolute_path(&file_url(
                &fx.chunk_paths[0],
            ))?,
            offset: 0,
            length: 5,
            checksum: None,
        })),
    )
    .await?;
    let snap = ds.commit("a virtual chunk", None).await?;

    let r = fx.repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(matches!(
        r.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Virtual(_))
    ));
    let got = get_chunk(
        r.get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got, Some(fx.chunk_bytes[0].clone()));
    Ok(())
}

/// `all_chunks` enumerates every chunk of the committed repository, whatever
/// its payload kind, keyed by the array path.
#[tokio::test]
async fn inherited_chunk_kinds__all_chunks_enumerates_them(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/")).await?;
    ds.add_array(p("/a"), basic_meta()).await?;
    ds.add_array(p("/b"), basic_meta()).await?;
    for coord in [0u32, 1] {
        ds.set_chunk_ref(
            p("/a"),
            ChunkIndices(vec![coord]),
            Some(ChunkPayload::Inline("a".into())),
        )
        .await?;
    }
    ds.set_chunk_ref(p("/b"), ChunkIndices(vec![0]), Some(ChunkPayload::Inline("b".into())))
        .await?;
    let snap = ds.commit("chunks", None).await?;

    let r = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let mut found: Vec<(String, Vec<u32>)> = r
        .all_chunks()
        .await?
        .map_ok(|(path, info)| (path.to_string(), info.coord.0.clone()))
        .try_collect()
        .await?;
    found.sort();
    assert_eq!(
        found,
        vec![
            ("/a".to_string(), vec![0]),
            ("/a".to_string(), vec![1]),
            ("/b".to_string(), vec![0]),
        ]
    );
    Ok(())
}

/// Deleting a chunk reference removes it; and a coordinate outside the array's
/// shape is refused rather than stored.
#[tokio::test]
async fn inherited_chunk_kinds__delete_and_invalid_coordinates(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("hello".into())),
    )
    .await?;
    assert!(ds.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?.is_some());

    ds.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), None).await?;
    assert_eq!(ds.get_chunk_ref(&p("/array"), &ChunkIndices(vec![0])).await?, None);

    // the array's shape is [5] with chunk shape [1], so index 5 is out of range
    assert!(matches!(
        ds.set_chunk_ref(
            p("/array"),
            ChunkIndices(vec![5]),
            Some(ChunkPayload::Inline("nope".into()))
        )
        .await,
        Err(SessionError::InvalidIndex { .. })
    ));

    // Section 4.8: a zero-dimensional array has exactly one chunk, and its
    // coordinate is the empty index tuple.
    let scalar = ZarrArrayMetadata {
        shape: vec![],
        chunk_shape: ChunkShape(vec![]),
        ..basic_meta()
    };
    ds.add_array(p("/scalar"), scalar).await?;
    ds.set_chunk_ref(
        p("/scalar"),
        ChunkIndices(vec![]),
        Some(ChunkPayload::Inline("s".into())),
    )
    .await?;
    assert_eq!(
        ds.get_chunk_ref(&p("/scalar"), &ChunkIndices(vec![])).await?,
        Some(ChunkPayload::Inline(Bytes::copy_from_slice(b"s")))
    );
    Ok(())
}

// ===========================================================================
// inherited_zarr_store - the Zarr-facing key/value view of the repository.
// Sources: archive/spec-alpha1.md "Overview"; upstream `icechunk/src/store.rs ::
//          test_metadata_set_and_get`, `test_list_dir`, `test_chunk_list`.
// ===========================================================================

/// Group and array metadata documents round-trip through their `zarr.json`
/// keys, and an absent node reports the not-found category.
#[tokio::test]
async fn inherited_zarr_store__metadata_keys_round_trip() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;

    assert!(matches!(
        store.get("zarr.json", &ByteRange::ALL).await,
        Err(StoreError::NotFound(_))
    ));

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    assert_same_json(&store.get("zarr.json", &ByteRange::ALL).await?, GROUP_META);

    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    assert_same_json(&store.get("array/zarr.json", &ByteRange::ALL).await?, ARRAY_META);

    // a byte range applied to a metadata read slices what a whole read returns
    let whole = store.get("array/zarr.json", &ByteRange::ALL).await?;
    assert!(whole.len() > 8);
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::bounded(0, 8)).await?,
        whole.slice(0..8)
    );
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::from_offset(8)).await?,
        whole.slice(8..)
    );
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::Last(4)).await?,
        whole.slice(whole.len() - 4..)
    );
    Ok(())
}

/// Chunk keys are `<array>/c/<i>/<j>/<k>`, they honour a byte range, and they
/// survive a commit.
#[tokio::test]
async fn inherited_zarr_store__chunk_keys_and_byte_ranges() -> Result<(), Box<dyn Error>>
{
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let small = Bytes::copy_from_slice(b"hello");
    let big = Bytes::copy_from_slice(b"hello".repeat(512).as_slice());
    store.set("array/c/0/1/0", small.clone()).await?;
    store.set("array/c/0/1/1", big.clone()).await?;

    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, small);
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::bounded(1, 4)).await?, small.slice(1..4));
    assert_eq!(store.get("array/c/0/1/1", &ByteRange::to_offset(5)).await?, big.slice(0..5));

    let snap = session.write().await.commit("chunks", None).await?;
    let reopened = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?,
    )))
    .await;
    assert_eq!(reopened.get("array/c/0/1/0", &ByteRange::ALL).await?, small);
    assert_eq!(reopened.get("array/c/0/1/1", &ByteRange::ALL).await?, big);
    Ok(())
}

/// `list` and `list_prefix` enumerate every key the store holds.
#[tokio::test]
async fn inherited_zarr_store__list_and_list_prefix() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"hello")).await?;
    store.set("array/c/1/1/1", Bytes::copy_from_slice(b"hello")).await?;
    // a sibling whose name merely starts with the same text
    store.set("array-suffix/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let mut all: Vec<String> = store.list().await?.try_collect().await?;
    all.sort();
    assert_eq!(
        all,
        vec![
            "array-suffix/zarr.json".to_string(),
            "array/c/0/1/0".to_string(),
            "array/c/1/1/1".to_string(),
            "array/zarr.json".to_string(),
            "zarr.json".to_string(),
        ]
    );

    let mut under: Vec<String> = store.list_prefix("array/c/").await?.try_collect().await?;
    under.sort();
    assert_eq!(
        under,
        vec!["array/c/0/1/0".to_string(), "array/c/1/1/1".to_string()]
    );

    // Section 4.7: a key belongs to a prefix by whole components, so
    // `list_prefix("array")` and `list_prefix("array/")` select the same keys
    // and neither selects `array-suffix/zarr.json`.
    let mut bare: Vec<String> = store.list_prefix("array").await?.try_collect().await?;
    bare.sort();
    let mut slashed: Vec<String> =
        store.list_prefix("array/").await?.try_collect().await?;
    slashed.sort();
    assert_eq!(
        bare,
        vec![
            "array/c/0/1/0".to_string(),
            "array/c/1/1/1".to_string(),
            "array/zarr.json".to_string(),
        ]
    );
    assert_eq!(bare, slashed);
    Ok(())
}

/// `list_dir` and `list_dir_items` report one level, separating keys from
/// prefixes.
#[tokio::test]
async fn inherited_zarr_store__list_dir_reports_one_level(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_list_dir_item!();
    fx_mem!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let store =
        Store::from_session(Arc::new(RwLock::new(repo.writable_session("main").await?)))
            .await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"hello")).await?;
    store.set("array/c/1/1/1", Bytes::copy_from_slice(b"hello")).await?;

    let mut root: Vec<String> = store.list_dir("/").await?.try_collect().await?;
    root.sort();
    assert_eq!(root, vec!["array".to_string(), "zarr.json".to_string()]);

    // Section 4.7: `""` and `"/"` both name the root and must report the same
    // names, neither prepending a `/`.
    let mut empty_prefix: Vec<String> = store.list_dir("").await?.try_collect().await?;
    empty_prefix.sort();
    assert_eq!(empty_prefix, root);

    // The stream's order is free (PROFILE P4), so the driver imposes its own
    // total order here rather than relying on `ListDirItem`'s `Ord`: keys
    // first, then prefixes, each by name.
    let mut items: Vec<ListDirItem> = store.list_dir_items("/").await?.try_collect().await?;
    items.sort_by_key(|item| match item {
        ListDirItem::Key(name) => (0u8, name.clone()),
        ListDirItem::Prefix(name) => (1u8, name.clone()),
    });
    assert_eq!(
        items,
        vec![
            ListDirItem::Key("zarr.json".to_string()),
            ListDirItem::Prefix("array".to_string()),
        ]
    );

    let mut chunks: Vec<String> = store.list_dir("array/c/").await?.try_collect().await?;
    chunks.sort();
    assert_eq!(chunks, vec!["0".to_string(), "1".to_string()]);
    Ok(())
}

/// The Zarr store writes through to the array/group model: a key written
/// through the store is a node on the session, and vice versa.
#[tokio::test]
async fn inherited_zarr_store__store_and_session_agree() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_store!();
    ic_format!();
    fx_mem!();
    fx_path!();
    fx_zarr_meta!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    {
        let guard = session.read().await;
        assert!(guard.get_group(&p("/")).await.is_ok());
        let node = guard.get_array(&p("/array")).await?;
        assert_eq!(node.path, p("/array"));
    }

    // a node added on the session shows up as a store key
    session.write().await.add_group(p("/added")).await?;
    assert!(store.exists("added/zarr.json").await?);
    Ok(())
}

// ===========================================================================
// inherited_nodes - groups, arrays and user attributes: the snapshot node
// model, reached through `Session`.
// Sources: archive/spec-alpha1.md "Snapshot Files"; upstream
//          `icechunk/src/session.rs :: test_add_delete_group`,
//          `test_add_delete_array`, `test_add_array_group_clash`.
// ===========================================================================

/// Groups and arrays are added, read back and deleted, and a deleted node stops
/// being visible.
#[tokio::test]
async fn inherited_nodes__groups_and_arrays_round_trip() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/")).await?;
    ds.add_group(p("/g")).await?;
    ds.add_array(p("/g/a"), basic_meta()).await?;

    assert!(ds.get_group(&p("/g")).await.is_ok());
    assert!(ds.get_array(&p("/g/a")).await.is_ok());
    // asking for the wrong kind is refused
    assert!(ds.get_array(&p("/g")).await.is_err());
    assert!(ds.get_group(&p("/g/a")).await.is_err());

    // a node reports its own kind, and carries the matching node data
    let group = ds.get_node(&p("/g")).await?;
    assert_eq!(group.node_type(), NodeType::Group);
    assert!(matches!(group.node_data, NodeData::Group));
    let array = ds.get_node(&p("/g/a")).await?;
    assert_eq!(array.node_type(), NodeType::Array);
    match array.node_data {
        NodeData::Array(meta, _) => assert_eq!(meta, basic_meta()),
        NodeData::Group => panic!("an array node must carry array data"),
    }

    let snap = ds.commit("nodes", None).await?;

    let mut ds = repo.writable_session("main").await?;
    ds.delete_array(p("/g/a")).await?;
    ds.delete_group(p("/g")).await?;
    ds.commit("deletions", None).await?;

    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(r.get_node(&p("/g")).await.is_err());
    assert!(r.get_node(&p("/g/a")).await.is_err());
    // the earlier snapshot still has them
    let before = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(before.get_group(&p("/g")).await.is_ok());
    Ok(())
}

/// Adding a node where one already exists is refused.
#[tokio::test]
async fn inherited_nodes__duplicate_paths_are_refused() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;
    assert!(matches!(
        ds.add_array(p("/g"), basic_meta()).await,
        Err(SessionError::AlreadyExists { .. })
    ));
    ds.add_array(p("/g/a"), basic_meta()).await?;
    assert!(matches!(
        ds.add_group(p("/g/a")).await,
        Err(SessionError::AlreadyExists { .. })
    ));
    Ok(())
}

/// User attributes are set, read back, replaced and cleared on a node.
#[tokio::test]
async fn inherited_nodes__user_attributes() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;

    assert_eq!(ds.get_node(&p("/g")).await?.user_attributes, None);
    // user attributes round-trip through their own byte form
    let atts = UserAttributes::try_new(br#"{"n":1}"#)?;
    assert_eq!(UserAttributes::try_new(atts.to_bytes().as_ref())?, atts);
    ds.set_user_attributes(p("/g"), Some(UserAttributes::try_new(br#"{"n":1}"#)?))
        .await?;
    assert_eq!(
        ds.get_node(&p("/g")).await?.user_attributes,
        Some(icechunk::format::snapshot::UserAttributesSnapshot::Inline(
            UserAttributes::try_new(br#"{"n":1}"#)?
        ))
    );

    let snap = ds.commit("attributes", None).await?;
    let r = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(r.get_node(&p("/g")).await?.user_attributes.is_some());

    let mut ds = repo.writable_session("main").await?;
    ds.set_user_attributes(p("/g"), None).await?;
    ds.commit("cleared", None).await?;
    let r = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(r.get_node(&p("/g")).await?.user_attributes, None);
    Ok(())
}

/// `list_nodes` reports every node of the repository, and `clear` removes them
/// all.
#[tokio::test]
async fn inherited_nodes__list_nodes_and_clear() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let repo = mem_repo().await;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/")).await?;
    ds.add_group(p("/g")).await?;
    ds.add_array(p("/g/a"), basic_meta()).await?;
    ds.add_array(p("/b"), meta_3d()).await?;

    let mut paths: Vec<String> =
        ds.list_nodes().await?.map(|n| n.path.to_string()).collect();
    paths.sort();
    assert_eq!(
        paths,
        vec!["/".to_string(), "/b".to_string(), "/g".to_string(), "/g/a".to_string()]
    );

    ds.clear().await?;
    assert_eq!(ds.list_nodes().await?.count(), 0);
    Ok(())
}

// ===========================================================================
// peripheral_gc - `icechunk::ops::gc`, the Rust-only garbage collector.
// Sources: GC-01 (spec.md "Storage Operations"); no prose documentation at this
//          release, so only the shape of the operation is asserted.
// ===========================================================================

/// A configuration that keeps everything reports so and deletes nothing.
#[tokio::test]
async fn peripheral_gc__keep_everything_is_a_noop() -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_gc!();
    ic_format!();
    fx_mem!();
    fx_path!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut ds = repo.writable_session("main").await?;
    ds.add_group(p("/g")).await?;
    ds.commit("a group", None).await?;

    let config = GCConfig::new(
        HashSet::new(),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
    );
    assert!(!config.deletes_chunks());
    assert!(!config.deletes_manifests());
    assert!(!config.deletes_attributes());
    assert!(!config.deletes_transaction_logs());
    assert!(!config.deletes_snapshots());

    let summary = garbage_collect(storage.as_ref(), &config).await?;
    assert_eq!(summary, GCSummary::default());
    Ok(())
}

/// `clean_all` with a cut-off before anything was written deletes nothing,
/// because nothing is older than the cut-off.
#[tokio::test]
async fn peripheral_gc__cutoff_before_everything_deletes_nothing(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_gc!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut ds = repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    ds.set_chunk_ref(
        p("/array"),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline("hi".into())),
    )
    .await?;
    ds.commit("data", None).await?;

    let epoch = chrono::DateTime::<chrono::Utc>::from_timestamp(0, 0)
        .expect("epoch is a valid timestamp");
    let config = GCConfig::clean_all(epoch, epoch, None);
    assert!(config.deletes_chunks());
    assert!(config.deletes_snapshots());

    let summary = garbage_collect(storage.as_ref(), &config).await?;
    assert_eq!(summary, GCSummary::default());
    Ok(())
}

/// Reachable data survives a full sweep: everything the branch can reach is a
/// root, so nothing it points at is collected.
#[tokio::test]
async fn peripheral_gc__reachable_data_survives_a_full_sweep(
) -> Result<(), Box<dyn Error>> {
    ic_storage!();
    ic_repository!();
    ic_session!();
    ic_config!();
    ic_gc!();
    ic_manifest!();
    ic_snapshot!();
    ic_format!();
    ic_metadata!();
    fx_mem!();
    fx_meta!();
    fx_path!();
    let storage = mem_storage();
    let repo = Repository::create(
        Some(RepositoryConfig { inline_chunk_threshold_bytes: 1, ..Default::default() }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let mut ds = repo.writable_session("main").await?;
    ds.add_array(p("/array"), basic_meta()).await?;
    let payload = ds.get_chunk_writer()(Bytes::copy_from_slice(b"materialised")).await?;
    assert!(matches!(payload, ChunkPayload::Ref(_)));
    ds.set_chunk_ref(p("/array"), ChunkIndices(vec![0]), Some(payload)).await?;
    let snap = ds.commit("materialised chunk", None).await?;

    let far_future = chrono::DateTime::<chrono::Utc>::from_timestamp(4_102_444_800, 0)
        .expect("2100-01-01 is a valid timestamp");
    let summary =
        garbage_collect(storage.as_ref(), &GCConfig::clean_all(far_future, far_future, None))
            .await?;
    assert_eq!(summary.chunks_deleted, 0);
    assert_eq!(summary.manifests_deleted, 0);
    assert_eq!(summary.snapshots_deleted, 0);

    // and the data is still readable afterwards
    let r = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let got = get_chunk(
        r.get_chunk_reader(&p("/array"), &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got, Some(Bytes::copy_from_slice(b"materialised")));
    Ok(())
}

// ===========================================================================
// peripheral_helpers - the small public helpers the store and the virtual
// resolver are built on.
// Sources: TEST_INTERFACE section 4; upstream
//          `icechunk/src/format/manifest.rs :: test_virtual_chunk_location_bad`.
// ===========================================================================

/// `construct_valid_byte_range` maps a request against a chunk's window inside
/// a larger object.
#[test]
fn peripheral_helpers__construct_valid_byte_range() {
    ic_session_helpers!();
    ic_format!();
    assert_eq!(
        construct_valid_byte_range(&ByteRange::bounded(1, 3), 100, 10),
        ByteRange::Bounded(101..103)
    );
    assert_eq!(
        construct_valid_byte_range(&ByteRange::from_offset(2), 100, 10),
        ByteRange::Bounded(102..110)
    );
    assert_eq!(
        construct_valid_byte_range(&ByteRange::ALL, 100, 10),
        ByteRange::Bounded(100..110)
    );
    assert_eq!(
        construct_valid_byte_range(&ByteRange::Last(3), 100, 10),
        ByteRange::Bounded(107..110)
    );
}

/// `is_prefix_match` matches on whole path components, never on a shared text
/// prefix of a component.
#[test]
fn peripheral_helpers__is_prefix_match() {
    ic_session_helpers!();
    assert!(is_prefix_match("array/zarr.json", "array"));
    assert!(is_prefix_match("array/c/0", "array/c"));
    assert!(is_prefix_match("array", "array"));
    assert!(is_prefix_match("anything", ""));
    assert!(!is_prefix_match("array-suffix/zarr.json", "array"));
    assert!(!is_prefix_match("other/zarr.json", "array"));
}

/// `ByteRange` conversions and slicing.
#[test]
fn peripheral_helpers__byte_range_constructors() {
    ic_format!();
    assert_eq!(ByteRange::ALL, ByteRange::From(0));
    assert_eq!(ByteRange::bounded(2, 5), ByteRange::Bounded(2..5));
    assert_eq!(ByteRange::to_offset(4), ByteRange::Bounded(0..4));
    assert_eq!(ByteRange::from_offset(4), ByteRange::From(4));
    assert_eq!(ByteRange::from_offset_with_length(2, 3), ByteRange::Bounded(2..5));
    assert_eq!(ByteRange::from(2u64..5u64), ByteRange::Bounded(2..5));

    let bytes = Bytes::copy_from_slice(b"0123456789");
    assert_eq!(ByteRange::bounded(2, 5).slice(bytes.clone()), bytes.slice(2..5));
    assert_eq!(ByteRange::from_offset(7).slice(bytes.clone()), bytes.slice(7..));
    assert_eq!(ByteRange::Last(3).slice(bytes.clone()), bytes.slice(7..10));
    assert_eq!(ByteRange::ALL.slice(bytes.clone()), bytes);
}

/// The storage constructors section 5.6's migration renamed, reached both
/// through `icechunk::storage` and through the crate-root re-exports. None of
/// them makes a request: `new_s3_storage` is synchronous and only describes a
/// backend (PROFILE P1).
#[test]
fn peripheral_helpers__storage_constructors() {
    ic_storage!();
    ic_storage_ctors!();
    ic_config!();
    let dir = tempfile::TempDir::new().expect("a temp dir");

    let _: Arc<dyn Storage + Send + Sync> =
        new_in_memory_storage().expect("in-memory storage");
    let _: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).expect("local filesystem storage");

    let _: ObjectStorage = ObjectStorage::new_in_memory().expect("an in-memory backend");
    let _: ObjectStorage =
        ObjectStorage::new_local_filesystem(dir.path()).expect("a local backend");

    let _: Arc<dyn Storage + Send + Sync> = new_s3_storage(
        S3Options {
            region: Some("us-east-1".to_string()),
            endpoint_url: None,
            anonymous: true,
            allow_http: false,
        },
        "a-bucket".to_string(),
        Some("a-prefix".to_string()),
        Some(S3Credentials::Anonymous),
    )
    .expect("an s3 storage description");
}

/// The two string-shaped values section 4.8 parses. A node path must be
/// absolute and canonical. A virtual chunk location must be a URL naming a
/// store, and it is normalised when it is accepted.
#[test]
fn peripheral_helpers__path_and_location_parsing() {
    ic_format!();
    ic_manifest!();
    // section 4.8: `"/a/b".try_into()` succeeds, `"a/b".try_into()` does not,
    // and a path that is absolute but not canonical is refused too.
    let absolute: Path = "/a/b".try_into().expect("an absolute, canonical path");
    assert_eq!(absolute.to_string(), "/a/b");
    assert!(Path::try_from("a/b").is_err());
    assert!(Path::new("a/b").is_err());
    assert!(Path::new("/a/../b").is_err());
    assert_eq!(Path::root(), Path::try_from("/").expect("the root path"));

    let ok = VirtualChunkLocation::from_absolute_path("s3://bucket//path//to/chunk")
        .expect("a well-formed s3 url");
    assert_eq!(ok.0, "s3://bucket/path/to/chunk");

    let local = VirtualChunkLocation::from_absolute_path("file:///tmp/a/chunk")
        .expect("a well-formed file url");
    assert_eq!(local.0, "file:///tmp/a/chunk");

    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("abcdef"),
        Err(VirtualReferenceError::CannotParseUrl(_))
    ));
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("s3:///foo/path"),
        Err(VirtualReferenceError::CannotParseBucketName(_))
    ));
}
