"""The behaviour corpus: every request whose response is scored.

A *scenario* is one browsing session -- a fresh cookie jar, an optional login,
and an ordered list of requests. Scenarios run in the order they appear here and
are never reordered, because some of them write to the database and later ones
observe what earlier ones wrote. Running the whole list against a freshly
started container is therefore a deterministic function of the container image.

Each request is scored on its status line, its `Location:` header and the bytes
of its body. Nothing is normalised except the PHP session id (see httpdrv).

`adversarial=True` marks a scenario whose inputs are chosen to break a migration
rather than to exercise a feature: collation-sensitive comparisons, PAD SPACE,
LIKE metacharacters, quoting, `LOWER()` over non-ASCII, character sets, NUL
bytes, and the numeric SQL injection the application ships on purpose.

One thing is deliberately absent: `/jform` and `/user/captcha`. Both render
AutoForm/AutoList element ids, which are bare runs of 2-10 random lowercase
letters -- unnormalisable without rewriting ordinary page text. Neither reads
nor writes application data.

`/mode/contest/user/signup` is present but costs one normalisation. It stores
`jf::$Security->RandomToken()` as the *name* of a jf_options row and then calls
mail(), which fails in the container; the token never reaches the response, so
only the database snapshot sees it, and random_cells() rewrites it there.

Inputs that MySQL cannot represent are absent too, and for a sharper reason.
The `utf8` character set of MySQL 5.x holds at most three bytes per character
and `sql_mode` here is not strict, so a four-byte character (an emoji) or an
over-length value is silently *truncated* on the way in. PostgreSQL stores it.
Asking a migration to reproduce silent truncation would be asking it to port a
defect, so no corpus input relies on it; three-byte characters, which both
databases hold exactly, carry the Unicode coverage instead.
"""

import re

# --------------------------------------------------------------- accounts ---
# Passwords of the accounts the corpus logs in as. The first three are created
# by the application's own installer (app/control/init.php); the rest come from
# the frozen seed, whose generator names them `awkward-password-<n>` or, for the
# firstname.lastname accounts, `pw-<firstname>-<lastname>`.
GUEST = ("guest", "guest")
WORKSHOP_ADMIN = ("workshop.admin", "Admin!234")
CONTEST_ADMIN = ("contest.admin", "Admin!234")
SEEDED_USER = ("alice.adams", "pw-alice-adams")
SEEDED_NONASCII = (u"mårten", "awkward-password-401")
SEEDED_CJK = (u"日本語", "awkward-password-408")

# The seed gives this account both `workshop_user` and `contest_user`, and
# neither `workshop` nor `contest`. It is therefore the only identity that can
# tell the two halves of the permission tree apart: it may read the challenges
# of both modes and is redirected away from both admin dashboards.
SEEDED_MEMBER = ("alice.lindqvist", "pw-alice-lindqvist")

# The highest jf_users.ID present in the frozen seed. Rows above it were created
# by the corpus, and their Password/Salt are random by construction -- see
# masked_columns().
SEED_MAX_USER_ID = 444


# -------------------------------------------------------------- scenarios ---

class Req(object):
    """One scored request."""

    __slots__ = ("method", "path", "data", "note")

    def __init__(self, method, path, data=None, note=""):
        self.method = method
        self.path = path
        self.data = data
        self.note = note


def GET(path, note=""):
    return Req("GET", path, None, note)


def POST(path, data, note=""):
    return Req("POST", path, data, note)


class Scenario(object):
    """One cookie jar. A login, where a scenario needs one, is itself a
    scored request rather than a fixture-time side arrangement."""

    __slots__ = ("name", "adversarial", "reqs")

    def __init__(self, name, reqs, adversarial=False):
        self.name = name
        self.reqs = reqs
        self.adversarial = adversarial


def _login_reqs(creds):
    """A login is itself scored: it is the widest single query in the app."""
    return [GET("/user/login"),
            POST("/user/login", {"Username": creds[0], "Password": creds[1]})]


# Usernames the corpus creates. Each one is chosen for what it does to a
# comparison, and each is representable in MySQL utf8 and in PostgreSQL alike.
NEW_PLAIN = "newuser.plain"
NEW_UPPER = "NEWUSER.PLAIN"          # LOWER() collision with the row above
NEW_ACCENT = u"renée.dupré"
NEW_SHARP_S = u"große.straße"   # LOWER('SS') != LOWER(U+00DF)
NEW_DOTTED_I = u"İzmir"         # LOWER(U+0130): one code point, or two?
NEW_DOTLESS_I = u"ızmir"        # its dotless twin
NEW_GREEK = u"Σοφία"   # final-sigma folding
NEW_PERCENT = "pct%user"
NEW_UNDERSCORE = "und_score"
NEW_BACKSLASH = "back\\slash2"
NEW_QUOTE = "o'brien2"
NEW_SPACES = "van der berg 2"

WS_PLAIN = "ws.member"
WS_UPPER = "WS.MEMBER"
WS_ACCENT = u"zöe.müller"
WS_CJK = u"山田"

# Accounts the contest signup form creates. Signup writes a jf_xuser row beside
# the jf_users row, which /user/create does not, so these are the only corpus
# accounts that exercise the extended user manager's INSERT.
SIGNUP_PLAIN = "signup.plain"
SIGNUP_ACCENT = u"signup.josé"

PW = "Corpus!Pass1"
PW2 = "Corpus!Pass2"

# The flags of the challenges seeded into contest 6, the one active under the
# frozen clock. contest_challenges.CorrectFlag holds md5 of these, which is what
# ModeContestAjaxChallenge writes and what ContestSubmissions::evaluate()
# compares against, so submitting one of them takes the completed branch.
FLAG_NUMERIC = "flag{0c67e9f4c137a463ce17cf26}"
FLAG_XSS2 = "flag{762b14f460efa373a75c5e77}"
FLAG_ACM = "flag{e56cbad2b6880b2278788f2e}"
FLAG_BLAC = "flag{5aef2f68c06205efaede629b}"


SCENARIOS = [

    # -- public pages ----------------------------------------------------
    Scenario("public-pages", [
        GET("/"),
        GET("/about"),
        GET("/main"),
        GET("/user/login"),
        GET("/user/login?return=/main"),
        GET("/user/create"),
    ]),

    Scenario("public-not-found", [
        GET("/no/such/controller"),
        GET("/about/extra/segments"),
        GET("/main?x=1&y=2"),
    ]),

    # Every page is gated; an anonymous visitor must be bounced identically.
    Scenario("anonymous-is-redirected", [
        GET("/mode/single/challenges/HTTPBasics/"),
        GET("/mode/single/challenges/NumericSQLInjection/"),
        GET("/mode/workshop/admin"),
        GET("/mode/contest/home"),
        GET("/mode/contest/admin"),
    ]),

    # The JSON endpoints answer rather than redirect, so their unauthorised
    # branch is observable on its own.
    Scenario("anonymous-json-endpoints", [
        GET("/mode/workshop/user/get"),
        POST("/mode/workshop/user/create", {"username": WS_PLAIN, "password": PW}),
        POST("/mode/workshop/user/delete", {"username": WS_PLAIN}),
    ]),

    # -- authentication --------------------------------------------------
    Scenario("login-guest", _login_reqs(GUEST) + [
        GET("/main"),
        GET("/about"),
    ]),

    Scenario("login-seeded-user", _login_reqs(SEEDED_USER) + [
        GET("/main"),
    ]),

    Scenario("login-then-logout", _login_reqs(GUEST) + [
        GET("/main"),
        GET("/user/logout"),
        GET("/main"),
        GET("/mode/single/challenges/HTTPBasics/"),
    ]),

    Scenario("login-wrong-password", [
        GET("/user/login"),
        POST("/user/login", {"Username": "guest", "Password": "not-the-password"}),
        POST("/user/login", {"Username": "guest", "Password": ""}),
        GET("/main"),
    ]),

    Scenario("login-unknown-user", [
        GET("/user/login"),
        POST("/user/login", {"Username": "no.such.person", "Password": "x"}),
        POST("/user/login", {"Username": "", "Password": ""}),
    ]),

    # ValidateUserCredentials matches on LOWER(Username)=LOWER(?), so login is
    # case-insensitive even though the column collates utf8_bin. Which spelling
    # of a non-ASCII name LOWER() folds to is a property of the database, not of
    # the application.
    Scenario("login-case-folding", [
        GET("/user/login"),
        POST("/user/login", {"Username": "GUEST", "Password": "guest"}),
        GET("/main"),
        GET("/user/logout"),
        POST("/user/login", {"Username": "GuEsT", "Password": "guest"}),
        GET("/main"),
    ], adversarial=True),

    Scenario("login-nonascii-case-folding", [
        GET("/user/login"),
        POST("/user/login", {"Username": SEEDED_NONASCII[0],
                             "Password": SEEDED_NONASCII[1]}),
        GET("/main"),
        GET("/user/logout"),
        POST("/user/login", {"Username": u"MÅRTEN",
                             "Password": SEEDED_NONASCII[1]}),
        GET("/main"),
        GET("/user/logout"),
        POST("/user/login", {"Username": SEEDED_CJK[0], "Password": SEEDED_CJK[1]}),
        GET("/main"),
    ], adversarial=True),

    # utf8_bin does not pad; PostgreSQL text does not pad either. A trailing
    # space must fail to log in on both, and a leading one likewise.
    Scenario("login-padding", [
        GET("/user/login"),
        POST("/user/login", {"Username": "guest ", "Password": "guest"}),
        POST("/user/login", {"Username": " guest", "Password": "guest"}),
        POST("/user/login", {"Username": "guest\t", "Password": "guest"}),
        POST("/user/login", {"Username": "guest", "Password": "guest "}),
        GET("/main"),
    ], adversarial=True),

    # The lookup is a prepared statement, so none of these may ever do anything
    # but fail to match. They are here because a migration that rebuilds the
    # query by hand -- or switches the driver's escaping -- makes exactly this
    # class of input behave differently.
    Scenario("login-quoting", [
        GET("/user/login"),
        POST("/user/login", {"Username": "' OR '1'='1", "Password": "x"}),
        POST("/user/login", {"Username": "guest'--", "Password": "x"}),
        POST("/user/login", {"Username": "guest\\", "Password": "x"}),
        POST("/user/login", {"Username": "gu%st", "Password": "x"}),
        POST("/user/login", {"Username": "gu_st", "Password": "x"}),
        POST("/user/login", {"Username": 'guest"', "Password": "x"}),
        POST("/user/login", {"Username": "back\\slash", "Password": "awkward-password-419"}),
        POST("/user/login", {"Username": "percent%user", "Password": "awkward-password-417"}),
        GET("/main"),
    ], adversarial=True),

    Scenario("login-existing-metachar-names", [
        GET("/user/login"),
        POST("/user/login", {"Username": "o'brien", "Password": "awkward-password-415"}),
        GET("/main"),
        GET("/user/logout"),
        POST("/user/login", {"Username": "under_score", "Password": "awkward-password-418"}),
        GET("/main"),
    ], adversarial=True),

    # -- user creation (writes) ------------------------------------------
    # /user/create echoes the new row's auto-increment id, so a sequence that
    # was not carried over shows up in the response body immediately.
    Scenario("create-user", [
        GET("/user/create"),
        POST("/user/create", {"username": NEW_PLAIN, "password": PW}),
        POST("/user/login", {"Username": NEW_PLAIN, "Password": PW}),
        GET("/main"),
    ]),

    Scenario("create-user-duplicate", [
        POST("/user/create", {"username": NEW_PLAIN, "password": PW}),
        POST("/user/create", {"username": NEW_UPPER, "password": PW}),
        POST("/user/create", {"username": "guest", "password": PW}),
        POST("/user/create", {"username": "GUEST", "password": PW}),
    ], adversarial=True),

    Scenario("create-user-missing-fields", [
        POST("/user/create", {"username": "", "password": PW}),
        POST("/user/create", {"username": "onlyname"}),
        POST("/user/create", {"password": PW}),
    ], adversarial=True),

    Scenario("create-user-unicode", [
        POST("/user/create", {"username": NEW_ACCENT, "password": PW}),
        POST("/user/login", {"Username": NEW_ACCENT, "Password": PW}),
        GET("/main"),
        GET("/user/logout"),
        POST("/user/create", {"username": NEW_SHARP_S, "password": PW}),
        POST("/user/create", {"username": NEW_GREEK, "password": PW}),
        POST("/user/login", {"Username": NEW_GREEK, "Password": PW}),
        GET("/main"),
    ], adversarial=True),

    # U+0130 lowercases to one code point under some rules and to two under
    # others; its dotless twin is a separate row either way.
    Scenario("create-user-turkish-i", [
        POST("/user/create", {"username": NEW_DOTTED_I, "password": PW}),
        POST("/user/create", {"username": NEW_DOTLESS_I, "password": PW}),
        POST("/user/create", {"username": "izmir", "password": PW}),
        POST("/user/create", {"username": "IZMIR", "password": PW}),
    ], adversarial=True),

    Scenario("create-user-metacharacters", [
        POST("/user/create", {"username": NEW_PERCENT, "password": PW}),
        POST("/user/create", {"username": NEW_UNDERSCORE, "password": PW}),
        POST("/user/create", {"username": NEW_BACKSLASH, "password": PW}),
        POST("/user/create", {"username": NEW_QUOTE, "password": PW}),
        POST("/user/create", {"username": NEW_SPACES, "password": PW}),
        POST("/user/login", {"Username": NEW_BACKSLASH, "Password": PW}),
        GET("/main"),
        GET("/user/logout"),
        POST("/user/login", {"Username": NEW_QUOTE, "Password": PW}),
        GET("/main"),
    ], adversarial=True),

    # Latin-1 range, three-byte range, and the CP1252-only characters that are
    # the reason a latin1 column cannot be read as ISO-8859-1.
    Scenario("create-user-cp1252-range", [
        POST("/user/create", {"username": u"euro€sign", "password": PW}),
        POST("/user/create", {"username": u"dash–user", "password": PW}),
        POST("/user/create", {"username": u"quote“user”", "password": PW}),
        POST("/user/login", {"Username": u"euro€sign", "Password": PW}),
        GET("/main"),
    ], adversarial=True),

    # -- lessons: numeric SQL injection ----------------------------------
    Scenario("lesson-numeric-sqli", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/NumericSQLInjection/"),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "101"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "7"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "16"}),
    ]),

    # The lesson concatenates its parameter into the SQL text on purpose. These
    # payloads are written to parse identically in both dialects; a payload that
    # only one of them accepts would be testing the dialect, not the migration.
    Scenario("lesson-numeric-sqli-injection", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "101 OR 1 = 1"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "0 OR station > 12"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "7 OR state = 'NY'"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "0 OR name LIKE 'S%'"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "0 OR name = 'seattle'"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "0 OR state = 'ny '"}),
    ], adversarial=True),

    # display_errors is Off, so a query the server rejects renders an empty
    # table rather than a dialect-specific error message. Both databases must
    # therefore agree on the page even though they disagree on the diagnosis.
    Scenario("lesson-numeric-sqli-malformed", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "101'"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": ""}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "not-a-number"}),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "1; DROP TABLE lesson_XSS2_messages"}),
    ], adversarial=True),

    # -- lessons: access control matrix ----------------------------------
    # The lesson joins a latin1 table to the RBAC nested set: getUserId() looks
    # the name up in a latin1_swedish_ci column, and the id it returns indexes
    # jf_rbac_*. `resource` is a permission title -- a role title there raises,
    # and the controller renders its catch branch instead, which is the
    # behaviour the last request in the adversarial scenario below pins down.
    Scenario("lesson-access-control-matrix", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/AccessControlMatrix/"),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Larry Stooge", "resource": "public_share"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Larry Stooge", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Moe Stooge", "resource": "performance_review"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Moe Stooge", "resource": "site_manager"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Curly Stooge", "resource": "account_manager"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Eric Walker", "resource": "time_card_entry"}),
    ]),

    # latin1_swedish_ci folds case and pads with spaces, so the first three of
    # these resolve to the same row as "Larry Stooge" and the page names the
    # role they inherit. The two double-encoded names are stored as the UTF-8
    # bytes of an accented name inside a latin1 column, so the spelling that
    # matches them is the mojibake one, not the repaired one.
    Scenario("lesson-acm-collation", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "LARRY STOOGE", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "larry stooge", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Larry Stooge  ", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": u"RenÃ©e DuprÃ©", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": u"Renée Dupré", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": u"JÃƒÂ¼rgen SchÃƒÂ¤fer", "resource": "site_manager"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": u"Seán O’Brien", "resource": "public_share"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Nobody At All", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Larry Stooge", "resource": "no_such_permission"}),
    ], adversarial=True),

    # -- lessons: business layer access control --------------------------
    # validateLogin() compares the password against a latin1_swedish_ci column
    # too, so the case and the padding of a *password* are load-bearing here.
    Scenario("lesson-blac", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/BusinessLayerAccessControl/"),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "8", "password": "tom"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "View"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "Logout"}),
    ]),

    Scenario("lesson-blac-collation", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "10", "password": "DAVID"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "Logout"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "10", "password": "david "}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "Logout"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "10", "password": "davi"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "99", "password": "david"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "", "password": ""}),
    ], adversarial=True),

    # showScreen3 renders every column of one row, so a View is the widest
    # single look the corpus gets at latin1 text: two mojibake names, a street,
    # a city. The Delete after it is the lesson's only DELETE, and the screen
    # that follows lists what is left.
    Scenario("lesson-blac-view-delete", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "12", "password": "anais"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "View", "user": "13"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "View", "user": "12"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "Delete", "user": "12"}),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "Delete", "user": "9"}),
        GET("/mode/single/challenges/BusinessLayerAccessControl/"),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"action": "Logout"}),
        GET("/mode/single/challenges/BusinessLayerAccessControl/"),
    ]),

    # -- lessons: stored XSS (writes) ------------------------------------
    Scenario("lesson-xss2-read", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/XSS2/"),
    ]),

    Scenario("lesson-xss2-write", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": "a plain corpus message"}),
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": "<script>alert('xss')</script>"}),
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": "quote ' and double \" and back \\ slash"}),
        GET("/mode/single/challenges/XSS2/"),
    ]),

    # The message column is latin1. Characters outside it, and characters that
    # are in CP1252 but not in ISO-8859-1, are what a wrongly-declared source
    # encoding destroys.
    Scenario("lesson-xss2-encoding", _login_reqs(GUEST) + [
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": u"latin-1 range: café naïve ßraße"}),
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": u"cp1252 only: € – “ ” Œ ™"}),
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": u"outside latin-1: 日本語 Δελτα"}),
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": b"raw bytes: \xc3 \xa9 \x81 \x8d \x9d"}),
        GET("/mode/single/challenges/XSS2/"),
    ], adversarial=True),

    # -- lessons with no database behind them ----------------------------
    Scenario("lessons-static", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/WebGoatIntro/"),
        GET("/mode/single/challenges/HTTPBasics/"),
        GET("/mode/single/challenges/HiddenFields/"),
        GET("/mode/single/challenges/HTMLClues/"),
        GET("/mode/single/challenges/LogSpoofing/"),
        GET("/mode/single/challenges/ForcedBrowsing/"),
        GET("/mode/single/challenges/NoSuchLesson/"),
    ]),

    # SingleModeController takes the lesson name with
    # `stristr($relativePath, "/", true)`, so a URL without a trailing slash has
    # no lesson name at all and every one of these falls into the same catch
    # branch. That is the application's real behaviour and the migration must
    # keep it: the branch still runs LessonScanner and still reads
    # jf_options.categoryLessons, so it is a database path, not a 404.
    Scenario("lesson-routing-no-trailing-slash", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/NumericSQLInjection"),
        GET("/mode/single/challenges/XSS2"),
        GET("/mode/single/challenges/NoSuchLesson"),
        GET("/mode/single/challenges/"),
        GET("/mode/single/challenges/NumericSQLInjection/index"),
    ], adversarial=True),

    # -- workshop mode: RBAC-gated CRUD ----------------------------------
    Scenario("workshop-admin-view", _login_reqs(WORKSHOP_ADMIN) + [
        GET("/mode/workshop/admin"),
        GET("/mode/workshop/user/get"),
    ]),

    Scenario("workshop-user-create", _login_reqs(WORKSHOP_ADMIN) + [
        POST("/mode/workshop/user/create", {"username": WS_PLAIN, "password": PW}),
        GET("/mode/workshop/user/get"),
        POST("/mode/workshop/user/create", {"username": WS_ACCENT, "password": PW}),
        POST("/mode/workshop/user/create", {"username": WS_CJK, "password": PW}),
        GET("/mode/workshop/user/get"),
    ]),

    Scenario("workshop-user-create-rejected", _login_reqs(WORKSHOP_ADMIN) + [
        POST("/mode/workshop/user/create", {"username": WS_PLAIN, "password": PW}),
        POST("/mode/workshop/user/create", {"username": WS_UPPER, "password": PW}),
        POST("/mode/workshop/user/create", {"username": "", "password": PW}),
        POST("/mode/workshop/user/create", {"username": WS_PLAIN, "password": ""}),
        POST("/mode/workshop/user/create", {"username": WS_PLAIN}),
        POST("/mode/workshop/user/create", {"password": PW}),
    ], adversarial=True),

    # The created account must be able to log in, which is the only check that
    # the random salt written alongside it survived the move intact.
    Scenario("workshop-user-can-log-in", [
        GET("/user/login"),
        POST("/user/login", {"Username": WS_PLAIN, "Password": PW}),
        GET("/main"),
        GET("/mode/workshop/admin"),
        GET("/user/logout"),
        POST("/user/login", {"Username": WS_ACCENT, "Password": PW}),
        GET("/main"),
    ]),

    Scenario("workshop-user-delete", _login_reqs(WORKSHOP_ADMIN) + [
        POST("/mode/workshop/user/delete", {"username": WS_CJK}),
        GET("/mode/workshop/user/get"),
        POST("/mode/workshop/user/delete", {"username": WS_CJK}),
        POST("/mode/workshop/user/delete", {"username": WS_UPPER}),
        POST("/mode/workshop/user/delete", {"username": ""}),
        POST("/mode/workshop/user/delete", {}),
        GET("/mode/workshop/user/get"),
    ], adversarial=True),

    Scenario("workshop-forbidden-to-guest", _login_reqs(GUEST) + [
        GET("/mode/workshop/admin"),
        GET("/mode/workshop/user/get"),
        POST("/mode/workshop/user/create", {"username": "guest.sneaks.in", "password": PW}),
        POST("/mode/workshop/user/delete", {"username": WS_PLAIN}),
    ], adversarial=True),

    # -- contest mode ----------------------------------------------------
    Scenario("contest-admin-view", _login_reqs(CONTEST_ADMIN) + [
        GET("/mode/contest/admin"),
        GET("/mode/contest/home"),
    ]),

    Scenario("contest-forbidden-to-guest", _login_reqs(GUEST) + [
        GET("/mode/contest/admin"),
        GET("/mode/contest/home"),
    ], adversarial=True),

    # -- workshop mode: the lessons ---------------------------------------
    # The same four lesson classes as single mode, reached through a different
    # controller and a different permission. WorkshopModeController reads
    # jf_options.hiddenWorkshopLessons on the way in, which single mode does not.
    Scenario("workshop-lessons", _login_reqs(WORKSHOP_ADMIN) + [
        GET("/mode/workshop/challenges/NumericSQLInjection/"),
        POST("/mode/workshop/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "13"}),
        GET("/mode/workshop/challenges/XSS2/"),
        GET("/mode/workshop/challenges/BusinessLayerAccessControl/"),
        GET("/mode/workshop/challenges/AccessControlMatrix/"),
        GET("/mode/workshop/challenges/HTTPBasics/"),
        GET("/mode/workshop/challenges/NoSuchLesson/"),
    ]),

    # `hide` appends to a serialized array in jf_options and `show` removes from
    # it by position, so the two are not inverses: hiding twice leaves two
    # copies and showing once leaves one. Both states are read back.
    Scenario("workshop-hide-and-show", _login_reqs(WORKSHOP_ADMIN) + [
        POST("/mode/workshop/admin", {"hide": "XSS2"}),
        GET("/mode/workshop/challenges/XSS2/"),
        GET("/mode/workshop/admin"),
        POST("/mode/workshop/admin", {"hide": "XSS2"}),
        POST("/mode/workshop/admin", {"hide": u"NoSuchLesson—ü"}),
        GET("/mode/workshop/admin"),
        POST("/mode/workshop/admin", {"show": "XSS2"}),
        GET("/mode/workshop/challenges/XSS2/"),
        POST("/mode/workshop/admin", {"show": "XSS2"}),
        POST("/mode/workshop/admin", {"show": "NeverHidden"}),
        POST("/mode/workshop/admin", {"show": u"NoSuchLesson—ü"}),
        GET("/mode/workshop/challenges/XSS2/"),
        GET("/mode/workshop/admin"),
    ], adversarial=True),

    Scenario("workshop-lesson-reset", _login_reqs(WORKSHOP_ADMIN) + [
        GET("/mode/workshop/challenges/XSS2/reset/"),
        GET("/mode/workshop/challenges/XSS2/"),
        POST("/mode/workshop/challenges/XSS2/",
             {"submit": "Post", "message": u"workshop reset — ünïcode"}),
        GET("/mode/workshop/challenges/XSS2/"),
    ], adversarial=True),

    # -- contest mode: the member's half of the permission tree ------------
    # `contest` and `workshop` are parents of `view_contest_chal` and
    # `view_workshop_chal`, so an admin passes the challenge checks by
    # inheritance while a member fails the admin checks. Every request here is
    # the same URL one of the admin scenarios above already asked for, under an
    # identity that resolves the nested-set join differently.
    Scenario("member-sees-only-the-challenges", _login_reqs(SEEDED_MEMBER) + [
        GET("/mode/contest/home"),
        GET("/mode/contest/admin"),
        GET("/mode/workshop/admin"),
        GET("/mode/workshop/challenges/XSS2/"),
        GET("/mode/single/challenges/XSS2/"),
    ], adversarial=True),

    Scenario("contest-member-submits", _login_reqs(SEEDED_MEMBER) + [
        GET("/mode/contest/challenges/NumericSQLInjection_6"),
        POST("/mode/contest/challenges/NumericSQLInjection_6",
             {"submit": "Submit", "flag": FLAG_NUMERIC}),
        POST("/mode/contest/challenges/NumericSQLInjection_6",
             {"submit": "Submit", "flag": FLAG_NUMERIC}),
        GET("/mode/contest/challenges/XSS2_6"),
        POST("/mode/contest/challenges/XSS2_6",
             {"submit": "Submit", "flag": FLAG_XSS2}),
        GET("/mode/contest/challenges/BusinessLayerAccessControl_6"),
        POST("/mode/contest/challenges/BusinessLayerAccessControl_6",
             {"submit": "Submit", "flag": FLAG_BLAC}),
        GET("/mode/contest/home"),
    ]),

    # Wrong flags, a flag for the wrong challenge, and the collation questions:
    # CorrectFlag is compared with `=` in PHP after md5(), so case and padding
    # in the submitted flag have to survive the round trip byte for byte.
    Scenario("contest-member-submits-wrong", _login_reqs(SEEDED_MEMBER) + [
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": FLAG_ACM.upper()}),
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": FLAG_ACM + " "}),
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": FLAG_NUMERIC}),
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": ""}),
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": u"flag{éüñ}"}),
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": "' OR '1'='1"}),
        POST("/mode/contest/challenges/AccessControlMatrix_6",
             {"submit": "Submit", "flag": FLAG_ACM}),
        GET("/mode/contest/home"),
    ], adversarial=True),

    # ContestChallenges::getByName() takes the URL path verbatim, so these ask
    # the database for rows that are not there and then read element [0] of the
    # empty result. The warning that follows is part of the response.
    Scenario("contest-challenge-not-found", _login_reqs(CONTEST_ADMIN) + [
        GET("/mode/contest/challenges/NoSuchChallenge"),
        GET("/mode/contest/challenges/NumericSQLInjection"),
        GET("/mode/contest/challenges/numericsqlinjection_6"),
        GET("/mode/contest/challenges/NumericSQLInjection_6%20"),
        GET("/mode/contest/challenges/"),
    ], adversarial=True),

    # -- contest mode: the admin's writes ---------------------------------
    Scenario("contest-admin-adds-challenges", _login_reqs(CONTEST_ADMIN) + [
        POST("/mode/contest/ajax/challenge",
             {"challenge": "CorpusChallenge_6", "name": "Corpus challenge",
              "points": "310", "flag": "flag{corpus-added}"}),
        POST("/mode/contest/ajax/challenge",
             {"challenge": u"Épreuve_6", "name": u"Épreuve — «corpus»",
              "points": "320", "flag": u"flag{drapeau-ajouté}"}),
        GET("/mode/contest/home"),
        GET("/mode/contest/challenges/CorpusChallenge_6"),
        POST("/mode/contest/challenges/CorpusChallenge_6",
             {"submit": "Submit", "flag": "flag{corpus-added}"}),
        GET("/mode/contest/home"),
    ]),

    # Missing parameters fall out of ModeContestAjaxChallenge with no return
    # value at all, which the framework turns into its own 404 page.
    Scenario("contest-admin-adds-challenges-rejected", _login_reqs(CONTEST_ADMIN) + [
        POST("/mode/contest/ajax/challenge",
             {"challenge": "Incomplete_6", "name": "Incomplete"}),
        POST("/mode/contest/ajax/challenge", {}),
        POST("/mode/contest/ajax/challenge",
             {"challenge": "", "name": "", "points": "", "flag": ""}),
    ], adversarial=True),

    Scenario("contest-ajax-forbidden-to-member", _login_reqs(SEEDED_MEMBER) + [
        POST("/mode/contest/ajax/challenge",
             {"challenge": "Sneaked_6", "name": "Sneaked",
              "points": "999", "flag": "flag{sneaked}"}),
        GET("/mode/contest/home"),
    ], adversarial=True),

    # addContest() runs before the dashboard is built, so both of its outcomes
    # are observable: `start >= end` sets Error and inserts nothing, and a valid
    # window reaches ContestDetails::add(), which refuses while contest 6 is
    # still open. Neither leaves a row behind, which is what the reads show.
    Scenario("contest-admin-adds-contest", _login_reqs(CONTEST_ADMIN) + [
        POST("/mode/contest/admin",
             {"contest_submit": "1", "contest_name": "Backwards Window",
              "contest_admin": "contest.admin",
              "start_date": "2026-03-02 00:00:00",
              "end_date": "2026-03-01 00:00:00"}),
        POST("/mode/contest/admin",
             {"contest_submit": "1", "contest_name": "Same Instant",
              "contest_admin": "contest.admin",
              "start_date": "2026-03-01 00:00:00",
              "end_date": "2026-03-01 00:00:00"}),
        POST("/mode/contest/admin",
             {"contest_submit": "1", "contest_name": u"Concours d’été 2027",
              "contest_admin": u"contest.admin",
              "start_date": "2027-06-01 00:00:00",
              "end_date": "2027-06-08 00:00:00"}),
        GET("/mode/contest/home"),
    ], adversarial=True),

    # -- contest mode: signup and the extended user manager ----------------
    # The only path in the application that writes jf_xuser. mail() cannot
    # succeed in the container, so the successful branch ends on its own error
    # message -- but the account and its extended row are already committed,
    # and the login below is what shows they are usable.
    Scenario("contest-signup", [
        GET("/mode/contest/user/signup"),
        POST("/mode/contest/user/signup",
             {"Username": SIGNUP_PLAIN, "Password": PW, "Confirm": PW,
              "Email": "signup.plain@example.com"}),
        POST("/user/login", {"Username": SIGNUP_PLAIN, "Password": PW}),
        GET("/main"),
        GET("/mode/contest/home"),
    ]),

    Scenario("contest-signup-rejected", [
        POST("/mode/contest/user/signup",
             {"Username": "bad.email", "Password": PW, "Confirm": PW,
              "Email": "not-an-address"}),
        POST("/mode/contest/user/signup",
             {"Username": "bad.email", "Password": PW, "Confirm": PW,
              "Email": u"josé@example.com"}),
        POST("/mode/contest/user/signup",
             {"Username": SIGNUP_PLAIN, "Password": PW, "Confirm": PW,
              "Email": "signup.plain@example.com"}),
        POST("/mode/contest/user/signup",
             {"Username": SIGNUP_PLAIN.upper(), "Password": PW, "Confirm": PW,
              "Email": "signup.plain@example.com"}),
        POST("/mode/contest/user/signup",
             {"Username": SIGNUP_ACCENT, "Password": PW, "Confirm": PW2,
              "Email": "signup.accent@example.com"}),
        POST("/mode/contest/user/signup",
             {"Username": SIGNUP_ACCENT, "Password": PW, "Confirm": PW,
              "Email": "signup.accent@example.com"}),
        POST("/user/login", {"Username": SIGNUP_ACCENT, "Password": PW}),
        GET("/main"),
        GET("/mode/contest/user/signup?validate=deadbeef"),
        GET("/mode/contest/user/signup?validate="),
    ], adversarial=True),

    # ModeContestUserUpdate rewrites jf_users.Password and jf_users.Salt in
    # place. Logging in afterwards with each of the two passwords is the only
    # check that the new hash, and not just the row, survived the move.
    Scenario("contest-user-changes-password", [
        GET("/user/login"),
        POST("/user/login", {"Username": NEW_PLAIN, "Password": PW}),
        POST("/mode/contest/user/update",
             {"old_password": PW, "new_password": PW2, "cnew_password": "x"}),
        POST("/mode/contest/user/update",
             {"old_password": "wrong", "new_password": PW2,
              "cnew_password": PW2}),
        POST("/mode/contest/user/update",
             {"old_password": PW, "new_password": PW2, "cnew_password": PW2}),
        GET("/user/logout"),
        POST("/user/login", {"Username": NEW_PLAIN, "Password": PW}),
        POST("/user/login", {"Username": NEW_PLAIN, "Password": PW2}),
        GET("/main"),
    ], adversarial=True),

    # -- the pages again, after every write above ------------------------
    # Repeating the read-only pages at the end is not redundancy: it is the only
    # check that the writes in between left the rest of the application saying
    # exactly what it said before.
    Scenario("public-pages-after-writes", [
        GET("/"),
        GET("/about"),
        GET("/main"),
        GET("/user/login"),
        GET("/mode/workshop/user/get"),
    ]),

    Scenario("lessons-after-writes", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/NumericSQLInjection/"),
        GET("/mode/single/challenges/AccessControlMatrix/"),
        GET("/mode/single/challenges/BusinessLayerAccessControl/"),
        GET("/mode/single/challenges/XSS2/"),
    ]),

    Scenario("mode-dashboards-after-writes", _login_reqs(CONTEST_ADMIN) + [
        GET("/mode/contest/home"),
        GET("/mode/contest/admin"),
        GET("/user/logout"),
        POST("/user/login", {"Username": WORKSHOP_ADMIN[0],
                             "Password": WORKSHOP_ADMIN[1]}),
        GET("/mode/workshop/admin"),
        GET("/mode/workshop/user/get"),
    ]),

    # -- runtime DDL -----------------------------------------------------
    # Each lesson's reset() drops its table and recreates it from SQL held in
    # the PHP source: `int auto_increment primary key`, `DROP TABLE IF EXISTS`.
    # None of that text is in the schema dump, so a migration that converts only
    # the dump leaves these four statements speaking MySQL. AccessControlMatrix
    # goes further and rebuilds its slice of the RBAC nested set through the
    # framework. This runs last because it replaces seeded table contents, and
    # the reads after it are what show the replacement is identical on both
    # databases.
    Scenario("lesson-reset", _login_reqs(GUEST) + [
        GET("/mode/single/challenges/NumericSQLInjection/reset/"),
        GET("/mode/single/challenges/NumericSQLInjection/"),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "0 OR 1 = 1"}),
        GET("/mode/single/challenges/XSS2/reset/"),
        GET("/mode/single/challenges/XSS2/"),
        POST("/mode/single/challenges/XSS2/",
             {"submit": "Post", "message": u"after reset — ünïcode"}),
        GET("/mode/single/challenges/XSS2/"),
        GET("/mode/single/challenges/BusinessLayerAccessControl/reset/"),
        GET("/mode/single/challenges/BusinessLayerAccessControl/"),
        POST("/mode/single/challenges/BusinessLayerAccessControl/",
             {"user": "1", "password": "larry"}),
        GET("/mode/single/challenges/AccessControlMatrix/reset/"),
        GET("/mode/single/challenges/AccessControlMatrix/"),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Larry", "resource": "performance_review"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "LARRY", "resource": "time_card_entry"}),
        POST("/mode/single/challenges/AccessControlMatrix/",
             {"user": "Mark", "resource": "account_manager"}),
    ], adversarial=True),

    # The installer is a routed controller with no permission gate, and running
    # it a second time against an already-installed database is the hardest SQL
    # in the codebase. RBACManager::Add inserts into a nested set: it reads the
    # parent's Rght, widens every node to the right of it by two with
    # backtick-quoted identifiers, and does it under LOCK TABLES with an
    # explicit UNLOCK afterwards. Nothing else in the interface writes to the
    # permission tree, so without this scenario the whole of nestedset/full.php
    # is scored only by the migrated-ness gates and never executed at all.
    #
    # It runs last and it is deliberately destructive: the tree it leaves behind
    # holds two nodes for every title the installer knows, so the requests that
    # follow are asking whether RBACManager::Check -- a four-way join over two
    # self-joined nested sets -- still resolves a duplicated tree the same way
    # after the migration. The three accounts it tries to create already exist,
    # so CreateUser returns false and every Assign that follows is a no-op;
    # jf_users and jf_rbac_userroles are expected to come out unchanged.
    Scenario("installer-rerun", [
        GET("/init"),
    ] + _login_reqs(GUEST) + [
        GET("/mode/single"),
        GET("/mode/single/challenges/NumericSQLInjection/"),
        POST("/mode/single/challenges/NumericSQLInjection/",
             {"submit": "Go", "station": "101"}),
        GET("/user/logout"),
    ], adversarial=True),
]


# ----------------------------------------------------------------- state ----

def masked_columns():
    """Columns whose stored value is random by construction.

    jf_users.Password and jf_users.Salt are derived from a per-user random salt,
    so for any row the corpus itself created they differ between two runs of the
    same image. They are replaced by their length. Rows from the frozen seed
    keep their exact value, so a migration that mangles a password hash is still
    caught -- and the corpus logs in as every account it creates, which is what
    proves the new hashes are intact.

    Returns {table: (id_column, threshold, [columns])}.
    """
    return {
        "jf_users": ("ID", SEED_MAX_USER_ID, ["Password", "Salt"]),
    }


def random_cells():
    """Cells that carry a random token rather than data.

    The contest signup controller stores `jf::$Security->RandomToken()` as the
    *name* of the jf_options row that records a pending activation, so the row
    exists in every run but under a different name each time. The token is
    replaced by a constant; the rest of the name, the row's value -- the new
    account's id -- and every other row of the table are compared unchanged.

    Returns {table: [(column, pattern, replacement)]}.
    """
    return {
        "jf_options": [("Name",
                        re.compile(r"^activation_[0-9a-f]{64}\Z"),
                        "activation_<TOKEN>")],
    }


def counts():
    """(scenarios, requests, adversarial requests)."""
    reqs = sum(len(s.reqs) for s in SCENARIOS)
    adv = sum(len(s.reqs) for s in SCENARIOS if s.adversarial)
    return len(SCENARIOS), reqs, adv


def validate():
    """Structural invariants of the corpus, checked at import.

    *No scenario's first request may be a login POST.* A successful login calls
    `UserManager` -> `Session::RollSession()` -> `session_regenerate_id()`, and
    the stats plugin has already written its `jf_stats` row using the *previous*
    session id. When the jar already holds a cookie that previous id is one the
    driver has seen and can substitute out; when the login is the jar's first
    request, PHP minted the id during that same request and only the rolled id
    is ever sent back, so an unpredictable value lands in the snapshot. Two
    recordings of the same image then disagree on `jf_stats` -- which is how
    this was found: 405/405 requests identical, five rows differing, every one
    of them a `/user/login` immediately followed by `/main`.

    Fetching the form first is what a browser does anyway, so the invariant
    costs nothing and keeps the whole database snapshot a function of the
    interface. It is asserted structurally rather than by whether the login
    happens to succeed, because success depends on the collation under test.
    """
    for s in SCENARIOS:
        if not s.reqs:
            raise ValueError("scenario %r has no requests" % s.name)
        first = s.reqs[0]
        if first.method == "POST" and first.path == "/user/login":
            raise ValueError(
                "scenario %r opens with a login POST; put GET /user/login "
                "first so the pre-roll session id is observable" % s.name)
    names = [s.name for s in SCENARIOS]
    if len(set(names)) != len(names):
        raise ValueError("duplicate scenario name in the corpus")


validate()
