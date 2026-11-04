// Verifier-owned conformance driver for milestone 11 — "headers, presets and
// hardening" (earth-mover/icechunk v2.2.0).
//
// Every assertion here traces to /app/TEST_INTERFACE.md (the public Rust and
// command-line surface this milestone must expose) or to /app/icechunk-spec/
// (the pinned Icechunk specification corpus plus PROFILE.md, which resolves the
// places the specification and this milestone's contract leave open).
//
// Group = the `<group>__` prefix of the test function name. The runner invokes
// one `cargo test` per group and asserts the executed count.
//
// Nothing here reaches the network, reads a cloud credential, or starts a
// container. Storage is `new_in_memory_storage()` or
// `new_local_filesystem_storage()` over a `tempfile::TempDir`; the storage
// presets are only ever *constructed* and read back through `storage_info()`,
// which is synchronous and does no I/O. The command-line groups run the
// candidate's own `icechunk` binary, whose path the runner exports as
// ICECHUNK_TEST_CLI_BIN, against a local-filesystem repository inside a
// `TempDir` with XDG_CONFIG_HOME pointed into that same directory.

#![allow(clippy::expect_used, clippy::unwrap_used, clippy::panic)]
// Group prefixes are separated from the case name by `__`, which the runner
// uses to filter and to attribute a compile failure to one group.
#![allow(non_snake_case)]

use std::{
    collections::{HashMap, HashSet},
    num::{NonZeroU16, NonZeroUsize},
    path::{Path as FsPath, PathBuf},
    process::Command,
    sync::Arc,
};

use bytes::Bytes;
use futures::TryStreamExt as _;
use serde_json::Value;
use tokio::sync::RwLock;

use icechunk::{
    Repository, Storage, Store,
    config::{
        ManifestConfig, ManifestSplitCondition, ManifestSplitDimCondition,
        ManifestSplittingConfig, RepositoryConfig, S3Credentials, S3Options,
        S3StaticCredentials,
    },
    format::{
        ByteRange, ChunkId, ChunkIndices, ManifestId, Path, SnapshotId,
        format_constants::{
            self, CompressionAlgorithmBin, FileHeader, FileTypeBin, SpecVersionBin,
        },
        manifest::{ChunkPayload, ChunkRef, VirtualChunkLocation, VirtualChunkRef},
        snapshot::ArrayShape,
    },
    inspect::{manifest_json, repo_info_json, snapshot_json, transaction_log_json},
    new_in_memory_storage,
    ops::{
        gc::{ExpiredRefAction, GCConfig, GCSummary, expire, garbage_collect},
        manifests::rewrite_manifests,
        stats::repo_chunks_storage,
    },
    repository::VersionInfo,
    session::{Session, SessionError, SessionErrorKind},
    // `Role`, `new_hf_storage` and `validate_extra_headers` are NEW at this
    // release. They are imported inside the case bodies that need them rather
    // than here, so that this file-level prelude still resolves against the
    // PREVIOUS checkpoint's workspace: the per-group attribution slice removes
    // case functions but never `use` items, and a module-level name that does
    // not exist yet would fail to compile every group, the inherited ones
    // included, instead of only the groups that actually need the new surface.
    storage::{
        ObjectStoreBackend, S3ObjectStoreBackend, Settings,
        new_local_filesystem_storage, new_r2_storage, new_s3_object_store_storage,
        new_s3_storage, new_tigris_storage,
    },
    store::{StoreError, StoreErrorKind},
};

type TestResult = Result<(), Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

/// A fresh, empty, spec-2 repository over in-memory storage.
async fn mem_repo() -> Result<Repository, Box<dyn std::error::Error>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(Repository::create(
        None,
        storage,
        HashMap::new(),
        Some(SpecVersionBin::V2),
        true,
    )
    .await?)
}

/// One committed snapshot holding a root group and a `/array` with one inline
/// chunk, so that a snapshot file, a manifest file and a transaction-log file
/// all exist for the `inspect_*` groups to read.
async fn fixture() -> Result<(Repository, SnapshotId), Box<dyn std::error::Error>> {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(4, 2)]).expect("a one-dimensional shape"),
            None,
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            apath,
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"abcd"))),
        )
        .await?;
    let snap = session.commit("first").execute().await?;
    Ok((repo, snap))
}

/// The `header` object of an `inspect_*` document, as a `serde_json::Value`.
/// Every `*Inspect` struct is crate-private, so an external driver parses the
/// returned `String` rather than deserializing into a named type.
fn header_of(json: &str) -> Result<Value, Box<dyn std::error::Error>> {
    let v: Value = serde_json::from_str(json)?;
    let h = v.get("header").ok_or("the inspect document has no `header` member")?;
    Ok(h.clone())
}

/// Assert the JSON header's six members against the writer identity every
/// metadata file this library produces must carry.
fn assert_writer_identity(h: &Value) {
    assert_eq!(
        h["app_name"], "ic",
        "header.app_name is the implementation name before the first `-`"
    );
    assert_eq!(
        h["app_version"], format_constants::ICECHUNK_LIB_VERSION,
        "header.app_version is the writing library's version"
    );
    assert_eq!(
        h["written_by"],
        format_constants::ICECHUNK_CLIENT_NAME.as_str(),
        "header.written_by is the full implementation string"
    );
    assert_eq!(
        h["written_by"],
        format!(
            "{}-{}",
            h["app_name"].as_str().expect("app_name is a string"),
            h["app_version"].as_str().expect("app_version is a string")
        ),
        "written_by decomposes into app_name and app_version"
    );
}

/// Assert the JSON header agrees, member for member, with the header the
/// candidate's own `AssetManager` reads back out of that same file's bytes.
fn assert_matches_file_header(h: &Value, file: &FileHeader) {
    assert_eq!(
        h["written_by"],
        file.impl_name.as_str(),
        "the reported header must describe the file that was read"
    );
    assert_eq!(h["app_name"], file.app_name.as_str());
    match file.app_version.as_deref() {
        Some(v) => assert_eq!(h["app_version"], v),
        // TEST_INTERFACE.md §3.1: the member is absent, not null, when the
        // implementation string carries no `-`.
        None => assert!(
            h.get("app_version").is_none(),
            "app_version is omitted, not null, when the file's header has none"
        ),
    }
    assert_eq!(h["spec_version"], file.spec_version.to_string());
    let expected_type = match file.file_type {
        FileTypeBin::Snapshot => format_constants::ICECHUNK_FILE_TYPE_SNAPSHOT,
        FileTypeBin::Manifest => format_constants::ICECHUNK_FILE_TYPE_MANIFEST,
        FileTypeBin::TransactionLog => {
            format_constants::ICECHUNK_FILE_TYPE_TRANSACTION_LOG
        }
        FileTypeBin::RepoInfo => format_constants::ICECHUNK_FILE_TYPE_REPO_INFO,
        FileTypeBin::Attributes => "attributes",
        FileTypeBin::Chunk => "chunk",
    };
    assert_eq!(h["file_type"], expected_type);
    let expected_compression = match file.compression {
        CompressionAlgorithmBin::None => "none",
        CompressionAlgorithmBin::Zstd => format_constants::ICECHUNK_COMPRESSION_ZSTD,
    };
    assert_eq!(h["compression"], expected_compression);
}

/// The Zarr group metadata document, as a Zarr client writes it.
const GROUP_META: &[u8] = br#"{"zarr_format":3, "node_type":"group"}"#;

/// A 2x2x2 Zarr array of 1x1x1 chunks, so that `array/c/1/1/1` is inside the
/// grid and `array/c/10/1/1` is outside it.
const ARRAY_META: &[u8] = br#"{"zarr_format":3,"node_type":"array","shape":[2,2,2],"data_type":"int32","chunk_grid":{"name":"regular","configuration":{"chunk_shape":[1,1,1]}},"chunk_key_encoding":{"name":"default","configuration":{"separator":"/"}},"fill_value":0,"codecs":[{"name":"bytes","configuration":{"endian":"little"}}]}"#;

/// A store over a fresh in-memory repository holding a root group and one
/// `/array` node, ready for chunk-key writes and deletes.
async fn store_with_array() -> Result<Store, Box<dyn std::error::Error>> {
    let repo = mem_repo().await?;
    let session = repo.writable_session("main").await?;
    let store = Store::from_session(Arc::new(RwLock::new(session))).await;
    store.set("zarr.json", Bytes::copy_from_slice(GROUP_META)).await?;
    store.set("array/zarr.json", Bytes::copy_from_slice(ARRAY_META)).await?;
    Ok(store)
}

/// Add a group at `path` on `branch` and commit it.
async fn commit_group(
    repo: &Repository,
    branch: &str,
    path: &str,
) -> Result<SnapshotId, Box<dyn std::error::Error>> {
    let mut session = repo.writable_session(branch).await?;
    let path = if path == "/" { Path::root() } else { Path::try_from(path)? };
    session.add_group(path, Bytes::new()).await?;
    Ok(session.commit(branch).execute().await?)
}

/// `storage_info()` fields as a map, so a preset's configuration can be read
/// back without touching the private `config` field or a network.
fn info_fields(
    storage: &(dyn Storage + Send + Sync),
) -> HashMap<String, String> {
    storage
        .storage_info()
        .fields
        .into_iter()
        .map(|(k, v)| (k.to_string(), v))
        .collect()
}

// ---------------------------------------------------------------------------
// Command-line helpers
//
// The candidate's `icechunk` binary is built by the runner with
// `--features cli --bin icechunk` and exported as ICECHUNK_TEST_CLI_BIN.
// ---------------------------------------------------------------------------

struct CliOutput {
    status: Option<i32>,
    stdout: String,
    stderr: String,
}

impl CliOutput {
    fn ok(&self) -> &Self {
        assert_eq!(
            self.status,
            Some(0),
            "expected exit status 0\nstdout:\n{}\nstderr:\n{}",
            self.stdout,
            self.stderr
        );
        self
    }

    fn lines(&self) -> Vec<&str> {
        self.stdout.lines().filter(|l| !l.trim().is_empty()).collect()
    }
}

/// A temporary home for one command-line scenario: a repository directory and
/// an XDG config directory holding `icechunk/cli-config.yaml`, both inside one
/// `TempDir` that is removed when the scenario ends.
struct CliHome {
    dir: tempfile::TempDir,
}

impl CliHome {
    fn new() -> Result<Self, Box<dyn std::error::Error>> {
        let dir = tempfile::tempdir()?;
        std::fs::create_dir_all(dir.path().join("xdg/icechunk"))?;
        std::fs::create_dir_all(dir.path().join("repo"))?;
        let home = CliHome { dir };
        // `RepositoryDefinition` is an externally tagged enum, and serde_yaml_ng
        // renders that as a YAML tag: `!LocalFileSystem`.
        home.write_config(&format!(
            "repos:\n  demo: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            home.repo_path().display()
        ))?;
        Ok(home)
    }

    fn xdg(&self) -> PathBuf {
        self.dir.path().join("xdg")
    }

    fn repo_path(&self) -> PathBuf {
        self.dir.path().join("repo")
    }

    fn config_file(&self) -> PathBuf {
        self.xdg().join("icechunk").join("cli-config.yaml")
    }

    fn write_config(&self, yaml: &str) -> Result<(), Box<dyn std::error::Error>> {
        std::fs::write(self.config_file(), yaml)?;
        Ok(())
    }

    /// Run with XDG_CONFIG_HOME **unset**, so the CLI has to fall back to
    /// `$HOME/.config` (TEST_INTERFACE.md §4.5).
    fn run_without_xdg(
        &self,
        args: &[&str],
    ) -> Result<CliOutput, Box<dyn std::error::Error>> {
        let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
            .map_err(|_| "ICECHUNK_TEST_CLI_BIN is not set")?;
        let out = Command::new(&bin)
            .args(args)
            .env_remove("XDG_CONFIG_HOME")
            .env("HOME", self.dir.path())
            .env_remove("ICECHUNK_LOG")
            .output()?;
        Ok(CliOutput {
            status: out.status.code(),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        })
    }

    /// Run the candidate's `icechunk` binary with this scenario's config
    /// directory, capturing status, stdout and stderr.
    fn run(&self, args: &[&str]) -> Result<CliOutput, Box<dyn std::error::Error>> {
        let bin = std::env::var("ICECHUNK_TEST_CLI_BIN")
            .map_err(|_| "ICECHUNK_TEST_CLI_BIN is not set")?;
        assert!(
            FsPath::new(&bin).is_file(),
            "the candidate's icechunk binary is not at {bin}"
        );
        let out = Command::new(&bin)
            .args(args)
            .env("XDG_CONFIG_HOME", self.xdg())
            .env("HOME", self.dir.path())
            .env_remove("ICECHUNK_LOG")
            .output()?;
        Ok(CliOutput {
            status: out.status.code(),
            stdout: String::from_utf8_lossy(&out.stdout).into_owned(),
            stderr: String::from_utf8_lossy(&out.stderr).into_owned(),
        })
    }

    /// `icechunk repo create demo`, the precondition of every other scenario.
    fn create_repo(&self) -> Result<(), Box<dyn std::error::Error>> {
        self.run(&["repo", "create", "demo"])?.ok();
        Ok(())
    }
}

/// The 20-character Crockford base-32 snapshot id at the head of a
/// `<id>  <name>` listing line.
fn listing_id(line: &str) -> &str {
    let id = line.split_whitespace().next().expect("a non-empty listing line");
    assert_eq!(id.len(), 20, "a snapshot id is 20 characters: {line:?}");
    assert!(
        id.chars().all(|c| c.is_ascii_uppercase() || c.is_ascii_digit()),
        "a snapshot id is Crockford base 32: {line:?}"
    );
    id
}

// ===========================================================================
// NEW CAPABILITY — the header every `inspect_*` entry point now reports
//
// TEST_INTERFACE.md §3.1. Each entry point keeps its signature; what is new is
// the `header` member of the JSON it returns.
// ===========================================================================

#[tokio::test]
async fn n01_inspect_snapshot_header__names_the_writing_library() -> TestResult {
    let (repo, snap) = fixture().await?;
    let h = header_of(&snapshot_json(repo.asset_manager(), &snap, true).await?)?;
    assert_writer_identity(&h);
    Ok(())
}

#[tokio::test]
async fn n01_inspect_snapshot_header__reports_spec_version_type_and_compression()
-> TestResult {
    let (repo, snap) = fixture().await?;
    let h = header_of(&snapshot_json(repo.asset_manager(), &snap, true).await?)?;
    assert_eq!(h["spec_version"], SpecVersionBin::V2.to_string());
    assert_eq!(h["file_type"], format_constants::ICECHUNK_FILE_TYPE_SNAPSHOT);
    assert_eq!(h["compression"], format_constants::ICECHUNK_COMPRESSION_ZSTD);
    Ok(())
}

#[tokio::test]
async fn n01_inspect_snapshot_header__describes_the_file_that_was_read() -> TestResult {
    let (repo, snap) = fixture().await?;
    let h = header_of(&snapshot_json(repo.asset_manager(), &snap, true).await?)?;
    let file = repo.asset_manager().fetch_snapshot_header(&snap).await?;
    assert_matches_file_header(&h, &file);
    Ok(())
}

#[tokio::test]
async fn n01_inspect_snapshot_header__document_keeps_its_body_and_both_renderings()
-> TestResult {
    let (repo, snap) = fixture().await?;
    let pretty = snapshot_json(repo.asset_manager(), &snap, true).await?;
    let compact = snapshot_json(repo.asset_manager(), &snap, false).await?;
    assert!(!compact.contains('\n'), "the compact rendering has no newlines");

    let p: Value = serde_json::from_str(&pretty)?;
    let c: Value = serde_json::from_str(&compact)?;
    assert_eq!(p, c, "pretty and compact render the same document");

    assert_eq!(p["id"], snap.to_string());
    for key in ["id", "header", "flushed_at", "commit_message", "manifests", "nodes"] {
        assert!(p.get(key).is_some(), "the snapshot document must carry `{key}`");
    }
    assert!(p["manifests"].as_array().is_some_and(|m| !m.is_empty()));
    let nodes = p["nodes"].as_array().expect("the snapshot lists its nodes");
    assert!(nodes.len() >= 2, "a root group and one array");

    // An array node reports its grid; a group node has no `shape` at all.
    let array = nodes.iter().find(|n| n["path"] == "/array").expect("the array node");
    let shape = array["shape"].as_array().expect("an array node carries its shape");
    assert_eq!(shape.len(), 1, "the fixture's array has one dimension");
    assert_eq!(shape[0]["array_length"], 4);
    assert_eq!(shape[0]["num_chunks"], 2);
    assert!(shape[0].get("name").is_none(), "an unnamed dimension omits `name`");

    let group = nodes.iter().find(|n| n["path"] == "/").expect("the root group");
    assert_eq!(group["node_type"], "group");
    assert!(group.get("shape").is_none(), "a group node has no shape");
    Ok(())
}

#[tokio::test]
async fn n01_inspect_snapshot_header__unknown_snapshot_is_an_error() -> TestResult {
    let (repo, _) = fixture().await?;
    let missing = SnapshotId::random();
    assert!(
        snapshot_json(repo.asset_manager(), &missing, true).await.is_err(),
        "inspecting a snapshot that was never written must fail"
    );
    Ok(())
}

#[tokio::test]
async fn n02_inspect_repoinfo_header__names_the_writing_library() -> TestResult {
    let (repo, _) = fixture().await?;
    let h = header_of(&repo_info_json(repo.asset_manager(), true).await?)?;
    assert_writer_identity(&h);
    assert_eq!(h["spec_version"], SpecVersionBin::V2.to_string());
    assert_eq!(h["file_type"], format_constants::ICECHUNK_FILE_TYPE_REPO_INFO);
    assert_eq!(h["compression"], format_constants::ICECHUNK_COMPRESSION_ZSTD);
    Ok(())
}

#[tokio::test]
async fn n02_inspect_repoinfo_header__describes_the_file_that_was_read() -> TestResult {
    let (repo, _) = fixture().await?;
    let h = header_of(&repo_info_json(repo.asset_manager(), true).await?)?;
    let file = repo.asset_manager().fetch_repo_info_header().await?;
    assert_matches_file_header(&h, &file);
    Ok(())
}

#[tokio::test]
async fn n02_inspect_repoinfo_header__document_keeps_its_body() -> TestResult {
    let (repo, snap) = fixture().await?;
    let pretty = repo_info_json(repo.asset_manager(), true).await?;
    let compact = repo_info_json(repo.asset_manager(), false).await?;
    assert!(!compact.contains('\n'));
    let v: Value = serde_json::from_str(&pretty)?;
    assert_eq!(serde_json::from_str::<Value>(&compact)?, v);

    assert_eq!(v["branches"]["main"], snap.to_string());
    assert!(v["snapshots"].as_array().is_some_and(|s| !s.is_empty()));
    for key in ["header", "spec_version", "branches", "tags", "snapshots"] {
        assert!(v.get(key).is_some(), "the repo-info document must carry `{key}`");
    }
    Ok(())
}

#[tokio::test]
async fn n03_inspect_manifest_header__names_the_writing_library() -> TestResult {
    let (repo, snap) = fixture().await?;
    let mid = manifest_id_of(&repo, &snap).await?;
    let h = header_of(&manifest_json(repo.asset_manager(), &mid, true).await?)?;
    assert_writer_identity(&h);
    assert_eq!(h["spec_version"], SpecVersionBin::V2.to_string());
    assert_eq!(h["file_type"], format_constants::ICECHUNK_FILE_TYPE_MANIFEST);
    assert_eq!(h["compression"], format_constants::ICECHUNK_COMPRESSION_ZSTD);
    Ok(())
}

#[tokio::test]
async fn n03_inspect_manifest_header__describes_the_file_that_was_read() -> TestResult {
    let (repo, snap) = fixture().await?;
    let mid = manifest_id_of(&repo, &snap).await?;
    let h = header_of(&manifest_json(repo.asset_manager(), &mid, true).await?)?;
    let file = repo.asset_manager().fetch_manifest_header(&mid).await?;
    assert_matches_file_header(&h, &file);
    Ok(())
}

#[tokio::test]
async fn n03_inspect_manifest_header__document_keeps_its_body() -> TestResult {
    let (repo, snap) = fixture().await?;
    let mid = manifest_id_of(&repo, &snap).await?;
    let pretty = manifest_json(repo.asset_manager(), &mid, true).await?;
    let compact = manifest_json(repo.asset_manager(), &mid, false).await?;
    assert!(!compact.contains('\n'));
    let v: Value = serde_json::from_str(&pretty)?;
    assert_eq!(serde_json::from_str::<Value>(&compact)?, v);

    assert_eq!(v["id"], mid.to_string());
    assert_eq!(v["total_chunk_refs"], 1);
    assert_eq!(v["total_inline"], 1);
    for key in ["id", "header", "num_arrays", "total_chunk_refs", "arrays"] {
        assert!(v.get(key).is_some(), "the manifest document must carry `{key}`");
    }
    Ok(())
}

#[tokio::test]
async fn n04_inspect_txlog_header__names_the_writing_library() -> TestResult {
    let (repo, snap) = fixture().await?;
    let h = header_of(&transaction_log_json(repo.asset_manager(), &snap, true).await?)?;
    assert_writer_identity(&h);
    assert_eq!(h["spec_version"], SpecVersionBin::V2.to_string());
    assert_eq!(h["file_type"], format_constants::ICECHUNK_FILE_TYPE_TRANSACTION_LOG);
    assert_eq!(h["compression"], format_constants::ICECHUNK_COMPRESSION_ZSTD);
    Ok(())
}

#[tokio::test]
async fn n04_inspect_txlog_header__describes_the_file_that_was_read() -> TestResult {
    let (repo, snap) = fixture().await?;
    let h = header_of(&transaction_log_json(repo.asset_manager(), &snap, true).await?)?;
    let file = repo.asset_manager().fetch_transaction_log_header(&snap).await?;
    assert_matches_file_header(&h, &file);
    Ok(())
}

#[tokio::test]
async fn n04_inspect_txlog_header__document_keeps_its_body() -> TestResult {
    let (repo, snap) = fixture().await?;
    let pretty = transaction_log_json(repo.asset_manager(), &snap, true).await?;
    let compact = transaction_log_json(repo.asset_manager(), &snap, false).await?;
    assert!(!compact.contains('\n'));
    let v: Value = serde_json::from_str(&pretty)?;
    assert_eq!(serde_json::from_str::<Value>(&compact)?, v);

    for key in ["header", "new_groups", "new_arrays", "updated_chunks"] {
        assert!(v.get(key).is_some(), "the transaction-log document must carry `{key}`");
    }
    assert_eq!(v["new_arrays"].as_array().map(|a| a.len()), Some(1));
    assert_eq!(v["new_groups"].as_array().map(|a| a.len()), Some(1));
    Ok(())
}

#[tokio::test]
async fn n04_inspect_txlog_header__collapsed_ancestry_reports_only_its_own_log()
-> TestResult {
    // TEST_INTERFACE.md §3.1 and PROFILE.md P21: when expiration has collapsed a
    // snapshot's ancestry the document gains `synthetic_composite`, and `header`
    // still describes THIS snapshot's own transaction-log file, not the merged
    // pruned-ancestor logs.
    let (_repo, am, _a, _b, c) = skew_fixture().await?;

    let doc: Value = serde_json::from_str(&transaction_log_json(&am, &c, true).await?)?;
    assert!(
        doc.get("synthetic_composite").is_some(),
        "a collapsed ancestry is reported as a synthetic composite: {doc}"
    );
    let h = doc.get("header").ok_or("the composite document has no `header`")?;
    let own = am.fetch_transaction_log_header(&c).await?;
    assert_matches_file_header(h, &own);

    // An uncollapsed snapshot has no such member.
    let (repo, snap) = fixture().await?;
    let plain: Value =
        serde_json::from_str(&transaction_log_json(repo.asset_manager(), &snap, true).await?)?;
    assert!(
        plain.get("synthetic_composite").is_none(),
        "a snapshot whose ancestry was never collapsed reports no composite"
    );
    Ok(())
}

/// The id of the single manifest the fixture's commit produced, read out of the
/// snapshot's own inspect document.
async fn manifest_id_of(
    repo: &Repository,
    snap: &SnapshotId,
) -> Result<ManifestId, Box<dyn std::error::Error>> {
    let v: Value = serde_json::from_str(&snapshot_json(repo.asset_manager(), snap, false).await?)?;
    let id = v["manifests"][0]["id"].as_str().ok_or("the snapshot lists no manifest")?;
    Ok(ManifestId::try_from(id)?)
}

// ===========================================================================
// NEW CAPABILITY — deleting a chunk key that cannot exist is a no-op, while
// writing one is still an error. TEST_INTERFACE.md §3.3.
// ===========================================================================

#[tokio::test]
async fn n05_delete_impossible_chunk_key__coordinates_outside_the_grid() -> TestResult {
    let store = store_with_array().await?;
    let data = Bytes::from_static(b"hello");
    store.set("array/c/1/1/1", data.clone()).await?;

    store.delete("array/c/10/1/1").await?;

    assert_eq!(store.get("array/c/1/1/1", &ByteRange::ALL).await?, data);
    Ok(())
}

#[tokio::test]
async fn n05_delete_impossible_chunk_key__node_does_not_exist() -> TestResult {
    let store = store_with_array().await?;
    store.delete("no/such/node/c/0/0/0").await?;
    assert!(store.exists("array/zarr.json").await?);
    Ok(())
}

#[tokio::test]
async fn n05_delete_impossible_chunk_key__key_names_a_group() -> TestResult {
    let store = store_with_array().await?;
    // The root is a group, so `c/0` cannot be a chunk of anything.
    store.delete("c/0").await?;
    assert!(store.exists("zarr.json").await?);
    assert!(store.exists("array/zarr.json").await?);
    Ok(())
}

#[tokio::test]
async fn n05_delete_impossible_chunk_key__real_chunk_is_still_deleted() -> TestResult {
    let store = store_with_array().await?;
    store.set("array/c/1/1/1", Bytes::from_static(b"hello")).await?;
    assert!(store.exists("array/c/1/1/1").await?);

    store.delete("array/c/1/1/1").await?;

    assert!(
        !store.exists("array/c/1/1/1").await?,
        "swallowing impossible keys must not swallow a real delete"
    );
    Ok(())
}

#[tokio::test]
async fn n05_delete_impossible_chunk_key__repeated_delete_stays_a_noop() -> TestResult {
    let store = store_with_array().await?;
    for _ in 0..3 {
        store.delete("array/c/10/1/1").await?;
        store.delete("no/such/node/c/0/0/0").await?;
        store.delete("c/0").await?;
    }
    assert!(store.exists("array/zarr.json").await?);
    Ok(())
}

#[tokio::test]
async fn n06_write_outside_grid_errors__store_set_is_rejected() -> TestResult {
    let store = store_with_array().await?;
    let err = store
        .set("array/c/10/1/1", Bytes::from_static(b"hello"))
        .await
        .expect_err("writing outside the chunk grid must be rejected");
    assert!(
        matches!(
            err,
            StoreError {
                kind: StoreErrorKind::SessionError(SessionErrorKind::InvalidIndex {
                    ref coords,
                    ref path,
                }),
                ..
            } if path.to_string() == "/array" && *coords == ChunkIndices(vec![10, 1, 1])
        ),
        "expected an out-of-grid index error, got {err:?}"
    );
    Ok(())
}

#[tokio::test]
async fn n06_write_outside_grid_errors__session_set_chunk_ref_is_rejected() -> TestResult
{
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(4, 2), (4, 2)]).expect("a shape"),
            None,
            Bytes::new(),
        )
        .await?;

    let res = session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![9, 0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"x"))),
        )
        .await;
    match res {
        Err(SessionError { kind: SessionErrorKind::InvalidIndex { coords, path }, .. }) => {
            assert_eq!(coords, ChunkIndices(vec![9, 0]));
            assert_eq!(path, apath);
        }
        other => panic!("expected InvalidIndex, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn n06_write_outside_grid_errors__in_grid_write_still_succeeds() -> TestResult {
    let store = store_with_array().await?;
    let data = Bytes::from_static(b"hello");
    store.set("array/c/0/0/0", data.clone()).await?;
    store.set("array/c/1/1/1", data.clone()).await?;
    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, data);
    assert_eq!(store.get("array/c/1/1/1", &ByteRange::ALL).await?, data);
    Ok(())
}

// ===========================================================================
// NEW CAPABILITY — a zero-length chunk is rejected for every reference kind.
// TEST_INTERFACE.md §3.4.
// ===========================================================================

/// A session holding one 2-dimensional `/array1`, for the zero-length cases.
async fn zero_length_session()
-> Result<(Repository, Session, Path), Box<dyn std::error::Error>> {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array1".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(5, 3), (5, 3)]).expect("a shape"),
            None,
            Bytes::new(),
        )
        .await?;
    Ok((repo, session, apath))
}

#[tokio::test]
async fn n07_zero_length_chunk_rejected__inline_payload() -> TestResult {
    let (_repo, mut session, apath) = zero_length_session().await?;
    let coords = ChunkIndices(vec![0, 0]);
    let res = session
        .set_chunk_ref(apath.clone(), coords.clone(), Some(ChunkPayload::Inline(Bytes::new())))
        .await;
    // `ZeroLengthChunk` is new at this release, so the match lives inside the
    // case rather than in a module-level helper the attribution slice keeps.
    match res {
        Err(SessionError {
            kind: SessionErrorKind::ZeroLengthChunk { coords: c, path },
            ..
        }) => {
            assert_eq!(c, coords);
            assert_eq!(path, apath);
        }
        other => panic!("expected ZeroLengthChunk, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn n07_zero_length_chunk_rejected__virtual_payload() -> TestResult {
    let (_repo, mut session, apath) = zero_length_session().await?;
    let coords = ChunkIndices(vec![0, 0]);
    let payload = ChunkPayload::Virtual(VirtualChunkRef {
        location: VirtualChunkLocation::from_url("s3://bucket/foo.nc")?,
        offset: 0,
        length: 0,
        checksum: None,
    });
    let res = session.set_chunk_ref(apath.clone(), coords.clone(), Some(payload)).await;
    // `ZeroLengthChunk` is new at this release, so the match lives inside the
    // case rather than in a module-level helper the attribution slice keeps.
    match res {
        Err(SessionError {
            kind: SessionErrorKind::ZeroLengthChunk { coords: c, path },
            ..
        }) => {
            assert_eq!(c, coords);
            assert_eq!(path, apath);
        }
        other => panic!("expected ZeroLengthChunk, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn n07_zero_length_chunk_rejected__materialized_payload() -> TestResult {
    let (_repo, mut session, apath) = zero_length_session().await?;
    let coords = ChunkIndices(vec![0, 0]);
    let payload =
        ChunkPayload::Ref(ChunkRef { id: ChunkId::random(), offset: 0, length: 0 });
    let res = session.set_chunk_ref(apath.clone(), coords.clone(), Some(payload)).await;
    // `ZeroLengthChunk` is new at this release, so the match lives inside the
    // case rather than in a module-level helper the attribution slice keeps.
    match res {
        Err(SessionError {
            kind: SessionErrorKind::ZeroLengthChunk { coords: c, path },
            ..
        }) => {
            assert_eq!(c, coords);
            assert_eq!(path, apath);
        }
        other => panic!("expected ZeroLengthChunk, got {other:?}"),
    }
    Ok(())
}

#[tokio::test]
async fn n07_zero_length_chunk_rejected__nonempty_accepted_and_delete_is_the_remedy()
-> TestResult {
    let (_repo, mut session, apath) = zero_length_session().await?;
    let coords = ChunkIndices(vec![0, 0]);

    session
        .set_chunk_ref(
            apath.clone(),
            coords.clone(),
            Some(ChunkPayload::Inline(Bytes::from_static(b"hello"))),
        )
        .await?;
    // `data: None` is untouched: deleting is how "not stored at all" is recorded.
    session.set_chunk_ref(apath.clone(), coords.clone(), None).await?;
    session.delete_chunks(&apath, vec![coords].into_iter()).await?;
    Ok(())
}

#[tokio::test]
async fn n07_zero_length_chunk_rejected__payload_length_is_public() -> TestResult {
    assert_eq!(ChunkPayload::Inline(Bytes::from_static(b"abcd")).length(), 4);
    assert_eq!(ChunkPayload::Inline(Bytes::new()).length(), 0);
    assert_eq!(
        ChunkPayload::Virtual(VirtualChunkRef {
            location: VirtualChunkLocation::from_url("s3://bucket/foo.nc")?,
            offset: 7,
            length: 11,
            checksum: None,
        })
        .length(),
        11
    );
    assert_eq!(
        ChunkPayload::Ref(ChunkRef { id: ChunkId::random(), offset: 3, length: 5 })
            .length(),
        5
    );
    Ok(())
}

#[tokio::test]
async fn n07_zero_length_chunk_rejected__empty_store_value_at_a_chunk_key() -> TestResult
{
    let store = store_with_array().await?;
    assert!(
        store.set("array/c/0/0/0", Bytes::new()).await.is_err(),
        "an empty value at a chunk key is rejected, unlike in a plain key-value store"
    );
    Ok(())
}

// ===========================================================================
// NEW CAPABILITY — the object-metadata key rename. TEST_INTERFACE.md §3.5.
// ===========================================================================

#[tokio::test]
async fn n08_metadata_key_rename__new_spellings_are_exact() -> TestResult {
    use format_constants::{
        ICECHUNK_CLIENT_NAME_METADATA_KEY, ICECHUNK_COMPRESSION_METADATA_KEY,
        ICECHUNK_FILE_TYPE_METADATA_KEY, LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY,
    };
    assert_eq!(LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY, "icspecver");
    assert_eq!(ICECHUNK_CLIENT_NAME_METADATA_KEY, "icclient");
    assert_eq!(ICECHUNK_FILE_TYPE_METADATA_KEY, "icfiletype");
    assert_eq!(ICECHUNK_COMPRESSION_METADATA_KEY, "iccompalg");
    Ok(())
}

#[tokio::test]
async fn n08_metadata_key_rename__deprecated_spellings_are_exact() -> TestResult {
    use format_constants::{
        ICECHUNK_CLIENT_NAME_METADATA_KEY_DEPRECATED,
        ICECHUNK_COMPRESSION_METADATA_KEY_DEPRECATED,
        ICECHUNK_FILE_TYPE_METADATA_KEY_DEPRECATED,
        LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY_DEPRECATED,
    };
    assert_eq!(LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY_DEPRECATED, "ic_spec_ver");
    assert_eq!(ICECHUNK_CLIENT_NAME_METADATA_KEY_DEPRECATED, "ic_client");
    assert_eq!(ICECHUNK_FILE_TYPE_METADATA_KEY_DEPRECATED, "ic_file_type");
    assert_eq!(ICECHUNK_COMPRESSION_METADATA_KEY_DEPRECATED, "ic_comp_alg");
    Ok(())
}

#[tokio::test]
async fn n08_metadata_key_rename__new_keys_survive_header_stripping_proxies()
-> TestResult {
    use format_constants::{
        ICECHUNK_CLIENT_NAME_METADATA_KEY,
        ICECHUNK_CLIENT_NAME_METADATA_KEY_DEPRECATED, ICECHUNK_COMPRESSION_METADATA_KEY,
        ICECHUNK_COMPRESSION_METADATA_KEY_DEPRECATED, ICECHUNK_FILE_TYPE_METADATA_KEY,
        ICECHUNK_FILE_TYPE_METADATA_KEY_DEPRECATED,
        LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY,
        LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY_DEPRECATED,
    };
    let pairs = [
        (
            LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY,
            LATEST_ICECHUNK_FORMAT_VERSION_METADATA_KEY_DEPRECATED,
        ),
        (ICECHUNK_CLIENT_NAME_METADATA_KEY, ICECHUNK_CLIENT_NAME_METADATA_KEY_DEPRECATED),
        (ICECHUNK_FILE_TYPE_METADATA_KEY, ICECHUNK_FILE_TYPE_METADATA_KEY_DEPRECATED),
        (ICECHUNK_COMPRESSION_METADATA_KEY, ICECHUNK_COMPRESSION_METADATA_KEY_DEPRECATED),
    ];
    let mut seen = HashSet::new();
    for (new, old) in pairs {
        assert!(!new.is_empty());
        assert!(
            new.starts_with(|c: char| c.is_ascii_alphabetic())
                && new.chars().all(|c| c.is_ascii_alphanumeric()),
            "{new} must be ASCII alphanumeric and start with a letter"
        );
        assert!(!new.contains('_') && !new.contains('-'), "{new} carries no separator");
        assert!(old.contains('_'), "{old} is the pre-rename spelling");
        assert_ne!(new, old);
        assert!(seen.insert(new), "{new} is used for exactly one datum");
    }
    Ok(())
}

#[tokio::test]
async fn n08_metadata_key_rename__file_type_and_compression_values_are_unchanged()
-> TestResult {
    assert_eq!(format_constants::ICECHUNK_FILE_TYPE_SNAPSHOT, "snapshot");
    assert_eq!(format_constants::ICECHUNK_FILE_TYPE_MANIFEST, "manifest");
    assert_eq!(format_constants::ICECHUNK_FILE_TYPE_TRANSACTION_LOG, "transaction-log");
    assert_eq!(format_constants::ICECHUNK_FILE_TYPE_REPO_INFO, "repo-info");
    assert_eq!(format_constants::ICECHUNK_COMPRESSION_ZSTD, "zstd");
    assert_eq!(
        format_constants::ICECHUNK_CLIENT_NAME.as_str(),
        format!("ic-{}", format_constants::ICECHUNK_LIB_VERSION)
    );
    Ok(())
}

// ===========================================================================
// NEW CAPABILITY — garbage collection is safe under clock skew.
// TEST_INTERFACE.md §3.6.
// ===========================================================================

/// Build a repository whose history is `root <- /a <- /b <- /c`, expire `/b`
/// so `/c` carries `pruned_ancestor_tx_logs = [a, b]`, then delete the branch
/// that held `/c` so only the repository info still references it. Returns the
/// asset manager and the three snapshot ids.
async fn skew_fixture() -> Result<
    (Repository, Arc<icechunk::asset_manager::AssetManager>, SnapshotId, SnapshotId, SnapshotId),
    Box<dyn std::error::Error>,
> {
    let inner: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    // Write latency widens the (flushed_at, created_at] window the cutoff must
    // land inside, without sleeping on a wall clock.
    let storage: Arc<dyn Storage + Send + Sync> =
        Arc::new(icechunk::storage::latency::LatencyStorage::new(inner, 20, 0));
    let repo =
        Repository::create(None, Arc::clone(&storage), HashMap::new(), None, true).await?;
    let am = Arc::clone(repo.asset_manager());

    let a = commit_group(&repo, "main", "/a").await?;
    repo.create_branch("feat", &a).await?;
    let b = commit_group(&repo, "feat", "/b").await?;
    let c = commit_group(&repo, "feat", "/c").await?;

    let result = expire(
        Arc::clone(&am),
        chrono::Utc::now() + chrono::Duration::days(1),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert_eq!(result.released_snapshots.len(), 1);
    assert!(result.edited_snapshots.contains(&c));

    repo.delete_branch("feat").await?;
    Ok((repo, am, a, b, c))
}

/// `/c`'s `created_at` as the object store records it, which is strictly later
/// than the host-stamped `flushed_at` the repository info carries.
async fn created_at_of(
    am: &icechunk::asset_manager::AssetManager,
    id: &SnapshotId,
) -> Result<chrono::DateTime<chrono::Utc>, Box<dyn std::error::Error>> {
    let listed = am.list_snapshots().await?.try_collect::<Vec<_>>().await?;
    Ok(listed
        .into_iter()
        .find(|s| &s.id == id)
        .ok_or("the snapshot is not listed in storage")?
        .created_at)
}

fn clean_all_at(cutoff: chrono::DateTime<chrono::Utc>) -> GCConfig {
    GCConfig::clean_all(
        cutoff,
        cutoff,
        None,
        NonZeroU16::new(50).expect("nonzero"),
        NonZeroUsize::new(512 * 1024 * 1024).expect("nonzero"),
        NonZeroU16::new(500).expect("nonzero"),
        false,
    )
}

#[tokio::test]
async fn n09_gc_clock_skew__referenced_transaction_log_survives() -> TestResult {
    let (_repo, am, a, b, c) = skew_fixture().await?;

    let c_created_at = created_at_of(&am, &c).await?;
    let (info, _) = am.fetch_repo_info().await?;
    assert!(
        info.find_snapshot(&c)?.flushed_at < c_created_at,
        "the fixture must place the cutoff inside the (flushed_at, created_at] window"
    );

    let summary: GCSummary =
        garbage_collect(Arc::clone(&am), &clean_all_at(c_created_at), None, 100).await?;

    assert_eq!(summary.snapshots_deleted, 1, "only the released snapshot's file goes");
    assert_eq!(
        summary.transaction_logs_deleted, 0,
        "a transaction log a retained snapshot still references is never deleted"
    );

    let (info, _) = am.fetch_repo_info().await?;
    assert_eq!(info.find_snapshot(&c)?.pruned_ancestor_tx_logs, vec![a, b.clone()]);
    am.fetch_transaction_log(&b).await?;
    Ok(())
}

#[tokio::test]
async fn n09_gc_clock_skew__a_later_cutoff_still_collects() -> TestResult {
    let (_repo, am, _a, b, c) = skew_fixture().await?;

    // Far past every object's storage `created_at`: nothing is protected by the
    // file gate any more, so the unreachable snapshot and its logs do go.
    let cutoff = chrono::Utc::now() + chrono::Duration::days(1);
    let summary =
        garbage_collect(Arc::clone(&am), &clean_all_at(cutoff), None, 100).await?;
    assert!(
        summary.snapshots_deleted >= 1,
        "an unreferenced snapshot older than the cutoff is collected"
    );
    let (info, _) = am.fetch_repo_info().await?;
    assert!(
        info.find_snapshot(&c).is_err(),
        "the released snapshot leaves the repository info once its file is gone"
    );
    let _ = b;
    Ok(())
}

// ===========================================================================
// NEW CAPABILITY — the Hugging Face storage preset. TEST_INTERFACE.md §3.7.
// No network, no credentials: construction is pure and `storage_info()` is
// synchronous.
// ===========================================================================

#[tokio::test]
async fn n10_hf_storage_preset__sets_the_gateway_endpoint_from_the_namespace()
-> TestResult {
    use icechunk::storage::new_hf_storage;
    let storage = new_hf_storage(
        S3Options::default(),
        "my-bucket".to_string(),
        Some("some/prefix".to_string()),
        "my-namespace",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(storage.as_ref());
    assert_eq!(f["endpoint_url"], "https://s3.hf.co/my-namespace");
    Ok(())
}

#[tokio::test]
async fn n10_hf_storage_preset__defaults_the_region_and_forces_path_style()
-> TestResult {
    use icechunk::storage::new_hf_storage;
    let storage = new_hf_storage(
        S3Options::default(),
        "my-bucket".to_string(),
        Some("some/prefix".to_string()),
        "my-namespace",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(storage.as_ref());
    assert_eq!(f["region"], "us-east-1");
    assert_eq!(f["force_path_style"], "True");
    Ok(())
}

#[tokio::test]
async fn n10_hf_storage_preset__sends_the_bare_bucket_name() -> TestResult {
    use icechunk::storage::new_hf_storage;
    let storage = new_hf_storage(
        S3Options::default(),
        "my-bucket".to_string(),
        Some("some/prefix".to_string()),
        "my-namespace",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let info = storage.storage_info();
    assert_eq!(info.backend_type, "S3 (native)");
    let f = info_fields(storage.as_ref());
    assert_eq!(f["bucket"], "my-bucket", "the namespace lives only in the endpoint path");
    assert_eq!(f["prefix"], "some/prefix");
    Ok(())
}

#[tokio::test]
async fn n10_hf_storage_preset__a_caller_supplied_region_is_kept() -> TestResult {
    use icechunk::storage::new_hf_storage;
    let mut config = S3Options::default();
    config.region = Some("eu-west-3".to_string());
    let storage = new_hf_storage(
        config,
        "my-bucket".to_string(),
        Some("some/prefix".to_string()),
        "other-ns",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(storage.as_ref());
    assert_eq!(f["region"], "eu-west-3", "an explicit region is not overwritten");
    assert_eq!(
        f["endpoint_url"], "https://s3.hf.co/other-ns",
        "the gateway endpoint is always overwritten"
    );
    assert_eq!(f["force_path_style"], "True");
    Ok(())
}

#[tokio::test]
async fn n10_hf_storage_preset__accepts_static_credentials_and_extra_headers()
-> TestResult {
    use icechunk::storage::new_hf_storage;
    let storage = new_hf_storage(
        S3Options::default(),
        "hf-bucket".to_string(),
        None,
        "ns",
        Some(S3Credentials::Static(S3StaticCredentials {
            access_key_id: "HFAKEXAMPLE".to_string(),
            secret_access_key: "secret".to_string(),
            session_token: None,
            expires_after: None,
        })),
        vec![("x-read".to_string(), "1".to_string())],
        vec![("x-write".to_string(), "1".to_string())],
        Some(false),
    )?;
    let f = info_fields(storage.as_ref());
    assert_eq!(f["bucket"], "hf-bucket");
    assert_eq!(f["endpoint_url"], "https://s3.hf.co/ns");
    assert!(!f.contains_key("prefix"), "an empty prefix is not reported");
    Ok(())
}

#[tokio::test]
async fn n10_hf_storage_preset__overwrites_an_endpoint_defaults_credentials_and_writes()
-> TestResult {
    use icechunk::storage::new_hf_storage;
    // TEST_INTERFACE.md §3.7: the gateway endpoint OVERWRITES one supplied in
    // `config`; `credentials: None` means `S3Credentials::FromEnv`; and the
    // storage is writable.
    let supplied = new_hf_storage(
        S3Options::default()
            .with_endpoint_url("https://not-the-gateway.invalid")
            .with_region("eu-west-1"),
        "my-bucket".to_string(),
        None,
        "ns",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(supplied.as_ref());
    assert_eq!(
        f["endpoint_url"], "https://s3.hf.co/ns",
        "Hugging Face runs one gateway, so a supplied endpoint is replaced"
    );
    assert_eq!(f["region"], "eu-west-1", "an explicit region is still kept");

    // `None` credentials fall back to `FromEnv`, which reads nothing at
    // construction time, so the configuration is identical either way.
    let defaulted = new_hf_storage(
        S3Options::default(),
        "my-bucket".to_string(),
        None,
        "ns",
        None,
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let explicit = new_hf_storage(
        S3Options::default(),
        "my-bucket".to_string(),
        None,
        "ns",
        Some(S3Credentials::FromEnv),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(info_fields(defaulted.as_ref()), info_fields(explicit.as_ref()));
    assert_eq!(defaulted.storage_info().backend_type, "S3 (native)");

    assert!(defaulted.can_write().await?, "the preset builds a writable storage");
    Ok(())
}

// ===========================================================================
// NEW CAPABILITY — the command-line interface. TEST_INTERFACE.md §4.
// ===========================================================================

#[tokio::test]
async fn n11_cli_branch__list_prints_the_default_branch_after_create() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let out = home.run(&["branch", "list", "demo"])?;
    out.ok();
    let lines = out.lines();
    assert_eq!(lines.len(), 1, "a fresh repository has exactly one branch: {lines:?}");
    listing_id(lines[0]);
    assert!(lines[0].ends_with("main"), "expected `<id>  main`, got {:?}", lines[0]);
    assert!(!out.stdout.trim_start().starts_with('{'), "the CLI never emits JSON");
    Ok(())
}

#[tokio::test]
async fn n11_cli_branch__create_then_delete_round_trips() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    home.run(&["branch", "create", "demo", "feature", "main"])?.ok();
    let after_create = home.run(&["branch", "list", "demo"])?;
    after_create.ok();
    let names: Vec<String> = after_create
        .lines()
        .iter()
        .map(|l| l.split_whitespace().nth(1).unwrap_or_default().to_string())
        .collect();
    assert_eq!(names.len(), 2);
    assert!(names.contains(&"feature".to_string()));
    assert!(names.contains(&"main".to_string()));
    // A new branch starts at the same snapshot it was created from.
    let ids: HashSet<&str> = after_create.lines().iter().map(|l| listing_id(l)).collect();
    assert_eq!(ids.len(), 1, "`feature` starts where `main` is");

    home.run(&["branch", "delete", "demo", "feature"])?.ok();
    let after_delete = home.run(&["branch", "list", "demo"])?;
    after_delete.ok();
    assert_eq!(after_delete.lines().len(), 1);
    assert!(after_delete.lines()[0].ends_with("main"));
    Ok(())
}

#[tokio::test]
async fn n11_cli_branch__failures_exit_one_and_report_on_stderr() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    // The default branch cannot be deleted.
    let del_main = home.run(&["branch", "delete", "demo", "main"])?;
    assert_eq!(del_main.status, Some(1));
    assert!(del_main.stdout.is_empty(), "an error prints nothing on stdout");
    assert!(!del_main.stderr.is_empty(), "an error is reported on stderr");

    // A reference that resolves to nothing.
    let bad_from = home.run(&["branch", "create", "demo", "other", "nope"])?;
    assert_eq!(bad_from.status, Some(1));
    assert!(bad_from.stderr.contains("nope"));

    // An alias that is not in the config.
    let bad_alias = home.run(&["branch", "list", "missing"])?;
    assert_eq!(bad_alias.status, Some(1));
    assert!(bad_alias.stderr.contains("missing"));
    Ok(())
}

#[tokio::test]
async fn n11_cli_branch__missing_positional_is_a_usage_error() -> TestResult {
    let home = CliHome::new()?;
    let out = home.run(&["branch", "create", "demo", "feature"])?;
    assert_eq!(out.status, Some(2), "clap reports a usage error with status 2");
    assert!(out.stdout.is_empty());
    assert!(!out.stderr.is_empty());
    Ok(())
}

#[tokio::test]
async fn n11_cli_branch__unknown_branch_subcommand_is_a_usage_error() -> TestResult {
    let home = CliHome::new()?;
    let out = home.run(&["branch", "rename", "demo", "a", "b"])?;
    assert_eq!(out.status, Some(2));
    assert!(out.stdout.is_empty());
    Ok(())
}

#[tokio::test]
async fn n12_cli_tag__create_list_delete_round_trips() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let empty = home.run(&["tag", "list", "demo"])?;
    empty.ok();
    assert!(empty.lines().is_empty(), "a fresh repository has no tags");

    let created = home.run(&["tag", "create", "demo", "v1", "main"])?;
    created.ok();
    let listed = home.run(&["tag", "list", "demo"])?;
    listed.ok();
    assert_eq!(listed.lines().len(), 1);
    let id = listing_id(listed.lines()[0]).to_string();
    assert!(listed.lines()[0].ends_with("v1"));
    // TEST_INTERFACE.md §4.4: the confirmation line names the RESOLVED snapshot
    // id, not the reference as it was typed.
    assert!(
        created.stdout.contains(&id),
        "`tag create` must name the resolved snapshot id: {:?}",
        created.stdout
    );
    assert!(!created.stdout.trim_start().starts_with('{'), "the CLI never emits JSON");

    // The tag points at the snapshot `main` pointed at.
    let branches = home.run(&["branch", "list", "demo"])?;
    branches.ok();
    assert_eq!(listing_id(branches.lines()[0]), id);

    home.run(&["tag", "delete", "demo", "v1"])?.ok();
    let after = home.run(&["tag", "list", "demo"])?;
    after.ok();
    assert!(after.lines().is_empty());
    Ok(())
}

#[tokio::test]
async fn n12_cli_tag__create_accepts_a_snapshot_id_and_a_tag_name() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let branches = home.run(&["branch", "list", "demo"])?;
    branches.ok();
    let root = listing_id(branches.lines()[0]).to_string();

    // A raw snapshot id resolves.
    home.run(&["tag", "create", "demo", "byid", &root])?.ok();
    // A tag name resolves too.
    home.run(&["tag", "create", "demo", "byname", "byid"])?.ok();

    let listed = home.run(&["tag", "list", "demo"])?;
    listed.ok();
    let mut names: Vec<&str> = listed
        .lines()
        .iter()
        .map(|l| l.split_whitespace().nth(1).unwrap_or_default())
        .collect();
    names.sort_unstable();
    assert_eq!(names, vec!["byid", "byname"]);
    for line in listed.lines() {
        assert_eq!(listing_id(line), root);
    }
    Ok(())
}

#[tokio::test]
async fn n12_cli_tag__failures_exit_one() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;
    home.run(&["tag", "create", "demo", "v1", "main"])?.ok();

    // A tag name that already exists.
    let again = home.run(&["tag", "create", "demo", "v1", "main"])?;
    assert_eq!(again.status, Some(1));
    assert!(!again.stderr.is_empty());

    // A tag that was never created.
    let missing = home.run(&["tag", "delete", "demo", "nope"])?;
    assert_eq!(missing.status, Some(1));
    assert!(missing.stderr.contains("nope"));

    // A target that resolves to nothing.
    let bad_target = home.run(&["tag", "create", "demo", "v2", "no-such-ref"])?;
    assert_eq!(bad_target.status, Some(1));
    assert!(bad_target.stderr.contains("no-such-ref"));
    Ok(())
}

#[tokio::test]
async fn n13_cli_ancestry__prints_the_snapshot_and_date_of_each_commit() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let out = home.run(&["ancestry", "demo"])?;
    out.ok();
    let text = &out.stdout;
    assert!(!text.trim_start().starts_with('{'), "the CLI never emits JSON");

    let snapshot_lines: Vec<&str> =
        text.lines().filter(|l| l.starts_with("Snapshot: ")).collect();
    assert_eq!(snapshot_lines.len(), 1, "a fresh repository has one commit:\n{text}");
    let id = snapshot_lines[0].trim_start_matches("Snapshot: ");
    assert_eq!(id.len(), 20);
    assert!(id.chars().all(|c| c.is_ascii_uppercase() || c.is_ascii_digit()));

    let date_lines: Vec<&str> = text.lines().filter(|l| l.starts_with("Date: ")).collect();
    assert_eq!(date_lines.len(), 1);
    assert!(date_lines[0].len() > "Date: ".len());
    Ok(())
}

#[tokio::test]
async fn n13_cli_ancestry__reference_defaults_to_main_and_n_bounds_the_output()
-> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let defaulted = home.run(&["ancestry", "demo"])?;
    defaulted.ok();
    let explicit = home.run(&["ancestry", "demo", "main"])?;
    explicit.ok();
    let count = |o: &CliOutput| o.stdout.matches("Snapshot: ").count();
    assert_eq!(count(&defaulted), count(&explicit), "`reference` defaults to `main`");

    let bounded = home.run(&["ancestry", "demo", "main", "-n", "1"])?;
    bounded.ok();
    assert_eq!(count(&bounded), 1);

    let zero = home.run(&["ancestry", "demo", "main", "-n", "0"])?;
    zero.ok();
    assert_eq!(count(&zero), 0, "-n 0 lists nothing");
    Ok(())
}

#[tokio::test]
async fn n13_cli_ancestry__unresolvable_reference_exits_one() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let out = home.run(&["ancestry", "demo", "nope"])?;
    assert_eq!(out.status, Some(1));
    assert!(out.stdout.is_empty());
    assert!(out.stderr.contains("nope"));

    let bad_n = home.run(&["ancestry", "demo", "main", "-n", "not-a-number"])?;
    assert_eq!(bad_n.status, Some(2), "a bad -n is a usage error");

    // TEST_INTERFACE.md §4.2: `-n` is short only. There is no `--n`.
    let long_form = home.run(&["ancestry", "demo", "main", "--n", "1"])?;
    assert_eq!(long_form.status, Some(2), "`--n` is not an option");
    assert!(long_form.stdout.is_empty());
    Ok(())
}

#[tokio::test]
async fn n14_cli_grammar_exit_status__snapshot_list_was_removed() -> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    let out = home.run(&["snapshot", "list", "demo"])?;
    assert_eq!(out.status, Some(2), "`snapshot` is no longer a subcommand");
    assert!(out.stdout.is_empty());
    assert!(!out.stderr.is_empty());

    let bare = home.run(&["snapshot"])?;
    assert_eq!(bare.status, Some(2));
    Ok(())
}

#[tokio::test]
async fn n14_cli_grammar_exit_status__no_subcommand_is_a_usage_error() -> TestResult {
    let home = CliHome::new()?;
    let out = home.run(&[])?;
    assert_eq!(out.status, Some(2));
    let unknown = home.run(&["nonsense"])?;
    assert_eq!(unknown.status, Some(2));
    Ok(())
}

#[tokio::test]
async fn n14_cli_grammar_exit_status__help_exits_zero_on_stdout() -> TestResult {
    let home = CliHome::new()?;
    let out = home.run(&["--help"])?;
    out.ok();
    assert!(!out.stdout.is_empty(), "help goes to stdout");
    for sub in ["ancestry", "branch", "tag", "repo", "config"] {
        assert!(out.stdout.contains(sub), "`{sub}` must appear in the top-level help");
    }

    // `branch` and `tag` each carry the same three operations.
    for group in ["branch", "tag"] {
        let sub = home.run(&[group, "--help"])?;
        sub.ok();
        for op in ["list", "create", "delete"] {
            assert!(
                sub.stdout.contains(op),
                "`{group} {op}` must appear in `{group} --help`:\n{}",
                sub.stdout
            );
        }
    }
    Ok(())
}

#[tokio::test]
async fn n14_cli_grammar_exit_status__config_list_prints_the_config_as_yaml()
-> TestResult {
    let home = CliHome::new()?;
    let out = home.run(&["config", "list"])?;
    out.ok();
    assert!(out.stdout.contains("repos:"));
    assert!(out.stdout.contains("demo"));
    assert!(out.stdout.contains("LocalFileSystem"));
    assert!(!out.stdout.trim_start().starts_with('{'), "the CLI never emits JSON");
    Ok(())
}

#[tokio::test]
async fn n14_cli_grammar_exit_status__an_unparseable_config_is_an_empty_config()
-> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;
    home.write_config("this: is not: a config\n  - at all\n")?;

    // `load_config().unwrap_or_default()` swallows the parse failure, so the
    // repository is simply not in the (empty) config.
    let out = home.run(&["branch", "list", "demo"])?;
    assert_eq!(out.status, Some(1));
    assert!(out.stderr.contains("demo"));

    let listed = home.run(&["config", "list"])?;
    listed.ok();
    assert!(!listed.stdout.contains("LocalFileSystem"));
    Ok(())
}

#[tokio::test]
async fn n14_cli_grammar_exit_status__config_home_fallback_and_round_trip()
-> TestResult {
    let home = CliHome::new()?;
    home.create_repo()?;

    // TEST_INTERFACE.md §4.5: with XDG_CONFIG_HOME unset the directory is
    // `$HOME/.config`. `CliHome`'s HOME is the scenario's own TempDir.
    std::fs::create_dir_all(home.dir.path().join(".config/icechunk"))?;
    std::fs::copy(
        home.config_file(),
        home.dir.path().join(".config/icechunk/cli-config.yaml"),
    )?;
    let fallback = home.run_without_xdg(&["branch", "list", "demo"])?;
    fallback.ok();
    assert_eq!(fallback.lines().len(), 1);
    assert!(fallback.lines()[0].ends_with("main"));

    // …and a config directory with no file in it leaves an empty repository map.
    let empty_home = CliHome::new()?;
    std::fs::remove_file(empty_home.config_file())?;
    let missing = empty_home.run(&["branch", "list", "demo"])?;
    assert_eq!(missing.status, Some(1), "a missing config file is an empty config");
    assert!(missing.stdout.is_empty());

    // TEST_INTERFACE.md §4.5: `config list` round-trips the document — feeding
    // its own output back in leaves every alias resolvable.
    let listed = home.run(&["config", "list"])?;
    listed.ok();
    home.write_config(&listed.stdout)?;
    let after = home.run(&["branch", "list", "demo"])?;
    after.ok();
    assert_eq!(after.lines().len(), 1);
    let relisted = home.run(&["config", "list"])?;
    relisted.ok();
    assert_eq!(relisted.stdout, listed.stdout, "the document is a fixed point");
    Ok(())
}

// ===========================================================================
// NEW CAPABILITY — the manifest/commit concurrency migration.
// TEST_INTERFACE.md §6.4 (`rewrite_manifests` takes `Option<usize>`) and §6.5
// (`ManifestConfig::max_concurrent_manifest_fetches_during_commit`, and
// `CommitBuilder::max_concurrent_nodes(0)` clamped to 1).
//
// These assertions do not belong in an `inherited` group: they reach surface
// this milestone introduces, so an untouched previous-checkpoint workspace
// cannot satisfy them. They are scored here, in the `new` tier.
// ===========================================================================

#[tokio::test]
async fn n15_manifest_commit_concurrency__rewrite_manifests_takes_an_option()
-> TestResult {
    use icechunk::session::CommitMethod;
    let (repo, snap) = fixture().await?;

    // §6.4: `None` means "take the concurrency from the manifest configuration".
    let rewritten =
        rewrite_manifests(&repo, "main", "rewrite", None, None, CommitMethod::NewCommit)
            .await?;
    assert_ne!(rewritten, snap, "a rewrite produces a new snapshot");
    assert_eq!(repo.lookup_branch("main").await?, rewritten);

    let explicit = rewrite_manifests(
        &repo,
        "main",
        "rewrite again",
        Some(2),
        None,
        CommitMethod::NewCommit,
    )
    .await?;
    assert_ne!(explicit, rewritten);
    assert_eq!(repo.lookup_branch("main").await?, explicit);
    Ok(())
}

#[tokio::test]
async fn n15_manifest_commit_concurrency__commit_concurrency_defaults_to_one()
-> TestResult {
    // §6.5: the new field defaults to `None` and the accessor then reports 1.
    assert_eq!(
        ManifestConfig::default().max_concurrent_manifest_fetches_during_commit(),
        1,
        "the effective commit concurrency is always at least 1"
    );

    // §6.5: a concurrency of 0 is clamped to 1 rather than refused, so the
    // commit still goes through. An implementation that does NOT clamp stalls
    // on a zero-permit flush instead of returning, so the call is bounded: the
    // requirement is "returns", and a stall must be reported as a failure
    // rather than left to the runner's own timeout.
    let repo = mem_repo().await?;
    let first = commit_group(&repo, "main", "/a").await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group("/zero".try_into()?, Bytes::new()).await?;
    let clamped = tokio::time::timeout(
        std::time::Duration::from_secs(120),
        session.commit("zero concurrency").max_concurrent_nodes(0).execute(),
    )
    .await
    .map_err(|_| "max_concurrent_nodes(0) must clamp to 1; the commit never returned")??;
    assert_ne!(clamped, first);
    assert_eq!(repo.lookup_branch("main").await?, clamped);
    let session = repo.readonly_session(&VersionInfo::SnapshotId(clamped)).await?;
    assert!(
        session.get_node(&"/zero".try_into()?).await.is_ok(),
        "clamping to 1 must still write every node, not drop the manifests"
    );
    Ok(())
}

// ===========================================================================
// INHERITED — milestone 01: a transactional, versioned Zarr repository.
// TEST_INTERFACE.md §2.1.
// ===========================================================================

#[tokio::test]
async fn i20_m01_repository_refs_store__create_commit_branch_tag_and_time_travel()
-> TestResult {
    let repo = mem_repo().await?;
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        vec!["main".to_string()]
    );

    let first = commit_group(&repo, "main", "/a").await?;
    repo.create_tag("v1", &first).await?;
    let second = commit_group(&repo, "main", "/b").await?;
    assert_ne!(first, second);

    assert_eq!(repo.lookup_branch("main").await?, second);
    assert_eq!(repo.lookup_tag("v1").await?, first);
    assert_eq!(repo.list_tags().await?.into_iter().collect::<Vec<_>>(), vec!["v1"]);

    // Newest first, ending at the initial commit.
    let ancestry = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect::<Vec<_>>()
        .await?;
    let ids: Vec<SnapshotId> = ancestry.iter().map(|s| s.id.clone()).collect();
    assert_eq!(ids.first(), Some(&second));
    assert_eq!(ids.len(), 3, "two commits over the initial one");

    // Time travel: the tagged snapshot still has only `/a`.
    let old = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert!(old.get_node(&"/a".try_into()?).await.is_ok());
    assert!(old.get_node(&"/b".try_into()?).await.is_err());
    Ok(())
}

#[tokio::test]
async fn i20_m01_repository_refs_store__store_round_trips_metadata_and_chunks()
-> TestResult {
    let store = store_with_array().await?;
    let data = Bytes::from_static(b"0123456789");
    store.set("array/c/0/0/0", data.clone()).await?;

    assert_eq!(store.get("array/c/0/0/0", &ByteRange::ALL).await?, data);
    assert_eq!(
        store.get("array/c/0/0/0", &ByteRange::bounded(2, 5)).await?,
        Bytes::from_static(b"234")
    );
    assert!(store.exists("zarr.json").await?);
    assert!(!store.exists("array/c/1/0/0").await?);

    let mut keys = store.list().await?.try_collect::<Vec<_>>().await?;
    keys.sort();
    assert_eq!(keys, vec!["array/c/0/0/0", "array/zarr.json", "zarr.json"]);

    let mut dir = store.list_dir("").await?.try_collect::<Vec<_>>().await?;
    dir.sort();
    assert_eq!(dir, vec!["array", "zarr.json"]);
    Ok(())
}

#[tokio::test]
async fn i20_m01_repository_refs_store__paths_and_object_ids() -> TestResult {
    assert_eq!(Path::root().to_string(), "/");
    assert_eq!(Path::new("/a/b")?.to_string(), "/a/b");
    assert!(Path::new("a/b").is_err(), "a relative path is rejected");
    assert!(Path::new("/a/../b").is_err(), "a non-canonical path is rejected");

    let id = SnapshotId::random();
    let rendered = id.to_string();
    assert_eq!(rendered.len(), 20, "a 12-byte id is 20 Crockford base-32 characters");
    assert_eq!(SnapshotId::try_from(rendered.as_str())?, id);
    assert!(SnapshotId::try_from(&[0u8; 5][..]).is_err(), "a short slice is rejected");
    Ok(())
}

#[tokio::test]
async fn i20_m01_repository_refs_store__reference_rules_time_travel_and_delete_dir()
-> TestResult {
    let repo = mem_repo().await?;
    let first = commit_group(&repo, "main", "/a").await?;

    // PROFILE.md P6: a reference cannot be created at a snapshot that does not
    // exist, a tag name cannot be reused, and a deleted tag's name is retired
    // for good.
    let ghost = SnapshotId::random();
    assert!(
        repo.create_branch("ghost", &ghost).await.is_err(),
        "a branch cannot start at a snapshot that was never written"
    );
    assert!(
        repo.create_tag("ghost", &ghost).await.is_err(),
        "a tag cannot point at a snapshot that was never written"
    );

    repo.create_tag("v1", &first).await?;
    assert!(repo.create_tag("v1", &first).await.is_err(), "a tag name is unique");
    repo.delete_tag("v1").await?;
    assert!(
        repo.create_tag("v1", &first).await.is_err(),
        "a deleted tag's name is retired permanently"
    );

    // PROFILE.md P6: `delete_branch("main")` fails. P11 fixes the granularity:
    // the category, never the message.
    {
        use icechunk::repository::{RepositoryError, RepositoryErrorKind};
        let refused = repo.delete_branch("main").await;
        assert!(
            matches!(
                refused,
                Err(RepositoryError { kind: RepositoryErrorKind::CannotDeleteMain, .. })
            ),
            "deleting the `main` branch must be refused, got {refused:?}"
        );
        assert_eq!(
            repo.lookup_branch("main").await?,
            first,
            "and `main` still resolves after the refusal"
        );
    }

    // TEST_INTERFACE.md §2.1: `VersionInfo::AsOf` reads a branch as it stood at
    // a moment in time.
    let second = commit_group(&repo, "main", "/b").await?;
    let at_second = repo.lookup_snapshot(&second).await?.flushed_at;
    let now = repo
        .readonly_session(&VersionInfo::AsOf {
            branch: "main".to_string(),
            at: at_second + chrono::Duration::days(1),
        })
        .await?;
    assert!(now.get_node(&"/b".try_into()?).await.is_ok());
    let before = repo
        .readonly_session(&VersionInfo::AsOf {
            branch: "main".to_string(),
            at: at_second - chrono::Duration::milliseconds(1),
        })
        .await?;
    assert!(
        before.get_node(&"/b".try_into()?).await.is_err(),
        "as of a moment before the second commit, only the first is visible"
    );

    // TEST_INTERFACE.md §2.1: `Store::delete_dir` removes a whole subtree.
    let store = store_with_array().await?;
    store.set("array/c/0/0/0", Bytes::from_static(b"x")).await?;
    store.delete_dir("array").await?;
    assert!(!store.exists("array/zarr.json").await?, "the subtree is gone");
    assert!(!store.exists("array/c/0/0/0").await?);
    assert!(store.exists("zarr.json").await?, "and nothing outside it is touched");
    Ok(())
}

// ===========================================================================
// INHERITED — milestone 02: sessions, conflict detection and rebase, and
// virtual chunk containers. TEST_INTERFACE.md §2.2.
// ===========================================================================

#[tokio::test]
async fn i21_m02_sessions_conflicts_virtual__concurrent_commits_conflict_and_rebase()
-> TestResult {
    let repo = mem_repo().await?;
    let mut setup = repo.writable_session("main").await?;
    setup.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    setup
        .add_array(apath.clone(), ArrayShape::new(vec![(4, 2)]).expect("shape"), None, Bytes::new())
        .await?;
    setup.commit("setup").execute().await?;

    let mut ours = repo.writable_session("main").await?;
    let mut theirs = repo.writable_session("main").await?;
    ours.set_chunk_ref(
        apath.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"ours"))),
    )
    .await?;
    theirs
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"theirs"))),
        )
        .await?;

    theirs.commit("theirs").execute().await?;
    let conflict = ours.commit("ours").execute().await;
    assert!(
        matches!(conflict, Err(SessionError { kind: SessionErrorKind::Conflict { .. }, .. })),
        "a stale parent must be reported as a conflict, got {conflict:?}"
    );

    let solver = icechunk::conflicts::basic_solver::BasicConflictSolver {
        on_chunk_conflict: icechunk::conflicts::basic_solver::VersionSelection::UseOurs,
        ..Default::default()
    };
    ours.rebase(&solver).await?;
    ours.commit("ours, rebased").execute().await?;

    let session = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    assert_eq!(
        session.get_chunk_ref(&apath, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline(Bytes::from_static(b"ours")))
    );

    // TEST_INTERFACE.md §2.2: the conflict vocabulary and the detector are
    // public beside the basic solver, so a caller can plug in its own policy.
    // The bare detector resolves nothing, so rebasing through it must fail.
    {
        use icechunk::conflicts::{
            Conflict, ConflictResolution, ConflictSolver, detector::ConflictDetector,
        };
        #[allow(dead_code)]
        type PublicConflict = Conflict;
        #[allow(dead_code)]
        type PublicResolution = ConflictResolution;

        let mut stale = repo.writable_session("main").await?;
        let mut winner = repo.writable_session("main").await?;
        stale
            .set_chunk_ref(
                apath.clone(),
                ChunkIndices(vec![1]),
                Some(ChunkPayload::Inline(Bytes::from_static(b"stale"))),
            )
            .await?;
        winner
            .set_chunk_ref(
                apath.clone(),
                ChunkIndices(vec![1]),
                Some(ChunkPayload::Inline(Bytes::from_static(b"winner"))),
            )
            .await?;
        winner.commit("winner").execute().await?;
        assert!(stale.commit("stale").execute().await.is_err());

        let bare: &(dyn ConflictSolver + Send + Sync) = &ConflictDetector;
        assert!(
            stale.rebase(bare).await.is_err(),
            "the bare detector reports conflicts instead of resolving them"
        );
    }
    Ok(())
}

#[tokio::test]
async fn i21_m02_sessions_conflicts_virtual__a_committed_session_is_read_only()
-> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    assert!(!session.read_only());
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("once").execute().await?;

    assert!(session.read_only(), "a committed session becomes read-only");
    assert!(session.branch().is_none());
    assert!(session.add_group("/again".try_into()?, Bytes::new()).await.is_err());
    Ok(())
}

#[tokio::test]
async fn i21_m02_sessions_conflicts_virtual__virtual_chunk_reads_from_an_authorized_container()
-> TestResult {
    use icechunk::config::{Credentials, ObjectStoreConfig};
    use icechunk::virtual_chunks::VirtualChunkContainer;

    let chunk_dir = tempfile::tempdir()?;
    let payload = Bytes::from_static(b"virtual-chunk-bytes");
    std::fs::write(chunk_dir.path().join("data.bin"), &payload)?;
    let prefix = format!("file://{}/", chunk_dir.path().to_str().ok_or("utf8 path")?);

    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(VirtualChunkContainer::new(
        prefix.clone(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    )?)?;
    let mut creds = HashMap::new();
    creds.insert(prefix.clone(), Some(Credentials::LocalFileSystemAccess));

    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo = Repository::create(Some(config), storage, creds, None, true).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/virtual".try_into()?;
    session
        .add_array(apath.clone(), ArrayShape::new(vec![(1, 1)]).expect("shape"), None, Bytes::new())
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_url(&format!("{prefix}data.bin"))?,
                offset: 0,
                length: payload.len() as u64,
                checksum: None,
            })),
        )
        .await?;

    let reader = session.get_chunk_reader(&apath, &ChunkIndices(vec![0]), &ByteRange::ALL).await?;
    let got = icechunk::session::get_chunk(reader).await?;
    assert_eq!(got.as_ref(), Some(&payload));

    // A reference outside every authorized prefix is refused.
    let outside: Path = "/outside".try_into()?;
    session
        .add_array(outside.clone(), ArrayShape::new(vec![(1, 1)]).expect("shape"), None, Bytes::new())
        .await?;
    session
        .set_chunk_ref(
            outside.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_url("s3://not-authorized/x.bin")?,
                offset: 0,
                length: 4,
                checksum: None,
            })),
        )
        .await?;
    let reader =
        session.get_chunk_reader(&outside, &ChunkIndices(vec![0]), &ByteRange::ALL).await?;
    assert!(
        icechunk::session::get_chunk(reader).await.is_err(),
        "an unauthorized container must not be read"
    );
    Ok(())
}

// ===========================================================================
// INHERITED — milestone 03: expiration, garbage collection and the
// self-describing metadata file header. TEST_INTERFACE.md §2.3.
// ===========================================================================

#[tokio::test]
async fn i22_m03_expiration_gc_headers__expire_then_collect_releases_files() -> TestResult
{
    let repo = mem_repo().await?;
    let _a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let c = commit_group(&repo, "main", "/c").await?;
    let am = Arc::clone(repo.asset_manager());

    let result = expire(
        Arc::clone(&am),
        chrono::Utc::now() + chrono::Duration::days(1),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;
    assert!(!result.released_snapshots.is_empty(), "older snapshots are released");
    assert!(result.edited_snapshots.contains(&c), "the surviving tip is re-parented");

    let summary = garbage_collect(
        Arc::clone(&am),
        &clean_all_at(chrono::Utc::now() + chrono::Duration::days(1)),
        None,
        100,
    )
    .await?;
    assert!(summary.snapshots_deleted >= 1);

    // `main` still resolves and its history is now the squashed chain.
    assert_eq!(repo.lookup_branch("main").await?, c);
    assert!(am.fetch_snapshot(&b).await.is_err(), "a collected snapshot file is gone");
    Ok(())
}

#[tokio::test]
async fn i22_m03_expiration_gc_headers__every_metadata_file_carries_a_header()
-> TestResult {
    let (repo, snap) = fixture().await?;
    let am = repo.asset_manager();

    let snapshot = am.fetch_snapshot_header(&snap).await?;
    assert_eq!(snapshot.file_type, FileTypeBin::Snapshot);
    assert_eq!(snapshot.spec_version, SpecVersionBin::V2);
    assert_eq!(snapshot.compression, CompressionAlgorithmBin::Zstd);
    assert_eq!(snapshot.app_name, "ic");

    let repo_info = am.fetch_repo_info_header().await?;
    assert_eq!(repo_info.file_type, FileTypeBin::RepoInfo);

    let txlog = am.fetch_transaction_log_header(&snap).await?;
    assert_eq!(txlog.file_type, FileTypeBin::TransactionLog);

    let mid = manifest_id_of(&repo, &snap).await?;
    let manifest = am.fetch_manifest_header(&mid).await?;
    assert_eq!(manifest.file_type, FileTypeBin::Manifest);
    Ok(())
}

#[tokio::test]
async fn i22_m03_expiration_gc_headers__the_header_parser_is_public_and_strict()
-> TestResult {
    use format_constants::{
        ICECHUNK_FILE_HEADER_LEN, ICECHUNK_FORMAT_MAGIC_BYTES, ICECHUNK_IMPL_NAME_LEN,
        parse_file_header,
    };
    assert_eq!(ICECHUNK_FORMAT_MAGIC_BYTES.len(), 12);
    assert_eq!(ICECHUNK_IMPL_NAME_LEN, 24);
    assert_eq!(ICECHUNK_FILE_HEADER_LEN, 39);

    let mut buf = Vec::with_capacity(ICECHUNK_FILE_HEADER_LEN);
    buf.extend_from_slice(ICECHUNK_FORMAT_MAGIC_BYTES);
    let name = format_constants::ICECHUNK_CLIENT_NAME.as_bytes();
    buf.extend_from_slice(name);
    buf.extend(std::iter::repeat_n(b' ', ICECHUNK_IMPL_NAME_LEN - name.len()));
    buf.push(SpecVersionBin::V2 as u8);
    buf.push(FileTypeBin::Manifest as u8);
    buf.push(CompressionAlgorithmBin::Zstd as u8);
    buf.extend_from_slice(b"a compressed body would follow");

    let header = parse_file_header(&buf)?;
    assert_eq!(header.impl_name, *format_constants::ICECHUNK_CLIENT_NAME);
    assert_eq!(header.app_name, "ic");
    assert_eq!(header.app_version.as_deref(), Some(format_constants::ICECHUNK_LIB_VERSION));
    assert_eq!(header.spec_version, SpecVersionBin::V2);
    assert_eq!(header.file_type, FileTypeBin::Manifest);
    assert_eq!(header.compression, CompressionAlgorithmBin::Zstd);

    assert!(parse_file_header(&buf[..10]).is_err(), "a truncated header is rejected");
    let mut bad = buf.clone();
    bad[0] = b'X';
    assert!(parse_file_header(&bad).is_err(), "a bad magic is rejected");
    Ok(())
}

// ===========================================================================
// INHERITED — milestones 04 and 06: the FlatBuffers metadata format and
// configurable manifest splitting. TEST_INTERFACE.md §2.4.
// ===========================================================================

#[tokio::test]
async fn i23_m04_m06_format_manifest_splitting__snapshot_buffer_round_trips()
-> TestResult {
    use icechunk::format::snapshot::Snapshot;
    let (repo, snap) = fixture().await?;
    let snapshot = repo.asset_manager().fetch_snapshot(&snap).await?;

    let bytes = snapshot.bytes().to_vec();
    let parsed = Snapshot::from_buffer(SpecVersionBin::V2, bytes.clone())?;
    assert_eq!(parsed.id(), snap);
    assert_eq!(parsed.message(), "first");
    assert!(parsed.get_node(&"/array".try_into()?).is_ok());

    assert!(
        Snapshot::from_buffer(SpecVersionBin::V2, bytes[..bytes.len() / 2].to_vec())
            .is_err(),
        "a truncated buffer must be rejected before any access"
    );
    Ok(())
}

#[tokio::test]
async fn i23_m04_m06_format_manifest_splitting__user_data_is_opaque_and_verbatim()
-> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let user_data = Bytes::from_static(br#"{"unknown_zarr_key":[1,2,3],"x":null}"#);
    let apath: Path = "/array".try_into()?;
    session
        .add_array(
            apath.clone(),
            ArrayShape::new(vec![(8, 4)]).expect("shape"),
            Some(vec!["t".into()]),
            user_data.clone(),
        )
        .await?;
    let snap = session.commit("with user data").execute().await?;

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let node = session.get_array(&apath).await?;
    assert_eq!(node.user_data, user_data, "user data round-trips byte for byte");

    // TEST_INTERFACE.md §2.4: a node carries its kind in `node_data`.
    use icechunk::format::snapshot::{NodeData, NodeType};
    match &node.node_data {
        NodeData::Array { dimension_names, .. } => {
            assert_eq!(
                dimension_names.as_ref().map(|d| d.len()),
                Some(1),
                "the dimension name given at `add_array` survives"
            );
        }
        NodeData::Group => panic!("/array is an array, not a group"),
    }
    let root = session.get_node(&Path::root()).await?;
    assert!(matches!(root.node_data, NodeData::Group));
    assert_eq!(NodeType::Array, NodeType::Array);
    assert_ne!(NodeType::Group, NodeType::Array);
    Ok(())
}

#[tokio::test]
async fn i23_m04_m06_format_manifest_splitting__splitting_produces_several_manifests()
-> TestResult {
    async fn manifests_with(
        manifest: ManifestConfig,
    ) -> Result<usize, Box<dyn std::error::Error>> {
        let mut config = RepositoryConfig::default();
        config.manifest = Some(manifest);
        let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
        let repo = Repository::create(Some(config), storage, HashMap::new(), None, true).await?;
        let mut session = repo.writable_session("main").await?;
        session.add_group(Path::root(), Bytes::new()).await?;
        let apath: Path = "/array".try_into()?;
        session
            .add_array(apath.clone(), ArrayShape::new(vec![(8, 8)]).expect("shape"), None, Bytes::new())
            .await?;
        for i in 0..8u32 {
            session
                .set_chunk_ref(
                    apath.clone(),
                    ChunkIndices(vec![i]),
                    Some(ChunkPayload::Inline(Bytes::from_static(b"x"))),
                )
                .await?;
        }
        let snap = session.commit("chunks").execute().await?;
        let manifests = repo.lookup_manifest_files(&snap).await?.count();
        Ok(manifests)
    }

    let mut split = ManifestConfig::default();
    split.splitting = Some(ManifestSplittingConfig {
        split_sizes: Some(vec![(
            ManifestSplitCondition::AnyArray,
            vec![icechunk::config::ManifestSplitDim {
                condition: ManifestSplitDimCondition::Any,
                num_chunks: 2,
            }],
        )]),
    });
    assert!(manifests_with(split).await? > 1, "splitting must produce several manifests");
    assert_eq!(
        manifests_with(ManifestConfig::empty()).await?,
        1,
        "without splitting one array writes one manifest"
    );
    Ok(())
}

// ===========================================================================
// INHERITED — milestone 05: the command-line interface, repository-level
// commit metadata and byte-level storage accounting. TEST_INTERFACE.md §2.5.
// ===========================================================================

#[tokio::test]
async fn i24_m05_cli_config_accounting__cli_repo_create_initialises_a_repository()
-> TestResult {
    let home = CliHome::new()?;
    let out = home.run(&["repo", "create", "demo"])?;
    out.ok();
    assert!(!out.stdout.is_empty(), "repo create reports what it did on stdout");

    assert!(home.repo_path().join("repo").is_file(), "the table of contents exists");
    assert!(home.repo_path().join("snapshots").is_dir());
    assert!(
        std::fs::read_dir(home.repo_path().join("snapshots"))?.next().is_some(),
        "the initial snapshot was written"
    );

    // The same repository is now readable through the library.
    let storage = new_local_filesystem_storage(&home.repo_path()).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    assert_eq!(repo.list_branches().await?.len(), 1);
    Ok(())
}

#[tokio::test]
async fn i24_m05_cli_config_accounting__default_commit_metadata_merges_under_user_keys()
-> TestResult {
    let mut repo = mem_repo().await?;
    let mut defaults = icechunk::format::snapshot::SnapshotProperties::new();
    defaults.insert("author".into(), serde_json::json!("ci"));
    defaults.insert("source".into(), serde_json::json!("default"));
    repo.set_default_commit_metadata(defaults);

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let mut props = icechunk::format::snapshot::SnapshotProperties::new();
    props.insert("source".into(), serde_json::json!("explicit"));
    let snap = session.commit("with metadata").properties(props).execute().await?;

    let info = repo.lookup_snapshot(&snap).await?;
    assert_eq!(info.metadata.get("author"), Some(&serde_json::json!("ci")));
    assert_eq!(
        info.metadata.get("source"),
        Some(&serde_json::json!("explicit")),
        "a user property wins over the repository default"
    );
    assert_eq!(info.message, "with metadata");
    Ok(())
}

#[tokio::test]
async fn i24_m05_cli_config_accounting__chunk_storage_is_accounted_per_chunk_kind()
-> TestResult {
    let mut config = RepositoryConfig::default();
    config.inline_chunk_threshold_bytes = Some(0);
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    let repo = Repository::create(Some(config), storage, HashMap::new(), None, true).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let apath: Path = "/array".try_into()?;
    session
        .add_array(apath.clone(), ArrayShape::new(vec![(4, 4)]).expect("shape"), None, Bytes::new())
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"1234"))),
        )
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Ref(ChunkRef {
                id: ChunkId::random(),
                offset: 0,
                length: 100,
            })),
        )
        .await?;
    session
        .set_chunk_ref(
            apath.clone(),
            ChunkIndices(vec![2]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_url("s3://bucket/f.nc")?,
                offset: 0,
                length: 1000,
                checksum: None,
            })),
        )
        .await?;
    session.commit("three kinds").execute().await?;

    let stats = repo_chunks_storage(
        Arc::clone(repo.asset_manager()),
        NonZeroU16::new(50).expect("nonzero"),
        NonZeroUsize::new(512 * 1024 * 1024).expect("nonzero"),
        NonZeroU16::new(500).expect("nonzero"),
    )
    .await?;
    assert_eq!(stats.inlined_bytes, 4);
    assert_eq!(stats.native_bytes, 100);
    assert_eq!(stats.virtual_bytes, 1000);

    // The three kinds are discriminated through the chunk reference itself.
    let session = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    assert!(matches!(
        session.get_chunk_ref(&apath, &ChunkIndices(vec![0])).await?,
        Some(ChunkPayload::Inline(_))
    ));
    assert!(matches!(
        session.get_chunk_ref(&apath, &ChunkIndices(vec![1])).await?,
        Some(ChunkPayload::Ref(_))
    ));
    assert!(matches!(
        session.get_chunk_ref(&apath, &ChunkIndices(vec![2])).await?,
        Some(ChunkPayload::Virtual(_))
    ));
    assert_eq!(session.get_chunk_ref(&apath, &ChunkIndices(vec![3])).await?, None);
    Ok(())
}

// ===========================================================================
// INHERITED — milestones 07 and 08: virtual-container prefix discipline, a
// dry-run garbage collection, and configuration merge semantics.
// TEST_INTERFACE.md §2.6.
// ===========================================================================

#[tokio::test]
async fn i25_m07_m08_async_backends__container_prefix_must_end_in_a_slash() -> TestResult {
    use icechunk::config::ObjectStoreConfig;
    use icechunk::virtual_chunks::VirtualChunkContainer;

    assert!(
        VirtualChunkContainer::new(
            "s3://bucket/data".to_string(),
            ObjectStoreConfig::S3(S3Options::default()),
        )
        .is_err(),
        "a prefix that does not end in `/` must be rejected"
    );
    let ok = VirtualChunkContainer::new(
        "s3://bucket/data/".to_string(),
        ObjectStoreConfig::S3(S3Options::default()),
    )?;
    assert_eq!(ok.url_prefix(), "s3://bucket/data/");
    Ok(())
}

#[tokio::test]
async fn i25_m07_m08_async_backends__gc_dry_run_reports_without_deleting() -> TestResult {
    let repo = mem_repo().await?;
    let _a = commit_group(&repo, "main", "/a").await?;
    let b = commit_group(&repo, "main", "/b").await?;
    let am = Arc::clone(repo.asset_manager());
    expire(
        Arc::clone(&am),
        chrono::Utc::now() + chrono::Duration::days(1),
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
        None,
        100,
    )
    .await?;

    let cutoff = chrono::Utc::now() + chrono::Duration::days(1);
    let dry = GCConfig::clean_all(
        cutoff,
        cutoff,
        None,
        NonZeroU16::new(50).expect("nonzero"),
        NonZeroUsize::new(512 * 1024 * 1024).expect("nonzero"),
        NonZeroU16::new(500).expect("nonzero"),
        true,
    );
    assert!(dry.action_needed());
    assert!(dry.deletes_snapshots());
    let summary = garbage_collect(Arc::clone(&am), &dry, None, 100).await?;
    assert!(summary.snapshots_deleted >= 1, "a dry run still reports what it would do");

    // Nothing was actually removed: the branch tip is still fetchable.
    let tip = repo.lookup_branch("main").await?;
    assert_eq!(tip, b);
    am.fetch_snapshot(&tip).await?;
    Ok(())
}

#[tokio::test]
async fn i25_m07_m08_async_backends__config_merge_replaces_virtual_chunk_containers()
-> TestResult {
    use icechunk::config::ObjectStoreConfig;
    use icechunk::virtual_chunks::VirtualChunkContainer;

    let mut base = RepositoryConfig::default();
    base.set_virtual_chunk_container(VirtualChunkContainer::new(
        "s3://one/".to_string(),
        ObjectStoreConfig::S3(S3Options::default()),
    )?)?;
    base.inline_chunk_threshold_bytes = Some(16);

    let mut other = RepositoryConfig::default();
    other.set_virtual_chunk_container(VirtualChunkContainer::new(
        "s3://two/".to_string(),
        ObjectStoreConfig::S3(S3Options::default()),
    )?)?;

    let merged = base.merge(other);
    let prefixes: Vec<&str> =
        merged.virtual_chunk_containers().map(|c| c.url_prefix()).collect();
    assert_eq!(prefixes, vec!["s3://two/"], "containers are replaced, not unioned");
    assert_eq!(
        merged.inline_chunk_threshold_bytes(),
        16,
        "an unset field falls back to the left-hand side"
    );
    assert_eq!(RepositoryConfig::default().inline_chunk_threshold_bytes(), 512);
    assert_eq!(RepositoryConfig::default().max_concurrent_requests(), 256);
    Ok(())
}

#[tokio::test]
async fn i25_m07_m08_async_backends__listing_takes_a_parent_and_rejects_bad_prefixes()
-> TestResult {
    let store = store_with_array().await?;
    store.set("array/c/0/0/0", Bytes::from_static(b"x")).await?;

    let session = store.session();
    let guard = session.read().await;
    let nodes = guard.list_nodes(&Path::root()).await?;
    let mut paths: Vec<String> = nodes
        .map(|n| n.map(|n| n.path.to_string()))
        .collect::<Result<Vec<_>, _>>()?;
    paths.sort();
    assert_eq!(paths, vec!["/", "/array"], "listing includes the parent itself");
    drop(guard);

    let mut under = store.list_prefix("array").await?.try_collect::<Vec<_>>().await?;
    under.sort();
    assert_eq!(under, vec!["array/c/0/0/0", "array/zarr.json"]);

    let mut dir = store.list_dir("array").await?.try_collect::<Vec<_>>().await?;
    dir.sort();
    assert_eq!(dir, vec!["c", "zarr.json"]);

    match store.list_prefix("array/c/0").await {
        Err(StoreError { kind: StoreErrorKind::BadKeyPrefix { .. }, .. }) => {}
        Err(other) => panic!("expected BadKeyPrefix, got {other:?}"),
        Ok(_) => panic!("a prefix that is neither the root nor a node must be rejected"),
    }

    // TEST_INTERFACE.md §2.6: `list_dir` does NOT reject. Inside an array's
    // chunk space it walks up to the closest ancestor array; with no ancestor
    // node at all it simply yields nothing.
    let inside = store.list_dir("array/c/0").await?.try_collect::<Vec<_>>().await?;
    assert!(
        !inside.is_empty(),
        "list_dir inside the chunk space walks up to the array: {inside:?}"
    );
    let nowhere = store.list_dir("no/such/node").await?.try_collect::<Vec<_>>().await?;
    assert!(nowhere.is_empty(), "a prefix with no ancestor node yields nothing");
    Ok(())
}

// ===========================================================================
// INHERITED — milestone 09: the Icechunk-2 table of contents.
// TEST_INTERFACE.md §2.7.
// ===========================================================================

#[tokio::test]
async fn i26_m09_table_of_contents__ops_log_records_every_mutation() -> TestResult {
    use icechunk::format::repo_info::UpdateType;
    let repo = mem_repo().await?;
    let snap = commit_group(&repo, "main", "/a").await?;
    repo.create_tag("v1", &snap).await?;

    let (stream, _info, _version) = repo.ops_log().await?;
    let entries = stream.try_collect::<Vec<_>>().await?;
    assert!(entries.len() >= 3, "initialisation, the commit and the tag: {entries:?}");

    let kinds: Vec<&UpdateType> = entries.iter().map(|(_, u, _)| u).collect();
    assert!(matches!(kinds.first(), Some(UpdateType::TagCreatedUpdate { name }) if name == "v1"),
        "the newest update comes first: {kinds:?}");
    assert!(kinds.iter().any(|u| matches!(u, UpdateType::NewCommitUpdate { .. })));
    assert!(kinds.iter().any(|u| matches!(u, UpdateType::RepoInitializedUpdate)));
    Ok(())
}

#[tokio::test]
async fn i26_m09_table_of_contents__status_metadata_and_feature_flags_live_in_it()
-> TestResult {
    use icechunk::format::repo_info::{RepoAvailability, RepoStatus};
    let repo = mem_repo().await?;

    assert_eq!(repo.get_status().await?.availability, RepoAvailability::Online);
    assert!(repo.get_metadata().await?.is_empty());

    let mut metadata = icechunk::format::snapshot::SnapshotProperties::new();
    metadata.insert("owner".into(), serde_json::json!("team"));
    repo.set_metadata(&metadata).await?;
    assert_eq!(repo.get_metadata().await?.get("owner"), Some(&serde_json::json!("team")));

    let flags: Vec<String> =
        repo.feature_flags().await?.map(|f| f.name().to_string()).collect();
    assert!(!flags.is_empty(), "the repository declares its feature flags");

    repo.set_status(&RepoStatus {
        availability: RepoAvailability::ReadOnly,
        set_at: chrono::Utc::now(),
        limited_availability_reason: Some("maintenance".into()),
    })
    .await?;
    assert_eq!(repo.get_status().await?.availability, RepoAvailability::ReadOnly);
    assert!(
        repo.writable_session("main").await.is_err(),
        "a read-only repository refuses a writable session"
    );
    Ok(())
}

#[tokio::test]
async fn i26_m09_table_of_contents__commit_builder_amend_and_anonymous() -> TestResult {
    let repo = mem_repo().await?;
    let first = commit_group(&repo, "main", "/a").await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group("/b".try_into()?, Bytes::new()).await?;
    let amended = session.commit("amended").amend().execute().await?;
    assert_ne!(amended, first);
    assert_eq!(repo.lookup_branch("main").await?, amended);
    let ancestry = repo
        .ancestry(&VersionInfo::BranchTipRef("main".into()))
        .await?
        .try_collect::<Vec<_>>()
        .await?;
    assert_eq!(ancestry.len(), 2, "an amend replaces the tip rather than adding to it");

    // §6.5's `max_concurrent_nodes(0)` clamp is this release's own change and is
    // scored by `n15_manifest_commit_concurrency`, not here: an `inherited`
    // group must assert only what the previous checkpoint already does.
    let mut session = repo.writable_session("main").await?;
    session.add_group("/c".try_into()?, Bytes::new()).await?;
    let anonymous = session.commit("anonymous").anonymous().execute().await?;
    assert_eq!(
        repo.lookup_branch("main").await?,
        amended,
        "an anonymous commit does not move the branch"
    );
    assert_ne!(anonymous, amended);
    let session = repo.readonly_session(&VersionInfo::SnapshotId(anonymous)).await?;
    assert!(session.get_node(&"/c".try_into()?).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn i26_m09_table_of_contents__move_requires_a_rearrange_session() -> TestResult {
    let repo = mem_repo().await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.add_group("/from".try_into()?, Bytes::new()).await?;
    session.commit("setup").execute().await?;

    let mut writable = repo.writable_session("main").await?;
    assert!(
        matches!(
            writable.move_node("/from".try_into()?, "/to".try_into()?).await,
            Err(SessionError { kind: SessionErrorKind::NonRearrangeSession, .. })
        ),
        "moving requires a rearrange session"
    );

    let mut rearrange = repo.rearrange_session("main").await?;
    rearrange.move_node("/from".try_into()?, "/to".try_into()?).await?;
    assert!(
        matches!(
            rearrange.add_group("/new".try_into()?, Bytes::new()).await,
            Err(SessionError { kind: SessionErrorKind::RearrangeSessionOnly, .. })
        ),
        "a rearrange session refuses ordinary edits"
    );
    rearrange.commit("moved").execute().await?;

    let session = repo.readonly_session(&VersionInfo::BranchTipRef("main".into())).await?;
    assert!(session.get_node(&"/to".try_into()?).await.is_ok());
    assert!(session.get_node(&"/from".try_into()?).await.is_err());
    Ok(())
}

// ===========================================================================
// INHERITED — milestone 10: spec 2.1, additive and compatible both ways.
// TEST_INTERFACE.md §2.8.
// ===========================================================================

#[tokio::test]
async fn i27_m10_spec21_additive__expiration_records_the_pruned_tx_log_ancestry()
-> TestResult {
    let (_repo, am, a, b, c) = skew_fixture().await?;
    let (info, _) = am.fetch_repo_info().await?;
    let kept = info.find_snapshot(&c)?;
    assert_eq!(
        kept.pruned_ancestor_tx_logs,
        vec![a, b],
        "the pruned ancestry is recorded oldest first"
    );

    // An unexpired snapshot carries no pruned ancestry at all.
    let repo = mem_repo().await?;
    let fresh = commit_group(&repo, "main", "/a").await?;
    let info = repo.lookup_snapshot(&fresh).await?;
    assert!(info.pruned_ancestor_tx_logs.is_empty());
    Ok(())
}

#[tokio::test]
async fn i27_m10_spec21_additive__the_spec_version_byte_is_two() -> TestResult {
    let (repo, snap) = fixture().await?;
    assert_eq!(repo.spec_version(), SpecVersionBin::V2);
    assert_eq!(SpecVersionBin::current(), SpecVersionBin::V2);
    assert_eq!(SpecVersionBin::V2 as u8, 2);
    assert_eq!(SpecVersionBin::V1 as u8, 1);
    assert_eq!(SpecVersionBin::try_from(2u8)?, SpecVersionBin::V2);
    assert!(SpecVersionBin::try_from(3u8).is_err(), "an unknown generation is rejected");

    assert_eq!(
        repo.asset_manager().fetch_snapshot_header(&snap).await?.spec_version,
        SpecVersionBin::V2
    );
    assert_eq!(
        repo.asset_manager().fetch_repo_info_header().await?.spec_version,
        SpecVersionBin::V2
    );
    Ok(())
}

#[tokio::test]
async fn i27_m10_spec21_additive__virtual_locations_keep_every_url_part() -> TestResult {
    // Every scheme but `file` is kept verbatim, so nothing of the object key is
    // lost between writing a reference and resolving it.
    for url in [
        "s3://bucket/a//b.nc",
        "s3://bucket/a/b.nc?versionId=7",
        "gcs://bucket/a/../b.nc",
        "vcc://container/relative/path.nc",
    ] {
        assert_eq!(VirtualChunkLocation::from_url(url)?.url(), url, "{url}");
    }

    // `file://` is a security boundary, so its path is normalised.
    let normalised = VirtualChunkLocation::from_url("file:///data/allowed/../secret.bin")?;
    assert!(!normalised.url().contains(".."), "file:// paths resolve `..`");

    let relative = VirtualChunkLocation::from_url("vcc://mycontainer/sub/chunk.bin")?;
    assert!(relative.is_relative());
    assert_eq!(relative.parse_vcc(), Some(("mycontainer", "sub/chunk.bin")));
    Ok(())
}

// ===========================================================================
// PERIPHERAL — the other object-store presets and the per-role client
// construction they all now share. TEST_INTERFACE.md §5. Nothing here opens a
// socket: every constructor is pure and every client is built lazily.
// ===========================================================================

#[tokio::test]
async fn p30_storage_preset_matrix__s3_preset_records_its_endpoint_and_region()
-> TestResult {
    let storage = new_s3_storage(
        S3Options::default()
            .with_region("us-west-2")
            .with_endpoint_url("https://example.invalid")
            .with_force_path_style(true)
            .with_anonymous(true),
        "bucket".to_string(),
        Some("pfx".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(storage.as_ref());
    assert_eq!(f["bucket"], "bucket");
    assert_eq!(f["prefix"], "pfx");
    assert_eq!(f["region"], "us-west-2");
    assert_eq!(f["endpoint_url"], "https://example.invalid");
    assert_eq!(f["force_path_style"], "True");
    assert_eq!(f["anonymous"], "True");
    Ok(())
}

#[tokio::test]
async fn p30_storage_preset_matrix__r2_and_tigris_presets_are_offline_constructible()
-> TestResult {
    let tigris = new_tigris_storage(
        S3Options::default().with_region("iad"),
        "bucket".to_string(),
        None,
        Some(S3Credentials::Anonymous),
        false,
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(tigris.as_ref());
    assert_eq!(f["bucket"], "bucket");
    assert_eq!(f["endpoint_url"], "https://t3.storage.dev");
    assert_eq!(f["region"], "iad");

    // Tigris needs a region for a consistent, writable storage.
    assert!(
        new_tigris_storage(
            S3Options::default(),
            "bucket".to_string(),
            None,
            Some(S3Credentials::Anonymous),
            false,
            Vec::new(),
            Vec::new(),
            None,
        )
        .is_err(),
        "a consistent Tigris storage without a region must be refused"
    );

    let r2 = new_r2_storage(
        S3Options::default().with_region("auto"),
        Some("bucket".to_string()),
        None,
        Some("account".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(r2.as_ref());
    assert_eq!(f["bucket"], "bucket");
    assert_eq!(f["endpoint_url"], "https://account.r2.cloudflarestorage.com");
    assert_eq!(f["region"], "auto");
    assert_eq!(f["force_path_style"], "True");

    let object_store = new_s3_object_store_storage(
        S3Options::default().with_region("us-east-1"),
        "bucket".to_string(),
        Some("pfx".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
    )
    .await?;
    assert_eq!(info_fields(object_store.as_ref())["bucket"], "bucket");
    Ok(())
}

#[tokio::test]
async fn p30_storage_preset_matrix__backends_build_a_client_for_each_role() -> TestResult
{
    use icechunk::storage::Role;
    let backend = S3ObjectStoreBackend {
        bucket: "bucket".to_string(),
        prefix: Some("pfx".to_string()),
        credentials: Some(S3Credentials::Anonymous),
        config: Some(S3Options::default().with_region("us-east-1")),
        extra_read_headers: Vec::new(),
        extra_write_headers: Vec::new(),
    };
    let settings = Settings::default();
    assert!(backend.mk_object_store(&settings, Role::Read).is_ok());
    assert!(backend.mk_object_store(&settings, Role::Write).is_ok());
    assert!(!backend.read_write_headers_differ(), "identical header sets share a client");
    assert_eq!(backend.prefix(), "pfx");
    assert_eq!(backend.storage_info().backend_type, "S3");

    let with_headers = S3ObjectStoreBackend {
        bucket: "bucket".to_string(),
        prefix: None,
        credentials: Some(S3Credentials::Anonymous),
        config: Some(S3Options::default().with_region("us-east-1")),
        extra_read_headers: vec![("x-role".to_string(), "read".to_string())],
        extra_write_headers: vec![("x-role".to_string(), "write".to_string())],
    };
    assert!(with_headers.read_write_headers_differ());
    assert!(with_headers.mk_object_store(&settings, Role::Read).is_ok());
    assert!(with_headers.mk_object_store(&settings, Role::Write).is_ok());
    assert_ne!(Role::Read, Role::Write);
    Ok(())
}

#[tokio::test]
async fn p30_storage_preset_matrix__gateway_presets_handle_their_edge_cases()
-> TestResult {
    // TEST_INTERFACE.md §5, the preset table and the paragraph under it.

    // Tigris: no region is an error *unless* weak consistency was asked for…
    let weak = new_tigris_storage(
        S3Options::default(),
        "bucket".to_string(),
        None,
        Some(S3Credentials::Anonymous),
        true,
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(info_fields(weak.as_ref())["endpoint_url"], "https://t3.storage.dev");

    // …and Tigris leaves `force_path_style` exactly as it was given.
    let as_given = new_tigris_storage(
        S3Options::default().with_region("iad").with_force_path_style(true),
        "bucket".to_string(),
        None,
        Some(S3Credentials::Anonymous),
        false,
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(info_fields(as_given.as_ref())["force_path_style"], "True");
    let left_alone = new_tigris_storage(
        S3Options::default().with_region("iad"),
        "bucket".to_string(),
        None,
        Some(S3Credentials::Anonymous),
        false,
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert!(
        !info_fields(left_alone.as_ref()).contains_key("force_path_style"),
        "Tigris does not force path style on its own"
    );

    // R2 defaults the region to `auto` when none was given.
    let r2 = new_r2_storage(
        S3Options::default(),
        Some("bucket".to_string()),
        None,
        Some("account".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(info_fields(r2.as_ref())["region"], "auto");

    // R2 needs a bucket (or a prefix it can split one out of) AND either an
    // account id or an endpoint; without them it fails.
    assert!(
        new_r2_storage(
            S3Options::default().with_region("auto"),
            None,
            None,
            Some("account".to_string()),
            Some(S3Credentials::Anonymous),
            Vec::new(),
            Vec::new(),
            None,
        )
        .is_err(),
        "neither a bucket nor a prefix: R2 must refuse"
    );
    assert!(
        new_r2_storage(
            S3Options::default().with_region("auto"),
            Some("bucket".to_string()),
            None,
            None,
            Some(S3Credentials::Anonymous),
            Vec::new(),
            Vec::new(),
            None,
        )
        .is_err(),
        "neither an account id nor an endpoint: R2 must refuse"
    );
    // A splittable prefix supplies the bucket instead.
    let split = new_r2_storage(
        S3Options::default().with_region("auto"),
        None,
        Some("bucket/inner".to_string()),
        Some("account".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    let f = info_fields(split.as_ref());
    assert_eq!(f["bucket"], "bucket");
    assert_eq!(f["prefix"], "inner");
    Ok(())
}

#[tokio::test]
async fn p30_storage_preset_matrix__concrete_constructors_and_backend_serde()
-> TestResult {
    use icechunk::storage::{
        GcsCredentials, GcsObjectStoreBackend, new_gcs_storage, r2_storage, s3_storage,
        tigris_storage,
    };
    // TEST_INTERFACE.md §5: each `new_*` preset has a sibling returning the
    // concrete `S3Storage`, with the same argument list.
    let s3 = s3_storage(
        S3Options::default().with_region("us-west-2"),
        "bucket".to_string(),
        Some("pfx".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(s3.storage_info().backend_type, "S3 (native)");
    let r2 = r2_storage(
        S3Options::default().with_region("auto"),
        Some("bucket".to_string()),
        None,
        Some("account".to_string()),
        Some(S3Credentials::Anonymous),
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(info_fields(&r2)["endpoint_url"], "https://account.r2.cloudflarestorage.com");
    let tigris = tigris_storage(
        S3Options::default().with_region("iad"),
        "bucket".to_string(),
        None,
        Some(S3Credentials::Anonymous),
        false,
        Vec::new(),
        Vec::new(),
        None,
    )?;
    assert_eq!(info_fields(&tigris)["endpoint_url"], "https://t3.storage.dev");

    // …and the GCS preset, which takes its own credential type and no S3Options.
    let gcs = new_gcs_storage(
        "gcs-bucket".to_string(),
        Some("pfx".to_string()),
        Some(GcsCredentials::Anonymous),
        None,
        Vec::new(),
        Vec::new(),
    )?;
    let f = info_fields(gcs.as_ref());
    assert_eq!(f["bucket"], "gcs-bucket");
    assert_eq!(f["prefix"], "pfx");

    // TEST_INTERFACE.md §6.3: `GcsObjectStoreBackend` gained the same two header
    // vectors as `S3ObjectStoreBackend`.
    let gcs_backend = GcsObjectStoreBackend {
        bucket: "gcs-bucket".to_string(),
        prefix: Some("pfx".to_string()),
        credentials: Some(GcsCredentials::Anonymous),
        config: None,
        extra_read_headers: vec![("x-role".to_string(), "read".to_string())],
        extra_write_headers: vec![("x-role".to_string(), "write".to_string())],
    };
    assert!(gcs_backend.read_write_headers_differ());
    assert_eq!(gcs_backend.prefix(), "pfx");

    // …and both carry them as `#[serde(default, skip_serializing_if)]`, so a
    // backend an earlier release serialised — with neither field present —
    // still deserialises, with both vectors empty.
    let older = r#"{"bucket":"b","prefix":"p","credentials":null,"config":null}"#;
    let revived: S3ObjectStoreBackend = serde_json::from_str(older)?;
    assert_eq!(revived.bucket, "b");
    assert!(revived.extra_read_headers.is_empty());
    assert!(revived.extra_write_headers.is_empty());
    let round_tripped = serde_json::to_string(&revived)?;
    assert!(
        !round_tripped.contains("extra_read_headers"),
        "an empty header vector is skipped, so the serialised form is unchanged"
    );
    Ok(())
}

#[tokio::test]
async fn p30_storage_preset_matrix__extra_headers_are_validated() -> TestResult {
    use icechunk::storage::validate_extra_headers;
    validate_extra_headers(&[
        ("x-amz-meta-team".to_string(), "eng".to_string()),
        ("authorization-hint".to_string(), "none".to_string()),
    ])?;
    assert!(
        validate_extra_headers(&[("bad header name".to_string(), "v".to_string())])
            .is_err(),
        "a header name with a space is not a valid HTTP header name"
    );
    assert!(
        validate_extra_headers(&[("x-ok".to_string(), "bad\nvalue".to_string())]).is_err(),
        "a header value with a newline is rejected"
    );
    Ok(())
}
