// Verifier-owned conformance driver for step 03 (`icechunk` 0.1.0).
//
// Every case reaches the candidate through the crate's PUBLIC `icechunk::`
// surface only, exactly as `/app/TEST_INTERFACE.md` documents it. Nothing here
// needs the network, a cloud account, or a local object-store emulator: the two
// backends used are `icechunk::new_in_memory_storage()` and
// `icechunk::new_local_filesystem_storage(&Path)` over `tempfile::tempdir()`.
//
// Case functions are named `<group>__<case>`; `tests/assets/groups.txt` is the
// authority for which groups exist and how many cases each one has.
#![allow(clippy::expect_used, clippy::unwrap_used, clippy::panic)]
#![allow(non_snake_case)]

use std::{
    collections::{HashMap, HashSet},
    num::NonZeroU64,
    path::PathBuf,
    sync::Arc,
};

use bytes::Bytes;
use chrono::{DateTime, TimeDelta, Utc};
use futures::{StreamExt, TryStreamExt};
// FILE-SCOPE PRELUDE — DO NOT "TIDY" THIS BACK TOGETHER.
//
// Everything named here — every `use` item, `const`, `type` and module-level
// `fn` — survives in EVERY per-group attribution slice, because the slicer in
// `tests/test.sh` removes case functions and nothing else. If one file-scope
// name existed only at this release, the slice of every group, including the
// inherited-regression ones, would fail to compile against the previous
// checkpoint and a no-op continuation would score 0 instead of its inherited
// share. So this list is restricted to surface that already existed at the
// PREVIOUS release; everything this release introduces is pulled in by the
// individual case bodies that need it, and the module-level fixtures that call
// this release's changed signatures are `macro_rules!` below (a macro body is
// not type-checked until it is expanded, so it never reaches a slice that does
// not use it).
use icechunk::{
    conflicts::{basic_solver::BasicConflictSolver, detector::ConflictDetector, Conflict},
    format::{
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::{NodeData, SnapshotProperties, ZarrArrayMetadata},
        ByteRange, ChunkIndices, ManifestId, Path, SnapshotId,
    },
    metadata::{ChunkKeyEncoding, ChunkShape, DataType, FillValue},
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{garbage_collect, Action, GCConfig, GCSummary},
    refs::{Ref, RefError},
    repository::{RepositoryError, VersionInfo},
    session::{get_chunk, SessionError},
    storage::ObjectStorage,
    virtual_chunks::VirtualChunkContainer,
    ObjectStoreConfig, Repository, RepositoryConfig, Storage,
};
use tokio::io::{AsyncRead, AsyncReadExt};

type R = Result<(), Box<dyn std::error::Error>>;

/// The first four bytes of every Zstandard frame (RFC 8478 section 3.1.1).
const ZSTD_FRAME_MAGIC: [u8; 4] = [0x28, 0xB5, 0x2F, 0xFD];
/// `ICE<U+1F9CA>CHUNK` in UTF-8, spelled out byte by byte.
const MAGIC_LITERAL: [u8; 12] =
    [0x49, 0x43, 0x45, 0xF0, 0x9F, 0xA7, 0x8A, 0x43, 0x48, 0x55, 0x4E, 0x4B];
/// The documented on-disk header length: 12 + 24 + 1 + 1 + 1.
const HEADER_LEN: usize = 39;

// ---------------------------------------------------------------------------
// helpers (deliberately free of `__`, so the verifier's per-group attribution
// slicer never mistakes one for a case function)
// ---------------------------------------------------------------------------

fn mem_storage() -> Arc<dyn Storage + Send + Sync> {
    new_in_memory_storage().expect("in-memory storage")
}

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

/// The bytes written at one chunk coordinate. Every coordinate and every
/// generation gets DISTINCT content, so a count of chunk objects means the same
/// thing whether or not an implementation deduplicates identical payloads.
fn chunk_bytes(coord: &[u32; 2], generation: u32) -> Bytes {
    Bytes::from(format!("icechunk-chunk-{}-{}-gen{}", coord[0], coord[1], generation))
}

/// Read the first `n` bytes of an object exactly as the storage layer serves them.
async fn read_n(rd: Box<dyn AsyncRead + Unpin + Send>, n: usize) -> Vec<u8> {
    let mut rd = rd;
    let mut buf = vec![0u8; n];
    rd.read_exact(&mut buf).await.expect("object is shorter than the bytes requested");
    buf
}

/// Every byte of an object, exactly as the storage layer serves them.
async fn read_all(rd: Box<dyn AsyncRead + Unpin + Send>) -> Vec<u8> {
    let mut rd = rd;
    let mut buf = Vec::new();
    rd.read_to_end(&mut buf).await.expect("read the whole object");
    buf
}

/// The 39-byte header plus the first four bytes that follow it.
async fn header_and_payload_start(
    rd: Box<dyn AsyncRead + Unpin + Send>,
) -> (Vec<u8>, Vec<u8>) {
    let all = read_n(rd, HEADER_LEN + 4).await;
    (all[..HEADER_LEN].to_vec(), all[HEADER_LEN..].to_vec())
}

// ---------------------------------------------------------------------------
// Release-new fixtures, as `macro_rules!`.
//
// Each of these calls a signature this release introduced or changed, so it
// cannot live at file scope (see the prelude note above). Each expands to an
// ordinary `fn` defined inside the case body, and each is self-contained: no
// macro here invokes another, so expanding any combination of them in one case
// body can never declare a name twice.
// ---------------------------------------------------------------------------

/// `repo.ancestry(&VersionInfo)`, collected latest-first.
macro_rules! def_ancestry_of {
    () => {
        #[allow(dead_code)]
        async fn ancestry_of(
            repo: &Repository,
            version: VersionInfo,
        ) -> Vec<icechunk::format::snapshot::SnapshotInfo> {
            repo.ancestry(&version)
                .await
                .expect("ancestry")
                .try_collect::<Vec<_>>()
                .await
                .expect("ancestry stream")
        }
    };
}

/// The commit messages along a branch's ancestry, latest-first.
macro_rules! def_branch_messages {
    () => {
        #[allow(dead_code)]
        async fn branch_messages(repo: &Repository, branch: &str) -> Vec<String> {
            repo.ancestry(&VersionInfo::BranchTipRef(branch.to_string()))
                .await
                .expect("ancestry")
                .try_collect::<Vec<_>>()
                .await
                .expect("ancestry stream")
                .into_iter()
                .map(|i| i.message)
                .collect()
        }
    };
}

/// The commit messages along a tag's ancestry, latest-first.
macro_rules! def_tag_messages {
    () => {
        #[allow(dead_code)]
        async fn tag_messages(repo: &Repository, tag: &str) -> Vec<String> {
            repo.ancestry(&VersionInfo::TagRef(tag.to_string()))
                .await
                .expect("ancestry")
                .try_collect::<Vec<_>>()
                .await
                .expect("ancestry stream")
                .into_iter()
                .map(|i| i.message)
                .collect()
        }
    };
}

/// An `AssetManager` with no cache, so nothing is answered from memory.
macro_rules! def_no_cache_assets {
    () => {
        #[allow(dead_code)]
        fn no_cache_assets(
            storage: &Arc<dyn Storage + Send + Sync>,
            settings: &icechunk::storage::Settings,
        ) -> Arc<icechunk::asset_manager::AssetManager> {
            Arc::new(icechunk::asset_manager::AssetManager::new_no_cache(
                Arc::clone(storage),
                settings.clone(),
                1,
            ))
        }
    };
}

/// `(snapshots, manifests, chunks, transaction logs)` currently in the store.
macro_rules! def_count_objects {
    () => {
        #[allow(dead_code)]
        async fn count_objects(
            storage: &Arc<dyn Storage + Send + Sync>,
            settings: &icechunk::storage::Settings,
        ) -> (usize, usize, usize, usize) {
            let snapshots = storage.list_snapshots(settings).await.unwrap().count().await;
            let manifests = storage.list_manifests(settings).await.unwrap().count().await;
            let chunks = storage.list_chunks(settings).await.unwrap().count().await;
            let txlogs =
                storage.list_transaction_logs(settings).await.unwrap().count().await;
            (snapshots, manifests, chunks, txlogs)
        }
    };
}

/// The set of snapshot ids the storage layer still lists.
macro_rules! def_snapshot_ids_in_store {
    () => {
        #[allow(dead_code)]
        async fn snapshot_ids_in_store(
            storage: &Arc<dyn Storage + Send + Sync>,
            settings: &icechunk::storage::Settings,
        ) -> HashSet<SnapshotId> {
            storage
                .list_snapshots(settings)
                .await
                .unwrap()
                .try_collect::<Vec<_>>()
                .await
                .unwrap()
                .into_iter()
                .map(|info| info.id)
                .collect()
        }
    };
}

/// The manifest ids a snapshot names, in a stable order.
macro_rules! def_manifest_ids_of {
    () => {
        #[allow(dead_code)]
        async fn manifest_ids_of(repo: &Repository, snap: &SnapshotId) -> Vec<ManifestId> {
            let snapshot =
                repo.asset_manager().fetch_snapshot(snap).await.expect("snapshot");
            let mut ids: Vec<ManifestId> =
                snapshot.manifest_files().keys().cloned().collect();
            ids.sort_by_key(|id| id.to_string());
            ids
        }
    };
}

/// A repository with one array whose chunks are all materialised (never
/// inlined), so chunk objects really exist in the store.
macro_rules! def_repo_with_materialised_chunks {
    () => {
        #[allow(dead_code)]
        async fn repo_with_materialised_chunks(
            storage: &Arc<dyn Storage + Send + Sync>,
            coords: &[[u32; 2]],
            message: &str,
        ) -> Result<(Repository, Path, SnapshotId), Box<dyn std::error::Error>> {
            let repo = Repository::create(
                Some(RepositoryConfig {
                    inline_chunk_threshold_bytes: Some(0),
                    ..Default::default()
                }),
                Arc::clone(storage),
                HashMap::new(),
            )
            .await?;
            let array: Path = "/array".try_into()?;
            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root()).await?;
            session.add_array(array.clone(), array_meta(vec![8, 8], vec![1, 1])).await?;
            for c in coords {
                let writer = session.get_chunk_writer();
                let payload = writer(chunk_bytes(c, 0)).await?;
                session
                    .set_chunk_ref(array.clone(), ChunkIndices(c.to_vec()), Some(payload))
                    .await?;
            }
            let snap = session.commit(message, None).await?;
            Ok((repo, array, snap))
        }
    };
}

/// A repo whose second commit overwrote `overwritten` chunks and was then
/// forgotten by resetting the branch back to the first commit. Self-contained:
/// it builds the materialised-chunk repository itself rather than calling
/// `repo_with_materialised_chunks`, so a case may expand both.
macro_rules! def_repo_with_forgotten_commit {
    () => {
        #[allow(dead_code)]
        async fn repo_with_forgotten_commit(
            storage: &Arc<dyn Storage + Send + Sync>,
        ) -> Result<(Repository, Path, SnapshotId, SnapshotId), Box<dyn std::error::Error>>
        {
            let repo = Repository::create(
                Some(RepositoryConfig {
                    inline_chunk_threshold_bytes: Some(0),
                    ..Default::default()
                }),
                Arc::clone(storage),
                HashMap::new(),
            )
            .await?;
            let array: Path = "/array".try_into()?;
            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root()).await?;
            session.add_array(array.clone(), array_meta(vec![8, 8], vec![1, 1])).await?;
            for c in [[0u32, 0u32], [1, 1], [2, 2], [3, 3]] {
                let writer = session.get_chunk_writer();
                let payload = writer(chunk_bytes(&c, 0)).await?;
                session
                    .set_chunk_ref(array.clone(), ChunkIndices(c.to_vec()), Some(payload))
                    .await?;
            }
            let first = session.commit("first", None).await?;
            let mut session = repo.writable_session("main").await?;
            for c in [[0u32, 0u32], [1, 1]] {
                let writer = session.get_chunk_writer();
                let payload = writer(chunk_bytes(&c, 1)).await?;
                session
                    .set_chunk_ref(array.clone(), ChunkIndices(c.to_vec()), Some(payload))
                    .await?;
            }
            let second = session.commit("second", None).await?;
            repo.reset_branch("main", &first).await?;
            Ok((repo, array, first, second))
        }
    };
}

/// Build a repository whose commit produced a snapshot, a manifest and a
/// transaction log, and hand back the ids needed to read all three raw.
/// Self-contained for the same reason as `def_repo_with_forgotten_commit`.
macro_rules! def_repo_with_all_three_metadata_kinds {
    () => {
        #[allow(dead_code)]
        async fn repo_with_all_three_metadata_kinds() -> (
            Arc<dyn Storage + Send + Sync>,
            icechunk::storage::Settings,
            Repository,
            SnapshotId,
            ManifestId,
        ) {
            let storage = mem_storage();
            let settings = storage.default_settings();
            let repo = Repository::create(
                Some(RepositoryConfig {
                    inline_chunk_threshold_bytes: Some(0),
                    ..Default::default()
                }),
                Arc::clone(&storage),
                HashMap::new(),
            )
            .await
            .expect("create the repository");
            let array: Path = "/array".try_into().expect("array path");
            let mut session = repo.writable_session("main").await.expect("session");
            session.add_group(Path::root()).await.expect("add the root group");
            session
                .add_array(array.clone(), array_meta(vec![8, 8], vec![1, 1]))
                .await
                .expect("add the array");
            for c in [[0u32, 0u32], [1u32, 1u32]] {
                let writer = session.get_chunk_writer();
                let payload = writer(chunk_bytes(&c, 0)).await.expect("write a chunk");
                session
                    .set_chunk_ref(array.clone(), ChunkIndices(c.to_vec()), Some(payload))
                    .await
                    .expect("set a chunk ref");
            }
            let snap = session.commit("first", None).await.expect("commit");
            let snapshot =
                repo.asset_manager().fetch_snapshot(&snap).await.expect("snapshot");
            let mut ids: Vec<ManifestId> =
                snapshot.manifest_files().keys().cloned().collect();
            ids.sort_by_key(|id| id.to_string());
            let manifest = ids.pop().expect("one manifest");
            (storage, settings, repo, snap, manifest)
        }
    };
}

/// The repository of `design/007-basic-expiration.md`'s worked example, built
/// exactly in the document's order. Returns the expiration cutoff, taken from
/// the timestamp icechunk itself recorded for commit "8" — the first commit
/// made after the document's threshold — so no case depends on a wall clock.
macro_rules! def_make_design_doc_repo {
    () => {
        #[allow(dead_code)]
        async fn make_design_doc_repo(
            repo: &Repository,
        ) -> Result<DateTime<Utc>, Box<dyn std::error::Error>> {
            // named apart from `branch_messages`/`ancestry_of` so that a case
            // may expand those macros as well as this one
            #[allow(dead_code)]
            async fn design_doc_messages(
                repo: &Repository,
                version: VersionInfo,
            ) -> Vec<String> {
                repo.ancestry(&version)
                    .await
                    .expect("ancestry")
                    .try_collect::<Vec<_>>()
                    .await
                    .expect("ancestry stream")
                    .into_iter()
                    .map(|i| i.message)
                    .collect()
            }
            let branch_of = |b: &str| VersionInfo::BranchTipRef(b.to_string());

            let mut session = repo.writable_session("main").await?;
            session.add_group(Path::root()).await?;
            session.commit("1", None).await?;
            let commit_2 = commit_group(repo, "main", "2").await;
            commit_group(repo, "main", "4").await;
            let commit_5 = commit_group(repo, "main", "5").await;
            repo.create_tag("tag2", &commit_5).await?;

            repo.create_branch("develop", &commit_2).await?;
            let commit_3 = commit_group(repo, "develop", "3").await;
            repo.create_tag("tag1", &commit_3).await?;
            let commit_6 = commit_group(repo, "develop", "6").await;

            repo.create_branch("test", &commit_6).await?;
            let commit_7 = commit_group(repo, "test", "7").await;

            repo.create_branch("qa", &commit_7).await?;
            let commit_8 = commit_group(repo, "qa", "8").await;

            commit_group(repo, "main", "12").await;
            commit_group(repo, "main", "13").await;
            commit_group(repo, "main", "14").await;
            commit_group(repo, "develop", "10").await;
            commit_group(repo, "develop", "11").await;
            commit_group(repo, "test", "9").await;

            assert_eq!(
                design_doc_messages(repo, branch_of("main")).await,
                ["14", "13", "12", "5", "4", "2", "1", "Repository initialized"]
            );
            assert_eq!(
                design_doc_messages(repo, branch_of("develop")).await,
                ["11", "10", "6", "3", "2", "1", "Repository initialized"]
            );
            assert_eq!(
                design_doc_messages(repo, branch_of("test")).await,
                ["9", "7", "6", "3", "2", "1", "Repository initialized"]
            );
            assert_eq!(
                design_doc_messages(repo, branch_of("qa")).await,
                ["8", "7", "6", "3", "2", "1", "Repository initialized"]
            );

            let cutoff = repo
                .ancestry(&VersionInfo::SnapshotId(commit_8.clone()))
                .await?
                .try_collect::<Vec<_>>()
                .await?
                .first()
                .expect("commit 8 is in its own ancestry")
                .flushed_at;
            Ok(cutoff)
        }
    };
}

async fn commit_group(repo: &Repository, branch: &str, name: &str) -> SnapshotId {
    let mut session = repo.writable_session(branch).await.expect("writable session");
    let path = if name == "/" {
        Path::root()
    } else {
        Path::try_from(format!("/{name}").as_str()).expect("path")
    };
    session.add_group(path).await.expect("add group");
    session.commit(name, None).await.expect("commit")
}

fn file_container() -> VirtualChunkContainer {
    VirtualChunkContainer {
        name: "file".to_string(),
        url_prefix: "file://".to_string(),
        store: ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    }
}

/// A configuration carrying exactly ONE virtual chunk container, the `file`
/// one: `clear_virtual_chunk_containers()` drops the built-in defaults and
/// `set_virtual_chunk_container()` then adds this one.
fn config_with_file_container() -> RepositoryConfig {
    let mut config = RepositoryConfig::default();
    config.clear_virtual_chunk_containers();
    config.set_virtual_chunk_container(file_container());
    config
}

/// Two writable sessions anchored at the same branch tip, so the second one to
/// commit is always stale.
async fn sessions_for_conflict(
    repo: &Repository,
) -> Result<(icechunk::session::Session, icechunk::session::Session), Box<dyn std::error::Error>>
{
    let mut setup = repo.writable_session("main").await?;
    setup.add_group(Path::root()).await?;
    setup.add_array("/array".try_into()?, array_meta(vec![4, 4], vec![1, 1])).await?;
    setup.commit("setup", None).await?;
    let first = repo.writable_session("main").await?;
    let second = repo.writable_session("main").await?;
    Ok((first, second))
}

// ===========================================================================
// hdr_metadata_header — the 39-byte header every metadata file starts with
// (design/004-binary-format.md, "## Metadata files" and "### File structure")
// ===========================================================================

#[tokio::test]
async fn hdr_metadata_header__magic_bytes() -> R {
    use icechunk::format::format_constants::ICECHUNK_FORMAT_MAGIC_BYTES;
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, _repo, snap, manifest) =
        repo_with_all_three_metadata_kinds().await;

    let snap_header = read_n(storage.fetch_snapshot(&settings, &snap).await?, 12).await;
    assert_eq!(snap_header, ICECHUNK_FORMAT_MAGIC_BYTES);
    assert_eq!(snap_header, MAGIC_LITERAL);
    assert_eq!(snap_header, "ICE\u{1F9CA}CHUNK".as_bytes());
    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES.len(), 12);

    let man_header =
        read_n(storage.fetch_manifest_unknown_size(&settings, &manifest).await?, 12).await;
    assert_eq!(man_header, MAGIC_LITERAL);

    let tx_header =
        read_n(storage.fetch_transaction_log(&settings, &snap).await?, 12).await;
    assert_eq!(tx_header, MAGIC_LITERAL);
    Ok(())
}

#[tokio::test]
async fn hdr_metadata_header__client_id_is_24_bytes_right_space_padded() -> R {
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, _repo, snap, _manifest) =
        repo_with_all_three_metadata_kinds().await;

    let header = read_n(storage.fetch_snapshot(&settings, &snap).await?, HEADER_LEN).await;
    let field = &header[12..36];
    assert_eq!(field.len(), 24);
    let text = std::str::from_utf8(field)?;
    assert!(text.starts_with("ic-"), "client id was {text:?}");
    let trimmed = text.trim_end_matches(' ');
    assert!(!trimmed.is_empty());
    // padding is ASCII spaces, on the right only
    assert_eq!(text, format!("{trimmed:<24}"));
    assert!(field[trimmed.len()..].iter().all(|b| *b == b' '));
    Ok(())
}

#[tokio::test]
async fn hdr_metadata_header__spec_version_byte() -> R {
    use icechunk::format::format_constants::SpecVersionBin;
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, _repo, snap, manifest) =
        repo_with_all_three_metadata_kinds().await;

    assert_eq!(SpecVersionBin::V0dot1 as u8, 0x01);
    assert_eq!(SpecVersionBin::current() as u8, 0x01);
    // 0x00 is reserved, so it is not a readable spec version
    assert!(SpecVersionBin::try_from(0x00u8).is_err());

    for header in [
        read_n(storage.fetch_snapshot(&settings, &snap).await?, HEADER_LEN).await,
        read_n(
            storage.fetch_manifest_unknown_size(&settings, &manifest).await?,
            HEADER_LEN,
        )
        .await,
        read_n(storage.fetch_transaction_log(&settings, &snap).await?, HEADER_LEN).await,
    ] {
        assert_eq!(header[36], 0x01);
    }
    Ok(())
}

#[tokio::test]
async fn hdr_metadata_header__file_type_byte_identifies_the_kind() -> R {
    use icechunk::format::format_constants::FileTypeBin;
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, _repo, snap, manifest) =
        repo_with_all_three_metadata_kinds().await;

    assert_eq!(FileTypeBin::Snapshot as u8, 0x01);
    assert_eq!(FileTypeBin::Manifest as u8, 0x02);
    assert_eq!(FileTypeBin::Attributes as u8, 0x03);
    assert_eq!(FileTypeBin::TransactionLog as u8, 0x04);
    assert!(FileTypeBin::try_from(0x09u8).is_err());

    let snap_h = read_n(storage.fetch_snapshot(&settings, &snap).await?, HEADER_LEN).await;
    assert_eq!(snap_h[37], FileTypeBin::Snapshot as u8);

    let man_h = read_n(
        storage.fetch_manifest_unknown_size(&settings, &manifest).await?,
        HEADER_LEN,
    )
    .await;
    assert_eq!(man_h[37], FileTypeBin::Manifest as u8);

    let tx_h =
        read_n(storage.fetch_transaction_log(&settings, &snap).await?, HEADER_LEN).await;
    assert_eq!(tx_h[37], FileTypeBin::TransactionLog as u8);
    Ok(())
}

#[tokio::test]
async fn hdr_metadata_header__payload_starts_at_offset_39() -> R {
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, repo, snap, manifest) =
        repo_with_all_three_metadata_kinds().await;

    // the header itself is never compressed, so the compressed payload begins
    // immediately after byte 38
    let (header, payload) =
        header_and_payload_start(storage.fetch_snapshot(&settings, &snap).await?).await;
    assert_eq!(header.len(), HEADER_LEN);
    assert_eq!(payload, ZSTD_FRAME_MAGIC);

    let (_h, payload) = header_and_payload_start(
        storage.fetch_manifest_unknown_size(&settings, &manifest).await?,
    )
    .await;
    assert_eq!(payload, ZSTD_FRAME_MAGIC);

    // and the typed reader still round-trips the very same object
    let decoded = repo.asset_manager().fetch_snapshot(&snap).await?;
    assert_eq!(decoded.message(), "first");
    assert_eq!(decoded.id(), &snap);
    Ok(())
}

#[tokio::test]
async fn hdr_metadata_header__a_doctored_header_is_rejected() -> R {
    use icechunk::format::format_constants::FileTypeBin;
    def_no_cache_assets!();
    def_repo_with_all_three_metadata_kinds!();
    // The header exists so that a reader can REFUSE an object it does not
    // understand instead of misparsing it (TEST_INTERFACE.md 5.4). Each case
    // below takes the real, valid object and changes exactly ONE header byte,
    // leaving the Zstd payload untouched, so only a reader that actually
    // checks the header can tell the difference.
    let (storage, settings, _repo, snap, _manifest) =
        repo_with_all_three_metadata_kinds().await;

    let good = read_all(storage.fetch_snapshot(&settings, &snap).await?).await;
    assert!(good.len() > HEADER_LEN);
    assert!(no_cache_assets(&storage, &settings).fetch_snapshot(&snap).await.is_ok());

    let doctored = [
        // magic bytes that are not the icechunk ones
        ("wrong magic bytes", 0usize, b'X'),
        // 0x00 is reserved, so it is not a readable spec version
        ("a reserved spec version", 36, 0x00),
        // the file type of a manifest, in a snapshot object
        ("the wrong file type", 37, FileTypeBin::Manifest as u8),
        // 0x7F names no CompressionAlgorithmBin variant
        ("an unknown compression algorithm", 38, 0x7F),
    ];
    for (what, offset, byte) in doctored {
        let mut bytes = good.clone();
        assert_ne!(bytes[offset], byte);
        bytes[offset] = byte;
        storage
            .write_snapshot(&settings, snap.clone(), vec![], Bytes::from(bytes))
            .await?;
        // a fresh AssetManager, so nothing is answered from a cache
        let res = no_cache_assets(&storage, &settings).fetch_snapshot(&snap).await;
        assert!(res.is_err(), "{what} was accepted instead of rejected");
    }

    // putting the original bytes back makes the very same object readable
    // again, so the three refusals were caused by the header and nothing else
    storage
        .write_snapshot(&settings, snap.clone(), vec![], Bytes::from(good))
        .await?;
    let restored = no_cache_assets(&storage, &settings).fetch_snapshot(&snap).await?;
    assert_eq!(restored.message(), "first");
    Ok(())
}

// ===========================================================================
// hdr_compression_zstd — byte 38 and the Zstd payload
// (design/004-binary-format.md, "## Metadata files")
// ===========================================================================

#[tokio::test]
async fn hdr_compression_zstd__snapshot_compression_byte() -> R {
    use icechunk::format::format_constants::CompressionAlgorithmBin;
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, _repo, snap, _manifest) =
        repo_with_all_three_metadata_kinds().await;
    assert_eq!(CompressionAlgorithmBin::None as u8, 0x00);
    assert_eq!(CompressionAlgorithmBin::Zstd as u8, 0x01);

    let header = read_n(storage.fetch_snapshot(&settings, &snap).await?, HEADER_LEN).await;
    assert_eq!(header[38], CompressionAlgorithmBin::Zstd as u8);
    Ok(())
}

#[tokio::test]
async fn hdr_compression_zstd__manifests_are_zstd_compressed() -> R {
    use icechunk::format::format_constants::CompressionAlgorithmBin;
    def_manifest_ids_of!();
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, repo, snap, manifest) =
        repo_with_all_three_metadata_kinds().await;

    let (header, payload) = header_and_payload_start(
        storage.fetch_manifest_unknown_size(&settings, &manifest).await?,
    )
    .await;
    assert_eq!(header[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(payload, ZSTD_FRAME_MAGIC);

    // the compressed manifest still decodes through the typed reader
    let decoded = repo.asset_manager().fetch_manifest_unknown_size(&manifest).await?;
    assert_eq!(decoded.chunk_payloads().count(), 2);
    assert_eq!(&decoded.id, &manifest);
    assert!(manifest_ids_of(&repo, &snap).await.contains(&manifest));
    Ok(())
}

#[tokio::test]
async fn hdr_compression_zstd__transaction_logs_are_zstd_compressed() -> R {
    use icechunk::format::format_constants::CompressionAlgorithmBin;
    def_repo_with_all_three_metadata_kinds!();
    let (storage, settings, repo, snap, _manifest) =
        repo_with_all_three_metadata_kinds().await;

    let (header, payload) =
        header_and_payload_start(storage.fetch_transaction_log(&settings, &snap).await?)
            .await;
    assert_eq!(header[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(payload, ZSTD_FRAME_MAGIC);

    let log = repo.asset_manager().fetch_transaction_log(&snap).await?;
    assert!(!log.is_empty());
    Ok(())
}

#[tokio::test]
async fn hdr_compression_zstd__compression_is_configurable_and_still_zstd() -> R {
    use icechunk::config::CompressionAlgorithm;
    use icechunk::config::CompressionConfig;
    use icechunk::format::format_constants::CompressionAlgorithmBin;
    def_manifest_ids_of!();
    let storage = mem_storage();
    let settings = storage.default_settings();

    // the built-in default
    assert_eq!(RepositoryConfig::default().compression().level(), 1);
    assert_eq!(
        RepositoryConfig::default().compression().algorithm(),
        CompressionAlgorithm::Zstd
    );

    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            compression: Some(CompressionConfig {
                algorithm: Some(CompressionAlgorithm::Zstd),
                level: Some(3),
            }),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    assert_eq!(repo.config().compression().level(), 3);
    assert_eq!(repo.config().compression().algorithm(), CompressionAlgorithm::Zstd);

    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    let writer = session.get_chunk_writer();
    let payload = writer(Bytes::copy_from_slice(b"0123456789abcdef")).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    let snap = session.commit("compressed", None).await?;

    let manifest = manifest_ids_of(&repo, &snap).await.pop().expect("one manifest");
    let (header, body) = header_and_payload_start(
        storage.fetch_manifest_unknown_size(&settings, &manifest).await?,
    )
    .await;
    assert_eq!(header[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(body, ZSTD_FRAME_MAGIC);
    assert_eq!(repo.asset_manager().fetch_manifest_unknown_size(&manifest).await?.chunk_payloads().count(), 1);
    Ok(())
}

// ===========================================================================
// hdr_unheadered_objects — what does NOT carry the binary header
// (design/004-binary-format.md, "## Chunk files" and "## Metadata files")
// ===========================================================================

#[tokio::test]
async fn hdr_unheadered_objects__chunk_objects_are_written_verbatim() -> R {
    use icechunk::format::format_constants::ICECHUNK_FORMAT_MAGIC_BYTES;
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, snap) =
        repo_with_materialised_chunks(&storage, &[[0, 0]], "first").await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let payload = session.get_chunk_ref(&array, &ChunkIndices(vec![0, 0])).await?;
    let chunk_ref = match payload {
        Some(ChunkPayload::Ref(r)) => r,
        other => panic!("expected a materialised chunk reference, got {other:?}"),
    };

    let raw = storage
        .fetch_chunk(&settings, &chunk_ref.id, &(0..chunk_ref.length))
        .await?;
    assert_eq!(raw, chunk_bytes(&[0, 0], 0));
    assert!(!raw.starts_with(ICECHUNK_FORMAT_MAGIC_BYTES));
    assert_eq!(raw.len() as u64, chunk_ref.length);
    Ok(())
}

#[tokio::test]
async fn hdr_unheadered_objects__reference_files_are_plain_json() -> R {
    use icechunk::format::format_constants::ICECHUNK_FORMAT_MAGIC_BYTES;
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let snap = commit_group(&repo, "main", "only").await;
    repo.create_tag("v1", &snap).await?;

    for key in ["branch.main/ZZZZZZZZ.json", "tag.v1/ref.json"] {
        let bytes = storage.get_ref(&settings, key).await?;
        assert!(!bytes.starts_with(ICECHUNK_FORMAT_MAGIC_BYTES), "{key} looks binary");
        let value: serde_json::Value = serde_json::from_slice(&bytes)?;
        let pointer = value
            .as_object()
            .and_then(|o| o.get("snapshot"))
            .and_then(|s| s.as_str())
            .unwrap_or_else(|| panic!("{key} has no snapshot pointer: {value}"));
        assert!(!pointer.is_empty());
    }
    // the tag file points at the snapshot the tag was created from
    assert_eq!(repo.lookup_tag("v1").await?, snap);
    Ok(())
}

#[tokio::test]
async fn hdr_unheadered_objects__the_config_file_is_not_a_binary_metadata_file() -> R {
    use icechunk::format::format_constants::ICECHUNK_FORMAT_MAGIC_BYTES;
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(77),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    repo.save_config().await?;

    let (bytes, _etag) =
        storage.fetch_config(&settings).await?.expect("a persisted config");
    assert!(!bytes.starts_with(ICECHUNK_FORMAT_MAGIC_BYTES));
    // plain text, not a compressed binary payload: valid UTF-8 with no NULs
    let text = std::str::from_utf8(&bytes)?;
    assert!(!text.is_empty());
    assert!(!text.contains('\0'));
    assert_ne!(&bytes[..1], &ZSTD_FRAME_MAGIC[..1]);

    // and those exact bytes are what the typed reader parses back
    let (persisted, _etag) =
        Repository::fetch_config(storage.as_ref()).await?.expect("a persisted config");
    assert_eq!(persisted.inline_chunk_threshold_bytes(), 77);
    assert_eq!(&persisted, repo.config());
    Ok(())
}

// ===========================================================================
// expire_squash_history — design/007-basic-expiration.md, "Update A in place"
// ===========================================================================

#[tokio::test]
async fn expire_squash_history__branch_history_is_squashed() -> R {
    use icechunk::ops::gc::expire_ref;
    def_branch_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        branch_messages(&reopened, "main").await,
        ["14", "13", "12", "Repository initialized"]
    );
    Ok(())
}

#[tokio::test]
async fn expire_squash_history__released_snapshots_are_reported() -> R {
    use icechunk::ops::gc::expire_ref;
    def_ancestry_of!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    let before: Vec<SnapshotId> =
        ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string()))
            .await
            .into_iter()
            .map(|i| i.id)
            .collect();

    let released = expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;

    // "5", "4", "2" and "1" leave main's history; the root does not
    assert_eq!(released.len(), 4);
    for id in &released {
        assert!(before.contains(id), "released an id that was never in the history");
    }
    Ok(())
}

#[tokio::test]
async fn expire_squash_history__edited_snapshot_keeps_its_id_and_reparents_to_the_root() -> R
{
    use icechunk::ops::gc::expire_ref;
    def_ancestry_of!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    let before = ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string())).await;
    let commit_12 = before.iter().find(|i| i.message == "12").expect("commit 12");
    let oldest_kept = commit_12.id.clone();
    let oldest_kept_flushed_at = commit_12.flushed_at;
    let root = before.last().expect("a root").id.clone();
    assert_eq!(before.last().unwrap().message, "Repository initialized");

    expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let after = ancestry_of(&reopened, VersionInfo::BranchTipRef("main".to_string())).await;
    let edited = after.iter().find(|i| i.message == "12").expect("commit 12 survived");
    // same SnapshotId, same object path — updated in place
    assert_eq!(edited.id, oldest_kept);
    assert_eq!(edited.parent_id.as_ref(), Some(&root));
    // an in-place rewrite keeps the snapshot's ORIGINAL timestamp; it is not
    // restamped with the moment expiration ran
    assert_eq!(edited.flushed_at, oldest_kept_flushed_at);
    assert_eq!(after.last().expect("a root").id, root);
    Ok(())
}

#[tokio::test]
async fn expire_squash_history__ancestry_stays_well_formed() -> R {
    use icechunk::ops::gc::expire_ref;
    def_ancestry_of!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let after = ancestry_of(&reopened, VersionInfo::BranchTipRef("main".to_string())).await;
    assert!(after.len() >= 2);
    for pair in after.windows(2) {
        assert_eq!(pair[0].parent_id.as_ref(), Some(&pair[1].id));
    }
    assert!(after.last().expect("a root").parent_id.is_none());
    let unique: HashSet<&SnapshotId> = after.iter().map(|i| &i.id).collect();
    assert_eq!(unique.len(), after.len());
    Ok(())
}

#[tokio::test]
async fn expire_squash_history__other_refs_are_untouched() -> R {
    use icechunk::ops::gc::expire_ref;
    def_branch_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        branch_messages(&reopened, "develop").await,
        ["11", "10", "6", "3", "2", "1", "Repository initialized"]
    );
    assert_eq!(
        branch_messages(&reopened, "test").await,
        ["9", "7", "6", "3", "2", "1", "Repository initialized"]
    );
    assert_eq!(
        branch_messages(&reopened, "qa").await,
        ["8", "7", "6", "3", "2", "1", "Repository initialized"]
    );
    Ok(())
}

// ===========================================================================
// expire_boundary — the operations expiration must refuse or skip
// (design/007-basic-expiration.md, "## Forbidden Expiration Operations")
// ===========================================================================

#[tokio::test]
async fn expire_boundary__a_ref_already_at_an_expired_snapshot_is_a_noop() -> R {
    use icechunk::ops::gc::expire_ref;
    def_ancestry_of!();
    def_branch_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    make_design_doc_repo(&repo).await?;

    let tip = ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string()))
        .await
        .first()
        .expect("a tip")
        .clone();
    // a threshold strictly newer than the tip: every snapshot on main is "old"
    let cutoff = tip.flushed_at + TimeDelta::try_seconds(1).expect("delta");

    let before = branch_messages(&repo, "main").await;
    let released = expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;
    assert!(released.is_empty());

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(branch_messages(&reopened, "main").await, before);
    Ok(())
}

#[tokio::test]
async fn expire_boundary__a_ref_pointing_at_the_root_is_a_noop() -> R {
    use icechunk::ops::gc::expire_ref;
    def_ancestry_of!();
    def_branch_messages!();
    def_no_cache_assets!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let root = ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string()))
        .await
        .first()
        .expect("the root")
        .clone();
    assert_eq!(root.message, "Repository initialized");

    let released = expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        root.flushed_at,
    )
    .await?;
    assert!(released.is_empty());
    assert_eq!(repo.lookup_branch("main").await?, root.id);
    assert_eq!(branch_messages(&repo, "main").await, ["Repository initialized"]);
    Ok(())
}

#[tokio::test]
async fn expire_boundary__every_ref_still_points_where_it_did() -> R {
    use icechunk::ops::gc::expire;
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    let mut branch_tips = Vec::new();
    for name in ["main", "develop", "test", "qa"] {
        branch_tips.push((name, repo.lookup_branch(name).await?));
    }
    let mut tag_targets = Vec::new();
    for name in ["tag1", "tag2"] {
        tag_targets.push((name, repo.lookup_tag(name).await?));
    }

    let released = expire(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        cutoff,
    )
    .await?;

    // a snapshot a branch points at directly is never skipped from history
    for (name, tip) in &branch_tips {
        assert!(!released.contains(tip), "branch {name} lost its tip to expiration");
    }
    // and every reference still resolves to exactly the snapshot it did before
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    for (name, tip) in &branch_tips {
        assert_eq!(&reopened.lookup_branch(name).await?, tip);
    }
    for (name, target) in &tag_targets {
        assert_eq!(&reopened.lookup_tag(name).await?, target);
        // and the data behind it is still readable
        assert!(reopened
            .readonly_session(&VersionInfo::TagRef(name.to_string()))
            .await
            .is_ok());
    }
    Ok(())
}

#[tokio::test]
async fn expire_boundary__the_root_snapshot_is_never_released() -> R {
    use icechunk::ops::gc::expire_ref;
    def_ancestry_of!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    let root = ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string()))
        .await
        .last()
        .expect("a root")
        .id
        .clone();

    let released = expire_ref(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?;
    assert!(!released.contains(&root));

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let after = ancestry_of(&reopened, VersionInfo::BranchTipRef("main".to_string())).await;
    assert_eq!(after.last().expect("a root").id, root);
    Ok(())
}

// ===========================================================================
// expire_all_refs — `expire` over every branch and tag
// (design/007-basic-expiration.md, "## Using this algorithm ...")
// ===========================================================================

#[tokio::test]
async fn expire_all_refs__every_ref_is_processed() -> R {
    use icechunk::ops::gc::expire;
    def_branch_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    expire(storage.as_ref(), &settings, no_cache_assets(&storage, &settings), cutoff)
        .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        branch_messages(&reopened, "main").await,
        ["14", "13", "12", "Repository initialized"]
    );
    assert_eq!(
        branch_messages(&reopened, "develop").await,
        ["11", "10", "Repository initialized"]
    );
    assert_eq!(branch_messages(&reopened, "test").await, ["9", "Repository initialized"]);
    assert_eq!(branch_messages(&reopened, "qa").await, ["8", "Repository initialized"]);
    Ok(())
}

#[tokio::test]
async fn expire_all_refs__the_released_set_is_reported() -> R {
    use icechunk::ops::gc::expire;
    def_tag_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    let released: HashSet<SnapshotId> =
        expire(storage.as_ref(), &settings, no_cache_assets(&storage, &settings), cutoff)
            .await?;
    // "1".."7" leave every history; the ids are reported once each
    assert_eq!(released.len(), 7);

    // The reported ids are the ones "skipped from history"; they are NOT
    // necessarily collectable, because another reference may still point at
    // them. tag1's snapshot is skipped from develop, test and qa, yet the tag
    // still resolves to it and still exposes its whole old ancestry.
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let tag1 = reopened.lookup_tag("tag1").await?;
    assert!(released.contains(&tag1), "commit 3 left every branch history");
    assert_eq!(
        tag_messages(&reopened, "tag1").await,
        ["3", "2", "1", "Repository initialized"]
    );
    Ok(())
}

#[tokio::test]
async fn expire_all_refs__the_end_state_does_not_depend_on_ref_order() -> R {
    use icechunk::ops::gc::expire_ref;
    use icechunk::ops::gc::expire;
    def_branch_messages!();
    def_tag_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    // one repository expired in a single `expire` call ...
    let storage_a = mem_storage();
    let settings_a = storage_a.default_settings();
    let repo_a = Repository::create(None, Arc::clone(&storage_a), HashMap::new()).await?;
    let cutoff_a = make_design_doc_repo(&repo_a).await?;
    expire(
        storage_a.as_ref(),
        &settings_a,
        no_cache_assets(&storage_a, &settings_a),
        cutoff_a,
    )
    .await?;

    // ... and an identical one expired ref by ref, in the reverse order
    let storage_b = mem_storage();
    let settings_b = storage_b.default_settings();
    let repo_b = Repository::create(None, Arc::clone(&storage_b), HashMap::new()).await?;
    let cutoff_b = make_design_doc_repo(&repo_b).await?;
    for name in ["qa", "test", "develop", "main"] {
        expire_ref(
            storage_b.as_ref(),
            &settings_b,
            no_cache_assets(&storage_b, &settings_b),
            &Ref::Branch(name.to_string()),
            cutoff_b,
        )
        .await?;
    }

    let a = Repository::open(None, Arc::clone(&storage_a), HashMap::new()).await?;
    let b = Repository::open(None, Arc::clone(&storage_b), HashMap::new()).await?;
    for name in ["main", "develop", "test", "qa"] {
        assert_eq!(
            branch_messages(&a, name).await,
            branch_messages(&b, name).await,
            "branch {name} ended in a different state"
        );
    }
    for name in ["tag1", "tag2"] {
        assert_eq!(tag_messages(&a, name).await, tag_messages(&b, name).await);
    }
    Ok(())
}

#[tokio::test]
async fn expire_all_refs__expiration_deletes_no_object() -> R {
    use icechunk::ops::gc::expire;
    def_no_cache_assets!();
    def_count_objects!();
    def_snapshot_ids_in_store!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;

    let before = count_objects(&storage, &settings).await;
    let ids_before = snapshot_ids_in_store(&storage, &settings).await;

    expire(storage.as_ref(), &settings, no_cache_assets(&storage, &settings), cutoff)
        .await?;

    let after = count_objects(&storage, &settings).await;
    assert_eq!(before, after, "expiration removed objects");
    assert_eq!(ids_before, snapshot_ids_in_store(&storage, &settings).await);
    Ok(())
}

#[tokio::test]
async fn expire_all_refs__tags_keep_resolving_with_their_old_ancestry() -> R {
    use icechunk::ops::gc::expire;
    def_tag_messages!();
    def_no_cache_assets!();
    def_make_design_doc_repo!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let cutoff = make_design_doc_repo(&repo).await?;
    let tag1_before = repo.lookup_tag("tag1").await?;
    let tag2_before = repo.lookup_tag("tag2").await?;

    expire(storage.as_ref(), &settings, no_cache_assets(&storage, &settings), cutoff)
        .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(reopened.lookup_tag("tag1").await?, tag1_before);
    assert_eq!(reopened.lookup_tag("tag2").await?, tag2_before);
    assert_eq!(
        tag_messages(&reopened, "tag1").await,
        ["3", "2", "1", "Repository initialized"]
    );
    assert_eq!(
        tag_messages(&reopened, "tag2").await,
        ["5", "4", "2", "1", "Repository initialized"]
    );
    Ok(())
}

#[tokio::test]
async fn expire_all_refs__a_tag_is_expired_even_when_no_branch_reaches_it() -> R {
    use icechunk::ops::gc::expire;
    def_ancestry_of!();
    def_branch_messages!();
    def_tag_messages!();
    def_no_cache_assets!();
    // `expire` walks EVERY reference, tags included. Here the only reference
    // that reaches `c` and `d` is a tag, so an implementation that iterated
    // branches alone would leave the tag's history untouched.
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    commit_group(&repo, "main", "a").await;
    let b = commit_group(&repo, "main", "b").await;
    repo.create_branch("side", &b).await?;
    commit_group(&repo, "side", "c").await;
    let d = commit_group(&repo, "side", "d").await;
    repo.create_tag("rel", &d).await?;
    // the branch goes away, so `rel` is the only way back to "d" and "c"
    repo.delete_branch("side").await?;
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), ["main"]);

    let e = commit_group(&repo, "main", "e").await;
    commit_group(&repo, "main", "f").await;

    // the cutoff is "d"'s own timestamp: d, e and f are kept, a, b and c go
    let cutoff = ancestry_of(&repo, VersionInfo::SnapshotId(d.clone()))
        .await
        .first()
        .expect("d is in its own ancestry")
        .flushed_at;
    let c_id = ancestry_of(&repo, VersionInfo::TagRef("rel".to_string()))
        .await
        .into_iter()
        .find(|i| i.message == "c")
        .expect("commit c")
        .id;
    assert_eq!(
        tag_messages(&repo, "rel").await,
        ["d", "c", "b", "a", "Repository initialized"]
    );

    let released =
        expire(storage.as_ref(), &settings, no_cache_assets(&storage, &settings), cutoff)
            .await?;

    // "c" is reachable from no branch at all, so only a tag walk reports it
    assert!(released.contains(&c_id), "the tag's own ancestry was never expired");

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(reopened.lookup_tag("rel").await?, d);
    assert_eq!(tag_messages(&reopened, "rel").await, ["d", "Repository initialized"]);
    // and the branch that shares "a" and "b" was squashed the same way
    assert_eq!(
        branch_messages(&reopened, "main").await,
        ["f", "e", "Repository initialized"]
    );
    let after = ancestry_of(&reopened, VersionInfo::BranchTipRef("main".to_string())).await;
    let rewritten = after.iter().find(|i| i.id == e).expect("commit e survived");
    assert_eq!(rewritten.parent_id.as_ref(), Some(&after.last().expect("a root").id));
    Ok(())
}

// ===========================================================================
// gc_summary_counts — what garbage collection reports
// (design/007-basic-expiration.md; GCSummary's five counts)
// ===========================================================================

#[tokio::test]
async fn gc_summary_counts__nothing_dangling_is_an_empty_summary() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, _array, _snap) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1], [2, 2]], "first").await?;
    let _ = &repo;

    let before = count_objects(&storage, &settings).await;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;

    assert_eq!(summary, GCSummary::default());
    assert_eq!(summary.chunks_deleted, 0);
    assert_eq!(summary.manifests_deleted, 0);
    assert_eq!(summary.snapshots_deleted, 0);
    assert_eq!(summary.attributes_deleted, 0);
    assert_eq!(summary.transaction_logs_deleted, 0);
    assert_eq!(count_objects(&storage, &settings).await, before);
    Ok(())
}

#[tokio::test]
async fn gc_summary_counts__one_count_per_category() -> R {
    def_no_cache_assets!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, first) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1], [2, 2]], "first").await?;

    // a second commit that overwrites two chunks, then forget it
    let mut session = repo.writable_session("main").await?;
    for c in [[0u32, 0u32], [1, 1]] {
        let writer = session.get_chunk_writer();
        let payload = writer(chunk_bytes(&c, 1)).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(c.to_vec()), Some(payload)).await?;
    }
    session.commit("second", None).await?;
    repo.reset_branch("main", &first).await?;

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;

    assert_eq!(summary.snapshots_deleted, 1);
    assert_eq!(summary.manifests_deleted, 1);
    assert_eq!(summary.chunks_deleted, 2);
    assert_eq!(summary.transaction_logs_deleted, 1);
    // attribute files are not produced at this release, so their count stays 0
    assert_eq!(summary.attributes_deleted, 0);
    Ok(())
}

#[tokio::test]
async fn gc_summary_counts__keep_actions_delete_nothing() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, first) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1]], "first").await?;
    let mut session = repo.writable_session("main").await?;
    let writer = session.get_chunk_writer();
    let payload = writer(chunk_bytes(&[0, 0], 1)).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    session.commit("second", None).await?;
    repo.reset_branch("main", &first).await?;

    let keep_everything = GCConfig::new(
        HashSet::new(),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
    );
    assert!(!keep_everything.deletes_chunks());
    assert!(!keep_everything.deletes_manifests());
    assert!(!keep_everything.deletes_attributes());
    assert!(!keep_everything.deletes_transaction_logs());
    assert!(!keep_everything.deletes_snapshots());

    let before = count_objects(&storage, &settings).await;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &keep_everything,
    )
    .await?;
    assert_eq!(summary, GCSummary::default());
    assert_eq!(count_objects(&storage, &settings).await, before);
    Ok(())
}

#[tokio::test]
async fn gc_summary_counts__each_category_is_selected_independently() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, first) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1]], "first").await?;
    let mut session = repo.writable_session("main").await?;
    let writer = session.get_chunk_writer();
    let payload = writer(chunk_bytes(&[0, 0], 1)).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    session.commit("second", None).await?;
    repo.reset_branch("main", &first).await?;

    let snapshots_only = GCConfig::new(
        HashSet::new(),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::DeleteIfCreatedBefore(Utc::now()),
    );
    assert!(snapshots_only.deletes_snapshots());
    assert!(!snapshots_only.deletes_chunks());
    assert!(!snapshots_only.deletes_manifests());
    assert!(!snapshots_only.deletes_transaction_logs());

    let (snaps_before, manifests_before, chunks_before, tx_before) =
        count_objects(&storage, &settings).await;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &snapshots_only,
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1);
    assert_eq!(summary.chunks_deleted, 0);
    assert_eq!(summary.manifests_deleted, 0);
    assert_eq!(summary.transaction_logs_deleted, 0);

    let (snaps_after, manifests_after, chunks_after, tx_after) =
        count_objects(&storage, &settings).await;
    assert_eq!(snaps_after, snaps_before - 1);
    assert_eq!(manifests_after, manifests_before);
    assert_eq!(chunks_after, chunks_before);
    assert_eq!(tx_after, tx_before);
    Ok(())
}

// ===========================================================================
// gc_keeps_reachable — GC never deletes an object a live ref can reach
// ===========================================================================

#[tokio::test]
async fn gc_keeps_reachable__objects_reachable_from_a_branch_survive() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, _array, _snap) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1], [2, 2]], "first").await?;
    let _ = &repo;

    let before = count_objects(&storage, &settings).await;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary, GCSummary::default());
    assert_eq!(count_objects(&storage, &settings).await, before);
    Ok(())
}

#[tokio::test]
async fn gc_keeps_reachable__a_tag_keeps_its_snapshot_alive() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, first) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1]], "first").await?;

    let mut session = repo.writable_session("main").await?;
    let writer = session.get_chunk_writer();
    let payload = writer(chunk_bytes(&[0, 0], 1)).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    let second = session.commit("second", None).await?;
    // the tag is the only thing that will still reach `second`
    repo.create_tag("keeper", &second).await?;
    repo.reset_branch("main", &first).await?;

    let before = count_objects(&storage, &settings).await;
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary, GCSummary::default());
    assert_eq!(count_objects(&storage, &settings).await, before);
    assert_eq!(repo.lookup_tag("keeper").await?, second);
    Ok(())
}

#[tokio::test]
async fn gc_keeps_reachable__collection_is_idempotent() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, first) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1]], "first").await?;
    let mut session = repo.writable_session("main").await?;
    let writer = session.get_chunk_writer();
    let payload = writer(chunk_bytes(&[0, 0], 1)).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    session.commit("second", None).await?;
    repo.reset_branch("main", &first).await?;

    let config = GCConfig::clean_all(Utc::now(), Utc::now(), None);
    let first_pass = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &config,
    )
    .await?;
    assert_ne!(first_pass, GCSummary::default());

    let after_first = count_objects(&storage, &settings).await;
    let second_pass = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &config,
    )
    .await?;
    assert_eq!(second_pass, GCSummary::default());
    assert_eq!(count_objects(&storage, &settings).await, after_first);
    Ok(())
}

#[tokio::test]
async fn gc_keeps_reachable__surviving_data_is_still_readable() -> R {
    def_no_cache_assets!();
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, first) =
        repo_with_materialised_chunks(&storage, &[[0, 0], [1, 1]], "first").await?;
    let mut session = repo.writable_session("main").await?;
    let writer = session.get_chunk_writer();
    let payload = writer(chunk_bytes(&[0, 0], 1)).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    session.commit("second", None).await?;
    repo.reset_branch("main", &first).await?;

    garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let session = reopened
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    for c in [[0u32, 0u32], [1, 1]] {
        let reader = session
            .get_chunk_reader(&array, &ChunkIndices(c.to_vec()), &ByteRange::ALL)
            .await?;
        assert_eq!(
            get_chunk(reader).await?,
            Some(chunk_bytes(&c, 0)),
            "chunk {c:?} did not survive collection"
        );
    }
    Ok(())
}

#[tokio::test]
async fn gc_keeps_reachable__an_extra_root_keeps_an_unreferenced_snapshot_alive() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_snapshot_ids_in_store!();
    def_repo_with_forgotten_commit!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (_repo, _array, _first, second) = repo_with_forgotten_commit(&storage).await?;
    // no branch and no tag reaches `second` — the sibling group shows that GC
    // collects it — but naming it as an extra root makes it reachable again
    let before = count_objects(&storage, &settings).await;

    let mut roots = HashSet::new();
    roots.insert(second.clone());
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), Some(roots)),
    )
    .await?;
    assert_eq!(summary, GCSummary::default());
    assert_eq!(count_objects(&storage, &settings).await, before);
    assert!(snapshot_ids_in_store(&storage, &settings).await.contains(&second));

    // the six-argument constructor takes the same set, and reports the same
    let mut roots = HashSet::new();
    roots.insert(second.clone());
    let config = GCConfig::new(
        roots,
        Action::DeleteIfCreatedBefore(Utc::now()),
        Action::DeleteIfCreatedBefore(Utc::now()),
        Action::DeleteIfCreatedBefore(Utc::now()),
        Action::DeleteIfCreatedBefore(Utc::now()),
        Action::DeleteIfCreatedBefore(Utc::now()),
    );
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &config,
    )
    .await?;
    assert_eq!(summary, GCSummary::default());
    assert!(snapshot_ids_in_store(&storage, &settings).await.contains(&second));
    Ok(())
}

// ===========================================================================
// gc_collects_unreachable — GC frees exactly what no ref can reach
// ===========================================================================

#[tokio::test]
async fn gc_collects_unreachable__an_unreachable_snapshot_is_deleted() -> R {
    def_no_cache_assets!();
    def_snapshot_ids_in_store!();
    def_repo_with_forgotten_commit!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (_repo, _array, _first, second) = repo_with_forgotten_commit(&storage).await?;

    assert!(snapshot_ids_in_store(&storage, &settings).await.contains(&second));
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1);
    assert!(!snapshot_ids_in_store(&storage, &settings).await.contains(&second));
    Ok(())
}

#[tokio::test]
async fn gc_collects_unreachable__an_exclusive_manifest_is_deleted() -> R {
    def_no_cache_assets!();
    def_manifest_ids_of!();
    def_repo_with_forgotten_commit!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, _array, first, second) = repo_with_forgotten_commit(&storage).await?;

    let kept = manifest_ids_of(&repo, &first).await;
    let dropped = manifest_ids_of(&repo, &second).await;
    assert_eq!(kept.len(), 1);
    assert_eq!(dropped.len(), 1);
    assert_ne!(kept, dropped);

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary.manifests_deleted, 1);

    let surviving: HashSet<ManifestId> = storage
        .list_manifests(&settings)
        .await?
        .try_collect::<Vec<_>>()
        .await?
        .into_iter()
        .map(|info| info.id)
        .collect();
    assert!(surviving.contains(&kept[0]));
    assert!(!surviving.contains(&dropped[0]));
    Ok(())
}

#[tokio::test]
async fn gc_collects_unreachable__overwritten_chunks_are_deleted() -> R {
    def_no_cache_assets!();
    def_count_objects!();
    def_repo_with_forgotten_commit!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (_repo, _array, _first, _second) = repo_with_forgotten_commit(&storage).await?;

    let (_s, _m, chunks_before, _t) = count_objects(&storage, &settings).await;
    assert_eq!(chunks_before, 6); // 4 from the first commit, 2 overwrites

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary.chunks_deleted, 2);

    let (_s, _m, chunks_after, _t) = count_objects(&storage, &settings).await;
    assert_eq!(chunks_after, 4);
    Ok(())
}

#[tokio::test]
async fn gc_collects_unreachable__exactly_the_unreachable_objects_are_removed() -> R {
    def_ancestry_of!();
    def_branch_messages!();
    def_no_cache_assets!();
    def_snapshot_ids_in_store!();
    def_repo_with_forgotten_commit!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, _array, first, second) = repo_with_forgotten_commit(&storage).await?;

    let reachable: HashSet<SnapshotId> =
        ancestry_of(&repo, VersionInfo::SnapshotId(first.clone()))
            .await
            .into_iter()
            .map(|i| i.id)
            .collect();
    assert!(!reachable.contains(&second));

    garbage_collect(
        storage.as_ref(),
        &settings,
        no_cache_assets(&storage, &settings),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;

    assert_eq!(snapshot_ids_in_store(&storage, &settings).await, reachable);
    // and the repository still opens and reads at its tip
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(reopened.lookup_branch("main").await?, first);
    assert_eq!(branch_messages(&reopened, "main").await, ["first", "Repository initialized"]);
    Ok(())
}

// ===========================================================================
// tag_delete_and_listing — reference deletion: tags by tombstone, and the one
// branch that can never be deleted
// (design/006-tag-delete.md, "## Design"; TEST_INTERFACE.md 7 and 8)
// ===========================================================================

/// The backing store is handed back as the concrete `ObjectStorage` so that a
/// case can look at the reference keys directly through `all_keys()`.
async fn repo_with_two_snapshots(
) -> Result<(Arc<ObjectStorage>, Repository, SnapshotId, SnapshotId), Box<dyn std::error::Error>>
{
    let store = Arc::new(ObjectStorage::new_in_memory()?);
    let storage: Arc<dyn Storage + Send + Sync> = Arc::clone(&store) as _;
    let repo = Repository::create(None, storage, HashMap::new()).await?;
    let a = commit_group(&repo, "main", "a").await;
    let b = commit_group(&repo, "main", "b").await;
    Ok((store, repo, a, b))
}

#[tokio::test]
async fn tag_delete_and_listing__a_deleted_tag_disappears_from_the_listing() -> R {
    let (store, repo, a, _b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), ["tag1"]);
    let keys = store.all_keys().await?;
    assert!(keys.iter().any(|k| k.ends_with("refs/tag.tag1/ref.json")));
    assert!(!keys.iter().any(|k| k.ends_with("refs/tag.tag1/ref.json.deleted")));

    repo.delete_tag("tag1").await?;
    assert!(repo.list_tags().await?.is_empty());

    // the delete is LOGICAL: a tombstone appears beside the live reference and
    // the live reference itself is left exactly where it was
    let keys = store.all_keys().await?;
    assert!(
        keys.iter().any(|k| k.ends_with("refs/tag.tag1/ref.json.deleted")),
        "no tombstone was written: {keys:?}"
    );
    assert!(
        keys.iter().any(|k| k.ends_with("refs/tag.tag1/ref.json")),
        "the live reference was removed instead of tombstoned: {keys:?}"
    );
    Ok(())
}

#[tokio::test]
async fn tag_delete_and_listing__a_branch_is_deletable_but_main_never_is() -> R {
    let (store, repo, a, b) = repo_with_two_snapshots().await?;
    let settings = store.default_settings();
    repo.create_branch("side", &a).await?;
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        ["main", "side"]
    );

    repo.delete_branch("side").await?;
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), ["main"]);
    assert!(repo.lookup_branch("side").await.is_err());

    // the default-branch constant, the two `Ref` predicates, `Ref::fetch` and
    // the derived ordering (TEST_INTERFACE.md 7)
    assert_eq!(Ref::DEFAULT_BRANCH, "main");
    let main_ref = Ref::Branch(Ref::DEFAULT_BRANCH.to_string());
    assert!(main_ref.is_branch() && !main_ref.is_tag());
    let tag_ref = Ref::Tag("v1".to_string());
    assert!(tag_ref.is_tag() && !tag_ref.is_branch());
    // `fetch` resolves a reference to the snapshot it points at
    assert_eq!(main_ref.fetch(store.as_ref(), &settings).await?.snapshot, b);
    // `Ord` is derived, so two references of the same kind sort by name
    assert!(Ref::Branch("a".to_string()) < Ref::Branch("b".to_string()));
    assert!(Ref::Tag("a".to_string()) < Ref::Tag("b".to_string()));

    let err = repo
        .delete_branch(Ref::DEFAULT_BRANCH)
        .await
        .expect_err("the default branch cannot be deleted");
    assert!(
        matches!(err, RepositoryError::CannotDeleteMain),
        "unexpected error: {err:?}"
    );
    // the refusal changed nothing
    assert_eq!(repo.lookup_branch("main").await?, b);
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), ["main"]);
    Ok(())
}

#[tokio::test]
async fn tag_delete_and_listing__a_deleted_tag_no_longer_resolves() -> R {
    let (_storage, repo, a, _b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    assert_eq!(repo.lookup_tag("tag1").await?, a);

    repo.delete_tag("tag1").await?;
    let err = repo.lookup_tag("tag1").await.expect_err("a deleted tag must not resolve");
    assert!(
        matches!(err, RepositoryError::Ref(RefError::RefNotFound(_))),
        "unexpected error: {err:?}"
    );
    // and a session cannot be opened at it either
    assert!(repo
        .readonly_session(&VersionInfo::TagRef("tag1".to_string()))
        .await
        .is_err());
    Ok(())
}

#[tokio::test]
async fn tag_delete_and_listing__deleting_the_same_tag_twice_fails() -> R {
    let (_storage, repo, a, _b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    repo.delete_tag("tag1").await?;
    assert!(repo.delete_tag("tag1").await.is_err());
    assert!(repo.list_tags().await?.is_empty());
    Ok(())
}

#[tokio::test]
async fn tag_delete_and_listing__deleting_an_unknown_tag_fails() -> R {
    let (_storage, repo, a, _b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    assert!(repo.delete_tag("never-existed").await.is_err());
    // the failure changed nothing
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), ["tag1"]);
    assert_eq!(repo.lookup_tag("tag1").await?, a);
    Ok(())
}

#[tokio::test]
async fn tag_delete_and_listing__other_tags_are_unaffected() -> R {
    let (_storage, repo, a, b) = repo_with_two_snapshots().await?;
    repo.create_tag("one", &a).await?;
    repo.create_tag("two", &a).await?;
    repo.create_tag("three", &b).await?;

    repo.delete_tag("two").await?;
    assert_eq!(
        repo.list_tags().await?.into_iter().collect::<Vec<_>>(),
        ["one", "three"]
    );
    assert_eq!(repo.lookup_tag("one").await?, a);
    assert_eq!(repo.lookup_tag("three").await?, b);
    Ok(())
}

// ===========================================================================
// tag_name_never_reused — design/006-tag-delete.md; guide version-control "## Tags"
// ===========================================================================

#[tokio::test]
async fn tag_name_never_reused__recreating_at_the_same_snapshot_fails() -> R {
    let (_storage, repo, a, _b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    repo.delete_tag("tag1").await?;

    let err = repo
        .create_tag("tag1", &a)
        .await
        .expect_err("a deleted tag name can never be reused");
    assert!(
        matches!(&err, RepositoryError::Ref(RefError::TagAlreadyExists(name)) if name == "tag1"),
        "unexpected error: {err:?}"
    );
    assert!(repo.list_tags().await?.is_empty());
    Ok(())
}

#[tokio::test]
async fn tag_name_never_reused__recreating_at_a_different_snapshot_fails() -> R {
    let (_storage, repo, a, b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    repo.delete_tag("tag1").await?;

    let err = repo
        .create_tag("tag1", &b)
        .await
        .expect_err("the name is burned regardless of the snapshot");
    assert!(
        matches!(&err, RepositoryError::Ref(RefError::TagAlreadyExists(name)) if name == "tag1"),
        "unexpected error: {err:?}"
    );
    assert!(repo.lookup_tag("tag1").await.is_err());
    Ok(())
}

#[tokio::test]
async fn tag_name_never_reused__an_unused_name_is_still_available() -> R {
    let (_storage, repo, a, b) = repo_with_two_snapshots().await?;
    repo.create_tag("tag1", &a).await?;
    repo.delete_tag("tag1").await?;

    repo.create_tag("tag2", &b).await?;
    assert_eq!(repo.lookup_tag("tag2").await?, b);
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), ["tag2"]);
    // an existing, never-deleted tag is still immutable
    assert!(repo.create_tag("tag2", &a).await.is_err());
    Ok(())
}

// ===========================================================================
// repo_clean_prefix — creating a repository only in a clean prefix
// ===========================================================================

#[tokio::test]
async fn repo_clean_prefix__creating_twice_in_the_same_prefix_fails() -> R {
    let dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path())?;
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let err = Repository::create(None, Arc::clone(&storage), HashMap::new())
        .await
        .expect_err("the prefix is no longer clean");
    assert!(
        matches!(err, RepositoryError::ParentDirectoryNotClean),
        "unexpected error: {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn repo_clean_prefix__creating_under_a_populated_sub_prefix_fails() -> R {
    let dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path())?;
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let inner = dir.path().join("snapshots");
    let inner_storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&inner)?;
    let err = Repository::create(None, inner_storage, HashMap::new())
        .await
        .expect_err("the sub-prefix already holds objects");
    assert!(
        matches!(err, RepositoryError::ParentDirectoryNotClean),
        "unexpected error: {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn repo_clean_prefix__a_clean_prefix_and_an_existing_repository_both_work() -> R {
    let dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path())?;
    assert!(!Repository::exists(storage.as_ref()).await?);
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let snap = commit_group(&repo, "main", "only").await;

    assert!(Repository::exists(storage.as_ref()).await?);
    let opened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(opened.lookup_branch("main").await?, snap);
    let or_created =
        Repository::open_or_create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(or_created.lookup_branch("main").await?, snap);

    // a genuinely clean prefix still accepts a new repository
    let other = tempfile::tempdir()?;
    let other_storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(other.path())?;
    Repository::create(None, other_storage, HashMap::new()).await?;
    Ok(())
}

// ===========================================================================
// commit_metadata_ancestry — arbitrary commit properties, read through ancestry
// ===========================================================================

#[tokio::test]
async fn commit_metadata_ancestry__arbitrary_properties_round_trip() -> R {
    def_ancestry_of!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut props = SnapshotProperties::default();
    props.insert("author".to_string(), serde_json::Value::from("icechunk-team"));
    props.insert("build".to_string(), serde_json::Value::from(42));
    props.insert(
        "tags".to_string(),
        serde_json::Value::Array(vec![serde_json::Value::from("a")]),
    );

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    let snap = session.commit("some message", Some(props.clone())).await?;

    // read back through a cold instance, so the properties really came from disk
    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let history = ancestry_of(&reopened, VersionInfo::SnapshotId(snap.clone())).await;
    assert_eq!(history[0].id, snap);
    assert_eq!(history[0].message, "some message");
    assert_eq!(history[0].metadata, props);
    Ok(())
}

#[tokio::test]
async fn commit_metadata_ancestry__the_root_commit_carries_its_own_marker() -> R {
    def_ancestry_of!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let snap = commit_group(&repo, "main", "first").await;

    let history = ancestry_of(&repo, VersionInfo::SnapshotId(snap)).await;
    let root = history.last().expect("a root");
    assert_eq!(root.message, "Repository initialized");
    assert!(root.metadata.contains_key("__root"));
    assert!(root.parent_id.is_none());
    // an ordinary commit with no properties gets an empty map, not the marker
    assert!(history[0].metadata.is_empty());
    Ok(())
}

#[tokio::test]
async fn commit_metadata_ancestry__ancestry_resolves_branch_tag_and_snapshot_id() -> R {
    def_ancestry_of!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let first = commit_group(&repo, "main", "first").await;
    let second = commit_group(&repo, "main", "second").await;
    repo.create_tag("v1", &second).await?;

    let by_branch =
        ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string())).await;
    let by_tag = ancestry_of(&repo, VersionInfo::TagRef("v1".to_string())).await;
    let by_id = ancestry_of(&repo, VersionInfo::SnapshotId(second.clone())).await;
    assert_eq!(by_branch, by_tag);
    assert_eq!(by_branch, by_id);
    assert_eq!(
        by_branch.iter().map(|i| i.message.as_str()).collect::<Vec<_>>(),
        ["second", "first", "Repository initialized"]
    );
    assert_eq!(by_branch[1].id, first);

    // the by-id entry point is also reachable without a VersionInfo
    let direct = repo
        .snapshot_ancestry(&second)
        .await?
        .try_collect::<Vec<_>>()
        .await?;
    assert_eq!(direct, by_id);

    // and the same two walks from an `Arc<Repository>`, whose streams do not
    // borrow the repository (TEST_INTERFACE.md 8)
    let repo = Arc::new(repo);
    let by_arc = Arc::clone(&repo)
        .ancestry_arc(&VersionInfo::TagRef("v1".to_string()))
        .await?
        .try_collect::<Vec<_>>()
        .await?;
    assert_eq!(by_arc, by_tag);
    let by_arc_id =
        repo.snapshot_ancestry_arc(&second).await?.try_collect::<Vec<_>>().await?;
    assert_eq!(by_arc_id, by_id);
    Ok(())
}

#[tokio::test]
async fn commit_metadata_ancestry__entries_carry_id_parent_and_timestamp() -> R {
    def_ancestry_of!();
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    commit_group(&repo, "main", "first").await;
    commit_group(&repo, "main", "second").await;

    let history = ancestry_of(&repo, VersionInfo::BranchTipRef("main".to_string())).await;
    assert_eq!(history.len(), 3);
    for pair in history.windows(2) {
        assert_eq!(pair[0].parent_id.as_ref(), Some(&pair[1].id));
        // latest first, and timestamps are strictly monotonic down the chain
        assert!(
            pair[0].flushed_at > pair[1].flushed_at,
            "{:?} is not newer than {:?}",
            pair[0].flushed_at,
            pair[1].flushed_at
        );
    }
    assert!(history.last().expect("a root").parent_id.is_none());
    Ok(())
}

// ===========================================================================
// chunk_coordinates — Session::chunk_coordinates
// ===========================================================================

#[tokio::test]
async fn chunk_coordinates__lists_every_initialized_chunk() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 2, 4], vec![2, 1, 2])).await?;
    let written = [[0, 0, 0], [0, 0, 1], [1, 0, 0], [0, 1, 0]];
    for c in written {
        session
            .set_chunk_ref(
                array.clone(),
                ChunkIndices(c.to_vec()),
                Some(ChunkPayload::Inline("hello".into())),
            )
            .await?;
    }
    let snap = session.commit("write", None).await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let coords: HashSet<ChunkIndices> =
        session.chunk_coordinates(&array).await?.try_collect().await?;
    assert_eq!(coords.len(), 4);
    for c in written {
        assert!(coords.contains(&ChunkIndices(c.to_vec())), "missing {c:?}");
    }
    assert!(!coords.contains(&ChunkIndices(vec![1, 1, 1])));
    Ok(())
}

#[tokio::test]
async fn chunk_coordinates__uncommitted_chunks_are_visible_to_the_writer() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0, 0]),
            Some(ChunkPayload::Inline("a".into())),
        )
        .await?;
    session.commit("first", None).await?;

    let mut session = repo.writable_session("main").await?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![2, 3]),
            Some(ChunkPayload::Inline("b".into())),
        )
        .await?;
    let coords: HashSet<ChunkIndices> =
        session.chunk_coordinates(&array).await?.try_collect().await?;
    assert_eq!(coords.len(), 2);
    assert!(coords.contains(&ChunkIndices(vec![0, 0])));
    assert!(coords.contains(&ChunkIndices(vec![2, 3])));
    Ok(())
}

#[tokio::test]
async fn chunk_coordinates__an_array_with_no_chunks_yields_nothing() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array: Path = "/empty".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    let snap = session.commit("empty array", None).await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let coords: Vec<ChunkIndices> =
        session.chunk_coordinates(&array).await?.try_collect().await?;
    assert!(coords.is_empty());

    // a group is not an array, and asking for its coordinates is an error
    let err = session.chunk_coordinates(&Path::root()).await.err();
    assert!(
        matches!(err, Some(SessionError::NotAnArray { .. })),
        "unexpected result: {err:?}"
    );
    Ok(())
}

// ===========================================================================
// manifest_per_array — one manifest per array, with a chunk-space bounding box
// (design/005-manifest-split.md)
// ===========================================================================

async fn repo_with_two_arrays(
) -> Result<(Arc<ObjectStorage>, Repository, Path, Path, SnapshotId), Box<dyn std::error::Error>>
{
    let object_storage = Arc::new(ObjectStorage::new_in_memory()?);
    let storage: Arc<dyn Storage + Send + Sync> = Arc::clone(&object_storage) as _;
    let repo = Repository::create(None, storage, HashMap::new()).await?;

    let a1: Path = "/array1".try_into()?;
    let a2: Path = "/array2".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(a1.clone(), array_meta(vec![10, 10], vec![1, 1])).await?;
    session.add_array(a2.clone(), array_meta(vec![10, 10], vec![1, 1])).await?;
    for c in [[1u32, 2u32], [3, 4], [2, 7]] {
        session
            .set_chunk_ref(
                a1.clone(),
                ChunkIndices(c.to_vec()),
                Some(ChunkPayload::Inline("x".into())),
            )
            .await?;
    }
    session
        .set_chunk_ref(
            a2.clone(),
            ChunkIndices(vec![0, 0]),
            Some(ChunkPayload::Inline("y".into())),
        )
        .await?;
    let snap = session.commit("write", None).await?;
    Ok((object_storage, repo, a1, a2, snap))
}

async fn manifest_refs_of(
    repo: &Repository,
    snap: &SnapshotId,
    array: &Path,
) -> Vec<icechunk::format::manifest::ManifestRef> {
    let session =
        repo.readonly_session(&VersionInfo::SnapshotId(snap.clone())).await.unwrap();
    let node = session.get_array(array).await.unwrap();
    match node.node_data {
        NodeData::Array(_, refs) => refs,
        NodeData::Group => panic!("{array:?} is not an array"),
    }
}

#[tokio::test]
async fn manifest_per_array__each_array_gets_its_own_manifest_object() -> R {
    let (object_storage, _repo, _a1, _a2, _snap) = repo_with_two_arrays().await?;
    let manifest_keys: Vec<String> = object_storage
        .all_keys()
        .await?
        .into_iter()
        .filter(|k| k.contains("manifests/"))
        .collect();
    assert_eq!(manifest_keys.len(), 2, "keys were {manifest_keys:?}");

    // and a manifest is only built when there is something to put in it: an
    // empty stream of chunk references yields `None`, so no third object could
    // ever have been written (TEST_INTERFACE.md 9.2)
    let empty = icechunk::format::manifest::Manifest::from_stream(
        futures::stream::empty::<Result<_, std::convert::Infallible>>(),
    )
    .await
    .expect("an empty stream is not an error");
    assert!(empty.is_none());
    let empty = icechunk::format::manifest::Manifest::from_iter(std::iter::empty())
        .await
        .expect("an empty iterator is not an error");
    assert!(empty.is_none());
    Ok(())
}

#[tokio::test]
async fn manifest_per_array__an_array_references_exactly_one_manifest() -> R {
    let (_object_storage, repo, a1, a2, snap) = repo_with_two_arrays().await?;
    let r1 = manifest_refs_of(&repo, &snap, &a1).await;
    let r2 = manifest_refs_of(&repo, &snap, &a2).await;
    assert_eq!(r1.len(), 1);
    assert_eq!(r2.len(), 1);
    assert_ne!(r1[0].object_id, r2[0].object_id);
    Ok(())
}

#[tokio::test]
async fn manifest_per_array__the_extent_is_the_chunk_space_bounding_box() -> R {
    let (_object_storage, repo, a1, a2, snap) = repo_with_two_arrays().await?;
    let r1 = manifest_refs_of(&repo, &snap, &a1).await;
    // chunks (1,2) (3,4) (2,7): per-axis minimum, and maximum + 1 (end exclusive)
    assert_eq!(r1[0].extents.start, ChunkIndices(vec![1, 2]));
    assert_eq!(r1[0].extents.end, ChunkIndices(vec![4, 8]));

    let r2 = manifest_refs_of(&repo, &snap, &a2).await;
    assert_eq!(r2[0].extents.start, ChunkIndices(vec![0, 0]));
    assert_eq!(r2[0].extents.end, ChunkIndices(vec![1, 1]));
    Ok(())
}

#[tokio::test]
async fn manifest_per_array__the_snapshot_records_each_manifest_size_and_row_count() -> R {
    let (_object_storage, repo, a1, a2, snap) = repo_with_two_arrays().await?;
    let snapshot = repo.asset_manager().fetch_snapshot(&snap).await?;
    assert_eq!(snapshot.manifest_files().len(), 2);

    let id1 = manifest_refs_of(&repo, &snap, &a1).await[0].object_id.clone();
    let id2 = manifest_refs_of(&repo, &snap, &a2).await[0].object_id.clone();
    let info1 = snapshot.manifest_info(&id1).expect("array1's manifest info");
    let info2 = snapshot.get_manifest_file(&id2).expect("array2's manifest info");
    assert_eq!(info1.num_rows, 3);
    assert_eq!(info2.num_rows, 1);
    assert!(info1.size_bytes > 0);
    assert!(info2.size_bytes > 0);
    assert_eq!(&info1.id, &id1);
    Ok(())
}

// ===========================================================================
// config_field_merge — field-by-field override over the persisted config
// ===========================================================================

#[tokio::test]
async fn config_field_merge__creating_a_repository_persists_no_configuration() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert!(Repository::fetch_config(storage.as_ref()).await?.is_none());
    assert_eq!(repo.config(), &RepositoryConfig::default());

    // With nothing in `config.storage`, the repository's effective storage
    // settings are whatever the backend itself reports, and `storage()` hands
    // back a handle to the repository's own backing store (TEST_INTERFACE.md 8
    // and 9.4). Nothing here assumes the handle is the very same `Arc` — only
    // that it reaches the same store.
    assert!(repo.config().storage().is_none());
    assert_eq!(repo.storage_settings(), &storage.default_settings());
    assert!(Repository::exists(repo.storage().as_ref()).await?);

    // The 18 / 12 MiB figures in TEST_INTERFACE.md 9.5 are the *struct*
    // fall-backs, read through the getters when the fields are unset. A
    // backend's `default_settings()` is free to set its own instead, so they
    // are asserted on the struct and never on a particular backend.
    let unset = icechunk::storage::ConcurrencySettings::default();
    assert_eq!(unset.max_concurrent_requests_for_object().get(), 18);
    assert_eq!(unset.ideal_concurrent_request_size().get(), 12 * 1024 * 1024);
    assert_eq!(
        icechunk::storage::Settings::default().concurrency(),
        &icechunk::storage::ConcurrencySettings::default()
    );

    // an explicit save does persist it, and what comes back is what was saved
    repo.save_config().await?;
    let (persisted, _etag) =
        Repository::fetch_config(storage.as_ref()).await?.expect("a persisted config");
    assert_eq!(&persisted, repo.config());
    Ok(())
}

#[tokio::test]
async fn config_field_merge__an_explicit_field_wins_and_the_rest_fall_back() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    repo.save_config().await?;

    let repo = Repository::open(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(42),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    assert_eq!(repo.config().inline_chunk_threshold_bytes(), 42);
    // every unset field still reports its built-in default
    assert!(!repo.config().unsafe_overwrite_refs());
    assert_eq!(repo.config().get_partial_values_concurrency(), 10);
    assert_eq!(repo.config().caching().num_chunk_refs(), 5_000_000);
    assert_eq!(repo.config().caching().num_snapshot_nodes(), 10_000);
    assert_eq!(repo.config().caching().num_transaction_changes(), 0);
    assert_eq!(repo.config().caching().num_bytes_attributes(), 0);
    assert_eq!(repo.config().caching().num_bytes_chunks(), 0);
    assert_eq!(repo.config().compression().level(), 1);
    assert_eq!(repo.config().manifest().preload().max_total_refs(), 10_000);
    Ok(())
}

#[tokio::test]
async fn config_field_merge__the_persisted_configuration_round_trips() -> R {
    let storage = mem_storage();
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let repo = Repository::open(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(42),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    repo.save_config().await?;

    let reopened = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(reopened.config().inline_chunk_threshold_bytes(), 42);
    let (persisted, _etag) =
        Repository::fetch_config(storage.as_ref()).await?.expect("a persisted config");
    assert_eq!(persisted.inline_chunk_threshold_bytes, Some(42));
    assert_eq!(persisted.caching, None);
    Ok(())
}

#[tokio::test]
async fn config_field_merge__reopen_merges_nested_fields() -> R {
    use icechunk::config::CachingConfig;
    let storage = mem_storage();
    Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let repo = Repository::open(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(42),
            ..Default::default()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let reopened = repo.reopen(
        Some(RepositoryConfig {
            caching: Some(CachingConfig {
                num_chunk_refs: Some(100),
                ..Default::default()
            }),
            ..Default::default()
        }),
        None,
    )?;
    assert_eq!(reopened.config().inline_chunk_threshold_bytes(), 42);
    assert_eq!(reopened.config().caching().num_chunk_refs(), 100);
    // sibling fields inside the nested config keep their defaults
    assert_eq!(reopened.config().caching().num_snapshot_nodes(), 10_000);
    // reopening with nothing to override preserves everything
    let again = reopened.reopen(None, None)?;
    assert_eq!(again.config(), reopened.config());
    Ok(())
}

// ===========================================================================
// inherited_object_layout — milestone 1's on-disk repository format,
// re-read through this release's API
// ===========================================================================

#[tokio::test]
async fn inherited_object_layout__objects_live_under_the_documented_prefixes() -> R {
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
    session.commit("write", None).await?;

    let keys = object_storage.all_keys().await?;
    for prefix in ["refs/", "snapshots/", "manifests/", "chunks/", "transactions/"] {
        assert!(
            keys.iter().any(|k| k.contains(prefix)),
            "no object under {prefix}: {keys:?}"
        );
    }
    Ok(())
}

#[tokio::test]
async fn inherited_object_layout__the_branch_tip_is_the_first_key_in_its_prefix() -> R {
    let object_storage = Arc::new(ObjectStorage::new_in_memory()?);
    let storage: Arc<dyn Storage + Send + Sync> = Arc::clone(&object_storage) as _;
    let repo = Repository::create(None, storage, HashMap::new()).await?;
    let first = commit_group(&repo, "main", "first").await;

    let keys = object_storage.all_keys().await?;
    assert!(keys.iter().any(|k| k.contains("refs/branch.main/ZZZZZZZZ.json")));
    assert!(keys.iter().any(|k| k.contains("refs/branch.main/ZZZZZZZY.json")));

    let mut branch_keys: Vec<&String> =
        keys.iter().filter(|k| k.contains("refs/branch.main/")).collect();
    branch_keys.sort();
    // the newest sequence sorts FIRST, and that is the tip
    assert!(branch_keys[0].ends_with("ZZZZZZZY.json"));
    assert_eq!(repo.lookup_branch("main").await?, first);
    Ok(())
}

#[tokio::test]
async fn inherited_object_layout__reference_files_carry_one_snapshot_pointer() -> R {
    let storage = mem_storage();
    let settings = storage.default_settings();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let snap = commit_group(&repo, "main", "first").await;
    repo.create_tag("v1", &snap).await?;

    let bytes = storage.get_ref(&settings, "tag.v1/ref.json").await?;
    let value: serde_json::Value = serde_json::from_slice(&bytes)?;
    let object = value.as_object().expect("a JSON object");
    assert_eq!(object.len(), 1);
    let pointer = object.get("snapshot").and_then(|s| s.as_str()).expect("a pointer");
    // the pointer is the Base32-Crockford spelling of the snapshot id
    assert_eq!(pointer, snap.to_string());
    assert_eq!(pointer.len(), 20);
    Ok(())
}

// ===========================================================================
// inherited_chunk_ref_kinds — milestone 1's three chunk reference kinds
// ===========================================================================

#[tokio::test]
async fn inherited_chunk_ref_kinds__a_chunk_under_the_threshold_is_inlined() -> R {
    let object_storage = Arc::new(ObjectStorage::new_in_memory()?);
    let storage: Arc<dyn Storage + Send + Sync> = Arc::clone(&object_storage) as _;
    let repo = Repository::create(None, storage, HashMap::new()).await?;
    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    let writer = session.get_chunk_writer();
    let payload = writer(Bytes::copy_from_slice(b"small")).await?;
    assert!(matches!(payload, ChunkPayload::Inline(_)));
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    let snap = session.commit("inline", None).await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    match session.get_chunk_ref(&array, &ChunkIndices(vec![0, 0])).await? {
        Some(ChunkPayload::Inline(bytes)) => {
            assert_eq!(bytes, Bytes::copy_from_slice(b"small"))
        }
        other => panic!("expected an inline payload, got {other:?}"),
    }
    // nothing was written to the chunk prefix
    let keys = object_storage.all_keys().await?;
    assert!(!keys.iter().any(|k| k.contains("chunks/")), "keys: {keys:?}");
    Ok(())
}

#[tokio::test]
async fn inherited_chunk_ref_kinds__a_chunk_over_the_threshold_is_materialised() -> R {
    def_repo_with_materialised_chunks!();
    let storage = mem_storage();
    let settings = storage.default_settings();
    let (repo, array, snap) =
        repo_with_materialised_chunks(&storage, &[[0, 0]], "materialised").await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let chunk_ref = match session.get_chunk_ref(&array, &ChunkIndices(vec![0, 0])).await? {
        Some(ChunkPayload::Ref(r)) => r,
        other => panic!("expected a chunk reference, got {other:?}"),
    };
    let expected = chunk_bytes(&[0, 0], 0);
    assert_eq!(chunk_ref.offset, 0);
    assert_eq!(chunk_ref.length, expected.len() as u64);
    let raw = storage.fetch_chunk(&settings, &chunk_ref.id, &(0..chunk_ref.length)).await?;
    assert_eq!(raw, expected);

    let reader = session
        .get_chunk_reader(&array, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
        .await?;
    assert_eq!(get_chunk(reader).await?, Some(expected));
    Ok(())
}

#[tokio::test]
async fn inherited_chunk_ref_kinds__a_virtual_payload_points_at_a_foreign_file() -> R {
    let chunk_dir = tempfile::tempdir()?;
    let chunk_path = chunk_dir.path().join("foreign.bin");
    tokio::fs::write(&chunk_path, b"HELLOVIRTUAL").await?;

    let repo_dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let repo = Repository::create(
        Some(config_with_file_container()),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let array: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root()).await?;
    session.add_array(array.clone(), array_meta(vec![4, 4], vec![1, 1])).await?;
    let location = VirtualChunkLocation::from_absolute_path(&format!(
        "file://{}",
        chunk_path.to_str().expect("utf-8 path")
    ))?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0, 0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: location.clone(),
                offset: 5,
                length: 7,
                checksum: None,
            })),
        )
        .await?;
    let snap = session.commit("virtual", None).await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(matches!(
        session.get_chunk_ref(&array, &ChunkIndices(vec![0, 0])).await?,
        Some(ChunkPayload::Virtual(_))
    ));
    let reader = session
        .get_chunk_reader(&array, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
        .await?;
    assert_eq!(get_chunk(reader).await?, Some(Bytes::copy_from_slice(b"VIRTUAL")));
    Ok(())
}

// ===========================================================================
// inherited_sessions_commit — milestone 2's Repository / Session split
// ===========================================================================

#[tokio::test]
async fn inherited_sessions_commit__a_writable_session_commits_once() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    assert_eq!(session.branch(), Some("main"));
    assert!(!session.read_only());
    session.add_group(Path::root()).await?;
    assert!(session.has_uncommitted_changes());
    let snap = session.commit("first", None).await?;

    // committing moved the session to the new snapshot and made it read-only
    assert!(session.read_only());
    assert_eq!(session.branch(), None);
    assert_eq!(session.snapshot_id(), &snap);
    assert!(!session.has_uncommitted_changes());

    // so a second commit through the same session is refused
    session.add_group("/again".try_into()?).await?;
    let err = session.commit("again", None).await.expect_err("the session is read only");
    assert!(matches!(err, SessionError::ReadOnlySession), "unexpected error: {err:?}");
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

#[tokio::test]
async fn inherited_sessions_commit__a_read_only_session_refuses_writes() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let snap = commit_group(&repo, "main", "first").await;

    let mut session = repo.readonly_session(&VersionInfo::SnapshotId(snap.clone())).await?;
    assert!(session.read_only());
    assert_eq!(session.branch(), None);
    assert_eq!(session.snapshot_id(), &snap);

    // the session can stage changes, but it can never publish them
    session.add_group("/nope".try_into()?).await?;
    let err = session.commit("nope", None).await.expect_err("the session is read only");
    assert!(matches!(err, SessionError::ReadOnlySession), "unexpected error: {err:?}");
    let err = session
        .rebase(&BasicConflictSolver::default())
        .await
        .expect_err("the session is read only");
    assert!(matches!(err, SessionError::ReadOnlySession), "unexpected error: {err:?}");
    // and the branch is untouched
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

#[tokio::test]
async fn inherited_sessions_commit__sessions_open_at_branch_tag_and_snapshot() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let first = commit_group(&repo, "main", "first").await;
    repo.create_tag("v1", &first).await?;
    let second = commit_group(&repo, "main", "second").await;

    let at_tag = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert_eq!(at_tag.snapshot_id(), &first);
    assert!(at_tag.get_group(&"/first".try_into()?).await.is_ok());
    assert!(at_tag.get_group(&"/second".try_into()?).await.is_err());

    let at_branch = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    assert_eq!(at_branch.snapshot_id(), &second);
    assert!(at_branch.get_group(&"/second".try_into()?).await.is_ok());

    let at_id = repo.readonly_session(&VersionInfo::SnapshotId(first.clone())).await?;
    assert_eq!(at_id.snapshot_id(), &first);
    Ok(())
}

// ===========================================================================
// inherited_conflicts_rebase — milestone 2's conflict classification and rebase
// ===========================================================================

#[tokio::test]
async fn inherited_conflicts_rebase__a_stale_commit_reports_both_parents() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let (mut first, mut second) = sessions_for_conflict(&repo).await?;

    first.add_group("/from-first".try_into()?).await?;
    let winner = first.commit("first wins", None).await?;

    second.add_group("/from-second".try_into()?).await?;
    let err = second.commit("second loses", None).await.expect_err("stale parent");
    match err {
        SessionError::Conflict { expected_parent, actual_parent } => {
            assert_eq!(actual_parent, Some(winner));
            assert!(expected_parent.is_some());
            assert_ne!(expected_parent, actual_parent);
        }
        other => panic!("unexpected error: {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn inherited_conflicts_rebase__the_detector_classifies_the_conflict() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let (mut first, mut second) = sessions_for_conflict(&repo).await?;

    let conflict_path: Path = "/both".try_into()?;
    first.add_group(conflict_path.clone()).await?;
    first.commit("first", None).await?;

    second.add_array(conflict_path.clone(), array_meta(vec![2, 2], vec![1, 1])).await?;
    second.commit("second", None).await.expect_err("stale parent");

    let err = second.rebase(&ConflictDetector).await.expect_err("a conflict");
    match err {
        SessionError::RebaseFailed { conflicts, .. } => {
            assert!(
                conflicts
                    .contains(&Conflict::NewNodeConflictsWithExistingNode(conflict_path)),
                "conflicts were {conflicts:?}"
            );
        }
        other => panic!("unexpected error: {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn inherited_conflicts_rebase__the_basic_solver_lets_the_commit_through() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array: Path = "/array".try_into()?;
    let (mut first, mut second) = sessions_for_conflict(&repo).await?;

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
    second.commit("second", None).await.expect_err("stale parent");

    // disjoint chunks: nothing actually conflicts, so the rebase fast-forwards
    second.rebase(&BasicConflictSolver::default()).await?;
    let merged = second.commit("second", None).await?;

    // Both writes survive the fast-forward. Read back through
    // `Session::get_chunk_ref`, milestone 2's own accessor
    // (TEST_INTERFACE.md 10.2), rather than through this release's new
    // `chunk_coordinates` — an inherited-regression group must not depend on a
    // capability this milestone introduces, and the payload check is stricter
    // than a coordinate listing anyway.
    let session = repo.readonly_session(&VersionInfo::SnapshotId(merged)).await?;
    match session.get_chunk_ref(&array, &ChunkIndices(vec![0, 0])).await? {
        Some(ChunkPayload::Inline(bytes)) => {
            assert_eq!(bytes, Bytes::copy_from_slice(b"ours"))
        }
        other => panic!("expected our chunk at (0,0), got {other:?}"),
    }
    match session.get_chunk_ref(&array, &ChunkIndices(vec![1, 1])).await? {
        Some(ChunkPayload::Inline(bytes)) => {
            assert_eq!(bytes, Bytes::copy_from_slice(b"theirs"))
        }
        other => panic!("expected their chunk at (1,1), got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn inherited_conflicts_rebase__an_unsolvable_rebase_names_the_snapshot() -> R {
    let storage = mem_storage();
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array: Path = "/array".try_into()?;
    let (mut first, mut second) = sessions_for_conflict(&repo).await?;

    first.update_array(array.clone(), array_meta(vec![8, 8], vec![1, 1])).await?;
    let blocking = first.commit("first", None).await?;

    second.update_array(array.clone(), array_meta(vec![16, 16], vec![1, 1])).await?;
    second.commit("second", None).await.expect_err("stale parent");

    let err = second
        .rebase(&BasicConflictSolver::default())
        .await
        .expect_err("array metadata cannot be merged");
    match err {
        SessionError::RebaseFailed { snapshot, conflicts } => {
            assert_eq!(snapshot, blocking);
            assert!(
                conflicts.iter().any(|c| matches!(
                    c,
                    Conflict::ZarrMetadataDoubleUpdate(p) if p == &array
                )),
                "conflicts were {conflicts:?}"
            );
        }
        other => panic!("unexpected error: {other:?}"),
    }
    Ok(())
}

// ===========================================================================
// inherited_virtual_containers — milestone 2's virtual chunk containers
// ===========================================================================

#[tokio::test]
async fn inherited_virtual_containers__a_configured_container_is_matched() -> R {
    let storage = mem_storage();
    let repo = Repository::create(
        Some(config_with_file_container()),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let container =
        repo.config().get_virtual_chunk_container("file").expect("the file container");
    assert_eq!(container.url_prefix, "file://");
    assert!(repo
        .config()
        .virtual_chunk_containers()
        .any(|c| c.name == "file"));

    let session = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    let location = VirtualChunkLocation::from_absolute_path("file:///tmp/whatever.bin")?;
    let matched = session.matching_container(&location).expect("a matching container");
    assert_eq!(matched.name, "file");
    Ok(())
}

#[tokio::test]
async fn inherited_virtual_containers__a_virtual_reference_resolves_through_it() -> R {
    let chunk_dir = tempfile::tempdir()?;
    let chunk_path = chunk_dir.path().join("data.bin");
    tokio::fs::write(&chunk_path, b"0123456789").await?;

    let repo_dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    let repo = Repository::create(
        Some(config_with_file_container()),
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
                offset: 2,
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
    assert_eq!(get_chunk(reader).await?, Some(Bytes::copy_from_slice(b"2345")));

    let locations: Vec<String> =
        session.all_virtual_chunk_locations().await?.try_collect().await?;
    assert_eq!(locations.len(), 1);
    assert!(locations[0].starts_with("file://"));
    Ok(())
}

#[tokio::test]
async fn inherited_virtual_containers__a_location_with_no_container_is_rejected() -> R {
    let repo_dir = tempfile::tempdir()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(repo_dir.path())?;
    // a repository configured with ONLY the file container
    let repo = Repository::create(
        Some(config_with_file_container()),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let session = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    let unmatched =
        VirtualChunkLocation::from_absolute_path("s3://some-bucket/some/key")?;
    assert!(session.matching_container(&unmatched).is_none());
    assert!(repo.config().get_virtual_chunk_container("s3").is_none());
    Ok(())
}
