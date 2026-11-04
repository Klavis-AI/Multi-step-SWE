# Step 6 — The text is repaired

## 1. Implement this milestone

### The deployment as it stands

The deployment you inherit runs entirely on PostgreSQL 17. MySQL is gone, the
schema is PostgreSQL's, every query is PostgreSQL's, and the server, the
client and every text column are UTF-8. The move is finished.

What the move did not do is *look at* the text it carried. It preserved every
stored byte exactly, which was the right thing for it to do — and a faithful
carry of a damaged value is still a damaged value. Some of the text in this database went through a
latin1/UTF-8 round trip before it ever reached the migration, back when a
MySQL client and a MySQL column disagreed about which encoding the connection
was in. `Café` was written as UTF-8, read back one byte at a time as if it
were latin1, and stored as the five characters that reading produced. The
database now holds `CafÃ©`, and it holds it as perfectly valid UTF-8, so
nothing in the stack has any reason to complain.

The corpus of a long-lived deployment being what it is, the damage is not
uniform. Some values went through the accident once. Some went through it
twice, and read back as `ÃƒÂ¼` where a `ü` belongs. The client that did it
was not always the same client, so not every value was misread through the
same single-byte codec.

And most of the non-ASCII text in this database is *not* damaged. It is simply
non-ASCII — names, street addresses, contest titles, lesson messages, in Greek,
Japanese, Russian and half a dozen Latin-script languages. That text is
correct, it is what the users typed, and this step must hand it back unchanged.

The same damaged values are in `install/_db/pdo_pgsql.data.sql`, the checked-in
data the installer applies to a fresh database, because that file was generated
from this database.

### What must change

**No value in the database carries a double-encoding signature any more, and
every damaged value has been replaced by the text it was meant to be.** Not
deleted, not blanked, not replaced with a substitute character: recovered. The
information is still there — a latin1/UTF-8 round trip is lossless whenever
the misreading succeeded, which is why the damage is repairable at all.

Some of the damaged values are PHP `serialize()` output, whose `s:<n>:`
prefixes count the bytes of the string that follows. A repaired one may keep
the prefix it carries now or state its repaired string's own length; either
is the recovered text, as long as the database and the installer data agree.

**The same repair is in the checked-in installer data.** A fresh install from
`install/_db/` must produce the repaired text, not the damaged text. The
installer data and the live database have to hold the same text as each other:
one repaired and the other not is half a repair. In the file, each repaired
value is written out as the plain UTF-8 text it now is, and no damaged
spelling survives anywhere in it, comments included.

The file is otherwise what it is today, and the repair goes into it as edits to
its values rather than as a regenerated dump: it is SQL and only SQL; it
applies top to bottom in one pass, in under 300 seconds, to a database whose
tables have just been built or emptied, with every statement succeeding, when
it is run by an ordinary role that owns that database rather than by a
superuser; it applies a second time to the database it just filled without
failing a statement or holding a row twice; `PREFIX_` still stands for the
table prefix in its statements, so it installs at a prefix this deployment
does not use; and it still leaves the generated keys past the rows it loads.

### What must not change

**Text that is not damaged.** This is the larger half of the work and the only
part that can be got wrong silently. Reaching for a blanket conversion of the
whole database — `CONVERT`, `iconv -f ISO-8859-1`, `utf8_decode`, a
re-encode-everything loop — does not do this step. It mangles correct text, and
on this corpus it will also fail outright on values it cannot encode. Among the
things in these columns that a blanket conversion gets wrong:

- Text outside latin1 entirely: Greek, Japanese, Cyrillic. There is no
  single-byte encoding of `日本語` to re-read.
- Text that *looks* like the damage and is not. One value ends in a truncated
  sequence where the second byte of a two-byte character was cut off by a
  column width; another is a legitimate sentence about encodings that contains
  a bare `Ã` and a `©`. Neither is recoverable and neither is damaged. Left
  alone they are exactly what the user typed. "Repaired" they are destroyed.
- A value whose repair needs one of the C1 control characters ISO-8859-1
  proper has at `0x80`–`0x9F`, where MySQL's `latin1` has printable
  punctuation instead, and a value whose repair needs one of that punctuation,
  which ISO-8859-1 does not have. Whichever single codec you pick, some of this
  corpus is outside it.

**Unicode normalisation.** Do not normalise. `jf_users` holds two accounts
whose usernames are the NFC and the NFD spelling of the same word, each with
its own password and its own e-mail address; the column is uniquely indexed.
Normalising that column either raises on the index or silently merges two
users, and there is no rule in this application that says which of them should
survive. Both spellings are correct text and both stay. Repairing an encoding
and normalising Unicode are two different operations and only the first one is
this step.

**Everything structural.** The schema is untouched — same tables, same columns,
same types, same constraints, same sequences. In every table that holds the
application's own content, no row is added or removed, no row count changes,
and no stored value changes other than a damaged one being repaired. Those rows
are this deployment's dataset as it stood before anyone exercised it, not
whatever the database you inherit happens to hold: if testing in an earlier
session left writes behind in it, putting those rows back is restoring the
data, not changing it. The three runtime bookkeeping tables — `jf_stats`,
`jf_session` and `jf_logs` — sit outside that: their extent may change, and
the identity sequence that follows it only has to stay past the table's own
highest key, because `jf_stats` and `jf_session` grow whenever anyone browses
the deployment, including you. So exercise the application as much as you need
to check your own work: the rows browsing adds to those three do not matter,
and there is no need to tidy them afterwards. The rows `jf_stats` and `jf_logs`
held when you arrived stay, in the database and in the installer data, and
they are stored text like any other: a damaged value among them is repaired
too, in both places alike. The application's wiring, the retirement
of MySQL, the installer's schema file and the provisioning recipe all stay
exactly as you inherited them.

**Every SQL statement the application's PHP builds is SQL PostgreSQL 17 would
run.** A statement qualifies when the PostgreSQL 17 grammar parses it and every
function and collation it names exists in the database this deployment uses,
wherever it is in the tree — vendored code for database servers this deployment
has never used included, where a file holding one that does not qualify is
ported or not kept. That goes file by file: a file for another server that
holds no SQL of its own — an exception class, a keyword list, a driver class
with no statement in it — stays. That is a requirement on the tree you hand
on, not only a description of the one you were handed: if you find a statement that does not
qualify, making it qualify is part of this step.

**And the server accepts what the application sends it.** Parsing is not
the whole of running: a statement that qualifies can still be refused when it
executes — a duplicate key it was counting on catching, a `NOT NULL` column it
leaves empty, a value its column's type will not take, an object the
application's role may not alter — and a connection can be refused before any
statement is sent. While the site serves requests, the server must report no
error to any of the application's sessions — at severity `ERROR`, `FATAL` or
`PANIC`, whatever its SQLSTATE class, valid SQL or not — with exactly three
exceptions. In these places a refusal is allowed, though never required:

- the lesson that demonstrates numeric SQL injection, when the station its
  request supplies is neither a number nor a well-formed condition, or is a
  condition that pattern-matches with `LIKE` under a non-deterministic
  collation;
- the business layer access control lesson's login, when the user id its
  request supplies is empty: the lesson compares it with an integer column,
  and PostgreSQL will not read an empty string as an integer;
- the installer (`/init`), run again over the database it has already
  installed: the accounts it creates exist already, so the role assignments it
  writes for them name no user.

**Every page that did not render damaged text.** A page that displayed a
damaged value will display the repaired one, which is the point of the step.
Every other page answers exactly as it did before, byte for byte. A repair that
takes a page down, or that changes a page which had nothing damaged on it, is a
failed repair.

## 2. Read these local references

**Normative semantics:** the PostgreSQL 17 documentation on *Character Set
Support*, `convert_to`/`convert_from` and the `bytea` type; the Unicode
standard's definition of UTF-8 well-formedness; the WHATWG Encoding Standard
and the Windows-1252 and ISO-8859-1 code tables, and MySQL's own documentation
for what it means by the `latin1` character set — the two are not the same
mapping and the difference is load-bearing here. **Local:** the live PostgreSQL
database, which is the authority on what is stored; `/app/install/_db/`, which
has to agree with it; `/app/_japp/model/lib/db/adapter/pdo_pgsql.php` for how
the application connects; and the running application itself, which renders
these columns on the contest, lesson and user pages.

## 3. Meta requirements

- The implementation budget is 7,200 seconds. Check `bash /app/timer.sh`;
  the configured timeout is authoritative and resets for this step.
- Work in the cumulative `/app` workspace and in the live PostgreSQL database,
  and preserve the supplied source and data. Do not create a prose answer or an
  alternate entry point.
- What is kept from this session is `/app`,
  `/var/lib/postgresql` and `/etc/postgresql`, together with `/var/lib/mysql`
  and `/etc/mysql`, which must stay absent or empty: MySQL is retired. Nothing
  else is kept: the Apache and PHP configuration is supplied fresh, so a
  change made there does not carry over.
- Leave Apache and PostgreSQL running, or leave them so that
  `/usr/local/sbin/wg-services start` brings them up.
- Do not cheat by using online solutions or hints specific to this task. Work
  only from this workspace, its servers and the references above; do not
  search the system for other implementations or answers.
- Leave the deployment so that it comes back up from what is kept.
