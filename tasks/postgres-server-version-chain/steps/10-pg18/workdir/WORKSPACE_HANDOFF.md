# Workspace handoff

`/app` is carried forward from one milestone to the next, and each milestone is
worked by a fresh session that has seen none of the others. Leave the workspace
in a state the next session can pick up:

* the source of the server, and a `Makefile` whose default target builds
  `bin/postgres` and `bin/initdb`;
* `NOTES.md` -- what is implemented, what is not, and where things live;
* nothing that only works because of state in your shell.

**Leave the workspace source-only, and leave only text files in it.** `/app` is
archived as a text stream when your session ends, and a file that is not text
does not survive the trip: it takes the rest of the archive with it, and the
next session receives a workspace with files missing. So before you finish:

    make -C /app clean        # or: rm -f /app/src/*.o /app/src/*.d
    rm -rf /app/bin           # make rebuilds it from source
    rm -rf <any data directory you created under /app>

`bin/`, object files, a data directory, a core file, a compiled dependency, an
archive: all of these are build products, none of them belong in `/app`, and
every build creates its own. Keep the source, the `Makefile`, `NOTES.md`, the
documents you were given, and nothing else.

One thing about the makefile itself, because the failure it causes does not look
like a build failure. **The server must work when built from clean; your
incremental build is not clean; and this milestone changes headers.** Each
milestone adds fields to structures and values to enumerations, and if your
makefile does not track header dependencies, a header change recompiles nothing:
object files built against the old layout link happily with ones built against
the new one. The result is not a link error, it is a server that returns wrong
answers or crashes somewhere unrelated to what you changed, and it costs hours.
Check that rule before you trust an incremental build. With GCC or Clang the
whole fix is to compile with `-MMD -MP` and include the generated `.d` files:

    CPPFLAGS += -MMD -MP
    -include $(OBJS:.o=.d)
