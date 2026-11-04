// Verifier-owned end-to-end driver for milestone 04 ("the FlatBuffers format").
//
// `gate-endtoend` is one ordinary scored group among the others: it multiplies
// nothing and stops nothing. It holds this step's headline capability at
// proof-of-life depth plus one end-to-end path through every milestone this
// step inherits (01 the spec-1 repository format, 02 sessions / conflicts /
// virtual chunk containers, 03 expiration, garbage collection and the file
// header). It is compiled from its own target so that a conformance driver
// that does not typecheck cannot take it down with it.
//
// Public `icechunk::` API only; no network, no cloud.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
// case functions are named `<group>__<case>`; the double separator is what the
// verifier's per-group filter and attribution slice recognise.
#![allow(non_snake_case)]

use std::{collections::HashMap, sync::Arc};

use bytes::Bytes;
use chrono::Utc;
use futures::TryStreamExt;
use tokio::sync::RwLock;

use icechunk::{
    conflicts::basic_solver::{BasicConflictSolver, VersionSelection},
    format::{
        format_constants::{
            CompressionAlgorithmBin, FileTypeBin, SpecVersionBin,
            ICECHUNK_FORMAT_MAGIC_BYTES,
        },
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::{ArrayShape, DimensionName, NodeData, Snapshot, SnapshotInfo},
        transaction_log::TransactionLog,
        ByteRange, ChunkIndices, IcechunkFormatError, IcechunkFormatErrorKind, Path,
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{expire, garbage_collect, ExpiredRefAction, GCConfig},
    repository::VersionInfo,
    session::SessionErrorKind,
    storage,
    store::SetVirtualRefsResult,
    Repository, Storage, Store,
};

type Res = Result<(), Box<dyn std::error::Error>>;

async fn mem_repo() -> Repository {
    let st: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await.unwrap();
    Repository::create(None, st, HashMap::new()).await.unwrap()
}

async fn read_all(mut r: Box<dyn tokio::io::AsyncRead + Unpin + Send>) -> Vec<u8> {
    use tokio::io::AsyncReadExt;
    let mut v = Vec::new();
    r.read_to_end(&mut v).await.unwrap();
    v
}

// ---------------------------------------------------------------------------
// This step's headline capability: the FlatBuffers metadata format, written and
// read back end to end.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__flatbuffers_format_round_trip() -> Res {
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::from_static(br#"{"zarr_format":3}"#)).await?;
    let p: Path = "/temperature".try_into()?;
    let shape = ArrayShape::new(vec![(4u64, 2), (4u64, 2)]).unwrap();
    let dims = Some(vec![DimensionName::from("y"), DimensionName::from("x")]);
    s.add_array(p.clone(), shape.clone(), dims.clone(), Bytes::from_static(b"meta"))
        .await?;
    s.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![0, 0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"0123"))),
    )
    .await?;
    let node_id = s.get_array(&p).await?.id;
    let snap_id = s.commit("gate commit", None).await?;

    // --- the decoded snapshot ------------------------------------------------
    let am = repo.asset_manager();
    let snap = am.fetch_snapshot(&snap_id).await?;
    assert_eq!(snap.id(), snap_id);
    assert_eq!(snap.message(), "gate commit");
    assert_eq!(snap.len(), 2);
    let _ = snap.flushed_at()?;
    let node = snap.get_node(&p)?;
    assert_eq!(node.user_data, Bytes::from_static(b"meta"));
    let manifest_ref = match &node.node_data {
        NodeData::Array { shape: sh, dimension_names: dn, manifests } => {
            assert_eq!(sh, &shape);
            assert_eq!(dn, &dims);
            assert_eq!(manifests.len(), 1);
            manifests[0].clone()
        }
        NodeData::Group => panic!("/temperature must decode as an array"),
    };

    // --- the decoded manifest ------------------------------------------------
    let info = snap.manifest_info(&manifest_ref.object_id).expect("manifest info");
    let manifest = am.fetch_manifest(&info.id, info.size_bytes).await?;
    assert_eq!(manifest.id(), info.id);
    assert_eq!(manifest.len(), 1);
    assert_eq!(
        manifest.get_chunk_payload(&node_id, &ChunkIndices(vec![0, 0]))?,
        ChunkPayload::Inline(Bytes::from_static(b"0123"))
    );

    // --- the decoded transaction log ----------------------------------------
    let tx = am.fetch_transaction_log(&snap_id).await?;
    assert!(tx.array_created(&node_id));
    assert!(tx.chunks_updated(&node_id));
    assert_eq!(
        tx.updated_chunks_for(&node_id).collect::<Vec<_>>(),
        vec![ChunkIndices(vec![0, 0])]
    );

    // --- all three are FlatBuffers roots, and verification is what guards them
    for buffer in [snap.bytes(), manifest.bytes(), tx.bytes()] {
        assert_eq!(&buffer[4..8], b"Ichk");
    }
    assert!(Snapshot::from_buffer(snap.bytes().to_vec()).is_ok());
    assert!(TransactionLog::from_buffer(tx.bytes().to_vec()).is_ok());
    let half = snap.bytes()[..snap.bytes().len() / 2].to_vec();
    assert!(matches!(
        Snapshot::from_buffer(half),
        Err(IcechunkFormatError {
            kind: IcechunkFormatErrorKind::InvalidFlatBuffer(_),
            ..
        })
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 03: the 39-byte plaintext file header on a written object.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__file_header_on_disk() -> Res {
    let dir = tempfile::TempDir::new()?;
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, Arc::clone(&st), HashMap::new()).await?;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let snap_id = s.commit("header", None).await?;

    let settings = storage::Settings::default();
    let raw = read_all(st.fetch_snapshot(&settings, &snap_id).await?).await;
    assert!(raw.len() > 39);
    assert_eq!(&raw[0..12], ICECHUNK_FORMAT_MAGIC_BYTES);
    assert_eq!(raw[12..36].len(), 24);
    assert_eq!(raw[36], SpecVersionBin::current() as u8);
    assert_eq!(raw[37], FileTypeBin::Snapshot as u8);
    assert_eq!(raw[38], CompressionAlgorithmBin::Zstd as u8);
    assert_eq!(&raw[39..43], &[0x28u8, 0xb5, 0x2f, 0xfd]);

    let tx = read_all(st.fetch_transaction_log(&settings, &snap_id).await?).await;
    assert_eq!(tx[37], FileTypeBin::TransactionLog as u8);
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 01: the repository object layout and its reference kinds.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__repository_format_and_references() -> Res {
    let dir = tempfile::TempDir::new()?;
    let st: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(dir.path()).await?;
    assert!(!Repository::exists(st.as_ref()).await?);
    let repo = Repository::create(None, Arc::clone(&st), HashMap::new()).await?;
    assert!(Repository::exists(st.as_ref()).await?);

    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::from_static(b"root")).await?;
    let first = s.commit("first", None).await?;

    repo.create_branch("dev", &first).await?;
    repo.create_tag("v1", &first).await?;
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        vec!["dev".to_string(), "main".to_string()]
    );
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), vec!["v1"]);
    assert_eq!(repo.lookup_tag("v1").await?, first);

    // branch refs live in a single `ref.json`; tags keep theirs
    assert!(dir.path().join("refs").join("branch.main").join("ref.json").is_file());
    assert!(dir.path().join("refs").join("tag.v1").join("ref.json").is_file());
    assert!(dir.path().join("snapshots").is_dir());

    let history: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::TagRef("v1".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(history.len(), 2);
    assert!(history[1].is_initial());
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 02: sessions, conflict detection and rebase.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__sessions_conflicts_and_rebase() -> Res {
    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let p: Path = "/a".try_into()?;
    s.add_array(p.clone(), ArrayShape::new(vec![(4u64, 1)]).unwrap(), None, Bytes::new())
        .await?;
    s.commit("base", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"aa"))),
    )
    .await?;
    b.set_chunk_ref(
        p.clone(),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"bb"))),
    )
    .await?;
    a.commit("a", None).await?;

    // the second writer conflicts, rebases, and then commits
    let err = b.commit("b", None).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::Conflict { .. }));
    b.rebase(&BasicConflictSolver {
        on_chunk_conflict: VersionSelection::UseOurs,
        ..BasicConflictSolver::default()
    })
    .await?;
    let id = b.commit("b", None).await?;

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(id)).await?;
    assert!(ro.read_only());
    assert!(ro.get_chunk_ref(&p, &ChunkIndices(vec![0])).await?.is_some());
    assert!(ro.get_chunk_ref(&p, &ChunkIndices(vec![1])).await?.is_some());
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 02: virtual chunk containers and `file://` references, reached
// through this release's bulk setter.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__virtual_chunk_references() -> Res {
    let dir = tempfile::TempDir::new()?;
    let data = dir.path().join("data.bin");
    std::fs::write(&data, b"AAAABBBBCCCC")?;
    let url = format!("file://{}", data.display());

    let repo = mem_repo().await;
    let mut s = repo.writable_session("main").await?;
    s.add_group(Path::root(), Bytes::new()).await?;
    let p: Path = "/virtual".try_into()?;
    s.add_array(
        p.clone(),
        ArrayShape::new(vec![(3u64, 1)]).unwrap(),
        None,
        Bytes::from_static(
            br#"{"zarr_format":3,"node_type":"array","shape":[3],"chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1]}}}"#,
        ),
    )
    .await?;
    let store = Store::from_session(Arc::new(RwLock::new(s))).await;

    let refs: Vec<(ChunkIndices, VirtualChunkRef)> = (0u64..3)
        .map(|i| {
            (
                ChunkIndices(vec![i as u32]),
                VirtualChunkRef {
                    location: VirtualChunkLocation::from_absolute_path(&url).unwrap(),
                    offset: i * 4,
                    length: 4,
                    checksum: None,
                },
            )
        })
        .collect();
    assert_eq!(store.set_virtual_refs(&p, true, refs).await?, SetVirtualRefsResult::Done);
    store.session().write().await.commit("virtual", None).await?;

    let ro = Store::from_session(Arc::new(RwLock::new(
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?,
    )))
    .await;
    assert_eq!(ro.get("virtual/c/0", &ByteRange::ALL).await?, Bytes::from_static(b"AAAA"));
    assert_eq!(ro.get("virtual/c/1", &ByteRange::ALL).await?, Bytes::from_static(b"BBBB"));
    assert_eq!(ro.get("virtual/c/2", &ByteRange::ALL).await?, Bytes::from_static(b"CCCC"));
    Ok(())
}

// ---------------------------------------------------------------------------
// Milestone 03: expiration and garbage collection.
// ---------------------------------------------------------------------------

#[tokio::test]
async fn gate__expiration_and_garbage_collection() -> Res {
    let repo = mem_repo().await;
    for i in 0..4 {
        let mut s = repo.writable_session("main").await?;
        s.add_group(format!("/g{i}").as_str().try_into()?, Bytes::new()).await?;
        s.commit(&format!("commit {i}"), None).await?;
    }
    let before: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(before.len(), 5);
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
    assert_eq!(released.len(), 2);

    let after: Vec<SnapshotInfo> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(after.len(), 3);

    let summary = garbage_collect(
        repo.storage().as_ref(),
        repo.storage_settings(),
        Arc::clone(repo.asset_manager()),
        &GCConfig::clean_all(Utc::now(), Utc::now(), None),
    )
    .await?;
    assert_eq!(summary.snapshots_deleted, 2);
    assert_eq!(summary.transaction_logs_deleted, 2);

    // the surviving history is still readable
    let head = repo
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    assert!(head.get_node(&"/g3".try_into()?).await.is_ok());
    Ok(())
}
