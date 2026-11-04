# -*- coding: utf-8 -*-
"""Golden solution for rung 02, as a list of byte-anchored edits.

    python3 work/solve02.py <tree>

Applied on top of rung 01's tree. Same shape as ``work/solve01.py`` and for the
same reasons: every edit anchors on the exact bytes it expects, counts them,
and refuses to continue when a count is wrong or a replacement would be a
no-op (INSTRUCTIONS trap 7).

Five edits at three raise sites and two handlers:

  transport.perform_request   the retry loop kept only the last attempt's
  transport.sniff_hosts       failure and dropped the rest; both now raise
  analytics.Consumer.request  every attempt's failure together.

  Elasticsearch.ping          the two handlers that caught those failures are
  helpers.streaming_bulk      rewritten with ``except*``. Neither may keep its
                              old body: PEP 654 forbids ``return``, ``break``
                              and ``continue`` inside an ``except*`` block and
                              those are exactly what both of them used.

The vendored urllib3's ``Retry``/``MaxRetryError`` is deliberately untouched --
see NOTES-authoring.md section 4.7.
"""
from __future__ import print_function

import os
import sys


# (relpath, old, new, count): ``old`` must appear exactly ``count`` times.
EDITS = []


def edit(rel, old, new, count=1):
    EDITS.append((rel, old, new, count))


ES = "lib/elasticsearch"


# ------------------------------------------- 1. the transport retry loop ----
#
# ``for attempt in range(self.max_retries + 1)`` catches a TransportError per
# attempt, decides whether it is worth retrying, and on the last one re-raises
# whatever it is holding. The four earlier failures are gone by then. They are
# collected instead and raised together, the way asyncio.TaskGroup reports the
# failures of the tasks it supervised -- as one group, even when there is only
# one of them, which here is what ``max_retries=0`` produces.
#
# The message is ``str(e)``: the exception the loop re-raises today is the last
# attempt's, so that is the text this site reports today and it does not move.
edit(ES + "/transport.py",
     b"        for attempt in range(self.max_retries + 1):\n",
     b"        failures = []\n"
     b"\n"
     b"        for attempt in range(self.max_retries + 1):\n")
edit(ES + "/transport.py",
     b"                if retry:\n"
     b"                    # only mark as dead if we are retrying\n"
     b"                    self.mark_dead(connection)\n"
     b"                    # raise exception on last retry\n"
     b"                    if attempt == self.max_retries:\n"
     b"                        raise\n",
     b"                if retry:\n"
     b"                    # keep it: the loop used to report the last failure\n"
     b"                    # and drop every earlier one on the floor\n"
     b"                    failures.append(e)\n"
     b"                    # only mark as dead if we are retrying\n"
     b"                    self.mark_dead(connection)\n"
     b"                    # raise them all together on the last retry\n"
     b"                    if attempt == self.max_retries:\n"
     b"                        raise ExceptionGroup(str(e), failures)\n")


# ---------------------------------------------------- 2. sniffing ----------
#
# ``except (ConnectionError, SerializationError): pass`` discards one failure
# per connection and the ``for``/``else`` then reports a TransportError that
# says nothing about any of them. The group keeps them and carries the same
# message the TransportError carried.
#
# The sibling raise a few lines further down -- "Unable to sniff hosts - no
# viable hosts found." -- is not this: nothing failed there, the nodes simply
# did not parse. It stays a plain TransportError.
edit(ES + "/transport.py",
     b"                except (ConnectionError, SerializationError):\n"
     b"                    pass\n"
     b"            else:\n"
     b'                raise TransportError("N/A", "Unable to sniff hosts.")\n',
     b"                except (ConnectionError, SerializationError) as e:\n"
     b"                    failures.append(e)\n"
     b"            else:\n"
     b'                raise ExceptionGroup("Unable to sniff hosts.", failures)\n')
edit(ES + "/transport.py",
     b"            # go through all current connections as well as the\n"
     b"            # seed_connections for good measure\n"
     b"            for c in chain(",
     b"            # go through all current connections as well as the\n"
     b"            # seed_connections for good measure\n"
     b"            failures = []\n"
     b"            for c in chain(")


# ------------------------------------------------------- 3. ping ----------
#
# ``except TransportError: return False``. ``return`` is not allowed in an
# ``except*`` block, so the answer moves into a name and the return moves out.
edit(ES + "/client/__init__.py",
     b"        try:\n"
     b"            self.transport.perform_request('HEAD', '/', params=params)\n"
     b"        except TransportError:\n"
     b"            return False\n"
     b"        return True\n",
     b"        alive = True\n"
     b"        try:\n"
     b"            self.transport.perform_request('HEAD', '/', params=params)\n"
     b"        except* TransportError:\n"
     b"            # PEP 654: no ``return`` inside an except* block.\n"
     b"            alive = False\n"
     b"        return alive\n")


# ------------------------------------------------- 4. streaming_bulk ------
#
# The other handler that caught what the retry loop raised. Three things had to
# move: the bind is now a group, ``raise e`` becomes a bare ``raise`` so that
# the whole group propagates rather than one member of it, and the ``continue``
# that jumped back to the top of the ``while True`` moves out of the block.
#
# Everything the handler reports is unchanged. ``err_message``, ``status`` and
# the ``exception`` it files under each action all come from the last member of
# the group -- which is the exception this handler used to be handed.
edit(ES + "/helpers/__init__.py",
     b"        try:\n"
     b"            # send the actual request\n"
     b"            resp = client.bulk(bulk_actions, **kwargs)\n"
     b"        except TransportError as e:\n"
     b"            # default behavior - just propagate exception\n"
     b"            if raise_on_exception:\n"
     b"                raise e\n",
     b"        chunk_failed = False\n"
     b"        try:\n"
     b"            # send the actual request\n"
     b"            resp = client.bulk(bulk_actions, **kwargs)\n"
     b"        except* TransportError as group:\n"
     b"            # the transport reports every attempt it made; the one this\n"
     b"            # handler used to be handed is the last of them\n"
     b"            e = group.exceptions[-1]\n"
     b"            # default behavior - just propagate exception\n"
     b"            if raise_on_exception:\n"
     b"                raise\n")
edit(ES + "/helpers/__init__.py",
     b"            else:\n"
     b"                for err in exc_errors:\n"
     b"                    yield False, err\n"
     b"                continue\n",
     b"            else:\n"
     b"                for err in exc_errors:\n"
     b"                    yield False, err\n"
     b"                # PEP 654: no ``continue`` inside an except* block.\n"
     b"                chunk_failed = True\n"
     b"        if chunk_failed:\n"
     b"            continue\n")


# ------------------------------------------- 5. the analytics consumer ----
#
# ``Consumer.request`` recurses up to ``self.retries`` times and lets the last
# failure out; the four before it are lost. The recursion becomes a loop so
# that one frame holds the whole list -- the alternative is a third parameter,
# and the method's signature is part of its interface.
#
# ``BaseExceptionGroup`` rather than ``ExceptionGroup`` because the handler it
# replaces was a bare ``except:``. BaseExceptionGroup returns an ExceptionGroup
# when every member is an Exception, so ``upload``'s ``except Exception`` still
# catches an ordinary upload failure; a BaseException still goes straight past
# it, exactly as it does today.
edit("lib/analytics/consumer.py",
     b"    def request(self, batch, attempt=0):\n"
     b'        """Attempt to upload the batch and retry before raising an error """\n'
     b"        try:\n"
     b"            post(self.write_key, batch=batch)\n"
     b"        except:\n"
     b"            if attempt > self.retries:\n"
     b"                raise\n"
     b"            self.request(batch, attempt+1)\n",
     b"    def request(self, batch, attempt=0):\n"
     b'        """Attempt to upload the batch and retry before raising an error """\n'
     b"        failures = []\n"
     b"        while True:\n"
     b"            try:\n"
     b"                post(self.write_key, batch=batch)\n"
     b"                return\n"
     b"            except BaseException as e:\n"
     b"                failures.append(e)\n"
     b"                if attempt > self.retries:\n"
     b"                    raise BaseExceptionGroup(str(e), failures)\n"
     b"                attempt += 1\n")


def main(argv):
    if len(argv) != 2:
        raise SystemExit("usage: solve02.py <tree>")
    tree = os.path.abspath(argv[1])

    applied = 0
    for rel, old, new, count in EDITS:
        path = os.path.join(tree, rel)
        with open(path, "rb") as fh:
            src = fh.read()
        n = src.count(old)
        if n != count:
            raise SystemExit("%s: anchor appears %d times, expected %d:\n%r"
                             % (rel, n, count, old[:120]))
        out = src.replace(old, new)
        if out == src:
            raise SystemExit("%s: edit changed nothing" % rel)
        with open(path, "wb") as fh:
            fh.write(out)
        applied += 1

    print("solve02: %d edits" % applied)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
