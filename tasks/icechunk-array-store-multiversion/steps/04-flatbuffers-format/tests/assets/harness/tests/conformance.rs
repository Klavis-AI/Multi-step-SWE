// Verifier-owned conformance driver for milestone 04 ("the FlatBuffers format",
// upstream `v0.2.0`, crate `icechunk` 0.2.0).
//
// It is compiled in a throw-away crate OUTSIDE /app and linked to the candidate
// by path. Only the public `icechunk::` surface documented in
// /app/TEST_INTERFACE.md and /app/icechunk-spec/ is used.
//
// Every case function is named `<group>__<case>`; `<group>` must appear in
// tests/assets/groups.txt. Assertions are on observable values and error
// CATEGORIES - never on a `Display` string, a `Debug` rendering, wall-clock
// timing, allocation, or source layout.
//
// No network, no cloud, no MinIO: every case runs on the in-memory object store
// or on a `tempfile::TempDir`, and virtual chunk references use `file://` URLs
// into a temporary directory.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
// case functions are named `<group>__<case>`; the double separator is what the
// verifier's per-group filter and attribution slice recognise.
#![allow(non_snake_case)]

use std::{
    collections::{HashMap, HashSet},
    io::Cursor,
    num::NonZeroU64,
    sync::Arc,
};

use bytes::Bytes;
use chrono::{SubsecRound, Utc};
use futures::{StreamExt, TryStreamExt};
use tokio::io::AsyncReadExt;
use tokio::sync::RwLock;

// WHY THIS `use` BLOCK IS INCOMPLETE - do not "tidy" it.
//
// The verifier attributes a compile failure to one group by cutting every OTHER
// group's case FUNCTIONS out of this file (`SLICE_AWK` in tests/test.sh) and
// nothing else: every `use` item, const, type alias and module-level helper
// survives in EVERY slice. So a file-scope name that only this milestone's
// release publishes makes every group's slice fail to compile - the inherited
// groups included, and a candidate that got nineteen groups right included.
//
// Therefore this block names only the surface that already existed at the
// previous milestone. Everything this milestone adds is named INSIDE the case
// bodies that need it: a `use` line in the case itself, or one of the
// `macro_rules!` fixtures below, whose bodies are not type-checked until they
// are expanded inside a surviving case.
use icechunk::{
    asset_manager::AssetManager,
    conflicts::{
        basic_solver::{BasicConflictSolver, VersionSelection},
        detector::ConflictDetector,
        Conflict,
    },
    format::{
        format_constants::{
            CompressionAlgorithmBin, FileTypeBin, SpecVersionBin,
            ICECHUNK_FORMAT_MAGIC_BYTES,
        },
        manifest::{
            ChunkInfo, ChunkPayload, ChunkRef, Manifest, ManifestRef,
            VirtualChunkLocation, VirtualChunkRef,
        },
        snapshot::{
            ManifestFileInfo, NodeData, NodeSnapshot, Snapshot, SnapshotInfo,
            SnapshotProperties,
        },
        transaction_log::TransactionLog,
        ByteRange, ChunkId, ChunkIndices, IcechunkFormatError, ManifestId, NodeId, Path,
        SnapshotId,
    },
    new_local_filesystem_storage,
    ops::gc::{expire, garbage_collect, Action, GCConfig},
    refs::Ref,
    repository::{RepositoryError, VersionInfo},
    session::{Session, SessionError},
    storage,
    store::{StoreError},
    Repository, Storage, Store,
};

type Res = Result<(), Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// helpers (no `__` in any helper name: the verifier's per-group attribution
// slice recognises a case function only by that separator)
// ---------------------------------------------------------------------------

/// The zstd frame magic, little endian on the wire: 0xFD2FB528.
const ZSTD_FRAME_MAGIC: [u8; 4] = [0x28, 0xb5, 0x2f, 0xfd];

/// A byte string that is a plausible Msgpack map - the payload shape Icechunk
/// wrote before this release - and is certainly not a FlatBuffers root.
const MSGPACK_LOOKING_PAYLOAD: &[u8] = &[
    0x83, 0xa2, 0x69, 0x64, 0xc4, 0x0c, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0xa7, 0x6d, 0x65, 0x73, 0x73, 0x61, 0x67, 0x65, 0xa5, 0x68,
    0x65, 0x6c, 0x6c, 0x6f, 0xa5, 0x6e, 0x6f, 0x64, 0x65, 0x73, 0x90,
];

/// An in-memory object store. `new_in_memory_storage` became `async` at this
/// milestone, so this fixture is expanded inside the case that needs it.
macro_rules! mem_storage {
    () => {
        async fn mem_storage() -> std::sync::Arc<dyn icechunk::Storage + Send + Sync> {
            icechunk::new_in_memory_storage().await.unwrap()
        }
    };
}

/// An empty repository on an in-memory object store.
macro_rules! mem_repo {
    () => {
        async fn mem_repo() -> icechunk::Repository {
            icechunk::Repository::create(
                None,
                icechunk::new_in_memory_storage().await.unwrap(),
                std::collections::HashMap::new(),
            )
            .await
            .unwrap()
        }
    };
}

fn settings() -> storage::Settings {
    storage::Settings::default()
}

async fn read_to_vec(mut r: Box<dyn tokio::io::AsyncRead + Unpin + Send>) -> Vec<u8> {
    let mut v = Vec::new();
    r.read_to_end(&mut v).await.unwrap();
    v
}

/// The raw bytes of a snapshot object exactly as they sit in the object store.
async fn raw_snapshot(st: &(dyn Storage + Send + Sync), id: &SnapshotId) -> Vec<u8> {
    read_to_vec(st.fetch_snapshot(&settings(), id).await.unwrap()).await
}

async fn raw_manifest(st: &(dyn Storage + Send + Sync), id: &ManifestId) -> Vec<u8> {
    read_to_vec(st.fetch_manifest_unknown_size(&settings(), id).await.unwrap()).await
}

async fn raw_transaction_log(
    st: &(dyn Storage + Send + Sync),
    id: &SnapshotId,
) -> Vec<u8> {
    read_to_vec(st.fetch_transaction_log(&settings(), id).await.unwrap()).await
}

/// A 39-byte Icechunk metadata-file header, built only from the public
/// `format_constants` vocabulary, exactly as a third-party writer would.
fn make_header(implementation_id: &[u8; 24], spec: u8, file_type: u8, comp: u8) -> Vec<u8> {
    let mut h = Vec::with_capacity(39);
    h.extend_from_slice(ICECHUNK_FORMAT_MAGIC_BYTES);
    h.extend_from_slice(implementation_id);
    h.push(spec);
    h.push(file_type);
    h.push(comp);
    assert_eq!(h.len(), 39);
    h
}

fn current_header(file_type: FileTypeBin) -> Vec<u8> {
    make_header(
        b"third-party-writer      ",
        SpecVersionBin::current() as u8,
        file_type as u8,
        CompressionAlgorithmBin::Zstd as u8,
    )
}

/// A snapshot holding a root group and one array, useful wherever a decoded
/// object has to be inspected field by field. `add_group` and `add_array` both
/// take the node's Zarr document at this milestone, so this fixture is expanded
/// inside the case that needs it.
macro_rules! commit_sample {
    () => {
        async fn commit_sample(
            repo: &icechunk::Repository,
            message: &str,
        ) -> (
            icechunk::format::SnapshotId,
            icechunk::format::Path,
            icechunk::format::NodeId,
        ) {
            use bytes::Bytes;
            use icechunk::format::{
                manifest::ChunkPayload,
                snapshot::{ArrayShape, DimensionName},
                ChunkIndices, Path,
            };
            let mut s = repo.writable_session("main").await.unwrap();
            s.add_group(Path::root(), Bytes::from_static(br#"{"zarr_format":3}"#))
                .await
                .unwrap();
            let p: Path = "/array".try_into().unwrap();
            let shape = ArrayShape::new(vec![(8u64, 2), (6u64, 3)]).unwrap();
            let dims = Some(vec![DimensionName::from("x"), DimensionName::from("y")]);
            s.add_array(p.clone(), shape, dims, Bytes::from_static(b"array-user-data"))
                .await
                .unwrap();
            s.set_chunk_ref(
                p.clone(),
                ChunkIndices(vec![0, 0]),
                Some(ChunkPayload::Inline(Bytes::from_static(b"hello"))),
            )
            .await
            .unwrap();
            let node_id = s.get_array(&p).await.unwrap().id;
            let snap_id = s.commit(message, None).await.unwrap();
            (snap_id, p, node_id)
        }
    };
}

/// Two nodes - a root group and one array - built by hand. `NodeSnapshot` grew
/// `user_data` and `NodeData::Array` its three fields at this milestone.
macro_rules! sample_nodes {
    () => {
        fn sample_nodes() -> Vec<icechunk::format::snapshot::NodeSnapshot> {
            use bytes::Bytes;
            use icechunk::format::{
                manifest::{ManifestExtents, ManifestRef},
                snapshot::{ArrayShape, DimensionName, NodeData, NodeSnapshot},
                ManifestId, NodeId, Path,
            };
            let shape = ArrayShape::new(vec![(10u64, 3), (20u64, 2), (30u64, 1)]).unwrap();
            let dims = Some(vec![
                DimensionName::NotSpecified,
                DimensionName::NotSpecified,
                DimensionName::from("t"),
            ]);
            let manifest = ManifestRef {
                object_id: ManifestId::random(),
                extents: ManifestExtents::new(&[0, 0, 0], &[100, 100, 100]),
            };
            vec![
                NodeSnapshot {
                    path: Path::root(),
                    id: NodeId::random(),
                    user_data: Bytes::new(),
                    node_data: NodeData::Group,
                },
                NodeSnapshot {
                    path: "/b".try_into().unwrap(),
                    id: NodeId::random(),
                    user_data: Bytes::copy_from_slice(b"hello"),
                    node_data: NodeData::Array {
                        shape,
                        dimension_names: dims,
                        manifests: vec![manifest],
                    },
                },
            ]
        }
    };
}

/// `Snapshot::from_iter` over an already-sorted node list.
macro_rules! snapshot_from {
    () => {
        fn snapshot_from(
            nodes: Vec<icechunk::format::snapshot::NodeSnapshot>,
            files: Vec<icechunk::format::snapshot::ManifestFileInfo>,
        ) -> icechunk::format::snapshot::Snapshot {
            use icechunk::format::{snapshot::Snapshot, IcechunkFormatError};
            Snapshot::from_iter(
                None,
                None,
                "msg".to_string(),
                None,
                files,
                // section 4.4's bound is `IcechunkFormatError: From<E>`.
                // Instantiating it with `E = IcechunkFormatError` needs only
                // core's reflexive `impl<T> From<T> for T`, so this asks for no
                // conversion the contract does not already name.
                nodes.into_iter().map(Ok::<_, IcechunkFormatError>),
            )
            .unwrap()
        }
    };
}

async fn sample_manifest() -> Manifest {
    let node = NodeId::random();
    Manifest::from_iter(vec![ChunkInfo {
        node,
        coord: ChunkIndices(vec![0, 1]),
        payload: ChunkPayload::Inline(Bytes::from_static(b"hi")),
    }])
    .await
    .unwrap()
    .unwrap()
}

/// Lay out, on a local directory, the reference files a spec-1 (pre-`v0.2.0`)
/// writer produced: a sequence-numbered branch file under
/// `refs/branch.main/`, and nothing named `ref.json`.
fn write_spec1_branch(root: &std::path::Path) {
    let dir = root.join("refs").join("branch.main");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(dir.join("ZZZZZZZZ.json"), br#"{"snapshot":"VY76P925PRY57WFEK410"}"#)
        .unwrap();
    std::fs::create_dir_all(root.join("snapshots")).unwrap();
}

// ===========================================================================
// g01_file_header - the 39-byte plaintext header this release writes
// ===========================================================================

#[tokio::test]
async fn g01_file_header__snapshot_object() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let raw = raw_snapshot(repo.storage().as_ref(), &snap_id).await;

    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES.len(), 12);
    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES, "ICE\u{1F9CA}CHUNK".as_bytes());
    assert!(raw.len() > 39 + 4);
    assert_eq!(&raw[0..12], ICECHUNK_FORMAT_MAGIC_BYTES);
    assert_eq!(raw[36], SpecVersionBin::current() as u8);
    assert_eq!(raw[36], 1);
    assert_eq!(raw[37], FileTypeBin::Snapshot as u8);
    assert_eq!(raw[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(&raw[39..43], &ZSTD_FRAME_MAGIC);
    Ok(())
}

#[tokio::test]
async fn g01_file_header__manifest_object() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let info = snap.manifest_files().next().expect("the commit wrote a manifest");
    let raw = raw_manifest(repo.storage().as_ref(), &info.id).await;

    assert_eq!(&raw[0..12], ICECHUNK_FORMAT_MAGIC_BYTES);
    assert_eq!(raw[36], SpecVersionBin::current() as u8);
    assert_eq!(raw[37], FileTypeBin::Manifest as u8);
    assert_eq!(raw[37], 2);
    assert_eq!(raw[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(&raw[39..43], &ZSTD_FRAME_MAGIC);
    Ok(())
}

#[tokio::test]
async fn g01_file_header__transaction_log_object() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let raw = raw_transaction_log(repo.storage().as_ref(), &snap_id).await;

    assert_eq!(&raw[0..12], ICECHUNK_FORMAT_MAGIC_BYTES);
    assert_eq!(raw[36], SpecVersionBin::current() as u8);
    assert_eq!(raw[37], FileTypeBin::TransactionLog as u8);
    assert_eq!(raw[37], 4);
    assert_eq!(raw[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(&raw[39..43], &ZSTD_FRAME_MAGIC);
    Ok(())
}

#[tokio::test]
async fn g01_file_header__implementation_id_is_24_padded_ascii_bytes() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let raw = raw_snapshot(repo.storage().as_ref(), &snap_id).await;

    let id = &raw[12..36];
    assert_eq!(id.len(), 24);
    // printable ASCII only - no NUL padding, no UTF-8 above the ASCII range
    assert!(id.iter().all(|b| b.is_ascii() && *b >= 0x20));
    // the identification is left-aligned and padded on the RIGHT, so the field
    // never begins with padding. Its length is the writer's business and is not
    // asserted.
    assert_ne!(id[0], b' ');
    let identification = std::str::from_utf8(id)?.trim_end_matches(' ');
    assert!(!identification.is_empty());
    Ok(())
}

#[tokio::test]
async fn g01_file_header__only_the_file_type_byte_differs_between_types() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let info = snap.manifest_files().next().unwrap();

    let s = raw_snapshot(repo.storage().as_ref(), &snap_id).await;
    let m = raw_manifest(repo.storage().as_ref(), &info.id).await;
    let t = raw_transaction_log(repo.storage().as_ref(), &snap_id).await;

    assert_eq!(&s[0..37], &m[0..37]);
    assert_eq!(&s[0..37], &t[0..37]);
    assert_eq!(s[38], m[38]);
    assert_eq!(s[38], t[38]);
    assert_ne!(s[37], m[37]);
    assert_ne!(s[37], t[37]);
    assert_ne!(m[37], t[37]);

    // Byte 37 ranges over `FileTypeBin`. The three metadata kinds a writer
    // produces are 1, 2 and 4; code 3 is reserved for attributes files, which
    // nothing writes, and code 5 - `FileTypeBin::Chunk`, added at THIS release -
    // is reserved for chunk objects, which carry no header at all. So no
    // metadata object ever carries 3 or 5, and 5 is nonetheless a valid code.
    assert_eq!(FileTypeBin::Chunk as u8, 5);
    assert_eq!(FileTypeBin::try_from(5u8).unwrap() as u8, 5);
    for byte in [s[37], m[37], t[37]] {
        assert!(byte == 1 || byte == 2 || byte == 4, "unexpected file type {byte}");
        assert_ne!(byte, FileTypeBin::Chunk as u8);
        assert_ne!(byte, FileTypeBin::Attributes as u8);
    }
    Ok(())
}

// ===========================================================================
// g02_header_diagnostics - the named errors `check_header` produces
// ===========================================================================

/// Store `bytes` as the snapshot object `id` and try to read it back through
/// the asset manager.
macro_rules! fetch_crafted_snapshot {
    () => {
        async fn fetch_crafted_snapshot(
            bytes: Vec<u8>,
        ) -> (
            icechunk::format::SnapshotId,
            icechunk::repository::RepositoryError,
        ) {
            use icechunk::{asset_manager::AssetManager, format::SnapshotId, Storage};
            use std::sync::Arc;
            let st: Arc<dyn Storage + Send + Sync> =
                icechunk::new_in_memory_storage().await.unwrap();
            let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
            let id = SnapshotId::random();
            st.write_snapshot(&settings(), id.clone(), vec![], bytes::Bytes::from(bytes))
                .await
                .unwrap();
            let err = am.fetch_snapshot(&id).await.unwrap_err();
            (id, err)
        }
    };
}

#[tokio::test]
async fn g02_header_diagnostics__bad_magic_numbers() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    fetch_crafted_snapshot!();
    let mut h = current_header(FileTypeBin::Snapshot);
    h[0] ^= 0xff;
    h.extend_from_slice(&ZSTD_FRAME_MAGIC);
    let (_, err) = fetch_crafted_snapshot(h).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidMagicNumbers
            ),
            ..
        }
    ));
    Ok(())
}

#[tokio::test]
async fn g02_header_diagnostics__unknown_spec_version_at_byte_36() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    fetch_crafted_snapshot!();
    let mut h = make_header(
        b"third-party-writer      ",
        2, // a spec version this build does not know
        FileTypeBin::Snapshot as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    h.extend_from_slice(&ZSTD_FRAME_MAGIC);
    let (_, err) = fetch_crafted_snapshot(h).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidSpecVersion
            ),
            ..
        }
    ));
    Ok(())
}

#[tokio::test]
async fn g02_header_diagnostics__wrong_file_type_at_byte_37() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    fetch_crafted_snapshot!();
    let mut h = current_header(FileTypeBin::Manifest);
    h.extend_from_slice(&ZSTD_FRAME_MAGIC);
    let (_, err) = fetch_crafted_snapshot(h).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidFileType {
                    expected: FileTypeBin::Snapshot,
                    got: 2
                }
            ),
            ..
        }
    ));

    // an entirely unknown code is the same category
    let mut h = make_header(
        b"third-party-writer      ",
        SpecVersionBin::current() as u8,
        9,
        CompressionAlgorithmBin::Zstd as u8,
    );
    h.extend_from_slice(&ZSTD_FRAME_MAGIC);
    let (_, err) = fetch_crafted_snapshot(h).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidFileType { got: 9, .. }
            ),
            ..
        }
    ));
    Ok(())
}

#[tokio::test]
async fn g02_header_diagnostics__unknown_compression_at_byte_38() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    fetch_crafted_snapshot!();
    let mut h = make_header(
        b"third-party-writer      ",
        SpecVersionBin::current() as u8,
        FileTypeBin::Snapshot as u8,
        7,
    );
    h.extend_from_slice(&ZSTD_FRAME_MAGIC);
    let (_, err) = fetch_crafted_snapshot(h).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidCompressionAlgorithm
            ),
            ..
        }
    ));
    Ok(())
}

#[tokio::test]
async fn g02_header_diagnostics__short_object_is_an_io_error() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    fetch_crafted_snapshot!();
    // the magic is intact, so the first twelve bytes pass; the read then runs
    // out of bytes inside the implementation-id field (section 5.1)
    let h = current_header(FileTypeBin::Snapshot);
    let (_, err) = fetch_crafted_snapshot(h[..20].to_vec()).await;
    assert!(matches!(err, RepositoryError { kind: RepositoryErrorKind::IOError(_), .. }));

    // ... but shortness alone is not what makes it an IOError. Each field is
    // validated as soon as its own bytes are read (section 5.1), so twenty
    // bytes that do NOT carry the magic fail on the magic, not on the length
    let (_, err) = fetch_crafted_snapshot(vec![b'x'; 20]).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidMagicNumbers
            ),
            ..
        }
    ));

    // a well-formed header with no payload at all clears every header check and
    // then fails while reading the compressed frame
    let (_, err) = fetch_crafted_snapshot(h).await;
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::IO(_)),
            ..
        }
    ));
    Ok(())
}

// ===========================================================================
// g03_flatbuffer_payload - bytes 39.. are a zstd frame whose plaintext is the
// bare, non-size-prefixed FlatBuffers root
// ===========================================================================

#[tokio::test]
async fn g03_flatbuffer_payload__root_is_bare_not_size_prefixed() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    sample_nodes!();
    snapshot_from!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let buf = snap.bytes().to_vec();
    assert!(buf.len() > 12);
    // a standard FlatBuffers root: uoffset to the root table, then the 4-byte
    // file identifier. A size-prefixed buffer would carry the identifier at
    // bytes 8..12 instead.
    assert_eq!(&buf[4..8], b"Ichk");
    assert_ne!(&buf[8..12], b"Ichk");

    // the reader accepts the bare root and rejects the size-prefixed encoding
    Snapshot::from_buffer(buf.clone()).expect("bare root must verify");
    let mut prefixed = (buf.len() as u32).to_le_bytes().to_vec();
    prefixed.extend_from_slice(&buf);
    assert!(matches!(
        Snapshot::from_buffer(prefixed),
        Err(IcechunkFormatError {
            kind: IcechunkFormatErrorKind::InvalidFlatBuffer(_),
            ..
        })
    ));
    Ok(())
}

#[tokio::test]
async fn g03_flatbuffer_payload__identifier_written_in_all_three_types() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let am = repo.asset_manager();
    let snap = am.fetch_snapshot(&snap_id).await?;
    let info = snap.manifest_files().next().unwrap();
    let manifest = am.fetch_manifest(&info.id, info.size_bytes).await?;
    let tx = am.fetch_transaction_log(&snap_id).await?;

    assert_eq!(&snap.bytes()[4..8], b"Ichk");
    assert_eq!(&manifest.bytes()[4..8], b"Ichk");
    assert_eq!(&tx.bytes()[4..8], b"Ichk");
    Ok(())
}

#[tokio::test]
async fn g03_flatbuffer_payload__snapshot_frame_decodes_to_the_buffer() -> Res {
    mem_storage!();
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let original = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let raw = raw_snapshot(repo.storage().as_ref(), &snap_id).await;

    // Rebuild the header from the public constants, keeping the written
    // implementation id, and put the object's bytes 39.. behind it unchanged.
    // If the header is exactly 39 plaintext bytes and everything after it is a
    // self-contained compressed frame, the spliced object must decode to a
    // snapshot whose buffer is byte-identical to the one in memory.
    let mut impl_id = [0u8; 24];
    impl_id.copy_from_slice(&raw[12..36]);
    let mut spliced = make_header(
        &impl_id,
        SpecVersionBin::current() as u8,
        FileTypeBin::Snapshot as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    assert_eq!(&spliced[..], &raw[..39]);
    spliced.extend_from_slice(&raw[39..]);

    let st = mem_storage().await;
    let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
    let fresh = SnapshotId::random();
    st.write_snapshot(&settings(), fresh.clone(), vec![], Bytes::from(spliced)).await?;
    let back = am.fetch_snapshot(&fresh).await?;
    assert_eq!(back.bytes(), original.bytes());
    assert_eq!(&back.bytes()[4..8], b"Ichk");
    Ok(())
}

#[tokio::test]
async fn g03_flatbuffer_payload__manifest_frame_decodes_to_the_buffer() -> Res {
    mem_storage!();
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let info = snap.manifest_files().next().unwrap();
    let original = repo.asset_manager().fetch_manifest(&info.id, info.size_bytes).await?;
    let raw = raw_manifest(repo.storage().as_ref(), &info.id).await;

    let mut impl_id = [0u8; 24];
    impl_id.copy_from_slice(&raw[12..36]);
    let mut spliced = make_header(
        &impl_id,
        SpecVersionBin::current() as u8,
        FileTypeBin::Manifest as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    assert_eq!(&spliced[..], &raw[..39]);
    spliced.extend_from_slice(&raw[39..]);

    let st = mem_storage().await;
    let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
    let fresh = ManifestId::random();
    st.write_manifest(&settings(), fresh.clone(), vec![], Bytes::from(spliced)).await?;
    let back = am.fetch_manifest_unknown_size(&fresh).await?;
    assert_eq!(back.bytes(), original.bytes());
    Ok(())
}

#[tokio::test]
async fn g03_flatbuffer_payload__serializers_move_the_bare_root() -> Res {
    use icechunk::format::serializers::{
        deserialize_manifest, deserialize_snapshot, deserialize_transaction_log,
        serialize_manifest, serialize_snapshot, serialize_transaction_log,
    };
    mem_repo!();
    commit_sample!();
    sample_nodes!();
    snapshot_from!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let mut out = Vec::new();
    serialize_snapshot(&snap, SpecVersionBin::current(), &mut out)?;
    assert_eq!(out.as_slice(), snap.bytes());
    let back = deserialize_snapshot(SpecVersionBin::current(), Box::new(Cursor::new(out)))?;
    assert_eq!(back.bytes(), snap.bytes());

    let manifest = sample_manifest().await;
    let mut out = Vec::new();
    serialize_manifest(&manifest, SpecVersionBin::current(), &mut out)?;
    assert_eq!(out.as_slice(), manifest.bytes());
    let back = deserialize_manifest(SpecVersionBin::current(), Box::new(Cursor::new(out)))?;
    assert_eq!(back.bytes(), manifest.bytes());

    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let tx = repo.asset_manager().fetch_transaction_log(&snap_id).await?;
    let mut out = Vec::new();
    serialize_transaction_log(&tx, SpecVersionBin::current(), &mut out)?;
    assert_eq!(out.as_slice(), tx.bytes());
    let back =
        deserialize_transaction_log(SpecVersionBin::current(), Box::new(Cursor::new(out)))?;
    assert_eq!(back.bytes(), tx.bytes());
    Ok(())
}

// ===========================================================================
// g04_snapshot_decode - a written repository's snapshot, read back field by field
// ===========================================================================

#[tokio::test]
async fn g04_snapshot_decode__identity_and_message() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let before = Utc::now();
    let (snap_id, _, _) = commit_sample(&repo, "first commit").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    assert_eq!(snap.id(), snap_id);
    assert_eq!(snap.message(), "first commit");
    let parent = snap.parent_id().expect("a commit has the initial snapshot as parent");
    let initial = repo.asset_manager().fetch_snapshot(&parent).await?;
    assert_eq!(initial.message(), Snapshot::INITIAL_COMMIT_MESSAGE);
    assert!(initial.parent_id().is_none());

    // the caching constructors of section 4.9 read the same object
    let cached = AssetManager::new(
        Arc::clone(repo.storage()),
        repo.storage_settings().clone(),
        10,
        10,
        10,
        1_024,
        1_024,
        1,
    );
    assert_eq!(cached.fetch_snapshot(&snap_id).await?.bytes(), snap.bytes());
    let configured = AssetManager::new_with_config(
        Arc::clone(repo.storage()),
        repo.storage_settings().clone(),
        &icechunk::config::CachingConfig::default(),
        1,
    );
    assert_eq!(configured.fetch_snapshot(&snap_id).await?.bytes(), snap.bytes());

    // ... and report when the object store took the object, which is not the
    // instant the snapshot itself records
    let last_modified = cached.get_snapshot_last_modified(&snap_id).await?;
    assert!(last_modified >= before.trunc_subsecs(6));
    assert!(last_modified <= Utc::now());

    // the manager itself is serde-serialisable, so a caller can carry one
    // across a process boundary the way `Store::as_bytes` carries a session
    let json = serde_json::to_string(&cached)?;
    let rebuilt: AssetManager = serde_json::from_str(&json)?;
    let _ = rebuilt;
    Ok(())
}

#[tokio::test]
async fn g04_snapshot_decode__nodes_are_iterable_and_sorted_by_path() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    assert_eq!(snap.len(), 2);
    assert!(!snap.is_empty());
    let paths: Vec<String> =
        snap.iter().map(|n| n.unwrap().path.to_string()).collect::<Vec<_>>();
    assert_eq!(paths, vec!["/".to_string(), "/array".to_string()]);
    let arced: Vec<String> = Arc::clone(&snap)
        .iter_arc()
        .map(|n| n.unwrap().path.to_string())
        .collect::<Vec<_>>();
    assert_eq!(arced, paths);
    Ok(())
}

#[tokio::test]
async fn g04_snapshot_decode__array_node_fields() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    use icechunk::format::snapshot::DimensionName;
    use icechunk::format::snapshot::NodeType;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, p, node_id) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let node = snap.get_node(&p)?;
    assert_eq!(node.id, node_id);
    assert_eq!(node.path, p);
    assert_eq!(node.user_data, Bytes::from_static(b"array-user-data"));
    assert_eq!(node.node_type(), NodeType::Array);
    match node.node_data {
        NodeData::Array { shape, dimension_names, manifests } => {
            assert_eq!(shape, ArrayShape::new(vec![(8u64, 2), (6u64, 3)]).unwrap());
            assert_eq!(
                dimension_names,
                Some(vec![DimensionName::Name("x".to_string()), DimensionName::from("y")])
            );
            assert_eq!(manifests.len(), 1);
            assert_eq!(manifests[0].extents.iter().count(), 2);
        }
        NodeData::Group => panic!("/array must decode as an array node"),
    }
    Ok(())
}

#[tokio::test]
async fn g04_snapshot_decode__group_node_fields() -> Res {
    use icechunk::format::snapshot::NodeType;
    use icechunk::format::IcechunkFormatErrorKind;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let root = snap.get_node(&Path::root())?;
    assert_eq!(root.node_data, NodeData::Group);
    assert_eq!(root.node_type(), NodeType::Group);
    assert_eq!(root.user_data, Bytes::from_static(br#"{"zarr_format":3}"#));

    let missing = snap.get_node(&"/nope".try_into()?);
    assert!(matches!(
        missing,
        Err(IcechunkFormatError {
            kind: IcechunkFormatErrorKind::NodeNotFound { .. },
            ..
        })
    ));
    Ok(())
}

#[tokio::test]
async fn g04_snapshot_decode__flushed_at_and_metadata_are_fallible() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    mem_repo!();
    let repo = mem_repo().await;
    let before = Utc::now();
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let props: SnapshotProperties =
        [("zzz".to_string(), serde_json::json!(2)), ("aaa".to_string(), serde_json::json!("x"))]
            .into_iter()
            .collect();
    let snap_id = s.commit("with properties", Some(props.clone())).await?;
    let after = Utc::now();

    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let ts = snap.flushed_at()?;
    // `flushed_at` is stored at microsecond resolution, so compare against a
    // lower bound truncated the same way rather than against a nanosecond clock
    assert!(ts >= before.trunc_subsecs(6) && ts <= after);

    let meta = snap.metadata()?;
    assert_eq!(meta, props);
    // SnapshotProperties iterates in sorted key order
    assert_eq!(meta.keys().cloned().collect::<Vec<_>>(), vec!["aaa", "zzz"]);

    // `flushed_at` is fallible because `snapshot.fbs` stores the instant as a
    // raw `uint64` of microseconds: a stored value `DateTime<Utc>` cannot
    // represent is `IcechunkFormatErrorKind::InvalidTimestamp`, never a panic.
    // Overwriting that scalar in place leaves the buffer structurally valid, so
    // verification still passes and only the timestamp is out of range.
    let stored = (ts.timestamp_micros() as u64).to_le_bytes();
    let hits: Vec<usize> = snap
        .bytes()
        .windows(8)
        .enumerate()
        .filter(|(_, w)| *w == stored)
        .map(|(i, _)| i)
        .collect();
    assert!(!hits.is_empty(), "the stored `flushed_at` microseconds must be in the buffer");
    // A conforming buffer is free to lay the rest of its bytes out differently,
    // so those eight bytes may occur more than once; try each candidate rather
    // than requiring the pattern to be unique.
    let mut replaced = false;
    for at in &hits {
        let mut patched = snap.bytes().to_vec();
        patched[*at..*at + 8].copy_from_slice(&u64::MAX.to_le_bytes());
        let Ok(out_of_range) = Snapshot::from_buffer(patched) else { continue };
        if !matches!(
            out_of_range.flushed_at(),
            Err(IcechunkFormatError {
                kind: IcechunkFormatErrorKind::InvalidTimestamp,
                ..
            })
        ) {
            continue;
        }
        assert_eq!(out_of_range.id(), snap.id()); // only the timestamp changed
        assert_eq!(out_of_range.message(), snap.message());
        replaced = true;
        break;
    }
    assert!(replaced, "an out-of-range stored `flushed_at` must be `InvalidTimestamp`");
    Ok(())
}

#[tokio::test]
async fn g04_snapshot_decode__manifest_files_and_info() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let files: Vec<ManifestFileInfo> = snap.manifest_files().collect();
    assert_eq!(files.len(), 1);
    assert_eq!(files[0].num_chunk_refs, 1);
    assert!(files[0].size_bytes > 0);
    assert_eq!(snap.manifest_info(&files[0].id), Some(files[0].clone()));
    assert_eq!(snap.get_manifest_file(&files[0].id), Some(files[0].clone()));
    assert_eq!(snap.manifest_info(&ManifestId::random()), None);

    // A commit writes one manifest per array with chunk references, so a
    // snapshot reached after two more arrays gained chunks carries THREE
    // manifest files - and `snapshot.fbs` requires that vector sorted in
    // ascending order of `ManifestFileInfo.id` (section 6).
    let mut s = repo.writable_session("main").await?;
    for name in ["/b1", "/b2"] {
        let p: Path = name.try_into()?;
        s.add_array(p.clone(), ArrayShape::new(vec![(4u64, 2)]).unwrap(), None, Bytes::new())
            .await?;
        s.set_chunk_ref(
            p,
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"x"))),
        )
        .await?;
    }
    let second = s.commit("two more arrays", None).await?;
    let snap = repo.asset_manager().fetch_snapshot(&second).await?;
    let files: Vec<ManifestFileInfo> = snap.manifest_files().collect();
    assert_eq!(files.len(), 3, "one manifest file per array that has chunks");
    let ids: Vec<ManifestId> = files.iter().map(|f| f.id.clone()).collect();
    let mut sorted = ids.clone();
    sorted.sort();
    assert_eq!(ids, sorted, "manifest_files must be ascending by id");
    for f in &files {
        assert_eq!(f.num_chunk_refs, 1);
        assert_eq!(snap.manifest_info(&f.id).as_ref(), Some(f));
    }
    Ok(())
}

#[tokio::test]
async fn g04_snapshot_decode__snapshot_info_try_from() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let info = SnapshotInfo::try_from(snap.as_ref())?;
    assert_eq!(info.id, snap.id());
    assert_eq!(info.parent_id, snap.parent_id());
    assert_eq!(info.message, snap.message());
    assert_eq!(info.flushed_at, snap.flushed_at()?);
    assert_eq!(info.metadata, snap.metadata()?);
    assert!(!info.is_initial());

    let initial = repo.asset_manager().fetch_snapshot(&snap.parent_id().unwrap()).await?;
    assert!(SnapshotInfo::try_from(initial.as_ref())?.is_initial());
    Ok(())
}

// ===========================================================================
// g05_snapshot_build - building a snapshot buffer directly
// ===========================================================================

#[tokio::test]
async fn g05_snapshot_build__from_iter_round_trip() -> Res {
    sample_nodes!();
    snapshot_from!();
    let nodes = sample_nodes();
    let expected = nodes[1].clone();
    let manifest_id = match &expected.node_data {
        NodeData::Array { manifests, .. } => manifests[0].object_id.clone(),
        NodeData::Group => unreachable!(),
    };
    let files = vec![ManifestFileInfo {
        id: manifest_id.clone(),
        size_bytes: 1_000_000,
        num_chunk_refs: 100_000,
    }];
    let snap = snapshot_from(nodes.clone(), files);
    let back = Snapshot::from_buffer(snap.bytes().to_vec())?;
    assert_eq!(back.get_node(&"/b".try_into()?)?, expected);
    assert_eq!(back.manifest_info(&manifest_id).unwrap().num_chunk_refs, 100_000);
    assert_eq!(back.manifest_info(&manifest_id).unwrap().size_bytes, 1_000_000);
    assert_eq!(back.len(), 2);

    // `manifest_files` is stored ascending by id whatever order the caller
    // supplies (section 6). These three ids are fixed, so the check does not
    // depend on which random ids a run happens to draw.
    let descending: Vec<ManifestFileInfo> = [3u8, 2, 1]
        .into_iter()
        .map(|b| ManifestFileInfo {
            id: ManifestId::new([b; 12]),
            size_bytes: b as u64,
            num_chunk_refs: b as u32,
        })
        .collect();
    let ordered = snapshot_from(nodes, descending);
    let got: Vec<ManifestFileInfo> = ordered.manifest_files().collect();
    assert_eq!(
        got.iter().map(|f| f.id.clone()).collect::<Vec<_>>(),
        vec![
            ManifestId::new([1u8; 12]),
            ManifestId::new([2u8; 12]),
            ManifestId::new([3u8; 12])
        ]
    );
    assert_eq!(got[0].num_chunk_refs, 1);
    assert_eq!(got[2].size_bytes, 3);

    // `from_iter`'s item type is fallible: an `Err` item aborts the build and
    // the error reaches the caller instead of a half-written buffer.
    let failed = Snapshot::from_iter(
        None,
        None,
        "failing".to_string(),
        None,
        vec![],
        vec![Err(IcechunkFormatError::from(
            icechunk::format::IcechunkFormatErrorKind::InvalidTimestamp,
        ))]
        .into_iter(),
    );
    assert!(failed.is_err());
    Ok(())
}

#[tokio::test]
async fn g05_snapshot_build__initial_snapshot() -> Res {
    let snap = Snapshot::initial()?;
    assert_eq!(snap.message(), Snapshot::INITIAL_COMMIT_MESSAGE);
    assert_eq!(Snapshot::INITIAL_COMMIT_MESSAGE, "Repository initialized");
    assert!(snap.parent_id().is_none());
    assert_eq!(snap.len(), 0);
    assert!(snap.is_empty());
    assert!(snap.metadata()?.contains_key("__root"));
    assert!(SnapshotInfo::try_from(&snap)?.is_initial());
    Ok(())
}

#[tokio::test]
async fn g05_snapshot_build__array_shape_and_dimension_names() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    use icechunk::format::snapshot::DimensionName;
    use icechunk::format::snapshot::DimensionShape;
    use icechunk::session::SessionErrorKind;
    mem_repo!();
    assert!(ArrayShape::new(vec![(10u64, 0u64)]).is_none());
    let shape = ArrayShape::new(vec![(10u64, 3), (20u64, 2)]).unwrap();
    assert!(shape.valid_chunk_coord(&ChunkIndices(vec![0, 0])));
    assert!(!shape.valid_chunk_coord(&ChunkIndices(vec![100, 0])));

    let d = DimensionShape::new(10, NonZeroU64::new(3).unwrap());
    assert_eq!(d.array_length(), 10);
    assert_eq!(d.chunk_length(), 3);

    assert_eq!(DimensionName::from(Some("x")), DimensionName::Name("x".to_string()));
    assert_eq!(DimensionName::from(None), DimensionName::NotSpecified);
    assert_eq!(DimensionName::from(""), DimensionName::NotSpecified);

    // the shape vocabulary carries serde derives of its own
    let name = DimensionName::from("t");
    assert_eq!(serde_json::from_str::<DimensionName>(&serde_json::to_string(&name)?)?, name);
    assert_eq!(serde_json::from_str::<DimensionShape>(&serde_json::to_string(&d)?)?, d);
    assert_eq!(
        serde_json::from_str::<ArrayShape>(&serde_json::to_string(&shape)?)?,
        shape
    );

    // a coordinate `valid_chunk_coord` rejects is what `set_chunk_ref` reports
    // as `SessionErrorKind::InvalidIndex`
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let p: Path = "/a".try_into()?;
    s.add_array(p.clone(), shape.clone(), None, Bytes::new()).await?;
    let err = s
        .set_chunk_ref(
            p.clone(),
            ChunkIndices(vec![100, 0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"x"))),
        )
        .await
        .unwrap_err();
    assert!(matches!(
        err,
        SessionError { kind: SessionErrorKind::InvalidIndex { .. }, .. }
    ));
    Ok(())
}

#[tokio::test]
async fn g05_snapshot_build__manifest_refs_and_extents() -> Res {
    use icechunk::format::manifest::ManifestExtents;
    use icechunk::format::snapshot::ArrayShape;
    snapshot_from!();
    let extents = ManifestExtents::new(&[0, 5], &[10, 20]);
    let ranges: Vec<_> = extents.iter().cloned().collect();
    assert_eq!(ranges, vec![0..10, 5..20]);

    let id = ManifestId::random();
    let nodes = vec![NodeSnapshot {
        path: Path::root(),
        id: NodeId::random(),
        user_data: Bytes::new(),
        node_data: NodeData::Array {
            shape: ArrayShape::new(vec![(10u64, 1), (20u64, 1)]).unwrap(),
            dimension_names: None,
            manifests: vec![ManifestRef { object_id: id.clone(), extents: extents.clone() }],
        },
    }];
    let snap = snapshot_from(nodes.clone(), vec![]);
    let back = Snapshot::from_buffer(snap.bytes().to_vec())?;
    match back.get_node(&Path::root())?.node_data {
        NodeData::Array { manifests, dimension_names, .. } => {
            assert_eq!(manifests[0].object_id, id);
            assert_eq!(manifests[0].extents, extents);
            assert_eq!(dimension_names, None);
        }
        NodeData::Group => panic!("expected an array node"),
    }

    // `ManifestFileInfo::new` reads the manifest it describes
    let manifest = sample_manifest().await;
    let info = ManifestFileInfo::new(&manifest, 4_096);
    assert_eq!(info.id, manifest.id());
    assert_eq!(info.num_chunk_refs as usize, manifest.len());
    assert_eq!(info.size_bytes, 4_096);

    // the node types carry serde derives, so a caller can move them across a
    // process boundary without going through a snapshot buffer
    let json = serde_json::to_string(&nodes[0])?;
    let round_tripped: NodeSnapshot = serde_json::from_str(&json)?;
    assert_eq!(round_tripped, nodes[0]);
    Ok(())
}

#[tokio::test]
async fn g05_snapshot_build__adopt_makes_the_receiver_the_parent() -> Res {
    sample_nodes!();
    snapshot_from!();
    let parent = snapshot_from(sample_nodes(), vec![]);
    let child = snapshot_from(sample_nodes(), vec![]);
    let adopted = parent.adopt(&child)?;
    assert_eq!(adopted.id(), child.id());
    assert_eq!(adopted.parent_id(), Some(parent.id()));
    assert_eq!(adopted.message(), child.message());
    assert_eq!(adopted.len(), child.len());
    Ok(())
}

// ===========================================================================
// g06_manifest_decode
// ===========================================================================

#[tokio::test]
async fn g06_manifest_decode__id_is_a_method() -> Res {
    let manifest = sample_manifest().await;
    let id = manifest.id();
    assert_eq!(manifest.id(), id);
    let back = Manifest::from_buffer(manifest.bytes().to_vec())?;
    assert_eq!(back.id(), id);
    Ok(())
}

#[tokio::test]
async fn g06_manifest_decode__len_and_is_empty() -> Res {
    let manifest = sample_manifest().await;
    assert_eq!(manifest.len(), 1);
    assert!(!manifest.is_empty());

    let node = NodeId::random();
    let many = Manifest::from_iter((0..5u32).map(|i| ChunkInfo {
        node: node.clone(),
        coord: ChunkIndices(vec![i]),
        payload: ChunkPayload::Inline(Bytes::from(vec![i as u8])),
    }))
    .await?
    .unwrap();
    assert_eq!(many.len(), 5);
    Ok(())
}

#[tokio::test]
async fn g06_manifest_decode__an_empty_manifest_is_none() -> Res {
    let empty = Manifest::from_iter(Vec::<ChunkInfo>::new()).await?;
    assert!(empty.is_none());
    // the streaming constructor agrees, and it propagates its stream's error
    let empty = Manifest::from_stream(futures::stream::iter(
        Vec::<Result<ChunkInfo, IcechunkFormatError>>::new(),
    ))
    .await?;
    assert!(empty.is_none());
    let failed = Manifest::from_stream(futures::stream::iter(vec![Err::<
        ChunkInfo,
        IcechunkFormatError,
    >(
        IcechunkFormatError::from(
            icechunk::format::IcechunkFormatErrorKind::InvalidMagicNumbers,
        ),
    )]))
    .await;
    assert!(failed.is_err());
    Ok(())
}

#[tokio::test]
async fn g06_manifest_decode__get_chunk_payload_by_value() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    let node = NodeId::random();
    let payload = ChunkPayload::Inline(Bytes::from_static(b"hi"));
    let manifest = Manifest::from_iter(vec![ChunkInfo {
        node: node.clone(),
        coord: ChunkIndices(vec![0, 1]),
        payload: payload.clone(),
    }])
    .await?
    .unwrap();
    assert_eq!(manifest.get_chunk_payload(&node, &ChunkIndices(vec![0, 1]))?, payload);

    let missing = manifest.get_chunk_payload(&node, &ChunkIndices(vec![9, 9]));
    assert!(matches!(
        missing,
        Err(IcechunkFormatError {
            kind: IcechunkFormatErrorKind::ChunkCoordinatesNotFound { .. },
            ..
        })
    ));
    Ok(())
}

#[tokio::test]
async fn g06_manifest_decode__iterators_yield_fallible_items() -> Res {
    let node = NodeId::random();
    let manifest = Arc::new(
        Manifest::from_iter((0..3u32).map(|i| ChunkInfo {
            node: node.clone(),
            coord: ChunkIndices(vec![i]),
            payload: ChunkPayload::Inline(Bytes::from(vec![i as u8])),
        }))
        .await?
        .unwrap(),
    );
    let all: Vec<(ChunkIndices, ChunkPayload)> =
        Arc::clone(&manifest).iter(node.clone()).collect::<Result<_, _>>()?;
    assert_eq!(all.len(), 3);
    assert_eq!(all[0].0, ChunkIndices(vec![0]));
    assert_eq!(all[2].0, ChunkIndices(vec![2]));

    let payloads: Vec<ChunkPayload> = manifest.chunk_payloads().collect::<Result<_, _>>()?;
    assert_eq!(payloads.len(), 3);

    // an unknown node simply yields nothing
    let none: Vec<_> = Arc::clone(&manifest).iter(NodeId::random()).collect::<Vec<_>>();
    assert!(none.is_empty());

    // `manifest.fbs` stores `Manifest.arrays` ascending by `ArrayManifest.node_id`
    // and each array's refs ascending by coordinate (section 6), whatever order
    // the input arrived in. Two fixed node ids make the order observable.
    let low = NodeId::new([1u8; 8]);
    let high = NodeId::new([9u8; 8]);
    let mk = |node: &NodeId, c: u32, tag: u8| ChunkInfo {
        node: node.clone(),
        coord: ChunkIndices(vec![c]),
        payload: ChunkPayload::Inline(Bytes::from(vec![tag])),
    };
    let two_arrays = Arc::new(
        Manifest::from_stream(futures::stream::iter(vec![
            Ok::<_, IcechunkFormatError>(mk(&high, 1, 13)),
            Ok(mk(&high, 0, 12)),
            Ok(mk(&low, 2, 11)),
            Ok(mk(&low, 0, 10)),
        ]))
        .await?
        .unwrap(),
    );
    assert_eq!(two_arrays.len(), 4);
    let stored: Vec<ChunkPayload> =
        two_arrays.chunk_payloads().collect::<Result<_, _>>()?;
    assert_eq!(
        stored,
        vec![
            ChunkPayload::Inline(Bytes::from(vec![10u8])),
            ChunkPayload::Inline(Bytes::from(vec![11u8])),
            ChunkPayload::Inline(Bytes::from(vec![12u8])),
            ChunkPayload::Inline(Bytes::from(vec![13u8])),
        ]
    );
    let low_coords: Vec<ChunkIndices> = Arc::clone(&two_arrays)
        .iter(low.clone())
        .map(|r| r.unwrap().0)
        .collect();
    assert_eq!(low_coords, vec![ChunkIndices(vec![0]), ChunkIndices(vec![2])]);
    // the accessors binary-search that order, so both arrays stay reachable
    assert_eq!(
        two_arrays.get_chunk_payload(&high, &ChunkIndices(vec![1]))?,
        ChunkPayload::Inline(Bytes::from(vec![13u8]))
    );
    assert_eq!(
        two_arrays.get_chunk_payload(&low, &ChunkIndices(vec![0]))?,
        ChunkPayload::Inline(Bytes::from(vec![10u8]))
    );
    Ok(())
}

#[tokio::test]
async fn g06_manifest_decode__three_reference_kinds_round_trip() -> Res {
    use icechunk::format::manifest::{Checksum, SecondsSinceEpoch};
    let node = NodeId::random();
    let inline = ChunkPayload::Inline(Bytes::from_static(b"inline-data"));
    let native = ChunkPayload::Ref(ChunkRef {
        id: ChunkId::random(),
        offset: 128,
        length: 42,
    });
    let virt = ChunkPayload::Virtual(VirtualChunkRef {
        location: VirtualChunkLocation::from_absolute_path("file:///data/set.nc")?,
        offset: 1024,
        length: 64,
        checksum: Some(Checksum::LastModified(SecondsSinceEpoch(1_700_000_000))),
    });
    // the other checksum kind: an entity tag rather than a modification time
    let etagged = ChunkPayload::Virtual(VirtualChunkRef {
        location: VirtualChunkLocation::from_absolute_path("file:///data/other.nc")?,
        offset: 0,
        length: 8,
        checksum: Some(Checksum::ETag(icechunk::storage::ETag("abc-123".to_string()))),
    });
    let manifest = Manifest::from_iter(vec![
        ChunkInfo { node: node.clone(), coord: ChunkIndices(vec![0]), payload: inline.clone() },
        ChunkInfo { node: node.clone(), coord: ChunkIndices(vec![1]), payload: native.clone() },
        ChunkInfo { node: node.clone(), coord: ChunkIndices(vec![2]), payload: virt.clone() },
        ChunkInfo { node: node.clone(), coord: ChunkIndices(vec![3]), payload: etagged.clone() },
    ])
    .await?
    .unwrap();
    let back = Manifest::from_buffer(manifest.bytes().to_vec())?;
    assert_eq!(back.get_chunk_payload(&node, &ChunkIndices(vec![0]))?, inline);
    assert_eq!(back.get_chunk_payload(&node, &ChunkIndices(vec![1]))?, native);
    assert_eq!(back.get_chunk_payload(&node, &ChunkIndices(vec![2]))?, virt);
    assert_eq!(back.get_chunk_payload(&node, &ChunkIndices(vec![3]))?, etagged);
    Ok(())
}

// ===========================================================================
// g07_transaction_log
// ===========================================================================

macro_rules! tx_fixture {
    () => {
async fn tx_fixture() -> (
    icechunk::Repository,
    icechunk::format::SnapshotId,
    icechunk::format::SnapshotId,
    icechunk::format::NodeId,
    icechunk::format::NodeId,
    icechunk::format::NodeId,
) {
    use bytes::Bytes;
    use icechunk::format::{
        manifest::ChunkPayload, snapshot::ArrayShape, ChunkIndices, Path,
    };
    let repo = icechunk::Repository::create(
        None,
        icechunk::new_in_memory_storage().await.unwrap(),
        std::collections::HashMap::new(),
    )
    .await
    .unwrap();
    let mut s = repo.writable_session("main").await.unwrap();
    s.add_group(Path::root(), Bytes::new()).await.unwrap();
    let gp: Path = "/g".try_into().unwrap();
    s.add_group(gp.clone(), Bytes::from_static(b"g")).await.unwrap();
    let ap: Path = "/arr".try_into().unwrap();
    s.add_array(
        ap.clone(),
        ArrayShape::new(vec![(4u64, 2)]).unwrap(),
        None,
        Bytes::from_static(b"a"),
    )
    .await
    .unwrap();
    let dp: Path = "/doomed".try_into().unwrap();
    s.add_array(
        dp.clone(),
        ArrayShape::new(vec![(4u64, 2)]).unwrap(),
        None,
        Bytes::new(),
    )
    .await
    .unwrap();
    let group_id = s.get_group(&gp).await.unwrap().id;
    let array_id = s.get_array(&ap).await.unwrap().id;
    let doomed_id = s.get_array(&dp).await.unwrap().id;
    let first = s.commit("create", None).await.unwrap();

    let mut s = repo.writable_session("main").await.unwrap();
    s.set_chunk_ref(
        ap.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"abc"))),
    )
    .await
    .unwrap();
    s.update_group(&gp, Bytes::from_static(b"g2")).await.unwrap();
    s.update_array(
        &ap,
        ArrayShape::new(vec![(8u64, 2)]).unwrap(),
        None,
        Bytes::from_static(b"a2"),
    )
    .await
    .unwrap();
    s.delete_array(dp).await.unwrap();
    let second = s.commit("update", None).await.unwrap();
    (repo, first, second, group_id, array_id, doomed_id)
}
    };
}

#[tokio::test]
async fn g07_transaction_log__created_nodes() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    tx_fixture!();
    let (repo, first, _, group_id, array_id, _) = tx_fixture().await;
    let tx = repo.asset_manager().fetch_transaction_log(&first).await?;
    let new_groups: HashSet<NodeId> = tx.new_groups().collect();
    let new_arrays: HashSet<NodeId> = tx.new_arrays().collect();
    assert!(new_groups.contains(&group_id));
    assert_eq!(new_groups.len(), 2); // the root group and /g
    assert!(new_arrays.contains(&array_id));
    assert_eq!(new_arrays.len(), 2);
    assert!(tx.group_created(&group_id));
    assert!(tx.array_created(&array_id));
    assert!(!tx.array_created(&group_id));

    // `transaction_log.fbs` stores the six node-id vectors ascending (section
    // 6) and the `*_created` predicates binary-search them. Four groups and
    // four arrays created in one commit make that order observable.
    let mut s = repo.writable_session("main").await?;
    let mut want_groups: Vec<NodeId> = Vec::new();
    let mut want_arrays: Vec<NodeId> = Vec::new();
    for i in 0..4 {
        let gp: Path = format!("/bulk-g{i}").as_str().try_into()?;
        s.add_group(gp.clone(), Bytes::new()).await?;
        want_groups.push(s.get_group(&gp).await?.id);
        let ap: Path = format!("/bulk-a{i}").as_str().try_into()?;
        s.add_array(ap.clone(), ArrayShape::new(vec![(4u64, 2)]).unwrap(), None, Bytes::new())
            .await?;
        want_arrays.push(s.get_array(&ap).await?.id);
    }
    let bulk = s.commit("bulk create", None).await?;
    let tx = repo.asset_manager().fetch_transaction_log(&bulk).await?;
    let groups: Vec<NodeId> = tx.new_groups().collect();
    let arrays: Vec<NodeId> = tx.new_arrays().collect();
    want_groups.sort();
    want_arrays.sort();
    assert_eq!(groups, want_groups, "new_groups must decode ascending");
    assert_eq!(arrays, want_arrays, "new_arrays must decode ascending");
    for id in want_groups.iter() {
        assert!(tx.group_created(id));
        assert!(!tx.array_created(id));
    }
    for id in want_arrays.iter() {
        assert!(tx.array_created(id));
    }
    Ok(())
}

#[tokio::test]
async fn g07_transaction_log__deleted_nodes() -> Res {
    tx_fixture!();
    let (repo, _, second, _, array_id, doomed_id) = tx_fixture().await;
    let tx = repo.asset_manager().fetch_transaction_log(&second).await?;
    let deleted: Vec<NodeId> = tx.deleted_arrays().collect();
    assert_eq!(deleted, vec![doomed_id.clone()]);
    assert_eq!(tx.deleted_groups().count(), 0);
    assert!(tx.array_deleted(&doomed_id));
    assert!(!tx.array_deleted(&array_id));
    assert!(!tx.group_deleted(&doomed_id));
    Ok(())
}

#[tokio::test]
async fn g07_transaction_log__updated_nodes() -> Res {
    tx_fixture!();
    let (repo, _, second, group_id, array_id, _) = tx_fixture().await;
    let tx = repo.asset_manager().fetch_transaction_log(&second).await?;
    let groups: Vec<NodeId> = tx.updated_groups().collect();
    let arrays: Vec<NodeId> = tx.updated_arrays().collect();
    assert_eq!(groups, vec![group_id.clone()]);
    assert_eq!(arrays, vec![array_id.clone()]);
    assert!(tx.group_updated(&group_id));
    assert!(tx.array_updated(&array_id));
    assert!(!tx.group_updated(&array_id));
    assert!(!tx.array_updated(&group_id));
    Ok(())
}

#[tokio::test]
async fn g07_transaction_log__updated_chunks() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    tx_fixture!();
    let (repo, _, second, _, array_id, _) = tx_fixture().await;
    let tx = repo.asset_manager().fetch_transaction_log(&second).await?;
    assert!(tx.chunks_updated(&array_id));
    let coords: Vec<ChunkIndices> = tx.updated_chunks_for(&array_id).collect();
    assert_eq!(coords, vec![ChunkIndices(vec![0])]);

    let all: Vec<(NodeId, Vec<ChunkIndices>)> =
        tx.updated_chunks().map(|(n, it)| (n, it.collect())).collect();
    assert_eq!(all.len(), 1);
    assert_eq!(all[0].0, array_id);
    assert_eq!(all[0].1, vec![ChunkIndices(vec![0])]);
    assert_eq!(tx.updated_chunks_for(&NodeId::random()).count(), 0);

    // `ArrayUpdatedChunks.chunks` is stored ascending lexicographically and the
    // per-array records ascending by node id (section 6), whatever order the
    // writes arrived in. `/arr2` is two-dimensional so "lexicographic" has
    // something to say.
    let mut s = repo.writable_session("main").await?;
    let p2: Path = "/arr2".try_into()?;
    s.add_array(
        p2.clone(),
        ArrayShape::new(vec![(4u64, 2), (6u64, 2)]).unwrap(),
        None,
        Bytes::new(),
    )
    .await?;
    let id2 = s.get_array(&p2).await?.id;
    for coord in [vec![1u32, 2], vec![0, 0], vec![1, 0], vec![0, 2]] {
        s.set_chunk_ref(
            p2.clone(),
            ChunkIndices(coord),
            Some(ChunkPayload::Inline(Bytes::from_static(b"z"))),
        )
        .await?;
    }
    let ap: Path = "/arr".try_into()?;
    for coord in [3u32, 1] {
        s.set_chunk_ref(
            ap.clone(),
            ChunkIndices(vec![coord]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"y"))),
        )
        .await?;
    }
    let third = s.commit("more chunks", None).await?;
    let tx = repo.asset_manager().fetch_transaction_log(&third).await?;
    assert_eq!(
        tx.updated_chunks_for(&id2).collect::<Vec<_>>(),
        vec![
            ChunkIndices(vec![0, 0]),
            ChunkIndices(vec![0, 2]),
            ChunkIndices(vec![1, 0]),
            ChunkIndices(vec![1, 2])
        ]
    );
    assert_eq!(
        tx.updated_chunks_for(&array_id).collect::<Vec<_>>(),
        vec![ChunkIndices(vec![1]), ChunkIndices(vec![3])]
    );
    let nodes: Vec<NodeId> = tx.updated_chunks().map(|(n, _)| n).collect();
    assert_eq!(nodes.len(), 2);
    let mut sorted = nodes.clone();
    sorted.sort();
    assert_eq!(nodes, sorted, "updated_chunks must be ascending by node id");
    // six coordinates in all, counted by `len()`
    assert_eq!(tx.len(), 7); // 4 + 2 chunk coords, plus the new array
    Ok(())
}

#[tokio::test]
async fn g07_transaction_log__len_and_is_empty() -> Res {
    tx_fixture!();
    let (repo, first, second, _, _, _) = tx_fixture().await;
    let created = repo.asset_manager().fetch_transaction_log(&first).await?;
    let updated = repo.asset_manager().fetch_transaction_log(&second).await?;
    // four created nodes in the first transaction
    assert_eq!(created.len(), 4);
    assert!(!created.is_empty());
    // one chunk, one group update, one array update, one deletion
    assert_eq!(updated.len(), 4);
    Ok(())
}

#[tokio::test]
async fn g07_transaction_log__buffer_round_trip() -> Res {
    tx_fixture!();
    let (repo, _, second, _, array_id, _) = tx_fixture().await;
    let tx = repo.asset_manager().fetch_transaction_log(&second).await?;
    let back = TransactionLog::from_buffer(tx.bytes().to_vec())?;
    assert_eq!(&back, tx.as_ref());
    assert!(back.chunks_updated(&array_id));
    assert_eq!(back.len(), tx.len());
    assert_eq!(back.clone(), back);

    // the same record can be built straight from a session's change set
    let mut s = repo.writable_session("main").await?;
    s.add_group("/fresh".try_into()?, Bytes::new()).await?;
    let built = TransactionLog::new(&SnapshotId::random(), s.changes());
    assert_eq!(built.len(), 1);
    assert_eq!(built.new_groups().count(), 1);
    assert_eq!(built.new_arrays().count(), 0);
    assert_eq!(TransactionLog::from_buffer(built.bytes().to_vec())?, built);

    // `Default` exists; nothing reads an accessor off a default value
    assert_eq!(TransactionLog::default(), TransactionLog::default());
    Ok(())
}

// ===========================================================================
// g08_corrupt_rejected - a truncated or corrupted object is a named error
// ===========================================================================

/// `IcechunkFormatError` became a struct (`ICError`) at this milestone, so the
/// struct pattern below only type-checks against this release.
macro_rules! assert_invalid_flatbuffer {
    () => {
        fn assert_invalid_flatbuffer<T: std::fmt::Debug>(
            r: Result<T, icechunk::format::IcechunkFormatError>,
        ) {
            use icechunk::format::{IcechunkFormatError, IcechunkFormatErrorKind};
            assert!(matches!(
                r,
                Err(IcechunkFormatError {
                    kind: IcechunkFormatErrorKind::InvalidFlatBuffer(_),
                    ..
                })
            ));
        }
    };
}

#[tokio::test]
async fn g08_corrupt_rejected__truncated_snapshot() -> Res {
    sample_nodes!();
    snapshot_from!();
    assert_invalid_flatbuffer!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let good = snap.bytes().to_vec();
    Snapshot::from_buffer(good.clone()).expect("an intact buffer must verify");
    for cut in [good.len() / 4, good.len() / 2, good.len() - 1] {
        assert_invalid_flatbuffer(Snapshot::from_buffer(good[..cut].to_vec()));
    }
    Ok(())
}

#[tokio::test]
async fn g08_corrupt_rejected__corrupt_root_offset() -> Res {
    sample_nodes!();
    snapshot_from!();
    assert_invalid_flatbuffer!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let good = snap.bytes().to_vec();
    let mut bad = good.clone();
    bad[0] = bad[0].wrapping_add(0x7f);
    assert_invalid_flatbuffer(Snapshot::from_buffer(bad));

    let mut bad = good;
    bad[3] = 0xff; // a wildly out-of-range root offset
    assert_invalid_flatbuffer(Snapshot::from_buffer(bad));
    Ok(())
}

#[tokio::test]
async fn g08_corrupt_rejected__empty_and_tiny_buffers() -> Res {
    assert_invalid_flatbuffer!();
    assert_invalid_flatbuffer(Snapshot::from_buffer(Vec::new()));
    assert_invalid_flatbuffer(Snapshot::from_buffer(vec![0u8; 3]));
    assert_invalid_flatbuffer(Manifest::from_buffer(Vec::new()));
    assert_invalid_flatbuffer(TransactionLog::from_buffer(vec![0u8; 7]));
    Ok(())
}

#[tokio::test]
async fn g08_corrupt_rejected__truncated_manifest() -> Res {
    assert_invalid_flatbuffer!();
    let manifest = sample_manifest().await;
    let good = manifest.bytes().to_vec();
    Manifest::from_buffer(good.clone()).expect("an intact buffer must verify");
    assert_invalid_flatbuffer(Manifest::from_buffer(good[..good.len() / 2].to_vec()));
    let mut bad = good;
    bad[0] = bad[0].wrapping_add(0x5b);
    assert_invalid_flatbuffer(Manifest::from_buffer(bad));
    Ok(())
}

#[tokio::test]
async fn g08_corrupt_rejected__truncated_transaction_log() -> Res {
    mem_repo!();
    commit_sample!();
    assert_invalid_flatbuffer!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let tx = repo.asset_manager().fetch_transaction_log(&snap_id).await?;
    let good = tx.bytes().to_vec();
    TransactionLog::from_buffer(good.clone()).expect("an intact buffer must verify");
    assert_invalid_flatbuffer(TransactionLog::from_buffer(good[..good.len() / 2].to_vec()));
    Ok(())
}

#[tokio::test]
async fn g08_corrupt_rejected__garbage_object_in_storage() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    mem_storage!();
    let st = mem_storage().await;
    let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
    let manifest = Arc::new(sample_manifest().await);
    let id = manifest.id();
    am.write_manifest(Arc::clone(&manifest)).await?;

    st.write_manifest(
        &settings(),
        id.clone(),
        vec![],
        Bytes::from_static(b"not an icechunk file"),
    )
    .await?;

    let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
    let err = am.fetch_manifest_unknown_size(&id).await.unwrap_err();
    assert!(matches!(
        err,
        RepositoryError {
            kind: RepositoryErrorKind::FormatError(
                IcechunkFormatErrorKind::InvalidMagicNumbers
            ),
            ..
        }
    ));
    Ok(())
}

// ===========================================================================
// g09_verify_once - verification runs once, before any accessor
// ===========================================================================

#[tokio::test]
async fn g09_verify_once__verified_buffer_accessors_do_not_panic() -> Res {
    sample_nodes!();
    snapshot_from!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let back = Snapshot::from_buffer(snap.bytes().to_vec())?;
    // every accessor on a verified buffer
    let _ = back.id();
    let _ = back.parent_id();
    let _ = back.message();
    let _ = back.metadata()?;
    let _ = back.flushed_at()?;
    assert_eq!(back.len(), 2);
    assert!(!back.is_empty());
    assert_eq!(back.iter().filter(|n| n.is_ok()).count(), 2);
    assert_eq!(back.manifest_files().count(), 0);
    assert!(back.get_node(&Path::root()).is_ok());
    Ok(())
}

#[tokio::test]
async fn g09_verify_once__deserialize_verifies_before_returning() -> Res {
    use icechunk::format::serializers::{deserialize_manifest, deserialize_snapshot};
    sample_nodes!();
    snapshot_from!();
    assert_invalid_flatbuffer!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let good = snap.bytes().to_vec();
    let bad = good[..good.len() / 2].to_vec();
    let r = deserialize_snapshot(SpecVersionBin::current(), Box::new(Cursor::new(bad)));
    assert_invalid_flatbuffer(r);

    let manifest = sample_manifest().await;
    let good = manifest.bytes().to_vec();
    let bad = good[..good.len() / 2].to_vec();
    let r = deserialize_manifest(SpecVersionBin::current(), Box::new(Cursor::new(bad)));
    assert_invalid_flatbuffer(r);
    Ok(())
}

#[tokio::test]
async fn g09_verify_once__file_identifier_is_written_but_not_verified() -> Res {
    sample_nodes!();
    snapshot_from!();
    let snap = snapshot_from(sample_nodes(), vec![]);
    let mut buf = snap.bytes().to_vec();
    assert_eq!(&buf[4..8], b"Ichk");
    buf[4..8].copy_from_slice(b"XXXX");
    let back = Snapshot::from_buffer(buf)?;
    assert_eq!(back.message(), snap.message());
    assert_eq!(back.len(), snap.len());
    assert_eq!(back.get_node(&"/b".try_into()?)?, snap.get_node(&"/b".try_into()?)?);
    Ok(())
}

#[tokio::test]
async fn g09_verify_once__a_manifest_payload_is_rejected_as_a_snapshot() -> Res {
    sample_nodes!();
    snapshot_from!();
    assert_invalid_flatbuffer!();
    // All three file types carry the same 4-byte identifier, so it cannot be
    // what discriminates them: the structural verification must.
    let manifest = sample_manifest().await;
    assert_eq!(&manifest.bytes()[4..8], b"Ichk");
    assert_invalid_flatbuffer(Snapshot::from_buffer(manifest.bytes().to_vec()));

    let snap = snapshot_from(sample_nodes(), vec![]);
    assert_invalid_flatbuffer(Manifest::from_buffer(snap.bytes().to_vec()));
    Ok(())
}

// ===========================================================================
// g10_spec1_rejected - the two diagnostics a spec-1 repository produces
// ===========================================================================

#[tokio::test]
async fn g10_spec1_rejected__sequence_numbered_branch_is_not_a_repository() -> Res {
    let dir = tempfile::TempDir::new()?;
    write_spec1_branch(dir.path());
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    assert!(!Repository::exists(st.as_ref()).await?);
    Ok(())
}

#[tokio::test]
async fn g10_spec1_rejected__open_reports_repository_doesnt_exist() -> Res {
    use icechunk::repository::RepositoryErrorKind;
    let dir = tempfile::TempDir::new()?;
    write_spec1_branch(dir.path());
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    let err = Repository::open(None, st, HashMap::new()).await.unwrap_err();
    assert!(matches!(
        err,
        RepositoryError { kind: RepositoryErrorKind::RepositoryDoesntExist, .. }
    ));
    Ok(())
}

#[tokio::test]
async fn g10_spec1_rejected__this_release_writes_branch_ref_json() -> Res {
    let dir = tempfile::TempDir::new()?;
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, Arc::clone(&st), HashMap::new()).await?;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    s.commit("c", None).await?;

    let branch_dir = dir.path().join("refs").join("branch.main");
    assert!(branch_dir.join("ref.json").is_file());
    let entries: Vec<String> = std::fs::read_dir(&branch_dir)?
        .map(|e| e.unwrap().file_name().to_string_lossy().into_owned())
        .collect();
    assert_eq!(entries, vec!["ref.json".to_string()]);
    assert!(Repository::exists(st.as_ref()).await?);
    Ok(())
}

#[tokio::test]
async fn g10_spec1_rejected__tag_layout_is_unchanged() -> Res {
    let dir = tempfile::TempDir::new()?;
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, st, HashMap::new()).await?;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let id = s.commit("c", None).await?;
    repo.create_tag("v1", &id).await?;

    // tags kept the spec-1 `ref.json` file name; only branches moved
    assert!(dir.path().join("refs").join("tag.v1").join("ref.json").is_file());
    assert_eq!(repo.lookup_tag("v1").await?, id);
    Ok(())
}

#[tokio::test]
async fn g10_spec1_rejected__spec1_metadata_payload_is_invalid_flatbuffer() -> Res {
    assert_invalid_flatbuffer!();
    // A spec-1 metadata file clears the header check (byte 36 is still 1) and
    // is then rejected by the FlatBuffers verification of its payload.
    assert_eq!(SpecVersionBin::current() as u8, 1);
    assert_invalid_flatbuffer(Snapshot::from_buffer(MSGPACK_LOOKING_PAYLOAD.to_vec()));
    assert_invalid_flatbuffer(Manifest::from_buffer(MSGPACK_LOOKING_PAYLOAD.to_vec()));
    assert_invalid_flatbuffer(TransactionLog::from_buffer(
        MSGPACK_LOOKING_PAYLOAD.to_vec(),
    ));
    Ok(())
}

// ===========================================================================
// g11_zarr_extensions - unknown Zarr metadata fields survive a round trip
// ===========================================================================

const EXTENDED_ARRAY_METADATA: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"],"my_extension":{"anything":[1,2,3]}}"#;

async fn store_on(repo: &Repository) -> Store {
    Store::from_session(Arc::new(RwLock::new(
        repo.writable_session("main").await.unwrap(),
    )))
    .await
}

const ROOT_GROUP_METADATA: &[u8] = br#"{"zarr_format":3,"node_type":"group"}"#;

async fn seed_extended_array(repo: &Repository) -> Store {
    let store = store_on(repo).await;
    store
        .set("zarr.json", Bytes::copy_from_slice(ROOT_GROUP_METADATA))
        .await
        .unwrap();
    store
        .set("array/zarr.json", Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA))
        .await
        .unwrap();
    store
}

#[tokio::test]
async fn g11_zarr_extensions__unknown_fields_survive_before_commit() -> Res {
    mem_repo!();
    let repo = mem_repo().await;
    let store = seed_extended_array(&repo).await;
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA)
    );
    Ok(())
}

#[tokio::test]
async fn g11_zarr_extensions__unknown_fields_survive_a_commit() -> Res {
    mem_repo!();
    let repo = mem_repo().await;
    let store = seed_extended_array(&repo).await;
    store.session().write().await.commit("meta", None).await?;

    let store2 = store_on(&repo).await;
    assert_eq!(
        store2.get("array/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA)
    );

    // The document also survives the store being carried across a process
    // boundary as bytes and rebuilt. `as_bytes` records how to reach the
    // backend, so this is done on a repository a fresh process could reopen.
    let dir = tempfile::TempDir::new()?;
    let disk =
        Repository::create(None, new_local_filesystem_storage(dir.path()).await?, HashMap::new())
            .await?;
    let seeded = seed_extended_array(&disk).await;
    seeded.session().write().await.commit("meta", None).await?;
    let moved = Store::from_bytes(store_on(&disk).await.as_bytes().await?)?;
    assert_eq!(
        moved.get("array/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA)
    );
    let configured = Store::from_session_and_config(moved.session(), 4);
    assert_eq!(
        configured.get("array/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA)
    );
    Ok(())
}

#[tokio::test]
async fn g11_zarr_extensions__group_metadata_survives() -> Res {
    mem_repo!();
    let repo = mem_repo().await;
    let store = store_on(&repo).await;
    let group = Bytes::from_static(
        br#"{"zarr_format":3,"node_type":"group","attributes":{"a":1},"unknown_key":[1,2]}"#,
    );
    store.set("zarr.json", group.clone()).await?;
    store.session().write().await.commit("g", None).await?;
    let store2 = store_on(&repo).await;
    assert_eq!(store2.get("zarr.json", &ByteRange::ALL).await?, group);
    Ok(())
}

#[tokio::test]
async fn g11_zarr_extensions__snapshot_user_data_is_the_original_bytes() -> Res {
    mem_repo!();
    let repo = mem_repo().await;
    let store = seed_extended_array(&repo).await;
    let snap_id = store.session().write().await.commit("meta", None).await?;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let node = snap.get_node(&"/array".try_into()?)?;
    assert_eq!(node.user_data, Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA));
    Ok(())
}

#[tokio::test]
async fn g11_zarr_extensions__known_fields_still_drive_the_node_data() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    use icechunk::format::snapshot::DimensionName;
    mem_repo!();
    let repo = mem_repo().await;
    let store = seed_extended_array(&repo).await;
    let snap_id = store.session().write().await.commit("meta", None).await?;
    let snap = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    match snap.get_node(&"/array".try_into()?)?.node_data {
        NodeData::Array { shape, dimension_names, .. } => {
            assert_eq!(shape, ArrayShape::new(vec![(2u64, 1), (2, 1), (2, 1)]).unwrap());
            assert_eq!(
                dimension_names,
                Some(vec![
                    DimensionName::from("x"),
                    DimensionName::from("y"),
                    DimensionName::from("t")
                ])
            );
        }
        NodeData::Group => panic!("/array must be an array node"),
    }
    Ok(())
}

#[tokio::test]
async fn g11_zarr_extensions__getsize_measures_the_stored_bytes() -> Res {
    use icechunk::store::StoreErrorKind;
    mem_repo!();
    let repo = mem_repo().await;
    let store = seed_extended_array(&repo).await;
    store.set("array/c/0/0/0", Bytes::from_static(b"0123456789")).await?;
    store.set("array/c/1/1/1", Bytes::from_static(b"abcd")).await?;
    // a SECOND array whose name merely begins with "array": the prefix rule is
    // whole-path-component, so nothing under it counts towards "array"
    let sibling = Bytes::copy_from_slice(EXTENDED_ARRAY_METADATA);
    store.set("array-2/zarr.json", sibling.clone()).await?;
    store.set("array-2/c/0/0/0", Bytes::from_static(b"ZZZZZZZ")).await?;

    // a metadata key measures the document the caller stored, byte for byte -
    // nothing is re-serialised on the way in or out
    assert_eq!(
        store.getsize("array/zarr.json").await?,
        EXTENDED_ARRAY_METADATA.len() as u64
    );
    assert_eq!(store.getsize("zarr.json").await?, ROOT_GROUP_METADATA.len() as u64);
    assert_eq!(store.getsize("array/c/0/0/0").await?, 10);
    assert_eq!(store.getsize("array/c/1/1/1").await?, 4);

    // a prefix sums every metadata and chunk key under it, matched a whole path
    // component at a time: `array` covers `array/zarr.json` and `array/c/0/0/0`
    // but NOT `array-2/...`, even though the key text starts with it
    assert_eq!(
        store.getsize_prefix("array").await?,
        EXTENDED_ARRAY_METADATA.len() as u64 + 14
    );
    assert_eq!(
        store.getsize_prefix("array-2").await?,
        EXTENDED_ARRAY_METADATA.len() as u64 + 7
    );
    // ... and the empty prefix is the whole store, both arrays included
    assert_eq!(
        store.getsize_prefix("").await?,
        2 * EXTENDED_ARRAY_METADATA.len() as u64
            + ROOT_GROUP_METADATA.len() as u64
            + 14
            + 7
    );

    // The listing family answers the same question by key rather than by size,
    // and agrees about which keys sit under `array`.
    let mut under: Vec<String> = store.list_prefix("array/").await?.try_collect().await?;
    under.sort();
    assert_eq!(
        under,
        vec![
            "array/c/0/0/0".to_string(),
            "array/c/1/1/1".to_string(),
            "array/zarr.json".to_string()
        ]
    );
    let all: Vec<String> = store.list().await?.try_collect().await?;
    assert_eq!(all.len(), 6);
    let mut top: Vec<String> = store.list_dir("").await?.try_collect().await?;
    top.sort();
    assert_eq!(
        top,
        vec!["array".to_string(), "array-2".to_string(), "zarr.json".to_string()]
    );
    assert_eq!(store.list_dir_items("array/").await?.try_collect::<Vec<_>>().await?.len(), 2);

    // exists / getsize / delete / delete_dir / is_empty / clear
    assert!(store.exists("array/c/0/0/0").await?);
    assert!(!store.is_empty("array").await?);
    store.delete("array/c/0/0/0").await?;
    assert!(!store.exists("array/c/0/0/0").await?);
    assert_eq!(
        store.getsize_prefix("array").await?,
        EXTENDED_ARRAY_METADATA.len() as u64 + 4
    );
    store.delete_dir("array-2").await?;
    assert!(store.is_empty("array-2").await?);
    // a prefix that matches nothing is a no-op rather than an error, and it
    // takes nothing else with it (section 4.12)
    store.delete_dir("no-such-prefix").await?;
    assert!(store.is_empty("no-such-prefix").await?);
    assert!(store.exists("array/c/1/1/1").await?);
    assert!(store.exists("array/zarr.json").await?);
    // a chunk key that was never written is a named absence, not a panic
    let err = store.get("array/c/5/5/5", &ByteRange::ALL).await.unwrap_err();
    assert!(matches!(err.kind, StoreErrorKind::NotFound(_)));
    // ... and so is a metadata key whose node does not exist: the same kind,
    // never a zero-length success (section 4.12)
    let err = store.get("no-such-node/zarr.json", &ByteRange::ALL).await.unwrap_err();
    assert!(matches!(err.kind, StoreErrorKind::NotFound(_)));
    // `set_if_not_exists` leaves an occupied key alone and reports NO error
    // while doing it, so the write is silently declined rather than refused
    store.set_if_not_exists("array/c/1/1/1", Bytes::from_static(b"NO")).await?;
    assert_eq!(
        store.get("array/c/1/1/1", &ByteRange::ALL).await?,
        Bytes::from_static(b"abcd")
    );
    // a batch of reads answers per key, each one fallible on its own
    let batch = store
        .get_partial_values(vec![
            ("array/c/1/1/1".to_string(), ByteRange::ALL),
            ("array/c/9/9/9".to_string(), ByteRange::ALL),
        ])
        .await?;
    assert_eq!(batch.len(), 2);
    assert_eq!(batch[0].as_ref().unwrap(), &Bytes::from_static(b"abcd"));
    assert!(batch[1].is_err());
    store.clear().await?;
    assert!(store.is_empty("").await?);
    Ok(())
}

// ===========================================================================
// g12_bulk_virtual_refs - Store::set_virtual_refs
// ===========================================================================

macro_rules! virtual_fixture {
    () => {
        async fn virtual_fixture(
            repo: &icechunk::Repository,
        ) -> (icechunk::Store, icechunk::format::Path) {
            use bytes::Bytes;
            use icechunk::format::{snapshot::ArrayShape, Path};
            use icechunk::Store;
            use std::sync::Arc;
            use tokio::sync::RwLock;
            let mut s = repo.writable_session("main").await.unwrap();
            s.add_group(Path::root(), Bytes::new()).await.unwrap();
            let p: Path = "/array".try_into().unwrap();
            s.add_array(
                p.clone(),
                ArrayShape::new(vec![(3u64, 1)]).unwrap(),
                None,
                Bytes::from_static(
                    br#"{"zarr_format":3,"node_type":"array","shape":[3],"chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1]}}}"#,
                ),
            )
            .await
            .unwrap();
            (Store::from_session(Arc::new(RwLock::new(s))).await, p)
        }
    };
}

fn file_ref(url: &str, offset: u64, length: u64) -> VirtualChunkRef {
    VirtualChunkRef {
        location: VirtualChunkLocation::from_absolute_path(url).unwrap(),
        offset,
        length,
        checksum: None,
    }
}

#[tokio::test]
async fn g12_bulk_virtual_refs__bulk_write_without_validation() -> Res {
    use icechunk::store::SetVirtualRefsResult;
    mem_repo!();
    virtual_fixture!();
    let repo = mem_repo().await;
    let (store, p) = virtual_fixture(&repo).await;
    let refs: Vec<(ChunkIndices, VirtualChunkRef)> = (0u64..3)
        .map(|i| {
            (ChunkIndices(vec![i as u32]), file_ref("file:///tmp/data.bin", i * 4, 4))
        })
        .collect();
    assert_eq!(store.set_virtual_refs(&p, false, refs).await?, SetVirtualRefsResult::Done);
    for i in 0..3 {
        assert!(store.exists(&format!("array/c/{i}")).await?);
    }
    Ok(())
}

#[tokio::test]
async fn g12_bulk_virtual_refs__failed_validation_returns_indices() -> Res {
    use icechunk::store::SetVirtualRefsResult;
    mem_repo!();
    virtual_fixture!();
    let repo = mem_repo().await;
    let (store, p) = virtual_fixture(&repo).await;
    let bad = vec![(
        ChunkIndices(vec![0]),
        VirtualChunkRef {
            location: VirtualChunkLocation("weird://x/y".to_string()),
            offset: 0,
            length: 1,
            checksum: None,
        },
    )];
    assert_eq!(
        store.set_virtual_refs(&p, true, bad).await?,
        SetVirtualRefsResult::FailedRefs(vec![ChunkIndices(vec![0])])
    );
    // nothing was written for the rejected index
    assert!(!store.exists("array/c/0").await?);
    Ok(())
}

#[tokio::test]
async fn g12_bulk_virtual_refs__mixed_batch_applies_the_valid_refs() -> Res {
    use icechunk::store::SetVirtualRefsResult;
    mem_repo!();
    virtual_fixture!();
    let repo = mem_repo().await;
    let (store, p) = virtual_fixture(&repo).await;
    let refs = vec![
        (ChunkIndices(vec![0]), file_ref("file:///tmp/data.bin", 0, 4)),
        (
            ChunkIndices(vec![1]),
            VirtualChunkRef {
                location: VirtualChunkLocation("weird://x/y".to_string()),
                offset: 0,
                length: 4,
                checksum: None,
            },
        ),
        (ChunkIndices(vec![2]), file_ref("file:///tmp/data.bin", 8, 4)),
    ];
    assert_eq!(
        store.set_virtual_refs(&p, true, refs).await?,
        SetVirtualRefsResult::FailedRefs(vec![ChunkIndices(vec![1])])
    );
    assert!(store.exists("array/c/0").await?);
    assert!(!store.exists("array/c/1").await?);
    assert!(store.exists("array/c/2").await?);
    Ok(())
}

#[tokio::test]
async fn g12_bulk_virtual_refs__read_only_store_is_rejected() -> Res {
    use icechunk::store::StoreErrorKind;
    mem_repo!();
    virtual_fixture!();
    let repo = mem_repo().await;
    let (store, p) = virtual_fixture(&repo).await;
    store.session().write().await.commit("c", None).await?;
    let ro = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?,
    )))
    .await;
    assert!(ro.read_only().await);
    let err = ro
        .set_virtual_refs(&p, false, vec![(ChunkIndices(vec![0]), file_ref("file:///tmp/d", 0, 1))])
        .await
        .unwrap_err();
    assert!(matches!(err, StoreError { kind: StoreErrorKind::ReadOnly, .. }));
    Ok(())
}

#[tokio::test]
async fn g12_bulk_virtual_refs__references_resolve_after_commit() -> Res {
    use icechunk::store::SetVirtualRefsResult;
    mem_repo!();
    virtual_fixture!();
    let dir = tempfile::TempDir::new()?;
    let data_path = dir.path().join("data.bin");
    std::fs::write(&data_path, b"0123456789ab")?;
    let url = format!("file://{}", data_path.display());

    let repo = mem_repo().await;
    let (store, p) = virtual_fixture(&repo).await;
    let refs: Vec<(ChunkIndices, VirtualChunkRef)> =
        (0u64..3).map(|i| (ChunkIndices(vec![i as u32]), file_ref(&url, i * 4, 4))).collect();
    assert_eq!(store.set_virtual_refs(&p, true, refs).await?, SetVirtualRefsResult::Done);
    store.session().write().await.commit("virtual", None).await?;

    let ro = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?,
    )))
    .await;
    assert_eq!(ro.get("array/c/0", &ByteRange::ALL).await?, Bytes::from_static(b"0123"));
    assert_eq!(ro.get("array/c/2", &ByteRange::ALL).await?, Bytes::from_static(b"89ab"));
    Ok(())
}

#[tokio::test]
async fn g12_bulk_virtual_refs__agrees_with_the_single_reference_setter() -> Res {
    use icechunk::format::manifest::VirtualReferenceErrorKind;
    use icechunk::session::SessionErrorKind;
    use icechunk::store::StoreErrorKind;
    mem_repo!();
    virtual_fixture!();
    let repo = mem_repo().await;
    let (store, p) = virtual_fixture(&repo).await;
    store
        .set_virtual_ref("array/c/0", file_ref("file:///tmp/data.bin", 0, 4), false)
        .await?;
    let single = store.session().read().await.get_chunk_ref(&p, &ChunkIndices(vec![0])).await?;

    let repo2 = mem_repo().await;
    let (store2, p2) = virtual_fixture(&repo2).await;
    store2
        .set_virtual_refs(
            &p2,
            false,
            vec![(ChunkIndices(vec![0]), file_ref("file:///tmp/data.bin", 0, 4))],
        )
        .await?;
    let bulk = store2.session().read().await.get_chunk_ref(&p2, &ChunkIndices(vec![0])).await?;
    assert_eq!(single, bulk);
    assert!(matches!(bulk, Some(ChunkPayload::Virtual(_))));

    // Where the bulk setter COLLECTS a reference no container matches, the
    // single setter fails outright - that is the difference point 3 of section
    // 4.12 draws between them.
    let err = store
        .set_virtual_ref(
            "array/c/1",
            VirtualChunkRef {
                location: VirtualChunkLocation("weird://x/y".to_string()),
                offset: 0,
                length: 4,
                checksum: None,
            },
            true,
        )
        .await
        .unwrap_err();
    assert!(matches!(
        &err.kind,
        StoreErrorKind::InvalidVirtualChunkContainer { chunk_location }
            if chunk_location == "weird://x/y"
    ));
    assert!(!store.exists("array/c/1").await?);

    // and a reference stored WITHOUT validation is only refused when something
    // tries to read through it
    store
        .set_virtual_ref(
            "array/c/2",
            VirtualChunkRef {
                location: VirtualChunkLocation("weird://x/y".to_string()),
                offset: 0,
                length: 4,
                checksum: None,
            },
            false,
        )
        .await?;
    let err = store.get("array/c/2", &ByteRange::ALL).await.unwrap_err();
    assert!(matches!(
        err.kind,
        StoreErrorKind::SessionError(SessionErrorKind::VirtualReferenceError(
            VirtualReferenceErrorKind::NoContainerForUrl(_)
        ))
    ));
    Ok(())
}

// ===========================================================================
// g13_generated_types - the generated FlatBuffers module and its bridges
// ===========================================================================

#[tokio::test]
async fn g13_generated_types__object_id_structs() -> Res {
    use icechunk::format::flatbuffers::gen;
    let eight = gen::ObjectId8::new(&[1, 2, 3, 4, 5, 6, 7, 8]);
    assert_eq!(eight.0, [1, 2, 3, 4, 5, 6, 7, 8]);
    let twelve = gen::ObjectId12::new(&[9u8; 12]);
    assert_eq!(twelve.0, [9u8; 12]);
    assert_eq!(gen::ObjectId8::default().0, [0u8; 8]);
    Ok(())
}

#[tokio::test]
async fn g13_generated_types__node_id_bridge() -> Res {
    use icechunk::format::flatbuffers::gen;
    let raw = [1u8, 2, 3, 4, 5, 6, 7, 8];
    let id = NodeId::from(&gen::ObjectId8::new(&raw));
    assert_eq!(id, NodeId::new(raw));
    assert_eq!(id.0, raw);
    Ok(())
}

#[tokio::test]
async fn g13_generated_types__manifest_id_bridge() -> Res {
    use icechunk::format::flatbuffers::gen;
    let raw = [7u8; 12];
    let id = ManifestId::from(&gen::ObjectId12::new(&raw));
    assert_eq!(id, ManifestId::new(raw));
    assert_eq!(gen::ObjectId12::default().0, [0u8; 12]);
    // the same 12-byte struct also bridges to the attributes-id family
    let attrs = icechunk::format::AttributesId::from(&gen::ObjectId12::new(&raw));
    assert_eq!(attrs, icechunk::format::AttributesId::new(raw));
    Ok(())
}

#[tokio::test]
async fn g13_generated_types__node_data_union_tags() -> Res {
    use icechunk::format::flatbuffers::gen;
    assert_eq!(gen::NodeData::NONE.0, 0);
    assert_eq!(gen::NodeData::Array.0, 1);
    assert_eq!(gen::NodeData::Group.0, 2);
    Ok(())
}

#[tokio::test]
async fn g13_generated_types__dimension_shape_and_index_range() -> Res {
    use icechunk::format::flatbuffers::gen;
    use icechunk::format::snapshot::DimensionShape;
    let gen_shape = gen::DimensionShape::new(10, 3);
    let shape = DimensionShape::from(&gen_shape);
    assert_eq!(shape.array_length(), 10);
    assert_eq!(shape.chunk_length(), 3);

    let range = gen::ChunkIndexRange::new(0, 5);
    assert_eq!(range.from(), 0);
    assert_eq!(range.to(), 5);

    // `ManifestFileInfo` is a fixed-size struct in `snapshot.fbs` too, and it
    // bridges to the domain type of section 4.4
    let gen_info = gen::ManifestFileInfo::new(&gen::ObjectId12::new(&[5u8; 12]), 99, 7);
    assert_eq!(gen_info.size_bytes(), 99);
    assert_eq!(gen_info.num_chunk_refs(), 7);
    let info = ManifestFileInfo::from(&gen_info);
    assert_eq!(info.id, ManifestId::new([5u8; 12]));
    assert_eq!(info.size_bytes, 99);
    assert_eq!(info.num_chunk_refs, 7);
    Ok(())
}

// ===========================================================================
// g14_error_kinds - the ICError split
// ===========================================================================

#[tokio::test]
async fn g14_error_kinds__repository_error_carries_a_kind_and_a_span() -> Res {
    use icechunk::repository::RepositoryErrorKind;
    mem_storage!();
    let st = mem_storage().await;
    let err = Repository::open(None, st, HashMap::new()).await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::RepositoryDoesntExist));
    assert!(matches!(err.kind(), RepositoryErrorKind::RepositoryDoesntExist));
    let _span = err.span();
    Ok(())
}

#[tokio::test]
async fn g14_error_kinds__session_error_kind_matching() -> Res {
    use icechunk::session::SessionErrorKind;
    mem_repo!();
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    s.commit("c", None).await?;

    let mut ro = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let err = ro.commit("nope", None).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::ReadOnlySession));
    assert!(matches!(
        err,
        SessionError { kind: SessionErrorKind::ReadOnlySession, .. }
    ));

    // `commit` is not the only refusal: section 4.11 names three operations
    // that publish a session's work, and the other two answer the same way -
    // `rebase`, and `merge`, the coordinator half of the distributed-write
    // protocol
    let err = ro.rebase(&ConflictDetector).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::ReadOnlySession));
    let mut donor = repo.writable_session("main").await?;
    donor.add_group("/donated".try_into()?, Bytes::new()).await?;
    let donated = donor.discard_changes();
    let err = ro.merge(donated).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::ReadOnlySession));
    Ok(())
}

#[tokio::test]
async fn g14_error_kinds__format_error_is_constructible_from_its_kind() -> Res {
    use icechunk::error::ICError;
    use icechunk::format::IcechunkFormatErrorKind;
    let e: IcechunkFormatError =
        IcechunkFormatError::from(IcechunkFormatErrorKind::InvalidMagicNumbers);
    assert!(matches!(e.kind, IcechunkFormatErrorKind::InvalidMagicNumbers));
    let e2 = ICError::new(IcechunkFormatErrorKind::InvalidSpecVersion);
    assert!(matches!(e2.kind(), IcechunkFormatErrorKind::InvalidSpecVersion));

    // `with_context` builds the same carrier around a span taken from
    // elsewhere; the span's CONTENTS are never asserted, only that it is there
    let carried = ICError::with_context(
        IcechunkFormatErrorKind::InvalidCompressionAlgorithm,
        e.span().clone(),
    );
    assert!(matches!(carried.kind, IcechunkFormatErrorKind::InvalidCompressionAlgorithm));
    let _ = &carried.context;
    let _ = carried.span();
    Ok(())
}

#[tokio::test]
async fn g14_error_kinds__error_source_is_the_kind() -> Res {
    mem_storage!();
    use std::error::Error as _;
    let st = mem_storage().await;
    let err = Repository::open(None, st, HashMap::new()).await.unwrap_err();
    assert!(err.source().is_some());
    Ok(())
}

#[tokio::test]
async fn g14_error_kinds__wrapper_variants_carry_kinds() -> Res {
    use icechunk::format::IcechunkFormatErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    use icechunk::store::StoreErrorKind;
    mem_storage!();
    mem_repo!();
    // RepositoryErrorKind::FormatError wraps an IcechunkFormatErrorKind, not an
    // IcechunkFormatError.
    let st = mem_storage().await;
    let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
    let id = SnapshotId::random();
    let mut object = current_header(FileTypeBin::Snapshot);
    object.extend_from_slice(&ZSTD_FRAME_MAGIC);
    object[0] ^= 0xff;
    st.write_snapshot(&settings(), id.clone(), vec![], Bytes::from(object)).await?;
    let err = am.fetch_snapshot(&id).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::FormatError(IcechunkFormatErrorKind::InvalidMagicNumbers)
    ));

    // StoreErrorKind wraps a SessionErrorKind the same way
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    s.commit("c", None).await?;
    let store = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?,
    )))
    .await;
    let err = store.set("zarr.json", Bytes::from_static(b"{}")).await.unwrap_err();
    assert!(matches!(err.kind, StoreErrorKind::ReadOnly));

    // `StorageError` is the seventh alias, and the crate root re-exports the
    // storage and config surface section 4.1 lists
    let missing: Result<_, icechunk::StorageError> =
        st.fetch_snapshot(&settings(), &SnapshotId::random()).await;
    assert!(missing.is_err());
    let _: Option<icechunk::ObjectStorage> = None;
    let cfg: icechunk::RepositoryConfig = Default::default();
    assert_eq!(cfg.inline_chunk_threshold_bytes(), 512);
    assert!(matches!(
        icechunk::ObjectStoreConfig::LocalFileSystem(std::path::PathBuf::from("/tmp")),
        icechunk::ObjectStoreConfig::LocalFileSystem(_)
    ));
    Ok(())
}

/// The documented rejections `Path` and `Session` produce, asserted by category.
/// Sections 4.3 and 4.11 state each one.
#[tokio::test]
async fn g14_error_kinds__path_and_node_rejections() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    use icechunk::format::PathError;
    use icechunk::session::SessionErrorKind;
    mem_repo!();

    // `Path::new` accepts only absolute, canonic paths, and `TryFrom<&str>` is
    // the same function
    assert!(matches!(Path::new("relative/array"), Err(PathError::NotAbsolute)));
    assert!(matches!(Path::new("/a/../b"), Err(PathError::NotCanonic)));
    let from_str: Result<Path, PathError> = "no-leading-slash".try_into();
    assert!(matches!(from_str, Err(PathError::NotAbsolute)));

    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;

    // a freshly created repository holds no nodes at all, so nothing is an
    // ancestor of anything
    let orphan: Path = "/nope/deeper".try_into()?;
    let err = s.get_closest_ancestor_node(&orphan).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::AncestorNodeNotFound { .. }));

    // and a session with no changes has nothing to commit
    let err = s.commit("empty", None).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NoChangesToCommit));

    s.add_group(Path::root(), Bytes::new()).await?;
    let gp: Path = "/g".try_into()?;
    s.add_group(gp.clone(), Bytes::new()).await?;
    let ap: Path = "/arr".try_into()?;
    s.add_array(ap.clone(), ArrayShape::new(vec![(4u64, 2)]).unwrap(), None, Bytes::new())
        .await?;

    // the node accessors are typed
    let err = s.get_array(&gp).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NotAnArray { .. }));
    let err = s.get_group(&ap).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NotAGroup { .. }));

    // a path that already holds a node cannot be created again, whichever kind
    let err = s.add_group(gp.clone(), Bytes::new()).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::AlreadyExists { .. }));
    let err = s
        .add_array(gp.clone(), ArrayShape::new(vec![(4u64, 2)]).unwrap(), None, Bytes::new())
        .await
        .unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::AlreadyExists { .. }));

    // the positive half: the deepest existing ancestor is what is reported
    let deep: Path = "/g/inner/leaf".try_into()?;
    assert_eq!(s.get_closest_ancestor_node(&deep).await?.path, gp);
    Ok(())
}

/// A reference or a snapshot that does not exist is a named error, not a panic
/// and not an empty result. Section 4.10 states both.
#[tokio::test]
async fn g14_error_kinds__missing_reference_and_snapshot() -> Res {
    use icechunk::refs::RefErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    mem_repo!();
    let repo = mem_repo().await;

    let err = repo.lookup_tag("nope").await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))));
    let err = repo.lookup_branch("nope").await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))));
    let err = repo
        .readonly_session(&VersionInfo::TagRef("nope".to_string()))
        .await
        .unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))));
    let err = repo
        .readonly_session(&VersionInfo::BranchTipRef("nope".to_string()))
        .await
        .unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))));

    // a well-formed id that names no snapshot object
    let missing = SnapshotId::random();
    let err = repo
        .readonly_session(&VersionInfo::SnapshotId(missing.clone()))
        .await
        .unwrap_err();
    assert!(matches!(
        &err.kind,
        RepositoryErrorKind::SnapshotNotFound { id } if id == &missing
    ));
    Ok(())
}

// ===========================================================================
// g15_diff_and_ancestry
// ===========================================================================

macro_rules! five_commits {
    () => {
        async fn five_commits(
            repo: &icechunk::Repository,
        ) -> Vec<icechunk::format::SnapshotId> {
            use bytes::Bytes;
            let mut ids = Vec::new();
            for i in 0..5 {
                let mut s = repo.writable_session("main").await.unwrap();
                s.add_group(format!("/g{i}").as_str().try_into().unwrap(), Bytes::new())
                    .await
                    .unwrap();
                ids.push(s.commit(&format!("commit {i}"), None).await.unwrap());
            }
            ids
        }
    };
}

#[tokio::test]
async fn g15_diff_and_ancestry__ancestry_can_be_abandoned_early() -> Res {
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    five_commits(&repo).await;
    let head: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .take(2)
        .try_collect()
        .await?;
    assert_eq!(head.len(), 2);
    assert_eq!(head[0].message, "commit 4");
    assert_eq!(head[1].message, "commit 3");
    assert!(!head[0].is_initial());
    Ok(())
}

#[tokio::test]
async fn g15_diff_and_ancestry__full_ancestry_ends_at_the_initial_snapshot() -> Res {
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    five_commits(&repo).await;
    let all: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(all.len(), 6); // five commits plus the initial snapshot
    assert!(all.last().unwrap().is_initial());
    assert_eq!(all.last().unwrap().message, Snapshot::INITIAL_COMMIT_MESSAGE);
    Ok(())
}

#[tokio::test]
async fn g15_diff_and_ancestry__snapshot_ancestry_from_an_id() -> Res {
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    let ids = five_commits(&repo).await;
    let from_third: Vec<SnapshotInfo> =
        repo.snapshot_ancestry(&ids[2]).await?.try_collect().await?;
    assert_eq!(from_third.len(), 4);
    assert_eq!(from_third[0].id, ids[2]);

    let shared = Arc::new(repo);
    let arced: Vec<SnapshotInfo> = Arc::clone(&shared)
        .ancestry_arc(&VersionInfo::SnapshotId(ids[0].clone()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(arced.len(), 2);
    // the same walk addressed by snapshot id rather than by version
    let from_id: Vec<SnapshotInfo> =
        Arc::clone(&shared).snapshot_ancestry_arc(&ids[2]).await?.try_collect().await?;
    assert_eq!(from_id, from_third);
    Ok(())
}

#[tokio::test]
async fn g15_diff_and_ancestry__readonly_session_from_every_version_kind() -> Res {
    mem_repo!();
    let repo = mem_repo().await;
    let mut w = repo.writable_session("main").await?;
    w.add_group(Path::root(), Bytes::from_static(b"g")).await?;
    let id = w.commit("c", None).await?;
    repo.create_tag("v1", &id).await?;

    let ro = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(ro.read_only());
    assert_eq!(ro.snapshot_id(), &id);
    assert_eq!(ro.branch(), None);
    assert_eq!(ro.get_node(&Path::root()).await?.user_data, Bytes::from_static(b"g"));

    assert_eq!(
        repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?.snapshot_id(),
        &id
    );
    assert_eq!(
        repo.readonly_session(&VersionInfo::SnapshotId(id.clone()))
            .await?
            .snapshot_id(),
        &id
    );
    Ok(())
}

#[tokio::test]
async fn g15_diff_and_ancestry__repository_diff_between_two_versions() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    use icechunk::format::transaction_log::DiffBuilder;
    mem_repo!();
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let base = s.commit("base", None).await?;

    let mut s = repo.writable_session("main").await?;
    let p: Path = "/arr".try_into()?;
    s.add_array(p.clone(), ArrayShape::new(vec![(4u64, 2)]).unwrap(), None, Bytes::new())
        .await?;
    s.add_group("/newgroup".try_into()?, Bytes::new()).await?;
    let head = s.commit("head", None).await?;

    let diff = repo
        .diff(&VersionInfo::SnapshotId(base.clone()), &VersionInfo::SnapshotId(head.clone()))
        .await?;
    assert!(!diff.is_empty());
    assert!(diff.new_arrays.contains(&p));
    assert!(diff.new_groups.contains(&"/newgroup".try_into()?));
    assert!(diff.deleted_arrays.is_empty());
    assert!(diff.updated_chunks.is_empty());

    // `diff` is `DiffBuilder` folded over the transaction logs between the two
    // versions; driving the builder by hand reaches the same answer
    let mut builder = DiffBuilder::default();
    builder.add_changes(repo.asset_manager().fetch_transaction_log(&head).await?.as_ref());
    let from_session =
        repo.readonly_session(&VersionInfo::SnapshotId(base.clone())).await?;
    let to_session = repo.readonly_session(&VersionInfo::SnapshotId(head.clone())).await?;
    assert_eq!(builder.to_diff(&from_session, &to_session).await?, diff);

    // the fold only has transaction logs to walk when `to`'s ancestry reaches
    // `from`; reversed, the call is refused rather than answering an empty
    // `Diff` (section 4.10)
    {
        use icechunk::session::SessionErrorKind;
        let err = repo
            .diff(
                &VersionInfo::SnapshotId(head.clone()),
                &VersionInfo::SnapshotId(base.clone()),
            )
            .await
            .unwrap_err();
        assert!(matches!(err.kind, SessionErrorKind::BadSnapshotChainForDiff));
    }

    // `VersionInfo`'s three variants carry the serde names section 4.10 fixes
    for (version, key) in [
        (VersionInfo::SnapshotId(head.clone()), "snapshot_id"),
        (VersionInfo::TagRef("v1".to_string()), "tag"),
        (VersionInfo::BranchTipRef("main".to_string()), "branch"),
    ] {
        let value = serde_json::to_value(&version)?;
        assert!(value.get(key).is_some(), "the variant is tagged `{key}`");
        assert_eq!(serde_json::from_value::<VersionInfo>(value)?, version);
    }
    Ok(())
}

#[tokio::test]
async fn g15_diff_and_ancestry__session_status_reports_uncommitted_changes() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    mem_repo!();
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    assert!(s.status().await?.is_empty());
    assert!(!s.has_uncommitted_changes());

    s.add_group(Path::root(), Bytes::new()).await?;
    let p: Path = "/arr".try_into()?;
    s.add_array(p.clone(), ArrayShape::new(vec![(4u64, 2)]).unwrap(), None, Bytes::new())
        .await?;
    let status = s.status().await?;
    assert!(!status.is_empty());
    assert!(status.new_arrays.contains(&p));
    assert!(status.new_groups.contains(&Path::root()));
    assert!(s.has_uncommitted_changes());
    Ok(())
}

// ===========================================================================
// i01_object_layout - INHERITED from milestone 01 (the spec-1 repository format)
// ===========================================================================

#[tokio::test]
async fn i01_object_layout__default_branch_and_object_prefixes() -> Res {
    use icechunk::repository::RepositoryErrorKind;
    mem_storage!();
    commit_sample!();
    let dir = tempfile::TempDir::new()?;
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, Arc::clone(&st), HashMap::new()).await?;
    let (_, _, _) = commit_sample(&repo, "c1").await;

    assert!(dir.path().join("refs").join("branch.main").join("ref.json").is_file());
    for prefix in ["snapshots", "manifests", "transactions", "chunks"] {
        let p = dir.path().join(prefix);
        if prefix == "chunks" {
            continue; // the sample commit inlines its only chunk
        }
        assert!(p.is_dir(), "{prefix}/ must exist");
        assert!(std::fs::read_dir(&p)?.next().is_some(), "{prefix}/ must not be empty");
    }
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), vec!["main"]);

    // a repository is only ever created in a clean prefix, and this one now
    // holds objects
    let err = Repository::create(None, Arc::clone(&st), HashMap::new()).await.unwrap_err();
    assert!(matches!(
        err,
        RepositoryError { kind: RepositoryErrorKind::ParentDirectoryNotClean, .. }
    ));

    // a reference file is written with a conditional put: `for_creation()` is
    // create-if-absent, so a second write reports `WontOverwrite` rather than
    // replacing what is already there
    let fresh = mem_storage().await;
    let body = Bytes::from_static(br#"{"snapshot":"VY76P925PRY57WFEK410"}"#);
    assert_eq!(
        fresh
            .write_ref(
                &settings(),
                "branch.probe/ref.json",
                body.clone(),
                &storage::VersionInfo::for_creation(),
            )
            .await?,
        storage::WriteRefResult::Written
    );
    assert_eq!(
        fresh
            .write_ref(
                &settings(),
                "branch.probe/ref.json",
                body,
                &storage::VersionInfo::for_creation(),
            )
            .await?,
        storage::WriteRefResult::WontOverwrite
    );

    // "clean prefix" is a property the storage answers directly, and it is what
    // `create` consults
    assert!(mem_storage().await.root_is_clean().await?);
    assert!(!st.root_is_clean().await?);

    // configuration lives in the prefix too: nothing is stored until it is
    // saved, and it is read back without opening the repository
    assert!(Repository::fetch_config(st.as_ref()).await?.is_none());
    let _version = repo.save_config().await?;
    let (saved, _) =
        Repository::fetch_config(st.as_ref()).await?.expect("the config was saved");
    assert_eq!(
        saved.inline_chunk_threshold_bytes(),
        repo.config().inline_chunk_threshold_bytes()
    );

    // storage settings merge, and carry a concurrency block
    let merged = settings().merge(storage::Settings::default());
    let _ = merged.concurrency();

    // the repository config's remaining accessors
    let cfg = repo.config();
    let _ = cfg.compression();
    let _ = cfg.caching();
    let _ = cfg.storage();
    assert_eq!(
        cfg.merge(icechunk::RepositoryConfig::default()).inline_chunk_threshold_bytes(),
        cfg.inline_chunk_threshold_bytes()
    );
    assert!(cfg.virtual_chunk_containers().count() > 0);
    assert!(!raw_snapshot(st.as_ref(), &repo.lookup_branch("main").await?).await.is_empty());
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__branch_lifecycle() -> Res {
    use icechunk::repository::RepositoryErrorKind;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (id, _, _) = commit_sample(&repo, "c1").await;
    repo.create_branch("dev", &id).await?;
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        vec!["dev".to_string(), "main".to_string()]
    );
    assert_eq!(repo.lookup_branch("dev").await?, id);

    let mut s = repo.writable_session("dev").await?;
    s.add_group("/on-dev".try_into()?, Bytes::new()).await?;
    let dev_id = s.commit("dev commit", None).await?;
    assert_eq!(repo.lookup_branch("dev").await?, dev_id);
    assert_eq!(repo.lookup_branch("main").await?, id);

    repo.reset_branch("dev", &id).await?;
    assert_eq!(repo.lookup_branch("dev").await?, id);
    repo.delete_branch("dev").await?;
    assert_eq!(repo.list_branches().await?.into_iter().collect::<Vec<_>>(), vec!["main"]);

    let err = repo.delete_branch("main").await.unwrap_err();
    assert!(matches!(
        err,
        RepositoryError { kind: RepositoryErrorKind::CannotDeleteMain, .. }
    ));

    // `open_or_create` opens a prefix that already holds a repository rather
    // than refusing it as `create` would, and `reopen` makes a second handle
    let opened =
        Repository::open_or_create(None, Arc::clone(repo.storage()), HashMap::new()).await?;
    assert_eq!(opened.lookup_branch("main").await?, repo.lookup_branch("main").await?);
    let again = repo.reopen(Some(icechunk::RepositoryConfig::default()), None)?;
    assert_eq!(again.lookup_branch("main").await?, repo.lookup_branch("main").await?);
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__tags_are_immutable() -> Res {
    use icechunk::refs::RefErrorKind;
    use icechunk::repository::RepositoryErrorKind;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (id, _, _) = commit_sample(&repo, "c1").await;
    repo.create_tag("v1", &id).await?;
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), vec!["v1"]);
    assert_eq!(repo.lookup_tag("v1").await?, id);
    // a tag name is used once: re-creating it is `TagAlreadyExists`
    let err = repo.create_tag("v1", &id).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::Ref(RefErrorKind::TagAlreadyExists(_))
    ));

    repo.delete_tag("v1").await?;
    assert!(repo.list_tags().await?.is_empty());
    // deleting retires the name permanently: it is still `TagAlreadyExists`
    let err = repo.create_tag("v1", &id).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::Ref(RefErrorKind::TagAlreadyExists(_))
    ));
    Ok(())
}

/// What a reference file actually contains, and which names may address one.
/// Section 8 and `PROFILE.md` P3 fix both: the body is a JSON object with the
/// single key `"snapshot"` holding the 20-character Crockford spelling of the
/// snapshot id, and a branch or tag name may not contain `/`.
#[tokio::test]
async fn i01_object_layout__reference_files_and_names() -> Res {
    use icechunk::refs::{
        fetch_branch_tip, fetch_tag, list_branches, list_refs, list_tags, RefData,
        RefErrorKind, RefError,
    };
    use icechunk::repository::RepositoryErrorKind;
    mem_repo!();
    commit_sample!();
    let dir = tempfile::TempDir::new()?;
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, Arc::clone(&st), HashMap::new()).await?;
    let (id, _, _) = commit_sample(&repo, "c1").await;
    repo.create_tag("v1", &id).await?;

    // the body of both reference kinds, read off disk
    for (path, want) in [
        (dir.path().join("refs").join("branch.main").join("ref.json"), id.clone()),
        (dir.path().join("refs").join("tag.v1").join("ref.json"), id.clone()),
    ] {
        let body: serde_json::Value =
            serde_json::from_slice(&std::fs::read(&path)?)?;
        let object = body.as_object().expect("a reference file is a JSON object");
        assert_eq!(object.len(), 1, "exactly one key");
        let text = object
            .get("snapshot")
            .and_then(|v| v.as_str())
            .expect("the single key is `snapshot` and its value is a string");
        assert_eq!(text.len(), 20, "a 12-byte id spells as 20 Crockford characters");
        let parsed: SnapshotId = text.try_into().unwrap();
        assert_eq!(parsed, want);
        assert_eq!(text, want.to_string());
    }
    assert_eq!(repo.lookup_branch("main").await?, id);

    // the same files, read through the `refs` module rather than off disk
    assert_eq!(
        fetch_branch_tip(st.as_ref(), repo.storage_settings(), "main").await?,
        RefData { snapshot: id.clone() }
    );
    assert_eq!(fetch_tag(st.as_ref(), repo.storage_settings(), "v1").await?.snapshot, id);
    let all = list_refs(st.as_ref(), repo.storage_settings()).await?;
    assert!(all.contains(&Ref::Branch("main".to_string())));
    assert!(all.contains(&Ref::Tag("v1".to_string())));
    assert!(Ref::Branch("main".to_string()).is_branch());
    assert!(Ref::Tag("v1".to_string()).is_tag());
    assert_eq!(Ref::DEFAULT_BRANCH, "main");
    assert_eq!(
        Ref::Branch("main".to_string())
            .fetch(st.as_ref(), repo.storage_settings())
            .await?
            .snapshot,
        id
    );
    assert_eq!(
        list_branches(st.as_ref(), repo.storage_settings())
            .await?
            .into_iter()
            .collect::<Vec<_>>(),
        vec!["main".to_string()]
    );
    assert_eq!(
        list_tags(st.as_ref(), repo.storage_settings()).await?.into_iter().collect::<Vec<_>>(),
        vec!["v1".to_string()]
    );

    // a `/` in the name would put the reference file outside its own directory,
    // so neither reference kind accepts one
    let err = repo.create_branch("a/b", &id).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::Ref(RefErrorKind::InvalidRefName(_))
    ));
    let err = repo.create_tag("a/b", &id).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::Ref(RefErrorKind::InvalidRefName(_))
    ));
    // the same rejection reached through `refs` directly, carrying the alias
    let err: RefError =
        fetch_tag(st.as_ref(), repo.storage_settings(), "a/b").await.unwrap_err();
    assert!(matches!(err.kind, RefErrorKind::InvalidRefName(_)));
    assert!(!dir.path().join("refs").join("branch.a").exists());
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__object_ids_are_crockford_base32() -> Res {
    let snap = SnapshotId::random();
    let text = snap.to_string();
    assert_eq!(text.len(), 20);
    let back: SnapshotId = text.as_str().try_into().unwrap();
    assert_eq!(back, snap);

    let node = NodeId::random();
    let back: NodeId = node.to_string().as_str().try_into().unwrap();
    assert_eq!(back, node);

    let bad: Result<SnapshotId, _> = "not a valid id".try_into();
    assert!(bad.is_err());

    // the raw bytes parse back too, and every id family shares the spelling
    let raw = snap.0;
    let from_bytes: SnapshotId = raw.as_slice().try_into().unwrap();
    assert_eq!(from_bytes, snap);
    let chunk = ChunkId::random();
    assert_eq!(chunk.to_string().len(), 20);
    let manifest = ManifestId::new([4u8; 12]);
    assert_eq!(manifest.to_string().len(), 20);
    let manifest_back: ManifestId = manifest.to_string().as_str().try_into().unwrap();
    assert_eq!(manifest_back, manifest);

    // `FAKE` is the constant placeholder id, and it spells like any other
    assert_eq!(SnapshotId::FAKE.0, [0u8; 12]);
    assert_eq!(SnapshotId::FAKE.to_string().len(), 20);
    let fake_back: SnapshotId = SnapshotId::FAKE.to_string().as_str().try_into().unwrap();
    assert_eq!(fake_back, SnapshotId::FAKE);
    let node_fake: NodeId = NodeId::FAKE.to_string().as_str().try_into().unwrap();
    assert_eq!(node_fake, NodeId::FAKE);
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__inline_and_materialised_chunks() -> Res {
    use icechunk::format::snapshot::ArrayShape;
    mem_repo!();
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let p: Path = "/a".try_into()?;
    s.add_array(p.clone(), ArrayShape::new(vec![(2u64, 1)]).unwrap(), None, Bytes::new())
        .await?;
    // the rule is `len() > threshold` and the default threshold is 512, so a
    // payload of exactly 512 bytes is still inlined ...
    assert_eq!(repo.config().inline_chunk_threshold_bytes(), 512);
    let writer = s.get_chunk_writer();
    let small = writer(Bytes::from(vec![3u8; 512])).await?;
    assert!(matches!(small, ChunkPayload::Inline(_)));
    s.set_chunk_ref(p.clone(), ChunkIndices(vec![0]), Some(small)).await?;

    // ... and one byte more is written as its own chunk object
    let writer = s.get_chunk_writer();
    let big = writer(Bytes::from(vec![7u8; 513])).await?;
    assert!(matches!(big, ChunkPayload::Ref(_)));
    s.set_chunk_ref(p.clone(), ChunkIndices(vec![1]), Some(big)).await?;
    let id = s.commit("chunks", None).await?;

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    let reader = ro.get_chunk_reader(&p, &ChunkIndices(vec![1]), &ByteRange::ALL).await?;
    let bytes = reader.unwrap().await?;
    assert_eq!(bytes.len(), 513);
    Ok(())
}

// ===========================================================================
// i02_sessions_conflicts - INHERITED from milestone 02
// ===========================================================================

#[tokio::test]
async fn i02_sessions_conflicts__writable_and_readonly_sessions() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (id, _, _) = commit_sample(&repo, "c1").await;
    let w = repo.writable_session("main").await?;
    assert!(!w.read_only());
    assert_eq!(w.branch(), Some("main"));
    assert_eq!(w.snapshot_id(), &id);

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(id.clone())).await?;
    assert!(ro.read_only());
    assert_eq!(ro.branch(), None);
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__concurrent_commit_conflicts() -> Res {
    use icechunk::session::SessionErrorKind;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    commit_sample(&repo, "c1").await;
    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.add_group("/from-a".try_into()?, Bytes::new()).await?;
    b.add_group("/from-b".try_into()?, Bytes::new()).await?;
    a.commit("a", None).await?;
    let err = b.commit("b", None).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::Conflict { .. }));
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__detector_classifies_a_chunk_double_update() -> Res {
    use icechunk::session::SessionErrorKind;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (_, p, node_id) = commit_sample(&repo, "c1").await;
    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![0, 0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"aaa"))),
    )
    .await?;
    b.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![0, 0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"bbb"))),
    )
    .await?;
    a.commit("a", None).await?;

    let err = b.rebase(&ConflictDetector).await.unwrap_err();
    match err.kind {
        SessionErrorKind::RebaseFailed { conflicts, .. } => {
            assert!(conflicts.iter().any(|c| matches!(
                c,
                Conflict::ChunkDoubleUpdate { node_id: n, .. } if n == &node_id
            )));
        }
        other => panic!("expected a classified rebase failure, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__basic_solver_rebase_succeeds() -> Res {
    use icechunk::session::SessionErrorKind;
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (_, p, _) = commit_sample(&repo, "c1").await;
    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![0, 0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"aaa"))),
    )
    .await?;
    b.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![1, 0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"bbb"))),
    )
    .await?;
    a.commit("a", None).await?;

    let solver = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseOurs,
        ..BasicConflictSolver::default()
    };
    b.rebase(&solver).await?;
    let id = b.commit("b", None).await?;

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert!(ro.get_chunk_ref(&p, &ChunkIndices(vec![0, 0])).await?.is_some());
    assert!(ro.get_chunk_ref(&p, &ChunkIndices(vec![1, 0])).await?.is_some());

    // the defaults section 4.14 declares
    let d = BasicConflictSolver::default();
    assert!(matches!(d.on_chunk_conflict, VersionSelection::UseOurs));
    assert!(!d.fail_on_delete_of_updated_array);
    assert!(!d.fail_on_delete_of_updated_group);

    // On a REAL double update the selection is what decides. `Fail` refuses to
    // patch and reports the conflict ...
    let strict_repo = mem_repo().await;
    let (_, sp, _) = commit_sample(&strict_repo, "c1").await;
    let mut x = strict_repo.writable_session("main").await?;
    let mut y = strict_repo.writable_session("main").await?;
    for (s, v) in [(&mut x, &b"theirs"[..]), (&mut y, &b"ours"[..])] {
        s.set_chunk_ref(
            sp.clone(),
            ChunkIndices(vec![0, 1]),
            Some(ChunkPayload::Inline(Bytes::copy_from_slice(v))),
        )
        .await?;
    }
    x.commit("theirs", None).await?;
    let strict = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::Fail,
        ..BasicConflictSolver::default()
    };
    let err = y.rebase(&strict).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RebaseFailed { .. }));

    // ... while `UseTheirs` keeps the value that is already committed
    let theirs_repo = mem_repo().await;
    let (_, tp, _) = commit_sample(&theirs_repo, "c1").await;
    let mut x = theirs_repo.writable_session("main").await?;
    let mut y = theirs_repo.writable_session("main").await?;
    for (s, v) in [(&mut x, &b"theirs"[..]), (&mut y, &b"ours"[..])] {
        s.set_chunk_ref(
            tp.clone(),
            ChunkIndices(vec![0, 1]),
            Some(ChunkPayload::Inline(Bytes::copy_from_slice(v))),
        )
        .await?;
    }
    // A coordinate only `y` writes, so that after `UseTheirs` drops the
    // conflicting one the session still carries a real change. This case is
    // about the resolution, not about what `commit` does with an empty change
    // set - that question belongs to the `NoChangesToCommit` rule stated in
    // section 4, and nothing here should depend on how an implementation
    // bookkeeps a per-node bucket the solver has just emptied.
    y.set_chunk_ref(
        tp.clone(),
        ChunkIndices(vec![2, 0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"ours-only"))),
    )
    .await?;
    x.commit("theirs", None).await?;
    let take_theirs = BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseTheirs,
        ..BasicConflictSolver::default()
    };
    y.rebase(&take_theirs).await?;
    let id = y.commit("ours", None).await?;
    let ro = theirs_repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert_eq!(
        ro.get_chunk_ref(&tp, &ChunkIndices(vec![0, 1])).await?,
        Some(ChunkPayload::Inline(Bytes::from_static(b"theirs")))
    );
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__virtual_chunk_containers_match_by_prefix() -> Res {
    use icechunk::error::ICError;
    use icechunk::format::manifest::VirtualReferenceErrorKind;
    mem_repo!();
    let repo = mem_repo().await;
    let s = repo.writable_session("main").await?;
    let local = VirtualChunkLocation::from_absolute_path("file:///tmp/x.nc")?;
    assert!(s.matching_container(&local).is_some());
    assert!(s
        .matching_container(&VirtualChunkLocation("weird://nowhere/x".to_string()))
        .is_none());
    assert!(repo.config().virtual_chunk_containers().count() > 0);

    // a location that is not a parseable URL, and one whose URL carries no path
    // segments, are rejected by name rather than stored
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("not a url"),
        Err(ICError { kind: VirtualReferenceErrorKind::CannotParseUrl(_), .. })
    ));
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("weird:x/y"),
        Err(ICError { kind: VirtualReferenceErrorKind::NoPathSegments(_), .. })
    ));
    // ... and a non-`file` scheme with no authority has no bucket to name
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("weird:///y"),
        Err(ICError { kind: VirtualReferenceErrorKind::CannotParseBucketName(_), .. })
    ));

    // It does NOT restrict the scheme: an unknown scheme WITH a host is
    // normalised and returned, and it is the container lookup - not this
    // function - that later declines to handle it.
    let odd = VirtualChunkLocation::from_absolute_path("weird://x/y")?;
    assert_eq!(odd, VirtualChunkLocation("weird://x/y".to_string()));
    assert!(s.matching_container(&odd).is_none());
    // the same normalisation a `file://` location goes through
    assert_eq!(
        VirtualChunkLocation::from_absolute_path("file:///tmp//a//b.nc")?,
        VirtualChunkLocation("file:///tmp/a/b.nc".to_string())
    );

    // the container set itself, reached by module path
    let containers = icechunk::virtual_chunks::mk_default_containers();
    let file_container: &icechunk::virtual_chunks::VirtualChunkContainer =
        containers.get("file").expect("a `file` container ships by default");
    assert_eq!(file_container.name, "file");
    assert!(file_container.url_prefix.starts_with("file"));
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__distributed_merge_and_commit() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (_, p, _) = commit_sample(&repo, "c1").await;
    let mut main = repo.writable_session("main").await?;
    let mut worker = repo.writable_session("main").await?;
    worker
        .set_chunk_ref(
            p.clone(),
            ChunkIndices(vec![1, 1]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"remote"))),
        )
        .await?;
    let changes: icechunk::change_set::ChangeSet = worker.discard_changes();
    assert!(!worker.has_uncommitted_changes());
    main.merge(changes).await?;
    let id = main.commit("merged", None).await?;

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert!(ro.get_chunk_ref(&p, &ChunkIndices(vec![1, 1])).await?.is_some());

    let round_tripped = Session::from_bytes(
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
            .await?
            .as_bytes()?,
    )?;
    assert!(round_tripped.read_only());
    Ok(())
}

// ===========================================================================
// i03_expiration_gc - INHERITED from milestone 03
// ===========================================================================

#[tokio::test]
async fn i03_expiration_gc__expire_releases_old_snapshots() -> Res {
    use icechunk::ops::gc::ExpiredRefAction;
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    let ids = five_commits(&repo).await;
    let before: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(before.len(), 6); // five commits plus the initial snapshot
    // expire everything strictly older than the second-newest commit. Commit
    // enforces a strictly increasing `flushed_at` along a chain, so this cut is
    // determined by the data and not by the wall clock.
    let cutoff = before[1].flushed_at;

    let result = expire(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    // the root is never released, and the two newest commits are kept
    assert_eq!(
        result.released_snapshots,
        ids[0..3].iter().cloned().collect::<HashSet<SnapshotId>>()
    );
    assert!(result.edited_snapshots.contains(&ids[3]));
    assert!(result.deleted_refs.is_empty());

    let remaining: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(remaining.len(), 3);
    assert_eq!(remaining[0].message, "commit 4");
    assert_eq!(remaining[1].message, "commit 3");
    assert!(remaining[2].is_initial());
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__garbage_collect_deletes_unreachable_objects() -> Res {
    use icechunk::ops::gc::ExpiredRefAction;
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    five_commits(&repo).await;
    let before: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    let cutoff = before[1].flushed_at;
    let released = expire(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?
    .released_snapshots;
    assert_eq!(released.len(), 3);

    let config = GCConfig::clean_all(Utc::now(), Utc::now(), None);
    assert!(config.deletes_snapshots());
    assert!(config.deletes_chunks());
    assert!(config.deletes_manifests());
    assert!(config.deletes_transaction_logs());
    assert!(config.deletes_attributes());
    let summary = garbage_collect(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        &config,
    )
    .await?;
    // exactly the three snapshots expiration released, and their transaction logs
    assert_eq!(summary.snapshots_deleted, 3);
    assert_eq!(summary.transaction_logs_deleted, 3);
    // these commits only created groups, so nothing else became unreachable
    assert_eq!(summary.chunks_deleted, 0);
    assert_eq!(summary.manifests_deleted, 0);
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__reachable_objects_are_kept() -> Res {
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (id, p, _) = commit_sample(&repo, "keep me").await;
    let config = GCConfig::clean_all(Utc::now(), Utc::now(), None);
    garbage_collect(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        &config,
    )
    .await?;

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert_eq!(
        ro.get_chunk_ref(&p, &ChunkIndices(vec![0, 0])).await?,
        Some(ChunkPayload::Inline(Bytes::from_static(b"hello")))
    );
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__keep_everything_deletes_nothing() -> Res {
    use icechunk::ops::gc::{expire_ref, ExpireRefResult};
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    five_commits(&repo).await;
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
    // the same constructor spells a per-kind policy
    let chunks_only = GCConfig::new(
        HashSet::new(),
        Action::DeleteIfCreatedBefore(Utc::now()),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
    );
    assert!(chunks_only.deletes_chunks());
    assert!(!chunks_only.deletes_snapshots());
    let summary = garbage_collect(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        &config,
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 0);
    assert_eq!(summary.chunks_deleted, 0);
    assert_eq!(summary.manifests_deleted, 0);

    // Nothing has been collected, so the history is intact and expiration can
    // still be driven one reference at a time.
    let history: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    let cutoff = history[1].flushed_at;
    match expire_ref(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        &Ref::Branch("main".to_string()),
        cutoff,
    )
    .await?
    {
        ExpireRefResult::Done { released_snapshots, edited_snapshot } => {
            // the chain root is never released, the oldest KEPT snapshot is the
            // one rewritten in place
            assert!(released_snapshots.contains(&history[2].id));
            assert!(!released_snapshots.contains(&history[history.len() - 1].id));
            assert_eq!(edited_snapshot, history[1].id);
        }
        other => panic!("expected `Done`, got {other:?}"),
    }
    // a reference that already points at an expired snapshot is itself expired
    repo.create_tag("ancient", &history[3].id).await?;
    assert_eq!(
        expire_ref(
            repo.storage().as_ref(),
            repo.storage_settings(),
            Arc::clone(repo.asset_manager()),
            &Ref::Tag("ancient".to_string()),
            cutoff,
        )
        .await?,
        ExpireRefResult::RefIsExpired
    );
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__a_tag_holds_its_snapshots_alive() -> Res {
    use icechunk::ops::gc::ExpiredRefAction;
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    let ids = five_commits(&repo).await;
    repo.create_tag("keep", &ids[1]).await?;
    let before: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    let cutoff = before[1].flushed_at;
    let result = expire(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    // expiration releases a snapshot from the BRANCH history ...
    assert!(result.released_snapshots.contains(&ids[1]));
    assert!(result.deleted_refs.is_empty());

    // ... but the tag still points at it, so collection must not remove it:
    // only the one snapshot no reference can reach is deleted.
    let summary = garbage_collect(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 1);

    let tagged: Vec<SnapshotInfo> =
        repo.ancestry(&VersionInfo::TagRef("keep".to_string())).await?.try_collect().await?;
    assert_eq!(tagged[0].id, ids[1]);
    assert_eq!(tagged.len(), 3);
    assert!(tagged[2].is_initial());
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__an_expired_tag_is_deleted_when_asked() -> Res {
    use icechunk::ops::gc::ExpiredRefAction;
    mem_repo!();
    five_commits!();
    let repo = mem_repo().await;
    let ids = five_commits(&repo).await;
    repo.create_tag("old", &ids[1]).await?;
    let before: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    // the tag points at a snapshot strictly older than the cut, so the tag
    // itself is expired; `Delete` removes it and records it in `deleted_refs`
    let cutoff = before[1].flushed_at;
    let result = expire(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Delete,
    )
    .await?;
    assert!(result.deleted_refs.contains(&Ref::Tag("old".to_string())));
    assert!(repo.list_tags().await?.is_empty());
    // the default branch is not expired, so its history was walked as usual
    assert!(!result.released_snapshots.is_empty());
    Ok(())
}

// ===========================================================================
// i04_header_vocabulary - INHERITED from milestone 03 (the file header)
// ===========================================================================

#[tokio::test]
async fn i04_header_vocabulary__magic_bytes() -> Res {
    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES.len(), 12);
    assert_eq!(
        ICECHUNK_FORMAT_MAGIC_BYTES,
        &[0x49, 0x43, 0x45, 0xf0, 0x9f, 0xa7, 0x8a, 0x43, 0x48, 0x55, 0x4e, 0x4b]
    );
    Ok(())
}

#[tokio::test]
async fn i04_header_vocabulary__file_type_codes() -> Res {
    assert_eq!(FileTypeBin::Snapshot as u8, 1);
    assert_eq!(FileTypeBin::Manifest as u8, 2);
    assert_eq!(FileTypeBin::Attributes as u8, 3);
    assert_eq!(FileTypeBin::TransactionLog as u8, 4);
    // The `Chunk` discriminant is NOT asserted here: the previous milestone's
    // contract leaves its value open and only forbids the code 9. Its value is
    // fixed at this release and `g01_file_header` is where that is scored.
    for code in 1u8..=4 {
        assert_eq!(FileTypeBin::try_from(code).unwrap() as u8, code);
    }
    assert!(FileTypeBin::try_from(0u8).is_err());
    assert!(FileTypeBin::try_from(9u8).is_err());
    Ok(())
}

#[tokio::test]
async fn i04_header_vocabulary__spec_version_codes() -> Res {
    assert_eq!(SpecVersionBin::V0dot1 as u8, 1);
    assert_eq!(SpecVersionBin::current() as u8, 1);
    assert_eq!(SpecVersionBin::try_from(1u8).unwrap() as u8, 1);
    assert!(SpecVersionBin::try_from(0u8).is_err());
    assert!(SpecVersionBin::try_from(2u8).is_err());
    Ok(())
}

#[tokio::test]
async fn i04_header_vocabulary__compression_codes() -> Res {
    assert_eq!(CompressionAlgorithmBin::None as u8, 0);
    assert_eq!(CompressionAlgorithmBin::Zstd as u8, 1);
    assert_eq!(CompressionAlgorithmBin::try_from(0u8).unwrap() as u8, 0);
    assert_eq!(CompressionAlgorithmBin::try_from(1u8).unwrap() as u8, 1);
    assert!(CompressionAlgorithmBin::try_from(2u8).is_err());
    Ok(())
}

#[tokio::test]
async fn i04_header_vocabulary__a_foreign_implementation_id_is_ignored() -> Res {
    mem_storage!();
    mem_repo!();
    commit_sample!();
    let repo = mem_repo().await;
    let (snap_id, _, _) = commit_sample(&repo, "c1").await;
    let original = repo.asset_manager().fetch_snapshot(&snap_id).await?;
    let mut raw = raw_snapshot(repo.storage().as_ref(), &snap_id).await;
    // bytes 12..35 identify the writing client and are read but ignored
    raw[12..36].copy_from_slice(b"another-impl-9.9.9      ");

    let st = mem_storage().await;
    let am = AssetManager::new_no_cache(Arc::clone(&st), settings(), 1);
    let fresh = SnapshotId::random();
    st.write_snapshot(&settings(), fresh.clone(), vec![], Bytes::from(raw)).await?;
    let back = am.fetch_snapshot(&fresh).await?;
    assert_eq!(back.bytes(), original.bytes());
    Ok(())
}
