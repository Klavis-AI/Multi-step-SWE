"""Rehearses every check the suite relies on against a known answer.

Each block pairs a control that must pass with a mutation that must fail, so a
projector that silently stopped looking at something cannot report a clean
result.  Run before recording and after any change to cvh/.
"""

from __future__ import annotations

import contextlib
import copy
import io
import json
import os
import sys
import tempfile

from cvh import compare, norm, runner, scenarios

FAILURES = []
RAN = []

# A pass message a half-deleted file would print just as happily is not
# evidence, so the count is part of the output and the floor is asserted.
MIN_CHECKS = 192


def check(name, got, want):
    RAN.append(name)
    if got != want:
        FAILURES.append("%s\n    got:  %r\n    want: %r" % (name, got, want))


def raises(exc, fn, *args):
    """True if `fn(*args)` raised `exc`.  Anything else propagates."""
    try:
        fn(*args)
    except exc:
        return True
    return False


def check_differs(name, a, b):
    RAN.append(name)
    if a == b:
        FAILURES.append("%s: expected a difference, both were %r" % (name, a))


# --------------------------------------------------------------------------
# 1. reading order
# --------------------------------------------------------------------------

L = norm.Labeller()
p = norm.project_html("<p>Hello <b>world</b> again</p>", L)
check("text reading order", p["text"], "Hello world again")

p = norm.project_html("<ul><li>a</li><li>b</li><li>c</li></ul>", L)
check("list order", p["text"], "a b c")

p = norm.project_html(
    "<div>Left<span>Mid</span>Right</div><script>var x='NOPE';</script>", L)
check("script content dropped", p["text"], "Left Mid Right")

# The mutation: text that moved must change the projection.
a = norm.project_html("<p>one <b>two</b> three</p>", norm.Labeller())["text"]
b = norm.project_html("<p>one three <b>two</b></p>", norm.Labeller())["text"]
check_differs("reordered text is visible", a, b)


# --------------------------------------------------------------------------
# 2. forms: framework fields out, application fields in
# --------------------------------------------------------------------------

FORM = """
<form action="/subscription" method="post">
  <input type="hidden" name="utf8" value="&#x2713;">
  <input type="hidden" name="authenticity_token" value="RANDOMEVERYTIME">
  <input type="email" name="subscription[email]" value="">
  <input type="hidden" name="subscription[location_id]" value="7">
  <input type="submit" name="commit" value="Subscribe">
</form>
"""
L = norm.Labeller()
p = norm.project_html(FORM, L)
check("form count", len(p["forms"]), 1)
check("form action", p["forms"][0]["action"], "/subscription")
check("form method", p["forms"][0]["method"], "post")
check("form fields", p["forms"][0]["fields"],
      [["subscription[email]", "email", ""],
       ["subscription[location_id]", "hidden", "7"]])

# A fresh CSRF value must not move the projection ...
p2 = norm.project_html(FORM.replace("RANDOMEVERYTIME", "DIFFERENTTOKEN"),
                       norm.Labeller())
check("csrf value is not scored", p2["forms"][0]["fields"],
      p["forms"][0]["fields"])
# ... but a lost field must.
p3 = norm.project_html(
    FORM.replace('<input type="hidden" name="subscription[location_id]" value="7">', ""),
    norm.Labeller())
check_differs("dropped field is visible", p3["forms"][0]["fields"],
              p["forms"][0]["fields"])


# --------------------------------------------------------------------------
# 3. labelling: timestamps, tokens, digests
# --------------------------------------------------------------------------

# Timestamps the run itself produced collapse to one label, because the clock
# ticks and the second a value landed in is decided by how busy the box was.
L = norm.Labeller()
check("iso timestamp during the run", L.scrub("at 2016-01-19T12:00:03Z ok"),
      "at <NOW> ok")
check("a later second during the run is the same label",
      L.scrub("2016-01-19T12:00:09Z"), "<NOW>")
check("epoch during the run", L.scrub('{"updated":1453204800}'),
      '{"updated":<NOW>}')
check("rfc2822 during the run",
      L.scrub("Date: Tue, 19 Jan 2016 12:00:03 +0000"), "Date: <NOW>")

# The mutation that makes <NOW> worth having: a value outside the window is
# still indexed and still scored, so a stale or invented timestamp is caught.
L = norm.Labeller()
check("a timestamp before the run is indexed, not <NOW>",
      L.scrub("2016-01-18T12:00:00Z"), "<TS:0>")
check("a timestamp well after the run is indexed, not <NOW>",
      L.scrub("2016-01-21T12:00:00Z"), "<TS:1>")
check("seed data keeps its own label", L.scrub("2013-06-04"), "<TS:2>")
check("the same out-of-window value keeps its label",
      L.scrub("2016-01-18T12:00:00Z"), "<TS:0>")
check_differs("a stale timestamp does not pass as the current one",
              L.scrub("2016-01-18T12:00:00Z"), "<NOW>")
check("a bare clock carries no date, so it stays indexed",
      L.scrub("12:00:03"), "<TS:3>")

L = norm.Labeller()
tok = "AbCdEfGhIjKlMnOpQrStUv"
check("token labelled", L.scrub("/confirm?token=" + tok), "/confirm?token=<TOK:0>")
check("same token same label", L.scrub("&token=" + tok), "&token=<TOK:0>")
check_differs("different token different label",
              L.scrub("token=ZzYyXxWwVvUuTtSsRrQqPp"), "token=<TOK:0>")
# The mutation for the `token=` guard: the net must not fire where a token
# cannot appear.  "CityVoice_email_header" is 22 mixed-case characters and is
# the name of the header image the weekly digest links to, so a pattern without
# the prefix erases the only asset that mail carries.
check("22 mixed-case chars outside a token= context are kept",
      L.scrub("/images/CityVoice_email_header.png"),
      "/images/CityVoice_email_header.png")

L = norm.Labeller()
check("sprockets digest stripped",
      L.scrub("/assets/welcome-0123456789abcdef0123456789abcdef.mp3"),
      "/assets/welcome.mp3")
check("propshaft digest stripped",
      L.scrub("/assets/welcome-a1b2c3d4.mp3"), "/assets/welcome.mp3")
check("non-digest hyphen kept",
      L.scrub("/locations/1-eli-s-mile-high-club"),
      "/locations/1-eli-s-mile-high-club")

# A slug is exactly as long as a token and made of the same alphabet; only the
# case distinguishes them.  Both directions are checked, because getting this
# wrong in either direction silently destroys a whole surface.
L = norm.Labeller()
check("22-char lowercase slug is not a token",
      L.scrub("/locations/1-eli-s-mile-high-club"),
      "/locations/1-eli-s-mile-high-club")
check("mixed-case 22-char token is a token",
      L.scrub("?token=Ab3dEfGhIjKlMnOpQrStUv"), "?token=<TOK:0>")
check("21 chars is not a token",
      L.scrub("?token=Ab3dEfGhIjKlMnOpQrStU"), "?token=Ab3dEfGhIjKlMnOpQrStU")

# The mutation: a different *file* must survive digest stripping.
L = norm.Labeller()
check_differs("different asset still distinguishable",
              L.scrub("/assets/welcome-0123456789abcdef0123456789abcdef.mp3"),
              L.scrub("/assets/warning-0123456789abcdef0123456789abcdef.mp3"))

# --------------------------------------------------------------------------
# rake db:* projections
# --------------------------------------------------------------------------

L = norm.Labeller()
check("a migration timing is a duration",
      L.scrub("   -> 0.0271s"), "   -> <S>")
check("a word ending in s is not a duration",
      L.scrub("it lets 3 through"), "it lets 3 through")
check("a bare integer second is not matched",
      L.scrub("timeout 30s"), "timeout 30s")

# The two framings Rails has used for the same event, which must project the
# same: 4.0 omits the version from the banner, 5.0 onwards includes it.
RAILS40 = [
    "Migrating to CreateCalls (20140401145954)",
    "==  CreateCalls: migrating ====================================",
    "-- create_table(:calls)",
    "   -> <S>",
    "==  CreateCalls: migrated (<S>) ===============================",
]
RAILS50 = [
    "== 20140401145954 CreateCalls: migrating ======================",
    "-- create_table(:calls)",
    "   -> <S>",
    "== 20140401145954 CreateCalls: migrated (<S>) =================",
]
check("migration events are extracted in order",
      norm.project_migration(RAILS40)["events"],
      [["migrating", "CreateCalls"], ["op", "create_table(:calls)"],
       ["migrated", "CreateCalls"]])
check("the banner framing is not what is scored",
      norm.project_migration(RAILS50), norm.project_migration(RAILS40))
check("a reverted migration keeps its direction",
      norm.project_migration(
          ["==  RenameTable: reverting ===", "-- rename_table(:b, :a)",
           "==  RenameTable: reverted (<S>) ==="])["events"],
      [["reverting", "RenameTable"], ["op", "rename_table(:b, :a)"],
       ["reverted", "RenameTable"]])
check("a migration that did nothing projects to nothing",
      norm.project_migration([])["events"], [])

# The mutations: a lost operation, a lost migration and a swapped order all
# have to move the projection.
check_differs("a dropped DDL operation is visible",
              norm.project_migration(RAILS40),
              norm.project_migration([l for l in RAILS40
                                      if "create_table" not in l]))
check_differs("operation order is scored",
              norm.project_migration(["-- add_column(:a)", "-- add_index(:a)"]),
              norm.project_migration(["-- add_index(:a)", "-- add_column(:a)"]))

STATUS = [
    "database: cityvoice_production",
    "",
    " Status   Migration ID    Migration Name",
    "--------------------------------------------------",
    "   up     20130618185656  Create voice transcriptions",
    "  down    20140401145954  Create calls",
]
check("status rows are (status, version, name)",
      norm.project_migrate_status(STATUS)["rows"],
      [["up", "20130618185656", "Create voice transcriptions"],
       ["down", "20140401145954", "Create calls"]])
check("the status header is framing",
      norm.project_migrate_status(STATUS)["no_table"], False)
check("a missing schema_migrations table is a fact, not an empty list",
      norm.project_migrate_status(["Schema migrations table does not exist yet."]),
      {"rows": [], "no_table": True})
check_differs("a migration that stopped being applied is visible",
              norm.project_migrate_status(STATUS),
              norm.project_migrate_status(
                  [l.replace("   up  ", "  down") for l in STATUS]))

check("db:version reads the version",
      norm.project_db_version(["Current version: 20140401151212"]),
      {"version": "20140401151212"})
check("no version line is None, not a crash",
      norm.project_db_version(["rake aborted!"]), {"version": None})

# --------------------------------------------------------------------------
# database-server noise
# --------------------------------------------------------------------------

check("a PostgreSQL error keeps its class and loses its prose",
      norm.scrub_db_noise(
          "PG::StringDataRightTruncation: ERROR:  value too long for type "
          "character varying(255)"),
      "PG::StringDataRightTruncation: <DBERROR>")
check("a wrapped driver error keeps both classes",
      norm.scrub_db_noise(
          "ActiveRecord::StatementInvalid: PG::UndefinedTable: ERROR:  oops"),
      "ActiveRecord::StatementInvalid: PG::UndefinedTable: <DBERROR>")
check("generated SQL is not scored",
      norm.scrub_db_noise(
          ': INSERT INTO "locations" ("name") VALUES ($1) RETURNING "id"'),
      ": <SQL>")
# The application's own validation messages travel through the same pipe and
# must come out of it untouched -- collapsing them would delete the behaviour
# the importer cases exist to pin down.
check("an application validation message is not database noise",
      norm.scrub_db_noise(
          "ActiveRecord::RecordInvalid: Validation failed: Name can't be blank"),
      "ActiveRecord::RecordInvalid: Validation failed: Name can't be blank")
check("an importer message is not database noise",
      norm.scrub_db_noise("Line 2: Name can't be blank"),
      "Line 2: Name can't be blank")
check("a missing-column message is not database noise",
      norm.scrub_db_noise("Name column is missing"), "Name column is missing")
check_differs("two different database error classes stay different",
              norm.scrub_db_noise("PG::NotNullViolation: ERROR:  x"),
              norm.scrub_db_noise("PG::UniqueViolation: ERROR:  x"))

# Driver aliases win over the generic patterns and are applied longest-first.
L = norm.Labeller()
L.alias("SuperSecretAuthToken01", "<AUTHTOKEN:0>")
check("alias beats token pattern",
      L.scrub("token=SuperSecretAuthToken01"), "token=<AUTHTOKEN:0>")

# Rails' own timings reach the observation through rake stdout.  They are the
# one value on this box that genuinely cannot repeat, because the machine is
# shared, so they have to go before anything is compared.
L = norm.Labeller()
check("render timing scrubbed",
      L.scrub("  Rendered notification_mailer/weekly_activity.html.erb (91.2ms)"),
      "  Rendered notification_mailer/weekly_activity.html.erb (<MS>)")
check("delivery timing scrubbed",
      L.scrub("Sent mail to resident@example.com (101.5ms)"),
      "Sent mail to resident@example.com (<MS>)")
check("request timing scrubbed",
      L.scrub("Completed 200 OK in 15ms (Views: 10.2ms | ActiveRecord: 1.1ms)"),
      "Completed 200 OK in <MS> (Views: <MS> | ActiveRecord: <MS>)")
# The mutation: scrubbing the number must not scrub what was rendered.
check_differs("a different template is still distinguishable",
              L.scrub("Rendered locations/show.html.erb (9.1ms)"),
              L.scrub("Rendered locations/index.html.erb (9.1ms)"))
check("a bare number is not a duration",
      L.scrub("15 answers"), "15 answers")

# Rails 6.0 added two fields to those same two lines: an allocation count
# beside the render duration, and the Mail message id in place of the
# recipient.  Both vary run to run -- the count with the interpreter and with
# every gem on the render path, the id with the clock, the pid and the
# container's hostname -- so both have to go the way the duration goes.
check("allocation count scrubbed",
      L.scrub("  Rendered notification_mailer/weekly_activity.html.erb"
              " (Duration: 12.3ms | Allocations: 68652)"),
      "  Rendered notification_mailer/weekly_activity.html.erb"
      " (Duration: <MS> | Allocations: <ALLOC>)")
check("message id scrubbed",
      L.scrub("Delivered mail 569e254d41fbf_48e33a4-4c@fe36b002da5a.mail"
              " (101.5ms)"),
      "Delivered mail <MID> (<MS>)")
# Two runs of the same delivery differ in both values and in nothing else.
check("two runs of one delivery agree",
      L.scrub("Delivered mail 569e254caaac6_44633a4598c@fe36b002da5a.mail"
              " (98.2ms)"),
      L.scrub("Delivered mail 569e254a9cd2e_46533a484e1@0b11c2d3e4f5.mail"
              " (101.5ms)"))
check("two runs of one render agree",
      L.scrub("  Rendered x.html.erb (Duration: 1.0ms | Allocations: 68652)"),
      L.scrub("  Rendered x.html.erb (Duration: 9.9ms | Allocations: 70001)"))
# The mutations: neither rule may reach past the value it was written for.
check_differs("a different template is still distinguishable under 6.0",
              L.scrub("Rendered locations/show.html.erb"
                      " (Duration: 9.1ms | Allocations: 100)"),
              L.scrub("Rendered locations/index.html.erb"
                      " (Duration: 9.1ms | Allocations: 100)"))
check("the recipient on 5.x's line is not a message id",
      L.scrub("Sent mail to resident@example.com (101.5ms)"),
      "Sent mail to resident@example.com (<MS>)")
check("an address that is not a message id survives",
      L.scrub("From: CityVoice <no-reply@cityvoice.test>"),
      "From: CityVoice <no-reply@cityvoice.test>")
check("delivery prose without an id survives",
      L.scrub("Delivered mail to nobody"), "Delivered mail to nobody")

# The fixture directory belongs to the run, the basename belongs to the case.
L = norm.Labeller()
L.alias("/out/logs-full2/fixtures/", "<FIXTURES>/")
check("fixture directory scrubbed",
      L.scrub("import:locations[/out/logs-full2/fixtures/blank-name.csv]"),
      "import:locations[<FIXTURES>/blank-name.csv]")
check_differs("a different fixture is still distinguishable",
              L.scrub("import:locations[/out/logs-full2/fixtures/blank-name.csv]"),
              L.scrub("import:locations[/out/logs-full2/fixtures/utf8.csv]"))


# --------------------------------------------------------------------------
# 4. URLs
# --------------------------------------------------------------------------

L = norm.Labeller()
L.seed_hosts(["http://127.0.0.1:3000", "http://cityvoice.test"])
check("own host stripped", norm.norm_url("http://127.0.0.1:3000/calls/1/location", L),
      "/calls/1/location")
check("mail host stripped",
      norm.norm_url("http://cityvoice.test/subscription/confirm?token=x", L),
      "/subscription/confirm?token=x")
check("protocol-relative own host stripped",
      norm.norm_url("//127.0.0.1:3000/locations", L), "/locations")
check("relative untouched", norm.norm_url("/locations/2", L), "/locations/2")
check("bare own host becomes root", norm.norm_url("http://127.0.0.1:3000", L), "/")

# The mutation: host stripping is a whitelist, so a host the harness never
# bound is application content and has to stay scored.  CityVoice hardcodes
# raw.github.com in the weekly digest and codeforamerica.org in the footer;
# collapsing either to "/" hands a migration a free pass for dropping it.
check("foreign host kept",
      norm.norm_url("https://raw.github.com/daguar/x/CityVoice_email_header.png", L),
      "https://raw.github.com/daguar/x/CityVoice_email_header.png")
check("bare foreign host is not a root path",
      norm.norm_url("http://codeforamerica.org", L), "http://codeforamerica.org")
check_differs("two foreign hosts stay distinguishable",
              norm.norm_url("http://codeforamerica.org/a", L),
              norm.norm_url("http://example.org/a", L))
check("an unseeded labeller strips nothing",
      norm.norm_url("http://127.0.0.1:3000/x", norm.Labeller()),
      "http://127.0.0.1:3000/x")


# --------------------------------------------------------------------------
# 5. TwiML
# --------------------------------------------------------------------------

def seeded():
    """A labeller carrying the same origins the driver seeds per world."""
    lab = norm.Labeller()
    lab.seed_hosts(["http://127.0.0.1:3000", "http://cityvoice.test"])
    return lab


# The `asset_url` helper and `url_for` both emit an absolute URL into TwiML, so
# the verbs arrive carrying the harness's own port.  Scrubbing has to reach
# element text and attribute values, not just the Location header, or every
# golden is pinned to whichever port the recorder happened to bind.
TWIML = (
    '<?xml version="1.0" encoding="UTF-8"?>'
    '<Response>'
    '<Gather timeout="15" finishOnKey="" numDigits="3" '
    'action="http://127.0.0.1:3000/calls/1/location?attempts=1">'
    '<Play>http://127.0.0.1:3000'
    '/assets/welcome-0123456789abcdef0123456789abcdef.mp3</Play>'
    '<Play>http://127.0.0.1:3000'
    '/assets/location_prompt-0123456789abcdef0123456789abcdef.mp3</Play>'
    '</Gather>'
    '</Response>'
)
x = norm.project_xml(TWIML, seeded())
check("twiml root", x["root"]["tag"], "Response")
gather = x["root"]["children"][0]
check("gather attrs", gather["attrs"],
      {"action": "/calls/1/location?attempts=1", "finishOnKey": "",
       "numDigits": "3", "timeout": "15"})
check("play order and host",
      [c["text"] for c in gather["children"]],
      ["/assets/welcome.mp3", "/assets/location_prompt.mp3"])

# The mutation: a foreign host inside a verb is application content.
foreign = norm.project_xml(
    '<Response><Play>https://cdn.example.org/jingle.mp3</Play></Response>',
    seeded())
check("foreign host inside a verb is kept",
      foreign["root"]["children"][0]["text"],
      "https://cdn.example.org/jingle.mp3")

# The mutation: swapped prompts must not compare equal.
swapped = TWIML.replace("welcome-", "TMP-").replace(
    "location_prompt-", "welcome-").replace("TMP-", "location_prompt-")
check_differs("swapped prompts visible",
              json.dumps(norm.project_xml(TWIML, seeded()), sort_keys=True),
              json.dumps(norm.project_xml(swapped, seeded()), sort_keys=True))


# --------------------------------------------------------------------------
# 6. headers
# --------------------------------------------------------------------------

L = seeded()
r = norm.project_response(
    302,
    {"content-type": "text/html; charset=utf-8",
     "location": "http://127.0.0.1:3000/locations/1",
     "date": "Tue, 19 Jan 2016 12:00:00 GMT",
     "x-frame-options": "SAMEORIGIN",
     "set-cookie": "_automidnight_session=abc; path=/; HttpOnly",
     "x-runtime": "0.0123"},
    "", L)
check("kept headers", sorted(r["headers"]), ["content-type", "location"])
check("content type settled", r["headers"]["content-type"],
      "text/html; charset=utf-8")
check("location normalised", r["headers"]["location"], "/locations/1")

r2 = norm.project_response(
    302, {"content-type": "text/html;charset=UTF-8",
          "location": "http://elsewhere.example:80/locations/1"}, "", seeded())
check("content type spelling does not matter",
      r2["headers"]["content-type"], "text/html; charset=utf-8")
check("an off-site redirect keeps its host",
      r2["headers"]["location"], "http://elsewhere.example:80/locations/1")
check_differs("a changed redirect target is visible",
              r["headers"]["location"], "/locations/2")


# --------------------------------------------------------------------------
# 7. error page override
# --------------------------------------------------------------------------

L = norm.Labeller()
r = norm.project_response(404, {"content-type": "text/html; charset=utf-8"},
                          "<html><body><h1>Not found</h1></body></html>",
                          L, want="json")
check("error page projected as html", r["body"]["kind"], "html")
check("error heading", r["body"]["headings"], [["h1", "Not found"]])


# --------------------------------------------------------------------------
# 8. csv / json
# --------------------------------------------------------------------------

L = norm.Labeller()
c = norm.project_csv("Address,Total,Agree,Disagree\r\nEli's,3,2,1\r\n", L)
check("csv rows", c["rows"],
      [["Address", "Total", "Agree", "Disagree"], ["Eli's", "3", "2", "1"]])

L = norm.Labeller()
j = norm.project_json('{"b":2,"a":{"z":1,"y":"2016-01-19T12:00:00Z"}}', L)
check("json sorted and scrubbed", json.dumps(j["data"]),
      '{"a": {"y": "<NOW>", "z": 1}, "b": 2}')

# StatusController writes a bare `Time.now.to_i`, so the epoch arrives as a
# JSON *number*.  The control and its mutation: the current time collapses, a
# stale one does not.
L = norm.Labeller()
j = norm.project_json('{"status":"ok","updated":1453204812}', L)
check("json numeric epoch collapses", j["data"]["updated"], "<NOW>")
j = norm.project_json('{"status":"ok","updated":1451000000}', L)
check_differs("a stale numeric epoch does not collapse",
              j["data"]["updated"], "<NOW>")

j2 = norm.project_json("not json at all", norm.Labeller())
check("json parse error is recorded", j2["kind"], "json")
check("json parse error keeps raw", j2["raw"], "not json at all")


# --------------------------------------------------------------------------
# 8b. binary bodies
# --------------------------------------------------------------------------

AUDIO = b"ID3\x03\x00\x00\x00" + bytes(range(256)) * 40
r = norm.project_response(200, {"content-type": "audio/mpeg"},
                          AUDIO.decode("utf-8", "replace"), norm.Labeller(),
                          raw=AUDIO)
check("audio projected as binary", r["body"]["kind"], "binary")
check("audio length recorded", r["body"]["bytes"], len(AUDIO))
check_differs("a different audio file is visible",
              r["body"]["sha256"],
              norm.project_response(200, {"content-type": "audio/mpeg"}, "",
                                    norm.Labeller(),
                                    raw=AUDIO + b"!")["body"]["sha256"])

# send_data hands CSV back as application/octet-stream; an explicit `want`
# must still get the structured projection rather than a digest.
r = norm.project_response(200, {"content-type": "application/octet-stream"},
                          "a,b\n1,2\n", norm.Labeller(), want="csv",
                          raw=b"a,b\n1,2\n")
check("explicit want beats content type", r["body"]["kind"], "csv")


# --------------------------------------------------------------------------
# 9. the scorer itself
# --------------------------------------------------------------------------

GOLD = {"format": 1, "cases": [
    {"id": "w/a", "kind": "http", "obs": {"status": 200, "body": {"text": "x"}}},
    {"id": "w/b", "kind": "http", "obs": {"status": 404, "body": {"text": "y"}}},
    {"id": "w/c", "kind": "rake", "obs": {"exit_ok": True, "stdout": ["ok"]}},
    {"id": "w/d", "kind": "mail", "obs": {"count": 1}},
]}

same = copy.deepcopy(GOLD)
report = compare.score(GOLD, same)
check("identical scores 1.0", report["fraction"], 1.0)

broken = copy.deepcopy(GOLD)
broken["cases"][1]["obs"]["status"] = 500
report = compare.score(GOLD, broken)
check("one break scores 0.75", report["fraction"], 0.75)
check("the break is named",
      [r["id"] for r in report["results"] if not r["ok"]], ["w/b"])
check("the diff points at the field",
      report["results"][1]["diff"][0][0], ".status")

missing = copy.deepcopy(GOLD)
missing["cases"] = missing["cases"][:2]
report = compare.score(GOLD, missing)
check("a crashed world costs its cases", report["fraction"], 0.5)
check("missing is distinguished from wrong",
      sorted(r["reason"] for r in report["results"] if not r["ok"]),
      ["missing", "missing"])

extra = copy.deepcopy(GOLD)
extra["cases"].append({"id": "w/e", "kind": "http", "obs": {}})
report = compare.score(GOLD, extra)
check("extra cases do not raise the score", report["fraction"], 1.0)
check("extra cases are reported", report["extra"], ["w/e"])

report = compare.score(GOLD, broken, skip=["w/b"])
check("skip removes the case from the denominator", report["total"], 3)
check("skip restores the score", report["fraction"], 1.0)

# A list that changed length must be reported, not silently truncated.
g = {"format": 1, "cases": [{"id": "x", "obs": {"links": [["a", "A"], ["b", "B"]]}}]}
o = {"format": 1, "cases": [{"id": "x", "obs": {"links": [["a", "A"]]}}]}
report = compare.score(g, o)
check("shorter list fails", report["fraction"], 0.0)
check("length difference is reported",
      report["results"][0]["diff"][0][0], ".links[]")

# Stability detection, control and mutation.
check("stable recordings report nothing",
      compare.stability(GOLD, copy.deepcopy(GOLD)), [])
unstable = compare.stability(GOLD, broken)
check("unstable recordings are named", [c for c, _ in unstable], ["w/b"])

dropped = copy.deepcopy(GOLD)
dropped["cases"] = [c for c in dropped["cases"] if c["id"] != "w/b"]
unstable = compare.stability(GOLD, dropped)
check("a case that disappears is unstable, not absent",
      [c for c, _ in unstable], ["w/b"])
check("disappearance is reported as presence",
      unstable[0][1][0][0], "<presence>")

# `--stability` must exit non-zero when it found something, so that a shell
# `&&` cannot promote goldens from a pair that disagreed.
with tempfile.TemporaryDirectory() as tmp:
    a_path = os.path.join(tmp, "a.json")
    b_path = os.path.join(tmp, "b.json")
    with open(a_path, "w") as handle:
        json.dump(GOLD, handle)
    with open(b_path, "w") as handle:
        json.dump(GOLD, handle)
    with contextlib.redirect_stdout(io.StringIO()):
        rc_same = compare.main(["--stability", a_path, b_path])
        with open(b_path, "w") as handle:
            json.dump(broken, handle)
        rc_diff = compare.main(["--stability", a_path, b_path])
check("stability exits 0 when the pair agrees", rc_same, 0)
check("stability exits non-zero when it does not", rc_diff, 1)

# A duplicate id would otherwise collapse two cases into one: the count drops
# and the mean over the survivors goes up.
duped = copy.deepcopy(GOLD)
duped["cases"].append(copy.deepcopy(duped["cases"][0]))
check("a duplicate case id is an error",
      raises(compare.Duplicate, compare.index, duped), True)
check("a recording with no cases is an error",
      raises(compare.Duplicate, compare.index, {"format": 1, "cases": []}),
      True)
check("distinct ids index cleanly",
      sorted(compare.index(GOLD)), ["w/a", "w/b", "w/c", "w/d"])


# --------------------------------------------------------------------------
# 10. must-flip overlays
# --------------------------------------------------------------------------
#
# The overlay is how the documented-semantic-change exception is expressed,
# and it is the one place in the scorer where the expected answer is not the
# recorded one.  Every way it could quietly do nothing is a check here:
# a typo'd id, a re-recording that matches what it replaced, a missing citation,
# and -- the one that matters at step k -- an earlier step's flip not carrying
# forward, which would score an input against an answer this step's own
# requirements made unreachable.

FLIP_B = {
    "step": "01-x",
    "cases": {"w/b": {"obs": {"status": 200, "body": {"text": "y"}},
                      "why": "the framework stopped 404ing this",
                      "cite": "guides 4.2, section 1"}},
}

report = compare.score(GOLD, same, overlays=[FLIP_B])
check("an unflipped tree fails its must-flip case", report["fraction"], 0.75)
check("the failing case is the flipped one",
      [r["id"] for r in report["results"] if not r["ok"]], ["w/b"])
check("answering as the old framework did is reported as stale",
      report["flips"]["stale"], ["w/b"])
check("the must-flip total is counted", report["flips"]["total"], 1)
check("nothing flipped yet", report["flips"]["flipped"], 0)

flipped = copy.deepcopy(GOLD)
flipped["cases"][1]["obs"]["status"] = 200
report = compare.score(GOLD, flipped, overlays=[FLIP_B])
check("a flipped tree scores 1.0 with the overlay", report["fraction"], 1.0)
check("the flip is counted as flipped", report["flips"]["flipped"], 1)
check("a flipped tree is not stale", report["flips"]["stale"], [])
check("the same tree without the overlay fails",
      compare.score(GOLD, flipped)["fraction"], 0.75)

# Third-answer: neither the old recording nor the re-recording.
third = copy.deepcopy(GOLD)
third["cases"][1]["obs"]["status"] = 503
report = compare.score(GOLD, third, overlays=[FLIP_B])
check("a third answer fails the must-flip", report["fraction"], 0.75)
check("a third answer is not stale either", report["flips"]["stale"], [])

# Composition: step 2's verifier is handed 01 and 02, and both must hold.
FLIP_C = {
    "step": "02-x",
    "cases": {"w/c": {"obs": {"exit_ok": False, "stdout": ["ok"]},
                      "why": "the task now aborts", "cite": "guides 5.0"}},
}
both = copy.deepcopy(flipped)
both["cases"][2]["obs"]["exit_ok"] = False
report = compare.score(GOLD, both, overlays=[FLIP_B, FLIP_C])
check("composed overlays both apply", report["fraction"], 1.0)
check("composed overlays are both counted", report["flips"]["total"], 2)
check("dropping the earlier fragment loses its flip",
      compare.score(GOLD, both, overlays=[FLIP_C])["fraction"], 0.75)

# Restacking: a case one step flipped and a later step flips again.  This is
# the arrangement step 03 is in -- 22 of its 23 must-flip inputs were already
# moved by step 02 -- and it is the one where "still on the old answer" stops
# being a single sentence.  A candidate sitting on step 01's answer skipped
# this step; a candidate sitting on the recording's own answer skipped both,
# which usually means it was run against a tree that never had step 01 in it.
# The two are different findings and the report has to keep them apart.
RESTACK_1 = {
    "step": "01-x",
    "cases": {"w/b": {"obs": {"status": 200, "body": {"text": "y"}},
                      "why": "the framework stopped 404ing this",
                      "cite": "guides 4.2, section 1"}},
}
RESTACK_2 = {
    "step": "02-x",
    "cases": {"w/b": {"obs": {"status": 410, "body": {"text": "y"}},
                      "why": "and then started answering 410",
                      "cite": "guides 5.0, section 2"}},
}
STACK = [RESTACK_1, RESTACK_2]

applied, flips = compare.apply_overlays(GOLD, STACK)
check("the last fragment wins the answer",
      compare.index(applied)["w/b"]["obs"]["status"], 410)
check("the flip is attributed to the step that made it",
      flips["w/b"]["step"], "02-x")
check("`was` is what the step before it left",
      flips["w/b"]["was"]["status"], 200)
check("`was_step` names that step", flips["w/b"]["was_step"], "01-x")
check("`origin` is still the recording's own answer",
      flips["w/b"]["origin"]["status"], 404)

on_step_1 = copy.deepcopy(GOLD)
on_step_1["cases"][1]["obs"]["status"] = 200
report = compare.score(GOLD, on_step_1, overlays=STACK)
check("a tree left on the previous step's answer fails", report["fraction"], 0.75)
check("and is stale as that step, not as the baseline",
      report["flips"]["stale_by_step"], {"01-x": ["w/b"]})
# The pair is what the printed line uses, because the single label names the
# step that is *fine* -- 01-x's answer is on the screen and 02-x is the step
# whose flip went unmade.  Naming only the first reads like a regression in 01.
check("and the detail names the step that owes the flip as well",
      report["flips"]["stale_detail"],
      [{"owed_by": "02-x", "still_on": "01-x", "ids": ["w/b"]}])

on_baseline = copy.deepcopy(GOLD)
report = compare.score(GOLD, on_baseline, overlays=STACK)
check("a tree that never moved at all fails too", report["fraction"], 0.75)
check("and is stale as the baseline",
      report["flips"]["stale_by_step"], {"baseline": ["w/b"]})
check("with the same step still named as the one that owes the flip",
      report["flips"]["stale_detail"],
      [{"owed_by": "02-x", "still_on": "baseline", "ids": ["w/b"]}])

on_step_2 = copy.deepcopy(GOLD)
on_step_2["cases"][1]["obs"]["status"] = 410
report = compare.score(GOLD, on_step_2, overlays=STACK)
check("the restacked answer scores", report["fraction"], 1.0)
check("and nothing is stale", report["flips"]["stale_by_step"], {})
check("and the detail is empty rather than absent",
      report["flips"]["stale_detail"], [])
check("a restacked case is counted once, not twice",
      report["flips"]["total"], 1)

fourth = copy.deepcopy(GOLD)
fourth["cases"][1]["obs"]["status"] = 503
report = compare.score(GOLD, fourth, overlays=STACK)
check("an answer that is neither is not filed as either",
      [report["flips"]["stale"], report["flips"]["stale_by_step"],
       report["flips"]["stale_detail"]], [[], {}, []])

# A first flip reports `baseline` for both readings, since they are the same
# string -- the label must not become `None` or the printed line loses it.
report = compare.score(GOLD, copy.deepcopy(GOLD), overlays=[FLIP_B])
check("a single flip is stale as the baseline",
      report["flips"]["stale_by_step"], {"baseline": ["w/b"]})

# The three ways an overlay could be a no-op, each an error rather than a pass.
check("an overlay for an unrecorded case is an error",
      raises(compare.BadOverlay, compare.score, GOLD, same,
             (), [{"step": "z", "cases": {"w/zz": {"obs": {}, "why": "w",
                                                   "cite": "c"}}}]),
      True)
check("an overlay that re-records the same answer is an error",
      raises(compare.BadOverlay, compare.score, GOLD, same,
             (), [{"step": "z", "cases": {"w/b": {
                 "obs": GOLD["cases"][1]["obs"], "why": "w", "cite": "c"}}}]),
      True)
check("an overlay without a citation is an error",
      raises(compare.BadOverlay, compare.score, GOLD, same,
             (), [{"step": "z", "cases": {"w/b": {
                 "obs": {"status": 201}, "why": "w"}}}]),
      True)
check("an overlay without a reason is an error",
      raises(compare.BadOverlay, compare.score, GOLD, same,
             (), [{"step": "z", "cases": {"w/b": {
                 "obs": {"status": 201}, "cite": "c"}}}]),
      True)

# The golden the caller passed in must survive being amended, or a verifier
# that scores twice gets a different answer the second time.
_ = compare.score(GOLD, same, overlays=[FLIP_B])
check("applying an overlay does not mutate the golden",
      GOLD["cases"][1]["obs"]["status"], 404)
check("a second score after an overlay is unchanged",
      compare.score(GOLD, same)["fraction"], 1.0)


# --------------------------------------------------------------------------
# 11. a case from a world that never came up is not evidence
# --------------------------------------------------------------------------
#
# This is the rule that stops an observation from matching by accident.
# Scoring the untouched 4.0.0 tree with step 1's verifier -- a tree whose gems
# do not even install under that step's interpreter -- would otherwise come
# back 1/391 rather than 0/391, and the one case is
# `migrate/migrate.status_before`: "exited non-zero, printed no rows", which is
# what the 4.2 rake task does on an unmigrated database and *also* what
# `bundle exec` does when there is no bundle.  Two different worlds, one
# projection.
#
# The projection is right; the evidence is missing.  The runner records, per
# world, whether the application answered, so that flag decides whether the
# world's cases are evidence at all.  Checked here from both ends, because a
# rule that zeroed a live world would be far worse than the accident it
# prevents.

LIVE = {"empty": {"booted": True}, "w": {"booted": True}}
DEAD = {"empty": {"booted": True}, "w": {"booted": False}}

booted = copy.deepcopy(same)
booted["worlds"] = LIVE
check("a recording with no worlds map scores as before",
      compare.score(GOLD, same)["fraction"], 1.0)
check("a booted world scores exactly as before",
      compare.score(GOLD, booted)["fraction"], 1.0)
check("a booted world is not named dead",
      compare.score(GOLD, booted)["dead_worlds"], [])

dead = copy.deepcopy(same)
dead["worlds"] = DEAD
report = compare.score(GOLD, dead)
check("every case of a dead world fails though every obs matches",
      report["fraction"], 0.0)
check("the reason names the boot, not a difference",
      sorted({r["reason"] for r in report["results"]}), ["world-not-booted"])
check("the dead world is named", report["dead_worlds"], ["w"])
check("a dead world reports no spurious diff",
      [r["diff"] for r in report["results"]], [[], [], [], []])

# Only the dead one: a tree that loses one world keeps what the others earned.
MIXED_GOLD = copy.deepcopy(GOLD)
MIXED_GOLD["cases"][0]["id"] = "up/a"
MIXED_GOLD["cases"][1]["id"] = "up/b"
mixed = copy.deepcopy(MIXED_GOLD)
mixed["worlds"] = {"up": {"booted": True}, "w": {"booted": False}}
check("a live world beside a dead one keeps its cases",
      compare.score(MIXED_GOLD, mixed)["fraction"], 0.5)

# The accident itself, rehearsed: the observation matches, the world is dead.
accident = {"format": 1, "worlds": {"w": {"booted": False}},
            "cases": [{"id": "w/c", "kind": "rake",
                       "obs": {"exit_ok": True, "stdout": ["ok"]}}]}
check("the matching case of a dead world is not counted",
      [r["ok"] for r in compare.score(GOLD, accident)["results"]
       if r["id"] == "w/c"], [False])

# A must-flip in a dead world is not a flip, and -- the reporting half of the
# same defect -- a must-flip that never ran still counts toward the contract's
# size.  "must-flip 1/1" on a tree where three of four inputs never executed
# reads like a pass.
partial = {"format": 1, "worlds": {"w": {"booted": True}},
           "cases": [{"id": "w/a", "kind": "http",
                      "obs": {"status": 200, "body": {"text": "x"}}}]}
report = compare.score(GOLD, partial, overlays=[FLIP_B])
check("a missing must-flip still counts toward the total",
      report["flips"]["total"], 1)
check("a missing must-flip is not flipped", report["flips"]["flipped"], 0)
check("a missing must-flip is not called stale", report["flips"]["stale"], [])

deadflip = copy.deepcopy(same)
deadflip["cases"][1]["obs"]["status"] = 200
deadflip["worlds"] = DEAD
report = compare.score(GOLD, deadflip, overlays=[FLIP_B])
check("a flip in a dead world is not counted as flipped",
      report["flips"]["flipped"], 0)
check("a flip in a dead world still counts toward the total",
      report["flips"]["total"], 1)


# --------------------------------------------------------------------------
# 12. the environment application processes are given
# --------------------------------------------------------------------------
#
# Every one of these is a value that, if it drifted, would move a transcript
# without moving any behaviour -- which is the one failure this whole suite is
# built to make impossible.  HOME in particular: the recorder's Bundler 1.10
# says nothing about an unwritable HOME and a verifier's Bundler 1.17 prints
# "`/root` is not writable." onto the stdout of every rake task, which moves 47
# recorded cases between two runs of the same tree for no reason to do with
# the migration.

os.environ["HOME"] = "/root"
os.environ["PYTHONPATH"] = "/harness"
os.environ["CVH_SELFTEST_PASSTHROUGH"] = "kept"
E = runner.app_env()

check("HOME is not the invoking user's", E["HOME"], runner.HOME_DIR)
check("HOME is not /root", E["HOME"] == "/root", False)
check("the clock is preloaded", E["LD_PRELOAD"].endswith("libfaketime.so.1"),
      True)
check("the clock origin is the recorded one", E["FAKETIME"], runner.FAKE_CLOCK)
check("monotonic time is left alone", E["FAKETIME_DONT_FAKE_MONOTONIC"], "1")
check("timed waits keep their own deadline",
      E["FAKETIME_FORCE_MONOTONIC_FIX"], "0")
# The harness runs on CPython; an application subprocess that inherited
# PYTHONPATH would put the harness's own modules on a Ruby process's
# environment, and `python3` invoked from a rake task would import them.
check("PYTHONPATH does not reach the application", "PYTHONPATH" in E, False)
check("PYTHONHOME does not reach the application", "PYTHONHOME" in E, False)
check("unrelated environment is passed through",
      E["CVH_SELFTEST_PASSTHROUGH"], "kept")
check("the hooks of section 1.3 are present",
      [E["RAILS_ENV"], E["PORT"], E["APP_URL"], E["TZ"]],
      ["production", "3000", "cityvoice.test", "UTC"])
check("an explicit override wins",
      runner.app_env({"PORT": "4100"})["PORT"], "4100")
check("an override does not leak into the next call",
      runner.app_env()["PORT"], "3000")


# --------------------------------------------------------------------------
# 13. the hooks the instruction names are the hooks the harness supplies
# --------------------------------------------------------------------------
# Section 3 of the step instruction tells the candidate how their tree will be
# run: in production from the Procfile web command, with PORT from the
# environment, database.yml copied from the example, and the variables the
# application itself reads.  That list is a promise, and this check ties it to
# `runner.APP_ENV`: without it a hook added here could go unstated, and a hook
# named there could quietly stop being set.  Both directions are failures, and
# both are silent: the application just reads a variable that is not there.
#
# The list is mirrored rather than parsed out of the instruction because this
# file ships into `tests/` and the instruction does not.  Keeping the two in
# step is the point of the check; the mutation fixtures below are what make it
# an instrument rather than a copy.
INSTRUCTION_HOOKS = frozenset({
    "RAILS_ENV", "RACK_ENV",               # "started in production"
    "PORT",                                # "with PORT from the environment"
    "SECRET_TOKEN", "APP_URL",             # documented in `sample.env`
    "MAPBOX_MAP_ID", "GOOGLE_ANALYTICS_ID", "LOCK_CITYVOICE",
    "SENDGRID_USERNAME", "SENDGRID_PASSWORD",   # read by config/environment.rb
    "DISABLE_DATABASE_ENVIRONMENT_CHECK",
})

# The two the instruction names as set "wherever the lock is on".  They are not
# in `APP_ENV` -- they belong to the one world that turns the lock on -- so they
# are checked against that world's own table instead of the global one.  The
# instruction naming a variable nothing ever sets is the same silent failure as
# the other direction.
LOCK_HOOKS = frozenset({"CITYVOICE_LOCK_USERNAME", "CITYVOICE_LOCK_PASSWORD"})

# Locale and timezone are not hooks into the application: they are the
# conditions every recording was made under, and the instruction already says
# the tree must not depend on the clock.  They are carved out by name so that
# the carve-out cannot quietly grow.
LOCALE_HOOKS = frozenset({"LANG", "LC_ALL", "TZ"})


def unsupplied(table):
    """Hooks the instruction names that the harness does not set."""
    return sorted(INSTRUCTION_HOOKS - set(table))


def unstated(table):
    """Variables the harness sets that the instruction does not name."""
    return sorted(set(table) - INSTRUCTION_HOOKS - LOCALE_HOOKS)


check("every hook the instruction names is supplied",
      unsupplied(runner.APP_ENV), [])
check("every variable the harness sets is one the instruction names",
      unstated(runner.APP_ENV), [])
check("a hook dropped from the table is reported",
      unsupplied({k: v for k, v in runner.APP_ENV.items()
                  if k != "MAPBOX_MAP_ID"}),
      ["MAPBOX_MAP_ID"])
check("a variable the instruction does not name is reported",
      unstated(dict(runner.APP_ENV, CVH_NEW_KNOB="1")), ["CVH_NEW_KNOB"])
check("the locale carve-out is locale and nothing else",
      sorted(LOCALE_HOOKS), ["LANG", "LC_ALL", "TZ"])
check("the application is started in production, both spellings",
      [runner.APP_ENV["RAILS_ENV"], runner.APP_ENV["RACK_ENV"]],
      ["production", "production"])
# `sample.env` documents these two and the application reads them; an unset
# variable and a variable set to the empty string are different to Ruby, and
# the instruction says they are set.
check("the optional sample.env hooks are set, even when empty",
      [("GOOGLE_ANALYTICS_ID" in runner.APP_ENV,
        runner.APP_ENV["GOOGLE_ANALYTICS_ID"]),
       ("LOCK_CITYVOICE" in runner.APP_ENV,
        runner.APP_ENV["LOCK_CITYVOICE"])],
      [(True, ""), (True, "")])
check("the clock offered to the application is the repository's own era",
      runner.FAKE_CLOCK.startswith("@2016"), True)
_locked = [w for w in scenarios.WORLDS if "LOCK_CITYVOICE" in (w.env or {})]
check("exactly one world turns the HTTP-auth lock on", len(_locked), 1)
check("the lock credentials the instruction names are set where the lock is on",
      sorted(LOCK_HOOKS - set(_locked[0].env if _locked else {})), [])
check("a lock hook dropped from that world is reported",
      sorted(LOCK_HOOKS - {"CITYVOICE_LOCK_USERNAME"}),
      ["CITYVOICE_LOCK_PASSWORD"])


# --------------------------------------------------------------------------

if FAILURES:
    print("SELFTEST FAILED (%d of %d)" % (len(FAILURES), len(RAN)))
    for failure in FAILURES:
        print("  " + failure)
    sys.exit(1)
if len(RAN) < MIN_CHECKS:
    print("SELFTEST INCOMPLETE: %d checks ran, expected at least %d"
          % (len(RAN), MIN_CHECKS))
    sys.exit(1)
if len(set(RAN)) != len(RAN):
    dupes = sorted({n for n in RAN if RAN.count(n) > 1})
    print("SELFTEST AMBIGUOUS: duplicate check names %s" % dupes)
    sys.exit(1)
print("selftest ok: %d checks" % len(RAN))
