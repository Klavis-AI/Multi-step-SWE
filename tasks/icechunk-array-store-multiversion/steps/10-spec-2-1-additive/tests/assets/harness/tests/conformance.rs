// Verifier-owned conformance driver for step 10 ("spec 2.1", upstream tag
// v2.1.0 of earth-mover/icechunk, Apache-2.0).
//
// Every case reaches the candidate through the public `icechunk::` surface
// documented in /app/TEST_INTERFACE.md, plus the CLI binary named by
// ICECHUNK_TEST_CLI_BIN. Nothing touches the network, a cloud endpoint or
// MinIO: storage is in-memory, a `tempfile` directory, or an S3-shaped handle
// that is never asked to perform I/O.
//
// Case functions are named `<group>__<case>`; the runner filters one group at
// a time and checks the executed count against tests/assets/groups.txt.
//
// Provenance of the assertions is recorded per group; upstream test names refer
// to files inside the v2.1.0 source tree.

#![allow(non_snake_case)]
#![allow(clippy::type_complexity)]

use std::{
    collections::{BTreeSet, HashMap, HashSet},
    num::{NonZeroU16, NonZeroUsize},
    sync::Arc,
};

use bytes::Bytes;
use chrono::{DateTime, Utc};
use futures::{StreamExt as _, TryStreamExt as _, stream};
// FILE-SCOPE PRELUDE RULE - do not undo this.
//
// The verifier attributes a compile failure to one group by removing the case
// functions of every OTHER group and compiling what is left. Everything at file
// scope - `use` items, type aliases, helper functions - survives in every one
// of those slices. So a file-scope name that exists only at THIS release makes
// every slice fail to compile against the previous milestone, including the
// inherited-regression groups, which are exactly the ones an unchanged
// workspace is supposed to still pay out on.
//
// Therefore: every symbol this release introduces is imported INSIDE the case
// body that uses it, and no file-scope helper names one. Duplicating a small
// helper into the two or three case bodies that need it is deliberate here.
use icechunk::{
    ObjectStorage, ObjectStoreConfig, Repository, Storage, Store,
    asset_manager::AssetManager,
    config::{
        AzureCredentials, Credentials, ManifestConfig, ManifestSplitCondition,
        ManifestSplitDim, ManifestSplitDimCondition, ManifestSplittingConfig,
        RepositoryConfig, S3Credentials, S3Options, S3StaticCredentials,
    },
    conflicts::detector::ConflictDetector,
    error::ICError,
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        format_constants::{
            CompressionAlgorithmBin, FileTypeBin, ICECHUNK_FORMAT_MAGIC_BYTES,
            SpecVersionBin,
        },
        manifest::{
            ChunkPayload, VCC_RELATIVE_URL_SCHEME, VirtualChunkLocation,
            VirtualChunkRef, VirtualReferenceErrorKind,
        },
        repo_info::UpdateType,
        snapshot::{ArrayShape, DimensionName, SnapshotInfo},
        IcechunkFormatErrorKind,
    },
    new_in_memory_storage, new_local_filesystem_storage, new_s3_storage,
    ops::gc::{ExpiredRefAction, GCConfig, GCSummary, expire, garbage_collect},
    refs::RefErrorKind,
    repository::{RepositoryErrorKind, VersionInfo},
    session::{Session, SessionErrorKind},
    storage::{
        HttpObjectStoreBackend, ObjectStoreBackend, Settings, new_http_storage,
        new_r2_storage, new_tigris_storage,
    },
    store::StoreErrorKind,
    virtual_chunks::{VirtualChunkContainer, VirtualChunkResolver},
};
use tokio::sync::RwLock;

type Res = Result<(), Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// Shared helpers. `commit_group`, `threshold_between_commits` and
// `clean_all_now` are adapted from upstream `icechunk/tests/test_gc.rs`.
// ---------------------------------------------------------------------------

async fn memory_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(Repository::create(None, storage, HashMap::new(), None, true).await?)
}

fn path_of(p: &str) -> Path {
    if p == "/" { Path::root() } else { Path::try_from(p).unwrap() }
}

/// Commit a single new group on `branch` and return the new snapshot id.
async fn commit_group(
    repo: &Repository,
    branch: &str,
    path: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session(branch).await?;
    session.add_group(path_of(path), Bytes::new()).await?;
    Ok(session.commit(path).execute().await?)
}

/// Capture an expiration threshold cleanly separated from the surrounding
/// commits: the `flushed_at < older_than` gate is strict, so bracket the read
/// with sleeps that guarantee strict ordering on a fast machine.
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

async fn expire_at(
    am: &Arc<AssetManager>,
    threshold: DateTime<Utc>,
) -> Result<icechunk::ops::gc::ExpireResult, Box<dyn std::error::Error>> {
    Ok(expire(
        Arc::clone(am),
        threshold,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?)
}

async fn gc_all(
    am: &Arc<AssetManager>,
) -> Result<GCSummary, Box<dyn std::error::Error>> {
    Ok(garbage_collect(Arc::clone(am), &clean_all_now(), None, 100).await?)
}

async fn ancestry_of(
    repo: &Repository,
    version: &VersionInfo,
) -> Result<Vec<SnapshotInfo>, Box<dyn std::error::Error>> {
    Ok(repo.ancestry(version).await?.try_collect::<Vec<_>>().await?)
}

async fn tip_of(
    repo: &Repository,
    branch: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    Ok(repo
        .readonly_session(&VersionInfo::BranchTipRef(branch.to_string()))
        .await?
        .snapshot_id()
        .clone())
}

fn paths(items: &[&str]) -> BTreeSet<Path> {
    items.iter().map(|p| path_of(p)).collect()
}

/// Build a repository whose `main` history is
/// `initial -> /(root group) -> /a -> /b | threshold | -> /c`, then expire.
/// Returns (repo, asset manager, initial, [g0, a, b], c).
async fn expired_fixture() -> Result<
    (Repository, Arc<AssetManager>, SnapshotId, Vec<SnapshotId>, SnapshotId),
    Box<dyn std::error::Error>,
> {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let initial = tip_of(&repo, "main").await?;
    let g0 = commit_group(&repo, "main", "/").await?;
    let a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let threshold = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;
    let r = expire_at(&am, threshold).await?;
    assert_eq!(r.released_snapshots.len(), 3, "expected g0, a and b to be released");
    assert!(r.edited_snapshots.contains(&c), "expected /c to be the edited boundary");
    Ok((repo, am, initial, vec![g0, a, b], c))
}

/// The first 39 bytes of a well formed metadata file.
///
/// The two lengths are written as the literals TEST_INTERFACE.md section 3 and
/// PROFILE.md R2 tabulate (24-byte implementation name, 39-byte header) rather
/// than through `ICECHUNK_IMPL_NAME_LEN` / `ICECHUNK_FILE_HEADER_LEN`, because
/// this is a file-scope helper - see the prelude rule above. The constants
/// themselves are asserted against the same table in
/// `hdr_parse__layout_constants_describe_a_39_byte_header`.
fn synthetic_header(
    impl_name: &str,
    spec: u8,
    file_type: u8,
    compression: u8,
) -> Vec<u8> {
    let mut buf = Vec::with_capacity(39);
    buf.extend_from_slice(ICECHUNK_FORMAT_MAGIC_BYTES);
    let mut name = impl_name.as_bytes().to_vec();
    name.resize(24, b' ');
    buf.extend_from_slice(&name[..24]);
    buf.push(spec);
    buf.push(file_type);
    buf.push(compression);
    buf
}

fn s3_static_credentials() -> S3Credentials {
    S3Credentials::Static(S3StaticCredentials {
        access_key_id: "key".to_string(),
        secret_access_key: "secret".to_string(),
        session_token: None,
        expires_after: None,
    })
}

fn file_url_prefix(dir: &std::path::Path) -> String {
    format!("file://{}/", dir.to_str().unwrap())
}

async fn add_small_array(
    session: &mut Session,
    path: &str,
    chunks: u32,
) -> Result<(), Box<dyn std::error::Error>> {
    let shape = ArrayShape::new(vec![(chunks as u64, chunks)]).unwrap();
    session
        .add_array(path_of(path), shape, Some(vec![DimensionName::from(Some("t"))]), Bytes::new())
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

// ===========================================================================
// GROUP hdr_parse (new) - `format_constants::parse_file_header` and the five
// header-layout constants.
// Provenance: icechunk-format/src/lib.rs::format_constants and its unit tests
// `test_parse_file_header_*`; spec-v2-1.md "### Binary File Format".
// ===========================================================================

#[tokio::test]
async fn hdr_parse__layout_constants_describe_a_39_byte_header() {
    use icechunk::format::format_constants::{
        ICECHUNK_COMPRESSION_OFFSET, ICECHUNK_FILE_HEADER_LEN,
        ICECHUNK_FILE_TYPE_OFFSET, ICECHUNK_IMPL_NAME_LEN,
        ICECHUNK_SPEC_VERSION_OFFSET,
    };
    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES.len(), 12);
    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES, "ICE\u{1f9ca}CHUNK".as_bytes());
    assert_eq!(ICECHUNK_IMPL_NAME_LEN, 24);
    assert_eq!(ICECHUNK_SPEC_VERSION_OFFSET, 36);
    assert_eq!(ICECHUNK_FILE_TYPE_OFFSET, 37);
    assert_eq!(ICECHUNK_COMPRESSION_OFFSET, 38);
    assert_eq!(ICECHUNK_FILE_HEADER_LEN, 39);
    // The file-type codes the specification tabulates.
    assert_eq!(FileTypeBin::Snapshot as u8, 1);
    assert_eq!(FileTypeBin::Manifest as u8, 2);
    assert_eq!(FileTypeBin::TransactionLog as u8, 4);
    assert_eq!(FileTypeBin::RepoInfo as u8, 6);
    assert_eq!(CompressionAlgorithmBin::None as u8, 0);
    assert_eq!(CompressionAlgorithmBin::Zstd as u8, 1);
}

#[tokio::test]
async fn hdr_parse__reads_a_header_and_ignores_the_body() -> Res {
    use icechunk::format::format_constants::{ICECHUNK_FILE_HEADER_LEN, parse_file_header};
    let mut buf = synthetic_header(
        "ic-2.1.0",
        SpecVersionBin::V2 as u8,
        FileTypeBin::Snapshot as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    buf.extend_from_slice(b"a compressed body would go here, and must be ignored");

    let h = parse_file_header(&buf)?;
    assert_eq!(h.impl_name, "ic-2.1.0");
    assert_eq!(h.app_name, "ic");
    assert_eq!(h.app_version.as_deref(), Some("2.1.0"));
    assert_eq!(h.spec_version, SpecVersionBin::V2);
    assert_eq!(h.file_type, FileTypeBin::Snapshot);
    assert_eq!(h.compression, CompressionAlgorithmBin::Zstd);

    // Exactly the header, with no body at all, parses identically.
    let exact = parse_file_header(&buf[..ICECHUNK_FILE_HEADER_LEN])?;
    assert_eq!(exact, h);
    Ok(())
}

#[tokio::test]
async fn hdr_parse__discovers_every_metadata_file_type() -> Res {
    use icechunk::format::format_constants::parse_file_header;
    for (code, want) in [
        (FileTypeBin::Snapshot as u8, FileTypeBin::Snapshot),
        (FileTypeBin::Manifest as u8, FileTypeBin::Manifest),
        (FileTypeBin::TransactionLog as u8, FileTypeBin::TransactionLog),
        (FileTypeBin::RepoInfo as u8, FileTypeBin::RepoInfo),
    ] {
        let buf = synthetic_header(
            "ic-2.1.0",
            SpecVersionBin::V1 as u8,
            code,
            CompressionAlgorithmBin::None as u8,
        );
        let h = parse_file_header(&buf)?;
        assert_eq!(h.file_type, want);
        assert_eq!(h.spec_version, SpecVersionBin::V1);
        assert_eq!(h.compression, CompressionAlgorithmBin::None);
    }
    Ok(())
}

#[tokio::test]
async fn hdr_parse__impl_name_is_trimmed_and_split_best_effort() -> Res {
    use icechunk::format::format_constants::{ICECHUNK_SPEC_VERSION_OFFSET, parse_file_header};
    // Trailing spaces and NULs are both padding.
    let mut buf = synthetic_header(
        "",
        SpecVersionBin::V2 as u8,
        FileTypeBin::RepoInfo as u8,
        CompressionAlgorithmBin::None as u8,
    );
    let name = b"other-9.9.9";
    buf[ICECHUNK_FORMAT_MAGIC_BYTES.len()
        ..ICECHUNK_FORMAT_MAGIC_BYTES.len() + name.len()]
        .copy_from_slice(name);
    for i in ICECHUNK_FORMAT_MAGIC_BYTES.len() + name.len()..ICECHUNK_SPEC_VERSION_OFFSET
    {
        buf[i] = 0;
    }
    let h = parse_file_header(&buf)?;
    assert_eq!(h.impl_name, "other-9.9.9");
    assert_eq!(h.app_name, "other");
    assert_eq!(h.app_version.as_deref(), Some("9.9.9"));

    // No '-' at all: the whole string is the app name and there is no version.
    let buf = synthetic_header(
        "solo",
        SpecVersionBin::V2 as u8,
        FileTypeBin::Manifest as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    let h = parse_file_header(&buf)?;
    assert_eq!(h.impl_name, "solo");
    assert_eq!(h.app_name, "solo");
    assert_eq!(h.app_version, None);
    Ok(())
}

#[tokio::test]
async fn hdr_parse__rejects_malformed_headers_by_category() {
    use icechunk::format::format_constants::{ICECHUNK_FILE_HEADER_LEN, parse_file_header};
    // Too short.
    let short = vec![0u8; ICECHUNK_FILE_HEADER_LEN - 1];
    let err = parse_file_header(&short).unwrap_err();
    assert!(
        matches!(
            err.kind,
            IcechunkFormatErrorKind::InvalidIcechunkHeaderSize { found, expected }
                if found == ICECHUNK_FILE_HEADER_LEN - 1
                    && expected == ICECHUNK_FILE_HEADER_LEN
        ),
        "short buffer must report InvalidIcechunkHeaderSize"
    );

    // Wrong magic.
    let mut bad_magic = synthetic_header(
        "ic-2.1.0",
        SpecVersionBin::V2 as u8,
        FileTypeBin::Snapshot as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    bad_magic[0] ^= 0xff;
    assert!(matches!(
        parse_file_header(&bad_magic).unwrap_err().kind,
        IcechunkFormatErrorKind::InvalidMagicNumbers
    ));

    // Unknown file type code.
    let unknown_type = synthetic_header(
        "ic-2.1.0",
        SpecVersionBin::V2 as u8,
        99,
        CompressionAlgorithmBin::Zstd as u8,
    );
    assert!(matches!(
        parse_file_header(&unknown_type).unwrap_err().kind,
        IcechunkFormatErrorKind::UnknownFileType { got: 99 }
    ));

    // Unknown spec version byte.
    let unknown_spec = synthetic_header(
        "ic-2.1.0",
        99,
        FileTypeBin::Snapshot as u8,
        CompressionAlgorithmBin::Zstd as u8,
    );
    assert!(matches!(
        parse_file_header(&unknown_spec).unwrap_err().kind,
        IcechunkFormatErrorKind::InvalidSpecVersion { found: 99, .. }
    ));

    // Unknown compression code.
    let unknown_comp = synthetic_header(
        "ic-2.1.0",
        SpecVersionBin::V2 as u8,
        FileTypeBin::Snapshot as u8,
        99,
    );
    assert!(matches!(
        parse_file_header(&unknown_comp).unwrap_err().kind,
        IcechunkFormatErrorKind::InvalidCompressionAlgorithm
    ));
}

// ===========================================================================
// GROUP hdr_fetch (new) - `AssetManager::fetch_*_header` /
// `fetch_*_with_header`, reachable through `Repository::asset_manager()`.
// Provenance: icechunk/src/asset_manager.rs doc comments on `fetch_header`
// and the `*_with_header` family.
// ===========================================================================

#[tokio::test]
async fn hdr_fetch__repo_info_and_snapshot_headers() -> Res {
    let repo = memory_repo().await?;
    let snap = commit_group(&repo, "main", "/").await?;
    let am = Arc::clone(repo.asset_manager());

    let h = am.fetch_repo_info_header().await?;
    assert_eq!(h.file_type, FileTypeBin::RepoInfo);
    assert_eq!(h.spec_version, SpecVersionBin::V2);
    assert_eq!(h.app_name, "ic");
    assert!(h.app_version.is_some(), "the implementation name must carry a version");

    let hs = am.fetch_snapshot_header(&snap).await?;
    assert_eq!(hs.file_type, FileTypeBin::Snapshot);
    assert_eq!(hs.spec_version, SpecVersionBin::V2);
    assert_eq!(hs.impl_name, h.impl_name);
    Ok(())
}

#[tokio::test]
async fn hdr_fetch__transaction_log_and_manifest_headers() -> Res {
    let repo = memory_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    add_small_array(&mut session, "/array", 2).await?;
    let snap = session.commit("array").execute().await?;
    let am = Arc::clone(repo.asset_manager());

    let ht = am.fetch_transaction_log_header(&snap).await?;
    assert_eq!(ht.file_type, FileTypeBin::TransactionLog);
    assert_eq!(ht.spec_version, SpecVersionBin::V2);

    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert!(!manifests.is_empty(), "an array with chunks must produce a manifest");
    let hm = am.fetch_manifest_header(&manifests[0].id).await?;
    assert_eq!(hm.file_type, FileTypeBin::Manifest);
    assert_eq!(hm.spec_version, SpecVersionBin::V2);
    Ok(())
}

#[tokio::test]
async fn hdr_fetch__reports_the_type_found_not_the_type_requested() -> Res {
    let repo = memory_repo().await?;
    let snap = commit_group(&repo, "main", "/").await?;
    let am = Arc::clone(repo.asset_manager());

    // `fetch_header` is a pure inspection primitive over a storage path: it
    // discovers the file type rather than asserting it.
    let by_path = am.fetch_header(&format!("snapshots/{snap}")).await?;
    assert_eq!(by_path.file_type, FileTypeBin::Snapshot);
    let repo_info = am.fetch_header("repo").await?;
    assert_eq!(repo_info.file_type, FileTypeBin::RepoInfo);

    // A path that holds no object is an error, not a default header.
    assert!(am.fetch_header("snapshots/does-not-exist").await.is_err());
    Ok(())
}

#[tokio::test]
async fn hdr_fetch__header_read_does_not_need_the_body() -> Res {
    use icechunk::format::format_constants::{ICECHUNK_FILE_HEADER_LEN, parse_file_header};
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true)
            .await?;
    let snap = commit_group(&repo, "main", "/").await?;
    drop(repo);

    // Cut the stored snapshot down to exactly its header. A reader that only
    // range-GETs `ICECHUNK_FILE_HEADER_LEN` bytes still succeeds; a reader that
    // downloads and decodes the body cannot.
    let file = dir.path().join("snapshots").join(snap.to_string());
    let full = std::fs::metadata(&file)?.len();
    assert!(full > ICECHUNK_FILE_HEADER_LEN as u64);
    let bytes = std::fs::read(&file)?;
    // The same bytes parse directly, so what is on disk really is the header
    // `parse_file_header` documents, for a snapshot and for the repo object.
    assert_eq!(parse_file_header(&bytes)?.file_type, FileTypeBin::Snapshot);
    assert_eq!(
        parse_file_header(&std::fs::read(dir.path().join("repo"))?)?.file_type,
        FileTypeBin::RepoInfo
    );
    std::fs::write(&file, &bytes[..ICECHUNK_FILE_HEADER_LEN])?;

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    let am = Arc::clone(repo.asset_manager());
    am.clear_snapshot_cache();
    am.clear_manifest_cache();

    let h = am.fetch_snapshot_header(&snap).await?;
    assert_eq!(h.file_type, FileTypeBin::Snapshot);
    assert_eq!(h.spec_version, SpecVersionBin::V2);
    assert!(
        am.fetch_snapshot(&snap).await.is_err(),
        "decoding a body-less snapshot must fail, which is what makes the \
         successful header read meaningful"
    );
    Ok(())
}

#[tokio::test]
async fn hdr_fetch__with_header_variants_return_object_and_header() -> Res {
    let repo = memory_repo().await?;
    let snap = commit_group(&repo, "main", "/").await?;
    let am = Arc::clone(repo.asset_manager());

    // Populate the value cache first: the `*_with_header` variants bypass it,
    // so the header still comes back rather than a cached object alone.
    am.fetch_snapshot(&snap).await?;
    let (snapshot, hs) = am.fetch_snapshot_with_header(&snap).await?;
    assert_eq!(snapshot.id(), snap);
    assert_eq!(hs, am.fetch_snapshot_header(&snap).await?);

    let (tx, ht) = am.fetch_transaction_log_with_header(&snap).await?;
    assert_eq!(ht.file_type, FileTypeBin::TransactionLog);
    assert!(tx.new_groups().count() > 0, "the commit added the root group");

    let (info, _version, hr) = am.fetch_repo_info_with_header().await?;
    assert_eq!(hr, am.fetch_repo_info_header().await?);
    assert_eq!(info.spec_version()?, SpecVersionBin::V2);
    Ok(())
}

#[tokio::test]
async fn hdr_fetch__repo_info_backup_and_manifest_wrappers() -> Res {
    // `num_updates_per_repo_info_file = 1` rotates the previous repo object
    // into `overwritten/<file name>` on every update, which is how the two
    // backup wrappers are reachable with no endpoint.
    let config = RepositoryConfig {
        num_updates_per_repo_info_file: Some(1),
        ..RepositoryConfig::default()
    };
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo =
        Repository::create(Some(config), storage, HashMap::new(), None, true).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    add_small_array(&mut session, "/array", 2).await?;
    let snap = session.commit("array").execute().await?;
    commit_group(&repo, "main", "/g2").await?;
    commit_group(&repo, "main", "/g3").await?;
    let am = Arc::clone(repo.asset_manager());

    let (info, _version, _h) = am.fetch_repo_info_with_header().await?;
    let backup = info
        .repo_before_updates()?
        .expect("rotating on every update leaves a repo-info backup");
    let hb = am.fetch_repo_info_backup_header(backup).await?;
    assert_eq!(hb.file_type, FileTypeBin::RepoInfo);
    assert_eq!(hb.spec_version, SpecVersionBin::V2);
    let (backup_info, _version, hb2) =
        am.fetch_repo_info_backup_with_header(backup).await?;
    assert_eq!(hb2, hb, "the two backup wrappers report the same header");
    assert_eq!(backup_info.spec_version()?, SpecVersionBin::V2);

    // The one `*_with_header` variant no other case reaches.
    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert!(!manifests.is_empty(), "an array with chunks must produce a manifest");
    am.clear_manifest_cache();
    let (_manifest, hm) = am
        .fetch_manifest_with_header(&manifests[0].id, manifests[0].size_bytes)
        .await?;
    assert_eq!(hm.file_type, FileTypeBin::Manifest);
    assert_eq!(hm, am.fetch_manifest_header(&manifests[0].id).await?);
    Ok(())
}

// ===========================================================================
// GROUP spec21_field (new) - the one additive FlatBuffers field,
// `SnapshotInfo::pruned_ancestor_tx_logs`.
// Provenance: spec-v2-1.md "## Changes from spec version 2 to 2.1" and
// "##### `SnapshotInfo`"; upstream test_gc.rs::test_amend_preserves_pruned_logs
// and ::test_repeated_expiration_accumulates_pruned_logs.
// ===========================================================================

#[tokio::test]
async fn spec21_field__is_empty_when_a_repository_was_never_expired() -> Res {
    let repo = memory_repo().await?;
    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    let history = ancestry_of(&repo, &VersionInfo::BranchTipRef("main".into())).await?;
    assert!(history.len() >= 3);
    for info in &history {
        assert!(
            info.pruned_ancestor_tx_logs.is_empty(),
            "an unexpired snapshot must read as 'never expired'"
        );
    }
    Ok(())
}

#[tokio::test]
async fn spec21_field__records_pruned_ancestors_oldest_first() -> Res {
    let (repo, _am, _initial, pruned, c) = expired_fixture().await?;
    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(c.clone())).await?;
    assert_eq!(history[0].id, c);
    assert_eq!(
        history[0].pruned_ancestor_tx_logs, pruned,
        "the list is ordered by ancestry, oldest first"
    );
    // The surviving root is untouched.
    assert!(history.last().unwrap().pruned_ancestor_tx_logs.is_empty());
    Ok(())
}

#[tokio::test]
async fn spec21_field__accumulates_across_repeated_expirations() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let g0 = commit_group(&repo, "main", "/").await?;
    let a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let t1 = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;
    let r1 = expire_at(&am, t1).await?;
    assert_eq!(r1.released_snapshots.len(), 3);
    assert!(r1.edited_snapshots.contains(&c));

    let t2 = threshold_between_commits().await;
    let d = commit_group(&repo, "main", "/d").await?;
    commit_group(&repo, "main", "/e").await?;
    let r2 = expire_at(&am, t2).await?;
    assert_eq!(r2.released_snapshots.len(), 1);
    assert!(r2.edited_snapshots.contains(&d));

    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(d.clone())).await?;
    assert_eq!(history[0].id, d);
    assert_eq!(
        history[0].pruned_ancestor_tx_logs,
        vec![g0, a, b, c],
        "each collapsed ancestor's own list is spliced in before its id"
    );
    Ok(())
}

#[tokio::test]
async fn spec21_field__repo_info_lookup_agrees_with_ancestry() -> Res {
    let (repo, am, _initial, pruned, c) = expired_fixture().await?;
    let (repo_info, _version) = am.fetch_repo_info().await?;
    let found = repo_info.find_snapshot(&c)?;
    assert_eq!(found.id, c);
    assert_eq!(found.pruned_ancestor_tx_logs, pruned);

    let looked_up = repo.lookup_snapshot(&c).await?;
    assert_eq!(looked_up.pruned_ancestor_tx_logs, pruned);
    Ok(())
}

// ===========================================================================
// GROUP spec21_compat (new) - spec 2.0 and spec 2.1 objects are read by this
// release, and both flag the on-disk format as version byte 2.
// Provenance: spec-v2-1.md "## Changes from spec version 2 to 2.1" and
// "### Binary File Format"; version-policy.md "## On-Disk Format".
// ===========================================================================

#[tokio::test]
async fn spec21_compat__header_byte_is_2_with_and_without_the_new_field() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let g0 = commit_group(&repo, "main", "/").await?;
    let a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let threshold = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;

    // Before expiration nothing carries the field, and the header says `2`.
    assert_eq!(am.fetch_repo_info_header().await?.spec_version, SpecVersionBin::V2);
    assert_eq!(am.fetch_snapshot_header(&c).await?.spec_version, SpecVersionBin::V2);

    expire_at(&am, threshold).await?;

    // After expiration the repo info does carry it, and the header still says
    // `2`: the byte is the spec MAJOR only and cannot distinguish 2.0 from 2.1.
    let after = ancestry_of(&repo, &VersionInfo::SnapshotId(c.clone())).await?;
    assert_eq!(after[0].pruned_ancestor_tx_logs, vec![g0, a, b]);
    assert_eq!(am.fetch_repo_info_header().await?.spec_version, SpecVersionBin::V2);
    assert_eq!(am.fetch_snapshot_header(&c).await?.spec_version, SpecVersionBin::V2);
    Ok(())
}

#[tokio::test]
async fn spec21_compat__object_with_the_field_reads_back_unchanged() -> Res {
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true)
            .await?;
    let am = Arc::clone(repo.asset_manager());
    let g0 = commit_group(&repo, "main", "/").await?;
    let a = commit_group(&repo, "main", "/a").await?;
    let threshold = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;
    expire_at(&am, threshold).await?;
    drop(am);
    drop(repo);

    // A completely fresh reader over the persisted bytes sees the same list.
    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    let history = ancestry_of(&reopened, &VersionInfo::SnapshotId(c)).await?;
    assert_eq!(history[0].pruned_ancestor_tx_logs, vec![g0, a]);
    assert_eq!(reopened.spec_version(), SpecVersionBin::V2);
    Ok(())
}

#[tokio::test]
async fn spec21_compat__object_without_the_field_reads_as_never_expired() -> Res {
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true)
            .await?;
    let initial = tip_of(&repo, "main").await?;
    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    let tip = commit_group(&repo, "main", "/b").await?;
    drop(repo);

    // Nothing on disk carries the 2.1 field. Reading is not an error and needs
    // no migration; every snapshot reads as "never expired" and history is
    // complete.
    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    for info in ancestry_of(&reopened, &VersionInfo::SnapshotId(tip)).await? {
        assert!(info.pruned_ancestor_tx_logs.is_empty());
    }
    let diff = reopened
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    assert_eq!(diff.new_groups, paths(&["/", "/a", "/b"]));
    Ok(())
}

#[tokio::test]
async fn spec21_compat__spec_version_enum_gained_no_variant() {
    // Spec 2.1 is a MINOR version: it occupies no new on-disk version byte.
    assert_eq!(SpecVersionBin::V1 as u8, 1);
    assert_eq!(SpecVersionBin::V2 as u8, 2);
    assert_eq!(SpecVersionBin::current(), SpecVersionBin::V2);
    assert_eq!(SpecVersionBin::try_from(1u8).unwrap(), SpecVersionBin::V1);
    assert_eq!(SpecVersionBin::try_from(2u8).unwrap(), SpecVersionBin::V2);
    assert!(matches!(
        SpecVersionBin::try_from(3u8),
        Err(IcechunkFormatErrorKind::InvalidSpecVersion { found: 3, max_supported: 2 })
    ));
}

#[tokio::test]
async fn spec21_compat__detected_spec_version_of_a_21_repository_is_2() -> Res {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    assert!(!Repository::exists(Arc::clone(&storage), None).await?);
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true)
            .await?;
    let am = Arc::clone(repo.asset_manager());
    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/b").await?;
    expire_at(&am, threshold).await?;

    assert!(Repository::exists(Arc::clone(&storage), None).await?);
    let detected = Repository::fetch_spec_version(Arc::clone(&storage), None).await?.unwrap();
    assert_eq!(detected.spec_version(), SpecVersionBin::V2);

    // A repository that reports spec version 2 still carries the 2.1 field:
    // the byte says nothing about the minor version either way.
    let reopened = Repository::open(None, storage, HashMap::new()).await?;
    let tip = tip_of(&reopened, "main").await?;
    assert!(!reopened.lookup_snapshot(&tip).await?.pruned_ancestor_tx_logs.is_empty());
    Ok(())
}

// ===========================================================================
// GROUP expire_ancestry (new) - expiration records the pruned ancestry and GC
// retains exactly the transaction logs it references.
// Provenance: upstream test_gc.rs::{test_diff_complete_after_expire_and_gc,
// test_gc_deletes_only_unreferenced_expired_tx_logs,
// test_reparent_accumulates_existing_pruned_logs};
// design/016-expired-transaction-logs.md "### GC retention rule".
// ===========================================================================

#[tokio::test]
async fn expire_ancestry__reports_released_and_edited_snapshots() -> Res {
    let (_repo, _am, _initial, pruned, c) = expired_fixture().await?;
    assert_eq!(pruned.len(), 3);
    assert!(!pruned.contains(&c));
    Ok(())
}

#[tokio::test]
async fn expire_ancestry__gc_keeps_referenced_transaction_logs() -> Res {
    let (_repo, am, _initial, pruned, _c) = expired_fixture().await?;
    let summary = gc_all(&am).await?;
    assert_eq!(summary.snapshots_deleted, 3, "the expired snapshot files go away");
    assert_eq!(
        summary.transaction_logs_deleted, 0,
        "a log referenced by pruned_ancestor_tx_logs is retained"
    );
    for id in &pruned {
        am.fetch_transaction_log(id).await?;
    }
    Ok(())
}

#[tokio::test]
async fn expire_ancestry__gc_deletes_unreferenced_expired_logs() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let g0 = commit_group(&repo, "main", "/").await?;
    let a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;

    // A doomed side branch whose commits have no surviving descendant.
    repo.create_branch("doomed", &a).await?;
    let d = commit_group(&repo, "doomed", "/d").await?;
    let threshold = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;

    let r = expire(
        Arc::clone(&am),
        threshold,
        ExpiredRefAction::Delete,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert!(r.edited_snapshots.contains(&c));
    gc_all(&am).await?;

    // Referenced by /c's pruned chain: retained.
    for id in [&g0, &a, &b] {
        am.fetch_transaction_log(id).await?;
    }
    // Unreferenced: deleted.
    am.remove_cached_tx_log(&d);
    assert!(am.fetch_transaction_log(&d).await.is_err());
    Ok(())
}

#[tokio::test]
async fn expire_ancestry__reparenting_accumulates_an_existing_chain() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let gc_threshold = threshold_between_commits().await;
    let c = commit_group(&repo, "main", "/c").await?;

    // Reset to /a and GC between /b and /c: /b is dropped and /c re-parented
    // onto /a, seeding its chain with [b].
    repo.reset_branch("main", &a, None).await?;
    let gc_config = GCConfig::clean_all(
        gc_threshold,
        gc_threshold,
        None,
        NonZeroU16::new(50).unwrap(),
        NonZeroUsize::new(512 * 1024 * 1024).unwrap(),
        NonZeroU16::new(500).unwrap(),
        false,
    );
    garbage_collect(Arc::clone(&am), &gc_config, None, 100).await?;
    let (info, _) = am.fetch_repo_info().await?;
    let edited = info.find_snapshot(&c)?;
    assert_eq!(edited.parent_id.as_ref(), Some(&a));
    assert_eq!(edited.pruned_ancestor_tx_logs, vec![b.clone()]);

    // Now expire /a: it must be prepended, not overwrite [b].
    repo.reset_branch("main", &c, None).await?;
    let expire_threshold = threshold_between_commits().await;
    expire_at(&am, expire_threshold).await?;
    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(c.clone())).await?;
    assert_eq!(history[0].id, c);
    assert_eq!(history[0].pruned_ancestor_tx_logs, vec![a, b]);
    Ok(())
}

#[tokio::test]
async fn expire_ancestry__cutoff_older_than_everything_expires_nothing() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/").await?;
    let tip = commit_group(&repo, "main", "/a").await?;

    let r = expire_at(&am, threshold).await?;
    assert!(r.released_snapshots.is_empty());
    assert!(r.edited_snapshots.is_empty());
    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(tip)).await?;
    for info in history {
        assert!(info.pruned_ancestor_tx_logs.is_empty());
    }
    Ok(())
}

#[tokio::test]
async fn expire_ancestry__the_cutoff_is_exclusive_at_its_own_timestamp() -> Res {
    // E8. `understanding-expiration.md` works this example through: passing a
    // snapshot's own timestamp as `older_than` KEEPS that snapshot, because the
    // bound is strict, while its parent - written strictly before - is
    // released. An implementation comparing `<=` fails here and nowhere else.
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let g0 = commit_group(&repo, "main", "/").await?;
    tokio::time::sleep(std::time::Duration::from_millis(2)).await;
    let a = commit_group(&repo, "main", "/a").await?;
    tokio::time::sleep(std::time::Duration::from_millis(2)).await;
    let b = commit_group(&repo, "main", "/b").await?;

    // `/a`'s own timestamp, read back through the public ancestry.
    let history = ancestry_of(&repo, &VersionInfo::BranchTipRef("main".into())).await?;
    let cutoff = history
        .iter()
        .find(|si| si.id == a)
        .expect("/a is in the ancestry")
        .flushed_at;

    let r = expire_at(&am, cutoff).await?;
    assert!(
        !r.released_snapshots.contains(&a),
        "a snapshot written AT the cutoff is kept: the bound is exclusive"
    );
    assert!(
        r.released_snapshots.contains(&g0),
        "its parent, written strictly before the cutoff, is released"
    );
    assert!(!r.released_snapshots.contains(&b), "the branch tip is never released");
    Ok(())
}

// ===========================================================================
// GROUP expire_diff (new) - `Repository::diff` across an expiration boundary.
// Provenance: upstream test_gc.rs::{test_diff_complete_after_expire_and_gc,
// test_diff_skips_missing_pruned_log}.
// ===========================================================================

#[tokio::test]
async fn expire_diff__is_complete_after_expire_and_gc() -> Res {
    let (repo, am, initial, _pruned, _c) = expired_fixture().await?;
    let summary = gc_all(&am).await?;
    assert_eq!(summary.snapshots_deleted, 3);

    let diff = repo
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    // Without the pruned-ancestor chain this would report only "/c".
    assert_eq!(diff.new_groups, paths(&["/", "/a", "/b", "/c"]));
    Ok(())
}

#[tokio::test]
async fn expire_diff__is_complete_after_two_expirations() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let initial = tip_of(&repo, "main").await?;
    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    commit_group(&repo, "main", "/b").await?;
    let t1 = threshold_between_commits().await;
    commit_group(&repo, "main", "/c").await?;
    expire_at(&am, t1).await?;
    let t2 = threshold_between_commits().await;
    commit_group(&repo, "main", "/d").await?;
    commit_group(&repo, "main", "/e").await?;
    expire_at(&am, t2).await?;
    gc_all(&am).await?;

    let diff = repo
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    assert_eq!(diff.new_groups, paths(&["/", "/a", "/b", "/c", "/d", "/e"]));
    Ok(())
}

#[tokio::test]
async fn expire_diff__reports_arrays_and_chunks_across_the_boundary() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let initial = tip_of(&repo, "main").await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    add_small_array(&mut session, "/array", 2).await?;
    session.commit("array").execute().await?;

    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/later").await?;
    expire_at(&am, threshold).await?;
    gc_all(&am).await?;

    let diff = repo
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    assert_eq!(diff.new_groups, paths(&["/", "/later"]));
    assert_eq!(diff.new_arrays, paths(&["/array"]));
    assert!(
        diff.updated_chunks.contains_key(&path_of("/array")),
        "the chunk writes live only in the pruned ancestor's log"
    );
    Ok(())
}

#[tokio::test]
async fn expire_diff__degrades_gracefully_when_a_pruned_log_is_missing() -> Res {
    let (repo, am, initial, pruned, _c) = expired_fixture().await?;
    gc_all(&am).await?;

    // Simulate an older Icechunk GC having deleted one referenced log.
    let a = pruned[1].clone();
    am.delete_transaction_logs(stream::once(async { (a.clone(), 0u64) }).boxed())
        .await?;
    am.remove_cached_tx_log(&a);

    let diff = repo
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    // `diff` warns and skips rather than failing; /a is simply absent.
    assert_eq!(diff.new_groups, paths(&["/", "/b", "/c"]));
    Ok(())
}

// ===========================================================================
// GROUP expire_amend (new) - `CommitBuilder::amend` carries the chain forward.
// Provenance: upstream test_gc.rs::test_amend_preserves_pruned_logs.
// ===========================================================================

#[tokio::test]
async fn expire_amend__amended_snapshot_inherits_the_chain() -> Res {
    let (repo, _am, _initial, pruned, c) = expired_fixture().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(path_of("/c_extra"), Bytes::new()).await?;
    let amended = session.commit("c amended").amend().execute().await?;
    assert_ne!(amended, c);

    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(amended.clone())).await?;
    assert_eq!(history[0].id, amended);
    assert_eq!(history[0].pruned_ancestor_tx_logs, pruned);
    Ok(())
}

#[tokio::test]
async fn expire_amend__plain_commit_after_expiration_has_an_empty_chain() -> Res {
    let (repo, _am, _initial, pruned, _c) = expired_fixture().await?;
    let next = commit_group(&repo, "main", "/d").await?;
    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(next.clone())).await?;
    assert_eq!(history[0].id, next);
    assert!(
        history[0].pruned_ancestor_tx_logs.is_empty(),
        "a new commit records no pruned ancestry of its own"
    );
    // its parent, the edited boundary, still carries the chain
    assert_eq!(history[1].pruned_ancestor_tx_logs, pruned);
    Ok(())
}

#[tokio::test]
async fn expire_amend__diff_stays_complete_after_an_amend_and_gc() -> Res {
    let (repo, am, initial, _pruned, _c) = expired_fixture().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(path_of("/c_extra"), Bytes::new()).await?;
    session.commit("c amended").amend().execute().await?;
    gc_all(&am).await?;

    let diff = repo
        .diff(
            &VersionInfo::SnapshotId(initial),
            &VersionInfo::BranchTipRef("main".into()),
        )
        .await?;
    assert_eq!(diff.new_groups, paths(&["/", "/a", "/b", "/c", "/c_extra"]));
    Ok(())
}

// ===========================================================================
// GROUP expire_rebase (new) - rebase replays pruned ancestors' logs, and
// errors rather than silently skipping a missing one.
// Provenance: upstream test_gc.rs::{test_rebase_detects_conflict_in_pruned_ancestor,
// test_rebase_errors_on_missing_pruned_ancestor_log};
// design/016-expired-transaction-logs.md "## Compatibility".
// ===========================================================================

/// `main` gets a commit that adds a group at /conflict, then a later commit, so
/// that the /conflict commit becomes a pruned ancestor of the tip. A session
/// forked before either one adds an *array* at the same path.
async fn conflicting_over_expiration()
-> Result<(Repository, Arc<AssetManager>, Session, SnapshotId), Box<dyn std::error::Error>>
{
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let conflict_path = path_of("/conflict");

    let mut conflicting = repo.writable_session("main").await?;
    conflicting
        .add_array(
            conflict_path.clone(),
            ArrayShape::new(vec![(5, 5)]).unwrap(),
            None,
            Bytes::new(),
        )
        .await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(conflict_path, Bytes::new()).await?;
    let x = session.commit("add /conflict group").execute().await?;

    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/other").await?;
    let r = expire_at(&am, threshold).await?;
    assert!(r.released_snapshots.contains(&x));
    Ok((repo, am, conflicting, x))
}

#[tokio::test]
async fn expire_rebase__detects_a_conflict_that_lives_only_in_a_pruned_log() -> Res {
    let (_repo, am, mut conflicting, _x) = conflicting_over_expiration().await?;
    gc_all(&am).await?;

    let err = conflicting.rebase(&ConflictDetector).await.unwrap_err();
    match err.kind {
        SessionErrorKind::RebaseFailed { conflicts, .. } => {
            assert!(
                conflicts.iter().any(|c| matches!(
                    c,
                    icechunk::conflicts::Conflict::NewNodeConflictsWithExistingNode(p)
                        if p == &path_of("/conflict")
                )),
                "the conflict recorded only in the pruned ancestor must surface"
            );
        }
        other => panic!("expected a rebase conflict, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn expire_rebase__errors_when_a_pruned_log_is_missing() -> Res {
    let (_repo, am, mut conflicting, x) = conflicting_over_expiration().await?;
    gc_all(&am).await?;
    am.delete_transaction_logs(stream::once(async { (x.clone(), 0u64) }).boxed())
        .await?;
    am.remove_cached_tx_log(&x);

    let err = conflicting.rebase(&ConflictDetector).await.unwrap_err();
    match err.kind {
        SessionErrorKind::MissingPrunedAncestorTxLog { tx_log, .. } => {
            assert_eq!(tx_log, x, "the error names the log it could not check");
        }
        other => panic!("expected MissingPrunedAncestorTxLog, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn expire_rebase__succeeds_when_the_pruned_history_does_not_conflict() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());

    // Fork at the root snapshot, which survives expiration, and change a path
    // that no pruned ancestor touches.
    let mut pending = repo.writable_session("main").await?;
    pending.add_group(path_of("/independent"), Bytes::new()).await?;

    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/b").await?;
    expire_at(&am, threshold).await?;
    gc_all(&am).await?;

    pending.rebase(&ConflictDetector).await?;
    let committed = pending.commit("independent").execute().await?;
    let tip = tip_of(&repo, "main").await?;
    assert_eq!(committed, tip);
    Ok(())
}

#[tokio::test]
async fn expire_rebase__still_detects_conflicts_without_any_expiration() -> Res {
    let repo = memory_repo().await?;
    commit_group(&repo, "main", "/").await?;
    let mut conflicting = repo.writable_session("main").await?;
    conflicting
        .add_array(
            path_of("/conflict"),
            ArrayShape::new(vec![(5, 5)]).unwrap(),
            None,
            Bytes::new(),
        )
        .await?;
    commit_group(&repo, "main", "/conflict").await?;

    let err = conflicting.rebase(&ConflictDetector).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::RebaseFailed { .. }));
    Ok(())
}

// ===========================================================================
// GROUP expire_inspect (new) - `inspect::transaction_log_json` renders a
// flagged synthetic composite for an expiration-edited snapshot.
// Provenance: upstream test_gc.rs::test_inspect_shows_synthetic_composite.
// ===========================================================================

#[tokio::test]
async fn expire_inspect__renders_a_flagged_composite_for_an_edited_snapshot() -> Res {
    let (repo, am, _initial, pruned, c) = expired_fixture().await?;
    let tip = repo.readonly_session(&VersionInfo::SnapshotId(c.clone())).await?;
    let a_node = tip.get_node(&path_of("/a")).await?.id.to_string();
    let b_node = tip.get_node(&path_of("/b")).await?.id.to_string();
    let c_node = tip.get_node(&path_of("/c")).await?.id.to_string();

    let json = icechunk::inspect::transaction_log_json(am.as_ref(), &c, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&json)?;
    let composite = parsed
        .get("synthetic_composite")
        .expect("an edited snapshot's log is rendered as a synthetic composite");
    let merged = composite
        .get("merged_pruned_ancestor_tx_logs")
        .and_then(|v| v.as_array())
        .expect("the composite names the pruned logs it merged");
    let merged: Vec<String> =
        merged.iter().map(|v| v.as_str().unwrap_or_default().to_string()).collect();
    assert_eq!(merged, pruned.iter().map(|i| i.to_string()).collect::<Vec<_>>());

    // It merges content, not just ids: /a and /b live only in pruned logs.
    assert!(json.contains(&a_node));
    assert!(json.contains(&b_node));
    assert!(json.contains(&c_node));
    Ok(())
}

#[tokio::test]
async fn expire_inspect__renders_an_unedited_snapshot_as_a_plain_log() -> Res {
    let (repo, am, _initial, _pruned, _c) = expired_fixture().await?;
    // A commit made *after* the expiration is still in the table of contents and,
    // by section 2.3 E4, records an empty `pruned_ancestor_tx_logs`. That is the
    // snapshot section 6's "when the list is empty the JSON is the plain
    // transaction-log record" describes. Inspecting a *released* snapshot instead
    // would depend on how `transaction_log_json` treats an id the table of
    // contents no longer holds, which no document states.
    let d = commit_group(&repo, "main", "/d").await?;
    assert!(
        repo.lookup_snapshot(&d).await?.pruned_ancestor_tx_logs.is_empty(),
        "a plain commit after an expiration records an empty chain"
    );
    let json = icechunk::inspect::transaction_log_json(am.as_ref(), &d, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&json)?;
    assert!(
        parsed.get("synthetic_composite").is_none(),
        "a snapshot with an empty chain is rendered as its own log"
    );
    assert!(
        parsed.as_object().map(|o| !o.is_empty()).unwrap_or(false),
        "a plain transaction log is still rendered as a JSON object"
    );
    Ok(())
}

#[tokio::test]
async fn expire_inspect__flags_a_missing_pruned_log_and_drops_its_content() -> Res {
    let (repo, am, _initial, pruned, c) = expired_fixture().await?;
    let tip = repo.readonly_session(&VersionInfo::SnapshotId(c.clone())).await?;
    let a_node = tip.get_node(&path_of("/a")).await?.id.to_string();
    let b_node = tip.get_node(&path_of("/b")).await?.id.to_string();

    gc_all(&am).await?;
    let a = pruned[1].clone();
    am.delete_transaction_logs(stream::once(async { (a.clone(), 0u64) }).boxed())
        .await?;
    am.remove_cached_tx_log(&a);

    let json = icechunk::inspect::transaction_log_json(am.as_ref(), &c, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&json)?;
    let composite = parsed.get("synthetic_composite").expect("still a composite");
    let missing: Vec<String> = composite
        .get("missing_tx_logs")
        .and_then(|v| v.as_array())
        .expect("the composite flags the logs it could not read")
        .iter()
        .map(|v| v.as_str().unwrap_or_default().to_string())
        .collect();
    assert_eq!(missing, vec![a.to_string()]);
    assert!(!json.contains(&a_node), "the missing log's content is dropped");
    assert!(json.contains(&b_node), "surviving logs are still merged");
    Ok(())
}

#[tokio::test]
async fn expire_inspect__unexpired_repository_has_no_composite() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    commit_group(&repo, "main", "/").await?;
    let tip = commit_group(&repo, "main", "/a").await?;
    let json = icechunk::inspect::transaction_log_json(am.as_ref(), &tip, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&json)?;
    assert!(parsed.get("synthetic_composite").is_none());
    assert!(
        parsed.as_object().map(|o| !o.is_empty()).unwrap_or(false),
        "a plain transaction log is still rendered as a JSON object"
    );
    Ok(())
}

// ===========================================================================
// GROUP http_headers (new) - `config::HttpConfig`, the reshaped
// `ObjectStoreConfig::Http` payload, and static header injection.
// Provenance: icechunk/src/config.rs::HttpConfig doc comment and
// `test_http_vcc_backward_compat_yaml`; icechunk-arrow-object-store/src/lib.rs
// `tests::test_mk_object_store_*`.
// ===========================================================================

#[tokio::test]
async fn http_headers__config_carries_opts_and_headers() -> Res {
    use icechunk::config::HttpConfig;
    let cfg = HttpConfig {
        opts: HashMap::from([("allow_http".to_string(), "true".to_string())]),
        headers: HashMap::from([(
            "authorization".to_string(),
            "Bearer token123".to_string(),
        )]),
    };
    assert_eq!(cfg.opts.get("allow_http").map(String::as_str), Some("true"));
    assert_eq!(
        cfg.headers.get("authorization").map(String::as_str),
        Some("Bearer token123")
    );
    assert_eq!(HttpConfig::default(), HttpConfig { opts: HashMap::new(), headers: HashMap::new() });

    let store = ObjectStoreConfig::Http(cfg.clone());
    match &store {
        ObjectStoreConfig::Http(got) => assert_eq!(got, &cfg),
        other => panic!("expected ObjectStoreConfig::Http, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn http_headers__round_trips_through_serde() -> Res {
    use icechunk::config::HttpConfig;
    let cfg = HttpConfig {
        opts: HashMap::from([("allow_http".to_string(), "true".to_string())]),
        headers: HashMap::from([("x-api-key".to_string(), "secret".to_string())]),
    };
    let store = ObjectStoreConfig::Http(cfg.clone());
    let text = serde_json::to_string(&store)?;
    let back: ObjectStoreConfig = serde_json::from_str(&text)?;
    assert_eq!(back, store);
    Ok(())
}

#[tokio::test]
async fn http_headers__legacy_payload_without_headers_still_deserialises() -> Res {
    // Icechunk 2.0.x serialized `ObjectStoreConfig::Http` as a flat map of
    // transport options. That document must keep loading, with no headers.
    let legacy = r#"{"http":{"allow_http":"true"}}"#;
    let back: ObjectStoreConfig = serde_json::from_str(legacy)?;
    match back {
        ObjectStoreConfig::Http(cfg) => {
            assert_eq!(cfg.opts.get("allow_http").map(String::as_str), Some("true"));
            assert!(cfg.headers.is_empty());
        }
        other => panic!("expected ObjectStoreConfig::Http, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn http_headers__legacy_yaml_payload_without_headers_still_deserialises() -> Res
{
    // The configuration document the CLI writes is YAML, so this is the
    // spelling a 2.0.x installation actually has on disk. The flattened `opts`
    // must absorb the transport options and leave `headers` empty.
    let legacy = concat!(
        "virtual_chunk_containers:\n",
        "  https://example.com/:\n",
        "    name: null\n",
        "    url_prefix: https://example.com/\n",
        "    store: !http\n",
        "      allow_http: \"true\"\n",
        "manifest: null\n",
    );
    let config: RepositoryConfig = serde_yaml_ng::from_str(legacy)?;
    let containers =
        config.virtual_chunk_containers.as_ref().expect("the document has one");
    match &containers["https://example.com/"].store {
        ObjectStoreConfig::Http(cfg) => {
            assert_eq!(cfg.opts.get("allow_http").map(String::as_str), Some("true"));
            assert!(cfg.headers.is_empty());
        }
        other => panic!("expected ObjectStoreConfig::Http, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn http_headers__backend_accepts_valid_headers_alongside_options() -> Res {
    let settings = Settings::default();

    let headers_only = HttpObjectStoreBackend {
        url: "https://example.com/".to_string(),
        config: None,
        headers: Some(HashMap::from([(
            "Authorization".to_string(),
            "Bearer token123".to_string(),
        )])),
    };
    assert!(headers_only.mk_object_store(&settings).is_ok());
    assert_eq!(headers_only.prefix(), "");

    let neither = HttpObjectStoreBackend {
        url: "https://example.com/".to_string(),
        config: None,
        headers: None,
    };
    assert!(neither.mk_object_store(&settings).is_ok());

    // Transport options and headers coexist: neither clobbers the other.
    let both = new_http_storage(
        "http://example.com/",
        Some(HashMap::from([("allow_http".to_string(), "true".to_string())])),
        Some(HashMap::from([(
            "authorization".to_string(),
            "Bearer token123".to_string(),
        )])),
    )?;
    both.default_settings().await?;
    Ok(())
}

#[tokio::test]
async fn http_headers__backend_rejects_unrepresentable_headers() {
    let settings = Settings::default();
    for (name, value) in [("bad header", "value"), ("X-Custom", "val\nue")] {
        let backend = HttpObjectStoreBackend {
            url: "https://example.com/".to_string(),
            config: None,
            headers: Some(HashMap::from([(
                name.to_string(),
                value.to_string(),
            )])),
        };
        assert!(
            backend.mk_object_store(&settings).is_err(),
            "header {name:?}={value:?} cannot be sent and must be refused"
        );
    }
}

// ===========================================================================
// GROUP cred_sentinels (new) - `Credentials::{LocalFileSystemAccess, HttpAccess}`.
// Provenance: icechunk/src/virtual_chunks.rs::validate_credentials;
// guide/guides-configuration.md "## Virtual Chunk Credentials".
// ===========================================================================

// `fs_container` stays at file scope because `ObjectStoreConfig::LocalFileSystem`
// is unchanged at this release. The `http`/`s3` containers are built inside the
// case bodies instead, because `HttpConfig` and `S3Options::default()` are new
// here - see the prelude rule at the top of this file.
fn fs_container(dir: &std::path::Path) -> VirtualChunkContainer {
    VirtualChunkContainer::new(
        file_url_prefix(dir),
        ObjectStoreConfig::LocalFileSystem(dir.to_path_buf()),
    )
    .unwrap()
}

#[tokio::test]
async fn cred_sentinels__authorize_their_own_backend() -> Res {
    use icechunk::config::HttpConfig;
    let http_container = || {
        VirtualChunkContainer::new(
            "http://example.com/".to_string(),
            ObjectStoreConfig::Http(HttpConfig::default()),
        )
        .unwrap()
    };
    let dir = tempfile::tempdir()?;
    assert!(
        fs_container(dir.path())
            .validate_credentials(Some(&Credentials::LocalFileSystemAccess))
            .is_ok()
    );
    assert!(
        http_container().validate_credentials(Some(&Credentials::HttpAccess)).is_ok()
    );
    Ok(())
}

#[tokio::test]
async fn cred_sentinels__are_rejected_for_another_backend() -> Res {
    use icechunk::config::HttpConfig;
    let http_container = || {
        VirtualChunkContainer::new(
            "http://example.com/".to_string(),
            ObjectStoreConfig::Http(HttpConfig::default()),
        )
        .unwrap()
    };
    let s3_container = || {
        VirtualChunkContainer::new(
            "s3://bucket/".to_string(),
            ObjectStoreConfig::S3(S3Options::default().with_region("us-east-1")),
        )
        .unwrap()
    };
    let dir = tempfile::tempdir()?;
    assert!(
        fs_container(dir.path())
            .validate_credentials(Some(&Credentials::HttpAccess))
            .is_err()
    );
    assert!(
        http_container()
            .validate_credentials(Some(&Credentials::LocalFileSystemAccess))
            .is_err()
    );
    assert!(
        s3_container()
            .validate_credentials(Some(&Credentials::LocalFileSystemAccess))
            .is_err()
    );
    assert!(
        s3_container().validate_credentials(Some(&Credentials::HttpAccess)).is_err()
    );
    // `AzureCredentials::Anonymous` is new at this milestone and is a real
    // credential, not a sentinel: it does not authorize another backend.
    let anonymous = Credentials::Azure(AzureCredentials::Anonymous);
    assert!(fs_container(dir.path()).validate_credentials(Some(&anonymous)).is_err());
    assert!(http_container().validate_credentials(Some(&anonymous)).is_err());

    // Section 5's matrix has a single row for the whole S3 family, so the two
    // sentinels are rejected by `S3Compatible` and `Tigris` containers too,
    // while `None` still validates on them.
    for (prefix, store) in [
        (
            "s3://bucket/",
            ObjectStoreConfig::S3Compatible(
                S3Options::default().with_region("us-east-1"),
            ),
        ),
        (
            "tigris://bucket/",
            ObjectStoreConfig::Tigris(S3Options::default().with_region("us-east-1")),
        ),
    ] {
        let container = VirtualChunkContainer::new(prefix.to_string(), store)?;
        assert!(
            container
                .validate_credentials(Some(&Credentials::LocalFileSystemAccess))
                .is_err()
        );
        assert!(container.validate_credentials(Some(&Credentials::HttpAccess)).is_err());
        assert!(container.validate_credentials(None).is_ok());
    }
    // ...and the reject column runs the other way as well: a real S3 credential
    // is not accepted by the two backends the sentinels belong to.
    let s3_cred = Credentials::S3(s3_static_credentials());
    assert!(fs_container(dir.path()).validate_credentials(Some(&s3_cred)).is_err());
    assert!(http_container().validate_credentials(Some(&s3_cred)).is_err());
    Ok(())
}

#[tokio::test]
async fn cred_sentinels__none_is_still_accepted() -> Res {
    use icechunk::config::HttpConfig;
    let http_container = || {
        VirtualChunkContainer::new(
            "http://example.com/".to_string(),
            ObjectStoreConfig::Http(HttpConfig::default()),
        )
        .unwrap()
    };
    let s3_container = || {
        VirtualChunkContainer::new(
            "s3://bucket/".to_string(),
            ObjectStoreConfig::S3(S3Options::default().with_region("us-east-1")),
        )
        .unwrap()
    };
    let dir = tempfile::tempdir()?;
    assert!(fs_container(dir.path()).validate_credentials(None).is_ok());
    assert!(http_container().validate_credentials(None).is_ok());
    assert!(s3_container().validate_credentials(None).is_ok());
    Ok(())
}

#[tokio::test]
async fn cred_sentinels__authorize_a_container_end_to_end() -> Res {
    let data = tempfile::tempdir()?;
    let chunk = data.path().join("chunk-1");
    let payload = Bytes::from_static(b"first");
    std::fs::write(&chunk, &payload)?;

    let prefix = file_url_prefix(data.path());
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(fs_container(data.path()))?;
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo = Repository::create(
        Some(config),
        storage,
        HashMap::from([(prefix.clone(), Some(Credentials::LocalFileSystemAccess))]),
        None,
        true,
    )
    .await?;
    assert!(repo.authorized_virtual_container_prefixes().contains(&prefix));

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path_of("/array"),
            ArrayShape::new(vec![(1, 1)]).unwrap(),
            None,
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            path_of("/array"),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_url(&format!(
                    "{prefix}chunk-1"
                ))?,
                offset: 0,
                length: payload.len() as u64,
                checksum: None,
            })),
        )
        .await?;
    session.commit("virtual").execute().await?;

    let read = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let store = Store::from_session(Arc::new(RwLock::new(read))).await;
    assert_eq!(store.get("array/c/0", &ByteRange::ALL).await?, payload);
    Ok(())
}

// ===========================================================================
// GROUP prefix_gate (new) - creating a repository at an object store's root.
// Provenance: upstream test_storage.rs::create_refuses_empty_prefix_on_object_store;
// guide/guides-storage.md "### Empty prefix and legacy repositories".
// ===========================================================================

// `native_s3` and `object_store_s3` are defined inside each case body below
// rather than here: both name surface this release introduces (`s3_storage`,
// `S3Options::default`) - see the prelude rule at the top of this file.

#[tokio::test]
async fn prefix_gate__storage_refuses_creation_at_an_empty_prefix() -> Res {
    use icechunk::storage::RepositoryCreation;
    // The native-S3 handle, built without touching the network.
    fn native_s3(
        prefix: &str,
    ) -> Result<icechunk::storage::S3Storage, Box<dyn std::error::Error>> {
        Ok(icechunk::storage::s3_storage(
            S3Options::default().with_region("us-east-1"),
            "testbucket".to_string(),
            Some(prefix.to_string()),
            Some(s3_static_credentials()),
            None,
        )?)
    }
    let root: Arc<dyn Storage + Send + Sync> = Arc::new(native_s3("")?);
    assert_eq!(
        root.can_create_repository().await?,
        RepositoryCreation::RefusedEmptyPrefix
    );

    // The backend-level half of the same gate (section 7): an `ObjectStorage`
    // refuses the root only when its backend restricts empty-prefix creation.
    // The HTTP backend keeps the provided `false`, which is the negative half of
    // that rule and the reason an empty `prefix()` is not enough on its own.
    let http_backend = HttpObjectStoreBackend {
        url: "https://example.com/".to_string(),
        config: None,
        headers: None,
    };
    assert!(!http_backend.restricts_empty_prefix_creation());
    assert!(http_backend.prefix().is_empty());
    Ok(())
}

#[tokio::test]
async fn prefix_gate__create_fails_before_any_io() -> Res {
    // The native-S3 handle, built without touching the network.
    fn native_s3(
        prefix: &str,
    ) -> Result<icechunk::storage::S3Storage, Box<dyn std::error::Error>> {
        Ok(icechunk::storage::s3_storage(
            S3Options::default().with_region("us-east-1"),
            "testbucket".to_string(),
            Some(prefix.to_string()),
            Some(s3_static_credentials()),
            None,
        )?)
    }
    let root: Arc<dyn Storage + Send + Sync> = Arc::new(native_s3("")?);
    // No endpoint is reachable; the gate runs before the first request, so this
    // must return the empty-prefix error rather than a transport error.
    let err = Repository::create(None, root, HashMap::new(), None, true).await.unwrap_err();
    assert!(
        matches!(err, ICError { kind: RepositoryErrorKind::EmptyPrefixCreation, .. }),
        "expected EmptyPrefixCreation"
    );
    Ok(())
}

#[tokio::test]
async fn prefix_gate__non_empty_prefix_and_local_backends_are_allowed() -> Res {
    use icechunk::storage::RepositoryCreation;
    // The native-S3 handle, built without touching the network.
    fn native_s3(
        prefix: &str,
    ) -> Result<icechunk::storage::S3Storage, Box<dyn std::error::Error>> {
        Ok(icechunk::storage::s3_storage(
            S3Options::default().with_region("us-east-1"),
            "testbucket".to_string(),
            Some(prefix.to_string()),
            Some(s3_static_credentials()),
            None,
        )?)
    }
    let prefixed: Arc<dyn Storage + Send + Sync> = Arc::new(native_s3("some/prefix")?);
    assert_eq!(prefixed.can_create_repository().await?, RepositoryCreation::Allowed);

    assert_eq!(
        new_in_memory_storage().await?.can_create_repository().await?,
        RepositoryCreation::Allowed
    );
    let dir = tempfile::tempdir()?;
    assert_eq!(
        new_local_filesystem_storage(dir.path()).await?.can_create_repository().await?,
        RepositoryCreation::Allowed
    );

    // The other half of the pair: the location hook is a no-op where there is
    // nothing to create, and creates the directory where there is.
    new_in_memory_storage().await?.create_location_if_needed().await?;
    let nested = dir.path().join("nested");
    new_local_filesystem_storage(&nested).await?.create_location_if_needed().await?;
    assert!(nested.is_dir(), "local-filesystem storage creates its own location");
    Ok(())
}

#[tokio::test]
async fn prefix_gate__escape_hatch_re_allows_the_bucket_root() -> Res {
    use icechunk::storage::RepositoryCreation;
    // The native-S3 handle, built without touching the network.
    fn native_s3(
        prefix: &str,
    ) -> Result<icechunk::storage::S3Storage, Box<dyn std::error::Error>> {
        Ok(icechunk::storage::s3_storage(
            S3Options::default().with_region("us-east-1"),
            "testbucket".to_string(),
            Some(prefix.to_string()),
            Some(s3_static_credentials()),
            None,
        )?)
    }
    // The `object_store`-backed cloud handle, built without touching the network.
    async fn object_store_s3(
        prefix: Option<&str>,
    ) -> Result<ObjectStorage, Box<dyn std::error::Error>> {
        Ok(ObjectStorage::new_s3(
            "testbucket".to_string(),
            prefix.map(str::to_string),
            Some(s3_static_credentials()),
            Some(S3Options::default().with_region("us-east-1")),
        )
        .await?)
    }
    let hatch: Arc<dyn Storage + Send + Sync> =
        Arc::new(native_s3("")?.unsafe_allow_empty_prefix_creation());
    assert_eq!(hatch.can_create_repository().await?, RepositoryCreation::Allowed);

    // The same refusal and the same hatch on the `object_store`-backed handle.
    let root: Arc<dyn Storage + Send + Sync> = Arc::new(object_store_s3(None).await?);
    assert_eq!(
        root.can_create_repository().await?,
        RepositoryCreation::RefusedEmptyPrefix
    );
    let os_hatch: Arc<dyn Storage + Send + Sync> =
        Arc::new(object_store_s3(None).await?.unsafe_allow_empty_prefix_creation());
    assert_eq!(os_hatch.can_create_repository().await?, RepositoryCreation::Allowed);
    Ok(())
}

#[tokio::test]
async fn prefix_gate__open_never_creates_the_location() -> Res {
    let dir = tempfile::tempdir()?;
    let missing = dir.path().join("never-created");
    let storage = new_local_filesystem_storage(&missing).await?;

    // A location that does not exist yet reports a clean root rather than
    // propagating the not-found error.
    assert!(storage.root_is_clean(&Settings::default()).await?);

    // `open` does not consult the creation gate and does not create anything.
    assert!(
        Repository::open(None, Arc::clone(&storage), HashMap::new()).await.is_err()
    );
    assert!(!missing.exists(), "open must not create the storage location");

    // `create` does create it.
    Repository::create(None, storage, HashMap::new(), None, true).await?;
    assert!(missing.is_dir(), "create creates the storage location");
    Ok(())
}

// ===========================================================================
// GROUP vurl_parts (new) - `VirtualChunkLocation::from_url` preserves every
// URL part of a non-`file://` location.
// Provenance: icechunk-format/src/manifest.rs::tests::{test_from_url_*}.
// ===========================================================================

#[tokio::test]
async fn vurl_parts__userinfo_port_query_and_fragment_survive() -> Res {
    let input = "https://user:pass@host.com:8443/a//b/../c.bin?versionId=42#frag";
    assert_eq!(VirtualChunkLocation::from_url(input)?.url(), input);
    Ok(())
}

#[tokio::test]
async fn vurl_parts__object_store_keys_are_verbatim() -> Res {
    for input in [
        "s3://foo/bar//../baz",
        "gcs://b/a//b",
        "s3://bucket/key.with.dots/../sibling",
        "az://container/a/./b//c",
        "tigris://bucket/x//y",
        "https://host/a//b/../c.bin?v=1#frag",
        "s3://user:pass@host:9000/a//b/../c?x=1#f",
    ] {
        assert_eq!(VirtualChunkLocation::from_url(input)?.url(), input);
    }
    Ok(())
}

#[tokio::test]
async fn vurl_parts__vcc_relative_locations_are_verbatim() -> Res {
    let input = "vcc://name/a//b/../c";
    let loc = VirtualChunkLocation::from_url(input)?;
    assert_eq!(loc.url(), input);
    assert!(loc.is_relative());
    // `is_relative` is exactly "starts with the container-reference scheme".
    assert_eq!(VCC_RELATIVE_URL_SCHEME, "vcc://");
    assert!(loc.url().starts_with(VCC_RELATIVE_URL_SCHEME));
    assert!(!VirtualChunkLocation::from_url("s3://b/k")?.is_relative());
    Ok(())
}

#[tokio::test]
async fn vurl_parts__file_urls_are_normalized_and_schemes_lowercased() -> Res {
    // `file://` is the container-confinement boundary and is still normalized.
    assert_eq!(VirtualChunkLocation::from_url("file:///a/b/../c")?.url(), "file:///a/c");
    assert_eq!(
        VirtualChunkLocation::from_url("file:///authorized/%2e%2e/secret")?.url(),
        "file:///secret"
    );
    assert_eq!(VirtualChunkLocation::from_url("file:/x")?.url(), "file:///x");
    // The scheme is not part of the key, so only it is case-folded.
    assert_eq!(
        VirtualChunkLocation::from_url("S3://Bucket/Key//..")?.url(),
        "s3://Bucket/Key//.."
    );
    Ok(())
}

#[tokio::test]
async fn vurl_parts__rejects_input_that_names_no_object() {
    let kind =
        |s: &str| VirtualChunkLocation::from_url(s).map(|_| ()).unwrap_err().kind;
    assert!(matches!(kind("not-a-url"), VirtualReferenceErrorKind::CannotParseUrl { .. }));
    assert!(matches!(kind(""), VirtualReferenceErrorKind::CannotParseUrl { .. }));
    assert!(matches!(kind("mailto:foo"), VirtualReferenceErrorKind::NoPathSegments(_)));
    assert!(matches!(
        kind("s3:///key"),
        VirtualReferenceErrorKind::CannotParseBucketName(_)
    ));
    assert!(matches!(
        kind("s3:/bucket/key"),
        VirtualReferenceErrorKind::CannotParseBucketName(_)
    ));
    assert!(matches!(
        kind("vcc:///rel"),
        VirtualReferenceErrorKind::NoContainerForName(_)
    ));
}

// ===========================================================================
// GROUP vurl_resolve (new) - container matching and location resolution keep
// the port, and reject keys an object-store backend cannot address.
// Provenance: icechunk/src/virtual_chunks.rs::{matching_container,
// expand_location, validate_virtual_chunk_location};
// upstream test_virtual_refs.rs::test_file_virtual_ref_empty_segment_rejected_at_write.
// ===========================================================================

async fn repo_with_containers(
    containers: Vec<VirtualChunkContainer>,
) -> Result<Repository, Box<dyn std::error::Error>> {
    let mut config = RepositoryConfig::default();
    let mut auth = HashMap::new();
    for container in containers {
        auth.insert(container.url_prefix().to_string(), None);
        config.set_virtual_chunk_container(container)?;
    }
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(Repository::create(Some(config), storage, auth, None, true).await?)
}

#[tokio::test]
async fn vurl_resolve__non_default_ports_are_part_of_the_location() -> Res {
    let with_port = "http://127.0.0.1:8080/bucket/chunk-1";
    assert_eq!(VirtualChunkLocation::from_url(with_port)?.url(), with_port);
    // A location with no port keeps having none...
    assert_eq!(
        VirtualChunkLocation::from_url("http://127.0.0.1/bucket/chunk-1")?.url(),
        "http://127.0.0.1/bucket/chunk-1"
    );
    // ...and a *default* port is not special either: section 8's verbatim rule
    // applies to it like any other part of the string, so it is neither dropped
    // nor re-added.
    for verbatim in [
        "http://127.0.0.1:80/bucket/chunk-1",
        "https://example.com:443/bucket/chunk-1",
    ] {
        assert_eq!(VirtualChunkLocation::from_url(verbatim)?.url(), verbatim);
    }
    Ok(())
}

#[tokio::test]
async fn vurl_resolve__containers_on_different_ports_are_distinct() -> Res {
    use icechunk::config::HttpConfig;
    let a = VirtualChunkContainer::new(
        "http://127.0.0.1:8080/".to_string(),
        ObjectStoreConfig::Http(HttpConfig {
            opts: HashMap::from([("allow_http".to_string(), "true".to_string())]),
            headers: HashMap::new(),
        }),
    )?;
    let b = VirtualChunkContainer::new(
        "http://127.0.0.1:9090/".to_string(),
        ObjectStoreConfig::Http(HttpConfig::default()),
    )?;
    let repo = repo_with_containers(vec![a, b]).await?;
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;

    let on_8080 = VirtualChunkLocation::from_url("http://127.0.0.1:8080/chunk")?;
    let on_9090 = VirtualChunkLocation::from_url("http://127.0.0.1:9090/chunk")?;
    let matched_8080 = session.matching_container(&on_8080).expect("8080 container");
    let matched_9090 = session.matching_container(&on_9090).expect("9090 container");
    assert_eq!(matched_8080.url_prefix(), "http://127.0.0.1:8080/");
    assert_eq!(matched_9090.url_prefix(), "http://127.0.0.1:9090/");

    // A third port matches nothing, so the port is not merely decorative.
    let on_7070 = VirtualChunkLocation::from_url("http://127.0.0.1:7070/chunk")?;
    assert!(session.matching_container(&on_7070).is_none());
    Ok(())
}

#[tokio::test]
async fn vurl_resolve__relative_locations_expand_against_their_container() -> Res {
    use icechunk::config::HttpConfig;
    let named = VirtualChunkContainer::new_named(
        "web".to_string(),
        "http://127.0.0.1:8080/data/".to_string(),
        ObjectStoreConfig::Http(HttpConfig::default()),
    )?;
    let repo = repo_with_containers(vec![named]).await?;
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;

    let relative = VirtualChunkLocation::from_url("vcc://web/chunk-1")?;
    assert_eq!(
        session.resolve_virtual_location(&relative)?,
        "http://127.0.0.1:8080/data/chunk-1",
        "expansion keeps the container's port"
    );
    // An absolute location is returned unchanged.
    let absolute = VirtualChunkLocation::from_url("http://127.0.0.1:8080/data/x")?;
    assert_eq!(
        session.resolve_virtual_location(&absolute)?,
        "http://127.0.0.1:8080/data/x"
    );
    // An unknown container name is an error, not a silent pass-through.
    let unknown = VirtualChunkLocation::from_url("vcc://nope/chunk-1")?;
    assert!(matches!(
        session.resolve_virtual_location(&unknown).unwrap_err().kind,
        VirtualReferenceErrorKind::NoContainerForName(_)
    ));
    Ok(())
}

#[tokio::test]
async fn vurl_resolve__the_resolver_validates_locations_directly() -> Res {
    use icechunk::config::HttpConfig;
    let container = VirtualChunkContainer::new(
        "http://127.0.0.1:8080/".to_string(),
        ObjectStoreConfig::Http(HttpConfig::default()),
    )?;
    let resolver = VirtualChunkResolver::new(
        vec![container].into_iter(),
        HashMap::from([("http://127.0.0.1:8080/".to_string(), None)]),
        Settings::default(),
    );

    let covered = VirtualChunkLocation::from_url("http://127.0.0.1:8080/chunk")?;
    resolver.validate_virtual_chunk_location(&covered)?;

    // A location no container covers is refused by the resolver itself, not
    // only by the `Session` wrapper that delegates to it.
    let uncovered = VirtualChunkLocation::from_url("http://127.0.0.1:7070/chunk")?;
    assert!(matches!(
        resolver.validate_virtual_chunk_location(&uncovered).unwrap_err().kind,
        VirtualReferenceErrorKind::NoContainerForUrl(_)
    ));

    // And the `Session` wrapper gives the same answer for the same location.
    let dir = tempfile::tempdir()?;
    let repo = repo_with_containers(vec![fs_container(dir.path())]).await?;
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let elsewhere = VirtualChunkLocation::from_url("s3://other/chunk")?;
    assert!(matches!(
        session.validate_virtual_chunk_location(&elsewhere).unwrap_err().kind,
        VirtualReferenceErrorKind::NoContainerForUrl(_)
    ));
    Ok(())
}

#[tokio::test]
async fn vurl_resolve__keys_an_object_store_cannot_address_are_refused() -> Res {
    let data = tempfile::tempdir()?;
    let repo = repo_with_containers(vec![fs_container(data.path())]).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path_of("/array"),
            ArrayShape::new(vec![(1, 1)]).unwrap(),
            None,
            Bytes::new(),
        )
        .await?;
    session.commit("array").execute().await?;

    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    let location = format!("file://{}/sub//chunk.bin", data.path().to_str().unwrap());
    let reference = VirtualChunkRef {
        location: VirtualChunkLocation::from_url(&location)?,
        offset: 0,
        length: 4,
        checksum: None,
    };
    let err = store.set_virtual_ref("array/c/0", reference, true).await.unwrap_err();
    assert!(
        matches!(
            err.kind,
            StoreErrorKind::SessionError(SessionErrorKind::VirtualReferenceError(
                VirtualReferenceErrorKind::UnsupportedObjectKeyForBackend(_)
            ))
        ),
        "an empty path segment must be refused at write time"
    );
    Ok(())
}

// ===========================================================================
// GROUP migration_2_1 (new) - the source-breaking changes this release makes
// to previously public items.
// Provenance: icechunk-format/src/snapshot.rs::SnapshotInfo::from_snapshot_file;
// icechunk-storage/src/s3_config.rs; icechunk-s3/src/lib.rs;
// icechunk-arrow-object-store/src/lib.rs.
// ===========================================================================

#[tokio::test]
async fn migration_2_1__snapshot_info_is_built_with_from_snapshot_file() -> Res {
    let (_repo, am, _initial, pruned, c) = expired_fixture().await?;
    let snapshot = am.fetch_snapshot(&c).await?;
    let from_file = SnapshotInfo::from_snapshot_file(snapshot.as_ref())?;
    assert_eq!(from_file.id, c);
    assert_eq!(from_file.message, "/c");
    // The snapshot FILE does not store the pruned chain; the repository table
    // of contents does.
    assert!(from_file.pruned_ancestor_tx_logs.is_empty());
    let (repo_info, _) = am.fetch_repo_info().await?;
    assert_eq!(repo_info.find_snapshot(&c)?.pruned_ancestor_tx_logs, pruned);
    Ok(())
}

#[tokio::test]
async fn migration_2_1__s3_options_are_built_not_written_as_a_literal() {
    use icechunk::config::S3ChecksumAlgorithm;
    let opts = S3Options::default()
        .with_region("us-east-1")
        .with_endpoint_url("http://localhost:9000")
        .with_allow_http(true)
        .with_force_path_style(true)
        .with_anonymous(false)
        .with_requester_pays(true)
        .with_network_stream_timeout_seconds(30)
        .with_checksum_algorithm(S3ChecksumAlgorithm::Crc32c);
    assert_eq!(opts.region.as_deref(), Some("us-east-1"));
    assert_eq!(opts.endpoint_url.as_deref(), Some("http://localhost:9000"));
    assert!(opts.allow_http);
    assert!(opts.force_path_style);
    assert!(!opts.anonymous);
    assert!(opts.requester_pays);
    assert_eq!(opts.network_stream_timeout_seconds, Some(30));
    assert_eq!(opts.checksum_algorithm, Some(S3ChecksumAlgorithm::Crc32c));
    assert_eq!(S3Options::default().checksum_algorithm, None);
}

#[tokio::test]
async fn migration_2_1__s3_constructors_take_a_layout_override() -> Res {
    use icechunk::storage::s3_storage;
    let creds = s3_static_credentials();
    let opts = || S3Options::default().with_region("us-east-1");

    // `None` = detect, `Some(false)` = force the standard layout. Neither
    // touches the network at construction time.
    s3_storage(opts(), "b".to_string(), Some("p".to_string()), Some(creds.clone()), None)?;
    s3_storage(opts(), "b".to_string(), Some("p".to_string()), Some(creds.clone()), Some(false))?;
    new_s3_storage(opts(), "b".to_string(), Some("p".to_string()), Some(creds.clone()), None)?;
    new_r2_storage(
        opts(),
        Some("b".to_string()),
        Some("p".to_string()),
        Some("account".to_string()),
        Some(creds.clone()),
        Some(false),
    )?;
    new_tigris_storage(
        opts(),
        "b".to_string(),
        Some("p".to_string()),
        Some(creds.clone()),
        false,
        None,
    )?;
    // `Some(true)` forces the legacy rooted layout, which section 10 says is
    // only valid with an empty prefix: it builds at the bucket root and is
    // refused under a prefix. Neither call touches the network.
    s3_storage(opts(), "b".to_string(), None, Some(creds.clone()), Some(true))?;
    assert!(
        s3_storage(
            opts(),
            "b".to_string(),
            Some("p".to_string()),
            Some(creds.clone()),
            Some(true),
        )
        .is_err(),
        "the legacy rooted layout is only valid with an empty prefix"
    );

    // The inherent constructor carries the same trailing parameter.
    icechunk::storage::S3Storage::new(
        opts(),
        "b".to_string(),
        Some("p".to_string()),
        creds,
        true,
        Vec::new(),
        Vec::new(),
        Some(false),
    )?;
    Ok(())
}

#[tokio::test]
async fn migration_2_1__http_constructors_take_headers() -> Res {
    // Two-argument construction is gone; the third argument is the header map.
    let no_headers = new_http_storage("https://example.com/", None, None)?;
    assert!(!no_headers.can_write().await?);
    let with_headers = new_http_storage(
        "https://example.com/",
        None,
        Some(HashMap::from([("authorization".to_string(), "Bearer t".to_string())])),
    )?;
    with_headers.default_settings().await?;
    Ok(())
}

#[tokio::test]
async fn migration_2_1__path_gained_a_parent_accessor() -> Res {
    let leaf = path_of("/group/array");
    let parent = leaf.parent().expect("a nested path has a parent");
    assert_eq!(parent, path_of("/group"));
    assert_eq!(parent.parent(), Some(Path::root()));
    Ok(())
}

#[tokio::test]
async fn migration_2_1__additive_variants_and_telemetry_shutdown_exist() -> Res {
    // The three move-related rejection categories this release adds must exist
    // as variants of the (already `#[non_exhaustive]`) session error enum.
    // Their semantics are out of scope at this step; their existence is not.
    for kind in [
        SessionErrorKind::MoveIntoSelfOrDescendant {
            from: path_of("/a"),
            to: path_of("/a/b"),
        },
        SessionErrorKind::MoveDestinationParentMissing {
            to: path_of("/a/b"),
            missing_parent: path_of("/a"),
        },
        SessionErrorKind::MoveDestinationParentNotGroup {
            to: path_of("/a/b"),
            parent: path_of("/a"),
        },
    ] {
        assert!(matches!(
            kind,
            SessionErrorKind::MoveIntoSelfOrDescendant { .. }
                | SessionErrorKind::MoveDestinationParentMissing { .. }
                | SessionErrorKind::MoveDestinationParentNotGroup { .. }
        ));
    }

    // `shutdown_telemetry` exists and is a callable no-op without `otel`.
    icechunk::shutdown_telemetry();
    icechunk::shutdown_telemetry();
    Ok(())
}

// ===========================================================================
// GROUP inh_repo_sessions (inherited, steps 01-02) - repositories, sessions,
// branches, tags and conflict detection.
// ===========================================================================

#[tokio::test]
async fn inh_repo_sessions__commit_and_read_back() -> Res {
    let repo = memory_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::from_static(b"{}")).await?;
    add_small_array(&mut session, "/array", 2).await?;
    let snap = session.commit("initial").execute().await?;

    let read = repo.readonly_session(&VersionInfo::SnapshotId(snap.clone())).await?;
    assert_eq!(read.get_group(&Path::root()).await?.path, Path::root());
    let array = read.get_array(&path_of("/array")).await?;
    assert_eq!(array.path, path_of("/array"));
    let history = ancestry_of(&repo, &VersionInfo::SnapshotId(snap.clone())).await?;
    assert_eq!(history[0].id, snap);
    assert_eq!(history[0].message, "initial");
    Ok(())
}

#[tokio::test]
async fn inh_repo_sessions__branches_and_tags() -> Res {
    let repo = memory_repo().await?;
    let first = commit_group(&repo, "main", "/").await?;
    repo.create_branch("feature", &first).await?;
    let on_feature = commit_group(&repo, "feature", "/only-on-feature").await?;
    repo.create_tag("v1", &first).await?;

    assert!(repo.list_branches().await?.contains("feature"));
    assert!(repo.list_tags().await?.contains("v1"));
    assert_eq!(repo.lookup_tag("v1").await?, first);
    assert_eq!(repo.lookup_branch("feature").await?, on_feature);
    assert_eq!(repo.lookup_branch("main").await?, first);

    let tagged = repo.readonly_session(&VersionInfo::TagRef("v1".into())).await?;
    assert!(tagged.get_group(&path_of("/only-on-feature")).await.is_err());
    assert!(repo.create_tag("v1", &first).await.is_err(), "tags are immutable");
    Ok(())
}

#[tokio::test]
async fn inh_repo_sessions__readonly_sessions_reject_writes() -> Res {
    let repo = memory_repo().await?;
    let snap = commit_group(&repo, "main", "/").await?;
    let mut read = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(read.read_only());
    let err = read.add_group(path_of("/nope"), Bytes::new()).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::ReadOnlySession));
    Ok(())
}

#[tokio::test]
async fn inh_repo_sessions__concurrent_commits_conflict() -> Res {
    let repo = memory_repo().await?;
    commit_group(&repo, "main", "/").await?;
    let mut first = repo.writable_session("main").await?;
    let mut second = repo.writable_session("main").await?;
    first.add_group(path_of("/x"), Bytes::new()).await?;
    second.add_group(path_of("/x"), Bytes::new()).await?;
    first.commit("first wins").execute().await?;

    let err = second.commit("second loses").execute().await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::Conflict { .. }));
    let rebase_err = second.rebase(&ConflictDetector).await.unwrap_err();
    assert!(matches!(rebase_err.kind, SessionErrorKind::RebaseFailed { .. }));
    Ok(())
}

#[tokio::test]
async fn inh_repo_sessions__zarr_keys_round_trip_through_the_store() -> Res {
    let repo = memory_repo().await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::from_static(br#"{"zarr_format":3,"node_type":"group"}"#)).await?;
    store
        .set(
            "array/zarr.json",
            Bytes::from_static(
                br#"{"zarr_format":3,"node_type":"array","attributes":{},"shape":[4],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[2]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"bytes","configuration":{"endian":"little"}}]}"#,
            ),
        )
        .await?;
    store.set("array/c/0", Bytes::from_static(b"01234567")).await?;
    assert_eq!(store.get("array/c/0", &ByteRange::ALL).await?, Bytes::from_static(b"01234567"));
    assert!(store.exists("array/zarr.json").await?);

    let keys: Vec<String> = store.list_prefix("array/").await?.try_collect().await?;
    assert!(keys.iter().any(|k| k == "array/zarr.json"));
    Ok(())
}

// ===========================================================================
// GROUP inh_format_headers (inherited, steps 03-04) - the on-disk file header,
// structured error kinds, expiration and garbage collection.
// ===========================================================================

#[tokio::test]
async fn inh_format_headers__written_objects_start_with_the_documented_header() -> Res {
    let dir = tempfile::tempdir()?;
    let storage = new_local_filesystem_storage(dir.path()).await?;
    let repo = Repository::create(None, storage, HashMap::new(), None, true).await?;
    let snap = commit_group(&repo, "main", "/").await?;

    // The byte offsets are the ones TEST_INTERFACE.md section 3 and PROFILE.md
    // R2 tabulate; they are spelled literally because this is an inherited
    // milestone's observable and the named constants are new at this release.
    let bytes = std::fs::read(dir.path().join("snapshots").join(snap.to_string()))?;
    assert!(bytes.len() > 39);
    assert!(bytes.starts_with(ICECHUNK_FORMAT_MAGIC_BYTES));
    assert_eq!(bytes[36], SpecVersionBin::V2 as u8);
    assert_eq!(bytes[37], FileTypeBin::Snapshot as u8);

    let repo_bytes = std::fs::read(dir.path().join("repo"))?;
    assert!(repo_bytes.len() > 39);
    assert!(repo_bytes.starts_with(ICECHUNK_FORMAT_MAGIC_BYTES));
    assert_eq!(repo_bytes[36], SpecVersionBin::V2 as u8);
    assert_eq!(repo_bytes[37], FileTypeBin::RepoInfo as u8);
    Ok(())
}

#[tokio::test]
async fn inh_format_headers__errors_carry_a_structured_kind() -> Res {
    let repo = memory_repo().await?;
    commit_group(&repo, "main", "/").await?;

    let missing = SnapshotId::random();
    let err = repo.lookup_snapshot(&missing).await.unwrap_err();
    assert!(matches!(
        err.kind,
        RepositoryErrorKind::FormatError(
            IcechunkFormatErrorKind::SnapshotIdNotFound { .. }
        )
    ));

    let err = repo.lookup_branch("no-such-branch").await.unwrap_err();
    assert!(matches!(err.kind, RepositoryErrorKind::Ref(RefErrorKind::RefNotFound(_))));

    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let err = session.get_array(&path_of("/nope")).await.unwrap_err();
    assert!(matches!(err.kind, SessionErrorKind::NodeNotFound { .. }));
    Ok(())
}

#[tokio::test]
async fn inh_format_headers__expire_and_gc_remove_unreachable_objects() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/b").await?;

    let result = expire_at(&am, threshold).await?;
    assert_eq!(result.released_snapshots.len(), 2);
    let summary = gc_all(&am).await?;
    assert_eq!(summary.snapshots_deleted, 2);
    // Only the surviving snapshots remain reachable.
    let remaining = ancestry_of(&repo, &VersionInfo::BranchTipRef("main".into())).await?;
    assert_eq!(remaining.len(), 2);
    Ok(())
}

#[tokio::test]
async fn inh_format_headers__tags_hold_snapshots_alive_through_gc() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    commit_group(&repo, "main", "/").await?;
    let pinned = commit_group(&repo, "main", "/a").await?;
    repo.create_tag("keep", &pinned).await?;
    let threshold = threshold_between_commits().await;
    commit_group(&repo, "main", "/b").await?;

    expire_at(&am, threshold).await?;
    gc_all(&am).await?;
    // The tagged snapshot is still readable after GC.
    let tagged = repo.readonly_session(&VersionInfo::TagRef("keep".into())).await?;
    assert_eq!(tagged.snapshot_id(), &pinned);
    assert!(tagged.get_group(&path_of("/a")).await.is_ok());
    Ok(())
}

// ===========================================================================
// GROUP inh_cli_repo (inherited, step 05) - the `icechunk` binary built with
// `--features cli`, found through ICECHUNK_TEST_CLI_BIN.
// ===========================================================================

fn cli_bin() -> std::path::PathBuf {
    std::path::PathBuf::from(
        std::env::var("ICECHUNK_TEST_CLI_BIN")
            .expect("ICECHUNK_TEST_CLI_BIN must name the candidate's icechunk binary"),
    )
}

/// Write a CLI config naming one local-filesystem repository under `home`,
/// which is then used as XDG_CONFIG_HOME. The CLI reads
/// `<config dir>/icechunk/cli-config.yaml`, whose `RepositoryDefinition` values
/// are YAML-tagged enum variants.
fn write_cli_config(
    home: &std::path::Path,
    alias: &str,
    repo_path: &std::path::Path,
) -> Res {
    let dir = home.join("icechunk");
    std::fs::create_dir_all(&dir)?;
    let doc = format!(
        "repos:\n  {alias}: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
        repo_path.to_str().unwrap()
    );
    std::fs::write(dir.join("cli-config.yaml"), doc)?;
    Ok(())
}

fn run_cli(home: &std::path::Path, args: &[&str]) -> std::process::Output {
    std::process::Command::new(cli_bin())
        .args(args)
        .env("XDG_CONFIG_HOME", home)
        .env("HOME", home)
        .env("ICECHUNK_NO_LOGS", "1")
        .output()
        .expect("the icechunk binary must be executable")
}

#[tokio::test]
async fn inh_cli_repo__help_lists_the_command_groups() {
    let home = tempfile::tempdir().unwrap();
    let out = run_cli(home.path(), &["--help"]);
    assert!(out.status.success(), "`--help` must exit 0");
    let text = String::from_utf8_lossy(&out.stdout).to_lowercase();
    for group in ["repo", "snapshot", "config"] {
        assert!(text.contains(group), "`--help` must list the `{group}` command group");
    }
}

#[tokio::test]
async fn inh_cli_repo__creates_a_repository_from_the_config() -> Res {
    let home = tempfile::tempdir()?;
    let data = tempfile::tempdir()?;
    let repo_path = data.path().join("repo");
    write_cli_config(home.path(), "local", &repo_path)?;

    let out = run_cli(home.path(), &["repo", "create", "local"]);
    assert!(
        out.status.success(),
        "repo create failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let storage = new_local_filesystem_storage(&repo_path).await?;
    assert!(Repository::exists(storage, None).await?);
    Ok(())
}

#[tokio::test]
async fn inh_cli_repo__lists_snapshots_of_a_local_repository() -> Res {
    let home = tempfile::tempdir()?;
    let data = tempfile::tempdir()?;
    let repo_path = data.path().join("repo");
    std::fs::create_dir_all(&repo_path)?;
    let storage = new_local_filesystem_storage(&repo_path).await?;
    let repo = Repository::create(None, storage, HashMap::new(), None, true).await?;
    commit_group(&repo, "main", "/").await?;
    commit_group(&repo, "main", "/a").await?;
    // The initial snapshot plus the two commits above.
    let expected =
        ancestry_of(&repo, &VersionInfo::BranchTipRef("main".into())).await?.len();
    assert_eq!(expected, 3, "the fixture repository has three snapshots");
    drop(repo);

    write_cli_config(home.path(), "local", &repo_path)?;
    let out = run_cli(home.path(), &["snapshot", "list", "local"]);
    assert!(
        out.status.success(),
        "snapshot list failed: {}",
        String::from_utf8_lossy(&out.stderr)
    );
    let lines = String::from_utf8_lossy(&out.stdout)
        .lines()
        .filter(|l| !l.trim().is_empty())
        .count();
    // Section 9.3 grades "one line per snapshot", so the count is exact.
    assert_eq!(lines, expected, "expected exactly one line per snapshot");
    Ok(())
}

#[tokio::test]
async fn inh_cli_repo__rejects_unknown_input_with_a_nonzero_status() -> Res {
    let home = tempfile::tempdir()?;
    let out = run_cli(home.path(), &["definitely-not-a-subcommand"]);
    assert!(!out.status.success(), "an unknown subcommand must not exit 0");

    let data = tempfile::tempdir()?;
    write_cli_config(home.path(), "local", &data.path().join("repo"))?;
    let out = run_cli(home.path(), &["snapshot", "list", "no-such-alias"]);
    assert!(!out.status.success(), "an unknown alias must not exit 0");
    Ok(())
}

// ===========================================================================
// GROUP inh_manifest_split (inherited, step 06) - manifest splitting policy.
// ===========================================================================

fn splitting_config(split_size: u32) -> RepositoryConfig {
    RepositoryConfig {
        manifest: Some(ManifestConfig {
            splitting: Some(ManifestSplittingConfig::with_size(split_size)),
            ..ManifestConfig::default()
        }),
        ..RepositoryConfig::default()
    }
}

async fn commit_array_with_chunks(
    repo: &Repository,
    chunks: u32,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    add_small_array(&mut session, "/array", chunks).await?;
    Ok(session.commit("array").execute().await?)
}

#[tokio::test]
async fn inh_manifest_split__a_split_size_produces_several_manifests() -> Res {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo =
        Repository::create(Some(splitting_config(2)), storage, HashMap::new(), None, true)
            .await?;
    let snap = commit_array_with_chunks(&repo, 8).await?;
    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert!(manifests.len() > 1, "8 chunks split every 2 must span several manifests");
    Ok(())
}

#[tokio::test]
async fn inh_manifest_split__the_default_policy_produces_one_manifest() -> Res {
    let repo = memory_repo().await?;
    let snap = commit_array_with_chunks(&repo, 8).await?;
    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert_eq!(manifests.len(), 1);
    Ok(())
}

#[tokio::test]
async fn inh_manifest_split__reads_are_unchanged_by_the_split_policy() -> Res {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo =
        Repository::create(Some(splitting_config(2)), storage, HashMap::new(), None, true)
            .await?;
    let snap = commit_array_with_chunks(&repo, 8).await?;
    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    for i in 0..8u32 {
        assert_eq!(
            store.get(&format!("array/c/{i}"), &ByteRange::ALL).await?,
            Bytes::copy_from_slice(&[i as u8; 4])
        );
    }
    Ok(())
}

#[tokio::test]
async fn inh_manifest_split__conditions_select_which_arrays_split() -> Res {
    // The same policy and the same array shape, in two repositories that each
    // hold a single array: only the array whose NAME matches the condition is
    // split. Counting one array per repository keeps the assertion on section
    // 9.4's rule alone, instead of also depending on whether an unchanged
    // array's manifests are carried into a later snapshot or recomputed.
    async fn manifests_for(name: &str) -> Result<usize, Box<dyn std::error::Error>> {
        let only_named = ManifestSplittingConfig {
            split_sizes: Some(vec![(
                ManifestSplitCondition::NameMatches { regex: "^split$".to_string() },
                vec![ManifestSplitDim {
                    condition: ManifestSplitDimCondition::Axis(0),
                    num_chunks: 2,
                }],
            )]),
        };
        let config = RepositoryConfig {
            manifest: Some(ManifestConfig {
                splitting: Some(only_named),
                ..ManifestConfig::default()
            }),
            ..RepositoryConfig::default()
        };
        let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
        let repo =
            Repository::create(Some(config), storage, HashMap::new(), None, true)
                .await?;
        let mut session = repo.writable_session("main").await?;
        session.add_group(Path::root(), Bytes::new()).await?;
        add_small_array(&mut session, name, 6).await?;
        let snap = session.commit(name).execute().await?;
        let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
        Ok(manifests.len())
    }
    assert!(manifests_for("/split").await? > 1, "the matching array is split");
    assert_eq!(
        manifests_for("/other").await?,
        1,
        "an array the condition does not name keeps a single manifest"
    );
    Ok(())
}

// ===========================================================================
// GROUP inh_virtual_containers (inherited, steps 07-08) - virtual chunk
// container rules and the inspect module.
// ===========================================================================

#[tokio::test]
async fn inh_virtual_containers__url_prefix_must_end_in_a_separator() -> Res {
    let dir = tempfile::tempdir()?;
    let prefix = file_url_prefix(dir.path());
    assert!(
        VirtualChunkContainer::new(
            prefix.trim_end_matches('/').to_string(),
            ObjectStoreConfig::LocalFileSystem(dir.path().to_path_buf()),
        )
        .is_err(),
        "a prefix without a trailing separator is rejected"
    );
    assert!(
        VirtualChunkContainer::new(
            prefix,
            ObjectStoreConfig::LocalFileSystem(dir.path().to_path_buf()),
        )
        .is_ok()
    );
    // The scheme must match the backend.
    assert!(
        VirtualChunkContainer::new(
            "s3://bucket/".to_string(),
            ObjectStoreConfig::LocalFileSystem(dir.path().to_path_buf()),
        )
        .is_err()
    );
    // Section 9.5 states both rules for `new_named` too, not only for `new`.
    let named_dir = tempfile::tempdir()?;
    let named_prefix = file_url_prefix(named_dir.path());
    assert!(
        VirtualChunkContainer::new_named(
            "cont".to_string(),
            named_prefix.trim_end_matches('/').to_string(),
            ObjectStoreConfig::LocalFileSystem(named_dir.path().to_path_buf()),
        )
        .is_err(),
        "`new_named` rejects a prefix without a trailing separator too"
    );
    assert!(
        VirtualChunkContainer::new_named(
            "cont".to_string(),
            named_prefix,
            ObjectStoreConfig::LocalFileSystem(named_dir.path().to_path_buf()),
        )
        .is_ok()
    );
    Ok(())
}

#[tokio::test]
async fn inh_virtual_containers__a_sibling_prefix_is_not_covered() -> Res {
    // The rule is literal prefix matching; it is driven here over a
    // local-filesystem container because that backend's payload is the one this
    // release leaves unchanged.
    let dir = tempfile::tempdir()?;
    let authorized = dir.path().join("authorized");
    std::fs::create_dir_all(&authorized)?;
    let prefix = file_url_prefix(&authorized);
    let container = VirtualChunkContainer::new(
        prefix.clone(),
        ObjectStoreConfig::LocalFileSystem(authorized.clone()),
    )?;
    let repo = repo_with_containers(vec![container]).await?;
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let inside = VirtualChunkLocation::from_url(&format!("{prefix}chunk"))?;
    let sibling = VirtualChunkLocation::from_url(&format!(
        "{}-evil/chunk",
        prefix.trim_end_matches('/')
    ))?;
    assert!(session.matching_container(&inside).is_some());
    assert!(
        session.matching_container(&sibling).is_none(),
        "prefix matching must not leak into a sibling directory"
    );
    Ok(())
}

#[tokio::test]
async fn inh_virtual_containers__an_unmatched_location_is_refused() -> Res {
    // The inherited observable is container matching itself: a location under a
    // registered prefix matches, and one under no prefix at all matches
    // nothing, whatever scheme it names. The error *identity* a rejection
    // carries is §8's, and `vurl_resolve` scores it.
    let dir = tempfile::tempdir()?;
    let prefix = file_url_prefix(dir.path());
    let repo = repo_with_containers(vec![fs_container(dir.path())]).await?;
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;

    let covered = VirtualChunkLocation::from_url(&format!("{prefix}chunk"))?;
    assert!(
        session.matching_container(&covered).is_some(),
        "a location under the registered prefix is covered"
    );
    for elsewhere in ["s3://other/chunk", "file:///nowhere/at/all/chunk"] {
        let loc = VirtualChunkLocation::from_url(elsewhere)?;
        assert!(
            session.matching_container(&loc).is_none(),
            "{elsewhere} is covered by no container and must match none"
        );
    }
    Ok(())
}

#[tokio::test]
async fn inh_virtual_containers__inspect_renders_snapshot_and_repo_info() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    add_small_array(&mut session, "/array", 2).await?;
    let snap = session.commit("array").execute().await?;

    let snapshot_json = icechunk::inspect::snapshot_json(am.as_ref(), &snap, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&snapshot_json)?;
    assert_eq!(parsed.get("id").and_then(|v| v.as_str()), Some(snap.to_string().as_str()));

    let repo_json = icechunk::inspect::repo_info_json(am.as_ref(), true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&repo_json)?;
    assert!(parsed.is_object(), "the repo info renders as a JSON object");

    // The fourth `inspect` entry point renders a manifest of the same snapshot.
    let manifests: Vec<_> = repo.lookup_manifest_files(&snap).await?.collect();
    assert!(!manifests.is_empty(), "an array with chunks must produce a manifest");
    let manifest_json =
        icechunk::inspect::manifest_json(am.as_ref(), &manifests[0].id, true).await?;
    let parsed: serde_json::Value = serde_json::from_str(&manifest_json)?;
    assert!(parsed.is_object(), "a manifest renders as a JSON object");
    Ok(())
}

// ===========================================================================
// GROUP inh_toc_ops_log (inherited, step 09) - the Icechunk 2 repository table
// of contents and its operations log.
// ===========================================================================

#[tokio::test]
async fn inh_toc_ops_log__table_of_contents_lists_refs_and_snapshots() -> Res {
    let repo = memory_repo().await?;
    let am = Arc::clone(repo.asset_manager());
    let first = commit_group(&repo, "main", "/").await?;
    repo.create_branch("feature", &first).await?;
    repo.create_tag("v1", &first).await?;

    let (info, _version) = am.fetch_repo_info().await?;
    let branches: HashSet<String> =
        info.branches()?.map(|(name, _)| name.to_string()).collect();
    assert!(branches.contains("main") && branches.contains("feature"));
    let tags: HashSet<String> = info.tag_names()?.map(|s| s.to_string()).collect();
    assert!(tags.contains("v1"));
    assert_eq!(info.resolve_branch("feature")?, first);
    assert_eq!(info.resolve_tag("v1")?, first);
    assert_eq!(info.find_snapshot(&first)?.id, first);
    assert_eq!(info.spec_version()?, SpecVersionBin::V2);
    Ok(())
}

#[tokio::test]
async fn inh_toc_ops_log__operations_log_records_repository_changes() -> Res {
    let repo = memory_repo().await?;
    let first = commit_group(&repo, "main", "/").await?;
    repo.create_branch("feature", &first).await?;
    repo.create_tag("v1", &first).await?;

    let (stream, _info, _version) = repo.ops_log().await?;
    let entries: Vec<(DateTime<Utc>, UpdateType, Option<String>)> =
        stream.try_collect().await?;
    assert!(!entries.is_empty());
    let kinds: Vec<&UpdateType> = entries.iter().map(|(_, k, _)| k).collect();
    assert!(kinds.iter().any(|k| matches!(k, UpdateType::RepoInitializedUpdate)));
    assert!(
        kinds
            .iter()
            .any(|k| matches!(k, UpdateType::BranchCreatedUpdate { name } if name == "feature"))
    );
    assert!(
        kinds.iter().any(|k| matches!(k, UpdateType::TagCreatedUpdate { name } if name == "v1"))
    );
    Ok(())
}

#[tokio::test]
async fn inh_toc_ops_log__feature_flags_report_and_override_defaults() -> Res {
    let repo = memory_repo().await?;
    let flags: Vec<_> = repo.feature_flags().await?.collect();
    assert!(!flags.is_empty());
    let named: Vec<&str> = flags.iter().map(|f| f.name()).collect();
    assert!(named.contains(&"move_node"));
    assert!(flags.iter().all(|f| f.in_default_state()));

    repo.set_feature_flag("move_node", Some(true)).await?;
    let enabled: HashSet<String> =
        repo.enabled_feature_flags().await?.map(|f| f.name().to_string()).collect();
    assert!(enabled.contains("move_node"));
    let moved = repo
        .feature_flags()
        .await?
        .find(|f| f.name() == "move_node")
        .expect("move_node flag");
    assert_eq!(moved.setting(), Some(true));
    assert!(!moved.in_default_state());
    Ok(())
}

#[tokio::test]
async fn inh_toc_ops_log__ancestry_is_served_from_the_table_of_contents() -> Res {
    let repo = memory_repo().await?;
    let a = commit_group(&repo, "main", "/").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let history = ancestry_of(&repo, &VersionInfo::BranchTipRef("main".into())).await?;
    let ids: Vec<SnapshotId> = history.iter().map(|i| i.id.clone()).collect();
    assert_eq!(ids[0], b);
    assert_eq!(ids[1], a);
    assert_eq!(history[0].parent_id.as_ref(), Some(&a));
    // The initial snapshot closes the chain.
    assert!(history.last().unwrap().is_initial());
    Ok(())
}
