"""The behaviour suite: ten hermetic worlds over CityVoice's own surface.

Every input here is something the deployed application already accepts -- a
Twilio webhook, a browser request, a rake task the repository ships, an SMTP
conversation its mailer initiates, the HTTP dependency its status endpoint
polls.  Nothing is reached by loading application classes directly, so the
suite keeps working when Zeitwerk, strong parameters, or the Twilio gem's API
change underneath it.

Each world gets a fresh schema and a fresh app server, so the worlds cannot
order-couple.  Within a world, order matters and is deliberate: the IVR worlds
build the data that the read worlds then read back.
"""

from __future__ import annotations

from . import norm

# --------------------------------------------------------------------------
# constants the whole suite shares
# --------------------------------------------------------------------------

TWILIO_BASE = {
    "AccountSid": "AC00000000000000000000000000000001",
    "ApiVersion": "2010-04-01",
    "CallStatus": "in-progress",
    "Direction": "inbound",
}

SURVEY_LINE = "+15551230000"

LOCATIONS_CSV = (
    "Name,Lat,Long\n"
    "Eli's Mile High Club,37.826650,-122.269507\n"
    "Beer Revolution,37.797077,-122.276297\n"
    "Stork Club,37.813142,-122.268321\n"
)

QUESTIONS_CSV = (
    "Short Name,Feedback Type,Question Text\n"
    "property_outcome,numerical_response,Should this property be removed or "
    "repaired? Press 1 to remove or 2 to repair.\n"
    "property_comments,voice_file,Please leave some comments about this "
    "property. Do not leave self-identifying information.\n"
)

RECORDING = "http://api.twilio.com/2010-04-01/Accounts/AC1/Recordings/RE%02d"


class World:
    """A named scenario group with its own database and its own app server.

    `prepare` runs after the schema is reloaded and *before* the app server is
    started; `fn` runs against the running server.  The split exists because
    `db:drop` cannot take the database away from a unicorn that is holding
    connections to it, so a world that migrates from nothing has to do that
    part while nothing is listening.
    """

    def __init__(self, name, fn, env=None, prepare=None):
        self.name = name
        self.fn = fn
        self.env = env or {}
        self.prepare = prepare


# --------------------------------------------------------------------------
# shared helpers
# --------------------------------------------------------------------------

def call_params(sid, frm, **extra):
    params = dict(TWILIO_BASE)
    params["CallSid"] = "CA%032d" % sid
    params["From"] = frm
    params["To"] = SURVEY_LINE
    params.update(extra)
    return params


def redirect_target(obs):
    """The URL of the single <Redirect> verb in a TwiML body, or None."""
    body = obs.get("body") or {}
    root = body.get("root")
    if not root:
        return None
    for child in root.get("children", []):
        if child.get("tag") == "Redirect":
            return child.get("text")
    return None


def seed_from_repo(ctx, prefix="seed"):
    """Load the repository's own data/ fixtures through its own rake tasks."""
    ctx.rake("%s.locations" % prefix, "import:locations")
    ctx.rake("%s.questions" % prefix, "import:questions")


def run_ivr(ctx, tag, sid, phone, digits_location, answer_digits,
            recording_index, listen=False):
    """One complete inbound call, from `POST /calls` to the goodbye.

    Returns the call id the application assigned, read out of the Redirect it
    emits rather than assumed, so a change in id allocation shows up as a
    changed transcript instead of a 404 storm.
    """
    start = ctx.twilio("%s.start" % tag, "/calls", call_params(sid, phone))
    target = redirect_target(start)
    if not target or "/calls/" not in target:
        return None
    call_id = target.split("/calls/")[1].split("/")[0]

    ctx.twilio("%s.location_prompt" % tag, "/calls/%s/location" % call_id,
               call_params(sid, phone, attempts="0"))
    assigned = ctx.twilio(
        "%s.location_digits" % tag, "/calls/%s/location?attempts=1" % call_id,
        call_params(sid, phone, Digits=digits_location))

    if listen:
        ctx.twilio("%s.messages_prompt" % tag, "/calls/%s/messages" % call_id,
                   call_params(sid, phone, attempts="0"))
        ctx.twilio("%s.messages_listen" % tag,
                   "/calls/%s/messages?attempts=1" % call_id,
                   call_params(sid, phone, Digits="1"))
        ctx.twilio("%s.playback_0" % tag,
                   "/calls/%s/messages/0/playback" % call_id,
                   call_params(sid, phone, attempts="0"))
        ctx.twilio("%s.playback_0_next" % tag,
                   "/calls/%s/messages/0/playback?attempts=1" % call_id,
                   call_params(sid, phone, Digits="1"))
        ctx.twilio("%s.playback_1" % tag,
                   "/calls/%s/messages/1/playback" % call_id,
                   call_params(sid, phone, attempts="0"))
    else:
        ctx.twilio("%s.messages_prompt" % tag, "/calls/%s/messages" % call_id,
                   call_params(sid, phone, attempts="0"))
        ctx.twilio("%s.messages_skip" % tag,
                   "/calls/%s/messages?attempts=1" % call_id,
                   call_params(sid, phone, Digits="2"))

    ctx.twilio("%s.consent_prompt" % tag, "/calls/%s/consent" % call_id,
               call_params(sid, phone, attempts="0"))
    ctx.twilio("%s.consent_yes" % tag,
               "/calls/%s/consent?attempts=1" % call_id,
               call_params(sid, phone, Digits="1"))
    ctx.twilio("%s.questions" % tag, "/calls/%s/questions" % call_id,
               call_params(sid, phone))

    ctx.twilio("%s.q0_prompt" % tag,
               "/calls/%s/questions/0/answer" % call_id,
               call_params(sid, phone, attempts="0"))
    ctx.twilio("%s.q0_answer" % tag,
               "/calls/%s/questions/0/answer?attempts=1" % call_id,
               call_params(sid, phone, Digits=answer_digits))
    ctx.twilio("%s.q1_prompt" % tag,
               "/calls/%s/questions/1/answer" % call_id,
               call_params(sid, phone, attempts="0"))
    ctx.twilio("%s.q1_answer" % tag,
               "/calls/%s/questions/1/answer?attempts=1" % call_id,
               call_params(sid, phone,
                           RecordingUrl=RECORDING % recording_index,
                           RecordingDuration="17"))
    ctx.twilio("%s.q2_end" % tag,
               "/calls/%s/questions/2/answer" % call_id,
               call_params(sid, phone, attempts="0"))
    return call_id


def read_back(ctx, prefix):
    """Every read surface, in one block, so each world ends with a snapshot.

    `/locations` as HTML is deliberately NOT here.  This repository has an
    `index.json.jbuilder` and no `index.html.erb`, so that request is a 500 --
    a real recording, and one this step must preserve, but a 500 error page is
    the same bytes in every world.  Recorded once per world it would be eleven
    byte-identical copies of one observation: eleven cases that could only ever
    fail together, and eleven overlays for any later step that touches error
    rendering.  It is recorded once, in `world_empty`, where the missing
    template is the subject rather than a by-product; the index's *data* is
    read here as JSON, which is the projection that actually changes with the
    world.
    """
    ctx.get("%s.root" % prefix, "/")
    ctx.get("%s.locations_json" % prefix, "/locations.json", want="json")
    ctx.get("%s.location_1" % prefix, "/locations/1")
    ctx.get("%s.location_1_json" % prefix, "/locations/1.json", want="json")
    ctx.get("%s.location_2" % prefix, "/locations/2")
    ctx.get("%s.location_3" % prefix, "/locations/3")
    ctx.get("%s.voice_answers" % prefix, "/voice_answers")
    ctx.get("%s.numerical_answers" % prefix, "/numerical_answers")
    ctx.get("%s.numerical_answers_csv" % prefix, "/numerical_answers.csv",
            want="csv")
    ctx.get("%s.export_csv" % prefix, "/export.csv", want="csv")


# --------------------------------------------------------------------------
# world 1 -- an empty database
# --------------------------------------------------------------------------

def world_empty(ctx):
    """Nothing imported.  Every page must still render, and the reduce-over-
    empty paths (AnswerCounts, NumericalResponse#total_response_count) are
    exactly where a Ruby version bump shows up."""
    ctx.get("root", "/")
    # The one HTML recording of the locations index, kept on purpose.  This
    # repository ships `index.json.jbuilder` and no `index.html.erb`, so the
    # request is a 500 and the application's own 500 page is the answer.  That
    # is a real property of the tree and the step must preserve it -- adding
    # the missing template is a behaviour change nobody asked for, and it also
    # flips `http_edges/accept_nonsense`, which is a must-flip.  One recording
    # of it is enough; see `read_back` for why it is not recorded per world.
    ctx.get("locations", "/locations")
    ctx.get("locations_json", "/locations.json", want="json")
    ctx.get("voice_answers", "/voice_answers")
    ctx.get("numerical_answers", "/numerical_answers")
    ctx.get("numerical_answers_csv", "/numerical_answers.csv", want="csv")
    ctx.get("export_csv", "/export.csv", want="csv")

    # high_voltage's static pages.
    ctx.get("page_about", "/pages/about")
    ctx.get("page_contact", "/pages/contact")
    ctx.get("page_messages", "/pages/messages")
    with ctx.adversarial():
        ctx.get("page_missing", "/pages/no-such-page")
        ctx.get("page_traversal", "/pages/..%2F..%2Fconfig%2Fdatabase")

        # Not-found paths.
        ctx.get("location_missing", "/locations/1")
        ctx.get("location_zero", "/locations/0")
        ctx.get("no_route", "/this-route-does-not-exist")

    # Status with a live dependency but no data.
    ctx.get("status_empty_db", "/.well-known/status")

    # Subscription without the parameters it requires.
    with ctx.adversarial():
        ctx.post("subscribe_no_params", "/subscription")
        ctx.post("subscribe_empty_hash", "/subscription",
                 {"subscription[email]": ""})

    # Static files production.rb promises to serve itself.
    ctx.get("asset_welcome", "/assets/welcome.mp3")
    with ctx.adversarial():
        ctx.get("asset_missing", "/assets/does-not-exist.mp3")
    ctx.get("robots", "/robots.txt")


# --------------------------------------------------------------------------
# world 2 -- the repository's own seed data, read back
# --------------------------------------------------------------------------

def world_seeded(ctx):
    seed_from_repo(ctx)
    read_back(ctx, "after_seed")

    # to_param builds "<id>-<parameterized name>"; the application must keep
    # accepting the bare id and must not start caring about the slug.
    ctx.get("slug_correct", "/locations/1-eli-s-mile-high-club")
    ctx.get("slug_json", "/locations/2-beer-revolution.json", want="json")
    with ctx.adversarial():
        ctx.get("slug_wrong", "/locations/1-completely-wrong-slug")
        ctx.get("id_out_of_range", "/locations/99999")
        ctx.get("id_not_a_number", "/locations/not-a-number")
        ctx.get("id_negative", "/locations/-1")

    # Re-importing is not idempotent for locations (no uniqueness) but is for
    # questions (find_or_initialize_by short_name).  Both facts are scored.
    ctx.rake("reimport_questions", "import:questions")
    ctx.get("after_reimport_questions", "/numerical_answers")
    ctx.rake("reimport_locations", "import:locations")
    ctx.get("after_reimport_locations_json", "/locations.json", want="json")

    ctx.get("status_seeded", "/.well-known/status")


# --------------------------------------------------------------------------
# world 3 -- the IVR happy path, then the data it produced
# --------------------------------------------------------------------------

def world_ivr_happy(ctx):
    seed_from_repo(ctx)

    # Three callers: location 1 gets two answers, location 2 one, location 3
    # none.  Distinct totals matter -- AnswerCounts#to_array sorts by total and
    # Ruby's sort_by is not stable, so equal totals would make the page order
    # a coin flip rather than a fact.
    run_ivr(ctx, "call_a", 1, "+15105550101", "001", "1", 1)
    run_ivr(ctx, "call_b", 2, "+15105550102", "001", "1", 2)
    run_ivr(ctx, "call_c", 3, "+15105550103", "002", "2", 3)

    read_back(ctx, "after_calls")

    # A fourth caller listens to what the first three left before answering.
    run_ivr(ctx, "call_d", 4, "+15105550104", "001", "2", 4, listen=True)

    read_back(ctx, "after_playback_call")

    ctx.get("status_with_data", "/.well-known/status")


# --------------------------------------------------------------------------
# world 4 -- the IVR under abuse
# --------------------------------------------------------------------------

def world_ivr_adversarial(ctx):
    seed_from_repo(ctx)
    # Everything after the seed is an edge input, including the read-back:
    # what the pages look like once abuse has written rows is as much an edge
    # case as the abuse was.
    ctx.adv = True

    # A call that does not exist: load_call must answer with the fatal-error
    # recording rather than a 500 or a 404.
    ctx.twilio("no_such_call", "/calls/424242/location",
               call_params(9, "+15105559000", Digits="001", attempts="1"))
    ctx.twilio("call_id_not_numeric", "/calls/abc/location",
               call_params(9, "+15105559000", Digits="001", attempts="1"))

    # Attempt exhaustion.  Call::Attempt#last? is `current >= 2`, so the third
    # bad answer raises TwilioSessionError -- which subclasses Exception, not
    # StandardError, and is caught only because ActionController rescues
    # Exception.  That is exactly the kind of thing a version bump breaks.
    call_id = ctx.twilio("abuse.start", "/calls",
                         call_params(10, "+15105550110"))
    target = redirect_target(call_id)
    cid = target.split("/calls/")[1].split("/")[0] if target else "1"
    params = lambda **kw: call_params(10, "+15105550110", **kw)

    ctx.twilio("abuse.loc_attempt0", "/calls/%s/location" % cid,
               params(attempts="0"))
    ctx.twilio("abuse.loc_bad1", "/calls/%s/location?attempts=1" % cid,
               params(Digits="999"))
    ctx.twilio("abuse.loc_bad2", "/calls/%s/location?attempts=2" % cid,
               params(Digits="999"))
    ctx.twilio("abuse.loc_bad3", "/calls/%s/location?attempts=3" % cid,
               params(Digits="999"))
    ctx.twilio("abuse.loc_attempts_empty",
               "/calls/%s/location?attempts=" % cid, params(Digits="001"))
    ctx.twilio("abuse.loc_attempts_garbage",
               "/calls/%s/location?attempts=banana" % cid, params(Digits="001"))
    ctx.twilio("abuse.loc_attempts_negative",
               "/calls/%s/location?attempts=-5" % cid, params(Digits="001"))
    ctx.twilio("abuse.loc_digits_blank",
               "/calls/%s/location?attempts=1" % cid, params(Digits=""))
    ctx.twilio("abuse.loc_digits_unicode",
               "/calls/%s/location?attempts=1" % cid, params(Digits="00١"))
    ctx.twilio("abuse.loc_digits_sqlish",
               "/calls/%s/location?attempts=1" % cid,
               params(Digits="1 OR 1=1"))
    ctx.twilio("abuse.loc_ok", "/calls/%s/location?attempts=1" % cid,
               params(Digits="001"))

    # Playback with nothing recorded yet for that location is a different
    # template from playback that has run off the end of the list.
    ctx.twilio("abuse.messages_prompt", "/calls/%s/messages" % cid,
               params(attempts="0"))
    ctx.twilio("abuse.messages_bad", "/calls/%s/messages?attempts=1" % cid,
               params(Digits="7"))
    ctx.twilio("abuse.messages_listen", "/calls/%s/messages?attempts=1" % cid,
               params(Digits="1"))
    ctx.twilio("abuse.playback_empty", "/calls/%s/messages/0/playback" % cid,
               params(attempts="0"))
    ctx.twilio("abuse.playback_far", "/calls/%s/messages/99/playback" % cid,
               params(attempts="0"))
    ctx.twilio("abuse.playback_stop",
               "/calls/%s/messages/0/playback?attempts=1" % cid,
               params(Digits="2"))

    ctx.twilio("abuse.consent_bad1", "/calls/%s/consent?attempts=1" % cid,
               params(Digits="9"))
    ctx.twilio("abuse.consent_bad2", "/calls/%s/consent?attempts=2" % cid,
               params(Digits="9"))
    ctx.twilio("abuse.consent_no", "/calls/%s/consent?attempts=1" % cid,
               params(Digits="2"))

    # Answering the same question twice, the '#' repeat key, an out-of-range
    # question index, a numerical answer outside the inclusion list, and a
    # voice answer with no recording.
    ctx.twilio("abuse.questions", "/calls/%s/questions" % cid, params())
    ctx.twilio("abuse.q0_bad_digit",
               "/calls/%s/questions/0/answer?attempts=1" % cid,
               params(Digits="9"))
    ctx.twilio("abuse.q0_repeat",
               "/calls/%s/questions/0/answer?attempts=1" % cid,
               params(Digits="#"))
    ctx.twilio("abuse.q0_ok",
               "/calls/%s/questions/0/answer?attempts=1" % cid,
               params(Digits="1"))
    ctx.twilio("abuse.q0_again",
               "/calls/%s/questions/0/answer?attempts=1" % cid,
               params(Digits="2"))
    ctx.twilio("abuse.q1_no_recording",
               "/calls/%s/questions/1/answer?attempts=1" % cid, params())
    ctx.twilio("abuse.q1_empty_recording",
               "/calls/%s/questions/1/answer?attempts=1" % cid,
               params(RecordingUrl=""))
    ctx.twilio("abuse.q1_ok",
               "/calls/%s/questions/1/answer?attempts=1" % cid,
               params(RecordingUrl=RECORDING % 11, RecordingDuration="3"))
    ctx.twilio("abuse.q_index_huge",
               "/calls/%s/questions/99/answer" % cid, params(attempts="0"))
    ctx.twilio("abuse.q_index_negative",
               "/calls/%s/questions/-1/answer" % cid, params(attempts="0"))
    ctx.twilio("abuse.q_index_word",
               "/calls/%s/questions/zero/answer" % cid, params(attempts="0"))

    # A call with no From/To at all: Caller.find_or_create_by(nil) is legal
    # here, and the export page then has to render a caller with no number.
    ctx.twilio("abuse.call_no_params", "/calls", {})
    ctx.twilio("abuse.call_short_number", "/calls",
               dict(call_params(11, "+1"), **{}))
    ctx.twilio("abuse.call_long_number", "/calls",
               call_params(12, "+" + "9" * 40))

    # Wrong verbs and wrong CSRF handling on the Twilio surface.
    ctx.get("abuse.calls_get", "/calls")
    ctx.get("abuse.location_get", "/calls/%s/location" % cid)

    read_back(ctx, "after_abuse")


# --------------------------------------------------------------------------
# world 5 -- subscriptions and mail
# --------------------------------------------------------------------------

def world_subscriptions(ctx):
    seed_from_repo(ctx)

    page = ctx.get("sub.location_page", "/locations/1", cookies=True)
    token = ctx.csrf_from(page["_raw"])

    def form(email, location_id, **extra):
        body = {"subscription[email]": email,
                "subscription[location_id]": location_id}
        if token:
            body["authenticity_token"] = token
        body.update(extra)
        return body

    # Forgery protection is `with: :exception`; the Twilio controllers skip it
    # and this one must not.
    with ctx.adversarial():
        ctx.post("sub.no_csrf", "/subscription",
                 {"subscription[email]": "nocsrf@example.com",
                  "subscription[location_id]": "1"}, cookies=True)

    ctx.post("sub.create", "/subscription",
             form("resident@example.com", "1"), cookies=True)
    mail = ctx.mail("sub.confirmation_mail", count=1)

    # The token the mail carries is the token the row holds: the confirm page
    # has to accept it, and a neighbouring value must not work.
    real = ctx.tokens[0] if ctx.tokens else "missing"
    ctx.get("sub.confirm_ok", "/subscription/confirm?token=%s" % real)
    with ctx.adversarial():
        ctx.get("sub.confirm_twice", "/subscription/confirm?token=%s" % real)
        ctx.get("sub.confirm_wrong",
                "/subscription/confirm?token=%s" % ("A" * 22))
        ctx.get("sub.confirm_no_token", "/subscription/confirm")
        ctx.get("sub.confirm_blank_token", "/subscription/confirm?token=")
        # `token[]=` makes params[:token] an Array, and Rails 5 raises on a
        # nested-parameter type the query string did not promise.
        ctx.get("sub.confirm_array_token",
                "/subscription/confirm?token[]=%s" % real)

    ctx.get("sub.location_after_confirm", "/locations/1")

    # Rejected inputs.
    with ctx.adversarial():
        ctx.post("sub.bad_email", "/subscription",
                 form("not-an-email", "1"), cookies=True)
        ctx.post("sub.empty_email", "/subscription", form("", "1"),
                 cookies=True)
        ctx.post("sub.missing_location", "/subscription",
                 form("elsewhere@example.com", "99999"), cookies=True)
        ctx.post("sub.location_not_numeric", "/subscription",
                 form("elsewhere@example.com", "banana"), cookies=True)
        ctx.post("sub.extra_param", "/subscription",
                 form("injected@example.com", "1",
                      **{"subscription[confirmed]": "true",
                         "subscription[auth_token]": "chosen-by-the-caller"}),
                 cookies=True)
        mail_extra = ctx.mail("sub.extra_param_mail", count=1)

        # Strong parameters must reject the caller's chosen token: the mail
        # that went out has to carry a generated one.
        ctx.get("sub.forced_token_rejected",
                "/subscription/confirm?token=chosen-by-the-caller")

        # The same address subscribing twice, and to a second location.
        ctx.post("sub.same_again", "/subscription",
                 form("resident@example.com", "1"), cookies=True)
        ctx.mail("sub.same_again_mail", count=1)

    ctx.post("sub.second_location", "/subscription",
             form("resident@example.com", "2"), cookies=True)
    ctx.mail("sub.second_location_mail", count=1)

    # Case-insensitive uniqueness on Subscriber.
    with ctx.adversarial():
        ctx.post("sub.case_variant", "/subscription",
                 form("RESIDENT@example.com", "3"), cookies=True)
        ctx.mail("sub.case_variant_mail", count=1)

    # Weekly notifications: nothing to say yet, then something to say.
    with ctx.adversarial():
        ctx.rake("notify.quiet", "notifications:send")
        ctx.mail("notify.quiet_mail", count=0)

    run_ivr(ctx, "notify.call", 20, "+15105550120", "001", "1", 20)

    # `with_new_answers` is `answers.created_at >= last_email_sent_at`, and
    # `deliver` sets last_email_sent_at to the moment it delivers.  Whether a
    # second digest goes out therefore comes down to whether that delivery
    # landed in the same second as the answers -- which on a shared box is
    # decided by how busy the box was, not by the application.  Crossing a
    # second boundary first makes the delivery strictly later than the answers,
    # so `notify.again` finds nothing for a reason in the data.
    #
    # The exact `>=` boundary (an answer in the same second as the delivery) is
    # not scoreable and is deliberately not scored.  What is scored is the rule
    # either side of it: answers before a digest do not produce a second one,
    # and answers after a digest do.
    ctx.tick()
    ctx.rake("notify.after_answers", "notifications:send")
    ctx.mail("notify.after_answers_mail", count=1)
    ctx.tick()
    with ctx.adversarial():
        ctx.rake("notify.again", "notifications:send")
        ctx.mail("notify.again_mail", count=0)

    # New activity on the same location, after the digest went out.
    run_ivr(ctx, "notify.call2", 21, "+15105550121", "001", "2", 21)
    ctx.tick()
    ctx.rake("notify.new_activity", "notifications:send")
    ctx.mail("notify.new_activity_mail", count=1)

    # Unsubscribe, single and all.  The tokens are whatever the application
    # minted; how many it minted is recorded rather than branched on, so a
    # migration that stops minting them fails these cases instead of deleting
    # them.  A missing token is spelled out in full so the request is still
    # made and the application still answers.
    tokens = list(ctx.tokens)
    ctx.fact("unsub.tokens_seen", count=len(tokens))
    absent = "N" * 22
    first = tokens[0] if tokens else absent
    second = tokens[1] if len(tokens) > 1 else absent
    ctx.get("unsub.one", "/subscription/unsubscribe?token=%s" % first)
    with ctx.adversarial():
        ctx.get("unsub.one_again",
                "/subscription/unsubscribe?token=%s" % first)
    ctx.get("unsub.all", "/subscription/unsubscribe?token=%s&all=1" % second)
    with ctx.adversarial():
        ctx.get("unsub.bad_token",
                "/subscription/unsubscribe?token=%s" % ("Z" * 22))
        ctx.get("unsub.no_token", "/subscription/unsubscribe")

    with ctx.adversarial():
        ctx.rake("notify.after_unsub", "notifications:send")
        ctx.mail("notify.after_unsub_mail", count=0)

    read_back(ctx, "after_subscriptions")


# --------------------------------------------------------------------------
# world 6 -- the import tasks under malformed input
# --------------------------------------------------------------------------

def world_imports(ctx):
    def run(case, name, body, task="import:locations"):
        path = ctx.fixture(name, body)
        ctx.rake(case, "%s[%s]" % (task, path))

    run("loc.good", "good-locations.csv", LOCATIONS_CSV)
    # As JSON: this case exists to show that the good import landed, and the
    # HTML index is a 500 in every world (see `read_back`), which shows nothing
    # about what was imported.
    ctx.get("loc.good_read", "/locations.json", want="json")

    # Malformed CSV from here to the read-back: wrong headers, blank required
    # fields, encodings, line endings, a 4000-character name, a NUL byte, an
    # unterminated quote, and a path that is not there at all.
    ctx.adv = True

    run("loc.missing_name_header", "no-name.csv",
        "Lat,Long\n37.1,-122.1\n")
    run("loc.missing_all_headers", "no-headers.csv",
        "a,b,c\n1,2,3\n")
    run("loc.blank_name", "blank-name.csv",
        "Name,Lat,Long\n,37.1,-122.1\nSecond Place,37.2,-122.2\n")
    run("loc.two_bad_rows", "two-bad.csv",
        "Name,Lat,Long\n,37.1,-122.1\n,37.2,-122.2\n")
    run("loc.extra_column", "extra-col.csv",
        "Name,Lat,Long,Description\nWith Notes,37.3,-122.3,A description\n")

    # `Description` above is both mass-assignable and a real column, so it is
    # imported either way and distinguishes nothing.  `Notes` is neither: the
    # baseline's mass-assignment protection drops the key and the import
    # continues, while a tree that simply stops protecting hands the unknown
    # key to the model and the task aborts on it.  Both rows are recorded --
    # the second one is invalid for an unrelated reason (no name), which
    # reaches the same constructor without writing anything to the database.
    run("loc.unknown_column", "unknown-col.csv",
        "Name,Lat,Long,Notes\nUnknown Column Inn,37.31,-122.31,ignored\n")
    run("loc.unknown_column_invalid", "unknown-col-blank.csv",
        "Name,Lat,Long,Notes\n,37.32,-122.32,ignored\n")
    run("loc.quoted_comma", "quoted.csv",
        'Name,Lat,Long\n"Smith, Jones & Co",37.4,-122.4\n')
    run("loc.utf8", "utf8.csv",
        "Name,Lat,Long\nCafé Ångström 中文,37.5,-122.5\n")
    run("loc.crlf", "crlf.csv",
        "Name,Lat,Long\r\nCarriage Return Bar,37.6,-122.6\r\n")
    run("loc.bom", "bom.csv",
        "﻿Name,Lat,Long\nByte Order Mark Inn,37.7,-122.7\n")
    run("loc.header_only", "header-only.csv", "Name,Lat,Long\n")
    run("loc.empty", "empty.csv", "")
    run("loc.not_csv", "not-csv.csv", "this is not a csv at all\x00\x01\n")
    run("loc.unbalanced_quote", "unbalanced.csv",
        'Name,Lat,Long\n"Unclosed,37.8,-122.8\n')
    run("loc.huge_name", "huge-name.csv",
        "Name,Lat,Long\n%s,37.9,-122.9\n" % ("N" * 4000))
    run("loc.html_in_name", "html-name.csv",
        "Name,Lat,Long\n<script>alert(1)</script>,38.0,-122.0\n")
    ctx.rake("loc.no_such_file", "import:locations[/tmp/nope-does-not.csv]")

    ctx.adv = False
    run("q.good", "good-questions.csv", QUESTIONS_CSV, task="import:questions")
    ctx.adv = True
    run("q.missing_header", "q-no-type.csv",
        "Short Name,Question Text\nx,Why?\n", task="import:questions")
    run("q.blank_short_name", "q-blank.csv",
        "Short Name,Feedback Type,Question Text\n,numerical_response,Why?\n",
        task="import:questions")
    run("q.duplicate_short_name", "q-dupe.csv",
        "Short Name,Feedback Type,Question Text\n"
        "property_outcome,numerical_response,First wording\n"
        "property_outcome,numerical_response,Second wording\n",
        task="import:questions")
    run("q.unknown_type", "q-unknown.csv",
        "Short Name,Feedback Type,Question Text\n"
        "mystery,interpretive_dance,What do you feel?\n",
        task="import:questions")
    # The questions side of loc.unknown_column: an extra header that is not a
    # column on Question either.
    run("q.unknown_column", "q-unknown-col.csv",
        "Short Name,Feedback Type,Question Text,Notes\n"
        "extra_key,numerical_response,Does the extra key survive?,ignored\n",
        task="import:questions")
    run("q.unknown_column_invalid", "q-unknown-col-blank.csv",
        "Short Name,Feedback Type,Question Text,Notes\n"
        ",numerical_response,No short name on this row,ignored\n",
        task="import:questions")
    run("q.update_existing", "q-update.csv",
        "Short Name,Feedback Type,Question Text\n"
        "property_outcome,numerical_response,Reworded on import\n",
        task="import:questions")

    # The read-back is what the pages look like after all of that, which is
    # an ordinary render of extraordinary data.
    ctx.adv = False
    ctx.get("import.locations_json_after", "/locations.json", want="json")
    ctx.get("import.numerical_after", "/numerical_answers")
    ctx.get("import.export_after", "/export.csv", want="csv")
    ctx.get("import.status_after", "/.well-known/status")


# --------------------------------------------------------------------------
# world 7 -- the status endpoint's outbound dependency
# --------------------------------------------------------------------------

def world_status(ctx):
    ctx.stub.set_mode("healthy")
    ctx.get("status.no_db_healthy_dep", "/.well-known/status")

    seed_from_repo(ctx)
    ctx.get("status.db_healthy_dep", "/.well-known/status")

    with ctx.adversarial():
        ctx.stub.set_mode("unhealthy")
        ctx.get("status.db_dep_without_marker", "/.well-known/status")

        ctx.stub.set_mode("error")
        ctx.get("status.db_dep_500", "/.well-known/status")

        ctx.stub.set_mode("empty")
        ctx.get("status.db_dep_empty_body", "/.well-known/status")

        ctx.stub.set_mode("reset")
        ctx.get("status.db_dep_connection_reset", "/.well-known/status")

        # Recovery after the failures is itself the exhaustion-and-recovery
        # case, not a fresh happy path.
        ctx.stub.set_mode("healthy")
        ctx.get("status.recovered", "/.well-known/status")

        # The endpoint is a GET; everything else about it is a 404 or a 405.
        ctx.post("status.post", "/.well-known/status")
        ctx.get("status.trailing_slash", "/.well-known/status/")
        ctx.get("status.json_suffix", "/.well-known/status.json")


# --------------------------------------------------------------------------
# world 8 -- HTTP protocol edges
# --------------------------------------------------------------------------

def world_http_edges(ctx):
    seed_from_repo(ctx)
    # The whole world is protocol edges: content negotiation, query shapes,
    # path spellings, verbs the routes do not declare.  HEAD and `Accept: */*`
    # are in there because a browser sends them, but they are still the
    # negotiation path rather than the page, so they stay classified with it.
    ctx.adv = True

    ctx.http("head_root", "HEAD", "/")
    ctx.http("head_locations_json", "HEAD", "/locations.json")

    ctx.get("accept_json_on_html", "/locations",
            headers={"Accept": "application/json"})
    ctx.get("accept_nonsense", "/locations",
            headers={"Accept": "application/x-does-not-exist"})
    ctx.get("accept_wildcard", "/locations", headers={"Accept": "*/*"})
    ctx.get("format_unknown", "/locations.xyz")
    ctx.get("format_csv_on_html_only", "/locations.csv")
    ctx.get("format_json_on_export", "/export.json")

    # The query and path shapes are asked of `/locations.json`, and the two
    # path cases that need the path spelling itself are asked of `/locations`
    # with a JSON Accept.  Not of `/locations` as HTML, because there is no
    # `index.html.erb` in this repository: every one of these would record the
    # identical 500 page, so thirteen cases named for thirteen different edges
    # would be thirteen copies of one observation and could not distinguish
    # anything.  Against the GeoJSON the endpoint does render, a query shape
    # that starts raising, or stops parsing, or arrives as a different value,
    # changes the answer -- so each case can fail for the reason it is named
    # after.
    #
    # The format-negotiation cases above are deliberately left at `/locations`
    # as HTML: the missing template IS their subject, and one of them is a
    # must-flip.
    ctx.get("query_repeated", "/locations.json?page=1&page=2")
    ctx.get("query_array", "/locations.json?ids[]=1&ids[]=2")
    ctx.get("query_nested", "/locations.json?a[b][c]=1")
    ctx.get("query_bad_escape", "/locations.json?q=%zz")
    ctx.get("query_percent", "/locations.json?q=100%25")
    ctx.get("query_long", "/locations.json?q=" + "x" * 4000)
    ctx.get("query_unicode", "/locations.json?q=caf%C3%A9")
    ctx.get("query_null_byte", "/locations.json?q=a%00b")
    ctx.get("query_deep_nesting", "/locations.json?" + "&".join(
        "k%d=v" % i for i in range(120)))

    as_json = {"Accept": "application/json"}
    ctx.get("path_double_slash", "//locations", headers=as_json)
    ctx.get("path_dot_segments", "/locations/../locations", headers=as_json)
    ctx.get("path_encoded_slash", "/locations%2F1", headers=as_json)
    ctx.get("path_trailing_slash", "/locations/", headers=as_json)
    ctx.get("path_uppercase", "/LOCATIONS", headers=as_json)

    ctx.http("method_put_root", "PUT", "/")
    ctx.http("method_delete_location", "DELETE", "/locations/1")
    ctx.http("method_options_root", "OPTIONS", "/")
    ctx.post("method_override_delete", "/locations/1", {"_method": "delete"})

    ctx.post("post_large_body", "/subscription",
             {"subscription[email]": "a" * 20000 + "@example.com",
              "subscription[location_id]": "1"})
    ctx.http("post_json_body", "POST", "/subscription",
             body='{"subscription":{"email":"j@example.com",'
                  '"location_id":1}}',
             headers={"Content-Type": "application/json"}, encode="raw")
    ctx.http("post_wrong_content_type", "POST", "/subscription",
             body="subscription[email]=x@example.com",
             headers={"Content-Type": "application/octet-stream"},
             encode="raw")

    ctx.get("host_header_odd", "/", headers={"Host": "evil.example.com"})
    ctx.get("x_forwarded_proto", "/",
            headers={"X-Forwarded-Proto": "https"})
    ctx.get("if_none_match", "/", headers={"If-None-Match": '"nope"'})
    ctx.get("accept_encoding_gzip", "/",
            headers={"Accept-Encoding": "gzip"})
    ctx.get("bad_cookie", "/",
            headers={"Cookie": "_automidnight_session=garbage"})


# --------------------------------------------------------------------------
# world 9 -- the whole application behind HTTP basic auth
# --------------------------------------------------------------------------

def world_locked(ctx):
    seed_from_repo(ctx)

    # Unauthenticated requests against a locked application are rejections,
    # and rejections are edge inputs.
    with ctx.adversarial():
        ctx.get("locked.root", "/")
        ctx.get("locked.locations", "/locations")
        ctx.get("locked.status", "/.well-known/status")

    ok = {"Authorization": "Basic Y2l0eXZvaWNlOmxldG1laW4="}        # cityvoice:letmein
    bad = {"Authorization": "Basic Y2l0eXZvaWNlOndyb25n"}           # cityvoice:wrong
    junk = {"Authorization": "Basic !!!not-base64!!!"}
    bearer = {"Authorization": "Bearer abcdef"}

    ctx.get("locked.root_authed", "/", headers=ok)
    with ctx.adversarial():
        ctx.get("locked.root_bad_password", "/", headers=bad)
        ctx.get("locked.root_junk_auth", "/", headers=junk)
        ctx.get("locked.root_bearer", "/", headers=bearer)
    ctx.get("locked.locations_json_authed", "/locations.json", headers=ok,
            want="json")
    ctx.get("locked.status_authed", "/.well-known/status", headers=ok)

    # The Twilio surface inherits the filter too -- the phone call is locked
    # out exactly like a browser is.
    with ctx.adversarial():
        ctx.twilio("locked.call", "/calls", call_params(30, "+15105550130"))
    ctx.http("locked.call_authed", "POST", "/calls",
             params=call_params(30, "+15105550130"), headers=ok, want="xml")


# --------------------------------------------------------------------------
# world 10 -- the schema built the way a fresh deployment builds it
# --------------------------------------------------------------------------
#
# Every other world loads `db/schema.rb`, which is how the suite stays fast and
# how CityVoice's own Procfile release step works.  Nothing then ever executes
# db/migrate: 43 files and a third of the repository's Ruby, including the one
# migration that calls application models (`Answer`, `Caller`, `Call`,
# `Location`) from inside a migration -- the single most version-sensitive
# thing in the tree.  Leaving them out would mean that a migration deleting all
# 43 files still passed.

MIGRATE_REVERSIBLE = "20140331070507"  # RenameNotificationSubscriptionsToLocationSubscriptions


def prepare_migrate(ctx):
    """Tear the database down to nothing and rebuild it from the migrations.

    This runs before the world's app server starts: `db:drop` cannot take the
    database away from a unicorn that is holding connections to it.
    """
    ctx.rake("migrate.drop", "db:drop")
    ctx.rake("migrate.create", "db:create")

    with ctx.adversarial():
        # Both tasks against a database with no schema_migrations table at all.
        ctx.rake("migrate.status_before", "db:migrate:status",
                 project=norm.project_migrate_status)
        ctx.rake("migrate.version_before", "db:version",
                 project=norm.project_db_version)

    # Up to 7.2 this runs the 44 files under db/migrate/ and prints a banner
    # for each.  From 8.0 it does not: `db:migrate` initialises an
    # uninitialised database from `db/schema.rb` first and then finds nothing
    # pending, so the event stream is empty and the database is the dump's.
    # Both readings are kept -- the step 10 overlay re-records this case -- and
    # the silence is better evidence of the release than the banners were,
    # because no 7.2 tree can produce it.  The construction the two-database
    # comparison needs is taken in `test.sh`, which moves the dump aside first.
    ctx.rake("migrate.up", "db:migrate", timeout=1800,
             project=norm.project_migration)
    ctx.rake("migrate.status_after", "db:migrate:status",
             project=norm.project_migrate_status)
    ctx.rake("migrate.version_after", "db:version",
             project=norm.project_db_version)

    with ctx.adversarial():
        # A second `db:migrate` over a fully migrated schema must be a no-op.
        ctx.rake("migrate.up_again", "db:migrate",
                 project=norm.project_migration)

        # Reverse one `change` migration and put it straight back.  This is the
        # only path in the tree that runs Rails' command recorder backwards,
        # and `rename_table` is the operation whose inverse it has to know.
        ctx.rake("migrate.down_one", "db:migrate:down VERSION=%s"
                 % MIGRATE_REVERSIBLE, project=norm.project_migration)
        ctx.rake("migrate.status_with_hole", "db:migrate:status",
                 project=norm.project_migrate_status)
        ctx.rake("migrate.up_one", "db:migrate:up VERSION=%s"
                 % MIGRATE_REVERSIBLE, project=norm.project_migration)

        # The newest migration raises IrreversibleMigration by design, so a
        # rollback stops there and PostgreSQL's transactional DDL rolls the
        # attempt back whole.  That refusal is the behaviour under test; what
        # must not change is that it happens and that it changes nothing.
        ctx.rake("migrate.rollback", "db:rollback",
                 project=norm.project_migration)
        ctx.rake("migrate.down_to_zero", "db:migrate VERSION=0",
                 project=norm.project_migration)
        ctx.rake("migrate.up_unknown", "db:migrate:up VERSION=99999999999999",
                 project=norm.project_migration)

    ctx.rake("migrate.status_final", "db:migrate:status",
             project=norm.project_migrate_status)
    ctx.rake("migrate.version_final", "db:version",
             project=norm.project_db_version)


def world_migrate(ctx):
    """The application served off the database `prepare_migrate` left behind.

    Up to 7.2 that is a schema no `db:schema:load` ever touched.  From 8.0
    `db:migrate` loads the dump into an uninitialised database itself, so the
    two constructions meet here; what the world still checks either way is
    that the application serves correctly off whatever `db:migrate` built,
    including the column the 44th migration widens.
    """
    seed_from_repo(ctx)
    run_ivr(ctx, "migrate.call", 40, "+15105550140",
            digits_location="1", answer_digits="1", recording_index=40)
    read_back(ctx, "migrate")
    ctx.get("migrate.status_endpoint", "/.well-known/status")


# --------------------------------------------------------------------------

WORLDS = [
    World("empty", world_empty),
    World("seeded", world_seeded),
    World("ivr_happy", world_ivr_happy),
    World("ivr_adversarial", world_ivr_adversarial),
    World("subscriptions", world_subscriptions),
    World("imports", world_imports),
    World("status", world_status),
    World("http_edges", world_http_edges),
    World("locked", world_locked, env={
        "LOCK_CITYVOICE": "true",
        "CITYVOICE_LOCK_USERNAME": "cityvoice",
        "CITYVOICE_LOCK_PASSWORD": "letmein",
    }),
    World("migrate", world_migrate, prepare=prepare_migrate),
]
