// Verifier-owned conformance driver for milestone 01 (the spec-1 repository
// format of earth-mover/icechunk).
//
// Every assertion here traces to /app/icechunk-spec/ (the pinned Icechunk
// specification plus PROFILE.md, which resolves the places the specification
// and this milestone's contract disagree) or to /app/TEST_INTERFACE.md, which
// states the public Rust surface the milestone must expose.
//
// Group = the `gNN_<name>__` prefix of the test function name. The runner
// invokes one `cargo test` per group and asserts the executed count.
//
// Nothing here reaches the network, reads a cloud credential, or sleeps on a
// wall clock. Storage is either `ObjectStorage::new_in_memory_store` or
// `ObjectStorage::new_local_store` over a `tempfile::TempDir`; virtual chunk
// references address `file://` URLs inside a `TempDir`.

// Test function names carry their score-group prefix, which the runner filters
// on; that shape is not snake case.
#![allow(non_snake_case)]

use std::{num::NonZeroU64, sync::Arc};

use bytes::Bytes;
use futures::TryStreamExt;

use icechunk::{
    change_set::ChangeSet,
    format::{
        attributes::AttributesTable,
        manifest::{
            ChunkPayload, ChunkRef, VirtualChunkLocation, VirtualChunkRef,
            VirtualReferenceError,
        },
        snapshot::{NodeData, NodeType, Snapshot, ZarrArrayMetadata},
        ByteRange, ChunkIndices, IcechunkResult, Path, PathError, SnapshotId,
    },
    metadata::{ChunkKeyEncoding, ChunkShape, DataType, FillValue, UserAttributes},
    refs::{
        create_tag, fetch_branch_tip, fetch_ref, fetch_tag, list_refs, update_branch,
        BranchVersion, Ref, RefData, RefError,
    },
    repository::{get_chunk, RepositoryError},
    zarr::{
        AccessMode, ConsolidatedStore, KeyNotFoundError, RepositoryConfig as ZarrConfig,
        StorageConfig, StoreError, StoreOptions, VersionInfo,
    },
    MemCachingStorage, ObjectStorage, Repository, RepositoryConfig, Storage, Store,
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// A fresh in-memory repository backend. `prefix` only namespaces the keys
/// inside the store, so every call yields an independent, empty repository.
fn mem(prefix: &str) -> Arc<dyn Storage + Send + Sync> {
    Arc::new(ObjectStorage::new_in_memory_store(Some(prefix.to_string())))
}

/// The Zarr array metadata every driver that needs an array uses.
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

/// The Zarr group metadata document, as a Zarr client would write it.
const GROUP_META: &[u8] = br#"{"zarr_format":3, "node_type":"group"}"#;

/// The normalised form the store reads a bare group back as.
const GROUP_META_BACK: &[u8] = br#"{"zarr_format":3,"node_type":"group","attributes":null}"#;

/// A Zarr array metadata document already in the store's own member order, so
/// that reading it back returns these exact bytes.
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x","y","t"]}"#;

/// The same document with no `attributes` and no `storage_transformers`: the
/// store omits the member it does not hold (TEST_INTERFACE.md 5.2).
const ARRAY_META_NO_TRANSFORMERS: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":null,"shape":[4],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"dimension_names":["x"]}"#;

/// Every reference in the repository, as sorted `kind:name` labels, so that no
/// assertion depends on the order the backend happens to list refs in.
fn ref_labels(refs: &[Ref]) -> Vec<String> {
    let mut out: Vec<String> = refs
        .iter()
        .map(|r| match r {
            Ref::Tag(name) => format!("tag:{name}"),
            Ref::Branch(name) => format!("branch:{name}"),
        })
        .collect();
    out.sort();
    out
}

/// Every key in the store, sorted.
async fn sorted_keys(store: &Store) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut keys = store.list().await?.try_collect::<Vec<_>>().await?;
    keys.sort();
    Ok(keys)
}

/// Every key under `prefix`, sorted.
async fn sorted_prefix(
    store: &Store,
    prefix: &str,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut keys = store.list_prefix(prefix).await?.try_collect::<Vec<_>>().await?;
    keys.sort();
    Ok(keys)
}

/// Every immediate child of `prefix`, sorted.
async fn sorted_dir(
    store: &Store,
    prefix: &str,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let mut keys = store.list_dir(prefix).await?.try_collect::<Vec<_>>().await?;
    keys.sort();
    Ok(keys)
}

/// The messages of the repository's ancestry, newest first.
async fn ancestry_messages(
    repo: &Repository,
) -> Result<Vec<String>, Box<dyn std::error::Error>> {
    let all = repo.ancestry().await?.try_collect::<Vec<_>>().await?;
    Ok(all.into_iter().map(|m| m.message).collect())
}

/// A store over a fresh in-memory repository, already on `main`.
async fn mem_store(prefix: &str) -> Result<Store, Box<dyn std::error::Error>> {
    let storage = mem(prefix);
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();
    Ok(Store::from_repository(
        repo,
        AccessMode::ReadWrite,
        Some(Ref::DEFAULT_BRANCH.to_string()),
        None,
    ))
}

/// A store holding one group at the root and one array at `/array`.
async fn store_with_array(prefix: &str) -> Result<Store, Box<dyn std::error::Error>> {
    let store = mem_store(prefix).await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    Ok(store)
}

// ---------------------------------------------------------------------------
// g01_repository_init — spec.md "Initialize New Repository", "Reference Files"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g01_repository_init__exists_is_false_before_and_true_after() -> TestResult {
    let storage = mem("init-exists");
    assert!(!Repository::exists(storage.as_ref()).await?);
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();
    assert!(Repository::exists(storage.as_ref()).await?);
    // `storage()` hands back the backend the repository was built on.
    assert!(Repository::exists(repo.storage().as_ref()).await?);
    Ok(())
}

#[tokio::test]
async fn g01_repository_init__creates_the_default_branch() -> TestResult {
    let storage = mem("init-branch");
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();

    assert_eq!(Ref::DEFAULT_BRANCH, "main");
    let tip = fetch_branch_tip(storage.as_ref(), Ref::DEFAULT_BRANCH).await?;
    assert_eq!(&tip.snapshot, repo.snapshot_id());
    assert_eq!(ref_labels(&list_refs(storage.as_ref()).await?), vec!["branch:main"]);
    Ok(())
}

#[tokio::test]
async fn g01_repository_init__starts_from_one_empty_snapshot() -> TestResult {
    let storage = mem("init-snapshot");
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();

    assert!(!repo.has_uncommitted_changes());
    assert_eq!(repo.list_nodes().await?.count(), 0);
    assert_eq!(
        ancestry_messages(&repo).await?,
        vec![Snapshot::INITIAL_COMMIT_MESSAGE.to_string()]
    );
    Ok(())
}

#[tokio::test]
async fn g01_repository_init__initialising_twice_is_rejected() -> TestResult {
    let storage = mem("init-twice");
    let _repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let again = Repository::init(Arc::clone(&storage), false).await;
    assert!(
        matches!(again, Err(RepositoryError::AlreadyInitialized)),
        "initialising an initialised repository must be rejected"
    );
    Ok(())
}

#[tokio::test]
async fn g01_repository_init__opening_an_uninitialised_repository_fails() -> TestResult {
    let storage = mem("init-missing");
    assert!(Repository::from_branch_tip(Arc::clone(&storage), "main").await.is_err());
    assert!(Repository::from_tag(Arc::clone(&storage), "v1").await.is_err());

    let storage = mem("init-missing-2");
    let _repo = Repository::init(Arc::clone(&storage), false).await?.build();
    assert!(Repository::from_branch_tip(Arc::clone(&storage), "nope").await.is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// g02_paths — TEST_INTERFACE.md 4.2; node paths are absolute and canonical
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g02_paths__absolute_paths_parse() -> TestResult {
    assert_eq!(Path::root().to_string(), "/");
    assert_eq!(Path::new("/a")?.to_string(), "/a");
    assert_eq!(Path::new("/a/b/c")?.to_string(), "/a/b/c");

    let converted: Path = "/a/b".try_into()?;
    assert_eq!(converted, Path::new("/a/b")?);
    let owned: Path = String::from("/a/b").try_into()?;
    assert_eq!(owned, converted);
    Ok(())
}

#[tokio::test]
async fn g02_paths__relative_paths_are_rejected() -> TestResult {
    assert_eq!(Path::new("a/b"), Err(PathError::NotAbsolute));
    assert_eq!(Path::new("b"), Err(PathError::NotAbsolute));
    assert!(Path::try_from("relative").is_err());
    Ok(())
}

#[tokio::test]
async fn g02_paths__non_canonical_paths_are_rejected() -> TestResult {
    assert_eq!(Path::new("/a/../b"), Err(PathError::NotCanonic));
    assert_eq!(Path::new("/a/b/.."), Err(PathError::NotCanonic));
    assert_eq!(Path::new("/.."), Err(PathError::NotCanonic));
    // A path that needs no normalisation is accepted unchanged.
    assert_eq!(Path::new("/a/b")?.to_string(), "/a/b");
    Ok(())
}

#[tokio::test]
async fn g02_paths__prefixes_are_component_wise() -> TestResult {
    let ab = Path::new("/a/b")?;
    assert!(ab.starts_with(&Path::root()));
    assert!(ab.starts_with(&Path::new("/a")?));
    assert!(ab.starts_with(&ab));
    assert!(!Path::new("/ab")?.starts_with(&Path::new("/a")?));

    let ancestors: Vec<String> = ab.ancestors().map(|p| p.to_string()).collect();
    assert_eq!(ancestors, vec!["/a/b", "/a", "/"]);
    Ok(())
}

// ---------------------------------------------------------------------------
// g03_object_ids — spec.md "Reference Files" (ObjectId); PROFILE.md P1
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g03_object_ids__are_twelve_bytes() -> TestResult {
    let id = SnapshotId::new([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]);
    assert_eq!(id.0.len(), 12);
    assert_eq!(SnapshotId::FAKE, SnapshotId::new([0u8; 12]));
    assert_eq!(SnapshotId::FAKE.0, [0u8; 12]);
    Ok(())
}

#[tokio::test]
async fn g03_object_ids__render_as_crockford_base32() -> TestResult {
    let id = SnapshotId::new([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]);
    assert_eq!(String::from(&id), "000G40R40M30E209185G");
    assert_eq!(id.to_string(), "000G40R40M30E209185G");
    assert_eq!(SnapshotId::try_from("000G40R40M30E209185G")?, id);
    Ok(())
}

#[tokio::test]
async fn g03_object_ids__serialise_as_that_string() -> TestResult {
    let id = SnapshotId::new([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]);
    assert_eq!(serde_json::to_string(&id)?, r#""000G40R40M30E209185G""#);

    let random = SnapshotId::random();
    let bytes = serde_json::to_vec(&random)?;
    assert_eq!(serde_json::from_slice::<SnapshotId>(bytes.as_slice())?, random);
    Ok(())
}

#[tokio::test]
async fn g03_object_ids__reject_the_wrong_length_and_are_unique() -> TestResult {
    assert!(SnapshotId::try_from("000G").is_err());
    assert!(SnapshotId::try_from([0u8; 11].as_slice()).is_err());
    assert!(SnapshotId::try_from([0u8; 13].as_slice()).is_err());
    assert_eq!(SnapshotId::try_from([7u8; 12].as_slice())?, SnapshotId::new([7u8; 12]));

    let mut seen = std::collections::HashSet::new();
    for _ in 0..32 {
        seen.insert(SnapshotId::random());
    }
    assert_eq!(seen.len(), 32, "random snapshot ids must not collide");
    Ok(())
}

// ---------------------------------------------------------------------------
// g04_branch_refs — spec.md "Reference Files", "Creating and Updating Branches"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g04_branch_refs__creation_names_no_current_snapshot() -> TestResult {
    let storage = mem("branch-create");
    let s = storage.as_ref();
    let (s1, s2) = (SnapshotId::random(), SnapshotId::random());

    // Creating a branch means "there is no current snapshot"; naming one that
    // does not exist is a conflict, not a create.
    assert!(update_branch(s, "b0", s1.clone(), Some(&s2), false).await.is_err());
    assert_eq!(update_branch(s, "b1", s1.clone(), None, false).await?, BranchVersion(0));
    assert_eq!(fetch_branch_tip(s, "b1").await?.snapshot, s1);
    Ok(())
}

#[tokio::test]
async fn g04_branch_refs__update_is_compare_and_swap() -> TestResult {
    let storage = mem("branch-cas");
    let s = storage.as_ref();
    let (s1, s2, s3) = (SnapshotId::random(), SnapshotId::random(), SnapshotId::random());

    assert_eq!(update_branch(s, "b1", s1.clone(), None, false).await?, BranchVersion(0));
    assert_eq!(
        update_branch(s, "b1", s2.clone(), Some(&s1), false).await?,
        BranchVersion(1)
    );
    assert_eq!(fetch_branch_tip(s, "b1").await?.snapshot, s2);

    // A stale parent is refused and reports both sides of the comparison.
    let stale = update_branch(s, "b1", s3.clone(), Some(&s1), false).await;
    assert!(
        matches!(
            &stale,
            Err(RefError::Conflict { expected_parent, actual_parent })
                if expected_parent.as_ref() == Some(&s1)
                    && actual_parent.as_ref() == Some(&s2)
        ),
        "a stale parent must be refused as a conflict"
    );
    assert_eq!(fetch_branch_tip(s, "b1").await?.snapshot, s2);
    Ok(())
}

#[tokio::test]
async fn g04_branch_refs__versions_increase_by_one() -> TestResult {
    let storage = mem("branch-versions");
    let s = storage.as_ref();
    let ids: Vec<SnapshotId> = (0..4).map(|_| SnapshotId::random()).collect();

    assert_eq!(
        update_branch(s, "b1", ids[0].clone(), None, false).await?,
        BranchVersion(0)
    );
    for (n, pair) in ids.windows(2).enumerate() {
        assert_eq!(
            update_branch(s, "b1", pair[1].clone(), Some(&pair[0]), false).await?,
            BranchVersion(n as u64 + 1)
        );
    }
    assert_eq!(fetch_branch_tip(s, "b1").await?.snapshot, ids[3]);
    Ok(())
}

#[tokio::test]
async fn g04_branch_refs__recreating_an_existing_branch_conflicts() -> TestResult {
    let storage = mem("branch-recreate");
    let s = storage.as_ref();
    let (s1, s2) = (SnapshotId::random(), SnapshotId::random());

    update_branch(s, "b1", s1.clone(), None, false).await?;
    let recreate = update_branch(s, "b1", s2.clone(), None, false).await;
    assert!(
        matches!(
            &recreate,
            Err(RefError::Conflict { expected_parent, actual_parent })
                if expected_parent.is_none() && actual_parent.as_ref() == Some(&s1)
        ),
        "creating a branch that already exists must conflict"
    );
    assert_eq!(fetch_branch_tip(s, "b1").await?.snapshot, s1);
    Ok(())
}

#[tokio::test]
async fn g04_branch_refs__listing_lookup_and_name_validation() -> TestResult {
    let storage = mem("branch-list");
    let s = storage.as_ref();
    let s1 = SnapshotId::random();

    update_branch(s, "main", s1.clone(), None, false).await?;
    update_branch(s, "dev", s1.clone(), None, false).await?;
    assert_eq!(ref_labels(&list_refs(s).await?), vec!["branch:dev", "branch:main"]);

    let (which, data) = fetch_ref(s, "dev").await?;
    assert_eq!(which, Ref::Branch("dev".to_string()));
    assert_eq!(data.snapshot, s1);

    // A reference file is `RefData`'s JSON serialisation (PROFILE.md P6): one
    // member, `snapshot`, carrying the id's Crockford base32 string.
    let as_json = serde_json::to_value(&data)?;
    assert_eq!(as_json["snapshot"], serde_json::json!(String::from(&s1)));
    assert_eq!(serde_json::from_value::<RefData>(as_json)?, data);

    assert!(matches!(
        fetch_branch_tip(s, "absent").await,
        Err(RefError::RefNotFound(_))
    ));
    assert!(fetch_ref(s, "absent").await.is_err());
    // A branch name may not contain `/`.
    assert!(matches!(
        update_branch(s, "a/b", s1.clone(), None, false).await,
        Err(RefError::InvalidRefName(_))
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// g05_tags — spec.md "Reference Files" (tags), "Tags", "Create New Tag"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g05_tags__are_created_and_fetched_by_name() -> TestResult {
    let storage = mem("tag-create");
    let s = storage.as_ref();
    let snap = SnapshotId::random();

    create_tag(s, "v1", snap.clone(), false).await?;
    assert_eq!(fetch_tag(s, "v1").await?.snapshot, snap);

    let (which, data) = fetch_ref(s, "v1").await?;
    assert_eq!(which, Ref::Tag("v1".to_string()));
    assert_eq!(data.snapshot, snap);

    // A bare name that is both a tag and a branch resolves to the tag.
    let other = SnapshotId::random();
    update_branch(s, "v1", other.clone(), None, false).await?;
    assert_eq!(fetch_branch_tip(s, "v1").await?.snapshot, other);
    let (which, data) = fetch_ref(s, "v1").await?;
    assert_eq!(which, Ref::Tag("v1".to_string()));
    assert_eq!(data.snapshot, snap);
    Ok(())
}

#[tokio::test]
async fn g05_tags__are_immutable() -> TestResult {
    let storage = mem("tag-immutable");
    let s = storage.as_ref();
    let (snap, other) = (SnapshotId::random(), SnapshotId::random());

    create_tag(s, "v1", snap.clone(), false).await?;
    assert!(matches!(
        create_tag(s, "v1", other.clone(), false).await,
        Err(RefError::TagAlreadyExists(_))
    ));
    assert!(matches!(
        create_tag(s, "v1", snap.clone(), false).await,
        Err(RefError::TagAlreadyExists(_))
    ));
    assert_eq!(fetch_tag(s, "v1").await?.snapshot, snap);
    Ok(())
}

#[tokio::test]
async fn g05_tags__unknown_names_are_not_found() -> TestResult {
    let storage = mem("tag-missing");
    let s = storage.as_ref();

    assert!(matches!(fetch_tag(s, "absent").await, Err(RefError::RefNotFound(_))));
    assert!(fetch_ref(s, "absent").await.is_err());
    assert!(matches!(
        create_tag(s, "a/b", SnapshotId::random(), false).await,
        Err(RefError::InvalidRefName(_))
    ));
    Ok(())
}

#[tokio::test]
async fn g05_tags__coexist_with_branches_in_the_listing() -> TestResult {
    let storage = mem("tag-list");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group(Path::root()).await?;
    let snap = repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;

    repo.tag("v1", &snap).await?;
    repo.tag("v2", &snap).await?;
    assert_eq!(
        ref_labels(&list_refs(storage.as_ref()).await?),
        vec!["branch:main", "tag:v1", "tag:v2"]
    );
    assert_eq!(fetch_tag(storage.as_ref(), "v1").await?.snapshot, snap);
    Ok(())
}

#[tokio::test]
async fn g05_tags__a_repository_cannot_overwrite_one() -> TestResult {
    let storage = mem("tag-overwrite");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group(Path::root()).await?;
    let snap = repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;

    repo.tag("v1", &snap).await?;
    let again = repo.tag("v1", &snap).await;
    assert!(again.is_err(), "tags are immutable, re-tagging must fail");
    assert!(matches!(again, Err(RepositoryError::Ref(RefError::TagAlreadyExists(_)))));
    Ok(())
}

// ---------------------------------------------------------------------------
// g06_hierarchy — spec.md "Snapshot Files"; TEST_INTERFACE.md 4.3
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g06_hierarchy__groups_are_added_and_read_back() -> TestResult {
    let storage = mem("hier-groups");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();

    repo.add_group(Path::root()).await?;
    repo.add_group("/a".try_into()?).await?;
    repo.add_group("/a/b".try_into()?).await?;

    let node = repo.get_group(&"/a/b".try_into()?).await?;
    assert_eq!(node.path, Path::new("/a/b")?);
    assert_eq!(node.node_type(), NodeType::Group);
    assert!(matches!(node.node_data, NodeData::Group));
    assert!(repo.get_node(&Path::root()).await.is_ok());
    assert!(repo.has_uncommitted_changes());

    let mut paths: Vec<String> =
        repo.list_nodes().await?.map(|n| n.path.to_string()).collect();
    paths.sort();
    assert_eq!(paths, vec!["/", "/a", "/a/b"]);
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__arrays_carry_their_zarr_metadata() -> TestResult {
    let storage = mem("hier-arrays");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;

    repo.add_array(path.clone(), array_meta()).await?;
    let node = repo.get_array(&path).await?;
    assert_eq!(node.node_type(), NodeType::Array);
    match &node.node_data {
        NodeData::Array(meta, _) => assert_eq!(meta, &array_meta()),
        other => panic!("expected array node data, got {other:?}"),
    }

    let updated = ZarrArrayMetadata { shape: vec![4, 4, 4], ..array_meta() };
    repo.update_array(path.clone(), updated.clone()).await?;
    match &repo.get_array(&path).await?.node_data {
        NodeData::Array(meta, _) => assert_eq!(meta.shape, updated.shape),
        other => panic!("expected array node data, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__user_attributes_are_recorded() -> TestResult {
    let storage = mem("hier-attrs");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/a".try_into()?;
    repo.add_group(path.clone()).await?;

    let attrs = UserAttributes::try_new(br#"{"spam":"ham"}"#)?;
    repo.set_user_attributes(path.clone(), Some(attrs.clone())).await?;
    assert!(repo.get_node(&path).await?.user_attributes.is_some());
    // `to_bytes` serialises `parsed` back. The JSON layout it chooses —
    // whitespace and member order — is not part of the contract, so the
    // documents are compared, not the bytes.
    let serialised: serde_json::Value = serde_json::from_slice(attrs.to_bytes().as_ref())?;
    assert_eq!(serialised, attrs.parsed);
    assert_eq!(
        serialised,
        serde_json::from_slice::<serde_json::Value>(br#"{"spam":"ham"}"#)?
    );

    repo.set_user_attributes(path.clone(), None).await?;
    assert!(repo.get_node(&path).await?.user_attributes.is_none());

    assert!(UserAttributes::try_new(b"not json").is_err());
    assert!(repo
        .set_user_attributes("/absent".try_into()?, Some(attrs))
        .await
        .is_err());
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__adding_an_existing_node_is_rejected() -> TestResult {
    let storage = mem("hier-exists");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/a".try_into()?;

    repo.add_group(path.clone()).await?;
    assert!(matches!(
        repo.add_group(path.clone()).await,
        Err(RepositoryError::AlreadyExists { .. })
    ));
    assert!(matches!(
        repo.add_array(path.clone(), array_meta()).await,
        Err(RepositoryError::AlreadyExists { .. })
    ));
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__node_kind_mismatches_are_rejected() -> TestResult {
    let storage = mem("hier-kind");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let group: Path = "/g".try_into()?;
    let array: Path = "/arr".try_into()?;

    repo.add_group(group.clone()).await?;
    repo.add_array(array.clone(), array_meta()).await?;

    assert!(matches!(
        repo.get_array(&group).await,
        Err(RepositoryError::NotAnArray { .. })
    ));
    assert!(matches!(
        repo.get_group(&array).await,
        Err(RepositoryError::NotAGroup { .. })
    ));
    assert!(matches!(
        repo.get_node(&"/absent".try_into()?).await,
        Err(RepositoryError::NodeNotFound { .. })
    ));
    assert!(matches!(
        repo.update_array("/absent".try_into()?, array_meta()).await,
        Err(RepositoryError::NodeNotFound { .. })
    ));
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__deletes_are_idempotent() -> TestResult {
    let storage = mem("hier-delete");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let group: Path = "/g".try_into()?;
    let array: Path = "/arr".try_into()?;

    repo.add_group(group.clone()).await?;
    repo.add_array(array.clone(), array_meta()).await?;

    repo.delete_group(group.clone()).await?;
    assert!(repo.get_node(&group).await.is_err());
    // Deleting what is already gone succeeds.
    repo.delete_group(group.clone()).await?;
    repo.delete_array("/never-existed".try_into()?).await?;

    repo.delete_array(array.clone()).await?;
    assert!(repo.get_node(&array).await.is_err());
    assert_eq!(repo.list_nodes().await?.count(), 0);
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__metadata_value_types_convert() -> TestResult {
    // A data type's textual form is the lower-case Zarr name, and a name
    // outside the set must fail to parse (TEST_INTERFACE.md 4.5).
    for (text, data_type) in [
        ("bool", DataType::Bool),
        ("int32", DataType::Int32),
        ("uint64", DataType::UInt64),
        ("float64", DataType::Float64),
        ("complex128", DataType::Complex128),
        ("string", DataType::String),
        ("bytes", DataType::Bytes),
    ] {
        assert_eq!(DataType::try_from(text)?, data_type);
        assert_eq!(data_type.to_string(), text);
    }
    assert!(DataType::try_from("int31").is_err());
    assert!(DataType::try_from("Int32").is_err());

    // Every chunk key encoding converts to and from its byte, even though no
    // store key uses a separator other than `/` (TEST_INTERFACE.md 4.5, 8).
    for (byte, encoding) in [
        (b'/', ChunkKeyEncoding::Slash),
        (b'.', ChunkKeyEncoding::Dot),
        (b'x', ChunkKeyEncoding::Default),
    ] {
        assert_eq!(ChunkKeyEncoding::try_from(byte)?, encoding);
        assert_eq!(u8::from(encoding), byte);
    }
    assert!(ChunkKeyEncoding::try_from(b'z').is_err());

    // Attributes files are not implemented: the table is public and carries no
    // data (PROFILE.md P7).
    let table = AttributesTable {};
    assert_eq!(std::mem::size_of_val(&table), 0);
    Ok(())
}

#[tokio::test]
async fn g06_hierarchy__fill_values_are_read_against_the_data_type() -> TestResult {
    // A data type reports which integers it can hold (TEST_INTERFACE.md 4.5).
    assert!(DataType::Int8.fits_i64(127));
    assert!(!DataType::Int8.fits_i64(128));
    assert!(DataType::Int64.fits_i64(i64::MIN));
    assert!(DataType::UInt8.fits_u64(255));
    assert!(!DataType::UInt8.fits_u64(256));
    assert!(!DataType::Float32.fits_i64(0));

    let int = FillValue::from_data_type_and_json(&DataType::Int32, &serde_json::json!(7))?;
    assert_eq!(int, FillValue::Int32(7));
    assert_eq!(int.get_data_type(), DataType::Int32);

    // The three non-finite floats travel as the documented strings (5.2).
    assert_eq!(FillValue::NAN_STR, "NaN");
    assert_eq!(FillValue::INF_STR, "Infinity");
    assert_eq!(FillValue::NEG_INF_STR, "-Infinity");
    let inf = FillValue::from_data_type_and_json(
        &DataType::Float64,
        &serde_json::json!(FillValue::INF_STR),
    )?;
    assert_eq!(inf, FillValue::Float64(f64::INFINITY));
    assert_eq!(inf.get_data_type(), DataType::Float64);
    assert_eq!(
        FillValue::from_data_type_and_json(
            &DataType::Float64,
            &serde_json::json!(FillValue::NEG_INF_STR),
        )?,
        FillValue::Float64(f64::NEG_INFINITY)
    );
    match FillValue::from_data_type_and_json(
        &DataType::Float32,
        &serde_json::json!(FillValue::NAN_STR),
    )? {
        FillValue::Float32(f) => assert!(f.is_nan()),
        other => panic!("expected a float32 fill value, got {other:?}"),
    }

    // A JSON value the data type cannot hold fails. Only that it fails is
    // scored, never which variant reports it (section 6).
    let bad: IcechunkResult<FillValue> =
        FillValue::from_data_type_and_json(&DataType::Int32, &serde_json::json!("seven"));
    assert!(bad.is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// g07_chunk_refs — spec.md "Chunk Manifest Files"; PROFILE.md P8 (threshold)
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g07_chunk_refs__inline_payloads_round_trip() -> TestResult {
    let storage = mem("chunk-inline");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;

    let data = Bytes::copy_from_slice(b"hello");
    let coord = ChunkIndices(vec![0, 0, 0]);
    repo.set_chunk_ref(path.clone(), coord.clone(), Some(ChunkPayload::Inline(data.clone())))
        .await?;

    assert_eq!(
        repo.get_chunk_ref(&path, &coord).await?,
        Some(ChunkPayload::Inline(data.clone()))
    );
    let reader = repo.get_chunk_reader(&path, &coord, &ByteRange::ALL).await?;
    assert_eq!(get_chunk(reader).await?, Some(data.clone()));

    let reader = repo.get_chunk_reader(&path, &coord, &ByteRange::bounded(1, 4)).await?;
    assert_eq!(get_chunk(reader).await?, Some(data.slice(1..4)));
    Ok(())
}

#[tokio::test]
async fn g07_chunk_refs__the_writer_inlines_up_to_the_threshold() -> TestResult {
    assert_eq!(RepositoryConfig::default().inline_chunk_threshold_bytes, 512);
    assert!(!RepositoryConfig::default().unsafe_overwrite_refs);

    let storage = mem("chunk-threshold");
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();

    // Exactly the threshold is still inline: the predicate is `len > threshold`.
    let at = repo.get_chunk_writer()(Bytes::from(vec![7u8; 512])).await?;
    assert!(matches!(at, ChunkPayload::Inline(_)), "512 bytes must stay inline");

    let over = repo.get_chunk_writer()(Bytes::from(vec![7u8; 513])).await?;
    assert!(
        matches!(&over, ChunkPayload::Ref(ChunkRef { offset, length, .. })
            if *offset == 0 && *length == 513),
        "513 bytes must become a materialised chunk reference"
    );
    Ok(())
}

#[tokio::test]
async fn g07_chunk_refs__the_threshold_is_configurable() -> TestResult {
    let storage = mem("chunk-config");
    let repo = Repository::init(Arc::clone(&storage), false)
        .await?
        .with_inline_threshold_bytes(1)
        .build();
    assert!(matches!(
        repo.get_chunk_writer()(Bytes::from_static(b"a")).await?,
        ChunkPayload::Inline(_)
    ));
    assert!(matches!(
        repo.get_chunk_writer()(Bytes::from_static(b"ab")).await?,
        ChunkPayload::Ref(_)
    ));

    let storage = mem("chunk-config-2");
    let repo = Repository::init(Arc::clone(&storage), false)
        .await?
        .with_config(RepositoryConfig {
            inline_chunk_threshold_bytes: 4,
            unsafe_overwrite_refs: false,
        })
        // The escape hatch of PROFILE.md P8 exists and is settable; no scored
        // group turns it on.
        .with_unsafe_overwrite_refs(false)
        .build();
    assert!(matches!(
        repo.get_chunk_writer()(Bytes::from_static(b"abcd")).await?,
        ChunkPayload::Inline(_)
    ));
    assert!(matches!(
        repo.get_chunk_writer()(Bytes::from_static(b"abcde")).await?,
        ChunkPayload::Ref(_)
    ));
    Ok(())
}

#[tokio::test]
async fn g07_chunk_refs__materialised_chunks_honour_byte_ranges() -> TestResult {
    let storage = mem("chunk-material");
    let mut repo = Repository::init(Arc::clone(&storage), false)
        .await?
        .with_inline_threshold_bytes(1)
        .build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;

    let data = Bytes::from_static(b"0123456789abcdef");
    let payload = repo.get_chunk_writer()(data.clone()).await?;
    assert!(matches!(payload, ChunkPayload::Ref(_)));
    let coord = ChunkIndices(vec![0, 0, 0]);
    repo.set_chunk_ref(path.clone(), coord.clone(), Some(payload)).await?;

    for range in [
        ByteRange::ALL,
        ByteRange::bounded(2, 6),
        ByteRange::from_offset(10),
        ByteRange::to_offset(4),
        ByteRange::from_offset_with_length(3, 5),
    ] {
        let reader = repo.get_chunk_reader(&path, &coord, &range).await?;
        assert_eq!(
            get_chunk(reader).await?,
            Some(range.slice(data.clone())),
            "byte range {range:?} must window the chunk"
        );
    }
    assert_eq!(ByteRange::ALL.length(), None);
    assert_eq!(ByteRange::bounded(2, 6).length(), Some(4));
    Ok(())
}

#[tokio::test]
async fn g07_chunk_refs__absent_and_deleted_chunks_read_as_none() -> TestResult {
    let storage = mem("chunk-absent");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    let group: Path = "/g".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;
    repo.add_group(group.clone()).await?;

    let coord = ChunkIndices(vec![1, 1, 1]);
    assert_eq!(repo.get_chunk_ref(&path, &coord).await?, None);
    let reader = repo.get_chunk_reader(&path, &coord, &ByteRange::ALL).await?;
    assert_eq!(get_chunk(reader).await?, None);

    let data = Bytes::from_static(b"hello");
    repo.set_chunk_ref(path.clone(), coord.clone(), Some(ChunkPayload::Inline(data)))
        .await?;
    assert!(repo.get_chunk_ref(&path, &coord).await?.is_some());
    repo.set_chunk_ref(path.clone(), coord.clone(), None).await?;
    assert_eq!(repo.get_chunk_ref(&path, &coord).await?, None);

    // A chunk reference belongs to an array, and only to an array.
    assert!(matches!(
        repo.set_chunk_ref(group.clone(), coord.clone(), None).await,
        Err(RepositoryError::NotAnArray { .. })
    ));
    assert!(matches!(
        repo.set_chunk_ref("/absent".try_into()?, coord, None).await,
        Err(RepositoryError::NodeNotFound { .. })
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// g08_commit_ancestry — spec.md "Write New Snapshot", "Overview", "Snapshot Files"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g08_commit_ancestry__a_commit_moves_the_branch_tip() -> TestResult {
    let storage = mem("commit-tip");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let before = repo.snapshot_id().clone();

    repo.add_group(Path::root()).await?;
    assert!(repo.has_uncommitted_changes());
    let snap = repo.commit(Ref::DEFAULT_BRANCH, "first commit", None).await?;

    assert_ne!(snap, before);
    assert_eq!(repo.snapshot_id(), &snap);
    assert!(!repo.has_uncommitted_changes());
    assert_eq!(fetch_ref(storage.as_ref(), "main").await?.1.snapshot, snap);
    Ok(())
}

#[tokio::test]
async fn g08_commit_ancestry__lists_snapshots_newest_first() -> TestResult {
    let storage = mem("commit-ancestry");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();

    repo.add_group(Path::root()).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;
    repo.add_group("/a".try_into()?).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "second", None).await?;

    assert_eq!(
        ancestry_messages(&repo).await?,
        vec![
            "second".to_string(),
            "first".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string(),
        ]
    );
    Ok(())
}

#[tokio::test]
async fn g08_commit_ancestry__committing_nothing_is_rejected() -> TestResult {
    let storage = mem("commit-empty");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();

    assert!(matches!(
        repo.commit(Ref::DEFAULT_BRANCH, "nothing", None).await,
        Err(RepositoryError::NoChangesToCommit)
    ));
    assert!(matches!(
        repo.flush("nothing", Default::default()).await,
        Err(RepositoryError::NoChangesToCommit)
    ));
    // The branch is untouched by the refused commit.
    assert_eq!(
        fetch_branch_tip(storage.as_ref(), "main").await?.snapshot,
        *repo.snapshot_id()
    );
    Ok(())
}

#[tokio::test]
async fn g08_commit_ancestry__a_stale_writer_loses_the_commit() -> TestResult {
    let storage = mem("commit-conflict");
    let _init = Repository::init(Arc::clone(&storage), false).await?.build();

    let mut first = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    let mut second =
        Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    let parent = first.snapshot_id().clone();

    first.add_group("/a".try_into()?).await?;
    second.add_group("/b".try_into()?).await?;

    let winner = first.commit(Ref::DEFAULT_BRANCH, "from first", None).await?;
    let loser = second.commit(Ref::DEFAULT_BRANCH, "from second", None).await;
    assert!(
        matches!(
            &loser,
            Err(RepositoryError::Conflict { expected_parent, actual_parent })
                if expected_parent.as_ref() == Some(&parent)
                    && actual_parent.as_ref() == Some(&winner)
        ),
        "only one writer may move a branch from a given parent"
    );

    let reopened = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    assert_eq!(reopened.snapshot_id(), &winner);
    assert!(reopened.get_group(&"/a".try_into()?).await.is_ok());
    assert!(reopened.get_node(&"/b".try_into()?).await.is_err());
    Ok(())
}

#[tokio::test]
async fn g08_commit_ancestry__flush_writes_a_snapshot_without_moving_a_branch() -> TestResult
{
    let storage = mem("commit-flush");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let initial = repo.snapshot_id().clone();

    repo.add_group("/a".try_into()?).await?;
    let flushed = repo.flush("flushed", Default::default()).await?;
    assert_ne!(flushed, initial);
    assert_eq!(repo.snapshot_id(), &flushed);
    assert!(!repo.has_uncommitted_changes());

    // `flush` touched no reference; only `commit` does.
    assert_eq!(fetch_branch_tip(storage.as_ref(), "main").await?.snapshot, initial);

    // The snapshot it wrote is readable by id.
    let by_id = Repository::update(Arc::clone(&storage), flushed).build();
    assert!(by_id.get_group(&"/a".try_into()?).await.is_ok());
    Ok(())
}

// ---------------------------------------------------------------------------
// g09_time_travel — spec.md "From Snapshot ID", "From Branch", "From Tag"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g09_time_travel__a_snapshot_id_reopens_that_version() -> TestResult {
    let storage = mem("tt-snapshot");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group("/a".try_into()?).await?;
    let first = repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;
    repo.add_group("/b".try_into()?).await?;
    let second = repo.commit(Ref::DEFAULT_BRANCH, "second", None).await?;

    let at_first = Repository::update(Arc::clone(&storage), first.clone()).build();
    assert_eq!(at_first.snapshot_id(), &first);
    assert!(at_first.get_group(&"/a".try_into()?).await.is_ok());
    assert!(at_first.get_node(&"/b".try_into()?).await.is_err());

    let at_second = Repository::update(Arc::clone(&storage), second).build();
    assert!(at_second.get_group(&"/b".try_into()?).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn g09_time_travel__a_branch_tip_reopens_the_latest_version() -> TestResult {
    let storage = mem("tt-branch");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group("/a".try_into()?).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;
    repo.add_group("/b".try_into()?).await?;
    let second = repo.commit(Ref::DEFAULT_BRANCH, "second", None).await?;

    let reopened = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    assert_eq!(reopened.snapshot_id(), &second);
    assert!(reopened.get_group(&"/a".try_into()?).await.is_ok());
    assert!(reopened.get_group(&"/b".try_into()?).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn g09_time_travel__a_tag_pins_one_version() -> TestResult {
    let storage = mem("tt-tag");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group("/a".try_into()?).await?;
    let first = repo.commit(Ref::DEFAULT_BRANCH, "first", None).await?;
    repo.tag("v1", &first).await?;

    repo.add_group("/b".try_into()?).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "second", None).await?;

    let at_tag = Repository::from_tag(Arc::clone(&storage), "v1").await?.build();
    assert_eq!(at_tag.snapshot_id(), &first);
    assert!(at_tag.get_group(&"/a".try_into()?).await.is_ok());
    assert!(at_tag.get_node(&"/b".try_into()?).await.is_err());
    assert!(Repository::from_tag(Arc::clone(&storage), "absent").await.is_err());
    Ok(())
}

#[tokio::test]
async fn g09_time_travel__a_new_branch_diverges_from_the_old_one() -> TestResult {
    let storage = mem("tt-diverge");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group("/a".try_into()?).await?;
    let base = repo.commit(Ref::DEFAULT_BRANCH, "base", None).await?;

    assert_eq!(repo.new_branch("dev").await?, BranchVersion(0));
    repo.add_group("/dev-only".try_into()?).await?;
    let on_dev = repo.commit("dev", "dev work", None).await?;

    assert_eq!(fetch_branch_tip(storage.as_ref(), "main").await?.snapshot, base);
    assert_eq!(fetch_branch_tip(storage.as_ref(), "dev").await?.snapshot, on_dev);
    assert_eq!(
        ref_labels(&list_refs(storage.as_ref()).await?),
        vec!["branch:dev", "branch:main"]
    );

    let main = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    assert!(main.get_node(&"/dev-only".try_into()?).await.is_err());
    Ok(())
}

// ---------------------------------------------------------------------------
// g10_store_metadata — TEST_INTERFACE.md 4.9 (the Zarr key grammar)
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g10_store_metadata__the_root_group_round_trips() -> TestResult {
    let store = mem_store("store-meta-root").await?;

    assert!(matches!(
        store.get("zarr.json", &ByteRange::ALL).await,
        Err(StoreError::NotFound(KeyNotFoundError::NodeNotFound { path }))
            if path.to_string() == "/"
    ));

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    assert_eq!(
        store.get("zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(GROUP_META_BACK)
    );
    assert!(store.exists("zarr.json").await?);
    Ok(())
}

#[tokio::test]
async fn g10_store_metadata__attributes_are_normalised() -> TestResult {
    let store = mem_store("store-meta-attrs").await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store
        .set(
            "a/b/zarr.json",
            Bytes::copy_from_slice(
                br#"{"zarr_format":3, "node_type":"group", "attributes": {"spam":"ham", "eggs":42}}"#,
            ),
        )
        .await?;

    assert_eq!(
        store.get("a/b/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(
            br#"{"zarr_format":3,"node_type":"group","attributes":{"eggs":42,"spam":"ham"}}"#
        )
    );
    Ok(())
}

#[tokio::test]
async fn g10_store_metadata__array_metadata_round_trips() -> TestResult {
    let store = store_with_array("store-meta-array").await?;
    let meta = Bytes::copy_from_slice(ARRAY_META);

    assert_eq!(store.get("array/zarr.json", &ByteRange::ALL).await?, meta);
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::to_offset(5)).await?,
        meta.slice(..5)
    );
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::from_offset(5)).await?,
        meta.slice(5..)
    );
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::bounded(1, 24)).await?,
        meta.slice(1..24)
    );
    Ok(())
}

#[tokio::test]
async fn g10_store_metadata__malformed_metadata_is_rejected() -> TestResult {
    let store = mem_store("store-meta-bad").await?;

    assert!(matches!(
        store.set("zarr.json", Bytes::from_static(br#"{"zarr_format":3,"node_type":"nonsense"}"#)).await,
        Err(StoreError::BadMetadata(_))
    ));
    assert!(matches!(
        store.set("zarr.json", Bytes::from_static(b"not json at all")).await,
        Err(StoreError::BadMetadata(_))
    ));
    assert!(matches!(
        store.set("zarr.json", Bytes::from_static(br#"{"zarr_format":3}"#)).await,
        Err(StoreError::BadMetadata(_))
    ));
    assert!(!store.exists("zarr.json").await?);
    Ok(())
}

#[tokio::test]
async fn g10_store_metadata__malformed_keys_are_rejected() -> TestResult {
    let store = store_with_array("store-meta-keys").await?;

    assert!(matches!(
        store.get("foo", &ByteRange::ALL).await,
        Err(StoreError::InvalidKey { key }) if key == "foo"
    ));
    assert!(matches!(
        store.delete("array/foo").await,
        Err(StoreError::InvalidKey { key }) if key == "array/foo"
    ));
    assert!(matches!(
        store.set("array/c/0/x", Bytes::from_static(b"hi")).await,
        Err(StoreError::InvalidKey { .. })
    ));
    // `exists` answers `Ok(false)` for a well-formed key that resolves to
    // nothing, but propagates the error for a key that is not well formed.
    assert!(!store.exists("array/c/9/9/9").await?);
    assert!(matches!(
        store.exists("foo").await,
        Err(StoreError::InvalidKey { key }) if key == "foo"
    ));
    Ok(())
}

#[tokio::test]
async fn g10_store_metadata__storage_transformers_are_omitted_when_absent() -> TestResult {
    let store = mem_store("store-meta-transformers").await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store
        .set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META_NO_TRANSFORMERS))
        .await?;

    let back = store.get("array/zarr.json", &ByteRange::ALL).await?;
    assert_eq!(back, Bytes::copy_from_slice(ARRAY_META_NO_TRANSFORMERS));
    assert!(
        !String::from_utf8_lossy(back.as_ref()).contains("storage_transformers"),
        "an absent `storage_transformers` member is omitted, not written back as null"
    );
    Ok(())
}

#[tokio::test]
async fn g10_store_metadata__deleting_metadata_removes_the_node() -> TestResult {
    let store = store_with_array("store-meta-delete").await?;
    assert!(store.exists("array/zarr.json").await?);

    store.delete("array/zarr.json").await?;
    assert!(matches!(
        store.get("array/zarr.json", &ByteRange::ALL).await,
        Err(StoreError::NotFound(KeyNotFoundError::NodeNotFound { path }))
            if path.to_string() == "/array"
    ));
    assert!(!store.exists("array/zarr.json").await?);

    // The node can be created again under the same key.
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    assert!(store.exists("array/zarr.json").await?);
    Ok(())
}

// ---------------------------------------------------------------------------
// g11_store_chunks — TEST_INTERFACE.md 4.9; spec.md "Chunk Manifest Files"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g11_store_chunks__small_and_large_chunks_round_trip() -> TestResult {
    let store = store_with_array("store-chunk-rt").await?;

    let small = Bytes::copy_from_slice(b"hello");
    let big = Bytes::copy_from_slice(b"hello".repeat(512).as_slice());
    store.set("array/c/0/1/0", small.clone()).await?;
    store.set("array/c/0/1/1", big.clone()).await?;

    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, small);
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::bounded(1, 4)).await?, small.slice(1..4));
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::to_offset(2)).await?, small.slice(..2));
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::from_offset(3)).await?, small.slice(3..));

    assert_eq!(store.get("array/c/0/1/1", &ByteRange::ALL).await?, big);
    assert_eq!(store.get("array/c/0/1/1", &ByteRange::bounded(20, 90)).await?, big.slice(20..90));
    assert_eq!(store.get("array/c/0/1/1", &ByteRange::to_offset(5)).await?, big.slice(..5));
    Ok(())
}

#[tokio::test]
async fn g11_store_chunks__survive_commit_and_reopen() -> TestResult {
    let storage = mem("store-chunk-commit");
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let mut store = Store::from_repository(
        repo,
        AccessMode::ReadWrite,
        Some(Ref::DEFAULT_BRANCH.to_string()),
        None,
    );
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let small = Bytes::copy_from_slice(b"hello");
    let big = Bytes::copy_from_slice(b"hello".repeat(512).as_slice());
    store.set("array/c/0/1/0", small.clone()).await?;
    store.set("array/c/0/1/1", big.clone()).await?;
    let snapshot = store.commit("commit").await?;

    let reopened = Store::from_repository(
        Repository::update(Arc::clone(&storage), snapshot).build(),
        AccessMode::ReadWrite,
        None,
        None,
    );
    assert_eq!(reopened.get("array/c/0/1/0", &ByteRange::ALL).await?, small);
    assert_eq!(reopened.get("array/c/0/1/1", &ByteRange::ALL).await?, big);
    assert_eq!(reopened.get("array/zarr.json", &ByteRange::ALL).await?, Bytes::copy_from_slice(ARRAY_META));
    Ok(())
}

#[tokio::test]
async fn g11_store_chunks__the_root_array_has_chunk_keys_of_its_own() -> TestResult {
    // `zarr.json` may describe an array, and then `c` and `c/<i>/<j>/…` are
    // that array's chunk keys (TEST_INTERFACE.md 5.1).
    let store = mem_store("store-chunk-root").await?;
    store.set("zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let at_coord = Bytes::copy_from_slice(b"at a coordinate");
    let at_empty = Bytes::copy_from_slice(b"at the empty coordinate");
    store.set("c/0/1/0", at_coord.clone()).await?;
    store.set("c", at_empty.clone()).await?;

    assert_eq!(store.get("c/0/1/0", &ByteRange::ALL).await?, at_coord);
    assert_eq!(store.get("c", &ByteRange::ALL).await?, at_empty);
    assert!(store.exists("c").await?);
    assert_eq!(
        sorted_keys(&store).await?,
        vec!["c".to_string(), "c/0/1/0".to_string(), "zarr.json".to_string()]
    );
    Ok(())
}

#[tokio::test]
async fn g11_store_chunks__set_if_not_exists_keeps_the_old_value() -> TestResult {
    let store = store_with_array("store-chunk-sine").await?;
    let first = Bytes::copy_from_slice(b"hello");
    let second = Bytes::copy_from_slice(b"world");

    store.set_if_not_exists("array/c/0/0/0", first.clone()).await?;
    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, first);

    store.set_if_not_exists("array/c/0/0/0", second.clone()).await?;
    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, first);

    store.set("array/c/0/0/0", second.clone()).await?;
    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, second);
    Ok(())
}

#[tokio::test]
async fn g11_store_chunks__deletes_are_idempotent_and_reads_report_not_found() -> TestResult
{
    let store = store_with_array("store-chunk-delete").await?;
    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"hello")).await?;

    store.delete("array/c/0/1/0").await?;
    store.delete("array/c/0/1/0").await?;
    store.delete("array/c/1/1/1").await?;

    assert!(matches!(
        store.get("array/c/0/1/0", &ByteRange::ALL).await,
        Err(StoreError::NotFound(KeyNotFoundError::ChunkNotFound { key, path, coords }))
            if key == "array/c/0/1/0"
                && path.to_string() == "/array"
                && coords == ChunkIndices(vec![0, 1, 0])
    ));
    assert!(!store.exists("array/c/0/1/0").await?);
    Ok(())
}

#[tokio::test]
async fn g11_store_chunks__get_partial_values_preserves_request_order() -> TestResult {
    let store = mem_store("store-chunk-partial").await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store
        .set(
            "array/zarr.json",
            Bytes::copy_from_slice(
                br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[8],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"mycodec","configuration":{"foo":42}}],"storage_transformers":[{"name":"mytransformer","configuration":{"bar":43}}],"dimension_names":["x"]}"#,
            ),
        )
        .await?;

    let pairs: Vec<(String, Bytes)> = (0i32..8)
        .map(|i| (format!("array/c/{i}"), Bytes::copy_from_slice(&i.to_be_bytes())))
        .collect();
    for (key, value) in pairs.iter() {
        store.set(key.as_str(), value.clone()).await?;
    }

    let forward = store
        .get_partial_values(pairs.iter().map(|(k, _)| (k.clone(), ByteRange::ALL)))
        .await?;
    let forward: Vec<Bytes> = forward.into_iter().collect::<Result<_, _>>()?;
    assert_eq!(forward, pairs.iter().map(|(_, v)| v.clone()).collect::<Vec<_>>());

    let reverse = store
        .get_partial_values(pairs.iter().rev().map(|(k, _)| (k.clone(), ByteRange::ALL)))
        .await?;
    let reverse: Vec<Bytes> = reverse.into_iter().collect::<Result<_, _>>()?;
    assert_eq!(reverse, pairs.iter().rev().map(|(_, v)| v.clone()).collect::<Vec<_>>());
    Ok(())
}

// ---------------------------------------------------------------------------
// g12_store_listing — TEST_INTERFACE.md 4.9 (listing); spec.md "Storage Operations"
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g12_store_listing__an_empty_store_lists_nothing() -> TestResult {
    let store = mem_store("store-list-empty").await?;
    assert!(store.empty().await?);
    assert!(!store.exists("zarr.json").await?);
    assert_eq!(sorted_keys(&store).await?, Vec::<String>::new());
    assert!(store.supports_listing()?);
    Ok(())
}

#[tokio::test]
async fn g12_store_listing__list_reports_every_metadata_and_chunk_key() -> TestResult {
    let store = store_with_array("store-list-all").await?;
    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"hello")).await?;
    store.set("array/c/1/1/1", Bytes::copy_from_slice(b"hello")).await?;

    assert!(!store.empty().await?);
    assert_eq!(
        sorted_keys(&store).await?,
        vec![
            "array/c/0/1/0".to_string(),
            "array/c/1/1/1".to_string(),
            "array/zarr.json".to_string(),
            "zarr.json".to_string(),
        ]
    );
    Ok(())
}

#[tokio::test]
async fn g12_store_listing__list_prefix_matches_whole_components() -> TestResult {
    let store = mem_store("store-list-prefix").await?;
    for key in ["zarr.json", "group/zarr.json", "group-suffix/zarr.json"] {
        store.set(key, Bytes::copy_from_slice(GROUP_META)).await?;
    }
    store.set("group/array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    assert_eq!(
        sorted_prefix(&store, "group/").await?,
        vec!["group/array/zarr.json".to_string(), "group/zarr.json".to_string()]
    );
    assert_eq!(
        sorted_prefix(&store, "group/array/").await?,
        vec!["group/array/zarr.json".to_string()]
    );
    assert_eq!(sorted_prefix(&store, "/").await?, sorted_keys(&store).await?);
    Ok(())
}

#[tokio::test]
async fn g12_store_listing__list_dir_reports_immediate_children() -> TestResult {
    let store = store_with_array("store-list-dir").await?;
    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"hello")).await?;
    store.set("array/c/1/1/1", Bytes::copy_from_slice(b"hello")).await?;

    assert_eq!(
        sorted_dir(&store, "/").await?,
        vec!["array".to_string(), "zarr.json".to_string()]
    );
    assert_eq!(
        sorted_dir(&store, "array").await?,
        vec!["c".to_string(), "zarr.json".to_string()]
    );
    assert_eq!(
        sorted_dir(&store, "array/").await?,
        vec!["c".to_string(), "zarr.json".to_string()]
    );
    assert_eq!(sorted_dir(&store, "array/c/").await?, vec!["0".to_string(), "1".to_string()]);
    assert_eq!(sorted_dir(&store, "array/c/1/").await?, vec!["1".to_string()]);
    Ok(())
}

#[tokio::test]
async fn g12_store_listing__clear_empties_the_hierarchy() -> TestResult {
    let storage = mem("store-list-clear");
    let repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let mut store = Store::from_repository(
        repo,
        AccessMode::ReadWrite,
        Some(Ref::DEFAULT_BRANCH.to_string()),
        None,
    );
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/1/0/0", Bytes::copy_from_slice(b"hello")).await?;
    store.commit("initial commit").await?;
    assert_eq!(sorted_keys(&store).await?.len(), 3);

    store.clear().await?;
    assert_eq!(sorted_keys(&store).await?, Vec::<String>::new());
    assert!(store.empty().await?);

    let cleared = store.commit("cleared").await?;
    assert_eq!(sorted_keys(&store).await?, Vec::<String>::new());

    let reopened = Store::from_repository(
        Repository::update(Arc::clone(&storage), cleared).build(),
        AccessMode::ReadWrite,
        None,
        None,
    );
    assert_eq!(sorted_keys(&reopened).await?, Vec::<String>::new());
    Ok(())
}

// ---------------------------------------------------------------------------
// g13_store_versioning — spec.md "Reference Files"; TEST_INTERFACE.md 4.9
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g13_store_versioning__commit_then_checkout_by_snapshot_id() -> TestResult {
    let storage = mem("store-ver-checkout");
    let mut store = Store::new_from_storage(Arc::clone(&storage)).await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;

    let first_data = Bytes::copy_from_slice(b"hello");
    store.set("array/c/0/1/0", first_data.clone()).await?;
    let first = store.commit("initial commit").await?;

    let second_data = Bytes::copy_from_slice(b"world");
    store.set("array/c/0/1/0", second_data.clone()).await?;
    let second = store.commit("update").await?;
    assert_ne!(first, second);
    assert_eq!(store.snapshot_id().await, second);

    store.checkout(VersionInfo::SnapshotId(first.clone())).await?;
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, first_data);
    assert_eq!(store.current_branch(), &None);
    assert_eq!(store.current_version().await, VersionInfo::SnapshotId(first));

    store.checkout(VersionInfo::SnapshotId(second)).await?;
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, second_data);
    Ok(())
}

#[tokio::test]
async fn g13_store_versioning__uncommitted_changes_block_a_checkout() -> TestResult {
    let storage = mem("store-ver-dirty");
    let mut store = Store::new_from_storage(Arc::clone(&storage)).await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let committed = Bytes::copy_from_slice(b"hello");
    store.set("array/c/0/1/0", committed.clone()).await?;
    let first = store.commit("initial commit").await?;
    assert!(!store.has_uncommitted_changes().await);

    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"dirty")).await?;
    assert!(store.has_uncommitted_changes().await);
    assert!(matches!(
        store.checkout(VersionInfo::SnapshotId(first.clone())).await,
        Err(StoreError::UncommittedChanges)
    ));
    assert!(matches!(store.new_branch("dev").await, Err(StoreError::UncommittedChanges)));

    store.reset().await?;
    assert!(!store.has_uncommitted_changes().await);
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, committed);
    Ok(())
}

#[tokio::test]
async fn g13_store_versioning__a_new_branch_takes_the_writes() -> TestResult {
    let storage = mem("store-ver-branch");
    let mut store = Store::new_from_storage(Arc::clone(&storage)).await?;
    assert_eq!(store.current_branch(), &Some("main".to_string()));
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let base_data = Bytes::copy_from_slice(b"hello");
    store.set("array/c/0/1/0", base_data.clone()).await?;
    let base = store.commit("base").await?;

    let (from, version) = store.new_branch("dev").await?;
    assert_eq!(from, base);
    assert_eq!(version, BranchVersion(0));
    assert_eq!(store.current_branch(), &Some("dev".to_string()));

    let dev_data = Bytes::copy_from_slice(b"world");
    store.set("array/c/0/1/0", dev_data.clone()).await?;
    let on_dev = store.commit("dev work").await?;
    assert_ne!(on_dev, base);

    store.checkout(VersionInfo::BranchTipRef("main".to_string())).await?;
    assert_eq!(store.current_branch(), &Some("main".to_string()));
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, base_data);

    store.checkout(VersionInfo::BranchTipRef("dev".to_string())).await?;
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, dev_data);
    Ok(())
}

#[tokio::test]
async fn g13_store_versioning__a_tag_checkout_detaches_the_store() -> TestResult {
    let storage = mem("store-ver-tag");
    let mut store = Store::new_from_storage(Arc::clone(&storage)).await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    let tagged_data = Bytes::copy_from_slice(b"hello");
    store.set("array/c/0/1/0", tagged_data.clone()).await?;
    let tagged = store.commit("tagged").await?;

    store.tag("v1", &tagged).await?;
    store.set("array/c/0/1/0", Bytes::copy_from_slice(b"world")).await?;
    store.commit("after the tag").await?;

    store.checkout(VersionInfo::TagRef("v1".to_string())).await?;
    assert_eq!(store.current_branch(), &None);
    assert_eq!(store.get("array/c/0/1/0", &ByteRange::ALL).await?, tagged_data);

    // A detached store is not on a branch, so it cannot commit.
    store.set("array/c/0/1/1", Bytes::copy_from_slice(b"nope")).await?;
    assert!(matches!(store.commit("detached").await, Err(StoreError::NotOnBranch)));

    store.reset().await?;
    assert!(store.checkout(VersionInfo::TagRef("absent".to_string())).await.is_err());
    Ok(())
}

#[tokio::test]
async fn g13_store_versioning__ancestry_and_change_set_bytes() -> TestResult {
    let storage = mem("store-ver-ancestry");
    let mut store = Store::new_from_storage(Arc::clone(&storage)).await?;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.commit("first").await?;
    store.set("group/zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.commit("second").await?;

    let history = store.ancestry().await?.try_collect::<Vec<_>>().await?;
    let messages: Vec<String> = history.into_iter().map(|m| m.message).collect();
    assert_eq!(
        messages,
        vec![
            "second".to_string(),
            "first".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string(),
        ]
    );

    assert!(ChangeSet::import_from_bytes(store.change_set_bytes().await?.as_slice())?
        .is_empty());
    store.set("other/zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    assert!(!ChangeSet::import_from_bytes(store.change_set_bytes().await?.as_slice())?
        .is_empty());
    Ok(())
}

// ---------------------------------------------------------------------------
// g14_virtual_refs — spec.md "Chunk Manifest Files" (VirtualChunkRef),
//                    "Chunk Files"; PROFILE.md P10 (virtual references)
// ---------------------------------------------------------------------------

/// Write `bytes` into `dir` under `name` and return its `file://` location.
fn virtual_file(
    dir: &tempfile::TempDir,
    name: &str,
    bytes: &Bytes,
) -> Result<VirtualChunkLocation, Box<dyn std::error::Error>> {
    let path = dir.path().join(name);
    std::fs::write(&path, bytes.as_ref())?;
    let as_str = path.to_str().ok_or("temporary path must be UTF-8")?;
    Ok(VirtualChunkLocation::from_absolute_path(&format!("file://{as_str}"))?)
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_virtual_refs__a_local_file_reference_resolves() -> TestResult {
    let chunks = tempfile::TempDir::new()?;
    let bytes = Bytes::copy_from_slice(b"first");
    let location = virtual_file(&chunks, "chunk-1", &bytes)?;

    let repo_dir = tempfile::TempDir::new()?;
    let storage: Arc<dyn Storage + Send + Sync> =
        Arc::new(ObjectStorage::new_local_store(repo_dir.path())?);
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;

    let coord = ChunkIndices(vec![0, 0, 0]);
    repo.set_chunk_ref(
        path.clone(),
        coord.clone(),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location,
            offset: 0,
            length: bytes.len() as u64,
        })),
    )
    .await?;

    let reader = repo.get_chunk_reader(&path, &coord, &ByteRange::ALL).await?;
    assert_eq!(get_chunk(reader).await?, Some(bytes.clone()));
    assert!(matches!(
        repo.get_chunk_ref(&path, &coord).await?,
        Some(ChunkPayload::Virtual(_))
    ));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_virtual_refs__offset_and_length_window_the_file() -> TestResult {
    let chunks = tempfile::TempDir::new()?;
    let bytes = Bytes::copy_from_slice(b"second0000");
    let location = virtual_file(&chunks, "chunk-2", &bytes)?;

    let storage = mem("virtual-window");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;

    let coord = ChunkIndices(vec![0, 0, 1]);
    repo.set_chunk_ref(
        path.clone(),
        coord.clone(),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location,
            offset: 1,
            length: 5,
        })),
    )
    .await?;

    let reader = repo.get_chunk_reader(&path, &coord, &ByteRange::ALL).await?;
    assert_eq!(get_chunk(reader).await?, Some(bytes.slice(1..6)));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_virtual_refs__byte_ranges_compose_with_the_window() -> TestResult {
    let chunks = tempfile::TempDir::new()?;
    let bytes = Bytes::copy_from_slice(b"first");
    let location = virtual_file(&chunks, "chunk-3", &bytes)?;

    let storage = mem("virtual-ranges");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;

    let coord = ChunkIndices(vec![0, 0, 0]);
    repo.set_chunk_ref(
        path.clone(),
        coord.clone(),
        Some(ChunkPayload::Virtual(VirtualChunkRef {
            location,
            offset: 0,
            length: 5,
        })),
    )
    .await?;

    for range in [ByteRange::bounded(0, 3), ByteRange::from_offset(2), ByteRange::to_offset(4)]
    {
        let reader = repo.get_chunk_reader(&path, &coord, &range).await?;
        assert_eq!(
            get_chunk(reader).await?,
            Some(range.slice(bytes.clone())),
            "byte range {range:?} must window the virtual reference"
        );
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_virtual_refs__malformed_locations_are_rejected() -> TestResult {
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("abcdef"),
        Err(VirtualReferenceError::CannotParseUrl(_))
    ));
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("s3:///foo/path"),
        Err(VirtualReferenceError::CannotParseBucketName(_))
    ));
    // An `s3://` location with a bucket is constructible; it is never fetched
    // at this milestone (PROFILE.md P10).
    assert!(matches!(
        VirtualChunkLocation::from_absolute_path("s3://bucket/key")?,
        VirtualChunkLocation::Absolute(_)
    ));

    // A well-formed URL whose scheme this milestone does not resolve fails when
    // the chunk is read, not when the reference is built.
    let location = VirtualChunkLocation::from_absolute_path("ftp://example/chunk")?;
    let storage = mem("virtual-scheme");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let path: Path = "/array".try_into()?;
    repo.add_array(path.clone(), array_meta()).await?;
    let coord = ChunkIndices(vec![0, 0, 0]);
    repo.set_chunk_ref(
        path.clone(),
        coord.clone(),
        Some(ChunkPayload::Virtual(VirtualChunkRef { location, offset: 0, length: 4 })),
    )
    .await?;

    let reader = repo.get_chunk_reader(&path, &coord, &ByteRange::ALL).await?;
    assert!(matches!(
        get_chunk(reader).await,
        Err(RepositoryError::VirtualReferenceError(_))
    ));
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn g14_virtual_refs__the_store_sets_them_on_chunk_keys_only() -> TestResult {
    let chunks = tempfile::TempDir::new()?;
    let bytes = Bytes::copy_from_slice(b"virtual!!");
    let location = virtual_file(&chunks, "chunk-4", &bytes)?;

    let mut store = store_with_array("virtual-store").await?;
    let reference =
        VirtualChunkRef { location, offset: 0, length: bytes.len() as u64 };

    store.set_virtual_ref("array/c/0/0/0", reference.clone()).await?;
    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, bytes);

    assert!(matches!(
        store.set_virtual_ref("array/zarr.json", reference.clone()).await,
        Err(StoreError::NotAllowed(_))
    ));
    assert!(matches!(
        store.set_virtual_ref("array/foo", reference).await,
        Err(StoreError::InvalidKey { .. })
    ));
    Ok(())
}

// ---------------------------------------------------------------------------
// g15_change_set — TEST_INTERFACE.md 4.4 and 4.6; distributed writes
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g15_change_set__round_trips_through_its_serialised_form() -> TestResult {
    assert!(ChangeSet::default().is_empty());

    let storage = mem("changeset-rt");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_group("/a".try_into()?).await?;
    repo.add_array("/array".try_into()?, array_meta()).await?;

    let change_set: ChangeSet = repo.into();
    assert!(!change_set.is_empty());
    let bytes = change_set.export_to_bytes()?;
    assert_eq!(ChangeSet::import_from_bytes(bytes.as_slice())?, change_set);
    Ok(())
}

#[tokio::test]
async fn g15_change_set__merge_folds_two_sessions_into_one() -> TestResult {
    let storage = mem("changeset-merge");
    let path: Path = "/array".try_into()?;
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    repo.add_array(path.clone(), array_meta()).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "create array", None).await?;

    let first = Bytes::from_static(b"from the first session");
    let second = Bytes::from_static(b"from the second session");
    let mut one = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    one.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0, 0, 0]),
        Some(ChunkPayload::Inline(first.clone())),
    )
    .await?;
    let mut two = Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    two.set_chunk_ref(
        path.clone(),
        ChunkIndices(vec![0, 0, 1]),
        Some(ChunkPayload::Inline(second.clone())),
    )
    .await?;

    let one: ChangeSet = one.into();
    let two: ChangeSet = two.into();

    // Merging into an empty change set yields the one that was merged in.
    let mut merged = ChangeSet::default();
    merged.merge(one.clone());
    assert_eq!(merged, one);

    // `merge_many` folds a sequence in turn, and both sessions' work survives.
    let mut merged = ChangeSet::default();
    merged.merge_many(vec![one, two]);
    assert!(!merged.is_empty());

    let resumed = Repository::from_branch_tip(Arc::clone(&storage), "main")
        .await?
        .with_change_set(merged)
        .build();
    assert_eq!(
        resumed.get_chunk_ref(&path, &ChunkIndices(vec![0, 0, 0])).await?,
        Some(ChunkPayload::Inline(first))
    );
    assert_eq!(
        resumed.get_chunk_ref(&path, &ChunkIndices(vec![0, 0, 1])).await?,
        Some(ChunkPayload::Inline(second))
    );
    Ok(())
}

#[tokio::test]
async fn g15_change_set__a_garbage_payload_is_rejected() -> TestResult {
    assert!(ChangeSet::import_from_bytes(b"not a change set at all").is_err());
    assert!(ChangeSet::import_from_bytes(&[]).is_err());
    Ok(())
}

#[tokio::test]
async fn g15_change_set__resumes_an_exported_session() -> TestResult {
    let storage = mem("changeset-resume");
    let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
    let snapshot = repo.snapshot_id().clone();
    repo.add_group("/a".try_into()?).await?;
    let bytes = repo.change_set_bytes()?;

    let mut resumed = Repository::update(Arc::clone(&storage), snapshot)
        .with_change_set(ChangeSet::import_from_bytes(bytes.as_slice())?)
        .build();
    assert!(resumed.has_uncommitted_changes());
    assert!(resumed.get_group(&"/a".try_into()?).await.is_ok());

    let committed = resumed.commit(Ref::DEFAULT_BRANCH, "resumed", None).await?;
    let reopened = Repository::update(Arc::clone(&storage), committed).build();
    assert!(reopened.get_group(&"/a".try_into()?).await.is_ok());
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn g15_change_set__a_distributed_commit_merges_a_foreign_session() -> TestResult {
    let storage = mem("changeset-distributed");
    let path: Path = "/array".try_into()?;

    let mut writer = Repository::init(Arc::clone(&storage), false)
        .await?
        .with_inline_threshold_bytes(1)
        .build();
    writer.add_array(path.clone(), array_meta()).await?;
    writer.commit(Ref::DEFAULT_BRANCH, "create array", None).await?;

    // A second session writes a chunk and exports only its change set.
    let mut remote = Repository::from_branch_tip(Arc::clone(&storage), "main")
        .await?
        .with_inline_threshold_bytes(1)
        .build();
    let data = Bytes::from_static(b"0123456789abcdef");
    let payload = remote.get_chunk_writer()(data.clone()).await?;
    remote.set_chunk_ref(path.clone(), ChunkIndices(vec![0, 0]), Some(payload)).await?;
    let exported: ChangeSet = remote.into();
    let exported = ChangeSet::import_from_bytes(exported.export_to_bytes()?.as_slice())?;

    let mut coordinator =
        Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
    coordinator
        .distributed_commit(Ref::DEFAULT_BRANCH, vec![exported], "distributed", None)
        .await?;

    let reader = coordinator
        .get_chunk_reader(&path, &ChunkIndices(vec![0, 0]), &ByteRange::ALL)
        .await?;
    assert_eq!(get_chunk(reader).await?, Some(data));
    Ok(())
}

// ---------------------------------------------------------------------------
// g16_access_mode — TEST_INTERFACE.md 4.9 (access modes and capabilities)
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g16_access_mode__is_reported_and_can_be_narrowed() -> TestResult {
    let store = mem_store("access-report").await?;
    assert_eq!(store.access_mode(), &AccessMode::ReadWrite);
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;

    let read_only = store.with_access_mode(AccessMode::ReadOnly);
    assert_eq!(read_only.access_mode(), &AccessMode::ReadOnly);
    // The narrowed handle sees the same repository.
    assert_eq!(
        read_only.get("zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(GROUP_META_BACK)
    );
    Ok(())
}

#[tokio::test]
async fn g16_access_mode__read_only_refuses_every_write() -> TestResult {
    let store = store_with_array("access-readonly").await?;
    store.set("array/c/0/0/0", Bytes::copy_from_slice(b"hello")).await?;
    let mut read_only = store.with_access_mode(AccessMode::ReadOnly);

    assert!(matches!(
        read_only.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await,
        Err(StoreError::ReadOnly)
    ));
    assert!(matches!(read_only.delete("array/c/0/0/0").await, Err(StoreError::ReadOnly)));
    assert!(matches!(
        read_only
            .set_partial_values(Vec::<(&str, u64, Bytes)>::new())
            .await,
        Err(StoreError::ReadOnly)
    ));
    // A virtual reference is a write too, and the location is never resolved.
    let reference = VirtualChunkRef {
        location: VirtualChunkLocation::from_absolute_path(
            "file:///tmp/icechunk-never-read",
        )?,
        offset: 0,
        length: 1,
    };
    assert!(matches!(
        read_only.set_virtual_ref("array/c/0/0/0", reference).await,
        Err(StoreError::ReadOnly)
    ));

    // Reads still work, and nothing was written.
    assert_eq!(
        read_only.get("array/c/0/0/0", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(b"hello")
    );
    Ok(())
}

#[tokio::test]
async fn g16_access_mode__capabilities_are_reported() -> TestResult {
    let store = mem_store("access-caps").await?;
    assert!(store.supports_writes()?);
    assert!(store.supports_deletes()?);
    assert!(store.supports_listing()?);
    assert!(!store.supports_partial_writes()?);
    Ok(())
}

#[tokio::test]
async fn g16_access_mode__partial_writes_are_unimplemented() -> TestResult {
    let store = store_with_array("access-partial").await?;
    let result = store
        .set_partial_values(vec![("array/c/0/0/0", 0u64, Bytes::from_static(b"hi"))])
        .await;
    assert!(
        matches!(result, Err(StoreError::Unimplemented(_))),
        "partial writes are not implemented at this milestone"
    );
    Ok(())
}

// ---------------------------------------------------------------------------
// g17_storage_config — PROFILE.md P9; TEST_INTERFACE.md 4.7 and 4.9
// ---------------------------------------------------------------------------

#[tokio::test]
async fn g17_storage_config__memory_and_local_backends_behave_alike() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    // The local backend creates its directory if it does not exist.
    let root = dir.path().join("made/by/the/backend");
    assert!(!root.exists());
    let backends: Vec<Arc<dyn Storage + Send + Sync>> = vec![
        mem("backend-memory"),
        Arc::new(ObjectStorage::new_local_store(root.as_path())?),
    ];
    assert!(root.is_dir(), "new_local_store must create its directory");

    for storage in backends {
        let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
        repo.add_group(Path::root()).await?;
        repo.add_group("/group1".try_into()?).await?;
        let snapshot = repo.commit(Ref::DEFAULT_BRANCH, "init tree", None).await?;

        let reopened =
            Repository::from_branch_tip(Arc::clone(&storage), "main").await?.build();
        assert_eq!(reopened.snapshot_id(), &snapshot);
        assert!(reopened.get_group(&"/group1".try_into()?).await.is_ok());
        assert_eq!(
            ref_labels(&list_refs(storage.as_ref()).await?),
            vec!["branch:main"]
        );
        assert!(Repository::init(Arc::clone(&storage), false).await.is_err());
    }
    Ok(())
}

#[tokio::test]
async fn g17_storage_config__builds_a_working_backend() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let configs = vec![
        StorageConfig::InMemory { prefix: Some("cfg".to_string()) },
        StorageConfig::InMemory { prefix: None },
        StorageConfig::LocalFileSystem { root: dir.path().to_path_buf() },
    ];

    for config in configs {
        let storage = config.make_storage().await?;
        let mut repo = Repository::init(Arc::clone(&storage), false).await?.build();
        repo.add_group(Path::root()).await?;
        repo.commit(Ref::DEFAULT_BRANCH, "hello", None).await?;
        assert!(Repository::exists(storage.as_ref()).await?);
    }

    let cached = StorageConfig::InMemory { prefix: None }.make_cached_storage().await?;
    let mut repo = Repository::init(Arc::clone(&cached), false).await?.build();
    repo.add_group(Path::root()).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "cached", None).await?;
    assert!(Repository::exists(cached.as_ref()).await?);
    Ok(())
}

#[tokio::test]
async fn g17_storage_config__is_read_from_json() -> TestResult {
    let expected = ConsolidatedStore {
        storage: StorageConfig::LocalFileSystem { root: "/tmp/test".into() },
        repository: ZarrConfig {
            version: Some(VersionInfo::SnapshotId(SnapshotId::new([
                0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
            ]))),
            inline_chunk_threshold_bytes: Some(128),
            unsafe_overwrite_refs: Some(true),
            change_set_bytes: None,
            virtual_ref_config: None,
        },
        config: Some(StoreOptions { get_partial_values_concurrency: 100 }),
    };
    let json = r#"
        {"storage": {"type": "local_filesystem", "root":"/tmp/test"},
         "repository": {
            "version": {"snapshot_id":"000G40R40M30E209185G"},
            "inline_chunk_threshold_bytes":128,
            "unsafe_overwrite_refs":true
         },
         "config": {"get_partial_values_concurrency": 100}
        }"#;
    assert_eq!(serde_json::from_str::<ConsolidatedStore>(json)?, expected);

    let minimal = r#"{"storage":{"type":"in_memory"},"repository":{}}"#;
    let parsed: ConsolidatedStore = serde_json::from_str(minimal)?;
    assert_eq!(parsed.storage, StorageConfig::InMemory { prefix: None });
    assert_eq!(parsed.repository, ZarrConfig::new());
    assert_eq!(parsed.config, None);

    assert!(serde_json::from_str::<ConsolidatedStore>(
        r#"{"storage":{"type":"not_a_backend"},"repository":{}}"#
    )
    .is_err());
    Ok(())
}

#[tokio::test]
async fn g17_storage_config__opens_a_store_from_json() -> TestResult {
    let mut store = Store::from_json(
        br#"{"storage":{"type":"in_memory","prefix":"json"},"repository":{}}"#,
        AccessMode::ReadWrite,
    )
    .await?;
    assert_eq!(store.current_branch(), &Some("main".to_string()));

    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.commit("from json").await?;
    assert_eq!(
        store.get("zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(GROUP_META_BACK)
    );

    // The same document, already parsed, opens the same way.
    let consolidated = ConsolidatedStore {
        storage: StorageConfig::InMemory { prefix: Some("consolidated".to_string()) },
        repository: ZarrConfig::new(),
        config: None,
    };
    let mut parsed = Store::from_consolidated(&consolidated, AccessMode::ReadWrite).await?;
    assert_eq!(parsed.current_branch(), &Some("main".to_string()));
    parsed.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    parsed.commit("from consolidated").await?;
    assert_eq!(
        parsed.get("zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(GROUP_META_BACK)
    );

    // An in-memory caching wrapper is transparent to the repository.
    let backing = mem("json-cached");
    let cached: Arc<dyn Storage + Send + Sync> =
        Arc::new(MemCachingStorage::new(Arc::clone(&backing), 2, 2, 2, 2));
    let mut repo = Repository::init(cached, false).await?.build();
    repo.add_group(Path::root()).await?;
    repo.commit(Ref::DEFAULT_BRANCH, "cached", None).await?;
    assert!(Repository::exists(backing.as_ref()).await?);

    let wrapped = Repository::add_in_mem_asset_caching(Arc::clone(&backing));
    let reopened = Repository::from_branch_tip(wrapped, "main").await?.build();
    assert!(reopened.get_group(&Path::root()).await.is_ok());
    Ok(())
}
