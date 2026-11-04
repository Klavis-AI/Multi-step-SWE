# TEST_INTERFACE — step 12, bridges and pluggable transports

This is the complete list of public items the step-12 conformance suite checks.
The suite touches nothing that is not named below. Where a signature is given it
is the exact one the driver compiles against; where a trait method is checked
through a default implementation that is said explicitly, because the
implementation may supply only the required method and still pass.

Conventions used throughout:

* "crate::path" means the item is reachable at that path from a dependent crate,
  not necessarily where it is defined. The driver imports only public re-exports.
* Feature gates the driver enables: `tor-linkspec/pt-client`,
  `tor-guardmgr/bridge-client`, `tor-guardmgr/pt-client`, `tor-chanmgr/pt-client`,
  `tor-netdoc/routerdesc`, `tor-socksproto/client-handshake`,
  `tor-socksproto/proxy-handshake`, `tor-rtcompat/tokio`,
  `tor-rtcompat/native-tls`. Anything behind a different gate (notably
  `tor-chanmgr/testing`) is **not** checked.
* `Display`, `Debug`, `FromStr`, `PartialEq`, `Eq`, `Hash`, `Clone`, `Default`,
  `Serialize` and `Deserialize` are checked on many of the types below. Where a
  case asserts a rendering, this document says so and says what is asserted —
  either the expected text verbatim, or the fragment a case looks for (an error
  naming the field it rejected, an error naming the key or value it refused).
  Nothing is asserted about wording this document leaves free.

---

## 1. Transport naming — `tor_linkspec`

Checked by groups `transportid`, `pttarget`, `chanmethod`, `brlog`.

| Item | Signature / form |
| --- | --- |
| `tor_linkspec::TransportId` | opaque `struct`; `Clone + Debug + Eq + Hash + Default + Display + FromStr<Err = TransportIdError>` |
| `TransportId::new_builtin` | `fn new_builtin() -> Self` |
| `TransportId::new_pluggable` | `fn new_pluggable(pt: PtTransportName) -> Self` |
| `TransportId::is_builtin` | `fn is_builtin(&self) -> bool` |
| `TransportId::as_pluggable` | `fn as_pluggable(&self) -> Option<&PtTransportName>` |
| `TransportId::into_pluggable` | `fn into_pluggable(self) -> Option<PtTransportName>` |
| `tor_linkspec::PtTransportName` | opaque `struct`; `Display + FromStr<Err = TransportIdError> + Eq + Hash + Clone` |
| `tor_linkspec::TransportIdError` | `enum`; the variant the suite matches is `BadId(String)` — the payload is the rejected input verbatim |

Observable behaviour:

* `TransportId::default()` is the built-in transport, equals `new_builtin()`,
  reports `is_builtin() == true` and `as_pluggable() == None`.
* Four spellings parse to the built-in transport, and they are matched exactly:
  the empty string, `-`, `<none>`, and lowercase `bridge`. The comparison is a
  membership test against that fixed list, so it is case-SENSITIVE: `BRIDGE` and
  `Bridge` are not the built-in transport. They fall through to the pluggable
  name parser, which rejects any name equal to `bridge` ignoring case, so they
  are errors rather than either a built-in or a pluggable transport.
* The built-in transport always displays as the single character `-`, whichever
  of the four spellings produced it.
* A pluggable name round-trips: `TransportId::from_str(n).unwrap().to_string() == n`,
  and equals `TransportId::new_pluggable(PtTransportName::from_str(n).unwrap())`.
* A transport name is accepted exactly when it is a C identifier: a leading
  letter or underscore followed by letters, digits and underscores. Names with a
  leading digit, an interior hyphen, a dot, or any other punctuation are rejected
  with `TransportIdError::BadId`.
* `PtTransportName::from_str("bridge")` (any case) is rejected with
  `TransportIdError::BadId`, the same variant syntactically malformed names
  produce — `"bridge"` is reserved for the built-in role and so fails the
  well-formedness check by name rather than by shape — even though
  `TransportId::from_str("bridge")` succeeds as the built-in.

## 2. Transport targets and settings — `tor_linkspec`

Checked by groups `pttarget`, `briaddr`, `chanmethod`, `owntarget`, `brlog`.

| Item | Signature / form |
| --- | --- |
| `tor_linkspec::PtTarget` | `struct`; `Clone + Debug + Eq + Hash` |
| `PtTarget::new` | `fn new(transport: PtTransportName, addr: PtTargetAddr) -> Self` |
| `PtTarget::push_setting` | `fn push_setting(&mut self, k: impl Into<String>, v: impl Into<String>) -> Result<(), PtTargetInvalidSetting>` |
| `PtTarget::transport` | `fn transport(&self) -> &PtTransportName` |
| `PtTarget::addr` | `fn addr(&self) -> &PtTargetAddr` |
| `PtTarget::settings` | `fn settings(&self) -> impl Iterator<Item = (&str, &str)>` |
| `PtTarget::socket_addrs` | `fn socket_addrs(&self) -> Option<&[std::net::SocketAddr]>` |
| `PtTarget::into_parts` | `fn into_parts(self) -> (PtTransportName, PtTargetAddr, PtTargetSettings)` |
| `tor_linkspec::PtTargetSettings` | `struct`; `TryFrom<Vec<(String, String)>, Error = PtTargetInvalidSetting>`, `into_inner(self) -> Vec<(String, String)>`, and the infallible `From<PtTargetSettings> for Vec<(String, String)>` |
| `tor_linkspec::PtTargetInvalidSetting` | `enum { Key(String), Value(String) }` — **the payload is the offending text verbatim**: `Key` carries the rejected key, `Value` the rejected value, exactly as the setting was handed in |
| `tor_linkspec::PtTargetAddr` | `enum { IpPort(SocketAddr), HostPort(String, u16), None }`; `Display + Clone + Eq + FromStr<Err = BridgeAddrError>` (`IpPort` parses `host:port`, the bare `-` is `None`) |
| — its `HasAddrs` impl | `impl HasAddrs for PtTargetAddr`, supplying the trait's one and only method, `fn addrs(&self) -> &[SocketAddr]` — a **borrowed slice**, exactly as declared in §5, not a `Vec`. `IpPort` yields the one address it holds; `HostPort` and `None` yield an empty slice. The impl lives in `tor_linkspec`, so a name-grep for the enum will not find it. (`HasAddrs` has no other method; `contained_by` is an inherent method of `ChannelMethod`, see §3, and `PtTargetAddr` does not have one.) |
| — its conversions | `From<PtTargetAddr> for Option<BridgeAddr>` and `From<Option<BridgeAddr>> for PtTargetAddr` |
| `tor_linkspec::BridgeAddr` | opaque `struct`; `Display + FromStr<Err = BridgeAddrError> + Clone + Eq + Hash` |
| `BridgeAddr::new_addr_from_sockaddr` | `fn new_addr_from_sockaddr(sa: SocketAddr) -> Self` |
| `BridgeAddr::as_socketaddr` | `fn as_socketaddr(&self) -> Option<&SocketAddr>` |
| `BridgeAddr::new_named_host_port` | `fn new_named_host_port(hostname: impl Into<String>, port: u16) -> Self` |
| `BridgeAddr::as_host_port` | `fn as_host_port(&self) -> Option<(&str, u16)>` |
| `tor_linkspec::BridgeAddrError` | `enum`; the variant the suite matches is `BadAddress(String)` — **the payload is the rejected input verbatim**, the same rule `TransportIdError::BadId` follows in §1 |

Observable behaviour:

* A freshly constructed `PtTarget` has no settings.
* Settings preserve insertion order and their exact key/value text.
* A setting is refused when the key contains `=` or ASCII whitespace
  (`PtTargetInvalidSetting::Key`), or when the value contains ASCII whitespace
  (`PtTargetInvalidSetting::Value`). The failed push leaves the target unchanged.
* `into_parts` returns the transport, address and settings that were put in, in
  that order; the settings survive as `PtTargetSettings`.
* `socket_addrs()` is `Some(&[addr])` only when the target address is
  `PtTargetAddr::IpPort`; it is `None` for `HostPort` and for `None`.
* `PtTargetSettings::try_from` validates every pair with the same rules as
  `push_setting`, and reports the first offending pair.
* `BridgeAddr` keeps a numeric address as a `SocketAddr` (so `as_host_port()` is
  `None`) and a name as a name (so `as_socketaddr()` is `None`).
* `BridgeAddr` parsing and `Display` are inverse for both numeric and named
  forms; an IPv6 address displays bracketed, as `[::1]:443`.
* An address with no `:port` is rejected with `BridgeAddrError::BadAddress`,
  the same variant any other unparseable address produces — there is no
  variant specific to a missing port; `BridgeAddr` tries a numeric address,
  then a `name:port` split, and fails the same way either way. Its payload is
  the address as it was handed in, so parsing `"example.com"` yields
  `BadAddress("example.com")` and not a reconstructed or trimmed form.
* `PtTargetAddr::None` displays as `-`; the `IpPort` and `HostPort` forms display
  exactly as the corresponding `BridgeAddr`.

## 3. Channel methods — `tor_linkspec`

Checked by groups `chanmethod`, `chantarget`, `owntarget`, `bridgebuild`, `bridgeline`, `brlog`.

| Item | Signature / form |
| --- | --- |
| `tor_linkspec::ChannelMethod` | `enum { Direct(Vec<SocketAddr>), Pluggable(PtTarget) }`; `Clone + Debug + Eq + Hash` |
| `ChannelMethod::socket_addrs` | `fn socket_addrs(&self) -> Option<&[SocketAddr]>` |
| `ChannelMethod::target_addr` | `fn target_addr(&self) -> Option<PtTargetAddr>` |
| `ChannelMethod::is_direct` | `fn is_direct(&self) -> bool` |
| `ChannelMethod::transport_id` | `fn transport_id(&self) -> TransportId` |
| `ChannelMethod::retain_addrs` | `fn retain_addrs<P>(&mut self, pred: P) -> Result<(), RetainAddrsError> where P: Fn(&SocketAddr) -> bool` |
| `ChannelMethod::contained_by` | `fn contained_by(&self, other: &ChannelMethod) -> bool` |
| the error type returned by `retain_addrs` | **not** re-exported from `tor_linkspec`'s root at this checkpoint — the crate re-exports `BridgeAddr`, `BridgeAddrError`, `ChannelMethod`, `TransportId`, `TransportIdError`, `PtTarget`, `PtTargetAddr`, `PtTargetInvalidSetting`, `PtTargetSettings` and `PtTransportName` from the `transport` module, and nothing else. So the suite never names the type: it observes only `Result::is_ok()` / `is_err()` on what `retain_addrs` returns |

Observable behaviour:

* `Direct` keeps every address in the order given; `socket_addrs()` returns them
  all and `target_addr()` returns `PtTargetAddr::IpPort` of the **first**.
* An empty `Direct` has `target_addr() == None` and `socket_addrs() == Some(&[])`.
* `Direct(_).transport_id()` is the built-in transport; a `Pluggable` method's
  `transport_id()` is the pluggable one it names, and `is_direct()` is false.
* A `Pluggable` method whose target is `PtTargetAddr::None` reports
  `socket_addrs() == None` and `target_addr() == Some(PtTargetAddr::None)`.
* `retain_addrs` on a `Direct` method removes every address the predicate
  rejects, and returns `Err` when that would leave the method with no address at
  all. A `Pluggable` method whose target is a **numeric** `IpPort` is filtered
  exactly like a direct address: the rejected target is replaced by
  `PtTargetAddr::None` and the call returns `Err`, because nothing is left. A
  pluggable method whose target is a **named** `HostPort`, or already absent
  (`None`), is left untouched and the call succeeds — a hostname is never
  removed by an address predicate.
* `contained_by` on two `Direct` methods is subset containment on the address
  list. Across the two variants it is never satisfied: a `Pluggable` method is
  not contained by any `Direct` one. A `Pluggable` method is contained by an
  equal `Pluggable` method.

## 4. Bridge lines — `tor_guardmgr::bridge`

Checked by groups `bridgeline`, `bridgeerr`, `bridgebuild`, `chantarget`, `owntarget`, `bridgedesc`.

| Item | Signature / form |
| --- | --- |
| `tor_guardmgr::bridge::BridgeConfig` | opaque `struct`; `Clone + Debug + Eq + Hash + Display + FromStr<Err = BridgeParseError>`. It is **not** `Serialize`/`Deserialize`, and must not be made so: it wraps a `ChannelMethod`, which carries no serde impl at this checkpoint, so `#[derive(Serialize, Deserialize)]` on `BridgeConfig` does not compile. Serde belongs on `BridgeConfigBuilder` instead — see the next row |
| `tor_guardmgr::bridge::BridgeConfigBuilder` | `struct`; `Default + Clone + Debug`, and `FromStr`. This is the type that carries serde: upstream derives `Serialize`/`Deserialize` on the **builder**, which is what lets a bridge be written in a config file either as a bridge-line string or as a table of fields. **Neither of these two types has its serde checked here** — nothing in the suite serialises or deserialises a `BridgeConfig` or a `BridgeConfigBuilder`. (Serde *is* checked elsewhere in this step, on `PaddingLevel` and `ChannelConfigBuilder`; see §11.) These two rows are stated only so that a reader does not attach serde to the wrong type |
| `BridgeConfigBuilder::transport` | `fn transport(&mut self, transport: impl Into<String>) -> &mut Self` |
| `BridgeConfigBuilder::direct` | `fn direct(&mut self) -> &mut Self` |
| `BridgeConfigBuilder::addrs` | `fn addrs(&mut self) -> &mut Vec<BridgeAddr>` — an *accessor* that hands back the list being built, not a derive_builder setter. `BridgeConfigBuilder` is written out by hand (it holds a `ChannelMethod`, which `#[derive(Builder)]` cannot express), and its list fields get their accessors from `tor_config::define_list_builder_accessors!`. The suite writes `b.transport("").addrs().push(addr)`, which does not compile against a `fn addrs(&mut self, v: Vec<BridgeAddr>) -> &mut Self` setter |
| `BridgeConfigBuilder::ids` | `fn ids(&mut self) -> &mut Vec<RelayId>`, from the same macro and used the same way (`b.ids().push(id)`). The macro also emits `set_ids`, `opt_ids` and `opt_ids_mut` for each list field, but no case calls them |
| `BridgeConfigBuilder::push_setting` | `fn push_setting(&mut self, k: impl Into<String>, v: impl Into<String>) -> &mut Self` |
| `BridgeConfigBuilder::build` | `fn build(&self) -> Result<BridgeConfig, ConfigBuildError>` |
| `tor_guardmgr::bridge::BridgeParseError` | `enum`; variants the suite matches: `Empty`, `NoRsaIdentity`, `InvalidIdentityOrParameter { word }`, `MultipleIdentitiesOfSameType { .. }`, `InvalidIPtHostAddr { word, .. }`, `InvalidIpAddrOrPt { .. }`, `InvalidPtOrAddr { .. }`, `InvalidPtKeyValue { word }`, `DirectParametersNotAllowed`. The destructured field names matter: `InvalidPtKeyValue` has exactly one field, named `word` |
| `tor_config::ConfigBuildError` | `enum`; the suite destructures two variants: `Inconsistent { fields: Vec<String>, .. }` and `Invalid { field: String, .. }` — see the field-name rules below |

`BridgeConfig` implements `tor_linkspec::ChanTarget`, and therefore
`HasAddrs`, `HasRelayIds` and `HasChanMethod`.

Bridge-line grammar (`FromStr`):

* The leading `bridge` keyword is optional and case-insensitive, and carries no
  information: with and without it the parsed values compare equal.
* Word order is: optional keyword, optional transport name, address, one or more
  identity keys, then zero or more `key=value` transport parameters.
* A direct line yields `ChannelMethod::Direct` with exactly one address; an IPv6
  address is written and printed bracketed.
* A line naming a transport yields `ChannelMethod::Pluggable`. Its target may be
  a numeric address, a hostname and port, or `-` for no target at all.
* An Ed25519 identity may be given in addition to the RSA one; both then appear
  through `HasRelayIds`.
* Leading and trailing whitespace on the whole line is ignored.
* `Display` round-trips: re-parsing the printed form gives an equal value.

Rejection cases, each to its named variant:

* an empty or all-whitespace line, and a line that is only the keyword →
  `Empty`;
* an address with no identity key → `NoRsaIdentity`;
* a truncated or non-hex fingerprint → `InvalidIdentityOrParameter`;
* two identities of the same type → `MultipleIdentitiesOfSameType`;
* after a transport name, a target address that is neither a socket address
  nor a `host:port` → `InvalidIPtHostAddr`;
* a syntactically address-shaped word that is not a decodable IP address —
  `999.999.999.999:9001`, on a line with or without a transport name →
  `InvalidIpAddrOrPt`;
* a transport name outside C-identifier syntax → `InvalidPtOrAddr`;
* a parameter word with no `=` → `InvalidPtKeyValue`;
* parameters on a line that names no transport → `DirectParametersNotAllowed`;
* the word after a transport name is always read as its target, never as an
  identity key, so a transport line whose next word is a fingerprint fails on the
  *address*, not on the identity.

Builder behaviour:

The `ConfigBuildError` field names are asserted exactly, so they are part of the
contract:

| Rejected build | Variant | `field` / `fields` must contain |
| --- | --- | --- |
| no RSA identity | `Invalid` | `field == "ids"` |
| transport name outside C-identifier syntax | `Invalid` | `field == "transport"` |
| a setting whose value contains whitespace | `Invalid` | `field == "settings.0"` — the offending setting's **index**, zero-based |
| `direct()` plus transport settings | `Inconsistent` | `fields` contains `"settings"` and `"transport"` |
| `direct()` with a hostname address | `Inconsistent` | `fields` contains `"addrs"` |
| `direct()` with no address | `Inconsistent` | — |
| a pluggable bridge with two addresses | `Inconsistent` | `fields` contains `"addrs"` |

* `transport("")` and `direct()` both select a direct bridge.
* A direct bridge accepts several addresses and requires at least one; it refuses
  transport settings, and refuses a `BridgeAddr` that is a hostname.
* A pluggable bridge accepts at most one address, and carries its settings into
  the built `PtTarget`.
* A bridge with no RSA identity is refused.
* A transport name outside C-identifier syntax is refused at `build()` time.

## 5. Channel and circuit targets — `tor_linkspec`

Checked by groups `chantarget`, `owntarget`, `brlog`.

| Item | Signature / form |
| --- | --- |
| `tor_linkspec::HasAddrs` | `trait { fn addrs(&self) -> &[SocketAddr]; }` |
| `tor_linkspec::HasChanMethod` | `trait { fn chan_method(&self) -> ChannelMethod; }` |
| `tor_linkspec::ChanTarget` | `trait: HasAddrs + HasRelayIds + HasChanMethod`; it adds no required method of its own, only the provided `fn display_chan_target(&self) -> DisplayChanTarget<'_, Self> where Self: Sized`, which is what backs the `Display` of the owned types below |
| `tor_linkspec::CircTarget` | `trait: ChanTarget { fn ntor_onion_key(&self) -> &curve25519::PublicKey; fn protovers(&self) -> &Protocols; }` |
| `tor_linkspec::OwnedChanTarget` | `struct`; `Clone + Debug + Display` |
| `OwnedChanTarget::builder` | `fn builder() -> OwnedChanTargetBuilder` |
| `OwnedChanTarget::from_chan_target` | `fn from_chan_target<C: ChanTarget + ?Sized>(target: &C) -> Self` |
| `OwnedChanTarget::chan_method_mut` | `fn chan_method_mut(&mut self) -> &mut ChannelMethod` |
| `OwnedChanTargetBuilder` | `addrs(Vec<SocketAddr>)`, `method(ChannelMethod)`, `ed_identity(Ed25519Identity)`, `rsa_identity(RsaIdentity)`, `build() -> Result<OwnedChanTarget, ConfigBuildError>` |
| `tor_linkspec::IntoOwnedChanTarget` | `trait { fn to_owned(self) -> OwnedChanTarget; }` — note this shadows `std::borrow::ToOwned::to_owned` and must be called fully qualified |
| `tor_linkspec::OwnedCircTarget` | `struct`; `builder() -> OwnedCircTargetBuilder` |
| `OwnedCircTargetBuilder` | `chan_target(&mut self) -> &mut OwnedChanTargetBuilder`, `ntor_onion_key(curve25519::PublicKey)`, `protocols(Protocols)`, `build()` |

Observable behaviour:

* When a builder is given addresses but no explicit method, the resulting
  `chan_method()` is `ChannelMethod::Direct` of exactly those addresses.
* When a builder is given a `Pluggable` method, `addrs()` stays empty — the
  client has no address of its own for the relay.
* `ed_identity` and `rsa_identity` populate the nested relay-id builder; each is
  optional independently, and a missing one reads back as `identity(..) == None`.
* `from_chan_target` on a `BridgeConfig` preserves the transport, its settings
  and every identity; `IntoOwnedChanTarget::to_owned` is the same conversion and
  is the identity function on an `OwnedChanTarget`.
* `chan_method_mut` replaces the method in place and does not rewrite `addrs()`.
* `Display` of a channel target is `[<where> <id> <id> ...]`, where `<where>` is
  the single address, or the first address with a trailing `+` when there is more
  than one, or `?` when there is no way to reach the relay, or
  `<target-addr> via <transport>` for a pluggable method. Identities print in
  `HasRelayIds::identities()` order, Ed25519 first, each in its `RelayId` form.
* `OwnedCircTarget` forwards `addrs()`, `chan_method()` and `identity()` to its
  nested channel target, and adds `ntor_onion_key()` and `protovers()`.

## 6. Relay identities — `tor_linkspec` (inherited)

Checked by groups `relayid`, `ridtraits`, and used throughout.

| Item | Signature / form |
| --- | --- |
| `tor_linkspec::RelayId` | `enum { Ed25519(Ed25519Identity), Rsa(RsaIdentity) }`; `Display + FromStr<Err = RelayIdError> + Eq + Hash + Clone` |
| `RelayId::id_type` | `fn id_type(&self) -> RelayIdType` |
| `RelayId::as_bytes` | `fn as_bytes(&self) -> &[u8]` |
| `RelayId::from_type_and_bytes` | `fn from_type_and_bytes(t: RelayIdType, bytes: &[u8]) -> Result<Self, RelayIdError>` |
| `RelayId::as_ref` | `fn as_ref(&self) -> RelayIdRef<'_>` |
| `RelayId: From<RsaIdentity>`, `From<Ed25519Identity>` | conversions |
| `tor_linkspec::RelayIdRef<'a>` | borrowed counterpart; `Display + Eq`, `to_owned() -> RelayId`, `From<&Ed25519Identity>`, `From<&RsaIdentity>`, and `PartialEq` against the bare key types |
| `tor_linkspec::RelayIdType` | `enum { Ed25519, Rsa }`; `FromStr<Err = RelayIdError>` |
| `RelayIdType::id_len` | `fn id_len(&self) -> usize` |
| `RelayIdType::all_types` | `fn all_types() -> impl Iterator<Item = RelayIdType>` |
| `RelayIdType::COUNT` | `const COUNT: usize` |
| `tor_linkspec::RelayIdError` | `enum`; variants matched: `BadLength`, `BadHex`, `UnrecognizedIdType` |
| `tor_linkspec::RelayIdSet` | `new()`, `insert(impl Into<RelayId>) -> bool`, `contains(impl Into<RelayIdRef<'_>>) -> bool`, `remove(impl Into<RelayIdRef<'_>>) -> bool`, `len()`, `is_empty()`, `FromIterator<RelayId>`, `Eq` |
| `tor_linkspec::HasRelayIds` | `trait { fn identity(&self, key_type: RelayIdType) -> Option<RelayIdRef<'_>>; }` plus default methods `identities`, `ed_identity() -> Option<&Ed25519Identity>`, `rsa_identity() -> Option<&RsaIdentity>`, `has_identity`, `has_any_identity() -> bool`, `same_relay_ids`, `has_all_relay_ids_from`. Note that `ed_identity`/`rsa_identity` are **also** provided here returning `Option<&_>`; the same-named methods on `HasRelayIdsLegacy` below return the bare reference and are a different trait |
| `tor_linkspec::HasRelayIdsLegacy` | `trait { fn ed_identity(&self) -> &Ed25519Identity; fn rsa_identity(&self) -> &RsaIdentity; }`, with a blanket `impl HasRelayIds for T: HasRelayIdsLegacy` |
| `tor_llcrypto::pk::ed25519::Ed25519Identity` | `fn from_bytes(id: &[u8]) -> Option<Self>` — an `Option`, not a `Result`, and `None` for any length other than 32. Every case that needs an Ed25519 identity builds it from the driver's fixed 32 bytes with this, so a rename or a changed return type fails the whole driver to compile, not one group |
| `tor_llcrypto::pk::rsa::RsaIdentity` | `fn from_hex(s: &str) -> Option<Self>` — likewise an `Option`; the driver's fixed RSA identity is a 40-character hex string parsed with this. **Both types also need their infallible array conversions**: `impl From<[u8; 32]> for Ed25519Identity` and `impl From<[u8; 20]> for RsaIdentity`. A gate group builds its identities with `Ed25519Identity::from([0xAB; 32])` and `RsaIdentity::from([0x11; 20])`, so a crate that keeps only `from_bytes`/`from_hex` fails to compile that group and vetoes the step |

Observable behaviour:

* Forty hex digits are an RSA identity with or without a leading `$`; `Display`
  always writes the canonical lowercase `$`-prefixed form.
* A bare 43-character unpadded base64 string is an Ed25519 identity **in the tagged text form `RelayId` uses**; in that form `Display` writes `ed25519:<base64>` and re-parses to the same value. The bare `Ed25519Identity` type's own `Display` is a different rendering, and §7 states it where the router-descriptor case asserts it.
* An explicit `<algorithm>:` prefix overrides the shape of the payload, so
  `rsa:<27 chars of base64>` is an RSA identity. Algorithm names match without
  regard to case. An unknown algorithm gives `UnrecognizedIdType`.
* `Ed25519.id_len() == 32`, `Rsa.id_len() == 20`, `all_types()` yields Ed25519
  then Rsa, and `COUNT == 2`.
* Wrong byte lengths and wrong base64 payload lengths give `BadLength`; forty
  non-hex characters give `BadHex`.
* Bytes and text name the same identity, and `RelayIdRef` compares equal to the
  bare key it borrows.
* `HasRelayIds` default methods, checked against first-party implementors so that
  only the trait's own logic is under test: `identities()` lists present keys in
  `all_types()` order, skipping absent ones; `has_identity` matches only within a
  key type, and an unknown key is not a match; `same_relay_ids` compares every
  key type including the absent ones, so knowing one extra key is a difference;
  `has_all_relay_ids_from` is asymmetric — a fuller record satisfies a sparser
  one but not the other way round, and a conflicting key fails either way.
* `RelayIdSet` holds at most one identity per key type, reports insertion
  novelty, and can be collected straight from `identities()`.

## 7. Router descriptors — `tor_netdoc` and `tor_checkable`

Checked by groups `rdesc`, `bridgedesc`.

| Item | Signature / form |
| --- | --- |
| `tor_netdoc::doc::routerdesc::RouterDesc` | `struct` |
| `RouterDesc::parse` | `fn parse(s: &str) -> Result<UncheckedRouterDesc>` |
| `RouterDesc::rsa_identity` | `fn rsa_identity(&self) -> &RsaIdentity` |
| `RouterDesc::ed_identity` | `fn ed_identity(&self) -> &Ed25519Identity` |
| `RouterDesc::or_ports` | `fn or_ports(&self) -> impl Iterator<Item = SocketAddr> + '_` |
| `RouterDesc::published` | `fn published(&self) -> SystemTime` |
| `RouterDesc::ntor_onion_key` | `fn ntor_onion_key(&self) -> &curve25519::PublicKey` |
| `RouterDesc::protocols` | `fn protocols(&self) -> &tor_protover::Protocols` |
| `tor_netdoc::doc::routerdesc::UncheckedRouterDesc` | the `parse` result; a `SignatureGated<TimerangeBound<RouterDesc>>` |
| `tor_checkable::SelfSigned` | `trait SelfSigned<T>: Sized { type Error; fn is_well_signed(&self) -> Result<(), Self::Error>; fn dangerously_assume_wellsigned(self) -> T; fn check_signature(self) -> Result<T, Self::Error>; }` — generic over the checked type, and the success type is that `T`. There is no associated `Okay` type at this checkpoint, and `check_signature` **consumes** `self` |
| `tor_checkable::Timebound` | `trait Timebound<T>: Sized { type Error; fn is_valid_at(&self, t: &SystemTime) -> Result<(), Self::Error>; fn dangerously_assume_timely(self) -> T; fn check_valid_at(self, t: &SystemTime) -> Result<T, Self::Error>; fn check_valid_now(self) -> Result<T, Self::Error>; }` — same shape: generic over the checked type, and every consuming method takes `self` by value. Only `dangerously_assume_timely` is called by the suite |

Observable behaviour checked, against a real archived descriptor carried as a
fixture (`tests/assets/harness/fixtures/routerdesc-akka.txt`):

* `rsa_identity()` is the identity the `fingerprint` line states.
* `ed_identity()` is the master key the `identity-ed25519` certificate carries,
  cross-checked against the `master-key-ed25519` line.
* `or_ports()` yields the `router` line's address and port first, then each
  `or-address` line in document order.
* `published()` is the `published` line read as UTC.
* `ntor_onion_key()` is the curve25519 key from the `ntor-onion-key` line.
* `protocols()` is the `proto` line, and prints back in canonical form.
* Trailing blank lines after `router-signature` are tolerated.
* Altering one byte of the signed body makes `check_signature()` fail. This is
  the case that makes the group non-vacuous: it shows the signature is actually
  verified rather than skipped.

`published()` is read through `dangerously_assume_timely()`, never through
`check_valid_at` or `check_valid_now`, so the suite has no wall-clock dependence.

## 8. Bridge descriptors — `tor_guardmgr::bridge`

Checked by group `bridgedesc`.

| Item | Signature / form |
| --- | --- |
| `tor_guardmgr::bridge::BridgeDesc` | `struct`; `Clone + Debug`, `AsRef<RouterDesc>`, and `HasRelayIds` |
| `BridgeDesc::new` | `fn new(desc: Arc<RouterDesc>) -> Self` |
| `tor_guardmgr::bridge::BridgeDescList` | `type BridgeDescList = HashMap<BridgeConfig, Result<BridgeDesc, Box<dyn BridgeDescError>>>` |
| `tor_guardmgr::bridge::BridgeDescError` | `trait: std::error::Error + dyn_clone::DynClone + HasKind + HasRetryTime + Send + Sync + 'static`, with `dyn_clone::clone_trait_object!(BridgeDescError)` alongside it. The `DynClone` half is load-bearing: the suite calls `.clone()` on a `Box<dyn BridgeDescError>` taken out of a `BridgeDescList`, which only compiles if the blanket `Clone` for the boxed trait object exists |
| `tor_guardmgr::bridge::BridgeDescProvider` | `trait: DynClone + Send + Sync { fn bridges(&self) -> Arc<BridgeDescList>; fn events(&self) -> BoxStream<'static, BridgeDescEvent>; fn set_bridges(&self, bridges: &[BridgeConfig]); }` |
| `tor_guardmgr::bridge::BridgeDescEvent` | `enum`; the variant the suite matches is `SomethingChanged` |
| `tor_error::HasKind` / `HasRetryTime` / `ErrorKind` / `RetryTime` | `fn kind(&self) -> ErrorKind` and `fn retry_time(&self) -> RetryTime`; used here to give the driver's own first-party error type a kind and a retry time and read them back through the boxed trait object |

`ErrorKind` has grown since step 06 enumerated it, and one of the variants step
06 lists is relabelled here; §10 records what is true at this checkpoint. The
variant this section uses, `TorAccessFailed`, is not one of the affected ones.

Observable behaviour:

* A `BridgeDesc` reports the identities of the descriptor it wraps, through
  `HasRelayIds`.
* `AsRef<RouterDesc>` gives back the whole descriptor, still readable.
* A descriptor can confirm the identities a bridge line claimed:
  `desc.has_all_relay_ids_from(&bridge_config)` is true for a line that names a
  subset of the descriptor's keys, and `same_relay_ids` is true only when the
  line names all of them.
* A `BridgeDescList` is keyed by `BridgeConfig` — the bridge line is the lookup
  key for the descriptor fetched for it — and a slot may hold a boxed
  `BridgeDescError` instead of a descriptor.
* `BridgeDescProvider` is object-safe and clonable as a trait object, and its
  `events()` stream yields `BridgeDescEvent::SomethingChanged`. The implementor
  under test is first-party, so what is checked is the shape of the trait, not any
  particular arti provider.

## 9. SOCKS client handshake — `tor_socksproto`

Checked by group `socksclient`.

| Item | Signature / form |
| --- | --- |
| `tor_socksproto::SocksClientHandshake` | `struct` |
| `SocksClientHandshake::new` | `fn new(request: SocksRequest) -> Self` |
| `SocksClientHandshake::handshake` | `fn handshake(&mut self, input: &[u8]) -> Result<Result<Action, Error>, Truncated>` |
| `SocksClientHandshake::into_reply` | `fn into_reply(self) -> Option<SocksReply>` |
| `tor_socksproto::Action` | `struct { pub drain: usize, pub reply: Vec<u8>, pub finished: bool }` |
| `tor_socksproto::SocksRequest::new` | `fn new(version: SocksVersion, cmd: SocksCmd, addr: SocksAddr, port: u16, auth: SocksAuth) -> Result<Self, Error>` |
| `tor_socksproto::SocksVersion` | `enum { V4, V5 }` |
| `tor_socksproto::SocksCmd` | newtype over `u8`; `SocksCmd::CONNECT` |
| `tor_socksproto::SocksAddr` | `enum { Ip(IpAddr), Hostname(SocksHostname) }` |
| `tor_socksproto::SocksHostname` | `TryFrom<String, Error = Error>` — the suite builds every hostname address with `String::try_into()`, so the fallible conversion must exist and must accept a name of 255 bytes or fewer |
| `tor_socksproto::SocksAuth` | `enum { NoAuth, Socks4(Vec<u8>), Username(Vec<u8>, Vec<u8>) }` |
| `tor_socksproto::SocksStatus` | newtype over `u8`; `SUCCEEDED`, `GENERAL_FAILURE` |
| `tor_socksproto::SocksReply` | three accessors, **all three bound by the suite**: `status(&self) -> SocksStatus`, `addr(&self) -> &SocksAddr` (the proxy's bound address, borrowed — not returned by value) and `port(&self) -> u16` (the proxy's bound port). The borrowed address is compared by equality against a `SocksAddr::Ip`, so `SocksAddr` is `PartialEq + Debug` |
| `tor_socksproto::Truncated` | returned as the outer `Err` when input is short; at this checkpoint it is a re-export (`pub use tor_error::Truncated;`), so the name must resolve through `tor_socksproto` as well as `tor_error` |

Observable behaviour checked — the byte strings are asserted against the protocol
definitions, not against arti's own encoder, so the group checks the wire format:

* **The handshake is opened by calling `handshake(&[])` with an empty slice.**
  The client speaks first in both dialects, so in its initial state the handshake
  ignores `input` entirely and *emits*. Every case in the group starts this way,
  and the opening call must return `Ok(Ok(Action))` carrying the client's first
  message, with `drain == 0` ("nothing has arrived yet to consume") and
  `finished == false`. An empty slice must **not** give `Err(Truncated)`:
  truncation applies only once the client is waiting on the proxy — see the last
  bullet. A uniform "insufficient input ⇒ `Truncated`" rule fails this group.
* The first thing a version-5 client sends is `[0x05, nmethods, methods...]`: for
  `SocksAuth::NoAuth` that is `[5, 1, 0]`, and with a username/password it offers
  two methods, `[5, 2, 2, 0]`.
* When the server selects method 2, the client sends
  `[0x01, ulen, username, plen, password]`.
* The version-5 request is `[0x05, cmd, 0x00, atype, addr..., port_hi, port_lo]`,
  with address type 3 (length-prefixed hostname) or 1 (four-byte IPv4).
* The final version-5 reply is parsed and handed back through `into_reply()`
  with the status the server sent **and the bound address and port it carried**
  (all three of `status()`, `addr()` and `port()` are asserted), and the `Action`
  reports the handshake finished.
* A version-4 client sends one message,
  `[0x04, cmd, port_hi, port_lo, ip(4), userid..., 0x00]`, with the user ID
  NUL-terminated.
* Version 4a signals a hostname by writing an impossible address in the four
  address bytes, then appending the NUL-terminated name after the NUL-terminated
  user ID. The SOCKS4a memo allows any `0.0.0.x` with a nonzero last octet, but
  **the suite asserts the whole message byte-for-byte and the value it expects is
  `0.0.0.1`** — i.e. the four bytes `00 00 00 01`. Write that exact sentinel; any
  other nonzero octet is protocol-legal and still fails the comparison. This
  pins only what the **client emits**; the responder side introduced at step 01
  still recognises any `0.0.0.x` sentinel on the way in, and nothing here
  narrows that.
* Version-4 status bytes map onto the version-5 codes: `0x5A` becomes
  `SocksStatus::SUCCEEDED`, `0x5B` becomes `SocksStatus::GENERAL_FAILURE`.
* A reply short by even one byte gives the outer `Err(Truncated)`, and the
  handshake consumes nothing, so re-feeding the complete buffer succeeds.

## 10. Pluggable-transport manager — `tor_ptmgr`

Checked by groups `ptconfig`, `ptenv`, `ptproto`, `ptlaunch`, `pterrkind`.

| Item | Signature / form |
| --- | --- |
| `tor_ptmgr::ipc::PtParameters` | `struct`; `PtParameters::builder() -> PtParametersBuilder` |
| `PtParametersBuilder` | `state_location(PathBuf)`, `transports(Vec<PtTransportName>)`, `timeout(Option<Duration>)`, `proxy_uri(Option<String>)`, `outbound_bind_v4(Option<Ipv4Addr>)`, `outbound_bind_v6(Option<Ipv6Addr>)`, `build() -> Result<PtParameters, _>` |
| `tor_ptmgr::ipc::PluggableTransport` | `struct` |
| `PluggableTransport::new` | `fn new(binary_path: PathBuf, arguments: Vec<String>, params: PtParameters) -> Self` |
| `PluggableTransport::launch` | `async fn launch<R: Runtime>(&mut self, rt: R) -> Result<(), PtError>` — the only method on `PluggableTransport` the suite calls; it returns the launch outcome and nothing else |
| `tor_ptmgr::ipc::PtClientMethod` | `pub struct` with `pub fn kind(&self) -> SocksVersion` and `pub fn endpoint(&self) -> SocketAddr`, but **no public accessor on `PluggableTransport` returns one** — `transport_methods()` is `pub(crate)`, so the suite only checks what `launch` returns |
| `tor_ptmgr::err::PtError` | `enum`; the variants the suite matches are `UnsupportedVersion`, `ChildProtocolViolation(String)`, `ProtocolViolation(String)`, `ProxyError(String)`, `ClientTransportGaveError { transport, message }`, `ClientTransportsUnsupported(Vec<String>)`, `Timeout`, and `ChildSpawnFailed { .. }`. `ClientTransportGaveError` is destructured with **no** `..`, so it must have exactly those two fields and no others; the single-field variants are destructured positionally. `NotAFile { path }` is described in the last bullet below but no case produces it, and `Internal` is never matched |
| `tor_ptmgr::config::ManagedTransportConfigBuilder` | `protocols(Vec<PtTransportName>)`, `path(CfgPath)`, `arguments(Vec<String>)`, `run_on_startup(bool)`, `build() -> Result<ManagedTransportConfig, ConfigBuildError>` |
| `tor_ptmgr::PtMgr::new` | `fn new(cfg: Vec<ManagedTransportConfig>, state_dir: PathBuf, rt: R) -> Result<Self, PtError>` |
| `tor_ptmgr::PtMgr::reconfigure` | `fn reconfigure(&self, how: tor_config::Reconfigure, cfg: Vec<ManagedTransportConfig>) -> Result<(), tor_config::ReconfigureError>` |
| `tor_chanmgr::factory::AbstractPtMgr` | `#[async_trait] pub trait AbstractPtMgr: Send + Sync { async fn factory_for_transport(&self, transport: &PtTransportName) -> Result<Option<Arc<dyn ChannelFactory + Send + Sync>>, Arc<dyn AbstractPtError>>; }`. The error half is the separate object-safe trait `tor_chanmgr::factory::AbstractPtError: std::error::Error + HasKind + HasRetryTime + Send + Sync + Debug` — **not** a bare `dyn HasKind`. `ptconfig` only ever takes the `Ok` arm, but it formats the `Err` arm with `{}` in its panic message, so `Arc<dyn AbstractPtError>` must stay `Display`, which it is via the `std::error::Error` supertrait. `PtMgr` is the implementor the suite calls this on, so the trait must be in scope (`use tor_chanmgr::factory::AbstractPtMgr;`) for the method to resolve at all |
| `tor_config::CfgPath` | `pub fn new_literal<P: Into<PathBuf>>(path: P) -> Self`. The generic is load-bearing: the suite passes a `&str` literal directly (`CfgPath::new_literal("/usr/bin/obfs4proxy")`), which does not compile against a plain `PathBuf` parameter |
| `tor_rtcompat::tokio::TokioNativeTlsRuntime` | `fn create() -> std::io::Result<Self>` plus `Clone`; this is the concrete runtime every async case in §10 constructs, so the `tokio` and `native-tls` features must expose it at exactly this path |
| `tor_rtcompat::BlockOn` | used to drive the async items above without a `#[tokio::test]` attribute |
| `tor_error::HasKind`, `HasRetryTime`, `ErrorKind`, `RetryTime` | `fn kind(&self) -> ErrorKind` and `fn retry_time(&self) -> RetryTime`, implemented for `PtError`; both results are compared by equality against a named variant |

The managed proxy under test is **first-party**: `tests/assets/harness/src/bin/mockpt.rs`
is a small binary the driver builds and launches, which speaks the manager side
of the IPC protocol back at arti and records the environment it was given. No
real transport binary, and no network, is involved.

Observable behaviour:

* **Environment** (group `ptenv`) — the manager sets
  `TOR_PT_MANAGED_TRANSPORT_VER=1`, `TOR_PT_STATE_LOCATION` to the configured
  state directory, `TOR_PT_EXIT_ON_STDIN_CLOSE=1`, and `TOR_PT_CLIENT_TRANSPORTS`
  to the requested transport names joined with commas in the order requested.
  `TOR_PT_PROXY` is absent when no proxy was configured.
* **Protocol** (group `ptproto`) — the manager reads the proxy's stdout lines and
  classifies them: `VERSION-ERROR` gives `UnsupportedVersion`; a `VERSION` other
  than `1` is a `ProtocolViolation` whose message names the version that was
  stated; `ENV-ERROR` is a
  `ChildProtocolViolation`; `PROXY-ERROR` is reported as `ProxyError` carrying the
  message; `PROXY DONE` when no proxy was asked for is a `ProtocolViolation`;
  `CMETHOD-ERROR` gives `ClientTransportGaveError` naming the transport and its
  message; `CMETHODS DONE` with no `CMETHOD` line gives
  `ClientTransportsUnsupported` listing what was asked for; a `CMETHOD` for a
  transport that was not requested is a `ProtocolViolation`; and a `CMETHOD`
  naming a SOCKS dialect other than `socks4` or `socks5` is a
  `ProtocolViolation`.
* **Launch** (group `ptlaunch`) — a clean startup succeeds; the same script
  announcing `socks4` also succeeds; several transports all
  launch; a binary that says nothing at all is abandoned with `Timeout` after the
  configured timeout (which the suite sets to a short, fixed value — the only
  timing in the suite, and it is a bound not a sleep); `PROXY DONE` is accepted
  when a proxy *was* configured; and calling `launch` again on a live transport
  is harmless. The suite checks the launch outcome only: `PluggableTransport`
  exposes no public accessor for the announced dialect or endpoint, so
  `kind()`/`endpoint()` are not asserted anywhere.
* **Configuration** (group `ptconfig`) — the builder accepts a protocol list and
  a binary path, and refuses to build without either. **The refusal names the
  field it is missing**, in the error's `Display`, as
  `ConfigBuildError::MissingField { .. }` does for §4's bridge builder: an
  unset `path` produces a rendering containing `path`, and an unset `protocols`
  one containing `protocols`. (A case does not compare whole strings — it looks
  for the field's name — so the surrounding wording is yours; omitting the name
  is not.) A `PtMgr` asked for a
  transport it was never configured with returns `Ok(None)` from
  `factory_for_transport`; and `reconfigure` with `Reconfigure::CheckAllOrNothing`
  validates without changing the manager.
* **Error classification** (group `pterrkind`) — every error checked here is
  produced by a real scripted launch, never hand-constructed. `UnsupportedVersion`
  has `ErrorKind::LocalProtocolViolation` and `RetryTime::Never`;
  `ProtocolViolation` has `ErrorKind::LocalProtocolViolation`; `Timeout` has
  `ErrorKind::ExternalToolFailed` and `RetryTime::AfterWaiting` — a variant
  distinct from `RetryTime::After`, and the two are compared by equality, so the
  wrong one fails; `ClientTransportsUnsupported` has `ErrorKind::InvalidConfig`
  and `RetryTime::Never`. A binary path that cannot be spawned — including one
  that does not exist — is `PtError::ChildSpawnFailed { .. }`, and this step's
  case in scope requires `RetryTime::Never` for it. (The release's own mapping is
  finer: a spawn failure whose cause is `NotFound` is `Never`, any other cause
  is `AfterWaiting`. The path in scope is the not-found one, so either reading
  passes; implement the finer one if you want the release's behaviour.) That
  case is also why `NotAFile` never appears,
  because it is raised only for a configured path with no final component, i.e.
  one ending in a slash. The retry times of `ChildProtocolViolation`,
  `ProxyError` and `ClientTransportGaveError` are not asserted anywhere.
  `ExternalToolFailed` is one of six `ErrorKind` variants that did not exist
  when step 06 enumerated that enum; the subsection closing this section says
  what the enum is here.
* **Out of scope here** — the shutdown handshake of pt-spec §3.4 and the
  per-connection transport arguments of §3.5. `TOR_PT_EXIT_ON_STDIN_CLOSE` is
  checked as an environment entry above, but nothing observes the child's exit:
  the mock proxy ends when its own script ends, so a stdin-driven shutdown is
  indistinguishable from an ordinary one through the public API, which reports
  neither. The `key=value` settings a bridge line carries are checked where they
  are parsed, validated and stored (sections 2 and 4); they are never *sent*,
  because sending them means a real SOCKS handshake against a real transport,
  which scope decision S5 excludes. `SPEC_NOTES.md` section 17 records both
  exclusions in full.

### The `ErrorKind` enumeration at this checkpoint — superseding step 06 and step 07

`tor_error::ErrorKind` is `#[non_exhaustive]` and has grown since it was first
enumerated. Two earlier contracts still on disk under `/app/docs/` enumerate it.
Both were accurate at their own checkpoint and both remain binding, so where
they conflict with this subsection, this one wins:

* `/app/docs/step-06/TEST_INTERFACE.md` §1.1 declares
  `pub enum ErrorKind { /* 40 variants */ }`, and its §1.1.1 gives a 40-row
  variant→rendering table under the sentence "Every one of these 40 renderings
  is checked."
* `/app/docs/step-07/TEST_INTERFACE.md` §12.1 already supersedes that to **42**,
  adding `ClockSkew` and `Other`, and closes by saying the count grew "with no
  other variant renamed or relabelled". That clause was true where it was
  written and is **not true here** — see the relabelling below.

**At this checkpoint the enum has 46 variants.** Six exist here that step 06's
table does not list. **Nothing was renamed and nothing was removed**: every one
of the 40 names step 06 gives still resolves, and 39 of its 40 renderings are
unchanged.

| Variant added since step 06 | `to_string()` |
| --- | --- |
| `ClockSkew` | `possible clock skew detected` |
| `Other` | `unclassified error` |
| `FsPermissions` | `problem with filesystem permissions` |
| `RemoteHostResolutionFailed` | `remote hostname lookup failure` |
| `LocalProtocolFailed` | `local authentication refused.` |
| `ExternalToolFailed` | `an externally launched plug-in tool failed` |

`ExternalToolFailed` is the only one of the six that this step's suite names —
in the `pterrkind` assertion on `PtError::Timeout` above. An `ErrorKind`
reproduced with exactly step 06's 40 variants does not compile against that
case. The other five are not exercised by any group here, but the enum is
`#[non_exhaustive]`, growing it is the expected thing for it to do, and a
solution that reproduces this checkpoint has 46.

**One variant is relabelled here, and its former label now belongs to one of
the new variants.** This is the case step 07's "no other variant renamed or
relabelled" no longer covers, and it is the trap in copying either earlier
table forward verbatim:

| Variant | steps 06 and 07 give | true at this checkpoint |
| --- | --- | --- |
| `RemoteHostNotFound` | `remote hostname lookup failure` | `remote hostname not found` |
| `RemoteHostResolutionFailed` | (did not exist) | `remote hostname lookup failure` |

Copying `RemoteHostNotFound`'s row forward gives two variants the same `Display`
output and gives `RemoteHostNotFound` the one that belongs to the other. No
group in this step reads an `ErrorKind`'s rendering — `bridgedesc` and
`pterrkind` each compare `kind()` by equality against a named variant and never
call `to_string()` on one — so nothing checked here fails on the labels. They are
stated because step 06's table is binding, is written to be copied from, and
now names the wrong string for one of its 40 rows.

## 11. Channel padding — `tor_proto`, `tor_cell`, `tor_config`, `tor_chanmgr` (inherited)

Checked by groups `padparams`, `padlevel`.

| Item | Signature / form |
| --- | --- |
| `tor_proto::channel::padding::Parameters` | `struct`; `Debug + Copy + Clone + Eq + PartialEq`. Both fields are `IntegerMilliseconds<u32>` and non-public |
| `Parameters::builder` | `fn builder() -> ParametersBuilder`, with `low(IntegerMilliseconds<u32>)`, `high(IntegerMilliseconds<u32>)` and `build() -> Result<Parameters, tor_error::Bug>`. Two details are load-bearing: the build error is `tor_error::Bug` (the struct carries `#[builder(build_fn(error = "tor_error::Bug"))]`), not derive_builder's default `ParametersBuilderError` and not `ConfigBuildError`; and the setters are *not* `setter(into)`, so the suite writes `.low(1500_u32.into())` and a setter taking a bare `u32` would not match |
| `Parameters::default_padding` | `fn default_padding() -> Self` |
| `Parameters::disabled` | `fn disabled() -> Self` |
| `Parameters::padding_negotiate_cell` | `fn padding_negotiate_cell(&self) -> Result<PaddingNegotiate, tor_error::Bug>` |
| `tor_cell::chancell::msg::PaddingNegotiate` | `struct`; derives `Clone + Debug + Eq + PartialEq`, with `pub fn start(ito_low: IntegerMilliseconds<u16>, ito_high: IntegerMilliseconds<u16>) -> Self`, `pub fn start_default() -> Self` and `pub fn stop() -> Self`. Note the `u16` width here against the `u32` of `Parameters`: converting between them is what `padding_negotiate_cell` can fail at. `Default` is a *hand-written* impl that returns `start_default()`, not a derive — a `#[derive(Default)]` would zero the command field to STOP and the `default() == start_default()` case would fail |
| `tor_cell::chancell::msg::Body` | `trait Body: Readable { fn write_body_onto<W: Writer + ?Sized>(self, w: &mut W) -> EncodeResult<()>; … }` — the `Readable` supertrait is what makes decoding through `tor_bytes::Reader::extract::<T>()` work; the suite encodes into a `Vec<u8>`, so `Vec<u8>` must satisfy `Writer` |
| `tor_bytes::Reader` | `Reader::from_slice(&[u8])`, `extract::<T>() -> Result<T>`, and `remaining() -> usize`; the suite asserts `remaining() == 0` after a decode, so a body that leaves trailing bytes behind fails even though the decode succeeded |
| `tor_units::IntegerMilliseconds<T>` | `From<u32>` / `From<u16>` |
| `tor_config::PaddingLevel` | `enum { None, Reduced, Normal }`; `Default == Normal`, `Display`, `FromStr`, `Serialize`, `Deserialize` |
| `tor_chanmgr::ChannelConfig` | `struct`; `Debug + Clone + Eq`, a `Default` impl, and `ChannelConfig::builder() -> ChannelConfigBuilder`. It is **not** itself `Deserialize` at this checkpoint — configuration is read into the *builder*, see the next row |
| `tor_chanmgr::ChannelConfigBuilder` | `Deserialize` and `Default`; `build() -> Result<ChannelConfig, ConfigBuildError>`. The `Deserialize` is the load-bearing half: the suite does `toml::from_str::<ChannelConfigBuilder>("padding = …")` and then builds. A `padding(PaddingLevel)` setter is generated too, but no case calls it |

Observable behaviour:

* `Parameters::builder().low(1500_u32.into()).high(9500_u32.into()).build()`
  equals `Parameters::default_padding()` — the default padding window is 1500 to
  9500 milliseconds. (The `.into()` is required; see the setter note above.)
* `default_padding().padding_negotiate_cell()` equals
  `PaddingNegotiate::start(1500, 9500)`, and its body encodes to the six bytes
  `00 02 05 DC 25 1C`: a one-byte version 0, a one-byte command 2 (START), then
  the two timeouts as big-endian `u16` millisecond values.
* `Parameters::disabled().padding_negotiate_cell()` encodes to
  `00 02 00 00 00 00` and equals both `PaddingNegotiate::start_default()` and
  `PaddingNegotiate::default()` — a START with two zero timeouts is the request
  to use the responder's defaults.
* `PaddingNegotiate::stop()` encodes to `00 01 00 00 00 00`: command 1 is STOP.
* Every encoded body reads back through `Reader::extract` to an equal message.
* A version byte other than 0 is rejected at decode time.
* A timeout that does not fit in a `u16` of milliseconds (70 000) is refused by
  `padding_negotiate_cell()` rather than silently truncated.
* `PaddingLevel::default()` is `Normal`; it displays and parses in snake_case
  (`none`, `reduced`, `normal`); it deserialises from the boolean shorthand
  (`false` is `None`, `true` is `Normal`) but always serialises as a name; and
  unknown spellings are rejected.
* `ChannelConfigBuilder` deserialises a `padding` field from TOML in all of those
  forms — `"reduced"`, `"normal"`, and the boolean `false` — and the built
  configurations compare unequal to each other where the levels differ.
  `ChannelConfig::default()` equals `ChannelConfig::builder().build()`.

**Out of scope:** `ChannelConfig::padding()` is behind the `testing` feature of
`tor-chanmgr`, which the driver does not enable, so the padding level is observed
through equality of whole `ChannelConfig` values rather than through an accessor.

## 12. Safe logging — `safelog`

Checked by group `brlog`.

| Item | Signature / form |
| --- | --- |
| `safelog::Redactable` | `pub trait Redactable: std::fmt::Display + std::fmt::Debug { fn display_redacted(&self, f: &mut Formatter<'_>) -> fmt::Result; fn debug_redacted(&self, f: &mut Formatter<'_>) -> fmt::Result { … }; fn redacted(&self) -> Redacted<&Self>; fn maybe_redacted(&self, redact: bool) -> MaybeRedacted<&Self>; }`. The `Display + Debug` supertrait bound is part of the contract — only `display_redacted` has no default body; `debug_redacted`, `redacted` and `maybe_redacted` are all provided |

Implementors: `std::net::SocketAddr`, `tor_linkspec::BridgeAddr`,
`tor_linkspec::PtTargetAddr`, `tor_llcrypto::pk::ed25519::Ed25519Identity`,
`tor_llcrypto::pk::rsa::RsaIdentity`, `tor_linkspec::OwnedChanTarget`.

Observable behaviour:

* An IPv4 socket address redacts to `<first octet>.x.x.x:<port>`; an IPv6 one to
  `[<first group>:x:x:…]:<port>`.
* A `BridgeAddr` that is a hostname redacts to its first two characters, an
  ellipsis, and the port: `bridge.example:9001` becomes `br…:9001`. A numeric
  `BridgeAddr` redacts as the socket address does.
* `PtTargetAddr::None` is `-` redacted or not — there is nothing to hide.
* An identity redacts to a two-character prefix plus an ellipsis, keeping the
  `$` sigil for RSA. `maybe_redacted(false)` is the ordinary rendering and
  `maybe_redacted(true)` the redacted one.
* A redacted channel target keeps its (redacted) connection point and **only the
  first** identity, printed without its algorithm tag.
* A redacted pluggable target still names its transport in the clear — the
  transport name is not the secret; the address is.

---

## 13. Requirement groups → interface items

| Group | Kind | Items |
|---|---|---|
| `transportid` | requirement | §1 |
| `pttarget` | requirement | §1, §2 |
| `briaddr` | requirement | §2 |
| `bridgeline` | requirement | §3, §4 |
| `bridgeerr` | requirement | §4 |
| `bridgebuild` | requirement | §3, §4 |
| `chantarget` | requirement | §3, §4, §5 |
| `chanmethod` | requirement | §1, §2, §3 |
| `rdesc` | requirement | §7 |
| `bridgedesc` | requirement | §4, §7, §8 |
| `owntarget` | requirement | §2, §3, §4, §5 |
| `socksclient` | requirement | §9 |
| `ptconfig` | requirement | §10 |
| `ptenv` | requirement | §10 |
| `ptproto` | requirement | §10 |
| `ptlaunch` | requirement | §10 |
| `relayid` | gate | §6 |
| `ridtraits` | gate | §6 |
| `padparams` | gate | §11 |
| `padlevel` | gate | §11 |
| `pterrkind` | requirement | §10 |
| `brlog` | requirement | §1, §2, §3, §5, §12 |

The group names match `tests/assets/groups.txt`. The gate groups
(`relayid`, `ridtraits`, `padparams`, `padlevel`) re-check behaviour introduced
at step 11 against the cumulative step-12 workspace; `pterrkind` and `brlog`
cover breadth around the new bridge and pluggable-transport surface rather than
its core.

---

## Driver-side support, for completeness

These are first-party and are **not** part of the test interface; they are
listed so a reader can tell them apart from arti's own API.

* `tests/assets/harness/src/bin/mockpt.rs` — the mock managed proxy.
* `tests/assets/harness/tests/common/mod.rs` — fixture loading, the fixed test
  identities, scratch directories, and the helpers that launch the mock proxy and
  read back the environment it recorded.
* `tests/assets/harness/fixtures/routerdesc-akka.txt` — one archived router
  descriptor, used unmodified.

### Declared here, not driven by any case

The contract above is the surface, not the call list. The items below are named
because the milestone or a real caller needs them, and no group in this step
reaches them. Implement them as written.

- **`TOR_PT_PROXY` reaching the child.** The launch cases pin the manager's
  acceptance of a `PROXY DONE` line and its refusal of an unasked-for one; they
  do not read the environment the mock was started with. The mock records it to
  `env.txt` for a future case to check. Implement the passthrough.
- **Dropping a live `PtMgr`.** The relaunch obligation is exercised; the drop
  obligation — that dropping the manager does not leave a conversation
  half-finished — is stated in `SPEC_NOTES.md` and reached by no case, because
  no group drops a manager mid-conversation.
- **The `SocksHandshake` → `SocksProxyHandshake` migration.** The
  `proxy-handshake` feature is enabled and the responder type must exist and
  compile, but no group names or reaches it; the migration can break your own
  build and not a requirement group.
- **The `Error::NotAFile` variant** — raised only for a configured path with no
  final component, which no case configures.
- **The shutdown handshake** (`SHUTDOWN`/`SHUTDOWN-ERROR`) — declared out of scope
  in `SPEC_NOTES.md`; the mock exits on its own.

One inherited rendering that this step asserts is worth naming here rather than
leaving to be inferred: the bare `Ed25519Identity` prints **untagged** (its
base64 alone, no `ed25519:` prefix) in the router-descriptor case, while the
tagged form belongs to `RelayId`. §6 states the tagged form; §7's
`ed_identity()` bullet is where the bare one is checked.
