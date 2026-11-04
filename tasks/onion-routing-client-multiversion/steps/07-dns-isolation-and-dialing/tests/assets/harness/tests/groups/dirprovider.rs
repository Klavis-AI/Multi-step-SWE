//! Group `dirprovider`: the `DirProvider` trait which combines
//! `NetDirProvider` with bootstrap, reconfigure, and bootstrap events.
//!
//! Graded items: `tor_dirmgr::DirProvider`.

use std::sync::Arc;

use async_trait::async_trait;
use futures::stream::BoxStream;
use futures::StreamExt;
use tor_config::Reconfigure;
use tor_dirmgr::config::DirMgrExtensions;
use tor_dirmgr::{DirBootstrapStatus, DirMgrConfig, DirProvider};
use tor_rtcompat::SleepProviderExt;
use tor_netdir::{DirEvent, NetDir, NetDirProvider};

/// Build a `DirMgrConfig` without relying on the test-only `Default` derive.
fn offline_dir_cfg() -> DirMgrConfig {
    DirMgrConfig {
        cache_path: std::path::PathBuf::from("/nonexistent/step07-dirprovider"),
        network_config: tor_dirmgr::NetworkConfig::default(),
        schedule_config: tor_dirmgr::DownloadScheduleConfig::default(),
        override_net_params: tor_netdoc::doc::netstatus::NetParams::new(),
        extensions: DirMgrExtensions::default(),
    }
}

/// A first-party DirProvider stub that implements all required methods.
struct StubDirProvider;

#[async_trait]
impl DirProvider for StubDirProvider {
    fn reconfigure(
        &self,
        _new_config: &DirMgrConfig,
        _how: Reconfigure,
    ) -> std::result::Result<(), tor_config::ReconfigureError> {
        Ok(())
    }

    async fn bootstrap(&self) -> tor_dirmgr::Result<()> {
        Ok(())
    }

    fn bootstrap_events(&self) -> BoxStream<'static, DirBootstrapStatus> {
        Box::pin(futures::stream::empty())
    }
}

impl NetDirProvider for StubDirProvider {
    fn latest_netdir(&self) -> Option<Arc<NetDir>> {
        None
    }

    fn events(&self) -> BoxStream<'static, DirEvent> {
        Box::pin(futures::stream::empty())
    }
}

#[test]
fn dirprovider__trait_implemented_for_stub() {
    let prov = StubDirProvider;
    assert!(prov.latest_netdir().is_none());
}

#[test]
fn dirprovider__reconfigure_callable() {
    let prov = StubDirProvider;
    let cfg = offline_dir_cfg();
    let result = prov.reconfigure(&cfg, Reconfigure::AllOrNothing);
    assert!(result.is_ok());
}

#[test]
fn dirprovider__bootstrap_completes() {
    tor_rtcompat::test_with_one_runtime!(|_rt| async move {
        let prov = StubDirProvider;
        let result = prov.bootstrap().await;
        assert!(result.is_ok());
    });
}

#[test]
fn dirprovider__bootstrap_events_stream_empty() {
    tor_rtcompat::test_with_one_runtime!(|rt| async move {
        let prov = StubDirProvider;
        let mut stream = prov.bootstrap_events();

        // Same distinction as `netdirprov`: a bootstrap-status consumer must
        // treat stream termination as "this provider will report nothing
        // further" and idleness as "not yet".  Conflating them either hangs a
        // caller or makes it declare bootstrap finished when it is not.
        let next = rt
            .timeout(std::time::Duration::from_millis(50), stream.next())
            .await;
        assert!(
            next.is_ok(),
            "a terminated bootstrap-event stream must resolve, not hang"
        );
        assert!(
            next.unwrap().is_none(),
            "a provider with no status to report must end its stream"
        );

        let mut idle = futures::stream::pending::<DirBootstrapStatus>();
        let nothing = rt
            .timeout(std::time::Duration::from_millis(50), idle.next())
            .await;
        assert!(nothing.is_err(), "an idle stream must not resolve");
    });
}

#[test]
fn dirprovider__object_safety_via_arc() {
    let prov: Arc<dyn DirProvider + Send + Sync> = Arc::new(StubDirProvider);
    assert!(prov.latest_netdir().is_none());
}
