// Verifier-owned conformance driver for milestone 07 of
// earth-mover/icechunk — the release that ships the asynchronous API
// (icechunk-python 1.1.0) and, in the Rust crate, the virtual-chunk-container
// prefix-exactness flip plus the migrations that rode along with it.
//
// Every assertion here traces to /app/TEST_INTERFACE.md, which states the
// public Rust surface this milestone must expose, or to /app/icechunk-spec/
// (the pinned Icechunk documentation corpus plus PROFILE.md, which records how
// the corpus and this milestone's contract line up).
//
// Group = the `<group>__` prefix of the test function name. The runner invokes
// one filtered `cargo test` per group and asserts the executed count, so every
// group is independent and one group's failure never suppresses another.
//
// Tiers:
//   n..  this milestone's OWN new capability. Each of these groups must fail
//        against the previous checkpoint's workspace.
//   i..  inherited-regression coverage for milestones 01-06, expressed in the
//        surface THIS release publishes.
//
// Nothing here reaches the network, a cloud endpoint, MinIO, or a credential
// from the environment, and nothing sleeps on a wall clock. Repository storage
// is `icechunk::new_in_memory_storage()` or `new_local_filesystem_storage()`
// over a `tempfile::TempDir`; virtual chunk references address `file://` URLs
// inside a `TempDir`. The only process the drivers start is the candidate's own
// `icechunk` binary, whose path arrives in `ICECHUNK_TEST_CLI_BIN`.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]
#![allow(non_snake_case)]

use std::{
    collections::{HashMap, HashSet},
    num::{NonZeroU16, NonZeroUsize},
    path::Path as FsPath,
    sync::Arc,
};

use bytes::Bytes;
use chrono::Utc;
use futures::{StreamExt, TryStreamExt};
use tokio::sync::RwLock;

use icechunk::{
    ObjectStoreConfig, Repository, RepositoryConfig, Storage, Store,
    asset_manager::AssetManager,
    config::{CachingConfig, Credentials, ManifestSplittingConfig, S3Options},
    conflicts::{ConflictSolver, basic_solver::BasicConflictSolver, detector::ConflictDetector},
    format::{
        ByteRange, ChunkIndices, Path, SnapshotId,
        format_constants::{FileTypeBin, SpecVersionBin},
        manifest::{
            ChunkPayload, VirtualChunkLocation, VirtualChunkRef, VirtualReferenceError,
            VirtualReferenceErrorKind,
        },
        snapshot::{ArrayShape, NodeType, Snapshot},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::{
        gc::{Action, GCConfig, garbage_collect},
        pointed_snapshots,
        stats::repo_chunks_storage,
    },
    refs::{Ref, update_branch},
    repository::VersionInfo,
    session::{Session, get_chunk},
    store::{StoreError, StoreErrorKind},
    virtual_chunks::{VirtualChunkContainer, VirtualChunkResolver},
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// An anonymous S3 object-store configuration. Never contacted: every driver
/// that uses it stops at container construction or at prefix matching.
fn s3_store() -> ObjectStoreConfig {
    ObjectStoreConfig::S3(S3Options {
        region: Some("us-east-1".to_string()),
        endpoint_url: None,
        anonymous: true,
        allow_http: false,
        force_path_style: false,
    })
}

fn local_store(path: &str) -> ObjectStoreConfig {
    ObjectStoreConfig::LocalFileSystem(path.into())
}

/// A container built the only other way an external caller can obtain one:
/// deserialisation of a persisted repository configuration. This is how a
/// prefix that does not end in `/` reaches `VirtualChunkResolver::new` even
/// though `VirtualChunkContainer::new` now refuses to build one.
fn persisted_container(url_prefix: &str, local_path: &str) -> VirtualChunkContainer {
    serde_json::from_value(serde_json::json!({
        "url_prefix": url_prefix,
        "store": { "local_file_system": local_path },
    }))
    .expect("VirtualChunkContainer must deserialise from a persisted configuration")
}

/// The Zarr group metadata document a client writes for the root group.
const GROUP_META: &[u8] = br#"{"zarr_format":3,"node_type":"group"}"#;

/// A Zarr array metadata document. Icechunk stores the bytes verbatim, so this
/// is exactly what reads back.
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","attributes":{"foo":42},"shape":[4],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"bytes","configuration":{"endian":"little"}}]}"#;

/// An empty in-memory repository.
async fn mem_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    Ok(Repository::create(None, new_in_memory_storage().await?, HashMap::new()).await?)
}

/// An in-memory repository whose configuration is `config`.
async fn mem_repo_with(
    config: RepositoryConfig,
    creds: HashMap<String, Option<Credentials>>,
) -> Result<Repository, Box<dyn std::error::Error>> {
    Ok(Repository::create(Some(config), new_in_memory_storage().await?, creds).await?)
}

/// Adds `/array` with `n` chunks of `bytes` to a writable session.
async fn seed_array(
    session: &mut Session,
    path: &Path,
    n: u32,
    bytes: &'static [u8],
) -> TestResult {
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            path.clone(),
            ArrayShape::new(vec![(n as u64, 1)]).unwrap(),
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..n {
        let payload = session.get_chunk_writer()(Bytes::from_static(bytes)).await?;
        session.set_chunk_ref(path.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    Ok(())
}

/// A configuration whose inline threshold is zero, so every chunk is written as
/// its own object. Chunk-level accounting and garbage collection only see
/// materialised chunks, so every fixture that measures them uses this.
fn materialising() -> RepositoryConfig {
    RepositoryConfig { inline_chunk_threshold_bytes: Some(0), ..Default::default() }
}

/// An age threshold comfortably later than every object these fixtures write,
/// so "older than this" never depends on clock resolution. Moving the threshold
/// later is always safe: reachable objects are protected by reachability, never
/// by age.
fn after_everything() -> chrono::DateTime<Utc> {
    Utc::now() + chrono::Duration::seconds(60)
}

/// The GC tuning triple this release made mandatory, at values that comfortably
/// exceed every fixture here, so the knobs never change what is reported.
fn gc_knobs() -> (NonZeroU16, NonZeroUsize, NonZeroU16) {
    (
        NonZeroU16::new(50).unwrap(),
        NonZeroUsize::new(512 * 1024 * 1024).unwrap(),
        NonZeroU16::new(100).unwrap(),
    )
}

// ===========================================================================
// n01 — a virtual chunk container's url_prefix must end in a separator
// ===========================================================================

#[tokio::test]
async fn n01_container_prefix_rule__prefix_without_separator_is_rejected() -> TestResult {
    for prefix in ["s3://mybucket", "s3://mybucket/data", "s3://mybucket/a/b"] {
        let err = VirtualChunkContainer::new(prefix.to_string(), s3_store())
            .expect_err(&format!("{prefix} must be rejected"));
        assert!(
            err.contains("must end in a / character"),
            "unexpected rejection for {prefix}: {err}"
        );
    }

    let err = VirtualChunkContainer::new("file:///data/foo".to_string(), local_store("/data/foo"))
        .expect_err("file:///data/foo must be rejected");
    assert!(err.contains("must end in a / character"), "unexpected: {err}");
    Ok(())
}

#[tokio::test]
async fn n01_container_prefix_rule__separated_prefix_is_accepted() -> TestResult {
    let c = VirtualChunkContainer::new("s3://mybucket/data/".to_string(), s3_store())
        .expect("a separated s3 prefix is valid");
    assert_eq!(c.url_prefix(), "s3://mybucket/data/");

    let c = VirtualChunkContainer::new("s3://mybucket/".to_string(), s3_store())
        .expect("a separated bucket root is valid");
    assert_eq!(c.url_prefix(), "s3://mybucket/");

    let c = VirtualChunkContainer::new(
        "file:///data/foo/".to_string(),
        local_store("/data/foo"),
    )
    .expect("a separated file prefix is valid");
    assert_eq!(c.url_prefix(), "file:///data/foo/");
    Ok(())
}

#[tokio::test]
async fn n01_container_prefix_rule__separator_is_checked_before_the_url_is_parsed()
-> TestResult {
    // The separator rule is the constructor's first check, so a string that is
    // neither a URL nor separated is reported as unseparated.
    let err = VirtualChunkContainer::new("not a url".to_string(), s3_store())
        .expect_err("unparseable and unseparated");
    assert!(err.contains("must end in a / character"), "unexpected: {err}");

    // Separated but still unparseable: a different, pre-existing rejection.
    let err = VirtualChunkContainer::new("not a url/".to_string(), s3_store())
        .expect_err("unparseable");
    assert!(
        !err.contains("must end in a / character"),
        "a parse failure must not be reported as a missing separator: {err}"
    );
    Ok(())
}

#[tokio::test]
async fn n01_container_prefix_rule__earlier_validations_still_hold() -> TestResult {
    // Every one of these is separated, so it reaches the checks this milestone
    // did not change.
    assert!(
        VirtualChunkContainer::new("s3://".to_string(), s3_store()).is_err(),
        "an s3 prefix with no host is invalid"
    );
    assert!(
        VirtualChunkContainer::new("file:///".to_string(), local_store("/")).is_err(),
        "a file prefix with no path is invalid"
    );
    assert!(
        VirtualChunkContainer::new("s3://bucket/".to_string(), ObjectStoreConfig::InMemory)
            .is_err(),
        "an in-memory store cannot back an s3:// prefix"
    );
    assert!(
        VirtualChunkContainer::new("gcs://bucket/".to_string(), s3_store()).is_err(),
        "the prefix scheme must match the object store configuration"
    );
    // TEST_INTERFACE.md §1.1: `..` segments are collapsed while the url is
    // parsed, so this one arrives as `file:///` and is refused as a path no
    // longer than `/` -- not as a missing separator.
    let err = VirtualChunkContainer::new("file:///data/../".to_string(), local_store("/data"))
        .expect_err("a file prefix that collapses to the root is invalid");
    assert!(
        !err.contains("must end in a / character"),
        "a collapsed path must not be reported as a missing separator: {err}"
    );
    Ok(())
}

// ===========================================================================
// n02 — prefix matching is exact: a sibling prefix is not covered
// ===========================================================================

#[tokio::test]
async fn n02_prefix_exactness__sibling_prefix_is_not_covered() -> TestResult {
    let cont =
        VirtualChunkContainer::new("file:///data/foo/".to_string(), local_store("/data/foo"))?;
    let resolver =
        VirtualChunkResolver::new([cont].into_iter(), HashMap::new(), Default::default());

    assert!(
        resolver.matching_container("file:///data/foo/chunk-1").is_some(),
        "the authorised prefix must still be covered"
    );
    assert!(
        resolver.matching_container("file:///data/foo-production/chunk-1").is_none(),
        "a sibling that merely shares a textual prefix must not be covered"
    );
    assert!(
        resolver.matching_container("file:///data/foobar/chunk-1").is_none(),
        "a sibling that merely shares a textual prefix must not be covered"
    );

    // Two overlapping containers: matching takes the LONGEST prefix first,
    // whatever order they were declared in.
    let outer = VirtualChunkContainer::new(
        "file:///data/foo/".to_string(),
        local_store("/data/foo"),
    )?;
    let inner = VirtualChunkContainer::new(
        "file:///data/foo/bar/".to_string(),
        local_store("/data/foo/bar"),
    )?;
    let resolver = VirtualChunkResolver::new(
        [outer, inner].into_iter(),
        HashMap::new(),
        Default::default(),
    );
    assert_eq!(
        resolver.matching_container("file:///data/foo/bar/chunk-1").map(|c| c.url_prefix()),
        Some("file:///data/foo/bar/"),
        "the longest matching prefix wins"
    );
    assert_eq!(
        resolver.matching_container("file:///data/foo/other/chunk-1").map(|c| c.url_prefix()),
        Some("file:///data/foo/"),
        "a location the longer container does not cover falls back to the shorter"
    );
    Ok(())
}

#[tokio::test]
async fn n02_prefix_exactness__an_invalid_container_is_dropped() -> TestResult {
    // A persisted configuration can also carry a container that is STILL
    // invalid once the separator has been appended - here an `s3://` prefix
    // backed by a local-filesystem store. The resolver drops it silently: it
    // is still constructed, the valid container beside it still matches, and
    // nothing matches the dropped prefix.
    let broken = persisted_container("s3://bucket/data", "/data/foo");
    let good = VirtualChunkContainer::new(
        "file:///data/foo/".to_string(),
        local_store("/data/foo"),
    )?;
    let resolver = VirtualChunkResolver::new(
        [broken, good].into_iter(),
        HashMap::new(),
        Default::default(),
    );

    assert!(
        resolver.matching_container("s3://bucket/data/chunk-1").is_none(),
        "a container still invalid after normalisation must be dropped, not kept"
    );
    assert_eq!(
        resolver.matching_container("file:///data/foo/chunk-1").map(|c| c.url_prefix()),
        Some("file:///data/foo/"),
        "the valid container beside it is unaffected"
    );
    Ok(())
}

#[tokio::test]
async fn n02_prefix_exactness__persisted_prefix_is_normalised() -> TestResult {
    // A configuration persisted before this release - or edited by hand - can
    // still carry an unseparated prefix. The resolver normalises it instead of
    // honouring it verbatim, which is what makes the sibling safe.
    let cont = persisted_container("file:///data/foo", "/data/foo");
    assert_eq!(cont.url_prefix(), "file:///data/foo", "fixture sanity");

    let resolver =
        VirtualChunkResolver::new([cont].into_iter(), HashMap::new(), Default::default());

    let matched = resolver
        .matching_container("file:///data/foo/chunk-1")
        .expect("the intended prefix must still be covered");
    assert_eq!(
        matched.url_prefix(),
        "file:///data/foo/",
        "the resolver must normalise the persisted prefix"
    );
    assert!(
        resolver.matching_container("file:///data/foo-production/chunk-1").is_none(),
        "normalisation must remove the sibling from the container's reach"
    );
    Ok(())
}

#[tokio::test]
async fn n02_prefix_exactness__unmatched_location_fails_resolution() -> TestResult {
    let cont = persisted_container("file:///data/foo", "/data/foo");
    let creds: HashMap<String, Option<Credentials>> =
        [("file:///data/foo".to_string(), None)].into_iter().collect();
    let resolver = VirtualChunkResolver::new([cont].into_iter(), creds, Default::default());

    let sibling = "file:///data/foo-production/chunk-1";
    let res = resolver.fetch_chunk(sibling, &(0..8), None).await;
    match res {
        Err(VirtualReferenceError {
            kind: VirtualReferenceErrorKind::NoContainerForUrl(url),
            ..
        }) => assert_eq!(url, sibling),
        other => panic!("expected NoContainerForUrl for the sibling, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn n02_prefix_exactness__store_rejects_sibling_when_validating() -> TestResult {
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(VirtualChunkContainer::new(
        "file:///data/foo/".to_string(),
        local_store("/data/foo"),
    )?);
    let creds: HashMap<String, Option<Credentials>> =
        [("file:///data/foo/".to_string(), None)].into_iter().collect();
    let repo = mem_repo_with(config, creds).await?;

    let mut session = repo.writable_session("main").await?;
    let path: Path = "/array".try_into()?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(path.clone(), ArrayShape::new(vec![(2, 1)]).unwrap(), None, Bytes::new())
        .await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    let sibling = VirtualChunkLocation::from_absolute_path("file:///data/foo-production/c1")?;
    let reference = VirtualChunkRef {
        location: sibling.clone(),
        offset: 0,
        length: 4,
        checksum: None,
    };
    match store.set_virtual_ref("array/c/0", reference.clone(), true).await {
        Err(StoreError {
            kind: StoreErrorKind::InvalidVirtualChunkContainer { chunk_location },
            ..
        }) => assert_eq!(
            chunk_location,
            sibling.url(),
            "the rejection must name the location it refused"
        ),
        other => panic!("expected InvalidVirtualChunkContainer, got {other:?}"),
    }

    // The intended prefix is still covered by the same container.
    let inside = VirtualChunkLocation::from_absolute_path("file:///data/foo/c1")?;
    store
        .set_virtual_ref(
            "array/c/1",
            VirtualChunkRef { location: inside, offset: 0, length: 4, checksum: None },
            true,
        )
        .await
        .expect("a location inside the authorised prefix must be accepted");

    // With validation switched off the same sibling reference is recorded.
    store
        .set_virtual_ref("array/c/0", reference, false)
        .await
        .expect("validation off must accept an unmatched location");
    Ok(())
}

// ===========================================================================
// n03 — credential-map keys are normalised the same way
// ===========================================================================

#[tokio::test]
async fn n03_credential_prefix__unauthorised_container_reports_separated_prefix()
-> TestResult {
    let cont =
        VirtualChunkContainer::new("file:///example/".to_string(), local_store("/example"))?;
    let resolver =
        VirtualChunkResolver::new([cont].into_iter(), HashMap::new(), Default::default());

    match resolver.fetch_chunk("file:///example/foo.nc", &(0..8), None).await {
        Err(VirtualReferenceError {
            kind: VirtualReferenceErrorKind::UnauthorizedVirtualChunkContainer(c),
            ..
        }) => assert_eq!(c.url_prefix(), "file:///example/"),
        other => panic!("expected UnauthorizedVirtualChunkContainer, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn n03_credential_prefix__unseparated_key_still_authorises() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let file = dir.path().join("chunk.bin");
    std::fs::write(&file, b"0123456789abcdef")?;
    let prefix = format!("file://{}/", dir.path().display());

    let cont = VirtualChunkContainer::new(
        prefix.clone(),
        local_store(dir.path().to_str().unwrap()),
    )?;
    // An authorisation map written before this release keys the container by
    // its unseparated prefix. It must keep working.
    let key = prefix.trim_end_matches('/').to_string();
    let creds: HashMap<String, Option<Credentials>> =
        [(key, None)].into_iter().collect();
    let resolver = VirtualChunkResolver::new([cont].into_iter(), creds, Default::default());

    let url = format!("file://{}", file.display());
    let bytes = resolver.fetch_chunk(&url, &(0..4), None).await?;
    assert_eq!(bytes, Bytes::from_static(b"0123"));
    Ok(())
}

#[tokio::test]
async fn n03_credential_prefix__separated_key_authorises() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let file = dir.path().join("chunk.bin");
    std::fs::write(&file, b"0123456789abcdef")?;
    let prefix = format!("file://{}/", dir.path().display());

    let cont = VirtualChunkContainer::new(
        prefix.clone(),
        local_store(dir.path().to_str().unwrap()),
    )?;
    let creds: HashMap<String, Option<Credentials>> =
        [(prefix, None)].into_iter().collect();
    let resolver = VirtualChunkResolver::new([cont].into_iter(), creds, Default::default());

    let url = format!("file://{}", file.display());
    let bytes = resolver.fetch_chunk(&url, &(6..10), None).await?;
    assert_eq!(bytes, Bytes::from_static(b"6789"));
    Ok(())
}

// ===========================================================================
// n04 — the raised default asset cache sizes
// ===========================================================================

#[tokio::test]
async fn n04_cache_defaults__caching_config_defaults() -> TestResult {
    let c = CachingConfig::default();
    assert_eq!(c.num_snapshot_nodes(), 30_000);
    assert_eq!(c.num_chunk_refs(), 15_000_000);
    Ok(())
}

#[tokio::test]
async fn n04_cache_defaults__unchanged_defaults_stay_put() -> TestResult {
    let c = CachingConfig::default();
    assert_eq!(c.num_transaction_changes(), 0);
    assert_eq!(c.num_bytes_attributes(), 0);
    assert_eq!(c.num_bytes_chunks(), 0);
    // The other configuration default this release leaves alone: TEST_INTERFACE
    // §1.6's closing sentence, §3 M24.
    assert_eq!(icechunk::config::CompressionConfig::default().level(), 3);
    Ok(())
}

#[tokio::test]
async fn n04_cache_defaults__an_unconfigured_repository_gets_them() -> TestResult {
    let repo = mem_repo().await?;
    let caching = repo.config().caching();
    assert_eq!(caching.num_snapshot_nodes(), 30_000);
    assert_eq!(caching.num_chunk_refs(), 15_000_000);
    Ok(())
}

#[tokio::test]
async fn n04_cache_defaults__explicit_settings_still_win() -> TestResult {
    let caching = CachingConfig {
        num_chunk_refs: Some(7),
        num_snapshot_nodes: Some(0),
        ..Default::default()
    };
    assert_eq!(caching.num_chunk_refs(), 7);
    assert_eq!(caching.num_snapshot_nodes(), 0);

    let config = RepositoryConfig { caching: Some(caching), ..Default::default() };
    let repo = mem_repo_with(config, HashMap::new()).await?;
    assert_eq!(repo.config().caching().num_chunk_refs(), 7);

    // A tiny cache is advisory only: the data that comes back is still correct.
    let mut session = repo.writable_session("main").await?;
    let path: Path = "/array".try_into()?;
    seed_array(&mut session, &path, 4, b"abcdefgh").await?;
    session.commit("seed", None).await?;

    let session = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let got = get_chunk(
        session.get_chunk_reader(&path, &ChunkIndices(vec![2]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"abcdefgh"));
    Ok(())
}

// ===========================================================================
// n05 — VirtualChunkLocation is opaque and CannotParseUrl carries its URL
// ===========================================================================

#[tokio::test]
async fn n05_virtual_location_url__url_accessor_round_trips() -> TestResult {
    let loc = VirtualChunkLocation::from_absolute_path("s3://foo/bar/7")?;
    assert_eq!(loc.url(), "s3://foo/bar/7");
    let loc = VirtualChunkLocation::from_absolute_path("file:///data/foo/c/0/0")?;
    assert_eq!(loc.url(), "file:///data/foo/c/0/0");
    Ok(())
}

#[tokio::test]
async fn n05_virtual_location_url__cannot_parse_url_is_a_struct_variant() -> TestResult {
    let err = VirtualChunkLocation::from_absolute_path("not a url")
        .expect_err("an unparseable location must be rejected");
    match &err {
        VirtualReferenceError {
            kind: VirtualReferenceErrorKind::CannotParseUrl { url, cause },
            ..
        } => {
            assert_eq!(url, "not a url");
            // `cause` is the underlying parse failure, kept as a source.
            assert!(!cause.to_string().is_empty());
        }
        other => panic!("expected CannotParseUrl, got {other:?}"),
    }
    assert!(
        format!("{err}").contains("not a url"),
        "the display form must name the URL it could not parse: {err}"
    );
    Ok(())
}

#[tokio::test]
async fn n05_virtual_location_url__locations_are_readable_through_the_session()
-> TestResult {
    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(VirtualChunkContainer::new(
        "file:///data/foo/".to_string(),
        local_store("/data/foo"),
    )?);
    let creds: HashMap<String, Option<Credentials>> =
        [("file:///data/foo/".to_string(), None)].into_iter().collect();
    let repo = mem_repo_with(config, creds).await?;

    let mut session = repo.writable_session("main").await?;
    let path: Path = "/array".try_into()?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(path.clone(), ArrayShape::new(vec![(2, 1)]).unwrap(), None, Bytes::new())
        .await?;
    let loc = VirtualChunkLocation::from_absolute_path("file:///data/foo/c1")?;
    session
        .set_chunk_ref(
            path.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: loc.clone(),
                offset: 0,
                length: 4,
                checksum: None,
            })),
        )
        .await?;

    let locations: Vec<String> =
        session.all_virtual_chunk_locations().await?.try_collect().await?;
    assert_eq!(locations, vec![loc.url().to_string()]);

    // The same location is what `matching_container` is asked about.
    assert!(session.matching_container(&loc).is_some());
    Ok(())
}

#[tokio::test]
async fn n05_virtual_location_url__unmatched_location_is_reported_by_url() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    let path: Path = "/array".try_into()?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(path.clone(), ArrayShape::new(vec![(1, 1)]).unwrap(), None, Bytes::new())
        .await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    let loc = VirtualChunkLocation::from_absolute_path("s3://nowhere/c1")?;
    match store
        .set_virtual_ref(
            "array/c/0",
            VirtualChunkRef { location: loc.clone(), offset: 0, length: 4, checksum: None },
            true,
        )
        .await
    {
        Err(StoreError {
            kind: StoreErrorKind::InvalidVirtualChunkContainer { chunk_location },
            ..
        }) => assert_eq!(chunk_location, loc.url()),
        other => panic!("expected InvalidVirtualChunkContainer, got {other:?}"),
    }
    Ok(())
}

// ===========================================================================
// n06 — a conflict solver reference must be Send + Sync
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn n06_solver_send_sync__rebase_runs_on_a_spawned_task() -> TestResult {
    let repo = Arc::new(mem_repo().await?);
    let path: Path = "/arr".try_into()?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(path.clone(), ArrayShape::new(vec![(4, 1)]).unwrap(), None, Bytes::new())
        .await?;
    base.commit("base", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    let p = a.get_chunk_writer()(Bytes::from_static(b"aaaa")).await?;
    a.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    a.commit("a", None).await?;

    let p = b.get_chunk_writer()(Bytes::from_static(b"bbbb")).await?;
    b.set_chunk_ref(path.clone(), ChunkIndices(vec![1]), Some(p)).await?;
    assert!(b.commit("b", None).await.is_err(), "b must lose the race");

    // The solver reference must be `Send + Sync`, so the whole rebase future is
    // `Send` and can be moved onto another runtime worker.
    let solver: Arc<dyn ConflictSolver + Send + Sync> =
        Arc::new(BasicConflictSolver::default());
    let handle = tokio::spawn(async move {
        b.rebase(solver.as_ref()).await?;
        b.commit("b after rebase", None).await
    });
    let snap = handle.await??;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n06_solver_send_sync__commit_rebasing_runs_on_a_spawned_task() -> TestResult {
    let repo = Arc::new(mem_repo().await?);
    let path: Path = "/arr".try_into()?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(path.clone(), ArrayShape::new(vec![(4, 1)]).unwrap(), None, Bytes::new())
        .await?;
    base.commit("base", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    let p = a.get_chunk_writer()(Bytes::from_static(b"aaaa")).await?;
    a.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    a.commit("a", None).await?;
    let p = b.get_chunk_writer()(Bytes::from_static(b"bbbb")).await?;
    b.set_chunk_ref(path.clone(), ChunkIndices(vec![1]), Some(p)).await?;

    let solver: Arc<dyn ConflictSolver + Send + Sync> =
        Arc::new(BasicConflictSolver::default());
    let handle = tokio::spawn(async move {
        b.commit_rebasing(solver.as_ref(), 5, "b rebasing", None, |_| async {}, |_| async {})
            .await
    });
    let snap = handle.await??;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n06_solver_send_sync__detector_is_accepted_as_a_shared_solver() -> TestResult {
    let repo = Arc::new(mem_repo().await?);
    let path: Path = "/arr".try_into()?;

    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(path.clone(), ArrayShape::new(vec![(4, 1)]).unwrap(), None, Bytes::new())
        .await?;
    base.commit("base", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    let p = a.get_chunk_writer()(Bytes::from_static(b"aaaa")).await?;
    a.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    a.commit("a", None).await?;
    let p = b.get_chunk_writer()(Bytes::from_static(b"bbbb")).await?;
    b.set_chunk_ref(path.clone(), ChunkIndices(vec![1]), Some(p)).await?;

    // A bare `ConflictDetector` patches only non-conflicting changes; here
    // there is no conflict, so the rebase succeeds through a spawned task too.
    let solver: Arc<dyn ConflictSolver + Send + Sync> = Arc::new(ConflictDetector);
    let handle = tokio::spawn(async move {
        b.rebase(solver.as_ref()).await?;
        b.commit("b after detector", None).await
    });
    let snap = handle.await??;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

// ===========================================================================
// n07 — garbage collection: dry run plus the mandatory tuning parameters
// ===========================================================================

async fn gc_fixture() -> Result<
    (Arc<dyn Storage>, icechunk::storage::Settings, Repository, usize),
    Box<dyn std::error::Error>,
> {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    // Materialise every chunk: an inlined chunk is part of its manifest and
    // therefore has nothing of its own for garbage collection to account for.
    let repo = Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 12, b"keepkeep").await?;
    let first = session.commit("first", None).await?;

    let mut session = repo.writable_session("main").await?;
    for i in 0..6u32 {
        let p = session.get_chunk_writer()(Bytes::from_static(b"dropdrop")).await?;
        session.set_chunk_ref(path.clone(), ChunkIndices(vec![i]), Some(p)).await?;
    }
    let second = session.commit("second", None).await?;
    let before = storage.list_chunks(&settings).await?.count().await;

    // Strand the second commit so its chunks become unreachable.
    update_branch(storage.as_ref(), &settings, "main", first, Some(&second)).await?;
    Ok((storage, settings, repo, before))
}

#[tokio::test(flavor = "multi_thread")]
async fn n07_gc_dry_run__dry_run_reports_without_deleting() -> TestResult {
    let (storage, settings, repo, before) = gc_fixture().await?;
    let (snaps, mem, fetches) = gc_knobs();
    let config = GCConfig::clean_all(after_everything(), after_everything(), None, snaps, mem, fetches, true);

    let summary =
        garbage_collect(storage.as_ref(), &settings, repo.asset_manager().clone(), &config)
            .await?;
    assert!(summary.chunks_deleted > 0, "a dry run must report the work it would do");
    assert!(summary.bytes_deleted > 0, "a dry run must report the bytes it would free");
    assert_eq!(
        storage.list_chunks(&settings).await?.count().await,
        before,
        "a dry run must not remove anything"
    );
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n07_gc_dry_run__a_real_run_removes_what_the_dry_run_reported() -> TestResult {
    let (storage, settings, repo, before) = gc_fixture().await?;
    let (snaps, mem, fetches) = gc_knobs();

    let dry = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(after_everything(), after_everything(), None, snaps, mem, fetches, true),
    )
    .await?;
    let real = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(after_everything(), after_everything(), None, snaps, mem, fetches, false),
    )
    .await?;

    assert_eq!(real.chunks_deleted, dry.chunks_deleted);
    assert_eq!(real.bytes_deleted, dry.bytes_deleted);
    let after = storage.list_chunks(&settings).await?.count().await;
    assert!(after < before, "a real run must remove the unreachable chunks");
    assert_eq!(
        after as u64,
        before as u64 - real.chunks_deleted,
        "a real run removes exactly what it counts"
    );

    // Everything the surviving branch reaches still reads back.
    let path: Path = "/array".try_into()?;
    let session = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    for i in [0u32, 5, 11] {
        let got = get_chunk(
            session.get_chunk_reader(&path, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?;
        assert_eq!(got.unwrap(), Bytes::from_static(b"keepkeep"), "chunk {i}");
    }
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n07_gc_dry_run__the_tuning_knobs_do_not_change_the_result() -> TestResult {
    let (storage, settings, repo, _) = gc_fixture().await?;

    let tight = GCConfig::clean_all(
        after_everything(),
        after_everything(),
        None,
        NonZeroU16::new(1).unwrap(),
        NonZeroUsize::MIN,
        NonZeroU16::new(1).unwrap(),
        true,
    );
    let loose = GCConfig::clean_all(
        after_everything(),
        after_everything(),
        None,
        NonZeroU16::MAX,
        NonZeroUsize::MAX,
        NonZeroU16::MAX,
        true,
    );

    let a =
        garbage_collect(storage.as_ref(), &settings, repo.asset_manager().clone(), &tight)
            .await?;
    let b =
        garbage_collect(storage.as_ref(), &settings, repo.asset_manager().clone(), &loose)
            .await?;
    assert_eq!(a, b, "the parallelisation knobs change throughput, not results");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n07_gc_dry_run__gc_config_new_takes_the_same_knobs() -> TestResult {
    let (storage, settings, repo, before) = gc_fixture().await?;
    let (snaps, mem, fetches) = gc_knobs();

    let chunks_only = GCConfig::new(
        HashSet::new(),
        Action::DeleteIfCreatedBefore(after_everything()),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        snaps,
        mem,
        fetches,
        true,
    );
    assert!(chunks_only.deletes_chunks());
    assert!(!chunks_only.deletes_manifests());
    assert!(!chunks_only.deletes_snapshots());
    assert!(!chunks_only.deletes_attributes());
    assert!(!chunks_only.deletes_transaction_logs());

    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &chunks_only,
    )
    .await?;
    assert!(summary.chunks_deleted > 0);
    assert_eq!(summary.manifests_deleted, 0);
    assert_eq!(summary.snapshots_deleted, 0);
    assert_eq!(
        storage.list_chunks(&settings).await?.count().await,
        before,
        "dry_run applies to GCConfig::new as well"
    );
    Ok(())
}

// ===========================================================================
// n08 — repo_chunks_storage and its memory/concurrency knobs
// ===========================================================================

#[tokio::test(flavor = "multi_thread")]
async fn n08_chunk_storage_knobs__accounting_is_knob_invariant() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo =
        Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 40, b"0123456789").await?;
    session.commit("first", None).await?;

    let tight = repo_chunks_storage(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        NonZeroU16::new(1).unwrap(),
        NonZeroUsize::MIN,
        NonZeroU16::new(1).unwrap(),
    )
    .await?;
    let loose = repo_chunks_storage(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        NonZeroU16::MAX,
        NonZeroUsize::MAX,
        NonZeroU16::MAX,
    )
    .await?;
    assert_eq!(tight, loose, "the knobs change throughput, not the accounting");
    assert_eq!(tight, 40 * 10, "every distinct chunk is counted exactly once");
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n08_chunk_storage_knobs__concurrent_runs_agree() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo = Arc::new(
        Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?,
    );
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 25, b"abcdef").await?;
    session.commit("first", None).await?;

    let run = |mem: NonZeroUsize, fetches: u16| {
        let (st, se, am) =
            (storage.clone(), settings.clone(), repo.asset_manager().clone());
        async move {
            repo_chunks_storage(
                st.as_ref(),
                &se,
                am,
                NonZeroU16::new(4).unwrap(),
                mem,
                NonZeroU16::new(fetches).unwrap(),
            )
            .await
        }
    };

    let serial = run(NonZeroUsize::MAX, 8).await?;
    let (a, b) = tokio::join!(run(NonZeroUsize::MAX, 64), run(NonZeroUsize::MIN, 1));
    assert_eq!((a?, b?), (serial, serial));
    assert_eq!(serial, 25 * 6);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n08_chunk_storage_knobs__shared_chunks_are_counted_once() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo =
        Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 10, b"012345").await?;
    let first = session.commit("first", None).await?;
    // A tag and a branch over the same ancestry: the traversal must not
    // double-count anything they share.
    repo.create_tag("t1", &first).await?;
    repo.create_branch("other", &first).await?;

    let (snaps, mem, fetches) = gc_knobs();
    let total = repo_chunks_storage(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        snaps,
        mem,
        fetches,
    )
    .await?;
    assert_eq!(total, 10 * 6);
    Ok(())
}

#[tokio::test(flavor = "multi_thread")]
async fn n08_chunk_storage_knobs__inline_chunks_are_not_accounted() -> TestResult {
    // The same workload as the knob-invariance case, but with an inline
    // threshold above the chunk size, so every chunk lives inside its manifest
    // instead of becoming an object. TEST_INTERFACE.md §1.7 (and PROFILE.md
    // R5-2): an inlined chunk contributes nothing to `repo_chunks_storage` and
    // nothing to `GCSummary::chunks_deleted`.
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let inlining = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(64),
        ..Default::default()
    };
    let repo = Repository::create(Some(inlining), storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 40, b"0123456789").await?;
    let first = session.commit("first", None).await?;

    // Nothing was materialised, so there is no chunk object at all...
    assert_eq!(
        storage.list_chunks(&settings).await?.count().await,
        0,
        "a payload at or below the threshold is stored inside its manifest"
    );
    let (snaps, mem, fetches) = gc_knobs();
    let total = repo_chunks_storage(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        snaps,
        mem,
        fetches,
    )
    .await?;
    assert_eq!(total, 0, "an inlined chunk is not a chunk object and is not accounted");

    // ... and stranding the commit that wrote them leaves garbage collection
    // nothing of its own to delete either.
    let mut session = repo.writable_session("main").await?;
    for i in 0..6u32 {
        let p = session.get_chunk_writer()(Bytes::from_static(b"dropdrop")).await?;
        session.set_chunk_ref(path.clone(), ChunkIndices(vec![i]), Some(p)).await?;
    }
    let second = session.commit("second", None).await?;
    update_branch(storage.as_ref(), &settings, "main", first, Some(&second)).await?;

    let chunks_only = GCConfig::new(
        HashSet::new(),
        Action::DeleteIfCreatedBefore(after_everything()),
        Action::Keep,
        Action::Keep,
        Action::Keep,
        Action::Keep,
        snaps,
        mem,
        fetches,
        false,
    );
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &chunks_only,
    )
    .await?;
    assert_eq!(summary.chunks_deleted, 0, "there is no inlined chunk to collect");
    assert_eq!(summary.bytes_deleted, 0);
    Ok(())
}

// ===========================================================================
// n09 — the ancestry streams and the AssetManager rename
// ===========================================================================

async fn two_commits() -> Result<(Repository, SnapshotId), Box<dyn std::error::Error>> {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let _ = session.commit("c1", None).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group("/foo".try_into()?, Bytes::new()).await?;
    let head = session.commit("c2", None).await?;
    Ok((repo, head))
}

#[tokio::test]
async fn n09_ancestry_rename__snapshot_info_ancestry_yields_snapshot_info() -> TestResult {
    let (repo, head) = two_commits().await?;
    let am: Arc<AssetManager> = Arc::clone(repo.asset_manager());

    let infos: Vec<_> = am.snapshot_info_ancestry(&head).await?.try_collect().await?;
    assert_eq!(infos.len(), 3, "two commits plus the initial snapshot");
    assert_eq!(infos[0].id, head);
    assert_eq!(infos[0].message, "c2");
    assert_eq!(infos[1].message, "c1");
    assert!(infos[2].is_initial());
    assert_eq!(infos[0].parent_id.as_ref(), Some(&infos[1].id));
    Ok(())
}

#[tokio::test]
async fn n09_ancestry_rename__snapshot_ancestry_yields_snapshots() -> TestResult {
    let (repo, head) = two_commits().await?;
    let am: Arc<AssetManager> = Arc::clone(repo.asset_manager());

    let snaps: Vec<Arc<Snapshot>> =
        am.snapshot_ancestry(&head).await?.try_collect().await?;
    assert_eq!(snaps.len(), 3);
    assert_eq!(snaps[0].id(), head);
    assert_eq!(snaps[0].message(), "c2");
    // `Snapshot` is the flatbuffer-backed object, not a summary: it can be
    // walked node by node.
    assert!(snaps[0].len() >= 2, "root group plus /foo");
    assert_eq!(snaps[2].parent_id(), None);
    Ok(())
}

#[tokio::test]
async fn n09_ancestry_rename__the_two_streams_agree_on_identity() -> TestResult {
    let (repo, head) = two_commits().await?;
    let am: Arc<AssetManager> = Arc::clone(repo.asset_manager());

    let infos: Vec<_> =
        Arc::clone(&am).snapshot_info_ancestry(&head).await?.try_collect().await?;
    let snaps: Vec<Arc<Snapshot>> =
        am.snapshot_ancestry(&head).await?.try_collect().await?;
    let from_info: Vec<SnapshotId> = infos.iter().map(|i| i.id.clone()).collect();
    let from_snap: Vec<SnapshotId> = snaps.iter().map(|s| s.id()).collect();
    assert_eq!(from_info, from_snap);
    Ok(())
}

#[tokio::test]
async fn n09_ancestry_rename__repository_ancestry_still_yields_summaries() -> TestResult {
    let (repo, head) = two_commits().await?;
    let via_repo: Vec<_> = repo.snapshot_ancestry(&head).await?.try_collect().await?;
    assert_eq!(via_repo.len(), 3);
    assert_eq!(via_repo[0].id, head);
    assert_eq!(via_repo[0].message, "c2");

    let via_version: Vec<_> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(
        via_version.iter().map(|i| i.id.clone()).collect::<Vec<_>>(),
        via_repo.iter().map(|i| i.id.clone()).collect::<Vec<_>>()
    );

    // TEST_INTERFACE.md §1.8: `snapshot_ancestry_arc` and `ancestry_arc` keep
    // their signatures — `self: Arc<Self>` — and still yield `SnapshotInfo`,
    // unlike `AssetManager::snapshot_ancestry`, which was re-typed.
    let shared = Arc::new(repo);
    let via_arc: Vec<_> =
        Arc::clone(&shared).snapshot_ancestry_arc(&head).await?.try_collect().await?;
    assert_eq!(
        via_arc.iter().map(|i| i.id.clone()).collect::<Vec<_>>(),
        via_repo.iter().map(|i| i.id.clone()).collect::<Vec<_>>()
    );
    assert_eq!(via_arc[0].message, "c2");
    let via_arc_version: Vec<_> = shared
        .ancestry_arc(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect()
        .await?;
    assert_eq!(
        via_arc_version.iter().map(|i| i.id.clone()).collect::<Vec<_>>(),
        via_repo.iter().map(|i| i.id.clone()).collect::<Vec<_>>()
    );
    Ok(())
}

// ===========================================================================
// n10 — pointed_snapshots yields snapshots and visits each one once
// ===========================================================================

#[tokio::test]
async fn n10_pointed_snapshots__each_snapshot_is_yielded_once() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let repo = Repository::create(None, storage.clone(), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap1 = session.commit("c1", None).await?;
    repo.create_tag("tag1", &snap1).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group("/foo".try_into()?, Bytes::new()).await?;
    let snap2 = session.commit("c2", None).await?;
    repo.create_tag("tag2", &snap2).await?;

    // Three refs (main, tag1, tag2) over three snapshots.
    let all: Vec<Arc<Snapshot>> = pointed_snapshots(
        storage.as_ref(),
        &storage.default_settings(),
        repo.asset_manager().clone(),
        &HashSet::new(),
    )
    .await?
    .try_collect()
    .await?;

    assert_eq!(all.len(), 3, "shared ancestry must not be walked twice");
    let ids: HashSet<SnapshotId> = all.iter().map(|s| s.id()).collect();
    assert_eq!(ids.len(), 3);
    assert!(ids.contains(&snap1) && ids.contains(&snap2));

    // TEST_INTERFACE.md §1.8: `ops::all_roots` is unchanged — it still yields
    // the snapshot ids the repository's references point at, as ids, and it is
    // `pointed_snapshots` above that walks their ancestry and deduplicates.
    let roots: Vec<SnapshotId> = icechunk::ops::all_roots(
        storage.as_ref(),
        &storage.default_settings(),
        &HashSet::new(),
    )
    .await?
    .try_collect()
    .await?;
    let root_ids: HashSet<SnapshotId> = roots.into_iter().collect();
    assert_eq!(root_ids, HashSet::from([snap1.clone(), snap2.clone()]));
    Ok(())
}

#[tokio::test]
async fn n10_pointed_snapshots__extra_roots_are_included() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let repo = Repository::create(None, storage.clone(), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap1 = session.commit("c1", None).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group("/foo".try_into()?, Bytes::new()).await?;
    let snap2 = session.commit("c2", None).await?;

    // Reset main back to the first commit: snap2 is now unreachable from any
    // ref, and reappears only when named as an extra root.
    repo.reset_branch("main", &snap1).await?;
    let without: Vec<Arc<Snapshot>> = pointed_snapshots(
        storage.as_ref(),
        &storage.default_settings(),
        repo.asset_manager().clone(),
        &HashSet::new(),
    )
    .await?
    .try_collect()
    .await?;
    assert!(!without.iter().any(|s| s.id() == snap2));

    let roots: HashSet<SnapshotId> = [snap2.clone()].into_iter().collect();
    let with: Vec<Arc<Snapshot>> = pointed_snapshots(
        storage.as_ref(),
        &storage.default_settings(),
        repo.asset_manager().clone(),
        &roots,
    )
    .await?
    .try_collect()
    .await?;
    assert!(with.iter().any(|s| s.id() == snap2));
    assert_eq!(
        with.iter().map(|s| s.id()).collect::<HashSet<_>>().len(),
        with.len(),
        "extra roots must not reintroduce duplicates"
    );
    Ok(())
}

#[tokio::test]
async fn n10_pointed_snapshots__manifest_files_are_reachable_from_the_stream()
-> TestResult {
    let storage = new_in_memory_storage().await?;
    let repo = Repository::create(None, storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 6, b"abcd").await?;
    session.commit("c1", None).await?;

    let all: Vec<Arc<Snapshot>> = pointed_snapshots(
        storage.as_ref(),
        &storage.default_settings(),
        repo.asset_manager().clone(),
        &HashSet::new(),
    )
    .await?
    .try_collect()
    .await?;

    let manifests: Vec<_> =
        all.iter().flat_map(|s| s.manifest_files().collect::<Vec<_>>()).collect();
    assert_eq!(manifests.len(), 1);
    assert_eq!(manifests[0].num_chunk_refs, 6);
    assert!(manifests[0].size_bytes > 0);
    Ok(())
}

// ===========================================================================
// n11 — RefVersionInfo and resolve_ref_version
// ===========================================================================

#[tokio::test]
async fn n11_ref_version_info__the_three_ref_forms_resolve() -> TestResult {
    use icechunk::repository::RefVersionInfo;
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("c1", None).await?;
    repo.create_tag("v1", &snap).await?;

    assert_eq!(
        repo.resolve_ref_version(&RefVersionInfo::SnapshotId(snap.clone())).await?,
        snap
    );
    assert_eq!(
        repo.resolve_ref_version(&RefVersionInfo::TagRef("v1".to_string())).await?,
        snap
    );
    assert_eq!(
        repo.resolve_ref_version(&RefVersionInfo::BranchTipRef("main".to_string()))
            .await?,
        snap
    );
    Ok(())
}

#[tokio::test]
async fn n11_ref_version_info__unknown_refs_are_errors() -> TestResult {
    use icechunk::repository::RefVersionInfo;
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("c1", None).await?;

    assert!(
        repo.resolve_ref_version(&RefVersionInfo::TagRef("nope".to_string()))
            .await
            .is_err()
    );
    assert!(
        repo.resolve_ref_version(&RefVersionInfo::BranchTipRef("nope".to_string()))
            .await
            .is_err()
    );
    assert!(
        repo.resolve_ref_version(&RefVersionInfo::SnapshotId(SnapshotId::random()))
            .await
            .is_err()
    );

    // TEST_INTERFACE.md §1.9: the `#[non_exhaustive]` `RepositoryErrorKind`
    // gains `Other(String)`, whose display form the contract states.
    let other = icechunk::repository::RepositoryErrorKind::Other("boom".to_string());
    assert_eq!(format!("{other}"), "unexpected error: boom");
    Ok(())
}

#[tokio::test]
async fn n11_ref_version_info__try_from_version_info_splits_off_as_of() -> TestResult {
    use icechunk::repository::RefVersionInfo;
    assert_eq!(
        RefVersionInfo::try_from(&VersionInfo::TagRef("v1".to_string())).unwrap(),
        RefVersionInfo::TagRef("v1".to_string())
    );
    assert_eq!(
        RefVersionInfo::try_from(&VersionInfo::BranchTipRef("main".to_string())).unwrap(),
        RefVersionInfo::BranchTipRef("main".to_string())
    );
    let id = SnapshotId::random();
    assert_eq!(
        RefVersionInfo::try_from(&VersionInfo::SnapshotId(id.clone())).unwrap(),
        RefVersionInfo::SnapshotId(id)
    );

    let at = Utc::now();
    let err = RefVersionInfo::try_from(&VersionInfo::AsOf {
        branch: "main".to_string(),
        at,
    })
    .expect_err("AsOf has no ref-only form");
    assert_eq!(err, ("main".to_string(), at));
    Ok(())
}

#[tokio::test]
async fn n11_ref_version_info__resolve_version_still_accepts_as_of() -> TestResult {
    use icechunk::repository::RefVersionInfo;
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("c1", None).await?;

    let at = Utc::now();
    assert_eq!(
        repo.resolve_version(&VersionInfo::AsOf { branch: "main".to_string(), at })
            .await?,
        snap
    );
    // And the ref-only forms agree with resolve_ref_version.
    assert_eq!(
        repo.resolve_version(&VersionInfo::BranchTipRef("main".to_string())).await?,
        repo.resolve_ref_version(&RefVersionInfo::BranchTipRef("main".to_string()))
            .await?
    );
    Ok(())
}

// ===========================================================================
// n12 — inline chunks honour partial byte ranges
// ===========================================================================

async fn inline_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    // The threshold is above the chunk size, so every chunk is stored inline.
    let config =
        RepositoryConfig { inline_chunk_threshold_bytes: Some(512), ..Default::default() };
    mem_repo_with(config, HashMap::new()).await
}

#[tokio::test]
async fn n12_inline_partial_range__a_bounded_range_is_honoured() -> TestResult {
    let repo = inline_repo().await?;
    let mut session = repo.writable_session("main").await?;
    let path: Path = "/a".try_into()?;
    seed_array(&mut session, &path, 1, b"0123456789").await?;

    let all = get_chunk(
        session.get_chunk_reader(&path, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(all.unwrap(), Bytes::from_static(b"0123456789"));

    let mid = get_chunk(
        session
            .get_chunk_reader(&path, &ChunkIndices(vec![0]), &ByteRange::bounded(2, 5))
            .await?,
    )
    .await?;
    assert_eq!(mid.unwrap(), Bytes::from_static(b"234"));
    Ok(())
}

#[tokio::test]
async fn n12_inline_partial_range__an_overlong_range_is_clamped() -> TestResult {
    use icechunk::session::construct_valid_byte_range;

    let repo = inline_repo().await?;
    let mut session = repo.writable_session("main").await?;
    let path: Path = "/a".try_into()?;
    seed_array(&mut session, &path, 1, b"0123456789").await?;

    let clipped = get_chunk(
        session
            .get_chunk_reader(&path, &ChunkIndices(vec![0]), &ByteRange::bounded(8, 100))
            .await?,
    )
    .await?;
    assert_eq!(
        clipped.unwrap(),
        Bytes::from_static(b"89"),
        "a range past the end of an inline chunk is clamped to its length"
    );

    let from = get_chunk(
        session
            .get_chunk_reader(&path, &ChunkIndices(vec![0]), &ByteRange::from_offset(7))
            .await?,
    )
    .await?;
    assert_eq!(from.unwrap(), Bytes::from_static(b"789"));

    // TEST_INTERFACE.md §1.10: the clamping itself is a public function.
    assert_eq!(
        construct_valid_byte_range(&ByteRange::bounded(8, 100), 0, 10),
        8u64..10u64,
        "the overlong request is clamped to the chunk's own length"
    );
    assert_eq!(
        construct_valid_byte_range(&ByteRange::from_offset(7), 0, 10),
        7u64..10u64
    );
    Ok(())
}

#[tokio::test]
async fn n12_inline_partial_range__inline_and_materialised_chunks_agree() -> TestResult {
    // The same read, once against an inline chunk and once against a
    // materialised one, must produce the same bytes.
    let inline = inline_repo().await?;
    let materialised = mem_repo_with(
        RepositoryConfig { inline_chunk_threshold_bytes: Some(0), ..Default::default() },
        HashMap::new(),
    )
    .await?;

    for repo in [inline, materialised] {
        let mut session = repo.writable_session("main").await?;
        let path: Path = "/a".try_into()?;
        seed_array(&mut session, &path, 1, b"0123456789").await?;
        let got = get_chunk(
            session
                .get_chunk_reader(&path, &ChunkIndices(vec![0]), &ByteRange::bounded(3, 6))
                .await?,
        )
        .await?;
        assert_eq!(got.unwrap(), Bytes::from_static(b"345"));
    }
    Ok(())
}

// ===========================================================================
// i01 — milestone 01: the spec-1 repository format, in this release's surface
// ===========================================================================

#[tokio::test]
async fn i01_repository_format__paths_and_object_ids() -> TestResult {
    assert_eq!(Path::root().to_string(), "/");
    let p: Path = "/a/b".try_into()?;
    assert_eq!(p.to_string(), "/a/b");
    assert_eq!(p.name(), Some("b"));
    assert!(p.starts_with(&Path::root()));
    assert!(Path::new("a/b").is_err(), "a relative path is rejected");
    assert!(Path::new("/a/../b").is_err(), "a non-canonical path is rejected");

    // Snapshot ids are Crockford base32, 20 characters for 12 bytes.
    let id = SnapshotId::random();
    let text = String::from(&id);
    assert_eq!(text.len(), 20);
    assert_eq!(SnapshotId::try_from(text.as_str())?, id);
    assert_eq!(id.to_string(), text);
    Ok(())
}

#[tokio::test]
async fn i01_repository_format__branches_and_tags() -> TestResult {
    let repo = mem_repo().await?;
    assert_eq!(Ref::DEFAULT_BRANCH, "main");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let first = session.commit("c1", None).await?;

    repo.create_branch("dev", &first).await?;
    assert!(repo.list_branches().await?.contains("dev"));
    assert_eq!(repo.lookup_branch("dev").await?, first);

    repo.create_tag("v1", &first).await?;
    assert!(repo.list_tags().await?.contains("v1"));
    assert_eq!(repo.lookup_tag("v1").await?, first);
    assert!(repo.create_tag("v1", &first).await.is_err(), "tags are immutable");

    repo.delete_tag("v1").await?;
    assert!(!repo.list_tags().await?.contains("v1"));
    assert!(
        repo.create_tag("v1", &first).await.is_err(),
        "a deleted tag name stays burned"
    );

    repo.delete_branch("dev").await?;
    assert!(!repo.list_branches().await?.contains("dev"));
    assert!(repo.delete_branch("main").await.is_err(), "main cannot be deleted");
    Ok(())
}

#[tokio::test]
async fn i01_repository_format__hierarchy_and_nodes() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_group("/g".try_into()?, Bytes::copy_from_slice(b"group")).await?;
    let array: Path = "/g/a".try_into()?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 2)]).unwrap(),
            None,
            Bytes::copy_from_slice(b"array"),
        )
        .await?;

    let node = session.get_node(&array).await?;
    assert_eq!(node.node_type(), NodeType::Array);
    assert_eq!(node.user_data, Bytes::copy_from_slice(b"array"));
    assert_eq!(session.get_group(&"/g".try_into()?).await?.node_type(), NodeType::Group);
    assert!(session.get_array(&"/g".try_into()?).await.is_err());

    let paths: Vec<String> = session
        .list_nodes()
        .await?
        .map(|n| n.map(|n| n.path.to_string()))
        .collect::<Result<Vec<_>, _>>()?;
    assert!(paths.contains(&"/".to_string()));
    assert!(paths.contains(&"/g".to_string()));
    assert!(paths.contains(&"/g/a".to_string()));

    session.delete_array(array.clone()).await?;
    assert!(session.get_node(&array).await.is_err());

    // TEST_INTERFACE.md §2.7: `ArrayShape::new` is `None` if any chunk length
    // is zero, on any axis.
    assert!(
        ArrayShape::new(vec![(4u64, 0u64)]).is_none(),
        "a zero chunk length has no valid shape"
    );
    assert!(ArrayShape::new(vec![(4u64, 2u64), (8u64, 0u64)]).is_none());
    assert!(ArrayShape::new(vec![(4u64, 2u64), (8u64, 4u64)]).is_some());
    Ok(())
}

#[tokio::test]
async fn i01_repository_format__chunk_payload_kinds() -> TestResult {
    // The inline threshold decides whether a chunk becomes its own object.
    let config =
        RepositoryConfig { inline_chunk_threshold_bytes: Some(4), ..Default::default() };
    let repo = mem_repo_with(config, HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    let path: Path = "/a".try_into()?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(path.clone(), ArrayShape::new(vec![(2, 1)]).unwrap(), None, Bytes::new())
        .await?;

    let small = session.get_chunk_writer()(Bytes::from_static(b"abcd")).await?;
    assert!(matches!(small, ChunkPayload::Inline(_)), "at the threshold: inline");
    session.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(small)).await?;

    let big = session.get_chunk_writer()(Bytes::from_static(b"abcde")).await?;
    assert!(matches!(big, ChunkPayload::Ref(_)), "above the threshold: materialised");
    session.set_chunk_ref(path.clone(), ChunkIndices(vec![1]), Some(big)).await?;

    assert!(matches!(
        session.get_chunk_ref(&path, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline(_))
    ));
    assert!(matches!(
        session.get_chunk_ref(&path, &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Ref(_))
    ));
    assert!(session.get_chunk_ref(&path, &ChunkIndices(vec![0])).await.is_ok());

    // A coordinate outside the array's chunk grid is rejected.
    let payload = session.get_chunk_writer()(Bytes::from_static(b"zz")).await?;
    assert!(
        session
            .set_chunk_ref(path.clone(), ChunkIndices(vec![99]), Some(payload))
            .await
            .is_err()
    );

    // TEST_INTERFACE.md §2.1: with no explicit configuration the threshold is
    // 512, so a 512-byte payload is still inline and 513 bytes is not.
    let default_repo = mem_repo().await?;
    let default_session = default_repo.writable_session("main").await?;
    let at_default =
        default_session.get_chunk_writer()(Bytes::from(vec![7u8; 512])).await?;
    assert!(
        matches!(at_default, ChunkPayload::Inline(_)),
        "512 bytes is at the default threshold, so it stays inline"
    );
    let over_default =
        default_session.get_chunk_writer()(Bytes::from(vec![7u8; 513])).await?;
    assert!(
        matches!(over_default, ChunkPayload::Ref(_)),
        "513 bytes is above the default threshold, so it is materialised"
    );
    Ok(())
}

#[tokio::test]
async fn i01_repository_format__zarr_key_grammar_through_the_store() -> TestResult {
    let repo = mem_repo().await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    store
        .set("zarr.json", Bytes::copy_from_slice(GROUP_META))
        .await
        .expect("the root group metadata key");
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    store.set("array/c/0", Bytes::from_static(b"chunk-zero")).await?;

    assert!(store.exists("array/zarr.json").await?);
    assert!(store.exists("array/c/0").await?);
    assert!(!store.exists("array/c/1").await?);
    assert_eq!(
        store.get("array/c/0", &ByteRange::ALL).await?,
        Bytes::from_static(b"chunk-zero")
    );
    assert_eq!(
        store.get("array/c/0", &ByteRange::bounded(0, 5)).await?,
        Bytes::from_static(b"chunk")
    );

    let mut keys: Vec<String> = store.list_dir("/").await?.try_collect().await?;
    keys.sort();
    assert_eq!(keys, vec!["array".to_string(), "zarr.json".to_string()]);

    assert!(store.get("array/c/1", &ByteRange::ALL).await.is_err());
    store.delete("array/c/0").await?;
    assert!(!store.exists("array/c/0").await?);
    Ok(())
}

// ===========================================================================
// i02 — milestone 02: sessions, conflicts and rebase
// ===========================================================================

#[tokio::test]
async fn i02_sessions_conflicts__read_only_sessions_refuse_writes() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("c1", None).await?;

    // Committing mutates the session into a read-only one.
    assert!(session.read_only());
    assert_eq!(session.branch(), None);
    assert!(session.commit("again", None).await.is_err());

    let mut ro = repo.readonly_session(&VersionInfo::SnapshotId(snap.clone())).await?;
    assert!(ro.read_only());
    assert_eq!(ro.snapshot_id(), &snap);
    assert!(ro.add_group("/x".try_into()?, Bytes::new()).await.is_ok());
    assert!(ro.commit("nope", None).await.is_err(), "a read-only session cannot commit");
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__commit_detects_a_conflicting_tip() -> TestResult {
    let repo = mem_repo().await?;
    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.commit("base", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.add_group("/a".try_into()?, Bytes::new()).await?;
    a.commit("a", None).await?;
    b.add_group("/b".try_into()?, Bytes::new()).await?;

    let err = b.commit("b", None).await.expect_err("the tip moved under b");
    match err {
        icechunk::session::SessionError {
            kind: icechunk::session::SessionErrorKind::Conflict { expected_parent, .. },
            ..
        } => assert!(expected_parent.is_some(), "the conflict names the parent b expected"),
        other => panic!("a lost race must be reported as a conflict, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__rebase_resolves_with_the_basic_solver() -> TestResult {
    let repo = mem_repo().await?;
    let path: Path = "/arr".try_into()?;
    let mut base = repo.writable_session("main").await?;
    base.add_group(Path::root(), Bytes::new()).await?;
    base.add_array(path.clone(), ArrayShape::new(vec![(4, 1)]).unwrap(), None, Bytes::new())
        .await?;
    base.commit("base", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    let p = a.get_chunk_writer()(Bytes::from_static(b"aaaa")).await?;
    a.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    a.commit("a", None).await?;
    let p = b.get_chunk_writer()(Bytes::from_static(b"bbbb")).await?;
    b.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    assert!(b.commit("b", None).await.is_err());

    // The bare detector never patches a double chunk update.
    assert!(
        b.rebase(&ConflictDetector).await.is_err(),
        "the detector reports the conflict instead of solving it"
    );

    // The basic solver keeps our version by default.
    b.rebase(&BasicConflictSolver::default()).await?;
    let snap = b.commit("b after rebase", None).await?;
    assert_eq!(repo.lookup_branch("main").await?, snap);

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let got = get_chunk(
        ro.get_chunk_reader(&path, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"bbbb"));
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__sessions_round_trip_and_merge() -> TestResult {
    let repo = mem_repo().await?;
    let mut main = repo.writable_session("main").await?;
    main.add_group(Path::root(), Bytes::new()).await?;
    main.commit("base", None).await?;

    let mut worker = repo.writable_session("main").await?;
    worker.add_group("/from-worker".try_into()?, Bytes::new()).await?;
    let wire = worker.as_bytes()?;
    let restored = Session::from_bytes(wire)?;
    assert!(restored.has_uncommitted_changes());

    let mut coordinator = repo.writable_session("main").await?;
    coordinator.add_group("/from-coordinator".try_into()?, Bytes::new()).await?;
    coordinator.merge(restored).await?;
    let snap = coordinator.commit("merged", None).await?;

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(ro.get_group(&"/from-worker".try_into()?).await.is_ok());
    assert!(ro.get_group(&"/from-coordinator".try_into()?).await.is_ok());
    Ok(())
}

// ===========================================================================
// i03 — milestone 03: expiration, garbage collection and the file header
// ===========================================================================

#[tokio::test]
async fn i03_expiration_gc__file_format_constants() -> TestResult {
    assert_eq!(SpecVersionBin::current(), SpecVersionBin::V0dot1);
    assert_eq!(SpecVersionBin::V0dot1 as u8, 1);
    assert_eq!(SpecVersionBin::try_from(1u8)?, SpecVersionBin::V0dot1);
    assert!(SpecVersionBin::try_from(200u8).is_err());

    assert_eq!(FileTypeBin::Snapshot as u8, 1);
    assert_eq!(FileTypeBin::Manifest as u8, 2);
    assert_eq!(FileTypeBin::Attributes as u8, 3);
    assert_eq!(FileTypeBin::TransactionLog as u8, 4);
    assert_eq!(FileTypeBin::Chunk as u8, 5);
    assert_eq!(FileTypeBin::try_from(4u8)?, FileTypeBin::TransactionLog);
    assert!(FileTypeBin::try_from(0u8).is_err());
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__expire_releases_old_snapshots() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo =
        Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 4, b"first-one").await?;
    session.commit("first", None).await?;
    let mut session = repo.writable_session("main").await?;
    let p = session.get_chunk_writer()(Bytes::from_static(b"second-x")).await?;
    session.set_chunk_ref(path.clone(), ChunkIndices(vec![0]), Some(p)).await?;
    let head = session.commit("second", None).await?;

    let result = icechunk::ops::gc::expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        after_everything(),
        icechunk::ops::gc::ExpiredRefAction::Ignore,
        icechunk::ops::gc::ExpiredRefAction::Ignore,
    )
    .await?;
    assert!(!result.released_snapshots.is_empty(), "older snapshots are released");
    assert!(!result.edited_snapshots.is_empty());

    // The branch tip still resolves and still reads.
    assert_eq!(repo.lookup_branch("main").await?, head);

    // TEST_INTERFACE.md §1.7 and §2.7: the single-reference entry point keeps
    // the signature it had at the previous checkpoint — the same five
    // arguments, the same two-variant `ExpireRefResult`, the same `GCError`.
    let per_ref: Result<
        icechunk::ops::gc::ExpireRefResult,
        icechunk::ops::gc::GCError,
    > = icechunk::ops::gc::expire_ref(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &Ref::Branch("main".to_string()),
        after_everything(),
    )
    .await;
    let ref_is_expired = match per_ref? {
        icechunk::ops::gc::ExpireRefResult::NothingToDo { ref_is_expired } => {
            ref_is_expired
        }
        icechunk::ops::gc::ExpireRefResult::Done { ref_is_expired, .. } => ref_is_expired,
    };
    assert!(ref_is_expired, "every snapshot on `main` is older than the threshold");
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__an_expired_tag_can_be_deleted() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo = Repository::create(None, storage.clone(), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let first = session.commit("first", None).await?;
    repo.create_tag("old", &first).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group("/later".try_into()?, Bytes::new()).await?;
    session.commit("second", None).await?;

    let result = icechunk::ops::gc::expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        after_everything(),
        // Both actions delete, so the surviving `main` below is the rule
        // "except `main`, which is never deleted" and not the branch action.
        icechunk::ops::gc::ExpiredRefAction::Delete,
        icechunk::ops::gc::ExpiredRefAction::Delete,
    )
    .await?;
    assert!(
        result.deleted_refs.contains(&Ref::Tag("old".to_string())),
        "a fully expired tag is deleted when asked: {:?}",
        result.deleted_refs
    );
    assert!(!repo.list_tags().await?.contains("old"));
    // `main` is never deleted, whatever its age and whatever the branch action.
    assert!(repo.list_branches().await?.contains("main"));
    assert!(
        !result.deleted_refs.contains(&Ref::Branch("main".to_string())),
        "`main` must never be reported as deleted: {:?}",
        result.deleted_refs
    );
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__expiration_leaves_the_tip_readable() -> TestResult {
    let storage = new_in_memory_storage().await?;
    let settings = storage.default_settings();
    let repo =
        Repository::create(Some(materialising()), storage.clone(), HashMap::new()).await?;
    let path: Path = "/array".try_into()?;

    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 6, b"original").await?;
    session.commit("first", None).await?;
    let mut session = repo.writable_session("main").await?;
    for i in 0..3u32 {
        let p = session.get_chunk_writer()(Bytes::from_static(b"replaced")).await?;
        session.set_chunk_ref(path.clone(), ChunkIndices(vec![i]), Some(p)).await?;
    }
    let head = session.commit("second", None).await?;

    icechunk::ops::gc::expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        after_everything(),
        icechunk::ops::gc::ExpiredRefAction::Ignore,
        icechunk::ops::gc::ExpiredRefAction::Ignore,
    )
    .await?;

    assert_eq!(repo.lookup_branch("main").await?, head);
    let session = repo.readonly_session(&VersionInfo::SnapshotId(head)).await?;
    for i in 0..6u32 {
        let want: &[u8] = if i < 3 { b"replaced" } else { b"original" };
        let got = get_chunk(
            session.get_chunk_reader(&path, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?;
        assert_eq!(got.unwrap(), Bytes::copy_from_slice(want), "chunk {i}");
    }
    Ok(())
}

// ===========================================================================
// i04 — milestone 04: the FlatBuffers format and the error split
// ===========================================================================

#[tokio::test]
async fn i04_flatbuffers_format__snapshot_buffers_round_trip() -> TestResult {
    let (repo, head) = two_commits().await?;
    let snapshot = repo.asset_manager().fetch_snapshot(&head).await?;

    let bytes = snapshot.bytes().to_vec();
    assert!(bytes.len() > 8);
    assert_eq!(&bytes[4..8], b"Ichk", "the FlatBuffers file identifier");

    let reloaded = Snapshot::from_buffer(bytes)?;
    assert_eq!(reloaded.id(), head);
    assert_eq!(reloaded.message(), "c2");
    assert_eq!(reloaded.parent_id(), snapshot.parent_id());
    assert_eq!(reloaded.len(), snapshot.len());

    assert!(
        Snapshot::from_buffer(b"not a flatbuffer at all".to_vec()).is_err(),
        "a corrupt buffer is rejected before any field is read"
    );
    Ok(())
}

#[tokio::test]
async fn i04_flatbuffers_format__errors_carry_a_kind_and_a_context() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("c1", None).await?;

    let session = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    let err = session
        .get_node(&"/missing".try_into()?)
        .await
        .expect_err("no such node");
    match err {
        icechunk::session::SessionError {
            kind: icechunk::session::SessionErrorKind::NodeNotFound { path, .. },
            ..
        } => assert_eq!(path.to_string(), "/missing"),
        other => panic!("expected NodeNotFound, got {other:?}"),
    }

    let err = repo.lookup_tag("no-such-tag").await.expect_err("no such tag");
    assert!(matches!(
        err,
        icechunk::repository::RepositoryError {
            kind: icechunk::repository::RepositoryErrorKind::Ref(_),
            ..
        }
    ));
    Ok(())
}

#[tokio::test]
async fn i04_flatbuffers_format__zarr_metadata_bytes_survive_verbatim() -> TestResult {
    let repo = mem_repo().await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;

    // An extension key no Icechunk version knows about must round-trip.
    let meta = Bytes::from_static(
        br#"{"zarr_format":3,"node_type":"group","attributes":{"x":1},"must_understand":false,"unknown_extension":{"deep":[1,2,3]}}"#,
    );
    store.set("zarr.json", meta.clone()).await?;
    assert_eq!(store.get("zarr.json", &ByteRange::ALL).await?, meta);

    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    assert_eq!(
        store.get("array/zarr.json", &ByteRange::ALL).await?,
        Bytes::copy_from_slice(ARRAY_META)
    );
    Ok(())
}

#[tokio::test]
async fn i04_flatbuffers_format__diff_reports_changes_between_versions() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let base = session.commit("base", None).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group("/g".try_into()?, Bytes::new()).await?;
    let array: Path = "/g/a".try_into()?;
    session
        .add_array(array.clone(), ArrayShape::new(vec![(2, 1)]).unwrap(), None, Bytes::new())
        .await?;
    let p = session.get_chunk_writer()(Bytes::from_static(b"data")).await?;
    session.set_chunk_ref(array.clone(), ChunkIndices(vec![0]), Some(p)).await?;

    let status = session.status().await?;
    assert!(status.new_groups.contains(&"/g".try_into()?));
    assert!(status.new_arrays.contains(&array));
    let head = session.commit("changes", None).await?;

    let diff = repo
        .diff(&VersionInfo::SnapshotId(base), &VersionInfo::SnapshotId(head))
        .await?;
    assert!(diff.new_groups.contains(&"/g".try_into()?));
    assert!(diff.new_arrays.contains(&array));
    assert_eq!(
        diff.updated_chunks.get(&array),
        Some(&[ChunkIndices(vec![0])].into_iter().collect())
    );
    Ok(())
}

// ===========================================================================
// i05 — milestone 05: the icechunk command-line interface
//
// Process-level, because that is how a user reaches it: the candidate's own
// binary, its argv grammar, its exit status and its streams. Its path arrives
// in ICECHUNK_TEST_CLI_BIN and the configuration directory is redirected into a
// TempDir, so nothing touches the invoking user's environment.
// ===========================================================================

fn cli_bin() -> std::path::PathBuf {
    std::path::PathBuf::from(
        std::env::var("ICECHUNK_TEST_CLI_BIN")
            .expect("ICECHUNK_TEST_CLI_BIN must name the candidate's icechunk binary"),
    )
}

struct CliRun {
    status: Option<i32>,
    stdout: String,
    stderr: String,
}

/// Runs the candidate's `icechunk` binary with `XDG_CONFIG_HOME` pointed at
/// `config_home`, so the CLI reads and writes only inside the test's TempDir.
fn run_cli(config_home: &FsPath, args: &[&str]) -> CliRun {
    let out = std::process::Command::new(cli_bin())
        .args(args)
        .env("XDG_CONFIG_HOME", config_home)
        .env_remove("ICECHUNK_LOG")
        .output()
        .expect("the icechunk binary must be executable");
    CliRun {
        status: out.status.code(),
        stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
        stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
    }
}

/// Writes `cli-config.yaml` declaring one local-filesystem repository.
fn write_cli_config(config_home: &FsPath, alias: &str, repo_path: &FsPath) {
    let dir = config_home.join("icechunk");
    std::fs::create_dir_all(&dir).unwrap();
    std::fs::write(
        dir.join("cli-config.yaml"),
        format!(
            "repos:\n  {alias}: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_path.display()
        ),
    )
    .unwrap();
}

#[tokio::test]
async fn i05_cli_repo_snapshot__help_and_usage_exit_codes() -> TestResult {
    let home = tempfile::TempDir::new()?;

    let help = run_cli(home.path(), &["--help"]);
    assert_eq!(help.status, Some(0), "stderr: {}", help.stderr);
    assert!(help.stdout.contains("Usage: icechunk"), "stdout: {}", help.stdout);
    for sub in ["repo", "snapshot", "config"] {
        assert!(help.stdout.contains(sub), "{sub} must be listed: {}", help.stdout);
    }

    let none = run_cli(home.path(), &[]);
    assert_eq!(none.status, Some(2), "no subcommand is a usage error");

    let bogus = run_cli(home.path(), &["definitely-not-a-command"]);
    assert_eq!(bogus.status, Some(2), "an unknown subcommand is a usage error");

    let sub = run_cli(home.path(), &["snapshot", "list", "--help"]);
    assert_eq!(sub.status, Some(0));
    assert!(sub.stdout.contains("--branch"), "stdout: {}", sub.stdout);
    Ok(())
}

#[tokio::test]
async fn i05_cli_repo_snapshot__config_list_reads_the_config_file() -> TestResult {
    let home = tempfile::TempDir::new()?;
    let empty = run_cli(home.path(), &["config", "list"]);
    assert_eq!(empty.status, Some(0), "stderr: {}", empty.stderr);
    assert!(
        empty.stdout.contains("repos:"),
        "an absent config reads as an empty one: {}",
        empty.stdout
    );

    let repo_dir = tempfile::TempDir::new()?;
    write_cli_config(home.path(), "local1", repo_dir.path());
    let listed = run_cli(home.path(), &["config", "list"]);
    assert_eq!(listed.status, Some(0), "stderr: {}", listed.stderr);
    assert!(listed.stdout.contains("local1"), "stdout: {}", listed.stdout);
    assert!(
        listed.stdout.contains("LocalFileSystem"),
        "the repository kind is reported: {}",
        listed.stdout
    );
    Ok(())
}

#[tokio::test]
async fn i05_cli_repo_snapshot__repo_create_and_snapshot_list() -> TestResult {
    let home = tempfile::TempDir::new()?;
    let repo_root = tempfile::TempDir::new()?;
    let repo_dir = repo_root.path().join("repo");
    write_cli_config(home.path(), "local1", &repo_dir);

    let created = run_cli(home.path(), &["repo", "create", "local1"]);
    assert_eq!(created.status, Some(0), "stderr: {}", created.stderr);
    assert!(created.stdout.contains("local1"), "stdout: {}", created.stdout);
    assert!(repo_dir.join("refs").is_dir(), "the repository has a refs prefix");
    assert!(repo_dir.join("snapshots").is_dir(), "the repository has a snapshots prefix");

    let listed = run_cli(home.path(), &["snapshot", "list", "local1"]);
    assert_eq!(listed.status, Some(0), "stderr: {}", listed.stderr);
    let lines: Vec<&str> =
        listed.stdout.lines().filter(|l| l.starts_with("SnapshotInfo {")).collect();
    assert_eq!(
        lines.len(),
        1,
        "a fresh repository lists exactly one snapshot; stdout: {}",
        listed.stdout
    );
    assert!(
        lines[0].contains("Repository initialized"),
        "the root snapshot's message: {}",
        lines[0]
    );

    // A second snapshot, written through the library into the very repository
    // the binary created, so that "newest first" and `-n` have two lines to
    // distinguish rather than one.
    let storage = new_local_filesystem_storage(&repo_dir).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("second commit", None).await?;

    let both = run_cli(home.path(), &["snapshot", "list", "local1"]);
    assert_eq!(both.status, Some(0), "stderr: {}", both.stderr);
    let lines: Vec<&str> =
        both.stdout.lines().filter(|l| l.starts_with("SnapshotInfo {")).collect();
    assert_eq!(lines.len(), 2, "stdout: {}", both.stdout);
    assert!(lines[0].contains("second commit"), "newest first: {}", both.stdout);
    assert!(
        lines[1].contains("Repository initialized"),
        "the root snapshot comes last: {}",
        both.stdout
    );

    let capped = run_cli(home.path(), &["snapshot", "list", "local1", "-n", "1", "-b", "main"]);
    assert_eq!(capped.status, Some(0), "stderr: {}", capped.stderr);
    let capped_lines: Vec<&str> =
        capped.stdout.lines().filter(|l| l.starts_with("SnapshotInfo {")).collect();
    assert_eq!(capped_lines.len(), 1, "-n caps the number of lines: {}", capped.stdout);
    assert!(
        capped_lines[0].contains("second commit"),
        "and keeps the newest: {}",
        capped.stdout
    );
    Ok(())
}

#[tokio::test]
async fn i05_cli_repo_snapshot__unknown_alias_and_branch_fail() -> TestResult {
    let home = tempfile::TempDir::new()?;
    let repo_root = tempfile::TempDir::new()?;
    let repo_dir = repo_root.path().join("repo");
    write_cli_config(home.path(), "local1", &repo_dir);
    assert_eq!(run_cli(home.path(), &["repo", "create", "local1"]).status, Some(0));

    let missing = run_cli(home.path(), &["snapshot", "list", "no-such-alias"]);
    assert_eq!(missing.status, Some(1), "an unknown alias is a runtime failure");
    assert!(
        missing.stderr.contains("Repository not found in config"),
        "stderr: {}",
        missing.stderr
    );

    let branch = run_cli(home.path(), &["snapshot", "list", "local1", "-b", "no-such-branch"]);
    assert_eq!(branch.status, Some(1), "an unknown branch is a runtime failure");

    let again = run_cli(home.path(), &["repo", "create", "local1"]);
    assert_eq!(again.status, Some(1), "a second create over a dirty prefix fails");
    Ok(())
}

// ===========================================================================
// i06 — milestone 06: manifest splitting and the fixed first snapshot
// ===========================================================================

#[tokio::test]
async fn i06_manifest_splitting__split_edges_and_extents() -> TestResult {
    use icechunk::format::manifest::{ManifestExtents, ManifestSplits, uniform_manifest_split_edges};

    assert_eq!(uniform_manifest_split_edges(24, &12), vec![0, 12, 24]);
    assert_eq!(uniform_manifest_split_edges(25, &12), vec![0, 12, 24, 25]);

    let splits = ManifestSplits::from_edges(vec![vec![0u32, 2, 4]]);
    assert_eq!(splits.len(), 2);
    assert_eq!(splits.position(&ChunkIndices(vec![0])), Some(0));
    assert_eq!(splits.position(&ChunkIndices(vec![3])), Some(1));
    assert_eq!(splits.position(&ChunkIndices(vec![9])), None);

    let extent = ManifestExtents::new(&[0], &[2]);
    assert!(extent.contains(&[1]));
    assert!(!extent.contains(&[2]), "the upper bound is exclusive");
    assert_eq!(splits.find(&ChunkIndices(vec![1])), Some(&extent));
    assert!(
        splits.find(&ChunkIndices(vec![9])).is_none(),
        "`find`, like `position`, is None outside the grid"
    );
    Ok(())
}

#[tokio::test]
async fn i06_manifest_splitting__a_split_size_produces_several_manifests() -> TestResult {
    use icechunk::config::ManifestConfig;

    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        manifest: Some(ManifestConfig {
            splitting: Some(ManifestSplittingConfig::with_size(4)),
            ..Default::default()
        }),
        ..Default::default()
    };
    let repo = mem_repo_with(config, HashMap::new()).await?;
    let path: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 10, b"abcdef").await?;
    let head = session.commit("split", None).await?;

    let snapshot = repo.asset_manager().fetch_snapshot(&head).await?;
    let manifests: Vec<_> = snapshot.manifest_files().collect();
    assert_eq!(manifests.len(), 3, "ten chunks split four at a time");
    assert_eq!(manifests.iter().map(|m| m.num_chunk_refs).sum::<u32>(), 10);

    // Splitting is invisible to readers.
    let ro = repo.readonly_session(&VersionInfo::SnapshotId(head)).await?;
    for i in 0..10u32 {
        let got = get_chunk(
            ro.get_chunk_reader(&path, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?;
        assert_eq!(got.unwrap(), Bytes::from_static(b"abcdef"), "chunk {i}");
    }
    Ok(())
}

#[tokio::test]
async fn i06_manifest_splitting__the_first_snapshot_id_is_fixed() -> TestResult {
    assert_eq!(String::from(&Snapshot::INITIAL_SNAPSHOT_ID), "1CECHNKREP0F1RSTCMT0");
    assert_eq!(Snapshot::INITIAL_COMMIT_MESSAGE, "Repository initialized");

    let repo = mem_repo().await?;
    let root = repo.lookup_branch("main").await?;
    assert_eq!(root, Snapshot::INITIAL_SNAPSHOT_ID);
    let info = repo.lookup_snapshot(&root).await?;
    assert_eq!(info.message, Snapshot::INITIAL_COMMIT_MESSAGE);
    assert!(info.is_initial());
    Ok(())
}

#[tokio::test]
async fn i06_manifest_splitting__rewrite_manifests_records_its_configuration()
-> TestResult {
    use icechunk::{config::ManifestConfig, ops::manifests::rewrite_manifests};

    let config = RepositoryConfig {
        inline_chunk_threshold_bytes: Some(0),
        manifest: Some(ManifestConfig {
            splitting: Some(ManifestSplittingConfig::with_size(3)),
            ..Default::default()
        }),
        ..Default::default()
    };
    let repo = mem_repo_with(config, HashMap::new()).await?;
    let path: Path = "/array".try_into()?;
    let mut session = repo.writable_session("main").await?;
    seed_array(&mut session, &path, 9, b"xyz").await?;
    session.commit("seed", None).await?;

    let rewritten = rewrite_manifests(&repo, "main", "rewrite", None).await?;
    let info = repo.lookup_snapshot(&rewritten).await?;
    assert!(
        info.metadata.contains_key("splitting_config"),
        "the rewrite records the splitting configuration it used: {:?}",
        info.metadata.keys().collect::<Vec<_>>()
    );
    assert_eq!(repo.lookup_branch("main").await?, rewritten);

    let ro = repo.readonly_session(&VersionInfo::SnapshotId(rewritten)).await?;
    let got = get_chunk(
        ro.get_chunk_reader(&path, &ChunkIndices(vec![4]), &ByteRange::ALL).await?,
    )
    .await?;
    assert_eq!(got.unwrap(), Bytes::from_static(b"xyz"));
    Ok(())
}
