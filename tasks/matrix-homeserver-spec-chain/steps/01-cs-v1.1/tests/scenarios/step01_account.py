"""Step 1 — accounts: registration, user-interactive auth, login, devices, profile."""

from mxconf.mx import CS, User, login, ok, q, register
from mxconf.registry import scenario


@scenario("cs/versions", tags=["versions"], step=1)
def versions(c):
    # Which unstable features an implementation lists is its own business; the
    # published spec release it claims is not.
    c.req("GET", "/_matrix/client/versions", label="GET /versions",
          derive=lambda b: {
              "supports_v1_1": "v1.1" in b.get("versions", []),
              "versions_is_list_of_strings": all(
                  isinstance(v, str) for v in b.get("versions", [])),
          })


@scenario("cs/capabilities", tags=["capabilities"], step=1)
def capabilities(c):
    u = register(c, "cap1")

    def caps(b):
        caps = b.get("capabilities", {})
        rv = caps.get("m.room_versions", {})
        avail = rv.get("available", {})
        return {
            "m.change_password.enabled": caps.get("m.change_password", {}).get("enabled"),
            "m.room_versions.default_is_available": rv.get("default") in avail,
            "m.room_versions.1_to_6_stable": [
                avail.get(str(v)) for v in range(1, 7)],
        }

    c.req("GET", f"{CS}/capabilities", token=u.token, label="capabilities",
          derive=caps)
    c.req("GET", f"{CS}/capabilities", label="capabilities without a token")


@scenario("cs/login-flows", tags=["login"], step=1)
def login_flows(c):
    # Which login types a deployment offers is its own business; that the
    # password flow is advertised, and that every flow carries a `type`, is not.
    c.req("GET", f"{CS}/login", label="GET /login flows",
          derive=lambda b: {
              "password_flow_offered": any(
                  f.get("type") == "m.login.password" for f in b.get("flows", [])),
              "every_flow_has_a_type": all(
                  isinstance(f.get("type"), str) for f in b.get("flows", [])),
          })


@scenario("cs/unknown-endpoint", tags=["errors"], step=1)
def unknown_endpoint(c):
    c.req("GET", f"{CS}/this_endpoint_does_not_exist", label="unknown endpoint")
    c.req("GET", "/_matrix/client/v9/sync", label="unknown version prefix")


def uia_challenge(want):
    """What a user-interactive-authentication challenge has to say.

    The release fixes that the challenge is a 401 carrying `flows`, a
    `session`, and `params`; it does not fix *which* flows a server offers, nor
    that `params` is present when there is nothing to put in it.  So what is
    compared is that the stage the scenario is about is on offer, and that a
    session was issued.
    """
    def derive(b):
        flows = (b or {}).get("flows") or []
        stages = {st for f in flows if isinstance(f, dict)
                  for st in (f.get("stages") or [])}
        return {"offers": want in stages,
                "has_session": bool((b or {}).get("session")),
                "completed": (b or {}).get("completed", [])}
    return derive


@scenario("cs/register-dummy-flow", tags=["register", "uia"], step=1)
def register_dummy(c):
    body = {"username": "reg1", "password": "pw-reg1-123"}
    c.req("POST", f"{CS}/register", json_body=body,
          label="register: UIA challenge",
          derive=uia_challenge("m.login.dummy"))
    r = c.req("POST", f"{CS}/register", json_body=body, check=False)
    body["auth"] = {"type": "m.login.dummy", "session": (r.json or {}).get("session")}
    c.req("POST", f"{CS}/register", json_body=body, label="register: complete dummy")
    # A fresh attempt, with its own user-interactive-auth session, must be
    # refused because the localpart is now taken.
    again = {"username": "reg1", "password": "pw-reg1-123"}
    r2 = c.req("POST", f"{CS}/register", json_body=again, check=False)
    again["auth"] = {"type": "m.login.dummy", "session": (r2.json or {}).get("session")}
    c.req("POST", f"{CS}/register", json_body=again, label="register: username taken")


@scenario("cs/register-available", tags=["register"], step=1)
def register_available(c):
    c.req("GET", f"{CS}/register/available", query={"username": "freshname"},
          label="available: free")
    register(c, "taken1")
    c.req("GET", f"{CS}/register/available", query={"username": "taken1"},
          label="available: taken")
    c.req("GET", f"{CS}/register/available", query={"username": "BadName!"},
          label="available: invalid")


@scenario("cs/register-invalid-username", tags=["register"], step=1)
def register_invalid_username(c):
    for name in ("Has Space", "bad!char", "", "no:colon", "sharp#"):
        c.req("POST", f"{CS}/register",
              json_body={"username": name, "password": "pw-123456",
                         "auth": {"type": "m.login.dummy"}},
              label=f"register invalid username {name!r}")


@scenario("cs/register-inhibit-login", tags=["register"], step=1)
def register_inhibit_login(c):
    body = {"username": "inh1", "password": "pw-inh1-123", "inhibit_login": True,
            "auth": {"type": "m.login.dummy"}}
    # `access_token` and `device_id` are optional properties of the response,
    # so a comparison of the body alone would let a server that logged the user
    # in anyway pass: their *absence* is the whole point of the parameter, and
    # absence is what is derived here.
    c.req("POST", f"{CS}/register", json_body=body, label="register inhibit_login",
          derive=lambda b: {"has_user_id": bool((b or {}).get("user_id")),
                            "has_access_token": "access_token" in (b or {}),
                            "has_device_id": "device_id" in (b or {})})
    # And the account really was created: it can be logged into.
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "inh1"},
                     "password": "pw-inh1-123"},
          label="the inhibited registration still created the account",
          derive=lambda b: {"has_access_token": bool((b or {}).get("access_token"))})


@scenario("cs/register-guest-disabled", tags=["register"], step=1)
def register_guest(c):
    c.req("POST", f"{CS}/register", query={"kind": "guest"}, json_body={},
          label="guest registration while guests are disabled")


@scenario("cs/whoami", tags=["login"], step=1)
def whoami(c):
    u = register(c, "who1")
    c.req("GET", f"{CS}/account/whoami", token=u.token, label="whoami")
    c.req("GET", f"{CS}/account/whoami", token="syt_not_a_real_token",
          label="whoami with an invalid token")
    c.req("GET", f"{CS}/account/whoami", label="whoami with no token")


@scenario("cs/login-password", tags=["login"], step=1)
def login_password(c):
    u = register(c, "log1")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "log1"},
                     "password": u.password},
          label="login m.id.user")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": u.user_id},
                     "password": u.password},
          label="login with a full user id")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password", "user": "log1",
                     "password": u.password},
          label="login with the legacy user field")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "log1"},
                     "password": "wrong-password"},
          label="login with a wrong password")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "nosuchuser"},
                     "password": "whatever"},
          label="login as an unknown user")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.sso",
                     "identifier": {"type": "m.id.user", "user": "log1"}},
          label="login with an unsupported type")


@scenario("cs/logout", tags=["login", "devices"], step=1)
def logout(c):
    u = register(c, "out1")
    t2 = login(c, u)
    c.req("POST", f"{CS}/logout", token=u.token, json_body={}, label="logout")
    c.req("GET", f"{CS}/account/whoami", token=u.token,
          label="whoami after logout")
    c.req("GET", f"{CS}/account/whoami", token=t2,
          label="the other device still works")
    c.req("POST", f"{CS}/logout/all", token=t2, json_body={}, label="logout all")
    c.req("GET", f"{CS}/account/whoami", token=t2, label="whoami after logout all")


@scenario("cs/devices", tags=["devices"], step=1)
def devices(c):
    u = register(c, "dev1", device_id="DEVONE", display_name="first device")
    login(c, u, device_id="DEVTWO")
    c.req("GET", f"{CS}/devices", token=u.token, label="list devices")
    c.req("GET", f"{CS}/devices/DEVONE", token=u.token, label="get one device")
    c.req("PUT", f"{CS}/devices/DEVONE", token=u.token,
          json_body={"display_name": "renamed"}, label="rename device")
    c.req("GET", f"{CS}/devices/DEVONE", token=u.token, label="device after rename")
    c.req("GET", f"{CS}/devices/NOSUCHDEVICE", token=u.token,
          label="unknown device")


@scenario("cs/device-delete-uia", tags=["devices", "uia"], step=1)
def device_delete(c):
    u = register(c, "dd1", device_id="DELME")
    login(c, u, device_id="KEEPME")
    r = c.req("DELETE", f"{CS}/devices/DELME", token=u.token, json_body={},
              label="delete device: UIA challenge",
              derive=uia_challenge("m.login.password"))
    session = (r.json or {}).get("session")
    c.req("DELETE", f"{CS}/devices/DELME", token=u.token,
          json_body={"auth": {"type": "m.login.password", "session": session,
                              "identifier": {"type": "m.id.user", "user": "dd1"},
                              "password": u.password}},
          label="delete device: complete password auth")
    c.req("GET", f"{CS}/devices/DELME", token=u.token, label="device is gone")


@scenario("cs/device-delete-wrong-password", tags=["devices", "uia"], step=1)
def device_delete_bad(c):
    u = register(c, "dd2", device_id="DELME2")
    r = c.req("DELETE", f"{CS}/devices/DELME2", token=u.token, json_body={},
              check=False)
    session = (r.json or {}).get("session")
    c.req("DELETE", f"{CS}/devices/DELME2", token=u.token,
          json_body={"auth": {"type": "m.login.password", "session": session,
                              "identifier": {"type": "m.id.user", "user": "dd2"},
                              "password": "not-the-password"}},
          # A failed attempt is challenged again, and the challenge carries a
          # session and a flow the client can complete.  How many flows a
          # server offers is its own business -- this server supports two login
          # types -- so the list itself is not compared.
          drop=("flows",),
          label="delete device with a wrong password",
          derive=lambda b: {
              "a_session_to_retry_against": bool(b.get("session")),
              "offers_a_password_flow":
                  ["m.login.password"] in [f.get("stages")
                                           for f in (b.get("flows") or [])],
              "nothing_completed_yet": not b.get("completed")})


@scenario("cs/change-password", tags=["account", "uia"], step=1)
def change_password(c):
    u = register(c, "pw1")
    other = login(c, u)
    r = c.req("POST", f"{CS}/account/password",
              token=u.token, json_body={"new_password": "brand-new-pw-1"},
              label="change password: UIA challenge",
              derive=uia_challenge("m.login.password"))
    session = (r.json or {}).get("session")
    c.req("POST", f"{CS}/account/password", token=u.token,
          json_body={"new_password": "brand-new-pw-1",
                     "auth": {"type": "m.login.password", "session": session,
                              "identifier": {"type": "m.id.user", "user": "pw1"},
                              "password": u.password}},
          label="change password")
    c.req("GET", f"{CS}/account/whoami", token=other,
          label="other device logged out by the password change")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "pw1"},
                     "password": u.password},
          label="old password no longer works")
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "pw1"},
                     "password": "brand-new-pw-1"},
          label="new password works")


@scenario("cs/deactivate", tags=["account", "uia"], step=1)
def deactivate(c):
    u = register(c, "dz1")
    r = c.req("POST", f"{CS}/account/deactivate", token=u.token, json_body={},
              label="deactivate: UIA challenge",
              derive=uia_challenge("m.login.password"))
    session = (r.json or {}).get("session")
    c.req("POST", f"{CS}/account/deactivate", token=u.token,
          json_body={"auth": {"type": "m.login.password", "session": session,
                              "identifier": {"type": "m.id.user", "user": "dz1"},
                              "password": u.password}},
          label="deactivate",
          drop=("id_server_unbind_result",))
    # The release names `M_USER_DEACTIVATED` for this, the recording reference
    # answers `M_FORBIDDEN`, and a second independent homeserver answers
    # `M_USER_DEACTIVATED`.  Two conforming servers differ, so only the refusal
    # is scored.
    c.req("POST", f"{CS}/login",
          json_body={"type": "m.login.password",
                     "identifier": {"type": "m.id.user", "user": "dz1"},
                     "password": u.password},
          label="login as a deactivated user", drop=("errcode",))
    c.req("GET", f"{CS}/register/available", query={"username": "dz1"},
          label="a deactivated localpart is not available")


@scenario("cs/profile", tags=["profile"], step=1)
def profile(c):
    u = register(c, "prof1", profile_name=None)
    v = register(c, "prof2")
    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/displayname", token=u.token,
          json_body={"displayname": "Prof One"}, label="set displayname")
    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/avatar_url", token=u.token,
          json_body={"avatar_url": "mxc://example.org/SomeMediaId"},
          label="set avatar_url")
    c.req("GET", f"{CS}/profile/{q(u.user_id)}", label="full profile")
    c.req("GET", f"{CS}/profile/{q(u.user_id)}/displayname", label="get displayname")
    c.req("GET", f"{CS}/profile/{q(u.user_id)}/avatar_url", label="get avatar_url")
    c.req("PUT", f"{CS}/profile/{q(u.user_id)}/displayname", token=v.token,
          json_body={"displayname": "hijack"}, label="set another user's profile")
    c.req("GET", f"{CS}/profile/{q('@nosuchuser:' + c.server_name)}",
          label="profile of an unknown local user")
