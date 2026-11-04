// Verifier-owned conformance driver for milestone 09 — "Icechunk 2: the
// repository table of contents" (earth-mover/icechunk v2.0.0).
//
// Every assertion here traces either to /app/icechunk-spec/ (the pinned
// Icechunk specification corpus plus PROFILE.md, which resolves the places the
// corpus and this milestone's contract disagree) or to /app/TEST_INTERFACE.md,
// which states the public Rust surface this milestone must expose.
//
// Group = the `<prefix>__` part of a test function name. The runner invokes one
// `cargo test` per group and asserts the executed count, so a case function's
// name must begin at column 0 and carry its group prefix followed by `__`.
//
// Nothing here reaches the network, reads a cloud credential, starts a daemon
// or sleeps on a wall clock. Storage is `new_in_memory_storage()` except where
// the case must observe files on disk, which uses
// `new_local_filesystem_storage()` over a `tempfile::TempDir`.
//
// Upstream provenance is recorded per group in a comment above it.

#![allow(clippy::bool_assert_comparison)]

use std::{
    collections::{HashMap, HashSet},
    num::{NonZeroU16, NonZeroUsize},
    path::{Path as StdPath, PathBuf},
    sync::Arc,
};

use bytes::Bytes;
use chrono::Utc;
use futures::{StreamExt as _, TryStreamExt as _};
use tokio::sync::RwLock;


type TestResult = Result<(), Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// The `icechunk` prelude and the shared fixtures, as macros each case expands.
//
// NOTHING at file scope names anything from the candidate crate, and the reason
// is scoring rather than style. When the whole `conformance` target fails to
// compile, tests/test.sh attributes the failure group by group by removing the
// CASE FUNCTIONS of every other group - and nothing else. Every file-scope
// `use` item, const and helper survives in every slice, so a single file-scope
// name the candidate has not implemented would fail every group's slice,
// including the inherited-regression groups that never touch it. A
// `macro_rules!` body is name-resolved only where it is expanded, so here each
// case pulls in exactly the modules and fixtures it uses and a group's slice
// answers for that group's own surface alone.
//
// `ic_*!` are the import macros, one per module of TEST_INTERFACE.md §2.
// `fx_*!` are the shared fixtures. Both are expanded at the top of every case
// body that needs them; a fixture macro is expanded beside the import macros
// its body needs.
// ---------------------------------------------------------------------------

macro_rules! ic_core {
    () => {
        #[allow(unused_imports)]
        use icechunk::{
            ObjectStoreConfig, Repository, RepositoryConfig, Storage, Store,
            new_in_memory_storage, new_local_filesystem_storage,
        };
    };
}

macro_rules! ic_config {
    () => {
        #[allow(unused_imports)]
        use icechunk::config::{
            Credentials, ManifestConfig, ManifestSplitCondition, ManifestSplitDim,
            ManifestSplitDimCondition, ManifestSplittingConfig,
        };
    };
}

macro_rules! ic_conflicts {
    () => {
        #[allow(unused_imports)]
        use icechunk::conflicts::{
            basic_solver::{BasicConflictSolver, VersionSelection},
            detector::ConflictDetector,
        };
    };
}

macro_rules! ic_diff {
    () => {
        #[allow(unused_imports)]
        use icechunk::diff::Diff;
    };
}

macro_rules! ic_display {
    () => {
        #[allow(unused_imports)]
        use icechunk::display::AncestryGraph;
    };
}

macro_rules! ic_flags {
    () => {
        #[allow(unused_imports)]
        use icechunk::feature_flags::{CREATE_TAG_FLAG, DELETE_TAG_FLAG, MOVE_NODE_FLAG};
    };
}

macro_rules! ic_format {
    () => {
        #[allow(unused_imports)]
        use icechunk::format::{
            ByteRange, ChunkIndices, IcechunkFormatErrorKind, Path, SnapshotId,
            format_constants::SpecVersionBin,
        };
    };
}

macro_rules! ic_node_type {
    () => {
        #[allow(unused_imports)]
        use icechunk::format::NodeType;
    };
}

macro_rules! ic_manifest {
    () => {
        #[allow(unused_imports)]
        use icechunk::format::manifest::{
            ChunkPayload, VirtualChunkLocation, VirtualChunkRef,
        };
    };
}

macro_rules! ic_repo_info {
    () => {
        #[allow(unused_imports)]
        use icechunk::format::repo_info::{RepoAvailability, RepoInfo, RepoStatus, UpdateType};
    };
}

macro_rules! ic_snapshot {
    () => {
        #[allow(unused_imports)]
        use icechunk::format::snapshot::{
            ArrayShape, DimensionShape, NodeData, Snapshot, SnapshotInfo,
            SnapshotProperties,
        };
    };
}

macro_rules! ic_migrations {
    () => {
        #[allow(unused_imports)]
        use icechunk::migrations::{MigrationErrorKind, migrate_1_to_2};
    };
}

macro_rules! ic_refs {
    () => {
        #[allow(unused_imports)]
        use icechunk::refs::RefErrorKind;
    };
}

macro_rules! ic_gc {
    () => {
        #[allow(unused_imports)]
        use icechunk::ops::gc::{
            ExpiredRefAction, GCConfig, GCError, GCSummary, expire, garbage_collect,
        };
    };
}

macro_rules! ic_rewrite_manifests {
    () => {
        #[allow(unused_imports)]
        use icechunk::ops::manifests::rewrite_manifests;
    };
}

macro_rules! ic_repository {
    () => {
        #[allow(unused_imports)]
        use icechunk::repository::{RepositoryError, RepositoryErrorKind, VersionInfo};
    };
}

macro_rules! ic_detected_spec {
    () => {
        #[allow(unused_imports)]
        use icechunk::repository::DetectedSpecVersion;
    };
}

macro_rules! ic_session {
    () => {
        #[allow(unused_imports)]
        use icechunk::session::{Session, SessionError, SessionErrorKind, get_chunk};
    };
}

macro_rules! ic_session_new {
    () => {
        #[allow(unused_imports)]
        use icechunk::session::{
            CommitMethod, RebaseHook, ReindexMapping, ReindexOperationResult,
            SessionMode,
        };
    };
}

macro_rules! ic_store {
    () => {
        #[allow(unused_imports)]
        use icechunk::store::{StoreErrorKind, StoreResult};
    };
}

macro_rules! ic_array_metadata {
    () => {
        #[allow(unused_imports)]
        use icechunk::store::ArrayMetadata;
    };
}

macro_rules! ic_vchunks {
    () => {
        #[allow(unused_imports)]
        use icechunk::virtual_chunks::VirtualChunkContainer;
    };
}

macro_rules! fx_mem_storage {
    () => {
        #[allow(dead_code)]
        /// A fresh, empty in-memory object store.
        async fn mem_storage() -> Arc<dyn Storage + Send + Sync> {
            new_in_memory_storage().await.expect("in-memory storage")
        }
    };
}

macro_rules! fx_mem_repo {
    () => {
        #[allow(dead_code)]
        /// A fresh in-memory repository at the release's default on-disk generation.
        async fn mem_repo() -> Repository {
            let storage = mem_storage().await;
            Repository::create(None, storage, HashMap::new(), None, true)
                .await
                .expect("create repository")
        }
    };
}

macro_rules! fx_mem_repo_at {
    () => {
        #[allow(dead_code)]
        /// A fresh in-memory repository pinned to a chosen on-disk generation.
        async fn mem_repo_at(version: SpecVersionBin) -> Repository {
            let storage = mem_storage().await;
            Repository::create(None, storage, HashMap::new(), Some(version), true)
                .await
                .expect("create repository")
        }
    };
}

macro_rules! fx_tip {
    () => {
        #[allow(dead_code)]
        /// `VersionInfo` for a branch tip.
        fn tip(branch: &str) -> VersionInfo {
            VersionInfo::BranchTipRef(branch.to_string())
        }
    };
}

macro_rules! fx_p {
    () => {
        #[allow(dead_code)]
        /// A path, from a literal the caller knows is canonical and absolute.
        fn p(path: &str) -> Path {
            Path::new(path).expect("canonical absolute path")
        }
    };
}

macro_rules! fx_seed_root {
    () => {
        #[allow(dead_code)]
        /// Commit a root group on `main` so the repository has a non-initial snapshot.
        async fn seed_root(repo: &Repository, message: &str) -> Result<SnapshotId, Box<dyn std::error::Error>> {
            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root(), Bytes::new()).await?;
            Ok(session.commit(message).max_concurrent_nodes(4).execute().await?)
        }
    };
}

macro_rules! fx_seed_array {
    () => {
        #[allow(dead_code)]
        /// A one-dimensional array of `num_chunks` inline chunks, committed on `main`.
        /// Chunk `i` holds the decimal representation of `i`.
        async fn seed_array(
            repo: &Repository,
            path: &Path,
            num_chunks: u32,
        ) -> Result<SnapshotId, Box<dyn std::error::Error>> {
            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root(), Bytes::new()).await?;
            let shape = ArrayShape::new(vec![(u64::from(num_chunks) * 2, num_chunks)])
                .ok_or("array shape")?;
            session.add_array(path.clone(), shape, None, Bytes::new()).await?;
            for i in 0..num_chunks {
                session
                    .set_chunk_ref(
                        path.clone(),
                        ChunkIndices(vec![i]),
                        Some(ChunkPayload::Inline(i.to_string().into())),
                    )
                    .await?;
            }
            Ok(session.commit("seed array").max_concurrent_nodes(4).execute().await?)
        }
    };
}

macro_rules! fx_chunk_bytes {
    () => {
        #[allow(dead_code)]
        /// The resolved bytes of one chunk, through the documented reader interface.
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
    };
}

macro_rules! fx_messages {
    () => {
        #[allow(dead_code)]
        /// Every commit message reachable from a version, newest first.
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
    };
}

macro_rules! fx_history {
    () => {
        #[allow(dead_code)]
        /// Every snapshot reachable from a version, newest first.
        async fn history(
            repo: &Repository,
            version: &VersionInfo,
        ) -> Result<Vec<SnapshotInfo>, Box<dyn std::error::Error>> {
            Ok(repo.ancestry(version).await?.try_collect::<Vec<_>>().await?)
        }
    };
}

macro_rules! fx_ops {
    () => {
        #[allow(dead_code)]
        /// The operations log, newest first, as `(time, kind, backup)` triples.
        async fn ops(
            repo: &Repository,
        ) -> Result<Vec<(chrono::DateTime<Utc>, UpdateType, Option<String>)>, Box<dyn std::error::Error>>
        {
            let (stream, _toc, _version) = repo.ops_log().await?;
            Ok(stream.try_collect::<Vec<_>>().await?)
        }
    };
}

macro_rules! fx_op_kinds {
    () => {
        #[allow(dead_code)]
        /// Just the update kinds from the operations log, newest first.
        async fn op_kinds(repo: &Repository) -> Result<Vec<UpdateType>, Box<dyn std::error::Error>> {
            Ok(ops(repo).await?.into_iter().map(|(_, kind, _)| kind).collect())
        }
    };
}

macro_rules! fx_toc_of {
    () => {
        #[allow(dead_code)]
        /// The repository's table of contents and the storage version it was read at.
        async fn toc_of(
            repo: &Repository,
        ) -> Result<(Arc<RepoInfo>, icechunk::storage::VersionInfo), Box<dyn std::error::Error>> {
            Ok(repo.asset_manager().fetch_repo_info().await?)
        }
    };
}

macro_rules! fx_toc_branches {
    () => {
        #[allow(dead_code)]
        /// Sorted branch names named by the table of contents.
        fn toc_branches(toc: &RepoInfo) -> Vec<String> {
            let mut v: Vec<String> =
                toc.branch_names().expect("branch names").map(str::to_string).collect();
            v.sort();
            v
        }
    };
}

macro_rules! fx_toc_tags {
    () => {
        #[allow(dead_code)]
        /// Sorted tag names named by the table of contents.
        fn toc_tags(toc: &RepoInfo) -> Vec<String> {
            let mut v: Vec<String> =
                toc.tag_names().expect("tag names").map(str::to_string).collect();
            v.sort();
            v
        }
    };
}

macro_rules! fx_staged_v1_repo {
    () => {
        #[allow(dead_code)]
        /// Stage the spec-version-1 fixture into `dir` and open it.
        async fn staged_v1_repo(
            dir: &StdPath,
        ) -> Result<(Repository, Arc<dyn Storage + Send + Sync>), Box<dyn std::error::Error>> {
            let root = dir.join("test-repo-v1");
            copy_tree(StdPath::new(V1_FIXTURE), &root)?;
            let storage = new_local_filesystem_storage(root.as_path()).await?;
            let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
            Ok((repo, storage))
        }
    };
}

macro_rules! fx_seed_movable {
    () => {
        #[allow(dead_code)]
        /// A committed hierarchy `/foo/old/array` holding one inline chunk.
        async fn seed_movable(repo: &Repository) -> Result<(Path, SnapshotId), Box<dyn std::error::Error>> {
            let apath = p("/foo/old/array");
            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root(), Bytes::new()).await?;
            session.add_group(p("/foo"), Bytes::new()).await?;
            session.add_group(p("/foo/old"), Bytes::new()).await?;
            session
                .add_array(
                    apath.clone(),
                    ArrayShape::new(vec![(4u64, 2u32), (4u64, 2u32)]).ok_or("shape")?,
                    None,
                    Bytes::new(),
                )
                .await?;
            session
                .set_chunk_ref(
                    apath.clone(),
                    ChunkIndices(vec![0, 0]),
                    Some(ChunkPayload::Inline("payload".into())),
                )
                .await?;
            let snap = session.commit("first").max_concurrent_nodes(4).execute().await?;
            Ok((apath, snap))
        }
    };
}

macro_rules! fx_vcc_repo {
    () => {
        #[allow(dead_code)]
        /// A local-filesystem repository whose named container `local-data` points at
        /// `chunk_dir`, authorised by URL prefix.
        async fn vcc_repo(
            repo_dir: &StdPath,
            chunk_dir: &StdPath,
        ) -> Result<Repository, Box<dyn std::error::Error>> {
            let prefix = format!("file://{}/", chunk_dir.to_str().ok_or("utf-8 path")?);
            let container = VirtualChunkContainer::new_named(
                "local-data".to_string(),
                prefix,
                ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
            )?;
            let mut config = RepositoryConfig::default();
            config.set_virtual_chunk_container(container)?;
            let creds: HashMap<String, Option<Credentials>> =
                [(format!("file://{}", chunk_dir.to_str().ok_or("utf-8 path")?), None)].into();
            let storage = new_local_filesystem_storage(repo_dir).await?;
            Ok(Repository::create(Some(config), storage, creds, None, true).await?)
        }
    };
}

macro_rules! fx_vcc_array {
    () => {
        #[allow(dead_code)]
        /// The zarr documents a one-chunk virtual array needs.
        async fn vcc_array(store: &Store) -> TestResult {
            store.set("zarr.json", Bytes::from_static(GROUP_META)).await?;
            store
                .set(
                    "array/zarr.json",
                    array_meta("[2]", r#"{"name":"regular","configuration":{"chunk_shape":[1]}}"#),
                )
                .await?;
            Ok(())
        }
    };
}

macro_rules! fx_chunk_refs {
    () => {
        #[allow(dead_code)]
        /// Every `(array path, chunk coordinates, payload)` reachable from a version.
        async fn chunk_refs(
            repo: &Repository,
            version: &VersionInfo,
        ) -> Result<Vec<(String, Vec<u32>, ChunkPayload)>, Box<dyn std::error::Error>> {
            let session = repo.readonly_session(version).await?;
            let mut all: Vec<(String, Vec<u32>, ChunkPayload)> = session
                .all_chunks()
                .await?
                .map_ok(|(path, info)| (path.to_string(), info.coord.0.clone(), info.payload))
                .try_collect()
                .await?;
            all.sort();
            Ok(all)
        }
    };
}

macro_rules! fx_all_refs {
    () => {
        #[allow(dead_code)]
        /// Every ref in the repository, as `(kind, name, snapshot)` triples.
        async fn all_refs(
            repo: &Repository,
        ) -> Result<Vec<(String, String, SnapshotId)>, Box<dyn std::error::Error>> {
            let mut out = Vec::new();
            for branch in repo.list_branches().await? {
                let id = repo.lookup_branch(&branch).await?;
                out.push(("branch".to_string(), branch, id));
            }
            for tag in repo.list_tags().await? {
                let id = repo.lookup_tag(&tag).await?;
                out.push(("tag".to_string(), tag, id));
            }
            out.sort();
            Ok(out)
        }
    };
}

macro_rules! fx_seed_history {
    () => {
        #[allow(dead_code)]
        /// `count` sequential commits on `main`, each writing one inline chunk.
        async fn seed_history(
            repo: &Repository,
            path: &Path,
            count: u32,
        ) -> Result<Vec<SnapshotId>, Box<dyn std::error::Error>> {
            seed_array(repo, path, 4).await?;
            let mut ids = Vec::new();
            for i in 0..count {
                let mut session = repo.writable_session("main").await?;
                session
                    .set_chunk_ref(
                        path.clone(),
                        ChunkIndices(vec![i % 4]),
                        Some(ChunkPayload::Inline(format!("gen-{i}").into())),
                    )
                    .await?;
                ids.push(session.commit(format!("commit {i}")).max_concurrent_nodes(2).execute().await?);
            }
            Ok(ids)
        }
    };
}

macro_rules! fx_splitting {
    () => {
        #[allow(dead_code)]
        /// A manifest configuration that splits every array every `num_chunks` chunks.
        fn splitting(num_chunks: u32) -> ManifestConfig {
            ManifestConfig {
                splitting: Some(ManifestSplittingConfig {
                    split_sizes: Some(vec![(
                        ManifestSplitCondition::PathMatches { regex: r".*".to_string() },
                        vec![ManifestSplitDim {
                            condition: ManifestSplitDimCondition::Any,
                            num_chunks,
                        }],
                    )]),
                }),
                ..ManifestConfig::default()
            }
        }
    };
}

macro_rules! fx_split_repo {
    () => {
        #[allow(dead_code)]
        /// A repository whose manifests are split every `num_chunks` chunks, with an
        /// array of `chunks` materialised chunks committed on `main`.
        async fn split_repo(
            storage: Arc<dyn Storage + Send + Sync>,
            num_chunks: u32,
            chunks: u32,
        ) -> Result<(Repository, Path, SnapshotId), Box<dyn std::error::Error>> {
            let repo = Repository::create(
                Some(RepositoryConfig {
                    inline_chunk_threshold_bytes: Some(0),
                    manifest: Some(splitting(num_chunks)),
                    ..RepositoryConfig::default()
                }),
                storage,
                HashMap::new(),
                None,
                true,
            )
            .await?;
            let path = p("/array");
            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root(), Bytes::new()).await?;
            session
                .add_array(
                    path.clone(),
                    ArrayShape::new(vec![(u64::from(chunks) * 2, chunks)]).ok_or("shape")?,
                    Some(vec!["t".into()]),
                    Bytes::new(),
                )
                .await?;
            for i in 0..chunks {
                let payload = session.get_chunk_writer()?(Bytes::from(format!("chunk-{i}"))).await?;
                session.set_chunk_ref(path.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
            }
            let snap = session.commit("split").max_concurrent_nodes(2).execute().await?;
            Ok((repo, path, snap))
        }
    };
}

macro_rules! fx_s3_options {
    () => {
        #[allow(dead_code)]
        /// Anonymous S3 options, which name no credential and reach no network.
        fn s3_options() -> icechunk::config::S3Options {
            icechunk::config::S3Options {
                region: Some("us-east-1".to_string()),
                endpoint_url: None,
                anonymous: true,
                allow_http: false,
                force_path_style: false,
                network_stream_timeout_seconds: None,
                requester_pays: false,
            }
        }
    };
}


// ---------------------------------------------------------------------------
// The only shared helpers that stay at file scope: none of them names anything
// from the candidate crate, so none of them can sink another group's
// attribution slice. None is a scored case either - the runner only recognises
// a function whose name contains `__` and whose prefix names a group.
// ---------------------------------------------------------------------------

/// A Zarr group document, as a Zarr client writes it.
const GROUP_META: &[u8] = br#"{"zarr_format":3,"node_type":"group"}"#;

/// A Zarr array document with an explicit chunk grid spliced in.
fn array_meta(shape: &str, chunk_grid: &str) -> Bytes {
    Bytes::from(format!(
        r#"{{"zarr_format":3,"node_type":"array","shape":{shape},"data_type":"int32","chunk_grid":{chunk_grid},"chunk_key_encoding":{{"name":"default","configuration":{{"separator":"/"}}}},"fill_value":0,"codecs":[{{"name":"bytes","configuration":{{"endian":"little"}}}}]}}"#
    ))
}

/// The verifier-owned, checksum-verified copy of upstream's committed
/// spec-version-1 repository fixture. It is never migrated in place: each case
/// copies it into its own temporary directory first.
const V1_FIXTURE: &str = "/tests/assets/fixtures/test-repo-v1";

/// Recursive directory copy, so the migration cases never touch the fixture.
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
// n01 — the table of contents names every branch, tag, snapshot and
// configuration value.
//
// Provenance: `icechunk/src/repository.rs::test_manage_refs`; the accessor
// idiom is `icechunk/src/migrations/mod.rs::test_1_to_2_migration`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n01_toc_names_everything__branches_tags_and_snapshots() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_branches!();
    fx_toc_of!();
    fx_toc_tags!();
    let repo = mem_repo().await;
    let snap = seed_root(&repo, "first").await?;
    repo.create_branch("dev", &snap).await?;
    repo.create_tag("v1", &snap).await?;

    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc_branches(&toc), vec!["dev".to_string(), "main".to_string()]);
    assert_eq!(toc_tags(&toc), vec!["v1".to_string()]);
    assert_eq!(toc.resolve_branch("dev")?, snap);
    assert_eq!(toc.resolve_branch("main")?, snap);
    assert_eq!(toc.resolve_tag("v1")?, snap);

    // `branches()`/`tags()` are the paired views: a name with the snapshot it
    // points at, agreeing with `resolve_*` name for name
    let mut pairs: Vec<(String, SnapshotId)> =
        toc.branches()?.map(|(n, id)| (n.to_string(), id)).collect();
    pairs.sort();
    assert_eq!(
        pairs,
        vec![("dev".to_string(), snap.clone()), ("main".to_string(), snap.clone())]
    );
    let tag_pairs: Vec<(String, SnapshotId)> =
        toc.tags()?.map(|(n, id)| (n.to_string(), id)).collect();
    assert_eq!(tag_pairs, vec![("v1".to_string(), snap.clone())]);

    // the initial snapshot plus "first"
    let all: Vec<SnapshotInfo> =
        toc.all_snapshots()?.collect::<Result<Vec<_>, _>>()?;
    assert_eq!(all.len(), 2);
    assert_eq!(toc.find_snapshot(&snap)?.message, "first");

    // the ToC also answers ancestry without reading any snapshot object
    let anc: Vec<SnapshotInfo> = toc.ancestry(&snap)?.collect::<Result<Vec<_>, _>>()?;
    assert_eq!(anc.len(), 2);
    assert_eq!(anc[0].id, snap);
    assert_eq!(anc[1].id, Snapshot::INITIAL_SNAPSHOT_ID);
    assert_eq!(anc[0].parent_id.as_ref(), Some(&Snapshot::INITIAL_SNAPSHOT_ID));

    // and it agrees with the Repository-level views
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), toc_branches(&toc));
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), toc_tags(&toc));
    Ok(())
}

#[tokio::test]
async fn n01_toc_names_everything__config_and_metadata_live_inside() -> TestResult {
    ic_core!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_storage!();
    fx_toc_of!();
    let storage = mem_storage().await;
    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(16),
        ..RepositoryConfig::default()
    };
    let repo =
        Repository::create(Some(config), Arc::clone(&storage), HashMap::new(), None, true)
            .await?;
    repo.save_config().await?;
    repo.set_metadata(&SnapshotProperties::from([(
        "team".to_string(),
        serde_json::json!("science"),
    )]))
    .await?;

    let (toc, _) = toc_of(&repo).await?;
    let stored: RepositoryConfig = toc.config()?.ok_or("no config in the ToC")?;
    assert_eq!(stored.inline_chunk_threshold_bytes, Some(16));
    assert!(toc.config_bytes_raw()?.is_some());
    assert_eq!(toc.metadata()?["team"], serde_json::json!("science"));

    // the same configuration is what the public reader returns
    let (fetched, _) = Repository::fetch_config(Arc::clone(&storage))
        .await?
        .ok_or("fetch_config found nothing")?;
    assert_eq!(fetched.inline_chunk_threshold_bytes, Some(16));
    Ok(())
}

#[tokio::test]
async fn n01_toc_names_everything__spec_version_status_and_flags() -> TestResult {
    ic_core!();
    ic_flags!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let (toc, _) = toc_of(&repo).await?;

    // a repository created without an explicit configuration stores none, and
    // both configuration accessors say so
    assert!(toc.config::<RepositoryConfig>()?.is_none());
    assert!(toc.config_bytes_raw()?.is_none());

    assert_eq!(toc.spec_version()?, SpecVersionBin::V2);
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert_eq!(toc.status()?.availability, RepoAvailability::Online);
    assert!(toc.status()?.limited_availability_reason.is_none());

    // flags at their default are not serialised into the ToC
    assert_eq!(toc.feature_flag_enabled(MOVE_NODE_FLAG)?, None);
    assert!(toc.enabled_feature_flags()?.map(|it| it.len()).unwrap_or(0) == 0);
    assert!(toc.disabled_feature_flags()?.map(|it| it.len()).unwrap_or(0) == 0);

    repo.set_feature_flag("create_tag", Some(false)).await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.feature_flag_enabled(CREATE_TAG_FLAG)?, Some(false));
    let disabled: Vec<u16> =
        toc.disabled_feature_flags()?.ok_or("no disabled flags")?.collect();
    assert_eq!(disabled, vec![CREATE_TAG_FLAG]);
    Ok(())
}

#[tokio::test]
async fn n01_toc_names_everything__deleted_tag_is_remembered() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_of!();
    fx_toc_tags!();
    let repo = mem_repo().await;
    let snap = seed_root(&repo, "first").await?;
    repo.create_tag("v1", &snap).await?;
    repo.delete_tag("v1").await?;

    let (toc, _) = toc_of(&repo).await?;
    assert!(toc_tags(&toc).is_empty());
    assert!(toc.tag_was_deleted("v1")?);
    assert!(!toc.tag_was_deleted("never-existed")?);
    assert_eq!(toc.deleted_tags()?.collect::<Vec<_>>(), vec!["v1"]);

    // a deleted tag's name can never be reused
    assert!(repo.create_tag("v1", &snap).await.is_err());
    assert!(repo.lookup_tag("v1").await.is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// n02 — every kind of mutation changes the table of contents, and the object
// it is stored in is versioned.
//
// Provenance: `icechunk/src/repository.rs::test_manage_refs` and
// `test_ops_log_chain_with_changing_num_updates_per_file`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n02_toc_changes_per_mutation__commit_moves_branch_and_adds_snapshot() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let (before, before_version) = toc_of(&repo).await?;
    assert_eq!(before.resolve_branch("main")?, Snapshot::INITIAL_SNAPSHOT_ID);
    assert_eq!(before.all_snapshots()?.count(), 1);

    let snap = seed_root(&repo, "first").await?;

    let (after, after_version) = toc_of(&repo).await?;
    assert_eq!(after.resolve_branch("main")?, snap);
    assert_eq!(after.all_snapshots()?.count(), 2);
    assert_ne!(before_version, after_version);
    Ok(())
}

#[tokio::test]
async fn n02_toc_changes_per_mutation__branch_lifecycle_is_visible() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_branches!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "first").await?;

    repo.create_branch("dev", &Snapshot::INITIAL_SNAPSHOT_ID).await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.resolve_branch("dev")?, Snapshot::INITIAL_SNAPSHOT_ID);

    repo.reset_branch("dev", &first, Some(&Snapshot::INITIAL_SNAPSHOT_ID)).await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.resolve_branch("dev")?, first);

    repo.delete_branch("dev").await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc_branches(&toc), vec!["main".to_string()]);
    assert!(toc.resolve_branch("dev").is_err());
    Ok(())
}

#[tokio::test]
async fn n02_toc_changes_per_mutation__tag_lifecycle_is_visible() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_of!();
    fx_toc_tags!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "first").await?;

    repo.create_tag("a", &first).await?;
    repo.create_tag("b", &Snapshot::INITIAL_SNAPSHOT_ID).await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc_tags(&toc), vec!["a".to_string(), "b".to_string()]);
    let pairs: HashMap<String, SnapshotId> =
        toc.tags()?.map(|(n, id)| (n.to_string(), id)).collect();
    assert_eq!(pairs["a"], first);
    assert_eq!(pairs["b"], Snapshot::INITIAL_SNAPSHOT_ID);

    repo.delete_tag("b").await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc_tags(&toc), vec!["a".to_string()]);
    Ok(())
}

#[tokio::test]
async fn n02_toc_changes_per_mutation__non_ref_changes_bump_the_version() -> TestResult {
    ic_core!();
    ic_flags!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let (_, v0) = toc_of(&repo).await?;

    repo.set_metadata(&SnapshotProperties::from([(
        "a".to_string(),
        serde_json::json!(1),
    )]))
    .await?;
    let (toc1, v1) = toc_of(&repo).await?;
    assert_ne!(v0, v1);
    assert_eq!(toc1.metadata()?["a"], serde_json::json!(1));

    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("maintenance".to_string()),
    })
    .await?;
    let (toc2, v2) = toc_of(&repo).await?;
    assert_ne!(v1, v2);
    assert_eq!(toc2.status()?.availability, RepoAvailability::ReadOnly);
    assert_eq!(
        toc2.status()?.limited_availability_reason.as_deref(),
        Some("maintenance")
    );

    // restore, then change a feature flag
    repo.set_status(&RepoStatus {
        availability: RepoAvailability::Online,
        set_at: Utc::now(),
        limited_availability_reason: None,
    })
    .await?;
    repo.set_feature_flag("delete_tag", Some(false)).await?;
    let (toc3, v3) = toc_of(&repo).await?;
    assert_ne!(v2, v3);
    assert_eq!(toc3.feature_flag_enabled(DELETE_TAG_FLAG)?, Some(false));

    // §3.2: the named mutators are pure. Each returns a NEW `RepoInfo` carrying
    // the change; the one it was called on keeps showing the state it was
    // fetched at, and nothing reaches storage.
    let spec = repo.spec_version();
    let before = toc3.bytes().to_vec();
    let root = toc3.resolve_branch("main")?;

    let with_branch = toc3.add_branch(spec, "dev", &root, "previous-repo-info", 1000)?;
    assert!(with_branch.branch_names()?.any(|name| name == "dev"));

    let with_tag = toc3.add_tag(spec, "v1", &root, "previous-repo-info", 1000)?;
    assert!(with_tag.tag_names()?.any(|name| name == "v1"));

    let with_metadata = toc3.set_metadata(
        spec,
        &SnapshotProperties::from([("b".to_string(), serde_json::json!(2))]),
        "previous-repo-info",
        1000,
    )?;
    assert_eq!(with_metadata.metadata()?["b"], serde_json::json!(2));

    assert_eq!(toc3.bytes(), before.as_slice());
    assert!(!toc3.branch_names()?.any(|name| name == "dev"));
    assert_eq!(toc3.tag_names()?.count(), 0);
    assert!(toc3.metadata()?.get("b").is_none());
    let (stored, v4) = toc_of(&repo).await?;
    assert_eq!(v3, v4);
    assert!(!stored.branch_names()?.any(|name| name == "dev"));
    assert_eq!(stored.tag_names()?.count(), 0);
    Ok(())
}

#[tokio::test]
async fn n02_toc_changes_per_mutation__repo_info_bytes_round_trip() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_branches!();
    fx_toc_of!();
    fx_toc_tags!();
    let repo = mem_repo().await;
    let snap = seed_root(&repo, "first").await?;
    repo.create_tag("v1", &snap).await?;

    let (toc, _) = toc_of(&repo).await?;
    let rebuilt = RepoInfo::from_buffer(toc.bytes().to_vec())?;
    assert_eq!(toc_branches(&rebuilt), toc_branches(&toc));
    assert_eq!(toc_tags(&rebuilt), toc_tags(&toc));
    assert_eq!(rebuilt.spec_version()?, toc.spec_version()?);
    assert_eq!(rebuilt.resolve_tag("v1")?, snap);
    assert_eq!(rebuilt.all_snapshots()?.count(), toc.all_snapshots()?.count());
    Ok(())
}

// ---------------------------------------------------------------------------
// n03 — the operations log.
//
// Provenance: `icechunk/src/feature_flags.rs::set_and_unset_flags_on_repo`
// (the ops-log collection idiom, verbatim) and
// `icechunk/src/migrations/mod.rs::test_1_to_2_migration` (ordering).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n03_ops_log__tuple_order_and_newest_first() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_ops!();
    fx_seed_root!();
    fx_toc_of!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    let entries = ops(&repo).await?;
    assert_eq!(entries.len(), 2);

    // `(DateTime<Utc>, UpdateType, Option<String>)` — time first
    let (newest_time, newest_kind, _backup) = &entries[0];
    let (oldest_time, oldest_kind, _) = &entries[1];
    assert!(matches!(newest_kind, UpdateType::NewCommitUpdate { branch, .. } if branch == "main"));
    assert!(matches!(oldest_kind, UpdateType::RepoInitializedUpdate));
    assert!(newest_time > oldest_time);

    // `RepoInfo::latest_updates` uses the other order: kind first, then time
    let (toc, _) = toc_of(&repo).await?;
    let latest: Vec<(UpdateType, chrono::DateTime<Utc>, Option<String>)> = toc
        .latest_updates()?
        .map(|r| r.map(|(k, t, b)| (k, t, b.map(str::to_string))))
        .collect::<Result<Vec<_>, _>>()?;
    assert_eq!(latest.len(), 2);
    assert!(matches!(&latest[0].0, UpdateType::NewCommitUpdate { branch, .. } if branch == "main"));
    assert_eq!(&latest[0].1, newest_time);

    // the bound that decides when the log spills into a backup file, and its
    // default; two updates are far inside it, so the current `repo` object holds
    // the whole log and names no earlier copy
    assert_eq!(RepositoryConfig::default().num_updates_per_repo_info_file(), 1_000);
    assert_eq!(toc.repo_before_updates()?, None);
    Ok(())
}

#[tokio::test]
async fn n03_ops_log__records_non_commit_operations() -> TestResult {
    ic_core!();
    ic_flags!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_seed_root!();
    let repo = mem_repo().await;
    let snap = seed_root(&repo, "first").await?;
    repo.create_branch("dev", &snap).await?;
    repo.reset_branch("dev", &Snapshot::INITIAL_SNAPSHOT_ID, Some(&snap)).await?;
    repo.create_tag("v1", &snap).await?;
    repo.delete_tag("v1").await?;
    repo.delete_branch("dev").await?;
    repo.set_metadata(&SnapshotProperties::from([(
        "k".to_string(),
        serde_json::json!("v"),
    )]))
    .await?;
    repo.set_feature_flag("create_tag", Some(false)).await?;
    repo.save_config().await?;

    let kinds = op_kinds(&repo).await?;
    // newest first; `ancestry` could only ever have shown the single commit
    assert!(matches!(kinds[0], UpdateType::ConfigChangedUpdate));
    assert!(matches!(
        kinds[1],
        UpdateType::FeatureFlagChanged { id: CREATE_TAG_FLAG, new_value: Some(false) }
    ));
    assert!(matches!(kinds[2], UpdateType::MetadataChangedUpdate));
    assert!(matches!(&kinds[3], UpdateType::BranchDeletedUpdate { name, .. } if name == "dev"));
    assert!(matches!(&kinds[4], UpdateType::TagDeletedUpdate { name, .. } if name == "v1"));
    assert!(matches!(&kinds[5], UpdateType::TagCreatedUpdate { name } if name == "v1"));
    assert!(matches!(&kinds[6], UpdateType::BranchResetUpdate { name, .. } if name == "dev"));
    assert!(matches!(&kinds[7], UpdateType::BranchCreatedUpdate { name } if name == "dev"));
    assert!(matches!(&kinds[8], UpdateType::NewCommitUpdate { branch, .. } if branch == "main"));
    assert!(matches!(kinds[9], UpdateType::RepoInitializedUpdate));
    assert_eq!(kinds.len(), 10);
    Ok(())
}

#[tokio::test]
async fn n03_ops_log__commit_amend_and_anonymous_kinds() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_p!();
    fx_seed_root!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "first").await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    let amended = session.commit("amended").max_concurrent_nodes(2).amend().execute().await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/b"), Bytes::new()).await?;
    let detached =
        session.commit("detached").max_concurrent_nodes(2).anonymous().execute().await?;

    let kinds = op_kinds(&repo).await?;
    assert!(matches!(
        &kinds[0],
        UpdateType::NewDetachedSnapshotUpdate { new_snap_id } if *new_snap_id == detached
    ));
    assert!(matches!(
        &kinds[1],
        UpdateType::CommitAmendedUpdate { branch, previous_snap_id, new_snap_id }
            if branch == "main" && *previous_snap_id == first && *new_snap_id == amended
    ));
    assert!(matches!(
        &kinds[2],
        UpdateType::NewCommitUpdate { branch, new_snap_id }
            if branch == "main" && *new_snap_id == first
    ));
    Ok(())
}

#[tokio::test]
async fn n03_ops_log__gc_and_expiration_are_recorded() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_repo_info!();
    ic_snapshot!();
    ic_gc!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_op_kinds!();
    fx_ops!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    let cutoff = Utc::now();
    expire(
        Arc::clone(repo.asset_manager()),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;

    let gc_config = GCConfig::clean_all(
        cutoff,
        cutoff,
        None,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        false,
    );
    garbage_collect(Arc::clone(repo.asset_manager()), &gc_config, None, 100).await?;

    let kinds = op_kinds(&repo).await?;
    assert!(kinds.iter().any(|k| matches!(k, UpdateType::GCRanUpdate)));
    assert!(kinds.iter().any(|k| matches!(k, UpdateType::ExpirationRanUpdate)));
    // `ancestry` cannot show either of them: the branch's history is unchanged
    assert_eq!(messages(&repo, &tip("main")).await?.len(), 2);
    Ok(())
}

#[tokio::test]
async fn n03_ops_log__covers_status_and_migration_kinds() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_migrations!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_staged_v1_repo!();
    let repo = mem_repo().await;
    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("frozen".to_string()),
    })
    .await?;

    let kinds = op_kinds(&repo).await?;
    assert!(matches!(
        &kinds[0],
        UpdateType::RepoStatusChangedUpdate { status }
            if status.availability == RepoAvailability::ReadOnly
    ));

    // the migration kind is reachable only through the upgrade path
    let dir = tempfile::tempdir()?;
    let (v1, storage) = staged_v1_repo(dir.path()).await?;
    migrate_1_to_2(v1, false, true, None).await?;
    let migrated = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let kinds = op_kinds(&migrated).await?;
    assert!(matches!(
        &kinds[0],
        UpdateType::RepoMigratedUpdate { from_version: SpecVersionBin::V1, to_version: SpecVersionBin::V2 }
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// n04 — optimistic concurrency on the single mutable object.
//
// Provenance: `icechunk/src/asset_manager.rs::update_repo_info` and
// `icechunk/src/repository.rs::test_manage_refs` (the fail-fast reset).
// The retry schedule itself is deliberately not asserted; only the observable
// serialisation of concurrent mutations is.
// ---------------------------------------------------------------------------

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn n04_optimistic_concurrency__concurrent_mutations_all_land() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_branches!();
    fx_toc_of!();
    let storage = mem_storage().await;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let snap = seed_root(&repo, "first").await?;

    // four independent handles on one repository, each mutating the ToC
    let mut handles = Vec::new();
    for i in 0..4u32 {
        let storage = Arc::clone(&storage);
        let snap = snap.clone();
        handles.push(tokio::spawn(async move {
            // NOTE: the blanket `impl<E> From<E> for ICError<Kind>` impls are gone at
            // this release, so a task mixing repository and session errors must box
            // them rather than rely on `?` converting between error kinds.
            let out: Result<(), Box<dyn std::error::Error + Send + Sync>> = async {
                let repo = Repository::open(None, storage, HashMap::new()).await?;
                repo.create_branch(&format!("b{i}"), &snap).await?;
                Ok(())
            }
            .await;
            out
        }));
    }
    for handle in handles {
        handle.await?.map_err(|e| -> Box<dyn std::error::Error> { e })?;
    }

    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(
        toc_branches(&toc),
        vec![
            "b0".to_string(),
            "b1".to_string(),
            "b2".to_string(),
            "b3".to_string(),
            "main".to_string()
        ]
    );
    for i in 0..4u32 {
        assert_eq!(toc.resolve_branch(&format!("b{i}"))?, snap);
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread", worker_threads = 4)]
async fn n04_optimistic_concurrency__concurrent_commits_serialise() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_p!();
    fx_seed_root!();
    fx_toc_of!();
    let storage = mem_storage().await;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let snap = seed_root(&repo, "first").await?;
    repo.create_branch("other", &snap).await?;

    fn commit_on(
        storage: Arc<dyn Storage + Send + Sync>,
        branch: &'static str,
        group: &'static str,
        message: &'static str,
    ) -> tokio::task::JoinHandle<Result<SnapshotId, Box<dyn std::error::Error + Send + Sync>>>
    {
        tokio::spawn(async move {
            let repo = Repository::open(None, storage, HashMap::new()).await?;
            let mut session = repo.writable_session(branch).await?;
            session.add_group(p(group), Bytes::new()).await?;
            let id = session.commit(message).max_concurrent_nodes(2).execute().await?;
            Ok(id)
        })
    }
    let a = commit_on(Arc::clone(&storage), "main", "/from-main", "on main");
    let b = commit_on(Arc::clone(&storage), "other", "/from-other", "on other");
    let on_main = a.await?.map_err(|e| -> Box<dyn std::error::Error> { e })?;
    let on_other = b.await?.map_err(|e| -> Box<dyn std::error::Error> { e })?;
    assert_ne!(on_main, on_other);

    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.resolve_branch("main")?, on_main);
    assert_eq!(toc.resolve_branch("other")?, on_other);
    // both commits are recorded exactly once each
    let commits = op_kinds(&repo)
        .await?
        .into_iter()
        .filter(|k| matches!(k, UpdateType::NewCommitUpdate { .. }))
        .count();
    assert_eq!(commits, 3);
    Ok(())
}

#[tokio::test(flavor = "multi_thread", worker_threads = 2)]
async fn n04_optimistic_concurrency__one_racing_reset_wins_and_the_other_conflicts()
-> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    // Unlike the two cases above, this one does not depend on the tasks actually
    // overlapping: whichever order they run in, the second `reset_branch` sees a
    // branch that is no longer at the parent it was told to expect.
    let storage = mem_storage().await;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let parent = seed_root(&repo, "first").await?;
    repo.create_branch("dev", &parent).await?;

    // two candidate tips, each committed on a branch of its own from that parent
    let mut targets: Vec<SnapshotId> = Vec::new();
    for (i, branch) in ["t0", "t1"].into_iter().enumerate() {
        repo.create_branch(branch, &parent).await?;
        let mut session = repo.writable_session(branch).await?;
        session.add_group(p(&format!("/g{i}")), Bytes::new()).await?;
        targets.push(session.commit(format!("tip {i}")).execute().await?);
    }

    fn reset_dev(
        storage: Arc<dyn Storage + Send + Sync>,
        to: SnapshotId,
        from: SnapshotId,
    ) -> tokio::task::JoinHandle<Result<(), RepositoryError>> {
        tokio::spawn(async move {
            let repo = Repository::open(None, storage, HashMap::new()).await?;
            repo.reset_branch("dev", &to, Some(&from)).await
        })
    }
    let a = reset_dev(Arc::clone(&storage), targets[0].clone(), parent.clone());
    let b = reset_dev(Arc::clone(&storage), targets[1].clone(), parent.clone());
    let (ra, rb) = (a.await?, b.await?);

    // exactly one conditional write may be allowed to land
    let (winner, err) = match (ra, rb) {
        (Ok(()), Err(e)) => (targets[0].clone(), e),
        (Err(e), Ok(())) => (targets[1].clone(), e),
        (Ok(()), Ok(())) => {
            return Err("both resets from the same parent were allowed to land".into());
        }
        (Err(_), Err(_)) => return Err("neither reset from the parent landed".into()),
    };
    assert!(matches!(err.kind, RepositoryErrorKind::Conflict { .. }));
    assert_eq!(repo.lookup_branch("dev").await?, winner);
    Ok(())
}

#[tokio::test]
async fn n04_optimistic_concurrency__incompatible_change_fails_fast() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_branches!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "first").await?;
    repo.create_branch("dev", &first).await?;

    // resetting from a parent the branch is not at is refused, and changes nothing
    let err = repo
        .reset_branch("dev", &Snapshot::INITIAL_SNAPSHOT_ID, Some(&Snapshot::INITIAL_SNAPSHOT_ID))
        .await
        .unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::Conflict { .. }));
    assert_eq!(repo.lookup_branch("dev").await?, first);

    // and a branch another session deleted cannot be reset at all
    repo.delete_branch("dev").await?;
    assert!(repo.reset_branch("dev", &first, None).await.is_err());
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc_branches(&toc), vec!["main".to_string()]);
    Ok(())
}

#[tokio::test]
async fn n04_optimistic_concurrency__fetched_table_of_contents_is_immutable() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_toc_branches!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let snap = seed_root(&repo, "first").await?;

    let (before, before_version) = toc_of(&repo).await?;
    repo.create_branch("dev", &snap).await?;
    let (after, after_version) = toc_of(&repo).await?;

    // the mutator produced a new value; the one already in hand did not change
    assert_eq!(toc_branches(&before), vec!["main".to_string()]);
    assert_eq!(toc_branches(&after), vec!["dev".to_string(), "main".to_string()]);
    assert_ne!(before_version, after_version);
    assert_ne!(before.bytes(), after.bytes());
    Ok(())
}

// ---------------------------------------------------------------------------
// n05 — `Session::commit` is a builder finished with `.execute()`.
//
// Provenance: `icechunk/src/session.rs::{test_commit_and_refs, test_flush}`
// and `icechunk/src/ops/manifests.rs::rewrite_manifests` (the builder call it
// makes internally, verbatim).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n05_commit_builder__commit_is_not_async_and_execute_returns_the_id() -> TestResult {
    ic_core!();
    ic_format!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;

    // `commit` itself is a plain call; only `execute` is awaited
    let builder = session.commit("first");
    let snap = builder.execute().await?;

    assert_eq!(repo.lookup_branch("main").await?, snap);
    assert_eq!(repo.lookup_snapshot(&snap).await?.message, "first");
    assert!(!session.has_uncommitted_changes());
    Ok(())
}

#[tokio::test]
async fn n05_commit_builder__allow_empty_is_required_for_an_unchanged_session() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    let mut session = repo.writable_session("main").await?;
    let err = session.commit("nothing").execute().await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NoChangesToCommit));

    let mut session = repo.writable_session("main").await?;
    let snap = session.commit("nothing").allow_empty(true).execute().await?;
    assert_eq!(repo.lookup_snapshot(&snap).await?.message, "nothing");
    assert_eq!(messages(&repo, &tip("main")).await?.len(), 3);
    Ok(())
}

#[tokio::test]
async fn n05_commit_builder__properties_are_recorded_on_the_snapshot() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let props =
        SnapshotProperties::from([("run".to_string(), serde_json::json!(42))]);

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session
        .commit("with properties")
        .properties(props.clone())
        .max_concurrent_nodes(2)
        .execute()
        .await?;

    assert_eq!(repo.lookup_snapshot(&snap).await?.metadata, props);
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.find_snapshot(&snap)?.metadata, props);
    Ok(())
}

#[tokio::test]
async fn n05_commit_builder__max_concurrent_nodes_flushes_arrays_in_parallel() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_tip!();
    let repo = mem_repo().await;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let shape = ArrayShape::new(vec![(4u64, 2u32)]).ok_or("shape")?;
    for i in 0..4u32 {
        let path = p(&format!("/a{i}"));
        session.add_array(path.clone(), shape.clone(), None, Bytes::new()).await?;
        session
            .set_chunk_ref(
                path,
                ChunkIndices(vec![0]),
                Some(ChunkPayload::Inline(format!("v{i}").into())),
            )
            .await?;
    }
    session.commit("many arrays").max_concurrent_nodes(4).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(
            chunk_bytes(&session, &p(&format!("/a{i}")), 0).await?,
            Some(Bytes::from(format!("v{i}")))
        );
    }
    assert_eq!(session.list_nodes(&Path::root()).await?.count(), 5);
    Ok(())
}

#[tokio::test]
async fn n05_commit_builder__rewrite_manifests_keeps_every_chunk() -> TestResult {
    ic_core!();
    ic_conflicts!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_rewrite_manifests!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    let mut session = repo.writable_session("main").await?;
    let rewritten = session
        .commit("rewrite")
        .max_concurrent_nodes(2)
        .rewrite_manifests()
        .execute()
        .await?;
    assert_eq!(repo.lookup_branch("main").await?, rewritten);

    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(chunk_bytes(&session, &path, i).await?, Some(Bytes::from(i.to_string())));
    }
    // rewriting cannot be combined with rebase
    let solver = BasicConflictSolver::default();
    let mut session = repo.writable_session("main").await?;
    let err = session
        .commit("bad")
        .rewrite_manifests()
        .rebase(&solver, 3)
        .execute()
        .await
        .unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::InvalidCommitConfiguration { reason }
            if reason == "rewrite_manifests cannot be combined with rebase"
    ));
    Ok(())
}

#[tokio::test]
async fn n05_commit_builder__rebase_hooks_require_a_rebase() -> TestResult {
    ic_core!();
    ic_conflicts!();
    ic_format!();
    ic_repository!();
    ic_session!();
    ic_session_new!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    fn noop_hook() -> RebaseHook {
        Box::new(|_attempt: u16| {
            let fut: std::pin::Pin<Box<dyn std::future::Future<Output = ()> + Send>> =
                Box::pin(async {});
            fut
        })
    }

    // a hook with no `.rebase(..)` is refused, and nothing is written
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    let err = session
        .commit("hooked")
        .before_rebase_hook(noop_hook())
        .execute()
        .await
        .unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::InvalidCommitConfiguration { reason }
            if reason == "rebase hooks require .rebase() to be set"
    ));
    assert_eq!(messages(&repo, &tip("main")).await?.len(), 2);

    // the same hook alongside a solver is a legal configuration
    let solver = BasicConflictSolver::default();
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/b"), Bytes::new()).await?;
    let id = session
        .commit("hooked and rebasing")
        .rebase(&solver, 3)
        .after_rebase_hook(noop_hook())
        .execute()
        .await?;
    assert_eq!(repo.lookup_branch("main").await?, id);
    Ok(())
}

// ---------------------------------------------------------------------------
// n06 — `amend` replaces the branch's last commit.
//
// Provenance: `icechunk/src/session.rs::test_amend` (line 4930), trimmed.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n06_amend__replaces_the_branch_tip() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    ic_repository!();
    fx_history!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "make root").await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    let amended_away =
        session.commit("will be amended").max_concurrent_nodes(8).execute().await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/b"), Bytes::new()).await?;
    let amending =
        session.commit("first amend").max_concurrent_nodes(8).amend().execute().await?;

    assert_eq!(
        messages(&repo, &tip("main")).await?,
        vec![
            "first amend".to_string(),
            "make root".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string()
        ]
    );
    assert!(!history(&repo, &tip("main")).await?.iter().any(|s| s.id == amended_away));
    assert_ne!(amending, amended_away);

    // both the amended-away and the amending changes are present in the tip
    let session = repo.readonly_session(&tip("main")).await?;
    assert!(session.get_group(&p("/a")).await.is_ok());
    assert!(session.get_group(&p("/b")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn n06_amend__the_initial_commit_cannot_be_amended() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let err = session
        .commit("nope")
        .max_concurrent_nodes(8)
        .amend()
        .execute()
        .await
        .unwrap_err();
    assert!(err.to_string().contains("first commit"));

    let mut session = repo.writable_session("main").await?;
    let err = session
        .commit("nope")
        .max_concurrent_nodes(8)
        .amend()
        .allow_empty(true)
        .execute()
        .await
        .unwrap_err();
    assert!(err.to_string().contains("first commit"));

    // the branch never moved
    assert_eq!(repo.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);
    Ok(())
}

#[tokio::test]
async fn n06_amend__an_empty_amend_edits_only_the_message() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 3).await?;

    let mut session = repo.writable_session("main").await?;
    session
        .commit("better message")
        .amend()
        .allow_empty(true)
        .max_concurrent_nodes(2)
        .execute()
        .await?;

    assert_eq!(
        messages(&repo, &tip("main")).await?,
        vec![
            "better message".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string()
        ]
    );
    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..3u32 {
        assert_eq!(chunk_bytes(&session, &path, i).await?, Some(Bytes::from(i.to_string())));
    }
    Ok(())
}

#[tokio::test]
async fn n06_amend__is_recorded_as_its_own_update_kind() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_p!();
    fx_seed_root!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "first").await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    let amending =
        session.commit("amended").max_concurrent_nodes(2).amend().execute().await?;

    let kinds = op_kinds(&repo).await?;
    assert!(matches!(
        &kinds[0],
        UpdateType::CommitAmendedUpdate { branch, previous_snap_id, new_snap_id }
            if branch == "main" && *previous_snap_id == first && *new_snap_id == amending
    ));
    // the amended-away snapshot is gone from the table of contents' branch view
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.resolve_branch("main")?, amending);
    let anc: Vec<SnapshotInfo> =
        toc.ancestry(&amending)?.collect::<Result<Vec<_>, _>>()?;
    assert!(!anc.iter().any(|s| s.id == first));
    Ok(())
}

// ---------------------------------------------------------------------------
// n07 — anonymous (detached) snapshots.
//
// Provenance: `icechunk/src/session.rs::test_flush` (line 5698).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n07_anonymous_snapshots__are_detached_from_every_ref() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    let repo = mem_repo().await;
    let path = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(path.clone(), ArrayShape::new(vec![(2u64, 1u32)]).ok_or("shape")?, None, Bytes::new())
        .await?;
    session
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("hello".into())),
        )
        .await?;
    let snap = session.commit("flush").max_concurrent_nodes(8).anonymous().execute().await?;

    // the session detaches and is clean afterwards
    assert!(session.branch().is_none());
    assert!(!session.has_uncommitted_changes());
    assert_eq!(session.snapshot_id(), &snap);

    // no branch and no tag points at it, but it is readable
    assert_eq!(repo.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);
    assert!(repo.list_tags().await?.is_empty());
    let detached = repo.readonly_session(&VersionInfo::SnapshotId(snap.clone())).await?;
    assert_eq!(chunk_bytes(&detached, &path, 0).await?, Some(Bytes::from_static(b"hello")));
    Ok(())
}

#[tokio::test]
async fn n07_anonymous_snapshots__are_adoptable_by_a_branch() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    ic_repository!();
    fx_history!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_tip!();
    let repo = mem_repo().await;
    let props = SnapshotProperties::from([("test".to_string(), serde_json::json!(42))]);

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session
        .commit("flush")
        .max_concurrent_nodes(8)
        .anonymous()
        .properties(props.clone())
        .execute()
        .await?;

    repo.reset_branch("main", &snap, Some(&Snapshot::INITIAL_SNAPSHOT_ID)).await?;
    let parents = history(&repo, &tip("main")).await?;
    assert_eq!(parents.len(), 2);
    assert_eq!(parents[0].id, snap);
    assert_eq!(parents[0].metadata, props);
    assert_eq!(parents[1].id, Snapshot::INITIAL_SNAPSHOT_ID);
    Ok(())
}

#[tokio::test]
async fn n07_anonymous_snapshots__cannot_be_amended_or_rebased() -> TestResult {
    ic_core!();
    ic_conflicts!();
    ic_format!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    let err = session
        .commit("nope")
        .anonymous()
        .amend()
        .execute()
        .await
        .unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::InvalidCommitConfiguration { reason }
            if reason == "anonymous commits cannot be amended"
    ));

    let solver = BasicConflictSolver::default();
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/b"), Bytes::new()).await?;
    let err = session
        .commit("nope")
        .anonymous()
        .rebase(&solver, 2)
        .execute()
        .await
        .unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::InvalidCommitConfiguration { reason }
            if reason == "anonymous commits cannot use rebase"
    ));
    Ok(())
}

#[tokio::test]
async fn n07_anonymous_snapshots__are_tracked_in_the_table_of_contents() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_toc_branches!();
    fx_toc_of!();
    let repo = mem_repo().await;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("detached").anonymous().execute().await?;

    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.find_snapshot(&snap)?.message, "detached");
    assert_eq!(toc.all_snapshots()?.count(), 2);
    assert!(!toc_branches(&toc).iter().any(|b| b == "detached"));

    let kinds = op_kinds(&repo).await?;
    assert!(matches!(
        &kinds[0],
        UpdateType::NewDetachedSnapshotUpdate { new_snap_id } if *new_snap_id == snap
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// n08 — fork sessions: an anonymous snapshot used as a distributed-write base.
//
// Provenance: `icechunk/src/repository.rs::test_concurrent_distributer_writers_with_base_state`
// and `icechunk/tests/test_distributed_writes.rs` (the as_bytes/from_bytes idiom).
// There is no Rust type named `ForkSession`; a fork is a `Session` whose
// `branch()` is `None`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n08_fork_sessions__fork_captures_uncommitted_changes() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    let repo = mem_repo().await;
    let path = p("/array");
    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(
        path.clone(),
        ArrayShape::new(vec![(4u64, 2u32)]).ok_or("shape")?,
        Some(vec!["x".into()]),
        Bytes::from_static(b"{}"),
    )
    .await?;
    // the base has uncommitted changes and has never committed
    assert!(base.has_uncommitted_changes());

    let fork = base.fork().await?;
    assert!(fork.is_fork());
    assert!(fork.branch().is_none());
    assert!(!fork.read_only());
    // the fork is based on an anonymous snapshot, not on the branch tip
    assert_ne!(fork.snapshot_id(), base.snapshot_id());
    assert!(!fork.has_uncommitted_changes());
    // the array the base had not committed is visible from the fork
    assert!(fork.get_array(&path).await.is_ok());
    // and the base is still the branch session it was
    assert_eq!(base.branch(), Some("main"));
    assert!(!base.is_fork());
    Ok(())
}

#[tokio::test]
async fn n08_fork_sessions__serialise_to_workers_and_merge_back() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_p!();
    fx_tip!();
    // A serialised session carries its storage configuration, not the bytes
    // behind it, so this case uses a filesystem-backed repository: a worker
    // rebuilt with `Session::from_bytes` must reach the same objects.
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, storage, HashMap::new(), None, true).await?;
    let path = p("/array");
    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(
        path.clone(),
        ArrayShape::new(vec![(4u64, 2u32)]).ok_or("shape")?,
        None,
        Bytes::new(),
    )
    .await?;

    let fork = base.fork().await?;
    let bytes = fork.as_bytes()?;
    let mut w0 = Session::from_bytes(&bytes)?;
    let mut w1 = Session::from_bytes(&bytes)?;
    assert!(w0.is_fork() && w1.is_fork());
    w0.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(ChunkPayload::Inline("0".into())))
        .await?;
    w1.set_chunk_ref(path.clone(), ChunkIndices(vec![1]), Some(ChunkPayload::Inline("1".into())))
        .await?;

    w0.merge(w1).await?;
    base.merge(w0).await?;
    for i in 0..2u32 {
        assert_eq!(chunk_bytes(&base, &path, i).await?, Some(Bytes::from(i.to_string())));
    }
    base.commit("distributed").max_concurrent_nodes(4).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..2u32 {
        assert_eq!(chunk_bytes(&session, &path, i).await?, Some(Bytes::from(i.to_string())));
    }
    Ok(())
}

#[tokio::test]
async fn n08_fork_sessions__merge_direction_is_enforced() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(p("/a"), Bytes::new()).await?;
    let mut fork = base.fork().await?;
    fork.add_group(p("/b"), Bytes::new()).await?;

    // fork.merge(base) is refused ...
    let err = fork.merge(base.clone()).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::MergeNotAllowed));
    // ... a read-only receiver is refused separately, whatever it is handed ...
    let mut reader = repo.readonly_session(&tip("main")).await?;
    let err = reader.merge(fork.clone()).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::ReadOnlySession));
    // ... base.merge(fork) is the supported direction
    base.merge(fork).await?;
    assert!(base.get_group(&p("/b")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn n08_fork_sessions__cannot_commit_to_a_branch() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(p("/a"), Bytes::new()).await?;
    let mut fork = base.fork().await?;
    fork.add_group(p("/b"), Bytes::new()).await?;

    let err = fork.commit("nope").execute().await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::CommitNotAllowed));
    // but an anonymous commit from a fork is allowed
    let detached = fork.commit("anonymous from a fork").anonymous().execute().await?;
    assert_eq!(repo.lookup_snapshot(&detached).await?.message, "anonymous from a fork");

    // a read-only session cannot be forked at all
    let reader = repo.readonly_session(&tip("main")).await?;
    let err = reader.fork().await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::CannotForkReadOnlySession));
    assert!(!reader.is_fork());
    Ok(())
}

#[tokio::test]
async fn n08_fork_sessions__result_equals_the_sequential_writes() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_tip!();
    let path = p("/array");

    // forked, distributed writes
    let forked = mem_repo().await;
    let mut base = forked.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(
        path.clone(),
        ArrayShape::new(vec![(8u64, 4u32)]).ok_or("shape")?,
        None,
        Bytes::new(),
    )
    .await?;
    let fork = base.fork().await?;
    for i in 0..4u32 {
        let mut worker = fork.clone();
        worker
            .set_chunk_ref(
                path.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(format!("chunk-{i}").into())),
            )
            .await?;
        base.merge(worker).await?;
    }
    base.commit("forked").max_concurrent_nodes(4).execute().await?;

    // the same writes, done sequentially in one session
    let sequential = mem_repo().await;
    let mut session = sequential.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path.clone(),
            ArrayShape::new(vec![(8u64, 4u32)]).ok_or("shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..4u32 {
        session
            .set_chunk_ref(
                path.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(format!("chunk-{i}").into())),
            )
            .await?;
    }
    session.commit("sequential").max_concurrent_nodes(4).execute().await?;

    let a = forked.readonly_session(&tip("main")).await?;
    let b = sequential.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(chunk_bytes(&a, &path, i).await?, chunk_bytes(&b, &path, i).await?);
        assert_eq!(chunk_bytes(&a, &path, i).await?, Some(Bytes::from(format!("chunk-{i}"))));
    }
    let mut nodes_a: Vec<String> =
        a.list_nodes(&Path::root()).await?.map(|n| n.expect("node").path.to_string()).collect();
    let mut nodes_b: Vec<String> =
        b.list_nodes(&Path::root()).await?.map(|n| n.expect("node").path.to_string()).collect();
    nodes_a.sort();
    nodes_b.sort();
    assert_eq!(nodes_a, nodes_b);
    Ok(())
}

// ---------------------------------------------------------------------------
// n09 — `move_node`: metadata-only relabelling in a rearrange session.
//
// Provenance: `icechunk/src/session.rs::{test_basic_move,
// test_move_into_then_move_parent}`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n09_move_node__requires_a_rearrange_session() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    ic_session_new!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_movable(&repo).await?;

    let mut writable = repo.writable_session("main").await?;
    let err = writable.move_node(p("/foo/old"), p("/foo/new")).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NonRearrangeSession));

    let mut rearrange = repo.rearrange_session("main").await?;
    assert_eq!(rearrange.mode(), SessionMode::Rearrange);
    rearrange.move_node(p("/foo/old"), p("/foo/new")).await?;
    rearrange.commit("moved").max_concurrent_nodes(4).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    assert!(session.get_group(&p("/foo/new")).await.is_ok());
    assert!(session.get_group(&p("/foo/old")).await.is_err());
    Ok(())
}

#[tokio::test]
async fn n09_move_node__copies_no_chunk_bytes() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    let (apath, _) = seed_movable(&repo).await?;

    let before = repo.readonly_session(&tip("main")).await?;
    let reader = before
        .get_chunk_reader(&apath, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
        .await?;
    let before_bytes = get_chunk(reader).await?;
    assert_eq!(before_bytes, Some(Bytes::from_static(b"payload")));
    let chunks_before = repo.asset_manager().list_chunks().await?.count().await;

    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/foo/old"), p("/foo/new")).await?;
    rearrange.commit("moved").max_concurrent_nodes(4).execute().await?;

    let after = repo.readonly_session(&tip("main")).await?;
    let moved = p("/foo/new/array");
    assert!(after.get_node(&apath).await.is_err());
    let reader = after
        .get_chunk_reader(&moved, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
        .await?;
    assert_eq!(get_chunk(reader).await?, before_bytes);
    // no chunk object was written or copied
    assert_eq!(repo.asset_manager().list_chunks().await?.count().await, chunks_before);
    Ok(())
}

#[tokio::test]
async fn n09_move_node__a_group_moves_with_its_children() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_movable(&repo).await?;

    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/foo"), p("/bar")).await?;
    rearrange.commit("moved subtree").max_concurrent_nodes(4).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    let mut paths: Vec<String> = session
        .list_nodes(&Path::root())
        .await?
        .map(|n| n.expect("node").path.to_string())
        .collect();
    paths.sort();
    assert_eq!(
        paths,
        vec![
            "/".to_string(),
            "/bar".to_string(),
            "/bar/old".to_string(),
            "/bar/old/array".to_string(),
        ]
    );
    Ok(())
}

#[tokio::test]
async fn n09_move_node__refuses_to_overwrite() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_movable(&repo).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/foo/taken"), Bytes::new()).await?;
    session.commit("destination exists").max_concurrent_nodes(4).execute().await?;

    let mut rearrange = repo.rearrange_session("main").await?;
    let err = rearrange.move_node(p("/foo/old"), p("/foo/taken")).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::MoveWontOverwrite(_)));

    // the hierarchy is untouched
    let session = repo.readonly_session(&tip("main")).await?;
    assert!(session.get_group(&p("/foo/old")).await.is_ok());
    assert!(session.get_group(&p("/foo/taken")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn n09_move_node__is_reported_as_a_move_in_the_diff() -> TestResult {
    ic_core!();
    ic_diff!();
    ic_format!();
    ic_node_type!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    let (_, before) = seed_movable(&repo).await?;

    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/foo/old"), p("/foo/new")).await?;
    rearrange.commit("moved").max_concurrent_nodes(4).execute().await?;

    let diff: Diff =
        repo.diff(&VersionInfo::SnapshotId(before), &tip("main")).await?;
    assert!(!diff.is_empty());
    // the group and the array under it are both relabelled
    let mut moves: Vec<(String, String, bool)> = diff
        .moved_nodes
        .iter()
        .map(|m| {
            (m.from.to_string(), m.to.to_string(), m.node_type == NodeType::Array)
        })
        .collect();
    moves.sort();
    assert_eq!(
        moves,
        vec![
            ("/foo/old".to_string(), "/foo/new".to_string(), false),
            ("/foo/old/array".to_string(), "/foo/new/array".to_string(), true),
        ]
    );
    assert!(diff.new_arrays.is_empty());
    assert!(diff.deleted_arrays.is_empty());
    assert!(diff.updated_chunks.is_empty());
    Ok(())
}

// ---------------------------------------------------------------------------
// n10 — a rearrange session and an ordinary writable session are disjoint.
//
// Provenance: the error paths in `icechunk/src/change_set.rs`
// (`edits_mut`, `move_tracker_mut`) as exercised from
// `icechunk/src/session.rs::test_session_amending_with_move`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n10_rearrange_session_isolation__edits_are_refused() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    let (apath, _) = seed_movable(&repo).await?;

    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/foo/old"), p("/foo/new")).await?;

    let err = rearrange.add_group(p("/nope"), Bytes::new()).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let moved = p("/foo/new/array");
    let err = rearrange
        .set_chunk_ref(
            moved.clone(),
            ChunkIndices(vec![1, 1]),
            Some(ChunkPayload::Inline("x".into())),
        )
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    // every other edit §8.1 names is refused the same way
    let shape = ArrayShape::new(vec![(4u64, 2u32), (4u64, 2u32)]).ok_or("shape")?;
    let err = rearrange
        .add_array(p("/also-nope"), shape.clone(), None, Bytes::new())
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let err = rearrange.update_group(&p("/foo"), Bytes::from("g")).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let err = rearrange
        .update_array(&moved, shape, None, Bytes::from("a"))
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let err = rearrange
        .delete_chunks(&moved, [ChunkIndices(vec![0, 0])])
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let err = rearrange.delete_group(p("/foo")).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let group = rearrange.get_node(&p("/foo")).await?;
    let err = rearrange.delete_node(group).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let other = repo.writable_session("main").await?;
    let err = rearrange.merge(other).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    let err = rearrange.delete_array(moved).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    // and the move it was created for still commits
    rearrange.commit("moved").max_concurrent_nodes(4).execute().await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert!(session.get_node(&apath).await.is_err());
    Ok(())
}

#[tokio::test]
async fn n10_rearrange_session_isolation__moves_are_refused_in_a_writable_session() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_movable!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_movable(&repo).await?;

    let mut writable = repo.writable_session("main").await?;
    // an ordinary edit is fine
    writable.add_group(p("/other"), Bytes::new()).await?;
    // a move is not
    let err = writable.move_node(p("/foo/old"), p("/foo/new")).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NonRearrangeSession));
    // the session is still usable for edits afterwards
    writable.commit("edits only").max_concurrent_nodes(4).execute().await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert!(session.get_group(&p("/other")).await.is_ok());
    assert!(session.get_group(&p("/foo/old")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn n10_rearrange_session_isolation__reindex_is_an_edit_not_a_rearrangement() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_session!();
    ic_session_new!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    // `shift_array` is an edit, so a rearrange session refuses it
    let mut rearrange = repo.rearrange_session("main").await?;
    let err = rearrange.shift_array(&path, &[1]).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    // and so is `reindex_array`, of which `shift_array` is a special case
    let err = rearrange
        .reindex_array(&path, ReindexMapping::ForwardOnly(Box::new(|_| Ok(None))))
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RearrangeSessionOnly));

    // and an ordinary writable session accepts it
    let mut writable = repo.writable_session("main").await?;
    writable.shift_array(&path, &[1]).await?;
    assert_eq!(
        writable.get_chunk_ref(&path, &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Inline("0".into()))
    );
    Ok(())
}

#[tokio::test]
async fn n10_rearrange_session_isolation__session_mode_reports_the_kind() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    ic_session_new!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    let readonly = repo.readonly_session(&tip("main")).await?;
    assert_eq!(readonly.mode(), SessionMode::Readonly);
    assert!(readonly.read_only());
    assert_eq!(readonly.branch(), None);

    let writable = repo.writable_session("main").await?;
    assert_eq!(writable.mode(), SessionMode::Writable);
    assert!(!writable.read_only());
    assert_eq!(writable.branch(), Some("main"));

    let rearrange = repo.rearrange_session("main").await?;
    assert_eq!(rearrange.mode(), SessionMode::Rearrange);
    assert!(!rearrange.read_only());
    assert_eq!(rearrange.branch(), Some("main"));
    Ok(())
}

// ---------------------------------------------------------------------------
// n11 — `shift_array` and `reindex_array` relabel chunks without rewriting
// them, and their offsets are chunk indices.
//
// Provenance: `icechunk/src/session.rs::test_array_shift` (line 5645) and
// `Session::shift_array`'s own `ReindexMapping` construction (line 913).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n11_shift_reindex__shift_array_runs_in_a_writable_session() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 10).await?;

    let mut session = repo.writable_session("main").await?;
    session.shift_array(&path, &[-1]).await?;
    for i in 0..=8u32 {
        assert_eq!(
            session.get_chunk_ref(&path, &ChunkIndices(vec![i])).await?,
            Some(ChunkPayload::Inline((i + 1).to_string().into()))
        );
    }
    // the chunk shifted off the end is discarded and its position cleared
    assert_eq!(session.get_chunk_ref(&path, &ChunkIndices(vec![9])).await?, None);
    session.commit("shifted").max_concurrent_nodes(2).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &path, 0).await?, Some(Bytes::from_static(b"1")));
    assert_eq!(chunk_bytes(&session, &path, 9).await?, None);
    Ok(())
}

#[tokio::test]
async fn n11_shift_reindex__offsets_are_in_chunks_not_elements() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    // 8 elements across 4 chunks: each chunk covers two elements
    seed_array(&repo, &path, 4).await?;
    let session = repo.readonly_session(&tip("main")).await?;
    let node = session.get_array(&path).await?;
    let NodeData::Array { shape, .. } = node.node_data else {
        return Err("expected an array node".into());
    };
    assert_eq!(shape.num_chunks().collect::<Vec<_>>(), vec![4]);
    assert_eq!(shape.get(0).ok_or("dim 0")?.array_length(), 8);

    // an offset of 1 moves each chunk one whole chunk along, not one element
    let mut session = repo.writable_session("main").await?;
    session.shift_array(&path, &[1]).await?;
    assert_eq!(session.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?, None);
    for i in 1..4u32 {
        assert_eq!(
            session.get_chunk_ref(&path, &ChunkIndices(vec![i])).await?,
            Some(ChunkPayload::Inline((i - 1).to_string().into()))
        );
    }
    Ok(())
}

#[tokio::test]
async fn n11_shift_reindex__reindex_forward_only_leaves_stale_positions() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_session_new!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    let forward = Box::new(|c: &ChunkIndices| -> ReindexOperationResult {
        Ok(if c.0[0] >= 3 { None } else { Some(ChunkIndices(vec![c.0[0] + 1])) })
    });
    let mut session = repo.writable_session("main").await?;
    session.reindex_array(&path, ReindexMapping::ForwardOnly(forward)).await?;

    // forward-only never clears a vacated source, so position 0 keeps its chunk
    assert_eq!(
        session.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline("0".into()))
    );
    for i in 1..4u32 {
        assert_eq!(
            session.get_chunk_ref(&path, &ChunkIndices(vec![i])).await?,
            Some(ChunkPayload::Inline((i - 1).to_string().into()))
        );
    }
    Ok(())
}

#[tokio::test]
async fn n11_shift_reindex__reindex_backward_clears_vacated_positions() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_session_new!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    let forward = Box::new(|c: &ChunkIndices| -> ReindexOperationResult {
        Ok(if c.0[0] >= 3 { None } else { Some(ChunkIndices(vec![c.0[0] + 1])) })
    });
    let backward = Box::new(|c: &ChunkIndices| -> ReindexOperationResult {
        Ok(c.0[0].checked_sub(1).map(|v| ChunkIndices(vec![v])))
    });
    let mut session = repo.writable_session("main").await?;
    session
        .reindex_array(&path, ReindexMapping::ForwardBackward { forward, backward })
        .await?;

    // supplying the inverse lets Icechunk clear the position nothing maps to
    assert_eq!(session.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?, None);
    for i in 1..4u32 {
        assert_eq!(
            session.get_chunk_ref(&path, &ChunkIndices(vec![i])).await?,
            Some(ChunkPayload::Inline((i - 1).to_string().into()))
        );
    }
    Ok(())
}

#[tokio::test]
async fn n11_shift_reindex__an_out_of_range_destination_is_rejected() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    ic_session_new!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    // the forward function maps every chunk past the end of the four-chunk grid
    let forward = Box::new(|c: &ChunkIndices| -> ReindexOperationResult {
        Ok(Some(ChunkIndices(vec![c.0[0] + 4])))
    });
    let mut session = repo.writable_session("main").await?;
    let err = session
        .reindex_array(&path, ReindexMapping::ForwardOnly(forward))
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::InvalidIndex { .. }));

    // the committed array is untouched
    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(
            chunk_bytes(&session, &path, i).await?,
            Some(Bytes::from(i.to_string()))
        );
    }

    // a shift that would run off the end is not an error: those chunks are
    // discarded, which is what makes the rejection above specific to the mapping
    let mut session = repo.writable_session("main").await?;
    session.shift_array(&path, &[2]).await?;
    assert_eq!(session.get_chunk_ref(&path, &ChunkIndices(vec![3])).await?,
               Some(ChunkPayload::Inline("1".into())));
    Ok(())
}

// ---------------------------------------------------------------------------
// n12 — rectilinear chunk grids; the snapshot stores `num_chunks` per
// dimension rather than a single chunk length.
//
// Provenance: `icechunk/src/store.rs::{test_rectilinear_chunk_grid_metadata,
// test_get_chunk_shapes_regular, test_get_chunk_shapes_rectilinear}`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n12_rectilinear_grid__inline_and_run_length_chunk_shapes() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::from_static(GROUP_META)).await?;

    // dim 0 lists its chunk sizes; dim 1 is run-length encoded as "4, three times"
    let meta = array_meta(
        "[10,12]",
        r#"{"name":"rectilinear","configuration":{"kind":"inline","chunk_shapes":[[3,3,4],[[4,3]]]}}"#,
    );
    store.set("rect/zarr.json", meta.clone()).await?;
    assert_eq!(store.get("rect/zarr.json", &ByteRange::ALL).await?, meta);

    let node = session.read().await.get_array(&p("/rect")).await?;
    let NodeData::Array { shape, .. } = node.node_data else {
        return Err("expected an array node".into());
    };
    assert_eq!(shape.num_chunks().collect::<Vec<_>>(), vec![3, 3]);
    assert_eq!(shape.get(0).ok_or("dim 0")?.array_length(), 10);
    assert_eq!(shape.get(1).ok_or("dim 1")?.array_length(), 12);
    Ok(())
}

#[tokio::test]
async fn n12_rectilinear_grid__num_chunks_is_stored_per_dimension() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_tip!();
    // `DimensionShape::new` takes (array_length, num_chunks) at this release
    let dim = DimensionShape::new(20, 10);
    assert_eq!(dim.array_length(), 20);
    assert_eq!(dim.num_chunks(), 10);
    let shape = ArrayShape::new(vec![(20u64, 10u32)]).ok_or("shape")?;
    assert_eq!(shape.len(), 1);
    assert_eq!(shape.num_chunks().collect::<Vec<_>>(), vec![10]);
    assert!(shape.valid_chunk_coord(&ChunkIndices(vec![9])));
    assert!(!shape.valid_chunk_coord(&ChunkIndices(vec![10])));

    // the same count survives a commit and a re-read
    let repo = mem_repo().await;
    let path = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(path.clone(), shape, None, Bytes::new()).await?;
    session.commit("array").max_concurrent_nodes(2).execute().await?;

    let session = repo.readonly_session(&tip("main")).await?;
    let node = session.get_array(&path).await?;
    let NodeData::Array { shape, .. } = node.node_data else {
        return Err("expected an array node".into());
    };
    assert_eq!(shape.num_chunks().collect::<Vec<_>>(), vec![10]);
    assert_eq!(shape.get(0).ok_or("dim 0")?.array_length(), 20);
    Ok(())
}

#[tokio::test]
async fn n12_rectilinear_grid__get_chunk_shapes_reports_each_chunk_size() -> TestResult {
    ic_format!();
    ic_store!();
    ic_array_metadata!();
    let rect = array_meta(
        "[6,6,6,6,6,6]",
        r#"{"name":"rectilinear","configuration":{"kind":"inline","chunk_shapes":[[2,2,2],[[2,3]],[[1,6]],[1,[2,1],3],[[1,3],3],[6]]}}"#,
    );
    let meta: ArrayMetadata = serde_json::from_slice(&rect)?;
    let coords = [
        ChunkIndices(vec![0, 0, 0, 0, 0, 0]),
        ChunkIndices(vec![0, 1, 2, 2, 3, 0]),
    ];
    let sizes = meta.get_chunk_shapes(coords.iter())?.collect::<StoreResult<Vec<_>>>()?;
    assert_eq!(sizes, vec![vec![2, 2, 1, 1, 1, 6], vec![2, 2, 1, 3, 3, 6]]);

    // out-of-range chunk coordinates are rejected one by one
    let bad = [ChunkIndices(vec![3, 0, 0, 0, 0, 0]), ChunkIndices(vec![0, 0, 6, 0, 0, 0])];
    let results = meta.get_chunk_shapes(bad.iter())?.collect::<Vec<_>>();
    assert!(results.iter().all(|r| r.is_err()));

    // a regular grid reports the same way
    let regular = array_meta(
        "[6,6]",
        r#"{"name":"regular","configuration":{"chunk_shape":[4,3]}}"#,
    );
    let meta: ArrayMetadata = serde_json::from_slice(&regular)?;
    let coords = [ChunkIndices(vec![0, 0]), ChunkIndices(vec![1, 1])];
    let sizes = meta.get_chunk_shapes(coords.iter())?.collect::<StoreResult<Vec<_>>>()?;
    assert_eq!(sizes, vec![vec![4, 3], vec![2, 3]]);
    Ok(())
}

#[tokio::test]
async fn n12_rectilinear_grid__an_unknown_grid_name_is_rejected() -> TestResult {
    ic_core!();
    ic_format!();
    ic_store!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    store.set("zarr.json", Bytes::from_static(GROUP_META)).await?;

    let meta = array_meta(
        "[6]",
        r#"{"name":"hexagonal","configuration":{"chunk_shape":[2]}}"#,
    );
    let err = store.set("bad/zarr.json", meta).await.unwrap_err();
    assert!(
        matches!(err.kind, StoreErrorKind::BadChunkGridMetadata(_)),
        "{err:?}"
    );
    assert!(session.read().await.get_array(&p("/bad")).await.is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// n13 — repository status.
//
// Provenance: `Repository::{get_status,set_status}` and the enforcement point
// `Repository::fail_unless_online_status`; the scenario mirrors
// `icechunk-python/tests/test_session.py::test_repo_status_readonly_blocks_writable_session`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n13_repo_status__has_three_fields_and_starts_online() -> TestResult {
    ic_core!();
    ic_repo_info!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;
    let status = repo.get_status().await?;
    assert_eq!(status.availability, RepoAvailability::Online);
    assert!(status.limited_availability_reason.is_none());
    assert!(status.set_at <= Utc::now());

    let marked = RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("maintenance".to_string()),
    };
    repo.set_status(&marked).await?;
    let status = repo.get_status().await?;
    assert_eq!(status.availability, RepoAvailability::ReadOnly);
    assert_eq!(status.limited_availability_reason.as_deref(), Some("maintenance"));
    assert!(status.error_msg().contains("maintenance"));
    Ok(())
}

#[tokio::test]
async fn n13_repo_status__read_only_blocks_writers_but_not_readers() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("maintenance".to_string()),
    })
    .await?;

    let err = repo.writable_session("main").await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::ReadonlyRepository(_)));
    assert!(repo.rearrange_session("main").await.is_err());

    // reading is unaffected
    let session = repo.readonly_session(&tip("main")).await?;
    assert!(session.get_group(&Path::root()).await.is_ok());
    assert_eq!(messages(&repo, &tip("main")).await?.len(), 2);
    Ok(())
}

#[tokio::test]
async fn n13_repo_status__is_logged_and_restorable() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    fx_seed_root!();
    let repo = mem_repo().await;
    seed_root(&repo, "first").await?;

    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("freeze".to_string()),
    })
    .await?;
    repo.set_status(&RepoStatus {
        availability: RepoAvailability::Online,
        set_at: Utc::now(),
        limited_availability_reason: None,
    })
    .await?;

    // writes work again
    assert!(repo.writable_session("main").await.is_ok());
    assert_eq!(repo.get_status().await?.availability, RepoAvailability::Online);

    let kinds = op_kinds(&repo).await?;
    let status_changes: Vec<&UpdateType> = kinds
        .iter()
        .filter(|k| matches!(k, UpdateType::RepoStatusChangedUpdate { .. }))
        .collect();
    assert_eq!(status_changes.len(), 2);
    assert!(matches!(
        status_changes[0],
        UpdateType::RepoStatusChangedUpdate { status }
            if status.availability == RepoAvailability::Online
    ));
    assert!(matches!(
        status_changes[1],
        UpdateType::RepoStatusChangedUpdate { status }
            if status.availability == RepoAvailability::ReadOnly
    ));
    Ok(())
}

#[tokio::test]
async fn n13_repo_status__gc_and_expiration_refuse_when_not_online() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_repo_info!();
    ic_snapshot!();
    ic_gc!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 4).await?;

    let now = Utc::now();
    let dry_run = GCConfig::clean_all(
        now,
        now,
        None,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        true,
    );
    // both run while the repository is Online
    garbage_collect(Arc::clone(repo.asset_manager()), &dry_run, None, 100).await?;
    expire(
        Arc::clone(repo.asset_manager()),
        now,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;

    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: Some("maintenance".to_string()),
    })
    .await?;

    let err = garbage_collect(Arc::clone(repo.asset_manager()), &dry_run, None, 100)
        .await
        .unwrap_err();
    assert!(matches!(
        err,
        GCError::Repository(ref e)
            if matches!(e.kind, RepositoryErrorKind::ReadonlyRepository(_))
    ));
    let err = expire(
        Arc::clone(repo.asset_manager()),
        Utc::now(),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await
    .unwrap_err();
    assert!(matches!(
        err,
        GCError::Repository(ref e)
            if matches!(e.kind, RepositoryErrorKind::ReadonlyRepository(_))
    ));

    // restoring the status restores both
    repo.set_status(&RepoStatus {
        availability: RepoAvailability::Online,
        set_at: Utc::now(),
        limited_availability_reason: None,
    })
    .await?;
    garbage_collect(Arc::clone(repo.asset_manager()), &dry_run, None, 100).await?;
    Ok(())
}

// ---------------------------------------------------------------------------
// n14 — feature flags.
//
// Provenance: `icechunk/src/feature_flags.rs::{all_flags_on_new_repo,
// set_and_unset_flags_on_repo, try_tag_ops_without_feature_flag,
// try_rearrange_session_without_feature_flag}`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n14_feature_flags__three_flags_all_enabled_by_default() -> TestResult {
    ic_core!();
    ic_flags!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;
    let all: Vec<_> = repo.feature_flags().await?.collect();
    assert_eq!(all.len(), 3);

    let mut names: Vec<&str> = all.iter().map(|f| f.name()).collect();
    names.sort();
    assert_eq!(names, vec!["create_tag", "delete_tag", "move_node"]);

    for flag in &all {
        assert!(flag.default_enabled());
        assert!(!flag.default_disabled());
        assert!(flag.in_default_state());
        assert_eq!(flag.setting(), None);
        assert!(flag.enabled());
    }
    let by_name = |n: &str| all.iter().find(|f| f.name() == n).expect("flag").id();
    assert_eq!(by_name("move_node"), MOVE_NODE_FLAG);
    assert_eq!(by_name("create_tag"), CREATE_TAG_FLAG);
    assert_eq!(by_name("delete_tag"), DELETE_TAG_FLAG);

    assert_eq!(repo.enabled_feature_flags().await?.count(), 3);
    assert_eq!(repo.disabled_feature_flags().await?.count(), 0);
    Ok(())
}

#[tokio::test]
async fn n14_feature_flags__setting_is_tri_state() -> TestResult {
    ic_core!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;

    repo.set_feature_flag("create_tag", Some(false)).await?;
    let all: Vec<_> = repo.feature_flags().await?.collect();
    let ct = all.iter().find(|f| f.name() == "create_tag").expect("flag");
    assert_eq!(ct.setting(), Some(false));
    assert!(!ct.enabled());
    assert!(!ct.in_default_state());
    assert!(ct.default_enabled());
    assert_eq!(repo.disabled_feature_flags().await?.next().expect("flag").name(), "create_tag");
    assert_eq!(repo.enabled_feature_flags().await?.count(), 2);

    repo.set_feature_flag("create_tag", Some(true)).await?;
    let all: Vec<_> = repo.feature_flags().await?.collect();
    let ct = all.iter().find(|f| f.name() == "create_tag").expect("flag");
    assert_eq!(ct.setting(), Some(true));
    assert!(ct.enabled());
    assert!(!ct.in_default_state());

    repo.set_feature_flag("create_tag", None).await?;
    let all: Vec<_> = repo.feature_flags().await?.collect();
    let ct = all.iter().find(|f| f.name() == "create_tag").expect("flag");
    assert_eq!(ct.setting(), None);
    assert!(ct.in_default_state());
    assert_eq!(repo.disabled_feature_flags().await?.count(), 0);

    // an unknown flag name is rejected
    assert!(repo.set_feature_flag("no_such_flag", Some(false)).await.is_err());
    Ok(())
}

#[tokio::test]
async fn n14_feature_flags__a_disabled_flag_blocks_its_operation() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    let repo = mem_repo().await;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_group(p("/foo"), Bytes::new()).await?;
    let snap = session.commit("first").max_concurrent_nodes(4).execute().await?;
    repo.create_tag("exists", &snap).await?;

    // TEST_INTERFACE §11 names five (gate, feature_flag, feature_description)
    // triples and grades all three columns. Each of the five is exercised here.

    // 1. creating a rearrange session is gated by `move_node`
    assert!(repo.rearrange_session("main").await.is_ok());
    repo.set_feature_flag("move_node", Some(false)).await?;
    let err = repo.rearrange_session("main").await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::FeatureFlagDisabled {
            ref feature_flag,
            ref feature_description,
        }) if feature_flag == "move_node"
            && feature_description == "create rearrange session"
    ));
    repo.set_feature_flag("move_node", None).await?;
    assert!(repo.rearrange_session("main").await.is_ok());

    // 2. committing one is a separate gate, read when the commit lands rather
    //    than when the session was handed out
    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/foo"), p("/bar")).await?;
    repo.set_feature_flag("move_node", Some(false)).await?;
    let err = rearrange
        .commit("moved")
        .max_concurrent_nodes(4)
        .execute()
        .await
        .unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::RepositoryError(RepositoryErrorKind::FormatError(
            IcechunkFormatErrorKind::FeatureFlagDisabled {
                ref feature_flag,
                ref feature_description,
            }
        )) if feature_flag == "move_node"
            && feature_description == "commit rearrange session"
    ));

    // 3. flushing one is the third gate, and an anonymous commit is the flush
    repo.set_feature_flag("move_node", None).await?;
    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node(p("/foo"), p("/bar")).await?;
    repo.set_feature_flag("move_node", Some(false)).await?;
    let err = rearrange
        .commit("moved")
        .max_concurrent_nodes(4)
        .anonymous()
        .execute()
        .await
        .unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::RepositoryError(RepositoryErrorKind::FormatError(
            IcechunkFormatErrorKind::FeatureFlagDisabled {
                ref feature_flag,
                ref feature_description,
            }
        )) if feature_flag == "move_node"
            && feature_description == "flush rearrange session"
    ));
    repo.set_feature_flag("move_node", None).await?;

    // 4 and 5. tag creation and deletion have flags of their own
    repo.set_feature_flag("create_tag", Some(false)).await?;
    let err = repo.create_tag("blocked", &snap).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::FeatureFlagDisabled {
            ref feature_flag,
            ref feature_description,
        }) if feature_flag == "create_tag" && feature_description == "tag creation"
    ));
    repo.set_feature_flag("delete_tag", Some(false)).await?;
    let err = repo.delete_tag("exists").await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::FeatureFlagDisabled {
            ref feature_flag,
            ref feature_description,
        }) if feature_flag == "delete_tag" && feature_description == "tag delete"
    ));
    // ... and everything else still works
    assert!(repo.create_branch("dev", &snap).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn n14_feature_flags__changes_persist_and_are_logged() -> TestResult {
    ic_core!();
    ic_flags!();
    ic_repo_info!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    let storage = mem_storage().await;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    repo.set_feature_flag("create_tag", Some(false)).await?;
    repo.set_feature_flag("delete_tag", Some(true)).await?;
    repo.set_feature_flag("create_tag", None).await?;

    // a client that opens the repository afterwards sees the same settings
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let all: Vec<_> = reopened.feature_flags().await?.collect();
    let dt = all.iter().find(|f| f.name() == "delete_tag").expect("flag");
    assert_eq!(dt.setting(), Some(true));
    let ct = all.iter().find(|f| f.name() == "create_tag").expect("flag");
    assert_eq!(ct.setting(), None);

    let kinds = op_kinds(&reopened).await?;
    assert!(matches!(
        kinds[0],
        UpdateType::FeatureFlagChanged { id: CREATE_TAG_FLAG, new_value: None }
    ));
    assert!(matches!(
        kinds[1],
        UpdateType::FeatureFlagChanged { id: DELETE_TAG_FLAG, new_value: Some(true) }
    ));
    assert!(matches!(
        kinds[2],
        UpdateType::FeatureFlagChanged { id: CREATE_TAG_FLAG, new_value: Some(false) }
    ));
    Ok(())
}

#[tokio::test]
async fn n14_feature_flags__ids_and_names_resolve_through_the_free_functions()
-> TestResult {
    ic_core!();
    ic_flags!();
    ic_format!();
    ic_repo_info!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_toc_of!();
    use icechunk::feature_flags::{find_feature_flag_id, raise_if_feature_flag_disabled};

    let repo = mem_repo().await;
    let (toc, _) = toc_of(&repo).await?;

    // every registered name resolves to its id ...
    assert_eq!(find_feature_flag_id("move_node")?, MOVE_NODE_FLAG);
    assert_eq!(find_feature_flag_id("create_tag")?, CREATE_TAG_FLAG);
    assert_eq!(find_feature_flag_id("delete_tag")?, DELETE_TAG_FLAG);
    // ... and a name outside the registry does not
    let err = find_feature_flag_id("no_such_flag").unwrap_err();
    assert!(matches!(err.kind, IcechunkFormatErrorKind::InvalidFeatureFlagName { .. }));

    // a default-enabled flag passes the gate; nothing is explicitly set yet
    assert_eq!(toc.feature_flag_enabled(MOVE_NODE_FLAG)?, None);
    raise_if_feature_flag_disabled(&toc, MOVE_NODE_FLAG, "move a node")?;
    // an id outside the registry is rejected rather than treated as a default
    let err = raise_if_feature_flag_disabled(&toc, 9999, "nothing at all").unwrap_err();
    assert!(matches!(err.kind, IcechunkFormatErrorKind::InvalidFeatureFlagId { .. }));

    // and once a flag is disabled the same gate reports it
    repo.set_feature_flag("delete_tag", Some(false)).await?;
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.feature_flag_enabled(DELETE_TAG_FLAG)?, Some(false));
    let err =
        raise_if_feature_flag_disabled(&toc, DELETE_TAG_FLAG, "tag delete").unwrap_err();
    assert!(matches!(
        err.kind,
        IcechunkFormatErrorKind::FeatureFlagDisabled { ref feature_flag, .. }
            if feature_flag == "delete_tag"
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// n15 — repository-level metadata.
//
// Provenance: `Repository::{get_metadata,set_metadata,update_metadata}`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n15_repo_metadata__set_replaces_the_whole_map() -> TestResult {
    ic_core!();
    ic_snapshot!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;
    assert!(repo.get_metadata().await?.is_empty());

    repo.set_metadata(&SnapshotProperties::from([
        ("a".to_string(), serde_json::json!(1)),
        ("b".to_string(), serde_json::json!("two")),
    ]))
    .await?;
    let meta = repo.get_metadata().await?;
    assert_eq!(meta.len(), 2);
    assert_eq!(meta["a"], serde_json::json!(1));
    assert_eq!(meta["b"], serde_json::json!("two"));

    repo.set_metadata(&SnapshotProperties::from([(
        "c".to_string(),
        serde_json::json!([1, 2, 3]),
    )]))
    .await?;
    let meta = repo.get_metadata().await?;
    assert_eq!(meta.len(), 1);
    assert_eq!(meta["c"], serde_json::json!([1, 2, 3]));
    Ok(())
}

#[tokio::test]
async fn n15_repo_metadata__update_merges_into_it() -> TestResult {
    ic_core!();
    ic_snapshot!();
    fx_mem_repo!();
    fx_mem_storage!();
    let repo = mem_repo().await;
    repo.set_metadata(&SnapshotProperties::from([
        ("a".to_string(), serde_json::json!(1)),
        ("b".to_string(), serde_json::json!(2)),
    ]))
    .await?;

    let merged = repo
        .update_metadata(&SnapshotProperties::from([
            ("b".to_string(), serde_json::json!(20)),
            ("c".to_string(), serde_json::json!(3)),
        ]))
        .await?;
    assert_eq!(merged, repo.get_metadata().await?);
    assert_eq!(merged.len(), 3);
    assert_eq!(merged["a"], serde_json::json!(1));
    assert_eq!(merged["b"], serde_json::json!(20));
    assert_eq!(merged["c"], serde_json::json!(3));
    Ok(())
}

#[tokio::test]
async fn n15_repo_metadata__is_repository_level_and_survives_reopen() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    fx_mem_storage!();
    fx_op_kinds!();
    fx_ops!();
    let storage = mem_storage().await;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let snapshot_props =
        SnapshotProperties::from([("scope".to_string(), serde_json::json!("snapshot"))]);

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session
        .commit("first")
        .properties(snapshot_props.clone())
        .execute()
        .await?;
    repo.set_metadata(&SnapshotProperties::from([(
        "scope".to_string(),
        serde_json::json!("repository"),
    )]))
    .await?;

    // the two are independent
    assert_eq!(repo.lookup_snapshot(&snap).await?.metadata, snapshot_props);
    assert_eq!(repo.get_metadata().await?["scope"], serde_json::json!("repository"));

    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(reopened.get_metadata().await?["scope"], serde_json::json!("repository"));
    let kinds = op_kinds(&reopened).await?;
    assert!(matches!(kinds[0], UpdateType::MetadataChangedUpdate));
    Ok(())
}

// ---------------------------------------------------------------------------
// n16 — `vcc://` relative virtual chunk references.
//
// Provenance: `icechunk/tests/test_virtual_refs.rs::test_virtual_refs_with_vcc_relative_urls`
// (line 1119), the local-filesystem variant only: the chunk files live in a
// `TempDir` and are addressed with `file://` URLs, so nothing leaves the host.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n16_vcc_relative_refs__a_location_can_be_written_relative_to_a_name() -> TestResult {
    ic_manifest!();
    let loc = VirtualChunkLocation::from_vcc_path("local-data", "chunk-1")?;
    assert!(loc.is_relative());
    assert_eq!(loc.url(), "vcc://local-data/chunk-1");
    assert_eq!(loc.parse_vcc(), Some(("local-data", "chunk-1")));

    // an absolute URL is not relative and has no vcc parts
    let absolute = VirtualChunkLocation::from_url("s3://bucket/path/to/chunk")?;
    assert!(!absolute.is_relative());
    assert_eq!(absolute.parse_vcc(), None);

    // `from_url` also accepts the relative form
    let parsed = VirtualChunkLocation::from_url("vcc://local-data/a/b")?;
    assert!(parsed.is_relative());
    assert_eq!(parsed.parse_vcc(), Some(("local-data", "a/b")));
    Ok(())
}

#[tokio::test]
async fn n16_vcc_relative_refs__resolve_through_the_container_prefix() -> TestResult {
    ic_core!();
    ic_config!();
    ic_format!();
    ic_manifest!();
    ic_repository!();
    ic_vchunks!();
    fx_tip!();
    fx_vcc_array!();
    fx_vcc_repo!();
    let chunk_dir = tempfile::tempdir()?;
    std::fs::write(chunk_dir.path().join("chunk-1"), b"first")?;
    let repo_dir = tempfile::tempdir()?;
    let repo = vcc_repo(repo_dir.path(), chunk_dir.path()).await?;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    vcc_array(&store).await?;
    store
        .set_virtual_ref(
            "array/c/0",
            VirtualChunkRef {
                location: VirtualChunkLocation::from_vcc_path("local-data", "chunk-1")?,
                offset: 0,
                length: 5,
                checksum: None,
            },
            false,
        )
        .await?;

    assert_eq!(store.get("array/c/0", &ByteRange::ALL).await?, Bytes::from_static(b"first"));
    assert_eq!(
        store.get("array/c/0", &ByteRange::bounded(1, 3)).await?,
        Bytes::from_static(b"ir")
    );

    // the committed manifest reports the expanded, absolute location
    session.write().await.commit("virtual").max_concurrent_nodes(2).execute().await?;
    let reader = repo.readonly_session(&tip("main")).await?;
    let locations: HashSet<String> =
        reader.all_virtual_chunk_locations().await?.try_collect().await?;
    assert_eq!(
        locations,
        HashSet::from([format!("file://{}/chunk-1", chunk_dir.path().to_str().ok_or("utf-8")?)])
    );
    Ok(())
}

#[tokio::test]
async fn n16_vcc_relative_refs__relocating_the_prefix_moves_every_reference() -> TestResult {
    ic_core!();
    ic_config!();
    ic_format!();
    ic_manifest!();
    ic_repository!();
    ic_session!();
    ic_vchunks!();
    fx_chunk_bytes!();
    fx_p!();
    fx_tip!();
    fx_vcc_array!();
    fx_vcc_repo!();
    let first_dir = tempfile::tempdir()?;
    std::fs::write(first_dir.path().join("chunk-1"), b"first")?;
    let repo_dir = tempfile::tempdir()?;
    let repo = vcc_repo(repo_dir.path(), first_dir.path()).await?;

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    vcc_array(&store).await?;
    store
        .set_virtual_ref(
            "array/c/0",
            VirtualChunkRef {
                location: VirtualChunkLocation::from_vcc_path("local-data", "chunk-1")?,
                offset: 0,
                length: 5,
                checksum: None,
            },
            false,
        )
        .await?;
    session.write().await.commit("virtual").max_concurrent_nodes(2).execute().await?;

    // point the same container name at a different directory: no manifest is
    // rewritten, but every `vcc://` reference now resolves somewhere else
    let second_dir = tempfile::tempdir()?;
    std::fs::write(second_dir.path().join("chunk-1"), b"secnd")?;
    let new_prefix = format!("file://{}/", second_dir.path().to_str().ok_or("utf-8")?);
    let mut config = repo.config().clone();
    config.set_virtual_chunk_container(VirtualChunkContainer::new_named(
        "local-data".to_string(),
        new_prefix,
        ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    )?)?;
    let creds: HashMap<String, Option<Credentials>> =
        [(format!("file://{}", second_dir.path().to_str().ok_or("utf-8")?), None)].into();
    let relocated = repo.reopen(Some(config), Some(creds)).await?;

    let reader = relocated.readonly_session(&tip("main")).await?;
    assert_eq!(
        chunk_bytes(&reader, &p("/array"), 0).await?,
        Some(Bytes::from_static(b"secnd"))
    );
    Ok(())
}

#[tokio::test]
async fn n16_vcc_relative_refs__container_names_and_prefixes_are_validated() -> TestResult {
    ic_core!();
    ic_config!();
    ic_format!();
    ic_manifest!();
    ic_vchunks!();
    fx_vcc_array!();
    fx_vcc_repo!();
    // a name may be neither empty nor contain a path separator
    assert!(VirtualChunkLocation::from_vcc_path("", "chunk-1").is_err());
    assert!(VirtualChunkLocation::from_vcc_path("a/b", "chunk-1").is_err());
    assert!(
        VirtualChunkContainer::new_named(
            String::new(),
            "file:///data/".to_string(),
            ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
        )
        .is_err()
    );
    assert!(
        VirtualChunkContainer::new_named(
            "a/b".to_string(),
            "file:///data/".to_string(),
            ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
        )
        .is_err()
    );
    // a url prefix must end in a separator, in both constructors
    assert!(
        VirtualChunkContainer::new_named(
            "ok".to_string(),
            "file:///data".to_string(),
            ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
        )
        .is_err()
    );
    assert!(
        VirtualChunkContainer::new(
            "file:///data".to_string(),
            ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
        )
        .is_err()
    );
    assert!(
        VirtualChunkContainer::new(
            "file:///data/".to_string(),
            ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
        )
        .is_ok()
    );

    // an unknown container name cannot be resolved at read time
    let chunk_dir = tempfile::tempdir()?;
    std::fs::write(chunk_dir.path().join("chunk-1"), b"first")?;
    let repo_dir = tempfile::tempdir()?;
    let repo = vcc_repo(repo_dir.path(), chunk_dir.path()).await?;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    vcc_array(&store).await?;
    store
        .set_virtual_ref(
            "array/c/0",
            VirtualChunkRef {
                location: VirtualChunkLocation::from_vcc_path("no-such-container", "chunk-1")?,
                offset: 0,
                length: 5,
                checksum: None,
            },
            false,
        )
        .await?;
    assert!(store.get("array/c/0", &ByteRange::ALL).await.is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// n17 — the in-place upgrade from the first on-disk generation to the second.
//
// Provenance: `icechunk/src/migrations/mod.rs::{prepare_v1_repo,
// test_1_to_2_migration, test_1_to_2_migration_dry_run,
// test_1_to_2_migration_already_v2, test_1_to_2_migration_without_delete}`.
//
// The fixture is upstream's committed spec-version-1 repository, bundled with
// this verifier under `tests/assets/fixtures/` and checksum-verified before any
// group runs. Every case copies it into its own temporary directory first, so
// the fixture itself is never modified.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n17_migration_ic1_to_ic2__dry_run_leaves_the_repository_at_v1() -> TestResult {
    ic_core!();
    ic_format!();
    ic_migrations!();
    fx_all_refs!();
    fx_staged_v1_repo!();
    let dir = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(dir.path()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V1);
    let refs_before = all_refs(&repo).await?;

    migrate_1_to_2(repo, true, true, None).await?;

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V1);
    assert_eq!(all_refs(&repo).await?, refs_before);
    // no second-generation object was written
    assert!(!dir.path().join("test-repo-v1").join("repo").exists());
    assert!(dir.path().join("test-repo-v1").join("config.yaml").exists());
    Ok(())
}

#[tokio::test]
async fn n17_migration_ic1_to_ic2__preserves_every_ancestry() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_migrations!();
    ic_repository!();
    fx_all_refs!();
    fx_history!();
    fx_staged_v1_repo!();
    fx_toc_of!();
    let dir = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(dir.path()).await?;

    let refs_before = all_refs(&repo).await?;
    assert!(refs_before.len() >= 3, "fixture should carry branches and tags");
    let mut ancestries_before = Vec::new();
    for (kind, name, _) in &refs_before {
        let version = if kind == "branch" {
            VersionInfo::BranchTipRef(name.clone())
        } else {
            VersionInfo::TagRef(name.clone())
        };
        ancestries_before.push((name.clone(), history(&repo, &version).await?));
    }

    migrate_1_to_2(repo, false, true, None).await?;

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert_eq!(all_refs(&repo).await?, refs_before);
    for (name, before) in ancestries_before {
        let version = if repo.list_branches().await?.contains(&name) {
            VersionInfo::BranchTipRef(name.clone())
        } else {
            VersionInfo::TagRef(name.clone())
        };
        assert_eq!(history(&repo, &version).await?, before, "ancestry of {name}");
    }

    // the deleted tag's name is carried across, so it can never be reused
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc.deleted_tags()?.collect::<Vec<_>>(), vec!["deleted"]);
    Ok(())
}

#[tokio::test]
async fn n17_migration_ic1_to_ic2__reads_and_writes_no_chunks() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_migrations!();
    ic_repository!();
    ic_session!();
    fx_chunk_refs!();
    fx_p!();
    fx_staged_v1_repo!();
    fx_tip!();
    let dir = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(dir.path()).await?;
    let root = dir.path().join("test-repo-v1");

    let before = chunk_refs(&repo, &tip("main")).await?;
    assert!(!before.is_empty(), "fixture should carry chunks");
    let mut chunk_objects: Vec<String> = std::fs::read_dir(root.join("chunks"))?
        .map(|e| e.map(|e| e.file_name().to_string_lossy().into_owned()))
        .collect::<Result<Vec<_>, _>>()?;
    chunk_objects.sort();

    migrate_1_to_2(repo, false, true, None).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;

    // identical chunk references ...
    assert_eq!(chunk_refs(&repo, &tip("main")).await?, before);
    // ... over an identical set of chunk objects ...
    let mut after_objects: Vec<String> = std::fs::read_dir(root.join("chunks"))?
        .map(|e| e.map(|e| e.file_name().to_string_lossy().into_owned()))
        .collect::<Result<Vec<_>, _>>()?;
    after_objects.sort();
    assert_eq!(after_objects, chunk_objects);

    // ... and the bytes behind every materialised reference still read back
    let session = repo.readonly_session(&tip("main")).await?;
    for (path, coord, payload) in &before {
        if matches!(payload, ChunkPayload::Virtual(_)) {
            continue;
        }
        let reader = session
            .get_chunk_reader(&p(path), &ChunkIndices(coord.clone()), &ByteRange::ALL)
            .await?;
        assert!(get_chunk(reader).await?.is_some(), "chunk {path} {coord:?}");
    }
    Ok(())
}

#[tokio::test]
async fn n17_migration_ic1_to_ic2__rebuilds_the_operations_log() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_snapshot!();
    ic_migrations!();
    ic_repository!();
    fx_history!();
    fx_ops!();
    fx_staged_v1_repo!();
    fx_tip!();
    let dir = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(dir.path()).await?;
    let branches = repo.list_branches().await?;
    let snapshots = history(&repo, &tip("main")).await?.len();

    migrate_1_to_2(repo, false, true, None).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;

    let entries = ops(&repo).await?;
    let kinds: Vec<&UpdateType> = entries.iter().map(|(_, k, _)| k).collect();

    // newest entry is the migration itself
    assert!(matches!(
        kinds[0],
        UpdateType::RepoMigratedUpdate {
            from_version: SpecVersionBin::V1,
            to_version: SpecVersionBin::V2
        }
    ));
    // the repository's creation is at the other end
    assert!(kinds.iter().any(|k| matches!(k, UpdateType::RepoInitializedUpdate)));
    // one commit entry per non-root snapshot on main
    assert!(
        kinds
            .iter()
            .filter(|k| matches!(k, UpdateType::NewCommitUpdate { .. }))
            .count()
            >= snapshots - 1
    );
    // one branch-creation entry per non-default branch
    for branch in branches.iter().filter(|b| b.as_str() != "main") {
        assert!(
            kinds.iter().any(
                |k| matches!(k, UpdateType::BranchCreatedUpdate { name } if name == branch)
            ),
            "no BranchCreatedUpdate for {branch}"
        );
    }
    // the deleted tag was both created and deleted, newest first
    let created = kinds
        .iter()
        .position(|k| matches!(k, UpdateType::TagCreatedUpdate { name } if name == "deleted"));
    let deleted = kinds
        .iter()
        .position(|k| matches!(k, UpdateType::TagDeletedUpdate { name, .. } if name == "deleted"));
    assert!(deleted < created, "deletion must be newer than creation");
    // timestamps are strictly decreasing
    for window in entries.windows(2) {
        assert!(window[0].0 > window[1].0);
    }
    Ok(())
}

#[tokio::test]
async fn n17_migration_ic1_to_ic2__an_already_migrated_repository_is_refused() -> TestResult {
    ic_core!();
    ic_format!();
    ic_migrations!();
    fx_staged_v1_repo!();
    let dir = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(dir.path()).await?;

    migrate_1_to_2(repo, false, true, None).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);

    let err = migrate_1_to_2(repo, false, true, None).await.unwrap_err();
    assert!(matches!(
        err.kind,
        MigrationErrorKind::InvalidRepositoryMigration { actual: SpecVersionBin::V2, .. }
    ));

    // the repository is still usable afterwards
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    assert!(!repo.list_branches().await?.is_empty());
    assert!(repo.writable_session("main").await.is_ok());
    Ok(())
}

#[tokio::test]
async fn n17_migration_ic1_to_ic2__unused_first_generation_files_are_kept_or_removed()
-> TestResult {
    ic_core!();
    ic_format!();
    ic_repo_info!();
    ic_migrations!();
    ic_repository!();
    fx_staged_v1_repo!();
    fx_toc_branches!();
    fx_toc_of!();
    // `delete_unused_v1_files = false` leaves the first-generation refs readable,
    // and a non-default `prefetch_concurrency` changes nothing observable
    let keep = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(keep.path()).await?;
    let v1_branches = icechunk::refs::list_branches(
        repo.storage().as_ref(),
        repo.storage_settings(),
    )
    .await?;
    assert!(v1_branches.len() >= 2, "fixture should carry more than one branch");

    migrate_1_to_2(repo, false, false, Some(4)).await?;

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert_eq!(
        icechunk::refs::list_branches(repo.storage().as_ref(), repo.storage_settings())
            .await?,
        v1_branches,
        "with delete_unused_v1_files = false the refs/ prefix survives"
    );
    assert!(keep.path().join("test-repo-v1").join("config.yaml").exists());

    // `delete_unused_v1_files = true` removes both, and the migrated repository
    // still resolves every ref out of its table of contents
    let removed = tempfile::tempdir()?;
    let (repo, storage) = staged_v1_repo(removed.path()).await?;
    migrate_1_to_2(repo, false, true, None).await?;

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert!(
        icechunk::refs::list_branches(repo.storage().as_ref(), repo.storage_settings())
            .await?
            .is_empty(),
        "with delete_unused_v1_files = true the refs/ prefix is removed"
    );
    assert!(!removed.path().join("test-repo-v1").join("config.yaml").exists());
    let (toc, _) = toc_of(&repo).await?;
    assert_eq!(toc_branches(&toc), v1_branches.into_iter().collect::<Vec<_>>());
    Ok(())
}

// ---------------------------------------------------------------------------
// n18 — ancestry graph rendering.
//
// Provenance: `Repository::ancestry_graph` plus the assertions in
// `icechunk-python/tests/test_display.py::TestAncestryGraph`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n18_ancestry_graph__a_single_branch_renders_plain_and_linear() -> TestResult {
    ic_core!();
    ic_display!();
    ic_format!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "Initial work").await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    session.commit("Main work").max_concurrent_nodes(2).execute().await?;
    repo.create_tag("v1.0", &first).await?;

    let graph: AncestryGraph =
        repo.ancestry_graph(Some(&tip("main")), true).await?;
    assert_eq!(graph.num_columns, 1);
    assert!(graph.plain);
    assert_eq!(graph.nodes.len(), 3);
    assert!(graph.nodes.iter().all(|n| n.column == 0));
    assert_eq!(graph.nodes[0].branches, vec!["main".to_string()]);
    assert!(graph.nodes.iter().any(|n| n.tags == vec!["v1.0".to_string()]));

    let text = graph.to_string();
    assert!(!text.contains('\u{1b}'), "plain rendering must carry no ANSI escapes");
    assert!(text.contains("Main work"));
    assert!(text.contains("main"));
    assert!(text.contains("v1.0"));

    // a tag or a snapshot id walks a single, anonymous column instead
    for version in [VersionInfo::TagRef("v1.0".to_string()), VersionInfo::SnapshotId(first)] {
        let graph = repo.ancestry_graph(Some(&version), true).await?;
        assert_eq!(graph.num_columns, 1);
        assert!(graph.nodes.iter().all(|n| n.column == 0));
        // the tag's ancestry stops at the tagged snapshot, so "Main work" is absent
        assert!(!graph.to_string().contains("Main work"));
        assert!(graph.to_string().contains("Initial work"));
    }
    Ok(())
}

#[tokio::test]
async fn n18_ancestry_graph__all_branches_render_as_a_tree() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "Initial work").await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    session.commit("Main work").max_concurrent_nodes(2).execute().await?;
    repo.create_branch("feature", &first).await?;

    let graph = repo.ancestry_graph(None, false).await?;
    assert_eq!(graph.num_columns, 2);
    assert!(!graph.plain);
    // `main` is the trunk and comes first
    assert_eq!(graph.all_branches, vec!["main".to_string(), "feature".to_string()]);
    assert!(graph.to_string().contains('\u{1b}'), "coloured rendering carries ANSI escapes");
    assert!(!graph.layout().is_empty());
    assert_eq!(graph.column_colors().len(), graph.num_columns);

    // a single ref renders one column again
    let one = repo.ancestry_graph(Some(&tip("feature")), true).await?;
    assert_eq!(one.num_columns, 1);
    assert!(one.to_string().contains("Initial work"));
    Ok(())
}

#[tokio::test]
async fn n18_ancestry_graph__renders_svg_for_notebooks() -> TestResult {
    ic_core!();
    ic_format!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "Initial work").await?;
    repo.create_branch("feature", &first).await?;
    repo.create_tag("v1.0", &first).await?;

    let graph = repo.ancestry_graph(None, false).await?;
    let svg = graph.to_svg();
    assert!(svg.contains("<svg"));
    assert!(svg.contains("</svg>"));
    assert!(svg.contains("Initial work"));

    // an anonymous snapshot never appears in the graph
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/x"), Bytes::new()).await?;
    let detached = session.commit("detached").anonymous().execute().await?;
    let graph = repo.ancestry_graph(None, true).await?;
    assert!(!graph.nodes.iter().any(|n| n.info.id == detached));
    assert!(!graph.to_string().contains("detached"));
    Ok(())
}

// ---------------------------------------------------------------------------
// n19 — the workspace restructure, seen from outside the workspace.
//
// Provenance: `icechunk/src/lib.rs`, `icechunk/src/storage/mod.rs` and
// `icechunk/src/config.rs` (the re-export façades) and
// `icechunk-types/src/error.rs` (the error machinery).
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n19_workspace_surface__the_format_crate_is_reexported() -> TestResult {
    ic_format!();
    ic_node_type!();
    ic_snapshot!();
    // `icechunk::format` is the format crate itself, so every old path resolves
    let _: icechunk::format::SnapshotId = Snapshot::INITIAL_SNAPSHOT_ID;
    let _ = icechunk::format::Path::root();
    let _: icechunk::format::ChunkIndices = ChunkIndices(vec![0]);
    assert_eq!(icechunk::format::REPO_INFO_FILE_PATH, "repo");
    assert_eq!(icechunk::format::SNAPSHOTS_FILE_PATH, "snapshots");
    assert_eq!(icechunk::format::MANIFESTS_FILE_PATH, "manifests");
    assert_eq!(icechunk::format::TRANSACTION_LOGS_FILE_PATH, "transactions");
    assert_eq!(icechunk::format::CHUNKS_FILE_PATH, "chunks");
    assert_eq!(icechunk::format::OVERWRITTEN_FILES_PATH, "overwritten");
    assert_eq!(icechunk::format::CONFIG_FILE_PATH, "config.yaml");
    assert_eq!(icechunk::format::V1_REFS_FILE_PATH, "refs");

    // `NodeType` now lives at the format crate root, not under `snapshot`
    assert_ne!(NodeType::Group, NodeType::Array);
    assert!(icechunk::user_agent().starts_with("icechunk-rust-"));
    Ok(())
}

#[tokio::test]
async fn n19_workspace_surface__diff_moved_out_of_the_transaction_log() -> TestResult {
    ic_core!();
    ic_diff!();
    ic_format!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo().await;
    let first = seed_root(&repo, "first").await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/added"), Bytes::new()).await?;
    session.commit("second").max_concurrent_nodes(2).execute().await?;

    // `Diff` and `DiffBuilder` are `icechunk::diff::*` now
    let diff: Diff = repo.diff(&VersionInfo::SnapshotId(first), &tip("main")).await?;
    assert!(!diff.is_empty());
    assert!(diff.new_groups.contains(&p("/added")));
    assert!(diff.moved_nodes.is_empty());

    // and a session's pending changes report the same shape
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/pending"), Bytes::new()).await?;
    let status: Diff = session.status().await?;
    assert!(status.new_groups.contains(&p("/pending")));
    assert!(session.has_uncommitted_changes());
    session.discard_changes()?;
    assert!(!session.has_uncommitted_changes());
    assert!(session.status().await?.is_empty());
    Ok(())
}

#[tokio::test]
async fn n19_workspace_surface__errors_are_built_with_capture_and_inject() -> TestResult {
    ic_core!();
    ic_refs!();
    ic_repository!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    // `ICError::capture` replaces the removed `ICError::new`
    let repo_err = RepositoryError::capture(RepositoryErrorKind::RepositoryDoesntExist);
    assert!(matches!(repo_err.kind, RepositoryErrorKind::RepositoryDoesntExist));
    assert!(!repo_err.to_string().is_empty());

    // `ICError::inject` converts between kinds, preserving the span
    let session_err: SessionError = repo_err.inject();
    assert!(matches!(
        session_err.kind,
        SessionErrorKind::RepositoryError(RepositoryErrorKind::RepositoryDoesntExist)
    ));

    // the old path still names the same type
    let _: icechunk::error::ICError<SessionErrorKind> =
        SessionError::capture(SessionErrorKind::ReadOnlySession);

    // a real error from the library carries a kind a caller can match on
    let repo = mem_repo().await;
    let err = repo.lookup_branch("no-such-branch").await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(ref name))
            if name == "no-such-branch"
    ));
    Ok(())
}

#[tokio::test]
async fn n19_workspace_surface__dimension_shape_counts_chunks_not_lengths() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_session!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    // `ArrayShape::new(vec![(20, 10)])` compiles under both generations and
    // means "length 20, ten chunks" here.
    let shape = ArrayShape::new(vec![(20u64, 10u32)]).ok_or("shape")?;
    assert_eq!(shape.num_chunks().collect::<Vec<_>>(), vec![10]);
    assert_eq!(shape.get(0).ok_or("dim")?.array_length(), 20);

    let repo = mem_repo().await;
    let path = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_array(path.clone(), shape, None, Bytes::new()).await?;
    // chunk index 9 is the last valid one; 10 is out of range
    session
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![9]),
            Some(ChunkPayload::Inline("last".into())),
        )
        .await?;
    let err = session
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![10]),
            Some(ChunkPayload::Inline("past the end".into())),
        )
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::InvalidIndex { .. }));
    Ok(())
}

#[tokio::test]
async fn n19_workspace_surface__the_storage_facade_paths_resolve() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    fx_seed_root!();
    // every storage name a caller used before the split is still reachable
    // under `icechunk::storage`
    let settings = icechunk::storage::Settings::default();
    let _: Option<icechunk::storage::ConcurrencySettings> = settings.concurrency;
    let _: Option<icechunk::storage::RetriesSettings> = settings.retries;
    let _ = icechunk::storage::VersionInfo::for_creation();
    let _ = icechunk::storage::ETag("etag".to_string());
    let _ = icechunk::storage::Generation("gen".to_string());
    assert!(icechunk::storage::split_in_multiple_requests(&(0u64..10u64), 4, 8).count() > 0);

    // and the root re-exports still name the storage constructors
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    assert!(storage.can_write().await?);
    let _: &icechunk::storage::Settings = &storage.default_settings().await?;
    let dir = tempfile::tempdir()?;
    let _: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;

    // `refs::*` addresses the first-generation on-disk refs only
    let repo = Repository::create(
        None,
        Arc::clone(&storage),
        HashMap::new(),
        Some(SpecVersionBin::V1),
        true,
    )
    .await?;
    let snap = seed_root(&repo, "first").await?;
    let tip = icechunk::refs::fetch_branch_tip_v1(
        repo.storage().as_ref(),
        repo.storage_settings(),
        "main",
    )
    .await?;
    assert_eq!(tip.snapshot, snap);
    Ok(())
}

#[tokio::test]
async fn n19_workspace_surface__every_new_path_resolves() -> TestResult {
    ic_core!();
    ic_vchunks!();
    // TEST_INTERFACE §2.4 requires each of these paths to resolve for a caller
    // outside the workspace. The BEHAVIOUR behind the ones that need a network,
    // a daemon or a cloud credential is out of scope (§20); what is checked here
    // is only that the re-export façade names them.
    #[allow(unused_imports)]
    use icechunk::{
        change_set::{ArrayData, ChangeSet},
        cli,
        compat::IcechunkBoxFuture,
        diff::DiffBuilder,
        display::ancestry_graph::{AncestryNode, LayoutElement, palette_ansi, palette_hex},
        feature_flags::{find_feature_flag_id, raise_if_feature_flag_disabled},
        format::{
            attributes::AttributesTable, repo_info::UpdateInfo,
            transaction_log::TransactionLog,
        },
        inspect,
        storage::{
            DeleteObjectsResult, GetModifiedResult, VersionedUpdateResult, latency,
            new_http_storage, new_redirect_storage, redirect, s3_config,
            split_in_multiple_equal_requests, strip_quotes,
        },
    };
    use icechunk::format::manifest::VCC_RELATIVE_URL_SCHEME;

    // the handful that are cheap to observe, observed
    assert_eq!(VCC_RELATIVE_URL_SCHEME, "vcc://");
    assert_eq!(strip_quotes("\"quoted\""), "quoted");
    assert_eq!(strip_quotes("bare"), "bare");
    assert_eq!(split_in_multiple_equal_requests(&(0u64..8u64), 4, 8).count(), 2);
    let _: Option<icechunk::compat::IcechunkBoxFuture<'static, ()>> = None;
    let _: Option<DeleteObjectsResult> = None;
    let _: Option<GetModifiedResult> = None;
    let _: Option<VersionedUpdateResult> = None;
    let _: Option<TransactionLog> = None;
    let _: Option<ChangeSet> = None;
    let _: Option<ArrayData> = None;
    let _: Option<DiffBuilder> = None;
    let _: Option<AncestryNode> = None;
    let _: Option<LayoutElement> = None;
    let _: Option<s3_config::S3Options> = None;
    let _: fn(usize) -> &'static str = palette_ansi;
    let _: fn(usize) -> &'static str = palette_hex;

    // `VirtualChunkResolver::expand_location` is the name resolution of §13,
    // reachable on its own
    let container = VirtualChunkContainer::new_named(
        "data".to_string(),
        "file:///virtual/data/".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/virtual/data")),
    )?;
    let resolver = icechunk::virtual_chunks::VirtualChunkResolver::new(
        std::iter::once(container),
        HashMap::new(),
        icechunk::storage::Settings::default(),
    );
    assert_eq!(
        resolver.expand_location("vcc://data/a/b")?,
        "file:///virtual/data/a/b"
    );
    assert_eq!(
        resolver.expand_location("file:///elsewhere/x")?,
        "file:///elsewhere/x"
    );
    assert!(resolver.expand_location("vcc://no-such-container/a").is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// n20 — which on-disk generation a repository is written at, and what that
// gates.
//
// Provenance: `icechunk/src/repository.rs::{create_repo_with_spec_version_1,
// fetch_spec_version}` and `AssetManager::fail_unless_spec_at_least`.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn n20_spec_version_dispatch__create_defaults_to_the_second_generation() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    fx_p!();
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    // every chunk is written as its own object, so the layout is fully populated
    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        ..RepositoryConfig::default()
    };
    let repo =
        Repository::create(Some(config), storage, HashMap::new(), None, true).await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert_eq!(SpecVersionBin::current(), SpecVersionBin::V2);
    assert!(SpecVersionBin::V1 < SpecVersionBin::V2);

    // `get_chunk_writer` now returns a `Result`; with the inline threshold at
    // zero its payload is a chunk object of its own
    let path = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path.clone(),
            ArrayShape::new(vec![(4u64, 2u32)]).ok_or("shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    let write = session.get_chunk_writer()?;
    let payload = write(Bytes::from_static(b"materialised")).await?;
    assert!(matches!(payload, ChunkPayload::Ref(_)));
    session.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(payload)).await?;
    session.commit("first").max_concurrent_nodes(2).execute().await?;

    // the second-generation layout: one `repo` object, no refs prefix and no
    // configuration file
    assert!(dir.path().join("repo").is_file());
    assert!(!dir.path().join("refs").exists());
    assert!(!dir.path().join("config.yaml").exists());
    for prefix in ["snapshots", "manifests", "transactions", "chunks"] {
        assert!(dir.path().join(prefix).is_dir(), "missing {prefix}");
    }
    // overwriting the table of contents keeps a backup of the previous copy
    repo.create_tag("v1", &repo.lookup_branch("main").await?).await?;
    assert!(dir.path().join("overwritten").is_dir());

    // and the handle itself round-trips through its own serialisation
    let bytes = repo.as_bytes()?;
    let restored = Repository::from_bytes(&bytes)?;
    assert_eq!(restored.spec_version(), SpecVersionBin::V2);
    assert_eq!(
        restored.lookup_branch("main").await?,
        repo.lookup_branch("main").await?
    );
    Ok(())
}

#[tokio::test]
async fn n20_spec_version_dispatch__a_first_generation_repository_is_still_writable() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(
        None,
        Arc::clone(&storage),
        HashMap::new(),
        Some(SpecVersionBin::V1),
        true,
    )
    .await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V1);

    let path = p("/array");
    seed_array(&repo, &path, 3).await?;
    repo.create_branch("dev", &repo.lookup_branch("main").await?).await?;
    repo.create_tag("v1", &repo.lookup_branch("main").await?).await?;

    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(reopened.spec_version(), SpecVersionBin::V1);
    let session = reopened.readonly_session(&tip("main")).await?;
    for i in 0..3u32 {
        assert_eq!(chunk_bytes(&session, &path, i).await?, Some(Bytes::from(i.to_string())));
    }
    // the first-generation layout keeps refs and a configuration file
    assert!(dir.path().join("refs").exists());
    assert!(!dir.path().join("repo").exists());
    Ok(())
}

#[tokio::test]
async fn n20_spec_version_dispatch__second_generation_only_apis_reject_v1() -> TestResult {
    ic_core!();
    ic_format!();
    ic_refs!();
    ic_repo_info!();
    ic_repository!();
    ic_session!();
    ic_snapshot!();
    fx_mem_repo_at!();
    fx_mem_storage!();
    fx_messages!();
    fx_p!();
    fx_seed_root!();
    fx_tip!();
    let repo = mem_repo_at(SpecVersionBin::V1).await;
    seed_root(&repo, "first").await?;

    let bad = |err: icechunk::repository::RepositoryError| {
        matches!(
            err.kind,
            RepositoryErrorKind::BadRepoVersion {
                minimum_spec_version: SpecVersionBin::V2
            }
        )
    };
    // all fourteen of §16's second-generation-only operations, each one named
    let props = SnapshotProperties::from([("a".to_string(), serde_json::json!(1))]);
    let status = RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: Utc::now(),
        limited_availability_reason: None,
    };
    assert!(bad(repo.get_metadata().await.unwrap_err()));
    assert!(bad(repo.set_metadata(&props).await.unwrap_err()));
    assert!(bad(repo.update_metadata(&props).await.err().expect("update_metadata")));
    assert!(bad(repo.get_status().await.unwrap_err()));
    assert!(bad(repo.set_status(&status).await.unwrap_err()));
    assert!(bad(repo.feature_flags().await.err().expect("feature_flags")));
    assert!(bad(repo.enabled_feature_flags().await.err().expect("enabled_feature_flags")));
    assert!(bad(
        repo.disabled_feature_flags().await.err().expect("disabled_feature_flags")
    ));
    assert!(bad(repo.ops_log().await.err().expect("ops_log must fail on V1")));
    assert!(bad(repo.rearrange_session("main").await.unwrap_err()));
    assert!(repo.set_feature_flag("move_node", Some(false)).await.is_err());
    assert!(repo.asset_manager().fetch_repo_info().await.is_err());

    // `move_node` is gated on the session rather than the repository
    let mut mover = repo.writable_session("main").await?;
    let err = mover.move_node(Path::root(), p("/moved")).await.unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::RepositoryError(RepositoryErrorKind::BadRepoVersion {
            minimum_spec_version: SpecVersionBin::V2
        })
    ));

    // amending is a second-generation capability too
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/a"), Bytes::new()).await?;
    let err = session.commit("amend").amend().execute().await.unwrap_err();
    assert!(matches!(
        err.kind,
        SessionErrorKind::RepositoryError(RepositoryErrorKind::BadRepoVersion { .. })
    ));

    // but ordinary versioning keeps working
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/b"), Bytes::new()).await?;
    session.commit("second").execute().await?;
    assert_eq!(messages(&repo, &tip("main")).await?.len(), 3);

    // and a missing branch is the same error shape on this generation as on the
    // second one (TEST_INTERFACE §17)
    let err = repo.lookup_branch("no-such-branch").await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(ref name))
            if name == "no-such-branch"
    ));
    Ok(())
}

#[tokio::test]
async fn n20_spec_version_dispatch__fetch_spec_version_detects_both() -> TestResult {
    ic_core!();
    ic_format!();
    ic_detected_spec!();
    fx_mem_storage!();
    let v2_storage = mem_storage().await;
    Repository::create(None, Arc::clone(&v2_storage), HashMap::new(), None, true).await?;
    let detected = Repository::fetch_spec_version(Arc::clone(&v2_storage), None)
        .await?
        .expect("a repository exists");
    assert_eq!(detected.spec_version(), SpecVersionBin::V2);
    assert!(matches!(detected, DetectedSpecVersion::V2Plus { .. }));
    assert!(Repository::exists(Arc::clone(&v2_storage), None).await?);

    let v1_storage = mem_storage().await;
    Repository::create(
        None,
        Arc::clone(&v1_storage),
        HashMap::new(),
        Some(SpecVersionBin::V1),
        true,
    )
    .await?;
    let detected = Repository::fetch_spec_version(Arc::clone(&v1_storage), None)
        .await?
        .expect("a repository exists");
    assert_eq!(detected.spec_version(), SpecVersionBin::V1);
    assert!(matches!(detected, DetectedSpecVersion::V1));

    // an empty prefix holds no repository at all
    let empty = mem_storage().await;
    assert!(Repository::fetch_spec_version(Arc::clone(&empty), None).await?.is_none());
    assert!(!Repository::exists(empty, None).await?);
    Ok(())
}

// ===========================================================================
// Inherited regression groups. One end-to-end path through each milestone this
// step still carries, expressed through the v2.0.0 surface.
// ===========================================================================

// ---------------------------------------------------------------------------
// i01 — milestones 01 and 02: the repository format, sessions, conflicts and
// virtual chunk containers.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i01_core_repository_versioning__create_open_branches_and_tags() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    fx_mem_storage!();
    fx_seed_root!();
    let storage = mem_storage().await;
    assert!(!Repository::exists(Arc::clone(&storage), None).await?);
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    assert!(Repository::exists(Arc::clone(&storage), None).await?);
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), vec!["main"]);

    let first = seed_root(&repo, "first").await?;
    repo.create_branch("dev", &first).await?;
    repo.create_tag("v1", &first).await?;
    assert_eq!(repo.lookup_branch("dev").await?, first);
    assert_eq!(repo.lookup_tag("v1").await?, first);
    assert_eq!(repo.resolve_version(&VersionInfo::TagRef("v1".into())).await?, first);

    // creating either twice is refused, and `main` cannot be deleted
    assert!(repo.create_branch("dev", &first).await.is_err());
    assert!(repo.create_tag("v1", &first).await.is_err());
    let err = repo.delete_branch("main").await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::CannotDeleteMain));

    repo.delete_branch("dev").await?;
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), vec!["main"]);

    // a second handle over the same storage sees the same repository
    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(reopened.lookup_tag("v1").await?, first);
    Ok(())
}

#[tokio::test]
async fn i01_core_repository_versioning__sessions_write_and_read_back_through_the_store() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_tip!();
    let repo = mem_repo().await;
    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;

    store.set("zarr.json", Bytes::from_static(GROUP_META)).await?;
    store
        .set(
            "array/zarr.json",
            array_meta("[4]", r#"{"name":"regular","configuration":{"chunk_shape":[2]}}"#),
        )
        .await?;
    store.set("array/c/0", Bytes::from_static(b"0011")).await?;
    store.set("array/c/1", Bytes::from_static(b"2233")).await?;

    assert_eq!(store.get("array/c/0", &ByteRange::ALL).await?, Bytes::from_static(b"0011"));
    assert!(store.exists("array/c/1").await?);
    let mut keys: Vec<String> = store.list().await?.try_collect().await?;
    keys.sort();
    assert_eq!(
        keys,
        vec![
            "array/c/0".to_string(),
            "array/c/1".to_string(),
            "array/zarr.json".to_string(),
            "zarr.json".to_string(),
        ]
    );

    // `list_prefix` filters the store's own key space, which carries no leading
    // slash, so the array's subtree is `array` and the root group's document is
    // outside it
    let prefixed: Vec<String> = store.list_prefix("array").await?.try_collect().await?;
    for key in ["array/c/0", "array/c/1", "array/zarr.json"] {
        assert!(prefixed.iter().any(|k| k == key), "list_prefix: {key} in {prefixed:?}");
    }
    assert!(!prefixed.iter().any(|k| k == "zarr.json"), "{prefixed:?}");
    let mut dir: Vec<String> = store.list_dir("array").await?.try_collect().await?;
    dir.sort();
    assert_eq!(dir, vec!["c".to_string(), "zarr.json".to_string()]);

    store.delete("array/c/1").await?;
    assert!(!store.exists("array/c/1").await?);
    store.set("array/c/1", Bytes::from_static(b"2233")).await?;

    session.write().await.commit("wrote data").max_concurrent_nodes(2).execute().await?;

    let reader = repo.readonly_session(&tip("main")).await?;
    let store = Store::from_session(Arc::new(RwLock::new(reader))).await;
    assert_eq!(store.get("array/c/1", &ByteRange::ALL).await?, Bytes::from_static(b"2233"));
    assert!(store.read_only().await);
    assert!(store.set("array/c/0", Bytes::from_static(b"nope")).await.is_err());
    Ok(())
}

#[tokio::test]
async fn i01_core_repository_versioning__time_travel_by_tag_snapshot_and_timestamp() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_messages!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    let first = seed_array(&repo, &path, 2).await?;
    repo.create_tag("v1", &first).await?;

    let mut session = repo.writable_session("main").await?;
    session
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("changed".into())),
        )
        .await?;
    let second = session.commit("second").max_concurrent_nodes(2).execute().await?;

    // the tip carries the new value ...
    let now = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&now, &path, 0).await?, Some(Bytes::from_static(b"changed")));
    // ... and every older view still carries the old one
    for version in [
        VersionInfo::SnapshotId(first.clone()),
        VersionInfo::TagRef("v1".to_string()),
        VersionInfo::AsOf {
            branch: "main".to_string(),
            at: repo.lookup_snapshot(&first).await?.flushed_at,
        },
    ] {
        let then = repo.readonly_session(&version).await?;
        assert_eq!(chunk_bytes(&then, &path, 0).await?, Some(Bytes::from_static(b"0")));
        assert_eq!(repo.resolve_version(&version).await?, first);
    }
    assert_eq!(messages(&repo, &VersionInfo::SnapshotId(second)).await?.len(), 3);
    Ok(())
}

#[tokio::test]
async fn i01_core_repository_versioning__conflict_detection_and_rebase() -> TestResult {
    ic_core!();
    ic_conflicts!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    seed_array(&repo, &path, 2).await?;

    // two sessions from the same base, writing different chunks
    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(ChunkPayload::Inline("A".into())))
        .await?;
    a.commit("a").max_concurrent_nodes(2).execute().await?;
    b.set_chunk_ref(path.clone(), ChunkIndices(vec![1]), Some(ChunkPayload::Inline("B".into())))
        .await?;
    let err = b.commit("b").max_concurrent_nodes(2).execute().await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::Conflict { .. }));

    // disjoint changes rebase cleanly
    b.rebase(&ConflictDetector).await?;
    b.commit("b").max_concurrent_nodes(2).execute().await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &path, 0).await?, Some(Bytes::from_static(b"A")));
    assert_eq!(chunk_bytes(&session, &path, 1).await?, Some(Bytes::from_static(b"B")));

    // the same chunk from both sides is a real conflict the detector refuses
    let mut c = repo.writable_session("main").await?;
    let mut d = repo.writable_session("main").await?;
    c.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(ChunkPayload::Inline("C".into())))
        .await?;
    c.commit("c").max_concurrent_nodes(2).execute().await?;
    d.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(ChunkPayload::Inline("D".into())))
        .await?;
    assert!(d.commit("d").max_concurrent_nodes(2).execute().await.is_err());
    let err = d.rebase(&ConflictDetector).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RebaseFailed { .. }));

    // a policy that keeps our side resolves it
    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseOurs,
        ..BasicConflictSolver::default()
    };
    d.commit("d").max_concurrent_nodes(2).rebase(&solver, 5).execute().await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &path, 0).await?, Some(Bytes::from_static(b"D")));
    Ok(())
}

#[tokio::test]
async fn i01_core_repository_versioning__virtual_chunk_containers_resolve_absolute_urls() -> TestResult {
    ic_core!();
    ic_config!();
    ic_format!();
    ic_manifest!();
    ic_vchunks!();
    fx_vcc_array!();
    let chunk_dir = tempfile::tempdir()?;
    std::fs::write(chunk_dir.path().join("chunk-1"), b"virtual!!")?;
    let base = chunk_dir.path().to_str().ok_or("utf-8")?;
    let prefix = format!("file://{base}/");

    let container = VirtualChunkContainer::new(
        prefix.clone(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    )?;
    assert_eq!(container.name(), None);
    assert_eq!(container.url_prefix(), prefix);
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(container)?;
    let creds: HashMap<String, Option<Credentials>> =
        [(format!("file://{base}"), None)].into();

    let repo_dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(repo_dir.path()).await?;
    let repo = Repository::create(Some(config), storage, creds, None, true).await?;
    assert!(!repo.authorized_virtual_container_prefixes().is_empty());

    let session = Arc::new(RwLock::new(repo.writable_session("main").await?));
    let store = Store::from_session(Arc::clone(&session)).await;
    vcc_array(&store).await?;
    let location = VirtualChunkLocation::from_url(&format!("file://{base}/chunk-1"))?;
    assert!(!location.is_relative());
    store
        .set_virtual_ref(
            "array/c/0",
            VirtualChunkRef { location, offset: 0, length: 9, checksum: None },
            true,
        )
        .await?;
    assert_eq!(
        store.get("array/c/0", &ByteRange::ALL).await?,
        Bytes::from_static(b"virtual!!")
    );

    // a location no container covers is refused when validation is requested
    let outside = VirtualChunkLocation::from_url("s3://not-configured/chunk")?;
    assert!(
        store
            .set_virtual_ref(
                "array/c/1",
                VirtualChunkRef { location: outside, offset: 0, length: 1, checksum: None },
                true,
            )
            .await
            .is_err()
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// i02 — milestone 03: expiration and garbage collection.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i02_expiration_and_gc__expire_releases_old_snapshots() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_gc!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_history!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_seed_history!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    let ids = seed_history(&repo, &path, 4).await?;
    let before = history(&repo, &tip("main")).await?.len();

    let cutoff = repo.lookup_snapshot(&ids[2]).await?.flushed_at;
    let result = expire(
        Arc::clone(repo.asset_manager()),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert!(!result.released_snapshots.is_empty());
    assert!(result.deleted_refs.is_empty());

    // the branch tip is untouched, the history behind it is shorter
    let repo = Repository::open(None, Arc::clone(repo.storage()), HashMap::new()).await?;
    assert_eq!(repo.lookup_branch("main").await?, *ids.last().expect("commits"));
    let after = history(&repo, &tip("main")).await?.len();
    assert!(after < before, "expiration should shorten the ancestry");
    // and the tip's data is still readable
    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(
        chunk_bytes(&session, &path, 3).await?,
        Some(Bytes::from_static(b"gen-3"))
    );
    Ok(())
}

#[tokio::test]
async fn i02_expiration_and_gc__garbage_collect_deletes_dangling_objects() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_gc!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_seed_history!();
    fx_tip!();
    let repo = mem_repo().await;
    let path = p("/array");
    let ids = seed_history(&repo, &path, 3).await?;

    // abandon the last two commits
    repo.reset_branch("main", &ids[0], Some(ids.last().expect("commits"))).await?;
    let snapshots_before = repo.asset_manager().list_snapshots().await?.count().await;

    let now = Utc::now();
    let config = GCConfig::clean_all(
        now,
        now,
        None,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        false,
    );
    // chunk accounting reports the three kinds of byte separately, and counts
    // inline payloads that garbage collection never sees as objects
    let stats = icechunk::ops::stats::repo_chunks_storage(
        Arc::clone(repo.asset_manager()),
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
    )
    .await?;
    assert!(stats.inlined_bytes > 0, "{stats:?}");
    assert_eq!(stats.virtual_bytes, 0, "{stats:?}");

    let summary =
        garbage_collect(Arc::clone(repo.asset_manager()), &config, None, 100).await?;
    assert!(summary.snapshots_deleted >= 2, "{summary:?}");
    assert!(
        repo.asset_manager().list_snapshots().await?.count().await < snapshots_before
    );

    // what the branch still points at is intact
    let repo = Repository::open(None, Arc::clone(repo.storage()), HashMap::new()).await?;
    let session = repo.readonly_session(&tip("main")).await?;
    assert_eq!(chunk_bytes(&session, &path, 0).await?, Some(Bytes::from_static(b"gen-0")));
    Ok(())
}

#[tokio::test]
async fn i02_expiration_and_gc__a_dry_run_deletes_nothing() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_gc!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    fx_seed_history!();
    let repo = mem_repo().await;
    let path = p("/array");
    let ids = seed_history(&repo, &path, 3).await?;
    repo.reset_branch("main", &ids[0], Some(ids.last().expect("commits"))).await?;

    let snapshots_before = repo.asset_manager().list_snapshots().await?.count().await;
    let now = Utc::now();
    let config = GCConfig::clean_all(
        now,
        now,
        None,
        NonZeroU16::new(50).ok_or("nonzero")?,
        NonZeroUsize::new(512 * 1024 * 1024).ok_or("nonzero")?,
        NonZeroU16::new(50).ok_or("nonzero")?,
        true,
    );
    let summary =
        garbage_collect(Arc::clone(repo.asset_manager()), &config, None, 100).await?;
    assert!(summary.snapshots_deleted >= 2, "a dry run still reports what it would do");
    assert_ne!(summary, GCSummary::default());
    assert_eq!(
        repo.asset_manager().list_snapshots().await?.count().await,
        snapshots_before,
        "a dry run must delete nothing"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// i03 — milestone 04: the FlatBuffers on-disk format and its file headers.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i03_flatbuffers_format__snapshots_and_manifests_round_trip() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_storage!();
    fx_p!();
    fx_tip!();
    let storage = mem_storage().await;
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

    let path = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path.clone(),
            ArrayShape::new(vec![(4u64, 2u32)]).ok_or("shape")?,
            Some(vec!["t".into()]),
            Bytes::from_static(br#"{"this":"array"}"#),
        )
        .await?;
    for i in 0..2u32 {
        let payload = session.get_chunk_writer()?(Bytes::from(format!("chunk-{i}"))).await?;
        session.set_chunk_ref(path.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    let snap = session.commit("wrote").max_concurrent_nodes(2).execute().await?;

    // the snapshot names its manifest files, and both objects exist on storage
    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert_eq!(manifests.len(), 1);
    assert!(manifests[0].size_bytes > 0);
    assert_eq!(repo.asset_manager().list_manifests().await?.count().await, 1);
    assert!(repo.asset_manager().list_snapshots().await?.count().await >= 2);
    assert_eq!(repo.asset_manager().list_chunks().await?.count().await, 2);

    // a fresh handle reads the same bytes back out of those objects
    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    let session = reopened.readonly_session(&tip("main")).await?;
    for i in 0..2u32 {
        assert_eq!(chunk_bytes(&session, &path, i).await?, Some(Bytes::from(format!("chunk-{i}"))));
    }
    let node = session.get_array(&path).await?;
    assert_eq!(node.user_data, Bytes::from_static(br#"{"this":"array"}"#));
    Ok(())
}

#[tokio::test]
async fn i03_flatbuffers_format__transaction_logs_report_the_changes() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    fx_mem_repo!();
    fx_mem_storage!();
    fx_p!();
    fx_seed_array!();
    let repo = mem_repo().await;
    let path = p("/array");
    let first = seed_array(&repo, &path, 2).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/group"), Bytes::new()).await?;
    session
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline("new".into())),
        )
        .await?;
    session.delete_array(path.clone()).await?;
    session.add_group(p("/group/child"), Bytes::new()).await?;
    let second = session.commit("second").max_concurrent_nodes(2).execute().await?;

    let diff = repo
        .diff(&VersionInfo::SnapshotId(first), &VersionInfo::SnapshotId(second))
        .await?;
    assert!(diff.new_groups.contains(&p("/group")));
    assert!(diff.new_groups.contains(&p("/group/child")));
    assert!(diff.deleted_arrays.contains(&path));
    assert!(!diff.is_empty());
    assert_eq!(repo.asset_manager().list_transaction_logs().await?.count().await, 3);
    Ok(())
}

#[tokio::test]
async fn i03_flatbuffers_format__a_bad_spec_version_byte_is_rejected() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    ic_session!();
    fx_seed_root!();
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let snap = seed_root(&repo, "first").await?;

    // every metadata object starts with a 39-byte header whose 37th byte is the
    // format generation the file was written at
    let file = dir.path().join("snapshots").join(snap.to_string());
    let mut bytes = std::fs::read(&file)?;
    assert!(bytes.len() > 39);
    assert_eq!(bytes[36], SpecVersionBin::V2 as u8);
    bytes[36] = 99;
    std::fs::write(&file, &bytes)?;

    // a fresh handle refuses to read it rather than mis-parsing it
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let err = session.get_group(&Path::root()).await.unwrap_err();
    assert!(
        matches!(
            err.kind,
            SessionErrorKind::RepositoryError(RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidSpecVersion { found: 99, .. }
            ))
        ),
        "{err:?}"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// i04 — milestone 05: the `icechunk` command-line interface.
//
// The verifier builds the candidate's binary with `--features cli` and names it
// in `ICECHUNK_TEST_CLI_BIN`. Every invocation runs against a configuration
// file in a temporary `XDG_CONFIG_HOME`, so nothing touches the real one.
// ---------------------------------------------------------------------------

/// Run the candidate's command-line interface with a private configuration
/// directory, returning `(exit status, stdout, stderr)`.
fn run_cli(config_home: &StdPath, args: &[&str]) -> std::io::Result<(Option<i32>, String, String)> {
    let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
        .expect("ICECHUNK_TEST_CLI_BIN must name the candidate's icechunk binary");
    let out = std::process::Command::new(bin)
        .args(args)
        .env("XDG_CONFIG_HOME", config_home)
        .env("HOME", config_home)
        .output()?;
    Ok((
        out.status.code(),
        String::from_utf8_lossy(&out.stdout).into_owned(),
        String::from_utf8_lossy(&out.stderr).into_owned(),
    ))
}

/// Write `cli-config.yaml` naming one local-filesystem repository.
fn write_cli_config(config_home: &StdPath, alias: &str, repo_path: &StdPath) -> std::io::Result<()> {
    let dir = config_home.join("icechunk");
    std::fs::create_dir_all(&dir)?;
    let yaml = format!(
        "repos:\n  {alias}: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
        repo_path.display()
    );
    std::fs::write(dir.join("cli-config.yaml"), yaml)
}

#[tokio::test]
async fn i04_cli_repo_and_snapshots__help_lists_the_command_groups() -> TestResult {
    let home = tempfile::tempdir()?;
    let (code, stdout, _) = run_cli(home.path(), &["--help"])?;
    assert_eq!(code, Some(0));
    for group in ["repo", "snapshot", "config"] {
        assert!(stdout.contains(group), "`--help` should mention `{group}`:\n{stdout}");
    }

    // an unknown subcommand is a usage error, not a crash
    let (code, _, _) = run_cli(home.path(), &["definitely-not-a-command"])?;
    assert!(matches!(code, Some(c) if c != 0));

    // the `config` group carries `init`, `add` and `list`; the first two prompt
    // interactively for the repository definition, so only their presence in the
    // grammar is exercised here (TEST_INTERFACE §18.4)
    let (code, stdout, _) = run_cli(home.path(), &["config", "--help"])?;
    assert_eq!(code, Some(0));
    for sub in ["init", "add", "list"] {
        assert!(stdout.contains(sub), "`config --help` should mention `{sub}`:\n{stdout}");
    }
    Ok(())
}

#[tokio::test]
async fn i04_cli_repo_and_snapshots__config_list_prints_the_repository() -> TestResult {
    let home = tempfile::tempdir()?;
    let repo_dir = tempfile::tempdir()?;
    write_cli_config(home.path(), "demo", repo_dir.path())?;

    let (code, stdout, stderr) = run_cli(home.path(), &["config", "list"])?;
    assert_eq!(code, Some(0), "stderr: {stderr}");
    assert!(stdout.contains("demo"), "{stdout}");
    assert!(
        stdout.contains(&repo_dir.path().display().to_string()),
        "the configured path should be printed:\n{stdout}"
    );
    Ok(())
}

#[tokio::test]
async fn i04_cli_repo_and_snapshots__repo_create_then_snapshot_list() -> TestResult {
    ic_core!();
    ic_format!();
    ic_snapshot!();
    fx_seed_root!();
    let home = tempfile::tempdir()?;
    let repo_dir = tempfile::tempdir()?;
    let root = repo_dir.path().join("repo");
    std::fs::create_dir_all(&root)?;
    write_cli_config(home.path(), "demo", &root)?;

    let (code, _, stderr) = run_cli(home.path(), &["repo", "create", "demo"])?;
    assert_eq!(code, Some(0), "stderr: {stderr}");

    // the library sees the repository the command created
    let storage = new_local_filesystem_storage(&root).await?;
    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    seed_root(&repo, "from the library").await?;

    let (code, stdout, stderr) = run_cli(home.path(), &["snapshot", "list", "demo"])?;
    assert_eq!(code, Some(0), "stderr: {stderr}");
    assert!(stdout.contains("from the library"), "{stdout}");
    assert!(stdout.contains(Snapshot::INITIAL_COMMIT_MESSAGE), "{stdout}");

    // `-n` bounds how many snapshots are listed
    let (code, stdout, _) = run_cli(home.path(), &["snapshot", "list", "demo", "-n", "1"])?;
    assert_eq!(code, Some(0));
    assert_eq!(stdout.lines().filter(|l| !l.trim().is_empty()).count(), 1);
    Ok(())
}

#[tokio::test]
async fn i04_cli_repo_and_snapshots__an_unknown_alias_fails() -> TestResult {
    let home = tempfile::tempdir()?;
    let repo_dir = tempfile::tempdir()?;
    write_cli_config(home.path(), "demo", repo_dir.path())?;

    let (code, _, _) = run_cli(home.path(), &["snapshot", "list", "no-such-alias"])?;
    assert!(matches!(code, Some(c) if c != 0));

    let (code, _, _) = run_cli(home.path(), &["repo", "create", "no-such-alias"])?;
    assert!(matches!(code, Some(c) if c != 0));

    // `demo` resolves, but the directory it names holds no repository, so the
    // repository cannot be opened and the exit status is non-zero
    let (code, _, _) = run_cli(home.path(), &["snapshot", "list", "demo"])?;
    assert!(matches!(code, Some(c) if c != 0));
    Ok(())
}

#[tokio::test]
async fn i04_cli_repo_and_snapshots__an_unparsable_configuration_is_treated_as_empty()
-> TestResult {
    let home = tempfile::tempdir()?;
    let repo_dir = tempfile::tempdir()?;
    let root = repo_dir.path().join("repo");
    std::fs::create_dir_all(&root)?;
    write_cli_config(home.path(), "demo", &root)?;

    // with a readable configuration the alias resolves
    let (code, _, stderr) = run_cli(home.path(), &["repo", "create", "demo"])?;
    assert_eq!(code, Some(0), "stderr: {stderr}");

    // overwrite it with something that is not a `CliConfig`
    let config_file = home.path().join("icechunk").join("cli-config.yaml");
    std::fs::write(&config_file, "repos: [this is not a map\n  - neither is this\n")?;

    // `config list` still succeeds, and lists nothing
    let (code, stdout, stderr) = run_cli(home.path(), &["config", "list"])?;
    assert_eq!(code, Some(0), "stderr: {stderr}");
    assert!(
        !stdout.contains("demo"),
        "an unparsable configuration should list no alias:\n{stdout}"
    );
    // and every alias then fails to resolve
    let (code, _, _) = run_cli(home.path(), &["snapshot", "list", "demo"])?;
    assert!(matches!(code, Some(c) if c != 0));
    let (code, _, _) = run_cli(home.path(), &["repo", "create", "demo"])?;
    assert!(matches!(code, Some(c) if c != 0));
    Ok(())
}

// ---------------------------------------------------------------------------
// i05 — milestone 06: manifest splitting.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i05_manifest_splitting__the_configuration_round_trips() -> TestResult {
    ic_core!();
    ic_config!();
    fx_mem_storage!();
    fx_splitting!();
    let storage = mem_storage().await;
    let config = RepositoryConfig {
        manifest: Some(splitting(2)),
        ..RepositoryConfig::default()
    };
    let repo = Repository::create(
        Some(config.clone()),
        Arc::clone(&storage),
        HashMap::new(),
        None,
        true,
    )
    .await?;
    repo.save_config().await?;

    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(reopened.config().manifest(), config.manifest());
    let splits = reopened.config().manifest().splitting();
    let sizes = splits.split_sizes.as_ref().ok_or("split sizes")?;
    assert_eq!(sizes.len(), 1);
    assert_eq!(sizes[0].1[0].num_chunks, 2);
    assert!(matches!(sizes[0].1[0].condition, ManifestSplitDimCondition::Any));
    assert!(matches!(sizes[0].0, ManifestSplitCondition::PathMatches { .. }));
    Ok(())
}

#[tokio::test]
async fn i05_manifest_splitting__split_manifests_read_back_identically() -> TestResult {
    ic_core!();
    ic_config!();
    ic_format!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_mem_storage!();
    fx_p!();
    fx_split_repo!();
    fx_splitting!();
    fx_tip!();
    let storage = mem_storage().await;
    let (repo, path, snap) = split_repo(Arc::clone(&storage), 2, 4).await?;

    // four chunks split every two produce two manifest files
    assert_eq!(repo.lookup_manifest_files(&snap).await?.count(), 2);
    assert_eq!(repo.asset_manager().list_manifests().await?.count().await, 2);

    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    let session = reopened.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(
            chunk_bytes(&session, &path, i).await?,
            Some(Bytes::from(format!("chunk-{i}")))
        );
    }
    Ok(())
}

#[tokio::test]
async fn i05_manifest_splitting__rewrite_manifests_applies_the_new_splitting() -> TestResult {
    ic_core!();
    ic_config!();
    ic_format!();
    ic_snapshot!();
    ic_rewrite_manifests!();
    ic_repository!();
    ic_session!();
    ic_session_new!();
    fx_chunk_bytes!();
    fx_mem_storage!();
    fx_p!();
    fx_split_repo!();
    fx_splitting!();
    fx_tip!();
    let storage = mem_storage().await;
    let (repo, path, snap) = split_repo(Arc::clone(&storage), 8, 4).await?;
    assert_eq!(repo.lookup_manifest_files(&snap).await?.count(), 1);

    // reopen with a finer split and rewrite in place
    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        manifest: Some(splitting(2)),
        ..RepositoryConfig::default()
    };
    let repo = repo.reopen(Some(config), None).await?;
    let rewritten =
        rewrite_manifests(&repo, "main", "resplit", 4, None, CommitMethod::NewCommit)
            .await?;
    assert_eq!(repo.lookup_manifest_files(&rewritten).await?.count(), 2);
    assert_eq!(repo.lookup_branch("main").await?, rewritten);

    // every chunk still reads back the same bytes
    let session = repo.readonly_session(&tip("main")).await?;
    for i in 0..4u32 {
        assert_eq!(
            chunk_bytes(&session, &path, i).await?,
            Some(Bytes::from(format!("chunk-{i}")))
        );
    }
    Ok(())
}

// ---------------------------------------------------------------------------
// i06 — milestones 07 and 08: the asynchronous API, storage backends and
// their settings.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn i06_async_storage_and_backends__constructors_and_trait_methods_are_async() -> TestResult {
    ic_core!();
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    // `can_write`, `default_settings` and `root_is_clean` are all async and
    // fallible at this release
    assert!(storage.can_write().await?);
    let settings: icechunk::storage::Settings = storage.default_settings().await?;
    assert!(storage.root_is_clean(&settings).await?);
    let info = storage.storage_info();
    assert!(!info.backend_type.is_empty());

    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    assert!(!storage.root_is_clean(&settings).await?);
    assert!(repo.asset_manager().can_write_to_storage().await?);
    assert!(repo.storage_settings().retries().max_tries().get() > 0);
    assert_eq!(icechunk::config::CachingConfig::default().num_snapshot_nodes(), 500_000);
    assert!(repo.storage_settings().concurrency().max_concurrent_requests_for_object().get() > 0);
    Ok(())
}

#[tokio::test]
async fn i06_async_storage_and_backends__the_local_filesystem_backend_round_trips() -> TestResult {
    ic_core!();
    ic_format!();
    ic_manifest!();
    ic_snapshot!();
    ic_repository!();
    ic_session!();
    fx_chunk_bytes!();
    fx_p!();
    fx_seed_array!();
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let path = p("/array");
    seed_array(&repo, &path, 3).await?;
    repo.create_tag("v1", &repo.lookup_branch("main").await?).await?;
    drop(repo);

    // a completely fresh handle over the same directory sees everything
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), vec!["v1"]);
    let session = repo.readonly_session(&VersionInfo::TagRef("v1".into())).await?;
    for i in 0..3u32 {
        assert_eq!(chunk_bytes(&session, &path, i).await?, Some(Bytes::from(i.to_string())));
    }
    Ok(())
}

#[tokio::test]
async fn i06_async_storage_and_backends__settings_and_retries_are_configurable() -> TestResult {
    ic_core!();
    ic_format!();
    ic_repository!();
    fx_mem_storage!();
    fx_messages!();
    fx_seed_root!();
    fx_tip!();
    let retries = icechunk::storage::RetriesSettings {
        max_tries: Some(NonZeroU16::new(7).ok_or("nonzero")?),
        initial_backoff_ms: Some(11),
        max_backoff_ms: Some(222),
    };
    let timeouts = icechunk::storage::TimeoutSettings {
        connect_timeout_ms: Some(1_000),
        read_timeout_ms: Some(2_000),
        operation_timeout_ms: Some(3_000),
        operation_attempt_timeout_ms: Some(4_000),
    };
    let settings = icechunk::storage::Settings {
        retries: Some(retries),
        timeouts: Some(timeouts),
        ..icechunk::storage::Settings::default()
    };
    let config = RepositoryConfig {
        storage: Some(settings),
        max_concurrent_requests: Some(3),
        ..RepositoryConfig::default()
    };

    let storage = mem_storage().await;
    let repo =
        Repository::create(Some(config), storage, HashMap::new(), None, true).await?;
    let effective = repo.storage_settings();
    assert_eq!(effective.retries().max_tries().get(), 7);
    assert_eq!(effective.retries().initial_backoff_ms(), 11);
    assert_eq!(
        effective.timeouts().and_then(|t| t.connect_timeout_ms),
        Some(1_000)
    );
    assert_eq!(effective.timeouts().and_then(|t| t.read_timeout_ms), Some(2_000));
    assert_eq!(repo.config().max_concurrent_requests(), 3);

    // the repository still works with them applied
    seed_root(&repo, "first").await?;
    assert_eq!(messages(&repo, &tip("main")).await?.len(), 2);
    Ok(())
}

#[tokio::test]
async fn i06_async_storage_and_backends__object_store_config_variants_resolve() -> TestResult {
    ic_core!();
    ic_repository!();
    fx_mem_storage!();
    fx_s3_options!();
    // every backend descriptor a configuration may name is still constructible
    let variants = vec![
        ObjectStoreConfig::InMemory,
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/tmp")),
        ObjectStoreConfig::S3(s3_options()),
        ObjectStoreConfig::S3Compatible(s3_options()),
        ObjectStoreConfig::Tigris(s3_options()),
        ObjectStoreConfig::Gcs(HashMap::new()),
        ObjectStoreConfig::Azure(HashMap::new()),
        ObjectStoreConfig::Http(HashMap::new()),
    ];
    assert_eq!(variants.len(), 8);
    // and they round-trip through the configuration serialisation
    let json = serde_json::to_string(&variants)?;
    let back: Vec<ObjectStoreConfig> = serde_json::from_str(&json)?;
    assert_eq!(back.len(), 8);
    assert!(matches!(back[0], ObjectStoreConfig::InMemory));

    // an empty prefix holds no repository, and opening it says so
    let empty = mem_storage().await;
    let err = Repository::open(None, Arc::clone(&empty), HashMap::new()).await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::RepositoryDoesntExist));
    Ok(())
}
