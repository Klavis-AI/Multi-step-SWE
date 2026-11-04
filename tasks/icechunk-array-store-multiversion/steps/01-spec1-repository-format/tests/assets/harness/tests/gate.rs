// Verifier-owned end-to-end driver for milestone 01 (the spec-1 repository
// format). One path through this milestone's headline capability at
// proof-of-life depth; the harder variants are scored by tests/conformance.rs
// and still run when this group fails.
//
// Milestone 01 inherits nothing, so every case here is new capability.

// Test function names carry their score-group prefix, which the runner filters
// on; that shape is not snake case.
#![allow(non_snake_case)]

use std::{num::NonZeroU64, sync::Arc};

use bytes::Bytes;
use futures::TryStreamExt;

use icechunk::{
    format::{
        manifest::{ChunkPayload, VirtualChunkLocation, VirtualChunkRef},
        snapshot::ZarrArrayMetadata,
        ByteRange, ChunkIndices, Path,
    },
    metadata::{ChunkKeyEncoding, ChunkShape, DataType, FillValue},
    refs::{fetch_ref, fetch_tag, Ref},
    repository::get_chunk,
    ObjectStorage, Repository, Storage, Store,
};

type GateResult = Result<(), Box<dyn std::error::Error>>;

const GROUP_META: &[u8] = br#"{"zarr_format":3, "node_type":"group"}"#;
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"]}"#;

fn mem(prefix: &str) -> Arc<dyn Storage + Send + Sync> {
    Arc::new(ObjectStorage::new_in_memory_store(Some(prefix.to_string())))
}

fn array_meta() -> ZarrArrayMetadata {
    ZarrArrayMetadata {
        shape: vec![2, 2, 2],
        data_type: DataType::Int32,
        chunk_shape: ChunkShape(vec![NonZeroU64::new(1).ok_or("nonzero").unwrap()]),
        chunk_key_encoding: ChunkKeyEncoding::Slash,
        fill_value: FillValue::Int32(0),
        codecs: vec![],
        storage_transformers: None,
        dimension_names: None,
    }
}

/// Initialise, write metadata and a chunk through the Zarr store, commit, then
/// reopen at the branch tip and read everything back.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__write_commit_and_reopen_through_the_store() -> GateResult {
    let storage = mem("gate-store");
    let mut store = Store::new_from_storage(Arc::clone(&storage)).await?;

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let data = Bytes::copy_from_slice(b"hello");
    store.set("array/c/0/1/0", data.clone()).await?;
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, data);

    let snapshot = store.commit("gate commit").await?;
    assert_eq!(fetch_ref(storage.as_ref(), "main").await?.1.snapshot, snapshot);

    let reopened = Store::from_repository(
        Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build(),
        icechunk::zarr::AccessMode::ReadWrite,
        Some("main".to_string()),
        None,
    );
    assert_eq!(reopened.get("array/c/0/1/0", &ByteRange::ALL).await?, data);
    assert_eq!(
        reopened.get("array/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(ARRAY_META)
    );
    let mut keys = reopened.list().await?.try_collect::<Vec<_>>().await?;
    keys.sort();
    assert_eq!(
        keys,
        vec![
            "array/c/0/1/0".to_string(),
            "array/zarr.json".to_string(),
            "zarr.json".to_string()
        ]
    );
    Ok(())
}

/// Branch, commit on the branch, tag a snapshot, and read back at both.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__branch_and_tag() -> GateResult {
    let storage = mem("gate-refs");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group(Path::root()).await?;
    repo.add_group("/a".try_into()?).await?;
    let base = repo.commit(Ref::DEFAULT_BRANCH, "base", None).await?;

    repo.tag("v1", &base).await?;
    assert_eq!(fetch_tag(storage.as_ref(), "v1").await?.snapshot, base);

    repo.new_branch("dev").await?;
    repo.add_group("/dev-only".try_into()?).await?;
    let on_dev = repo.commit("dev", "dev work", None).await?;
    assert_ne!(on_dev, base);

    let at_tag = Repository::from_tag(Arc::clone(&storage), "v1").await?.build();
    assert!(at_tag.get_group(&"/a".try_into()?).await.is_ok());
    assert!(at_tag.get_node(&"/dev-only".try_into()?).await.is_err());

    let on_main = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    assert_eq!(on_main.snapshot_id(), &base);
    Ok(())
}

/// The three chunk reference kinds: inline, materialised and virtual.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__the_three_chunk_reference_kinds() -> GateResult {
    let files = tempfile::TempDir::new()?;
    let virtual_bytes = Bytes::copy_from_slice(b"virtual!!");
    let file_path = files.path().join("chunk");
    std::fs::write(&file_path, virtual_bytes.as_ref())?;
    let location = VirtualChunkLocation::from_absolute_path(&format!(
        "file://{}",
        file_path.to_str().ok_or("temporary path must be UTF-8")?
    ))?;

    let storage = mem("gate-chunks");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;

    let inline = Bytes::copy_from_slice(b"inline");
    repo.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0, 0, 0]),
        Some(ChunkPayload::Inline(inline.clone())),
    )
    .await?;

    let materialised = Bytes::copy_from_slice(b"m".repeat(600).as_slice());
    let payload = repo.get_chunk_writer()(materialised.clone()).await?;
    assert!(matches!(payload, ChunkPayload::Ref(_)));
    repo.set_chunk_ref(path.clone(), ChunkIndices(vec![0, 0, 1]), Some(payload)).await?;

    repo.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0, 1, 0]),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location,
            offset: 0,
            length: virtual_bytes.len() as u64,
        })),
    )
    .await?;

    let snapshot = repo.commit(Ref::DEFAULT_BRANCH, "three kinds", None).await?;
    let reopened = Repository::update(Arc::clone(&storage), snapshot).build();
    for (coord, expected) in [
        (ChunkIndices(vec![0, 0, 0]), inline),
        (ChunkIndices(vec![0, 0, 1]), materialised),
        (ChunkIndices(vec![0, 1, 0]), virtual_bytes),
    ] {
        let reader = reopened.get_chunk_reader(&path, &coord, &ByteRange::ALL).await?;
        assert_eq!(get_chunk(reader).await?, Some(expected));
    }
    Ok(())
}

/// The repository API end to end: groups, arrays, chunks, commit and ancestry.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__repository_api_end_to_end() -> GateResult {
    let storage = mem("gate-repo");
    assert!(!Repository::exists(storage.as_ref()).await?);
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    assert!(Repository::exists(storage.as_ref()).await?);

    repo.add_group(Path::root()).await?;
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;
    repo.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0, 0, 0]),
        Some(ChunkPayload::Inline(Bytes::copy_from_slice(b"hi"))),
    )
    .await?;
    assert!(repo.has_uncommitted_changes());

    let snapshot = repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;
    assert!(!repo.has_uncommitted_changes());
    assert_eq!(repo.snapshot_id(), &snapshot);

    let mut paths: Vec<String> =
        repo.list_nodes().await?.map(|n| n.path.to_string()).collect();
    paths.sort();
    assert_eq!(paths, vec!["/", "/array"]);
    assert_eq!(repo.ancestry().await?.try_collect::<Vec<_>>().await?.len(), 2);
    assert_eq!(repo.all_chunks().await?.try_collect::<Vec<_>>().await?.len(), 1);
    Ok(())
}

/// The whole path again on the local-filesystem backend.
#[tokio::test(flavor = "multi_thread")]
async fn gate01__local_filesystem_backend() -> GateResult {
    let dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        Arc::new(ObjectStorage::new_local_store(dir.path())?);

    let repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let mut store = Store::from_repository(
        repo,
        icechunk::zarr::AccessMode::ReadWrite,
        Some("main".to_string()),
        None,
    );
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let data = Bytes::copy_from_slice(b"on disk");
    store.set("array/c/0/0/0", data.clone()).await?;
    let snapshot = store.commit("on disk").await?;

    let reopened = Store::from_repository(
        Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build(),
        icechunk::zarr::AccessMode::ReadOnly,
        Some("main".to_string()),
        None,
    );
    assert_eq!(reopened.snapshot_id().await, snapshot);
    assert_eq!(reopened.get("array/c/0/0/0", &ByteRange::ALL).await?, data);
    Ok(())
}
