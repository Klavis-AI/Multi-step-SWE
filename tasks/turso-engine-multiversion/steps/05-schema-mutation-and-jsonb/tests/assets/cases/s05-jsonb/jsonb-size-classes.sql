-- R05.9: the payload size classes -- a payload short enough to be counted in
-- the header's own size nibble, and the 1-, 2- and 4-byte size fields above it.
--
-- length() of the blob is the observation, and each boundary is crossed by the
-- smallest step that crosses it: 11 payload bytes against 12, 255 against 256,
-- and 65535 against 65536. The blob is 1 byte longer than its payload while the
-- size still fits the nibble, 2 bytes longer once a 1-byte field is needed, 3
-- once a 2-byte field is, and 5 once a 4-byte field is -- so each result names
-- the width of the size field, not just the length of the string. Taking the
-- pairs one byte apart is what makes the boundary itself the observation: an
-- encoder that switched to the wider field one byte early or one byte late
-- would still produce the right answer for payloads further from the edge.
-- The last line shows jsonb() returning storage class blob at all.
--
-- The 8-byte class is not reachable here and is not part of R05.9; see
-- TEST_INTERFACE.md, "What this milestone does not include".
SELECT length(jsonb('"01234567890"')), length(jsonb('"012345678901"'));
SELECT length(jsonb(printf('"%s"', substr(replace(hex(zeroblob(128)),'0','a'),1,255))));
SELECT length(jsonb(printf('"%s"', replace(hex(zeroblob(128)),'0','a'))));
SELECT length(jsonb(printf('"%s"', substr(replace(hex(zeroblob(32768)),'0','a'),1,65535))));
SELECT length(jsonb(printf('"%s"', replace(hex(zeroblob(32768)),'0','a'))));
SELECT typeof(jsonb('{"a":1}')), length(jsonb('{"a":1}'));
