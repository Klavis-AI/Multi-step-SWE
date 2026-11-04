use webpki;

use crate::key;
#[cfg(feature = "logging")]
use crate::log::{debug, trace};
pub use crate::msgs::handshake::{DistinguishedName, DistinguishedNames};
use crate::pemfile;
use crate::x509;
use std::io;

/// This is like a `webpki::TrustAnchor`, except it owns
/// rather than borrows its memory.  That prevents lifetimes
/// leaking up the object tree.
#[derive(Debug, Clone)]
pub struct OwnedTrustAnchor {
    subject: Vec<u8>,
    spki: Vec<u8>,
    name_constraints: Option<Vec<u8>>,
}

impl OwnedTrustAnchor {
    /// Copy a `webpki::TrustAnchor` into owned memory
    pub fn from_trust_anchor(t: &webpki::TrustAnchor) -> OwnedTrustAnchor {
        unimplemented!()
    }

    /// Get a `webpki::TrustAnchor` by borrowing the owned elements.
    pub fn to_trust_anchor(&self) -> webpki::TrustAnchor {
        unimplemented!()
    }
}

impl From<webpki::TrustAnchor<'_>> for OwnedTrustAnchor {
    fn from(t: webpki::TrustAnchor) -> OwnedTrustAnchor {
        unimplemented!()
    }
}

impl<'a> Into<webpki::TrustAnchor<'a>> for &'a OwnedTrustAnchor {
    fn into(self) -> webpki::TrustAnchor<'a> {
        unimplemented!()
    }
}

/// A container for root certificates able to provide a root-of-trust
/// for connection authentication.
#[derive(Debug, Clone)]
pub struct RootCertStore {
    /// The list of roots.
    pub roots: Vec<OwnedTrustAnchor>,
}

impl RootCertStore {
    /// Make a new, empty `RootCertStore`.
    pub fn empty() -> RootCertStore {
        unimplemented!()
    }

    /// Return true if there are no certificates.
    pub fn is_empty(&self) -> bool {
        unimplemented!()
    }

    /// Say how many certificates are in the container.
    pub fn len(&self) -> usize {
        unimplemented!()
    }

    /// Return the Subject Names for certificates in the container.
    pub fn get_subjects(&self) -> DistinguishedNames {
        unimplemented!()
    }

    /// Add a single DER-encoded certificate to the store.
    pub fn add(&mut self, der: &key::Certificate) -> Result<(), webpki::Error> {
        unimplemented!()
    }

    /// Adds all the given TrustAnchors `anchors`.  This does not
    /// fail.
    pub fn add_server_trust_anchors(
        &mut self,
        &webpki::TLSServerTrustAnchors(anchors): &webpki::TLSServerTrustAnchors,
    ) {
        unimplemented!()
    }

    /// Parse a PEM file and add all certificates found inside.
    /// Errors are non-specific; they may be io errors in `rd` and
    /// PEM format errors, but not certificate validity errors.
    ///
    /// This is because large collections of root certificates often
    /// include ancient or syntactically invalid certificates.  CAs
    /// are competent like that.
    ///
    /// Returns the number of certificates added, and the number
    /// which were extracted from the PEM but ultimately unsuitable.
    pub fn add_pem_file(&mut self, rd: &mut dyn io::BufRead) -> Result<(usize, usize), ()> {
        unimplemented!()
    }
}
