// Verifier-owned conformance driver for milestone 06 — manifest splitting and
// the format-1.0 release of earth-mover/icechunk.
//
// Every assertion here traces to /app/icechunk-spec/ (the pinned Icechunk
// documentation corpus for this release, plus PROFILE.md, which resolves the
// places the documents and this milestone's contract disagree) or to
// /app/TEST_INTERFACE.md, which states the public Rust surface the milestone
// must expose.
//
// Group = the `<group>__` prefix of the test function name. The runner invokes
// one `cargo test` per group and asserts the executed count.
//
// Nothing here reaches the network, reads a cloud credential, starts a server
// or sleeps on a wall clock. Storage is `new_in_memory_storage()` or
// `new_local_filesystem_storage()` over a `tempfile::TempDir`; virtual chunk
// references address `file://` URLs inside a `TempDir`.

#![allow(clippy::unwrap_used, clippy::expect_used, clippy::panic)]

use std::{
    collections::{HashMap, HashSet},
    num::NonZeroU16,
    ops::Bound,
    path::PathBuf,
    sync::{
        Arc,
        atomic::{AtomicU16, Ordering},
    },
};

use bytes::Bytes;
use chrono::Utc;
use futures::{StreamExt, TryStreamExt};

// Only items that milestone 05's release already published may be named at
// file scope; see the `release_api!` comment below for why.
use icechunk::{
    ObjectStoreConfig, Repository, RepositoryConfig, Storage,
    config::{
        AzureCredentials, Credentials, GcsCredentials, ManifestConfig,
        ManifestPreloadCondition, ManifestPreloadConfig, S3Credentials, S3Options,
    },
    conflicts::{basic_solver::BasicConflictSolver, detector::ConflictDetector},
    format::{
        ByteRange, ChunkIndices, ManifestId, Path, SnapshotId,
        manifest::{
            ChunkPayload, ManifestExtents, VirtualChunkLocation, VirtualChunkRef,
            VirtualReferenceError, VirtualReferenceErrorKind,
        },
        snapshot::{ArrayShape, DimensionName, ManifestFileInfo, NodeData, Snapshot},
    },
    new_in_memory_storage, new_local_filesystem_storage,
    ops::gc::{ExpiredRefAction, GCConfig, GCSummary, expire, garbage_collect},
    repository::VersionInfo,
    session::{Session, SessionError, SessionErrorKind, get_chunk},
    storage,
    virtual_chunks::{VirtualChunkContainer, VirtualChunkResolver},
};

type TestResult = Result<(), Box<dyn std::error::Error>>;
type AnyResult<T> = Result<T, Box<dyn std::error::Error>>;

// ---------------------------------------------------------------------------
// Shared helpers
//
// None of these carry a `__` in their name, so the runner's per-group
// attribution slice never mistakes one for a scored case.
// ---------------------------------------------------------------------------

/// A fresh, independent in-memory backend.
async fn mem() -> AnyResult<Arc<dyn Storage + Send + Sync>> {
    let storage: Arc<dyn Storage + Send + Sync> = new_in_memory_storage().await?;
    Ok(storage)
}

/// A repository configuration that leaves the splitting policy alone and keeps
/// manifest preloading from running in the background, so no assertion depends
/// on background work. The preload *budget* is what is being set to zero here;
/// nothing in the corpus promises that zero disables preloading, so nothing is
/// asserted about it.
fn config_without_preload() -> RepositoryConfig {
    RepositoryConfig {
        manifest: Some(ManifestConfig {
            preload: Some(ManifestPreloadConfig {
                max_total_refs: Some(0),
                preload_if: None,
            }),
            ..Default::default()
        }),
        ..Default::default()
    }
}

/// A one-dimensional array of `len` chunks, every chunk holding the decimal
/// spelling of its own index inline. Returns the commit id.
async fn write_1d(
    repo: &Repository,
    array: &Path,
    len: u32,
    dimension_names: Option<Vec<DimensionName>>,
) -> AnyResult<SnapshotId> {
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(len as u64, 1)]).ok_or("bad shape")?,
            dimension_names,
            Bytes::new(),
        )
        .await?;
    for i in 0..len {
        session
            .set_chunk_ref(
                array.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(Bytes::from(format!("{i}")))),
            )
            .await?;
    }
    Ok(session.commit("write the array", None).await?)
}

/// Every chunk of a one-dimensional array, read back at the tip of `main`.
async fn read_1d(repo: &Repository, array: &Path, len: u32) -> AnyResult<Vec<Bytes>> {
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let mut out = Vec::with_capacity(len as usize);
    for i in 0..len {
        let bytes = get_chunk(
            session.get_chunk_reader(array, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?
        .ok_or("chunk is missing")?;
        out.push(bytes);
    }
    Ok(out)
}

/// What `read_1d` must return for an array written by `write_1d`.
fn expected_1d(len: u32) -> Vec<Bytes> {
    (0..len).map(|i| Bytes::from(format!("{i}"))).collect()
}

/// The number of manifest objects in the backend.
async fn manifest_objects(storage: &Arc<dyn Storage + Send + Sync>) -> AnyResult<usize> {
    Ok(storage.list_manifests(&storage.default_settings()).await?.count().await)
}

/// The manifests a single snapshot references.
async fn snapshot_manifests(
    repo: &Repository,
    snap: &SnapshotId,
) -> AnyResult<Vec<ManifestFileInfo>> {
    Ok(repo.lookup_snapshot(snap).await?.manifests)
}

/// The commit messages of a branch, newest first.
async fn branch_messages(repo: &Repository, branch: &str) -> AnyResult<Vec<String>> {
    Ok(repo
        .ancestry(&VersionInfo::BranchTipRef(branch.to_string()))
        .await?
        .map_ok(|info| info.message)
        .try_collect::<Vec<_>>()
        .await?)
}

/// A four-dimensional shape and its dimension names, as the release's own
/// splitting examples use them.
fn tzyx_shape() -> (ArrayShape, Option<Vec<DimensionName>>) {
    (
        ArrayShape::new(vec![(25, 1), (10, 1), (3, 1), (4, 1)]).unwrap(),
        Some(vec!["t".into(), "z".into(), "y".into(), "x".into()]),
    )
}

/// A `ManifestFileInfo` with a chosen reference count, for the pure predicates.
fn info(num_chunk_refs: u32) -> ManifestFileInfo {
    ManifestFileInfo { id: ManifestId::random(), size_bytes: 1, num_chunk_refs }
}

/// The path of a node, parsed. Panics on a malformed literal, which is a bug in
/// this driver rather than in the candidate.
fn p(path: &str) -> Path {
    Path::try_from(path).unwrap()
}

// ---------------------------------------------------------------------------
// The surface this release introduces, and the helpers built on it.
//
// These live inside a macro instead of at file scope, and every case body that
// needs them opens with `release_api!();`. The reason is the runner's
// per-group attribution slice: it removes the CASE FUNCTIONS of other groups
// and nothing else, so a file-scope `use` of a symbol that exists only at THIS
// release would make every slice - the inherited-regression groups included -
// fail to compile against the previous milestone's workspace, and a no-op
// continuation would score zero instead of the inherited share it earned. A
// `macro_rules!` body is name-resolved only where it is expanded, so the new
// surface is in scope exactly inside the cases that ask for it and nowhere
// else. `config_without_preload` above stays at file scope because the
// inherited groups use it and it names nothing new.
// ---------------------------------------------------------------------------
macro_rules! release_api {
    () => {
        #[allow(unused_imports)]
        use icechunk::{
            config::{
                ManifestSplitCondition, ManifestSplitDim, ManifestSplitDimCondition,
                ManifestSplittingConfig,
            },
            format::manifest::{ManifestSplits, Overlap, uniform_manifest_split_edges},
            ops::manifests::rewrite_manifests,
            session::find_coord,
            storage::RetriesSettings,
        };

        /// A repository configuration that pins the splitting policy and takes
        /// manifest preloading out of the picture, so no assertion depends on
        /// background work.
        #[allow(dead_code)]
        fn config_with_splitting(
            splitting: Option<ManifestSplittingConfig>,
        ) -> RepositoryConfig {
            RepositoryConfig {
                manifest: Some(ManifestConfig {
                    preload: Some(ManifestPreloadConfig {
                        max_total_refs: Some(0),
                        preload_if: None,
                    }),
                    splitting,
                }),
                ..Default::default()
            }
        }

        /// The same configuration, reached through `Repository::reopen`, which
        /// is how a reader changes the splitting policy without touching the
        /// stored data.
        #[allow(dead_code)]
        fn reopen_with_splitting(
            repo: &Repository,
            splitting: Option<ManifestSplittingConfig>,
        ) -> AnyResult<Repository> {
            Ok(repo.reopen(Some(config_with_splitting(splitting)), None)?)
        }

        /// One split rule: this condition selects arrays, these specs split
        /// their axes.
        #[allow(dead_code)]
        fn rule(
            condition: ManifestSplitCondition,
            dims: Vec<ManifestSplitDim>,
        ) -> (ManifestSplitCondition, Vec<ManifestSplitDim>) {
            (condition, dims)
        }

        /// `ManifestSplitDim` without the struct-literal noise.
        #[allow(dead_code)]
        fn dim(
            condition: ManifestSplitDimCondition,
            num_chunks: u32,
        ) -> ManifestSplitDim {
            ManifestSplitDim { condition, num_chunks }
        }

        /// Matches every array by path.
        #[allow(dead_code)]
        fn any_path() -> ManifestSplitCondition {
            ManifestSplitCondition::PathMatches { regex: r".*".to_string() }
        }
    };
}

// ===========================================================================
// g01_split_conditions — which arrays a splitting rule selects.
//   TEST_INTERFACE.md §3.1; icechunk-spec/guide/performance.md
//   §"Configuring splitting"; PROFILE.md P6-1.
// ===========================================================================

#[tokio::test]
async fn g01_split_conditions__any_array_matches_every_array() -> TestResult {
    release_api!();
    let cond = ManifestSplitCondition::AnyArray;
    for path in ["/", "/a", "/ocean/temperature", "/deeply/nested/array"] {
        assert!(cond.matches(&p(path)), "AnyArray must select {path}");
    }
    assert!(cond.matches(&Path::root()));
    Ok(())
}

#[tokio::test]
async fn g01_split_conditions__path_matches_is_an_unanchored_regex() -> TestResult {
    release_api!();
    let bare = ManifestSplitCondition::PathMatches { regex: "temp".to_string() };
    assert!(bare.matches(&p("/temperature")), "an unanchored regex is a search");
    assert!(bare.matches(&p("/ocean/temperature")));
    assert!(!bare.matches(&p("/salinity")));

    let anchored =
        ManifestSplitCondition::PathMatches { regex: "^/temperature$".to_string() };
    assert!(anchored.matches(&p("/temperature")));
    assert!(!anchored.matches(&p("/ocean/temperature")));

    let prefix = ManifestSplitCondition::PathMatches { regex: "^/ocean/".to_string() };
    assert!(prefix.matches(&p("/ocean/temperature")));
    assert!(!prefix.matches(&p("/land/temperature")));
    Ok(())
}

#[tokio::test]
async fn g01_split_conditions__name_matches_looks_at_the_node_name() -> TestResult {
    release_api!();
    let by_name =
        ManifestSplitCondition::NameMatches { regex: "^temperature$".to_string() };
    assert!(by_name.matches(&p("/temperature")));
    assert!(by_name.matches(&p("/ocean/temperature")), "the name is the last segment");
    assert!(!by_name.matches(&p("/ocean/salinity")));

    // The same regex read as a path never matches a nested array, which is what
    // makes `name_matches` and `path_matches` two distinct selectors.
    let by_path =
        ManifestSplitCondition::PathMatches { regex: "^temperature$".to_string() };
    assert!(!by_path.matches(&p("/ocean/temperature")));

    // The root has no name at all.
    assert!(!ManifestSplitCondition::NameMatches { regex: ".*".to_string() }
        .matches(&Path::root()));
    Ok(())
}

#[tokio::test]
async fn g01_split_conditions__an_uncompilable_regex_evaluates_false() -> TestResult {
    release_api!();
    let bad_path = ManifestSplitCondition::PathMatches { regex: "(".to_string() };
    assert!(!bad_path.matches(&p("/anything")), "a broken regex must not match");
    let bad_name = ManifestSplitCondition::NameMatches { regex: "[".to_string() };
    assert!(!bad_name.matches(&p("/anything")));

    // It is silent, not fatal: a sibling clause still decides the disjunction.
    let mixed = ManifestSplitCondition::Or(vec![
        bad_path.clone(),
        ManifestSplitCondition::NameMatches { regex: "^anything$".to_string() },
    ]);
    assert!(mixed.matches(&p("/anything")));

    // And a rule whose condition cannot compile simply never fires.
    let cfg = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            bad_path,
            vec![dim(ManifestSplitDimCondition::Any, 3)],
        )]),
    };
    let shape = ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?;
    assert_eq!(
        cfg.get_split_sizes(&p("/anything"), &shape, &None),
        ManifestSplits::from_edges(vec![vec![0, 10]]),
    );
    Ok(())
}

#[tokio::test]
async fn g01_split_conditions__round_trip_through_json() -> TestResult {
    release_api!();
    let cond = ManifestSplitCondition::And(vec![
        ManifestSplitCondition::PathMatches { regex: "^/ocean/".to_string() },
        ManifestSplitCondition::Or(vec![
            ManifestSplitCondition::NameMatches { regex: "^temperature$".to_string() },
            ManifestSplitCondition::AnyArray,
        ]),
    ]);
    let json = serde_json::to_string(&cond)?;
    let back: ManifestSplitCondition = serde_json::from_str(&json)?;
    assert_eq!(back, cond, "a persisted splitting policy must survive a round trip");
    Ok(())
}

// ===========================================================================
// g02_split_dim_conditions — how each axis is split.
//   TEST_INTERFACE.md §3.2; guide/performance.md §"Configuring splitting".
// ===========================================================================

#[tokio::test]
async fn g02_split_dim_conditions__axis_selects_one_axis_by_index() -> TestResult {
    release_api!();
    let (shape, names) = tzyx_shape();
    let cfg = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            ManifestSplitCondition::AnyArray,
            vec![dim(ManifestSplitDimCondition::Axis(2), 2)],
        )]),
    };
    // Axis 2 has 3 chunks and is split every 2; every other axis is left whole.
    assert_eq!(
        cfg.get_split_sizes(&p("/temperature"), &shape, &names),
        ManifestSplits::from_edges(vec![
            vec![0, 25],
            vec![0, 10],
            vec![0, 2, 3],
            vec![0, 4],
        ]),
    );
    Ok(())
}

#[tokio::test]
async fn g02_split_dim_conditions__dimension_name_is_a_regex() -> TestResult {
    release_api!();
    let (shape, names) = tzyx_shape();
    let by_name = |regex: &str, n: u32| ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            ManifestSplitCondition::AnyArray,
            vec![dim(ManifestSplitDimCondition::DimensionName(regex.to_string()), n)],
        )]),
    };
    // `^z$` names axis 1, which carries 10 chunks.
    assert_eq!(
        by_name("^z$", 9).get_split_sizes(&p("/temperature"), &shape, &names),
        ManifestSplits::from_edges(vec![
            vec![0, 25],
            vec![0, 9, 10],
            vec![0, 3],
            vec![0, 4],
        ]),
    );
    // A name that matches nothing leaves every axis whole.
    assert_eq!(
        by_name("^nosuchdim$", 9).get_split_sizes(&p("/temperature"), &shape, &names),
        ManifestSplits::from_edges(vec![
            vec![0, 25],
            vec![0, 10],
            vec![0, 3],
            vec![0, 4],
        ]),
    );
    // With no dimension names at all, a name condition can never match.
    assert_eq!(
        by_name("^z$", 9).get_split_sizes(&p("/temperature"), &shape, &None),
        ManifestSplits::from_edges(vec![
            vec![0, 25],
            vec![0, 10],
            vec![0, 3],
            vec![0, 4],
        ]),
    );
    // An axis whose dimension name is `NotSpecified` never matches either, not
    // even a regex that matches everything: only axes 0, 2 and 3 are named
    // here, so axis 1 keeps its single cell while the others are cut.
    // `From<&str>` maps the empty string onto `NotSpecified` (§3.7), so the two
    // spellings of "this axis has no name" are the same value.
    assert_eq!(DimensionName::from(""), DimensionName::NotSpecified);
    let partly_named = Some(vec![
        DimensionName::Name("t".to_string()),
        DimensionName::NotSpecified,
        "y".into(),
        "x".into(),
    ]);
    assert_eq!(
        by_name(".*", 2).get_split_sizes(&p("/temperature"), &shape, &partly_named),
        ManifestSplits::from_edges(vec![
            vec![0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 25],
            vec![0, 10],
            vec![0, 2, 3],
            vec![0, 2, 4],
        ]),
    );
    Ok(())
}

#[tokio::test]
async fn g02_split_dim_conditions__any_matches_every_remaining_axis() -> TestResult {
    release_api!();
    let (shape, names) = tzyx_shape();
    // The grid has one entry per axis, and the axis count is the shape's own.
    assert_eq!(shape.len(), 4);
    assert_eq!(shape.num_chunks().collect::<Vec<_>>(), vec![25, 10, 3, 4]);
    let cfg = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            ManifestSplitCondition::AnyArray,
            vec![
                dim(ManifestSplitDimCondition::Axis(0), 12),
                dim(ManifestSplitDimCondition::Any, 2),
            ],
        )]),
    };
    assert_eq!(
        cfg.get_split_sizes(&p("/temperature"), &shape, &names),
        ManifestSplits::from_edges(vec![
            vec![0, 12, 24, 25],
            vec![0, 2, 4, 6, 8, 10],
            vec![0, 2, 3],
            vec![0, 2, 4],
        ]),
    );
    Ok(())
}

#[tokio::test]
async fn g02_split_dim_conditions__first_match_wins_within_and_across_rules()
-> TestResult {
    release_api!();
    let (shape, names) = tzyx_shape();
    let path = p("/temperature");
    let expected = ManifestSplits::from_edges(vec![
        vec![0, 12, 24, 25],
        vec![0, 9, 10],
        vec![0, 2, 3],
        vec![0, 4],
    ]);

    // One rule listing three dim specs: the first spec that matches an axis
    // claims it, and later specs never revisit it.
    let one_rule = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            any_path(),
            vec![
                dim(ManifestSplitDimCondition::DimensionName("t".to_string()), 12),
                dim(ManifestSplitDimCondition::Axis(2), 2),
                dim(ManifestSplitDimCondition::Any, 9),
            ],
        )]),
    };
    // Three separate rules carrying the same specs in the same order are
    // equivalent, because `split_sizes` is an ordered list, not a map.
    let three_rules = ManifestSplittingConfig {
        split_sizes: Some(vec![
            rule(
                any_path(),
                vec![dim(
                    ManifestSplitDimCondition::DimensionName("t".to_string()),
                    12,
                )],
            ),
            rule(any_path(), vec![dim(ManifestSplitDimCondition::Axis(2), 2)]),
            rule(any_path(), vec![dim(ManifestSplitDimCondition::Any, 9)]),
        ]),
    };
    assert_eq!(one_rule.get_split_sizes(&path, &shape, &names), expected);
    assert_eq!(three_rules.get_split_sizes(&path, &shape, &names), expected);

    // Reversing the order changes the answer, which is what "first wins" means:
    // `Any` now claims every axis before the more specific specs are consulted.
    let reversed = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            any_path(),
            vec![
                dim(ManifestSplitDimCondition::Any, 9),
                dim(ManifestSplitDimCondition::DimensionName("t".to_string()), 12),
                dim(ManifestSplitDimCondition::Axis(2), 2),
            ],
        )]),
    };
    assert_eq!(
        reversed.get_split_sizes(&path, &shape, &names),
        ManifestSplits::from_edges(vec![
            vec![0, 9, 18, 25],
            vec![0, 9, 10],
            vec![0, 3],
            vec![0, 4],
        ]),
    );
    Ok(())
}

// ===========================================================================
// g03_split_combinators — `And` and `Or` over array conditions.
//   TEST_INTERFACE.md §3.1 and §"what is not scored"; PROFILE.md P6-2.
//   These are plain enum variants taking a `Vec`; the Rust crate has no
//   `and_conditions` / `or_conditions` constructors and no `&` / `|` operators.
// ===========================================================================

#[tokio::test]
async fn g03_split_combinators__or_matches_when_any_clause_matches() -> TestResult {
    release_api!();
    let cond = ManifestSplitCondition::Or(vec![
        ManifestSplitCondition::NameMatches { regex: "^temperature$".to_string() },
        ManifestSplitCondition::NameMatches { regex: "^salinity$".to_string() },
    ]);
    assert!(cond.matches(&p("/ocean/temperature")));
    assert!(cond.matches(&p("/ocean/salinity")));
    assert!(!cond.matches(&p("/ocean/pressure")));
    Ok(())
}

#[tokio::test]
async fn g03_split_combinators__and_requires_every_clause() -> TestResult {
    release_api!();
    let cond = ManifestSplitCondition::And(vec![
        ManifestSplitCondition::PathMatches { regex: "^/ocean/".to_string() },
        ManifestSplitCondition::NameMatches { regex: "^temperature$".to_string() },
    ]);
    assert!(cond.matches(&p("/ocean/temperature")));
    assert!(!cond.matches(&p("/land/temperature")), "the path clause fails");
    assert!(!cond.matches(&p("/ocean/salinity")), "the name clause fails");
    Ok(())
}

#[tokio::test]
async fn g03_split_combinators__empty_or_is_false_and_empty_and_is_true() -> TestResult {
    release_api!();
    assert!(!ManifestSplitCondition::Or(vec![]).matches(&p("/anything")));
    assert!(ManifestSplitCondition::And(vec![]).matches(&p("/anything")));
    Ok(())
}

#[tokio::test]
async fn g03_split_combinators__a_combined_condition_drives_the_grid() -> TestResult {
    release_api!();
    let ocean_and_either = ManifestSplitCondition::And(vec![
        ManifestSplitCondition::PathMatches { regex: "^/ocean/".to_string() },
        ManifestSplitCondition::Or(vec![
            ManifestSplitCondition::NameMatches { regex: "^temperature$".to_string() },
            ManifestSplitCondition::NameMatches { regex: "^salinity$".to_string() },
        ]),
    ]);
    let cfg = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            ocean_and_either,
            vec![dim(ManifestSplitDimCondition::Any, 3)],
        )]),
    };
    let shape = ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?;
    assert_eq!(
        cfg.get_split_sizes(&p("/ocean/temperature"), &shape, &None),
        ManifestSplits::from_edges(vec![vec![0, 3, 6, 9, 10]]),
    );
    assert_eq!(
        cfg.get_split_sizes(&p("/ocean/salinity"), &shape, &None),
        ManifestSplits::from_edges(vec![vec![0, 3, 6, 9, 10]]),
    );
    // An array the condition does not select keeps a single manifest.
    assert_eq!(
        cfg.get_split_sizes(&p("/land/temperature"), &shape, &None),
        ManifestSplits::from_edges(vec![vec![0, 10]]),
    );
    Ok(())
}

// ===========================================================================
// g04_split_edges — `uniform_manifest_split_edges`.
//   TEST_INTERFACE.md §3.3.
// ===========================================================================

#[tokio::test]
async fn g04_split_edges__a_ragged_tail_becomes_its_own_manifest() -> TestResult {
    release_api!();
    assert_eq!(uniform_manifest_split_edges(25, &12), vec![0, 12, 24, 25]);
    assert_eq!(uniform_manifest_split_edges(10, &9), vec![0, 9, 10]);
    assert_eq!(uniform_manifest_split_edges(10, &3), vec![0, 3, 6, 9, 10]);
    Ok(())
}

#[tokio::test]
async fn g04_split_edges__an_exact_division_has_no_tail() -> TestResult {
    release_api!();
    assert_eq!(uniform_manifest_split_edges(12, &3), vec![0, 3, 6, 9, 12]);
    assert_eq!(uniform_manifest_split_edges(10, &5), vec![0, 5, 10]);
    assert_eq!(uniform_manifest_split_edges(10, &10), vec![0, 10]);
    Ok(())
}

#[tokio::test]
async fn g04_split_edges__a_split_wider_than_the_axis_is_a_single_manifest()
-> TestResult {
    release_api!();
    assert_eq!(uniform_manifest_split_edges(4, &9), vec![0, 4]);
    assert_eq!(uniform_manifest_split_edges(4, &u32::MAX), vec![0, 4]);
    Ok(())
}

#[tokio::test]
async fn g04_split_edges__a_split_of_one_gives_one_manifest_per_chunk() -> TestResult {
    release_api!();
    assert_eq!(uniform_manifest_split_edges(4, &1), vec![0, 1, 2, 3, 4]);
    assert_eq!(uniform_manifest_split_edges(1, &1), vec![0, 1]);
    Ok(())
}

// ===========================================================================
// g05_split_grid — `ManifestSplits`, the cartesian product of the per-axis
//   edge lists, and the routing of a chunk coordinate into one cell.
//   TEST_INTERFACE.md §3.4.
// ===========================================================================

#[tokio::test]
async fn g05_split_grid__from_edges_is_the_cartesian_product() -> TestResult {
    release_api!();
    let actual = ManifestSplits::from_edges(vec![vec![0u32, 1, 2], vec![3u32, 4, 5]]);
    let expected = ManifestSplits::from_extents(vec![
        ManifestExtents::new(&[0, 3], &[1, 4]),
        ManifestExtents::new(&[0, 4], &[1, 5]),
        ManifestExtents::new(&[1, 3], &[2, 4]),
        ManifestExtents::new(&[1, 4], &[2, 5]),
    ]);
    assert_eq!(actual, expected, "the last axis must vary fastest");
    assert_eq!(actual.len(), 4);
    assert!(!actual.is_empty());

    // `from_extents` is the read-time constructor and it refuses an empty grid
    // (TEST_INTERFACE.md §3.4: "panics if empty").
    let hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let empty = std::panic::catch_unwind(|| ManifestSplits::from_extents(Vec::new()));
    std::panic::set_hook(hook);
    assert!(empty.is_err(), "an empty extent list is not a grid");
    Ok(())
}

#[tokio::test]
async fn g05_split_grid__one_edge_pair_per_axis_is_a_single_cell() -> TestResult {
    release_api!();
    let splits = ManifestSplits::from_edges(vec![vec![0u32, 10]]);
    assert_eq!(splits.len(), 1);
    assert_eq!(
        splits.iter().cloned().collect::<Vec<_>>(),
        vec![ManifestExtents::new(&[0], &[10])],
    );
    Ok(())
}

#[tokio::test]
async fn g05_split_grid__position_routes_a_coordinate_to_its_cell() -> TestResult {
    release_api!();
    let splits = ManifestSplits::from_edges(vec![vec![0u32, 3, 6, 9, 10]]);
    assert_eq!(splits.len(), 4);
    assert_eq!(splits.position(&ChunkIndices(vec![0])), Some(0));
    assert_eq!(splits.position(&ChunkIndices(vec![2])), Some(0));
    assert_eq!(splits.position(&ChunkIndices(vec![3])), Some(1), "ranges are half open");
    assert_eq!(splits.position(&ChunkIndices(vec![9])), Some(3));
    assert_eq!(
        splits.find(&ChunkIndices(vec![4])),
        Some(&ManifestExtents::new(&[3], &[6])),
    );

    // The free function behind both of them yields the index and the cell.
    let coord = ChunkIndices(vec![7]);
    let found = find_coord(splits.iter(), &coord);
    assert_eq!(found, Some((2, &ManifestExtents::new(&[6], &[9]))));
    Ok(())
}

#[tokio::test]
async fn g05_split_grid__a_coordinate_outside_the_grid_has_no_cell() -> TestResult {
    release_api!();
    let splits = ManifestSplits::from_edges(vec![vec![0u32, 3, 6, 9, 10]]);
    assert_eq!(splits.position(&ChunkIndices(vec![10])), None);
    assert_eq!(splits.position(&ChunkIndices(vec![99])), None);
    assert_eq!(splits.find(&ChunkIndices(vec![10])), None);
    Ok(())
}

#[tokio::test]
async fn g05_split_grid__compatible_with_rejects_a_partial_overlap() -> TestResult {
    release_api!();
    let three = ManifestSplits::from_edges(vec![vec![0u32, 3, 6]]);
    assert!(three.compatible_with(&three), "a grid is compatible with itself");

    let two = ManifestSplits::from_edges(vec![vec![0u32, 2, 4, 6]]);
    assert!(!three.compatible_with(&two), "cells that straddle each other conflict");

    let whole = ManifestSplits::from_edges(vec![vec![0u32, 6]]);
    assert!(!whole.compatible_with(&three));
    Ok(())
}

// ===========================================================================
// g06_manifest_extents — the bounding boxes the grid is made of.
//   TEST_INTERFACE.md §3.5.
// ===========================================================================

#[tokio::test]
async fn g06_manifest_extents__new_and_iter_round_trip() -> TestResult {
    let extents = ManifestExtents::new(&[0, 3], &[2, 7]);
    assert_eq!(extents.len(), 2);
    assert!(!extents.is_empty());
    assert_eq!(extents.iter().cloned().collect::<Vec<_>>(), vec![0..2, 3..7]);
    assert_eq!(ManifestExtents::from_ranges_iter(vec![0..2, 3..7]), extents);
    Ok(())
}

#[tokio::test]
async fn g06_manifest_extents__contains_is_half_open_per_axis() -> TestResult {
    let extents = ManifestExtents::new(&[0, 3], &[2, 7]);
    assert!(extents.contains(&[0, 3]));
    assert!(extents.contains(&[1, 6]));
    assert!(!extents.contains(&[2, 6]), "the upper bound is excluded");
    assert!(!extents.contains(&[1, 7]));
    assert!(!extents.contains(&[1, 2]));
    Ok(())
}

#[tokio::test]
async fn g06_manifest_extents__intersection_and_union() -> TestResult {
    let a = ManifestExtents::new(&[0, 0], &[4, 4]);
    let b = ManifestExtents::new(&[2, 2], &[6, 6]);
    assert_eq!(a.intersection(&b), Some(ManifestExtents::new(&[2, 2], &[4, 4])));
    assert_eq!(a.union(&b), ManifestExtents::new(&[0, 0], &[6, 6]));

    let disjoint = ManifestExtents::new(&[8, 8], &[9, 9]);
    assert_eq!(a.intersection(&disjoint), None);
    assert_eq!(a.union(&disjoint), ManifestExtents::new(&[0, 0], &[9, 9]));
    Ok(())
}

#[tokio::test]
async fn g06_manifest_extents__overlap_with_is_not_symmetric() -> TestResult {
    release_api!();
    let small = ManifestExtents::new(&[0], &[2]);
    let big = ManifestExtents::new(&[0], &[4]);
    // `a.overlap_with(b)` asks whether b fully covers a.
    assert_eq!(small.overlap_with(&big), Overlap::Complete);
    assert_eq!(big.overlap_with(&small), Overlap::Partial);
    assert_eq!(small.overlap_with(&small), Overlap::Complete);

    let elsewhere = ManifestExtents::new(&[8], &[9]);
    assert_eq!(small.overlap_with(&elsewhere), Overlap::None);
    assert_eq!(elsewhere.overlap_with(&small), Overlap::None);
    Ok(())
}

#[tokio::test]
async fn g06_manifest_extents__all_is_the_universal_sentinel() -> TestResult {
    release_api!();
    let all = ManifestExtents::ALL;
    assert!(all.is_empty());
    assert_eq!(all.len(), 0);

    let one = ManifestExtents::new(&[0], &[4]);
    assert!(all.matches(&one), "ALL matches any extents");
    assert!(one.matches(&one));
    assert!(!one.matches(&ManifestExtents::new(&[0], &[5])));

    assert_eq!(one.overlap_with(&all), Overlap::Complete);
    assert_eq!(all.overlap_with(&one), Overlap::Partial);
    assert_eq!(all.intersection(&one), Some(one.clone()));
    assert_eq!(all.union(&one), all);
    Ok(())
}

// ===========================================================================
// g07_splitting_config — the configuration objects themselves.
//   TEST_INTERFACE.md §3.6; guide/configuration.md §"manifest".
// ===========================================================================

#[tokio::test]
async fn g07_splitting_config__with_size_is_the_one_liner_for_every_axis()
-> TestResult {
    release_api!();
    let expanded = ManifestSplittingConfig {
        split_sizes: Some(vec![rule(
            any_path(),
            vec![dim(ManifestSplitDimCondition::Any, 3)],
        )]),
    };
    assert_eq!(ManifestSplittingConfig::with_size(3), expanded);

    let (shape, names) = tzyx_shape();
    assert_eq!(
        ManifestSplittingConfig::with_size(3).get_split_sizes(
            &p("/temperature"),
            &shape,
            &names
        ),
        ManifestSplits::from_edges(vec![
            vec![0, 3, 6, 9, 12, 15, 18, 21, 24, 25],
            vec![0, 3, 6, 9, 10],
            vec![0, 3],
            vec![0, 3, 4],
        ]),
    );
    Ok(())
}

#[tokio::test]
async fn g07_splitting_config__the_default_policy_never_splits() -> TestResult {
    release_api!();
    let (shape, names) = tzyx_shape();
    let single = ManifestSplits::from_edges(vec![
        vec![0, 25],
        vec![0, 10],
        vec![0, 3],
        vec![0, 4],
    ]);
    assert_eq!(
        ManifestSplittingConfig::default().get_split_sizes(
            &p("/temperature"),
            &shape,
            &names
        ),
        single,
    );
    // `split_sizes: None` falls back to that same default.
    assert_eq!(
        ManifestSplittingConfig { split_sizes: None }.get_split_sizes(
            &p("/temperature"),
            &shape,
            &names
        ),
        single,
    );
    Ok(())
}

#[tokio::test]
async fn g07_splitting_config__manifest_config_accessors_and_merge() -> TestResult {
    release_api!();
    let default = ManifestConfig::default();
    assert_eq!(default.splitting(), &ManifestSplittingConfig::default());
    assert_eq!(default.preload().max_total_refs(), 10_000);

    // `empty()` is the "no preload, no splitting" constructor.
    let empty = ManifestConfig::empty();
    assert_eq!(empty.splitting, None);
    assert_eq!(
        empty.preload,
        Some(ManifestPreloadConfig { max_total_refs: None, preload_if: None }),
    );

    // `merge` is right-biased, field by field.
    let base = ManifestConfig {
        preload: Some(ManifestPreloadConfig {
            max_total_refs: Some(7),
            preload_if: None,
        }),
        splitting: Some(ManifestSplittingConfig::with_size(3)),
    };
    let merged = base.merge(ManifestConfig {
        preload: None,
        splitting: Some(ManifestSplittingConfig::with_size(5)),
    });
    assert_eq!(merged.splitting, Some(ManifestSplittingConfig::with_size(5)));
    assert_eq!(merged.preload().max_total_refs(), 7);
    Ok(())
}

#[tokio::test]
async fn g07_splitting_config__the_whole_policy_round_trips_through_json()
-> TestResult {
    release_api!();
    let cfg = ManifestSplittingConfig {
        split_sizes: Some(vec![
            rule(
                ManifestSplitCondition::Or(vec![
                    ManifestSplitCondition::NameMatches {
                        regex: "^temperature$".to_string(),
                    },
                    ManifestSplitCondition::AnyArray,
                ]),
                vec![
                    dim(
                        ManifestSplitDimCondition::DimensionName("^time$".to_string()),
                        12,
                    ),
                    dim(ManifestSplitDimCondition::Axis(1), 2),
                    dim(ManifestSplitDimCondition::Any, 1),
                ],
            ),
            rule(any_path(), vec![dim(ManifestSplitDimCondition::Any, 4)]),
        ]),
    };
    let back: ManifestSplittingConfig = serde_json::from_str(&serde_json::to_string(&cfg)?)?;
    assert_eq!(back, cfg);

    // And it survives a round trip inside a whole repository configuration,
    // which is what `save_config` persists.
    let repo_config = config_with_splitting(Some(cfg));
    let back: RepositoryConfig =
        serde_json::from_str(&serde_json::to_string(&repo_config)?)?;
    assert_eq!(back, repo_config);
    Ok(())
}

// ===========================================================================
// g08_manifest_count — splitting observed where a user sees it: the number of
//   manifest objects a commit writes.
//   TEST_INTERFACE.md §3.7; guide/performance.md §§"Splitting manifests",
//   "Splitting behaviour".
// ===========================================================================

#[tokio::test]
async fn g08_manifest_count__no_splitting_writes_one_manifest_per_array() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(None)),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    assert_eq!(manifest_objects(&storage).await?, 1);
    Ok(())
}

#[tokio::test]
async fn g08_manifest_count__a_split_array_writes_one_manifest_per_cell() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    // 10 chunks at split size 3 -> edges [0, 3, 6, 9, 10] -> 4 cells.
    assert_eq!(manifest_objects(&storage).await?, 4);
    assert!(manifest_objects(&storage).await? > 1, "the array really was split");
    Ok(())
}

#[tokio::test]
async fn g08_manifest_count__empty_cells_produce_no_manifest() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(25, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    // The grid has nine cells; only one of them is written.
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"only"))),
        )
        .await?;
    session.commit("one chunk", None).await?;
    assert_eq!(manifest_objects(&storage).await?, 1);
    Ok(())
}

#[tokio::test]
async fn g08_manifest_count__the_snapshot_lists_every_manifest_it_references()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let snap = write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;

    let manifests = snapshot_manifests(&repo, &snap).await?;
    assert_eq!(manifests.len(), 4);
    assert_eq!(manifests.iter().map(|m| m.num_chunk_refs).sum::<u32>(), 10);
    assert!(manifests.iter().all(|m| m.size_bytes > 0));
    assert_eq!(
        manifests.iter().map(|m| m.id.clone()).collect::<HashSet<_>>().len(),
        4,
        "manifest ids must be distinct",
    );

    // The node itself carries one populated bounding box per manifest.
    let session =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let node = session.get_array(&array).await?;
    match node.node_data {
        NodeData::Array { manifests, .. } => {
            assert_eq!(manifests.len(), 4);
            assert!(manifests.iter().all(|m| !m.extents.is_empty()));
        }
        NodeData::Group => panic!("/temperature must be an array"),
    }
    Ok(())
}

#[tokio::test]
async fn g08_manifest_count__an_append_under_a_compatible_policy_reuses_manifests()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let fine = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(1)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/array");

    // A ten-chunk axis at split size 1, with only the first five chunks
    // written: one manifest per written cell, five in all.
    let mut session = fine.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    for i in 0..5u32 {
        session
            .set_chunk_ref(
                array.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(Bytes::from(format!("{i}")))),
            )
            .await?;
    }
    let first = session.commit("write five chunks", None).await?;
    let before = snapshot_manifests(&fine, &first).await?;
    assert_eq!(before.len(), 5, "split size 1 writes one manifest per chunk");
    assert_eq!(manifest_objects(&storage).await?, 5);

    // Reopen at split size 5. Every existing manifest's bounding box - [0,1),
    // [1,2), ... - lies wholly inside one cell of the new grid [0,5), [5,10),
    // so the old manifests stay compatible with it. Appending into the second
    // cell therefore writes ONE new manifest and rewrites nothing, which is
    // what guide/performance.md §"Splitting behaviour" states: "only one new
    // manifest with the 3 new chunk refs has been written".
    let coarse =
        reopen_with_splitting(&fine, Some(ManifestSplittingConfig::with_size(5)))?;
    let mut session = coarse.writable_session("main").await?;
    for i in 6..9u32 {
        session
            .set_chunk_ref(
                array.clone(),
                ChunkIndices(vec![i]),
                Some(ChunkPayload::Inline(Bytes::from(format!("{i}")))),
            )
            .await?;
    }
    let second = session.commit("append three chunks", None).await?;

    let after = snapshot_manifests(&coarse, &second).await?;
    assert_eq!(after.len(), 6, "the five existing manifests are kept, one is added");
    let kept: HashSet<_> = before.iter().map(|m| m.id.clone()).collect();
    let now: HashSet<_> = after.iter().map(|m| m.id.clone()).collect();
    assert!(
        kept.is_subset(&now),
        "a commit under a compatible policy rewrites no existing manifest",
    );
    assert_eq!(now.difference(&kept).count(), 1, "exactly one manifest is new");
    assert_eq!(
        after.iter().map(|m| m.num_chunk_refs).sum::<u32>(),
        8,
        "the five earlier references and the three just appended",
    );
    assert_eq!(
        manifest_objects(&storage).await?,
        6,
        "only one manifest object was added to the backend",
    );

    // And the data still reads back, old cells and new.
    let reader =
        coarse.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    for i in [0u32, 4, 6, 8] {
        let got = get_chunk(
            reader.get_chunk_reader(&array, &ChunkIndices(vec![i]), &ByteRange::ALL).await?,
        )
        .await?
        .ok_or("chunk is missing")?;
        assert_eq!(got, Bytes::from(format!("{i}")));
    }
    Ok(())
}

// ===========================================================================
// g09_reads_unchanged — the metamorphic property: the splitting policy is a
//   storage-layout decision and never changes what a reader sees.
//   TEST_INTERFACE.md §3.8; guide/performance.md §"Rewriting manifests".
// ===========================================================================

#[tokio::test]
async fn g09_reads_unchanged__three_split_policies_read_back_identically()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    let want = expected_1d(10);

    // Written at split size 3.
    assert_eq!(read_1d(&reopen_with_splitting(&repo, None)?, &array, 10).await?, want);

    // Consolidated to one manifest.
    let big = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(12)))?;
    rewrite_manifests(&big, "main", "consolidate", None).await?;
    assert_eq!(read_1d(&reopen_with_splitting(&repo, None)?, &array, 10).await?, want);

    // Re-split more finely.
    let small = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(4)))?;
    rewrite_manifests(&small, "main", "re-split", None).await?;
    assert_eq!(read_1d(&reopen_with_splitting(&repo, None)?, &array, 10).await?, want);
    Ok(())
}

#[tokio::test]
async fn g09_reads_unchanged__the_layout_does_change_while_the_data_does_not()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let first = write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    assert_eq!(snapshot_manifests(&repo, &first).await?.len(), 4);

    let big = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(12)))?;
    let consolidated = rewrite_manifests(&big, "main", "consolidate", None).await?;
    assert_eq!(snapshot_manifests(&repo, &consolidated).await?.len(), 1);

    let small = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(4)))?;
    let resplit = rewrite_manifests(&small, "main", "re-split", None).await?;
    // 10 chunks at split size 4 -> edges [0, 4, 8, 10] -> 3 cells.
    assert_eq!(snapshot_manifests(&repo, &resplit).await?.len(), 3);

    assert_eq!(
        read_1d(&reopen_with_splitting(&repo, None)?, &array, 10).await?,
        expected_1d(10),
    );
    Ok(())
}

#[tokio::test]
async fn g09_reads_unchanged__a_readers_own_policy_does_not_affect_what_it_reads()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    let want = expected_1d(10);

    for policy in [
        None,
        Some(ManifestSplittingConfig::with_size(1)),
        Some(ManifestSplittingConfig::with_size(7)),
        Some(ManifestSplittingConfig::default()),
    ] {
        let reader = reopen_with_splitting(&repo, policy)?;
        assert_eq!(read_1d(&reader, &array, 10).await?, want);
    }
    Ok(())
}

// ===========================================================================
// g10_rewrite_manifests — the operation that re-lays-out an existing repository.
//   TEST_INTERFACE.md §3.9; guide/performance.md §"Rewriting manifests".
// ===========================================================================

#[tokio::test]
async fn g10_rewrite_manifests__creates_a_new_commit_on_the_branch() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    let before = branch_messages(&repo, "main").await?;

    let big = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(12)))?;
    let snap = rewrite_manifests(&big, "main", "consolidate manifests", None).await?;

    let after = branch_messages(&repo, "main").await?;
    assert_eq!(after.len(), before.len() + 1);
    assert_eq!(after.first().map(String::as_str), Some("consolidate manifests"));
    assert_eq!(repo.lookup_branch("main").await?, snap);
    Ok(())
}

#[tokio::test]
async fn g10_rewrite_manifests__stamps_the_active_policy_into_the_snapshot()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let first = write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;
    assert!(
        !repo.lookup_snapshot(&first).await?.metadata.contains_key("splitting_config"),
        "an ordinary commit does not stamp the policy",
    );

    let policy = ManifestSplittingConfig::with_size(12);
    let big = reopen_with_splitting(&repo, Some(policy.clone()))?;
    let snap = rewrite_manifests(&big, "main", "consolidate", None).await?;

    let metadata = repo.lookup_snapshot(&snap).await?.metadata;
    let stamped = metadata.get("splitting_config").ok_or("no splitting_config key")?;
    assert_eq!(stamped, &serde_json::to_value(&policy)?);
    Ok(())
}

#[tokio::test]
async fn g10_rewrite_manifests__is_also_available_on_a_session() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;

    let big = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(12)))?;
    let mut session = big.writable_session("main").await?;
    let snap = session.rewrite_manifests("consolidate in session", None).await?;

    assert_eq!(snapshot_manifests(&repo, &snap).await?.len(), 1);
    assert!(repo.lookup_snapshot(&snap).await?.metadata.contains_key("splitting_config"));
    assert_eq!(
        read_1d(&reopen_with_splitting(&repo, None)?, &array, 10).await?,
        expected_1d(10),
    );
    Ok(())
}

#[tokio::test]
async fn g10_rewrite_manifests__keeps_extra_commit_properties() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;

    let mut properties = icechunk::format::snapshot::SnapshotProperties::default();
    properties.insert("who".to_string(), serde_json::json!("verifier"));

    let big = reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(12)))?;
    let snap = rewrite_manifests(&big, "main", "consolidate", Some(properties)).await?;

    let metadata = repo.lookup_snapshot(&snap).await?.metadata;
    assert_eq!(metadata.get("who"), Some(&serde_json::json!("verifier")));
    assert!(metadata.contains_key("splitting_config"));
    Ok(())
}

// ===========================================================================
// g11_session_splits — the grid a session derives and caches per array.
//   TEST_INTERFACE.md §3.10.
// ===========================================================================

#[tokio::test]
async fn g11_session_splits__adding_an_array_caches_its_grid() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    let node = session.get_array(&array).await?;
    assert_eq!(
        session.lookup_splits(&node.id).cloned(),
        Some(ManifestSplits::from_edges(vec![vec![0, 3, 6, 9, 10]])),
    );
    Ok(())
}

#[tokio::test]
async fn g11_session_splits__renaming_a_dimension_regrids_the_array() -> TestResult {
    release_api!();
    let shape = ArrayShape::new(vec![(13, 1), (2, 1), (1, 1)]).ok_or("bad shape")?;
    let array = p("/temperature");
    let policy = ManifestSplittingConfig {
        split_sizes: Some(vec![
            rule(
                any_path(),
                vec![dim(
                    ManifestSplitDimCondition::DimensionName("^t$".to_string()),
                    3,
                )],
            ),
            rule(
                any_path(),
                vec![dim(
                    ManifestSplitDimCondition::DimensionName("time".to_string()),
                    4,
                )],
            ),
        ]),
    };
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(policy))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            shape.clone(),
            Some(vec!["t".into(), "y".into(), "x".into()]),
            Bytes::new(),
        )
        .await?;
    let node = session.get_array(&array).await?;
    assert_eq!(
        session.lookup_splits(&node.id).cloned(),
        Some(ManifestSplits::from_edges(vec![
            vec![0, 3, 6, 9, 12, 13],
            vec![0, 2],
            vec![0, 1],
        ])),
    );

    session
        .update_array(
            &array,
            shape,
            Some(vec!["time".into(), "y".into(), "x".into()]),
            Bytes::new(),
        )
        .await?;
    assert_eq!(
        session.lookup_splits(&node.id).cloned(),
        Some(ManifestSplits::from_edges(vec![
            vec![0, 4, 8, 12, 13],
            vec![0, 2],
            vec![0, 1],
        ])),
        "the second rule now claims axis 0",
    );
    Ok(())
}

#[tokio::test]
async fn g11_session_splits__data_survives_a_regrid_inside_one_session() -> TestResult {
    release_api!();
    let shape = ArrayShape::new(vec![(13, 1), (2, 1), (1, 1)]).ok_or("bad shape")?;
    let array = p("/temperature");
    let policy = ManifestSplittingConfig {
        split_sizes: Some(vec![
            rule(
                any_path(),
                vec![dim(
                    ManifestSplitDimCondition::DimensionName("^t$".to_string()),
                    3,
                )],
            ),
            rule(
                any_path(),
                vec![dim(
                    ManifestSplitDimCondition::DimensionName("time".to_string()),
                    4,
                )],
            ),
        ]),
    };
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(policy))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            shape.clone(),
            Some(vec!["t".into(), "y".into(), "x".into()]),
            Bytes::new(),
        )
        .await?;
    for i in 0..12u32 {
        session
            .set_chunk_ref(
                array.clone(),
                ChunkIndices(vec![i, 0, 0]),
                Some(ChunkPayload::Inline(Bytes::from(format!("{i}")))),
            )
            .await?;
    }
    session
        .update_array(
            &array,
            shape,
            Some(vec!["time".into(), "y".into(), "x".into()]),
            Bytes::new(),
        )
        .await?;
    for i in 0..12u32 {
        let got = get_chunk(
            session
                .get_chunk_reader(&array, &ChunkIndices(vec![i, 0, 0]), &ByteRange::ALL)
                .await?,
        )
        .await?
        .ok_or("chunk is missing after the regrid")?;
        assert_eq!(got, Bytes::from(format!("{i}")));
    }
    session.commit("regridded", None).await?;
    Ok(())
}

#[tokio::test]
async fn g11_session_splits__resizing_an_array_regrids_it() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    let node = session.get_array(&array).await?;
    assert_eq!(
        session.lookup_splits(&node.id).cloned(),
        Some(ManifestSplits::from_edges(vec![vec![0, 3, 6, 9, 10]])),
    );

    // A resize is the other way an open session's grid changes: the dimension
    // names are untouched, but the axis is longer, so it takes another cell.
    session
        .update_array(
            &array,
            ArrayShape::new(vec![(13, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    assert_eq!(
        session.lookup_splits(&node.id).cloned(),
        Some(ManifestSplits::from_edges(vec![vec![0, 3, 6, 9, 12, 13]])),
        "the longer axis is cut into one more cell",
    );
    Ok(())
}

#[tokio::test]
async fn g11_session_splits__a_coordinate_outside_the_array_is_rejected() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    let res = session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![99]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"nope"))),
        )
        .await;
    assert!(
        matches!(
            res,
            Err(SessionError { kind: SessionErrorKind::InvalidIndex { .. }, .. })
        ),
        "an out-of-bounds coordinate must be a named error, not a panic",
    );
    Ok(())
}

// ===========================================================================
// g12_preload_condition — the predicate that decides which manifests are
//   preloaded when a session opens.
//   TEST_INTERFACE.md §3.11; guide/performance.md §§"Configuring preloading",
//   "Default preload configuration"; PROFILE.md P6-4 (the undocumented
//   1000-reference cap, and why the *effect* is out of scope).
// ===========================================================================

#[tokio::test]
async fn g12_preload_condition__the_default_cap_is_ten_thousand_refs() -> TestResult {
    assert_eq!(ManifestPreloadConfig::default().max_total_refs(), 10_000);
    assert_eq!(RepositoryConfig::default().manifest().preload().max_total_refs(), 10_000);
    assert_eq!(
        ManifestPreloadConfig { max_total_refs: Some(3), preload_if: None }
            .max_total_refs(),
        3,
    );
    Ok(())
}

#[tokio::test]
async fn g12_preload_condition__the_default_selects_coordinate_names() -> TestResult {
    let cond = RepositoryConfig::default().manifest().preload().preload_if().clone();
    for name in ["/time", "/latitude", "/longitude", "/x", "/y", "/depth"] {
        assert!(cond.matches(&p(name), &info(1)), "{name} is a coordinate axis");
    }
    for name in ["/array", "/temperature", "/nottime"] {
        assert!(!cond.matches(&p(name), &info(1)), "{name} is not a coordinate axis");
    }
    Ok(())
}

#[tokio::test]
async fn g12_preload_condition__the_default_refuses_a_large_manifest() -> TestResult {
    let cond = RepositoryConfig::default().manifest().preload().preload_if().clone();
    assert!(cond.matches(&p("/time"), &info(1_000)));
    assert!(!cond.matches(&p("/time"), &info(1_001)), "the cap is 1000 references");
    assert!(!cond.matches(&p("/time"), &info(1_000_000)));
    Ok(())
}

#[tokio::test]
async fn g12_preload_condition__num_refs_honours_every_bound_kind() -> TestResult {
    let between = ManifestPreloadCondition::NumRefs {
        from: Bound::Included(1000),
        to: Bound::Excluded(10_000),
    };
    let path = p("/anything");
    assert!(!between.matches(&path, &info(999)));
    assert!(between.matches(&path, &info(1000)));
    assert!(between.matches(&path, &info(9_999)));
    assert!(!between.matches(&path, &info(10_000)));

    let unbounded = ManifestPreloadCondition::NumRefs {
        from: Bound::Unbounded,
        to: Bound::Unbounded,
    };
    assert!(unbounded.matches(&path, &info(0)));
    assert!(unbounded.matches(&path, &info(u32::MAX)));
    Ok(())
}

#[tokio::test]
async fn g12_preload_condition__combines_and_round_trips() -> TestResult {
    let path = p("/ocean/latitude");
    assert!(ManifestPreloadCondition::True.matches(&path, &info(1)));
    assert!(!ManifestPreloadCondition::False.matches(&path, &info(1)));

    let cond = ManifestPreloadCondition::And(vec![
        ManifestPreloadCondition::Or(vec![
            ManifestPreloadCondition::NameMatches { regex: "^latitude$".to_string() },
            ManifestPreloadCondition::NameMatches { regex: "^longitude$".to_string() },
        ]),
        ManifestPreloadCondition::NumRefs {
            from: Bound::Unbounded,
            to: Bound::Included(1000),
        },
    ]);
    assert!(cond.matches(&path, &info(10)));
    assert!(!cond.matches(&path, &info(1_001)));
    assert!(!cond.matches(&p("/ocean/temperature"), &info(10)));

    // `PathMatches` searches the whole path, unanchored, exactly as its
    // split-condition namesake does.
    let by_path = ManifestPreloadCondition::PathMatches { regex: "ocean".to_string() };
    assert!(by_path.matches(&path, &info(1)));
    assert!(by_path.matches(&p("/ocean/temperature"), &info(1)));
    assert!(!by_path.matches(&p("/atmosphere/latitude"), &info(1)));

    let back: ManifestPreloadCondition =
        serde_json::from_str(&serde_json::to_string(&cond)?)?;
    assert_eq!(back, cond);

    // `ManifestPreloadConfig::merge` is right-biased per field: a field the
    // right side leaves `None` keeps the left side's value.
    let left = ManifestPreloadConfig {
        max_total_refs: Some(5),
        preload_if: Some(ManifestPreloadCondition::False),
    };
    let merged = left.merge(ManifestPreloadConfig {
        max_total_refs: Some(7),
        preload_if: None,
    });
    assert_eq!(merged.max_total_refs(), 7);
    assert_eq!(merged.preload_if(), &ManifestPreloadCondition::False);
    let merged = left.merge(ManifestPreloadConfig {
        max_total_refs: None,
        preload_if: Some(ManifestPreloadCondition::True),
    });
    assert_eq!(merged.max_total_refs(), 5);
    assert_eq!(merged.preload_if(), &ManifestPreloadCondition::True);
    Ok(())
}

// ===========================================================================
// g13_initial_snapshot_id — the fixed first-snapshot id, so that a single
//   lookup identifies a prefix as an Icechunk repository.
//   TEST_INTERFACE.md §4.1; icechunk-spec/spec.md §"Initialize New Repository".
// ===========================================================================

#[tokio::test]
async fn g13_initial_snapshot_id__is_a_fixed_twelve_byte_constant() -> TestResult {
    let id = Snapshot::INITIAL_SNAPSHOT_ID;
    assert_eq!(id.0.len(), 12);
    assert_eq!(
        id.0,
        [0x0b, 0x1c, 0xc8, 0xd6, 0x78, 0x75, 0x80, 0xf0, 0xe3, 0x3a, 0x65, 0x34],
    );
    assert_ne!(id, SnapshotId::FAKE);
    Ok(())
}

#[tokio::test]
async fn g13_initial_snapshot_id__renders_as_the_well_known_object_name() -> TestResult {
    assert_eq!(String::from(&Snapshot::INITIAL_SNAPSHOT_ID), "1CECHNKREP0F1RSTCMT0");
    assert_eq!(Snapshot::INITIAL_SNAPSHOT_ID.to_string(), "1CECHNKREP0F1RSTCMT0");
    assert_eq!(
        SnapshotId::try_from("1CECHNKREP0F1RSTCMT0")?,
        Snapshot::INITIAL_SNAPSHOT_ID,
    );
    Ok(())
}

#[tokio::test]
async fn g13_initial_snapshot_id__one_lookup_identifies_a_repository() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let root = repo.lookup_snapshot(&Snapshot::INITIAL_SNAPSHOT_ID).await?;
    assert_eq!(root.id, Snapshot::INITIAL_SNAPSHOT_ID);
    assert_eq!(root.message, Snapshot::INITIAL_COMMIT_MESSAGE);
    assert_eq!(Snapshot::INITIAL_COMMIT_MESSAGE, "Repository initialized");
    assert!(root.parent_id.is_none());
    assert!(root.is_initial());
    assert!(root.manifests.is_empty());

    // `open_or_create` reaches the same fixed root from either side: it creates
    // a repository where there is none, and opens the one it just made.
    let fresh = mem().await?;
    assert!(!Repository::exists(fresh.as_ref()).await?);
    let made =
        Repository::open_or_create(None, Arc::clone(&fresh), HashMap::new()).await?;
    assert!(Repository::exists(fresh.as_ref()).await?);
    assert_eq!(made.lookup_branch("main").await?, Snapshot::INITIAL_SNAPSHOT_ID);
    let reopened =
        Repository::open_or_create(None, Arc::clone(&fresh), HashMap::new()).await?;
    assert_eq!(
        reopened.lookup_snapshot(&Snapshot::INITIAL_SNAPSHOT_ID).await?.message,
        Snapshot::INITIAL_COMMIT_MESSAGE,
    );
    Ok(())
}

#[tokio::test]
async fn g13_initial_snapshot_id__is_the_root_of_every_ancestry() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let first = session.commit("first", None).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/g"), Bytes::new()).await?;
    let second = session.commit("second", None).await?;

    let ancestry = repo
        .ancestry(&VersionInfo::SnapshotId(second.clone()))
        .await?
        .try_collect::<Vec<_>>()
        .await?;
    assert_eq!(
        ancestry.iter().map(|s| s.id.clone()).collect::<Vec<_>>(),
        vec![second, first, Snapshot::INITIAL_SNAPSHOT_ID],
    );
    assert_eq!(
        ancestry.last().ok_or("empty ancestry")?.message,
        Snapshot::INITIAL_COMMIT_MESSAGE,
    );
    Ok(())
}

#[tokio::test]
async fn g13_initial_snapshot_id__is_the_same_in_every_repository() -> TestResult {
    let one = Repository::create(None, mem().await?, HashMap::new()).await?;
    let two = Repository::create(None, mem().await?, HashMap::new()).await?;
    assert_eq!(
        one.lookup_snapshot(&Snapshot::INITIAL_SNAPSHOT_ID).await?.id,
        two.lookup_snapshot(&Snapshot::INITIAL_SNAPSHOT_ID).await?.id,
        "the first snapshot id is no longer random",
    );
    // A prefix that holds no repository does not answer that lookup.
    let empty = mem().await?;
    assert!(!Repository::exists(empty.as_ref()).await?);
    Ok(())
}

// ===========================================================================
// g14_commit_rebasing — commit-then-rebase in a single call.
//   TEST_INTERFACE.md §4.2; guide/version-control.md §"Conflict Resolution".
//   The Rust spelling is `Session::commit_rebasing`; there is no `rebase_with`
//   identifier and no `Repository::commit` (PROFILE.md P6-3).
// ===========================================================================

#[tokio::test]
async fn g14_commit_rebasing__is_a_plain_commit_when_nothing_conflicts() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session
        .commit_rebasing(&ConflictDetector, 3, "no conflict", None, |_| async {}, |_| async {})
        .await?;
    assert_eq!(repo.lookup_branch("main").await?, snap);
    assert_eq!(branch_messages(&repo, "main").await?.first().map(String::as_str), Some("no conflict"));
    Ok(())
}

#[tokio::test]
async fn g14_commit_rebasing__solves_a_disjoint_conflict_and_commits() -> TestResult {
    let storage = mem().await?;
    let repo = Arc::new(Repository::create(None, Arc::clone(&storage), HashMap::new()).await?);
    let array = p("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    session.commit("create array", None).await?;

    // Ours touches chunk 1 ...
    let mut ours = repo.writable_session("main").await?;
    ours.set_chunk_ref(
        array.clone(),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"ours"))),
    )
    .await?;

    // ... and a concurrent writer commits chunk 2 first.
    let mut theirs = repo.writable_session("main").await?;
    theirs
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![2]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"theirs"))),
        )
        .await?;
    theirs.commit("theirs", None).await?;

    let solver = BasicConflictSolver::default();
    let snap = ours
        .commit_rebasing(&solver, 3, "ours", None, |_| async {}, |_| async {})
        .await?;
    assert_eq!(repo.lookup_branch("main").await?, snap);

    let reader = repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    for (coord, want) in [(1u32, "ours"), (2u32, "theirs")] {
        let got = get_chunk(
            reader
                .get_chunk_reader(&array, &ChunkIndices(vec![coord]), &ByteRange::ALL)
                .await?,
        )
        .await?
        .ok_or("chunk is missing")?;
        assert_eq!(got, Bytes::from(want));
    }
    Ok(())
}

#[tokio::test]
async fn g14_commit_rebasing__gives_up_after_the_configured_attempts() -> TestResult {
    let storage = mem().await?;
    let repo = Arc::new(Repository::create(None, Arc::clone(&storage), HashMap::new()).await?);
    let array = p("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    session.commit("create array", None).await?;

    let mut ours = repo.writable_session("main").await?;
    ours.set_chunk_ref(
        array.clone(),
        ChunkIndices(vec![1]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"ours"))),
    )
    .await?;

    // Seed the first conflict, so the very first commit attempt fails. The
    // concurrent writer touches a DIFFERENT chunk, so every rebase itself
    // succeeds and it is only the branch moving again that defeats the commit.
    let mut seed = repo.writable_session("main").await?;
    seed.set_chunk_ref(
        array.clone(),
        ChunkIndices(vec![2]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"theirs"))),
    )
    .await?;
    seed.commit("conflicting", None).await?;

    let repo_ref = &repo;
    let attempts = AtomicU16::new(0);
    let attempts_ref = &attempts;
    // `before_rebase` must fire exactly as often, and with the same attempt
    // numbers, as `after_rebase`.
    let before_attempts = AtomicU16::new(0);
    let before_ref = &before_attempts;
    let announcing = |attempt: u16| async move {
        before_ref.fetch_add(1, Ordering::SeqCst);
        assert_eq!(attempt, before_ref.load(Ordering::SeqCst));
    };
    // A writer that re-conflicts after every rebase, so no attempt can ever win.
    let conflicting = |attempt: u16| async move {
        attempts_ref.fetch_add(1, Ordering::SeqCst);
        assert_eq!(attempt, attempts_ref.load(Ordering::SeqCst));
        let mut other = Arc::clone(repo_ref).writable_session("main").await.unwrap();
        other
            .set_chunk_ref(
                Path::try_from("/array").unwrap(),
                ChunkIndices(vec![2]),
                Some(ChunkPayload::Inline(Bytes::from_static(b"theirs again"))),
            )
            .await
            .unwrap();
        other.commit("conflicting", None).await.unwrap();
    };

    let res = ours
        .commit_rebasing(&ConflictDetector, 3, "ours", None, announcing, conflicting)
        .await;
    assert!(
        matches!(
            res,
            Err(SessionError { kind: SessionErrorKind::Conflict { .. }, .. })
        ),
        "a conflict that cannot be solved must surface as SessionErrorKind::Conflict",
    );
    assert_eq!(attempts.into_inner(), 3, "it rebases exactly three times, then gives up");
    assert_eq!(
        before_attempts.into_inner(),
        3,
        "each callback runs exactly `rebase_attempts` times",
    );
    Ok(())
}

#[tokio::test]
async fn g14_commit_rebasing__merge_takes_a_session() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let array = p("/array");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    session.commit("create array", None).await?;

    // Two workers write disjoint chunks; their sessions travel as bytes and are
    // reduced into one before a single commit.
    let mut worker_a = repo.writable_session("main").await?;
    worker_a
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"a"))),
        )
        .await?;
    let mut worker_b = repo.writable_session("main").await?;
    worker_b
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"b"))),
        )
        .await?;

    let shipped = Session::from_bytes(worker_b.as_bytes()?)?;
    worker_a.merge(shipped).await?;
    worker_a.commit("reduced", None).await?;

    let reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    for (coord, want) in [(0u32, "a"), (1u32, "b")] {
        let got = get_chunk(
            reader
                .get_chunk_reader(&array, &ChunkIndices(vec![coord]), &ByteRange::ALL)
                .await?,
        )
        .await?
        .ok_or("chunk is missing")?;
        assert_eq!(got, Bytes::from(want));
    }
    Ok(())
}

#[tokio::test]
async fn g14_commit_rebasing__merging_incompatible_grids_is_refused() -> TestResult {
    release_api!();
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_with_splitting(Some(ManifestSplittingConfig::with_size(3)))),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(10, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    session.commit("create array", None).await?;

    // Two writers over the same array whose splitting policies disagree: the
    // grids are [0,3,6,9,10) against [0,4,8,10), which overlap only partially.
    let coarser =
        reopen_with_splitting(&repo, Some(ManifestSplittingConfig::with_size(4)))?;
    let mut ours = repo.writable_session("main").await?;
    ours.set_chunk_ref(
        array.clone(),
        ChunkIndices(vec![0]),
        Some(ChunkPayload::Inline(Bytes::from_static(b"ours"))),
    )
    .await?;
    let mut theirs = coarser.writable_session("main").await?;
    theirs
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![1]),
            Some(ChunkPayload::Inline(Bytes::from_static(b"theirs"))),
        )
        .await?;

    let res = ours.merge(theirs).await;
    assert!(
        matches!(
            res,
            Err(SessionError {
                kind: SessionErrorKind::IncompatibleSplittingConfig { .. },
                ..
            })
        ),
        "merging two sessions whose grids disagree must be IncompatibleSplittingConfig",
    );
    Ok(())
}

// ===========================================================================
// g15_retries_settings — `storage::RetriesSettings`.
//   TEST_INTERFACE.md §4.3; guide/performance.md §"Cold buckets and repos".
//   Only construction, defaults, merging and propagation are scored; see
//   §"what is not scored" for why the retry behaviour itself is not.
// ===========================================================================

#[tokio::test]
async fn g15_retries_settings__defaults_come_from_the_accessors() -> TestResult {
    release_api!();
    let defaults = RetriesSettings::default();
    assert_eq!(defaults.max_tries, None);
    assert_eq!(defaults.initial_backoff_ms, None);
    assert_eq!(defaults.max_backoff_ms, None);
    assert_eq!(defaults.max_tries().get(), 10);
    assert_eq!(defaults.initial_backoff_ms(), 100);
    assert_eq!(defaults.max_backoff_ms(), 3 * 60 * 1000);
    Ok(())
}

#[tokio::test]
async fn g15_retries_settings__merge_is_right_biased_per_field() -> TestResult {
    release_api!();
    let base = RetriesSettings {
        max_tries: NonZeroU16::new(4),
        initial_backoff_ms: Some(7),
        max_backoff_ms: Some(11),
    };
    let merged = base.merge(RetriesSettings {
        max_tries: NonZeroU16::new(3),
        ..Default::default()
    });
    assert_eq!(merged.max_tries().get(), 3);
    assert_eq!(merged.initial_backoff_ms(), 7);
    assert_eq!(merged.max_backoff_ms(), 11);

    let untouched = base.merge(RetriesSettings::default());
    assert_eq!(untouched.max_tries().get(), 4);
    Ok(())
}

#[tokio::test]
async fn g15_retries_settings__the_in_memory_backend_disables_retries() -> TestResult {
    let storage = mem().await?;
    let settings = storage.default_settings();
    assert_eq!(settings.retries().max_tries().get(), 1);
    assert_eq!(settings.retries().initial_backoff_ms(), 0);
    assert_eq!(settings.retries().max_backoff_ms(), 0);
    Ok(())
}

#[tokio::test]
async fn g15_retries_settings__a_repository_config_overrides_the_backend()
-> TestResult {
    release_api!();
    let storage = mem().await?;
    let settings = storage::Settings {
        retries: Some(RetriesSettings {
            max_tries: NonZeroU16::new(5),
            initial_backoff_ms: Some(7),
            max_backoff_ms: Some(11),
        }),
        ..storage.default_settings()
    };
    let repo = Repository::create(
        Some(RepositoryConfig { storage: Some(settings), ..Default::default() }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let retries = repo.storage_settings().retries();
    assert_eq!(retries.max_tries().get(), 5);
    assert_eq!(retries.initial_backoff_ms(), 7);
    assert_eq!(retries.max_backoff_ms(), 11);

    // And it survives the serde round trip the configuration file uses.
    let back: storage::Settings = serde_json::from_str(&serde_json::to_string(
        repo.storage_settings(),
    )?)?;
    assert_eq!(back.retries().max_tries().get(), 5);
    Ok(())
}

// ===========================================================================
// g16_virtual_authorization — virtual chunk containers must now be declared
//   and authorised explicitly, by url prefix.
//   TEST_INTERFACE.md §4.4 and §5; guide/migration.md §"Virtual Datasets";
//   guide/configuration.md §"Virtual Chunk Credentials".
// ===========================================================================

#[tokio::test]
async fn g16_virtual_authorization__there_are_no_default_containers() -> TestResult {
    let config = RepositoryConfig::default();
    assert_eq!(config.virtual_chunk_containers().count(), 0);
    for prefix in ["s3://", "gcs://", "az://", "tigris://", "file://"] {
        assert!(
            config.get_virtual_chunk_container(prefix).is_none(),
            "{prefix} must not resolve to a built-in container",
        );
    }

    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(repo.config().virtual_chunk_containers().count(), 0);
    Ok(())
}

#[tokio::test]
async fn g16_virtual_authorization__containers_are_keyed_by_url_prefix() -> TestResult {
    let container = VirtualChunkContainer::new(
        "file:///example".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/example")),
    )?;
    assert_eq!(container.url_prefix(), "file:///example");

    let mut config = RepositoryConfig::default();
    config.set_virtual_chunk_container(container.clone());
    assert_eq!(config.virtual_chunk_containers().count(), 1);
    assert_eq!(
        config.get_virtual_chunk_container("file:///example").map(|c| c.url_prefix()),
        Some("file:///example"),
    );
    config.clear_virtual_chunk_containers();
    assert_eq!(config.virtual_chunk_containers().count(), 0);

    // The constructor validates the prefix against the store type.
    assert!(
        VirtualChunkContainer::new(
            "http://".to_string(),
            ObjectStoreConfig::Http(HashMap::new()),
        )
        .is_err(),
        "a prefix without a host is refused",
    );
    assert!(
        VirtualChunkContainer::new(
            "s3://bucket".to_string(),
            ObjectStoreConfig::LocalFileSystem(PathBuf::from("/example")),
        )
        .is_err(),
        "the scheme must agree with the store configuration",
    );
    Ok(())
}

#[tokio::test]
async fn g16_virtual_authorization__a_container_validates_its_credentials() -> TestResult
{
    let local = VirtualChunkContainer::new(
        "file:///example".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/example")),
    )?;
    assert!(local.validate_credentials(None).is_ok(), "every store accepts None");
    assert!(
        local
            .validate_credentials(Some(&Credentials::S3(S3Credentials::Anonymous)))
            .is_err(),
        "a local-filesystem container takes no credential at all",
    );

    // The HTTP store exists and validates its own prefix, and it takes no
    // credential either (§4.4; nothing fetches over it, see §7).
    let http = VirtualChunkContainer::new(
        "http://example.com/data".to_string(),
        ObjectStoreConfig::Http(HashMap::new()),
    )?;
    assert!(http.validate_credentials(None).is_ok());
    assert!(
        http.validate_credentials(Some(&Credentials::S3(S3Credentials::Anonymous)))
            .is_err(),
    );

    // In-memory takes no credential either. It has no url scheme of its own, so
    // it is reached the way a persisted configuration reaches it: through the
    // public `store` field (§5.7).
    let mut in_memory = VirtualChunkContainer::new(
        "file:///example".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    )?;
    in_memory.store = ObjectStoreConfig::InMemory;
    assert!(in_memory.validate_credentials(None).is_ok());
    assert!(
        in_memory
            .validate_credentials(Some(&Credentials::S3(S3Credentials::Anonymous)))
            .is_err(),
        "in-memory storage takes no credential",
    );

    // The whole of §4.4's store x credential table, for the five stores that
    // do take a credential. Nothing here reaches an endpoint: the check is a
    // pure match on the pair.
    let s3 = || S3Options {
        region: None,
        endpoint_url: None,
        anonymous: true,
        allow_http: false,
        force_path_style: false,
    };
    let s3_cred = Credentials::S3(S3Credentials::Anonymous);
    let gcs_cred = Credentials::Gcs(GcsCredentials::Anonymous);
    let azure_cred = Credentials::Azure(AzureCredentials::FromEnv);

    for (prefix, store, accepted) in [
        ("s3://bucket/data", ObjectStoreConfig::S3(s3()), &s3_cred),
        ("s3://bucket/data", ObjectStoreConfig::S3Compatible(s3()), &s3_cred),
        ("tigris://bucket/data", ObjectStoreConfig::Tigris(s3()), &s3_cred),
        ("gcs://bucket/data", ObjectStoreConfig::Gcs(HashMap::new()), &gcs_cred),
        ("az://account/data", ObjectStoreConfig::Azure(HashMap::new()), &azure_cred),
    ] {
        let container = VirtualChunkContainer::new(prefix.to_string(), store)?;
        assert!(
            container.validate_credentials(None).is_ok(),
            "{prefix}: every store accepts None",
        );
        assert!(
            container.validate_credentials(Some(accepted)).is_ok(),
            "{prefix}: the store's own credential kind is accepted",
        );
        for wrong in [&s3_cred, &gcs_cred, &azure_cred] {
            if std::mem::discriminant(wrong) == std::mem::discriminant(accepted) {
                continue;
            }
            assert!(
                container.validate_credentials(Some(wrong)).is_err(),
                "{prefix}: a credential of another store's kind is refused",
            );
        }
    }

    // The repository constructors run the same check over every prefix they are
    // asked to authorise, so a credential the store cannot take is refused.
    let containers: HashMap<String, VirtualChunkContainer> =
        [(local.url_prefix().to_string(), local.clone())].into_iter().collect();
    let credentials: HashMap<String, Option<Credentials>> = [(
        local.url_prefix().to_string(),
        Some(Credentials::S3(S3Credentials::Anonymous)),
    )]
    .into_iter()
    .collect();
    let storage = mem().await?;
    let res = Repository::create(
        Some(RepositoryConfig {
            virtual_chunk_containers: Some(containers),
            ..Default::default()
        }),
        Arc::clone(&storage),
        credentials,
    )
    .await;
    assert!(
        res.is_err(),
        "authorising a declared container with a credential it cannot take is refused",
    );
    Ok(())
}

#[tokio::test]
async fn g16_virtual_authorization__an_invalid_container_is_dropped() -> TestResult {
    // A container can only be built through the validating constructor, but its
    // `store` field is public, so a configuration can still carry one whose
    // store no longer matches its prefix — exactly what a persisted, edited
    // configuration looks like. The resolver drops such a container silently.
    let mut broken = VirtualChunkContainer::new(
        "file:///example".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/example")),
    )?;
    broken.store = ObjectStoreConfig::InMemory;
    assert!(
        VirtualChunkContainer::new("file:///example".to_string(), broken.store.clone())
            .is_err(),
        "the mutated container is one the constructor would now reject",
    );

    // The prefix IS authorised, so had the container survived this would have
    // been an authorisation success and an I/O attempt, never NoContainerForUrl.
    let credentials: HashMap<String, Option<Credentials>> =
        [("file:///example".to_string(), None)].into_iter().collect();
    let resolver =
        VirtualChunkResolver::new([broken].into_iter(), credentials, Default::default());
    let res = resolver.fetch_chunk("file:///example/foo.nc", &(0u64..10), None).await;
    assert!(
        matches!(
            res,
            Err(VirtualReferenceError {
                kind: VirtualReferenceErrorKind::NoContainerForUrl(_),
                ..
            })
        ),
        "an invalid container is dropped, so no container claims the url",
    );
    Ok(())
}

#[tokio::test]
async fn g16_virtual_authorization__an_unauthorized_prefix_is_refused() -> TestResult {
    let container = VirtualChunkContainer::new(
        "file:///example".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/example")),
    )?;
    // An empty authorisation map authorises nothing.
    let resolver = VirtualChunkResolver::new(
        [container].into_iter(),
        HashMap::new(),
        Default::default(),
    );
    let res = resolver.fetch_chunk("file:///example/foo.nc", &(0u64..100), None).await;
    assert!(
        matches!(
            res,
            Err(VirtualReferenceError {
                kind: VirtualReferenceErrorKind::UnauthorizedVirtualChunkContainer(ref c),
                ..
            }) if c.url_prefix() == "file:///example"
        ),
        "an unauthorised container must be refused before any I/O",
    );
    Ok(())
}

#[tokio::test]
async fn g16_virtual_authorization__a_missing_container_is_a_different_error()
-> TestResult {
    let container = VirtualChunkContainer::new(
        "file:///other".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/other")),
    )?;
    let credentials: HashMap<String, Option<Credentials>> =
        [("file:///other".to_string(), None)].into_iter().collect();
    let resolver = VirtualChunkResolver::new(
        [container].into_iter(),
        credentials,
        Default::default(),
    );
    let res = resolver.fetch_chunk("file:///example/foo.nc", &(0u64..100), None).await;
    assert!(
        matches!(
            res,
            Err(VirtualReferenceError {
                kind: VirtualReferenceErrorKind::NoContainerForUrl(_),
                ..
            })
        ),
        "a url no container claims is NoContainerForUrl, not Unauthorized",
    );
    Ok(())
}

#[tokio::test]
async fn g16_virtual_authorization__an_authorized_container_resolves() -> TestResult {
    let chunks = tempfile::TempDir::new()?;
    let chunk_file = chunks.path().join("chunk-1");
    tokio::fs::write(&chunk_file, b"hello virtual").await?;
    let prefix = format!("file://{}", chunks.path().to_str().ok_or("bad temp path")?);

    let container = VirtualChunkContainer::new(
        prefix.clone(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::new()),
    )?;
    let containers: HashMap<String, VirtualChunkContainer> =
        [(container.url_prefix().to_string(), container)].into_iter().collect();
    // `Some(None)` is "authorised, use the natural credential for this store".
    let credentials: HashMap<String, Option<Credentials>> =
        [(prefix, None)].into_iter().collect();

    let storage = mem().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            virtual_chunk_containers: Some(containers),
            ..Default::default()
        }),
        Arc::clone(&storage),
        credentials,
    )
    .await?;
    assert_eq!(repo.config().virtual_chunk_containers().count(), 1);

    let array = p("/array");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(1, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    session
        .set_chunk_ref(
            array.clone(),
            ChunkIndices(vec![0]),
            Some(ChunkPayload::Virtual(VirtualChunkRef {
                location: VirtualChunkLocation::from_absolute_path(&format!(
                    "file://{}",
                    chunk_file.to_str().ok_or("bad chunk path")?
                ))?,
                offset: 0,
                length: 13,
                checksum: None,
            })),
        )
        .await?;
    session.commit("one virtual chunk", None).await?;

    let reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    let got = get_chunk(
        reader.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?
    .ok_or("virtual chunk did not resolve")?;
    assert_eq!(got, Bytes::from_static(b"hello virtual"));

    // `reopen` with `None` for the authorisation map keeps the current one
    // (§2), so the same virtual chunk still resolves through the reopened
    // repository even though no credential was passed again.
    let reopened = repo.reopen(None, None)?;
    let reader = reopened
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    let got = get_chunk(
        reader.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await?,
    )
    .await?
    .ok_or("virtual chunk did not resolve after reopen")?;
    assert_eq!(got, Bytes::from_static(b"hello virtual"));

    // An explicit map replaces it, so an empty one withdraws the authorisation
    // and the same read fails.
    let withdrawn = repo.reopen(None, Some(HashMap::new()))?;
    let reader = withdrawn
        .readonly_session(&VersionInfo::BranchTipRef("main".to_string()))
        .await?;
    let res = reader.get_chunk_reader(&array, &ChunkIndices(vec![0]), &ByteRange::ALL).await;
    let res = match res {
        Ok(reader) => get_chunk(reader).await.map(|_| ()),
        Err(err) => Err(err),
    };
    assert!(res.is_err(), "an emptied authorisation map refuses the same read");
    Ok(())
}

// ===========================================================================
// i01_object_layout — inherited from milestone 01: the object layout and the
//   two kinds of reference.
//   TEST_INTERFACE.md §6.1; icechunk-spec/spec.md §§"File Layout",
//   "Creating and Updating Branches", "Tags".
// ===========================================================================

#[tokio::test]
async fn i01_object_layout__a_repository_uses_the_specified_prefixes() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&root).await?;
    // Threshold zero forces every chunk into its own object under chunks/.
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..config_without_preload()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;

    let array = p("/temperature");
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            Some(vec!["t".into()]),
            Bytes::new(),
        )
        .await?;
    for i in 0..4u32 {
        let payload = session.get_chunk_writer()(Bytes::from(format!("chunk{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    session.commit("first", None).await?;
    repo.save_config().await?;

    for sub in ["refs", "snapshots", "manifests", "transactions", "chunks"] {
        assert!(root.join(sub).is_dir(), "{sub}/ must exist");
    }
    assert!(root.join("config.yaml").is_file(), "config.yaml holds the configuration");
    assert!(!root.join("attributes").exists(), "there is no attributes/ prefix");
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__a_branch_is_one_json_file() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&root).await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;

    let main_ref = root.join("refs").join("branch.main").join("ref.json");
    assert!(main_ref.is_file(), "refs/branch.main/ref.json is the branch");
    let body = std::fs::read_to_string(&main_ref)?;
    // The file names the snapshot the branch points at. *Which* id a fresh
    // repository starts from is §4.1's subject and g13's; here the branch file
    // is what is being checked.
    assert!(body.contains(&String::from(&repo.lookup_branch("main").await?)));

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("first", None).await?;
    repo.create_branch("other", &snap).await?;
    assert!(root.join("refs").join("branch.other").join("ref.json").is_file());
    assert_eq!(
        repo.list_branches().await?.into_iter().collect::<Vec<_>>(),
        vec!["main".to_string(), "other".to_string()],
    );
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__tags_are_created_listed_and_deleted() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    let snap = session.commit("first", None).await?;

    repo.create_tag("v1", &snap).await?;
    assert_eq!(
        repo.list_tags().await?.into_iter().collect::<Vec<_>>(),
        vec!["v1".to_string()],
    );
    assert_eq!(repo.lookup_tag("v1").await?, snap);
    // A tag names a version: a read-only session opens at `VersionInfo::TagRef`.
    let tagged = repo.readonly_session(&VersionInfo::TagRef("v1".to_string())).await?;
    assert!(tagged.read_only());
    assert_eq!(tagged.branch(), None);
    assert_eq!(
        tagged.list_nodes().await?.collect::<Result<Vec<_>, _>>()?.len(),
        1,
        "the tagged snapshot holds the one group that was committed",
    );
    // A tag is immutable: the same name cannot be created twice.
    assert!(repo.create_tag("v1", &snap).await.is_err());

    repo.delete_tag("v1").await?;
    assert!(repo.list_tags().await?.is_empty());
    // ... and the name stays burnt.
    assert!(repo.create_tag("v1", &snap).await.is_err());
    Ok(())
}

#[tokio::test]
async fn i01_object_layout__object_ids_render_as_crockford_base32() -> TestResult {
    let id = SnapshotId::new([0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11]);
    assert_eq!(id.0.len(), 12);
    assert_eq!(String::from(&id), "000G40R40M30E209185G");
    assert_eq!(SnapshotId::try_from("000G40R40M30E209185G")?, id);
    assert!(SnapshotId::try_from("000G").is_err());

    let mut seen = HashSet::new();
    for _ in 0..32 {
        seen.insert(SnapshotId::random());
    }
    assert_eq!(seen.len(), 32, "random ids must not collide");
    Ok(())
}

// ===========================================================================
// i02_sessions_conflicts — inherited from milestone 02: sessions, commit
//   conflicts, rebase and virtual chunk containers.
//   TEST_INTERFACE.md §6.2; guide/version-control.md.
// ===========================================================================

#[tokio::test]
async fn i02_sessions_conflicts__a_readonly_session_cannot_write() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    assert!(!session.read_only());
    session.add_group(Path::root(), Bytes::new()).await?;
    assert!(session.has_uncommitted_changes());
    let snap = session.commit("first", None).await?;

    let mut reader = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    assert!(reader.read_only());
    assert_eq!(reader.branch(), None);
    // Edits are recorded in memory, but a read-only session can never publish
    // them: there is no branch for the commit to advance.
    reader.add_group(p("/nope"), Bytes::new()).await?;
    let res = reader.commit("from a read-only session", None).await;
    assert!(matches!(
        res,
        Err(SessionError { kind: SessionErrorKind::ReadOnlySession, .. })
    ));
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__a_stale_session_cannot_commit() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut setup = repo.writable_session("main").await?;
    setup.add_group(Path::root(), Bytes::new()).await?;
    setup.commit("first", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.add_group(p("/a"), Bytes::new()).await?;
    b.add_group(p("/b"), Bytes::new()).await?;
    a.commit("a wins", None).await?;
    let res = b.commit("b loses", None).await;
    assert!(matches!(
        res,
        Err(SessionError { kind: SessionErrorKind::Conflict { .. }, .. })
    ));
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__rebase_resolves_a_disjoint_change() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let mut setup = repo.writable_session("main").await?;
    setup.add_group(Path::root(), Bytes::new()).await?;
    setup.commit("first", None).await?;

    let mut a = repo.writable_session("main").await?;
    let mut b = repo.writable_session("main").await?;
    a.add_group(p("/a"), Bytes::new()).await?;
    b.add_group(p("/b"), Bytes::new()).await?;
    a.commit("a", None).await?;
    assert!(b.commit("b", None).await.is_err());

    b.rebase(&BasicConflictSolver::default()).await?;
    b.commit("b after rebase", None).await?;

    let reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    assert!(reader.get_group(&p("/a")).await.is_ok());
    assert!(reader.get_group(&p("/b")).await.is_ok());
    Ok(())
}

#[tokio::test]
async fn i02_sessions_conflicts__virtual_containers_are_part_of_the_config()
-> TestResult {
    let container = VirtualChunkContainer::new(
        "file:///data".to_string(),
        ObjectStoreConfig::LocalFileSystem(PathBuf::from("/data")),
    )?;
    let containers: HashMap<String, VirtualChunkContainer> =
        [(container.url_prefix().to_string(), container)].into_iter().collect();
    let credentials: HashMap<String, Option<Credentials>> =
        [("file:///data".to_string(), None)].into_iter().collect();

    let storage = mem().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            virtual_chunk_containers: Some(containers),
            ..Default::default()
        }),
        Arc::clone(&storage),
        credentials.clone(),
    )
    .await?;
    repo.save_config().await?;

    // The configuration is persisted, so a later open sees the container.
    let reopened = Repository::open(None, Arc::clone(&storage), credentials).await?;
    assert_eq!(
        reopened
            .config()
            .get_virtual_chunk_container("file:///data")
            .map(|c| c.url_prefix().to_string()),
        Some("file:///data".to_string()),
    );
    Ok(())
}

// ===========================================================================
// i03_expiration_gc — inherited from milestone 03: expiration and garbage
//   collection.
//   TEST_INTERFACE.md §6.3; guide/expiration.md.
// ===========================================================================

#[tokio::test]
async fn i03_expiration_gc__collects_objects_no_reference_can_reach() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..config_without_preload()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/array");
    let settings = storage.default_settings();

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..4u32 {
        let payload = session.get_chunk_writer()(Bytes::from(format!("first{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    let first = session.commit("first", None).await?;

    let mut session = repo.writable_session("main").await?;
    for i in 0..4u32 {
        let payload =
            session.get_chunk_writer()(Bytes::from(format!("second{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    session.commit("second", None).await?;
    assert_eq!(storage.list_chunks(&settings).await?.count().await, 8);

    // Forget the second commit, then collect what it alone kept alive.
    repo.reset_branch("main", &first).await?;
    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert_eq!(summary.chunks_deleted, 4);
    assert_eq!(summary.snapshots_deleted, 1);
    assert_eq!(summary.manifests_deleted, 1, "the dropped commit's manifest too");
    assert!(summary.bytes_deleted > 0);
    assert_eq!(storage.list_chunks(&settings).await?.count().await, 4);

    // The surviving commit still reads correctly.
    let reader =
        repo.readonly_session(&VersionInfo::BranchTipRef("main".to_string())).await?;
    for i in 0..4u32 {
        let got = get_chunk(
            reader
                .get_chunk_reader(&array, &ChunkIndices(vec![i]), &ByteRange::ALL)
                .await?,
        )
        .await?
        .ok_or("chunk is missing")?;
        assert_eq!(got, Bytes::from(format!("first{i}")));
    }
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__never_deletes_a_reachable_object() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(
        Some(RepositoryConfig {
            inline_chunk_threshold_bytes: Some(0),
            ..config_without_preload()
        }),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/array");
    let settings = storage.default_settings();

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session
        .add_array(
            array.clone(),
            ArrayShape::new(vec![(4, 1)]).ok_or("bad shape")?,
            None,
            Bytes::new(),
        )
        .await?;
    for i in 0..4u32 {
        let payload = session.get_chunk_writer()(Bytes::from(format!("keep{i}"))).await?;
        session.set_chunk_ref(array.clone(), ChunkIndices(vec![i]), Some(payload)).await?;
    }
    session.commit("only commit", None).await?;

    let now = Utc::now();
    let summary = garbage_collect(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        &GCConfig::clean_all(now, now, None),
    )
    .await?;
    assert_eq!(summary, GCSummary::default(), "nothing is unreachable");
    assert_eq!(storage.list_chunks(&settings).await?.count().await, 4);
    Ok(())
}

#[tokio::test]
async fn i03_expiration_gc__expire_shortens_an_ancestry() -> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    let settings = storage.default_settings();

    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("1", None).await?;
    for name in ["2", "3"] {
        let mut session = repo.writable_session("main").await?;
        session.add_group(p(&format!("/g{name}")), Bytes::new()).await?;
        session.commit(name, None).await?;
    }
    assert_eq!(
        branch_messages(&repo, "main").await?,
        vec![
            "3".to_string(),
            "2".to_string(),
            "1".to_string(),
            Snapshot::INITIAL_COMMIT_MESSAGE.to_string(),
        ],
    );

    let older: HashSet<SnapshotId> = repo
        .ancestry(&VersionInfo::BranchTipRef("main".to_string()))
        .await?
        .try_collect::<Vec<_>>()
        .await?
        .into_iter()
        // Every commit except the repository's root snapshot, which `expire`
        // never releases.
        .filter(|info| !info.is_initial())
        .map(|info| info.id)
        .collect();
    assert_eq!(older.len(), 3);

    let mut session = repo.writable_session("main").await?;
    session.add_group(p("/kept"), Bytes::new()).await?;
    let survivor = session.commit("4", None).await?;
    // The cutoff is the surviving commit's own `flushed_at`, never a clock read:
    // `expire` keeps every snapshot whose `flushed_at` is at or after it, so the
    // boundary is exact and no group races a clock.
    let cutoff = repo.lookup_snapshot(&survivor).await?.flushed_at;

    let result = expire(
        storage.as_ref(),
        &settings,
        repo.asset_manager().clone(),
        cutoff,
        ExpiredRefAction::Ignore,
        ExpiredRefAction::Ignore,
    )
    .await?;
    assert_eq!(
        result.released_snapshots, older,
        "every snapshot older than the cutoff is released, except the root",
    );
    assert!(
        result.edited_snapshots.contains(&survivor),
        "the oldest survivor is the snapshot that gets re-parented onto the root",
    );

    let repo = Repository::open(None, Arc::clone(&storage), HashMap::new()).await?;
    assert_eq!(
        branch_messages(&repo, "main").await?,
        vec!["4".to_string(), Snapshot::INITIAL_COMMIT_MESSAGE.to_string()],
    );
    Ok(())
}

// ===========================================================================
// i04_flatbuffers_header — inherited from milestone 04: every metadata object
//   is a FlatBuffers document behind a fixed 39-byte header.
//   TEST_INTERFACE.md §6.4; icechunk-spec/flatbuffers/*.fbs;
//   PROFILE.md P6-5 (the spec-version byte was NOT bumped at this release).
// ===========================================================================

#[tokio::test]
async fn i04_flatbuffers_header__objects_start_with_the_icechunk_magic() -> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&root).await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    write_1d(&repo, &p("/temperature"), 4, Some(vec!["t".into()])).await?;

    let magic = "ICE\u{1f9ca}CHUNK".as_bytes();
    assert_eq!(magic.len(), 12);
    for prefix in ["snapshots", "manifests", "transactions"] {
        let mut seen = 0usize;
        for entry in std::fs::read_dir(root.join(prefix))? {
            let bytes = std::fs::read(entry?.path())?;
            assert!(bytes.len() > 39, "an object carries a 39-byte header");
            assert_eq!(&bytes[0..12], magic, "{prefix}/ objects carry the magic");
            seen += 1;
        }
        assert!(seen > 0, "{prefix}/ must hold at least one object");
    }
    Ok(())
}

#[tokio::test]
async fn i04_flatbuffers_header__records_spec_version_one_and_the_file_type()
-> TestResult {
    let dir = tempfile::TempDir::new()?;
    let root = dir.path().join("repo");
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&root).await?;
    let repo = Repository::create(None, Arc::clone(&storage), HashMap::new()).await?;
    write_1d(&repo, &p("/temperature"), 4, Some(vec!["t".into()])).await?;

    // snapshot = 1, manifest = 2, transaction log = 4.
    for (prefix, file_type) in
        [("snapshots", 1u8), ("manifests", 2u8), ("transactions", 4u8)]
    {
        for entry in std::fs::read_dir(root.join(prefix))? {
            let bytes = std::fs::read(entry?.path())?;
            let implementation = std::str::from_utf8(&bytes[12..36])?;
            assert!(
                implementation.starts_with("ic-"),
                "the header names the writing implementation, got {implementation:?}",
            );
            assert_eq!(implementation.trim_end(), implementation.trim());
            assert_eq!(bytes[36], 1u8, "the on-disk spec version is still 1");
            assert_eq!(bytes[37], file_type, "{prefix}/ objects declare their type");
            assert_eq!(bytes[38], 1u8, "the payload is zstd compressed");
        }
    }
    Ok(())
}

#[tokio::test]
async fn i04_flatbuffers_header__metadata_decodes_through_the_public_api()
-> TestResult {
    let storage = mem().await?;
    let repo = Repository::create(
        Some(config_without_preload()),
        Arc::clone(&storage),
        HashMap::new(),
    )
    .await?;
    let array = p("/temperature");
    let snap = write_1d(&repo, &array, 10, Some(vec!["t".into()])).await?;

    let info = repo.lookup_snapshot(&snap).await?;
    assert_eq!(info.id, snap);
    assert_eq!(info.message, "write the array");
    assert!(info.parent_id.is_some());
    assert!(info.flushed_at <= Utc::now());
    // One array, no splitting policy: one manifest, and the snapshot that
    // names it decodes through the public API (TEST_INTERFACE.md §3.7, §6.4).
    assert_eq!(info.manifests.len(), 1);

    let session = repo.readonly_session(&VersionInfo::SnapshotId(snap)).await?;
    let nodes = session.list_nodes().await?.collect::<Result<Vec<_>, _>>()?;
    let paths: Vec<String> = nodes.iter().map(|n| n.path.to_string()).collect();
    assert!(paths.contains(&"/".to_string()));
    assert!(paths.contains(&"/temperature".to_string()));
    Ok(())
}

// ===========================================================================
// i05_cli_repository — inherited from milestone 05: the `icechunk` binary,
//   built with `--features cli` and handed to the driver through
//   ICECHUNK_TEST_CLI_BIN.
//   TEST_INTERFACE.md §6.5.
// ===========================================================================

/// The candidate's `icechunk` binary, as the runner built it.
fn cli_bin() -> PathBuf {
    PathBuf::from(
        std::env::var("ICECHUNK_TEST_CLI_BIN")
            .expect("ICECHUNK_TEST_CLI_BIN is set by the verifier"),
    )
}

/// Runs the CLI with its own private configuration directory, so no test can
/// see another's repositories and nothing touches the real user profile.
fn run_cli(config_home: &std::path::Path, args: &[&str]) -> std::process::Output {
    std::process::Command::new(cli_bin())
        .args(args)
        .env("XDG_CONFIG_HOME", config_home)
        .env("HOME", config_home)
        .output()
        .expect("the icechunk binary must be executable")
}

/// Writes a CLI configuration naming one local-filesystem repository.
fn write_cli_config(
    config_home: &std::path::Path,
    alias: &str,
    repo_path: &std::path::Path,
) -> std::io::Result<()> {
    let dir = config_home.join("icechunk");
    std::fs::create_dir_all(&dir)?;
    std::fs::write(
        dir.join("cli-config.yaml"),
        format!(
            "repos:\n  {alias}: !LocalFileSystem\n    path: {}\n    config: {{}}\n",
            repo_path.display()
        ),
    )
}

#[tokio::test]
async fn i05_cli_repository__prints_help_and_exits_zero() -> TestResult {
    let home = tempfile::TempDir::new()?;
    for args in [["--help"], ["help"]] {
        let out = run_cli(home.path(), &args);
        assert!(out.status.success(), "`icechunk {}` must succeed", args[0]);
        let text = String::from_utf8(out.stdout)?;
        for command in ["repo", "snapshot", "config"] {
            assert!(text.contains(command), "the help lists the {command} command");
        }
    }

    // No subcommand at all is a usage error, not a success.
    let out = run_cli(home.path(), &[]);
    assert!(!out.status.success());

    // There is no `--version` (TEST_INTERFACE.md §6.5); an unknown flag is a
    // usage error and exits 2 (PROFILE.md P5-4).
    let out = run_cli(home.path(), &["--version"]);
    assert_eq!(
        out.status.code(),
        Some(2),
        "`icechunk --version` is a usage error, not a version banner",
    );
    Ok(())
}

#[tokio::test]
async fn i05_cli_repository__creates_a_repository_on_the_local_filesystem()
-> TestResult {
    let home = tempfile::TempDir::new()?;
    let repos = tempfile::TempDir::new()?;
    let repo_path = repos.path().join("local1");
    std::fs::create_dir_all(&repo_path)?;
    write_cli_config(home.path(), "local1", &repo_path)?;

    let out = run_cli(home.path(), &["repo", "create", "local1"]);
    assert!(
        out.status.success(),
        "repo create failed: {}",
        String::from_utf8_lossy(&out.stderr),
    );
    assert_eq!(
        String::from_utf8(out.stdout)?.lines().count(),
        1,
        "`repo create` prints exactly one line to stdout",
    );
    assert!(repo_path.join("refs").join("branch.main").join("ref.json").is_file());
    assert!(repo_path.join("snapshots").is_dir());

    // The repository the CLI wrote is a normal Icechunk repository.
    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&repo_path).await?;
    assert!(Repository::exists(storage.as_ref()).await?);
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    // The repository the CLI created has exactly one commit and it is the
    // well-known first one. Its *id* is §4.1's subject and g13's.
    let root = repo.lookup_branch("main").await?;
    let root = repo.lookup_snapshot(&root).await?;
    assert!(root.is_initial());
    assert_eq!(root.message, Snapshot::INITIAL_COMMIT_MESSAGE);
    Ok(())
}

#[tokio::test]
async fn i05_cli_repository__lists_snapshots() -> TestResult {
    let home = tempfile::TempDir::new()?;
    let repos = tempfile::TempDir::new()?;
    let repo_path = repos.path().join("local1");
    std::fs::create_dir_all(&repo_path)?;
    write_cli_config(home.path(), "local1", &repo_path)?;

    assert!(run_cli(home.path(), &["repo", "create", "local1"]).status.success());

    let storage: Arc<dyn Storage + Send + Sync> =
        new_local_filesystem_storage(&repo_path).await?;
    let repo = Repository::open(None, storage, HashMap::new()).await?;
    let mut session = repo.writable_session("main").await?;
    session.add_group(Path::root(), Bytes::new()).await?;
    session.commit("through the library", None).await?;

    let out = run_cli(home.path(), &["snapshot", "list", "local1"]);
    assert!(
        out.status.success(),
        "snapshot list failed: {}",
        String::from_utf8_lossy(&out.stderr),
    );
    let text = String::from_utf8(out.stdout)?;
    assert_eq!(text.lines().count(), 2, "one line per snapshot, newest first");
    assert!(text.contains("through the library"));
    assert!(text.contains(Snapshot::INITIAL_COMMIT_MESSAGE));

    // `-n` bounds the listing, and the listing is newest first, so the single
    // line it keeps is the most recent commit rather than the repository root.
    let out = run_cli(home.path(), &["snapshot", "list", "local1", "-n", "1"]);
    assert!(out.status.success());
    let text = String::from_utf8(out.stdout)?;
    assert_eq!(text.lines().count(), 1);
    assert!(
        text.contains("through the library"),
        "the first line is the newest snapshot",
    );
    assert!(
        !text.contains(Snapshot::INITIAL_COMMIT_MESSAGE),
        "an oldest-first listing would have kept the repository root instead",
    );

    // `config list` reports the configuration the binary loaded.
    let out = run_cli(home.path(), &["config", "list"]);
    assert!(
        out.status.success(),
        "config list failed: {}",
        String::from_utf8_lossy(&out.stderr),
    );
    assert!(String::from_utf8(out.stdout)?.contains("local1"));

    // Without `-n` the listing stops at ten, which is its documented default.
    for i in 0..11u32 {
        let mut session = repo.writable_session("main").await?;
        session.add_group(p(&format!("/g{i}")), Bytes::new()).await?;
        session.commit(&format!("filler {i}"), None).await?;
    }
    let out = run_cli(home.path(), &["snapshot", "list", "local1"]);
    assert!(out.status.success());
    assert_eq!(
        String::from_utf8(out.stdout)?.lines().count(),
        10,
        "`-n` defaults to 10 even though the ancestry is longer",
    );
    Ok(())
}

#[tokio::test]
async fn i05_cli_repository__reports_a_failure_with_a_nonzero_status() -> TestResult {
    let home = tempfile::TempDir::new()?;
    let repos = tempfile::TempDir::new()?;
    let repo_path = repos.path().join("local1");
    std::fs::create_dir_all(&repo_path)?;
    write_cli_config(home.path(), "local1", &repo_path)?;

    // An alias that is not in the configuration.
    let out = run_cli(home.path(), &["snapshot", "list", "nosuch"]);
    assert!(!out.status.success(), "an unknown alias must not exit zero");

    // A branch that does not exist in a repository that does.
    assert!(run_cli(home.path(), &["repo", "create", "local1"]).status.success());
    let out = run_cli(home.path(), &["snapshot", "list", "local1", "-b", "nobranch"]);
    assert!(!out.status.success());

    // An unknown subcommand is a usage error.
    let out = run_cli(home.path(), &["bogus"]);
    assert!(!out.status.success());
    Ok(())
}
