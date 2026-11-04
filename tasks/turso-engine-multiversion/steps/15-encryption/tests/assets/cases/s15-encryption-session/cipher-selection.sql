-- R15.11: the cipher pragma's set form and its query form, and that the
-- name it reports back is the engine's canonical spelling rather than the
-- spelling that selected it.
-- Encryption is opt-in and is configured per session through two pragmas.
-- Querying the cipher pragma with no argument reports the cipher selected for
-- the session, and the reported name is the engine's canonical spelling, not
-- whatever spelling was used to select it. The second selection below spells
-- the name in upper case for exactly that reason: recognition is
-- case-insensitive, the reply is not, and the two query forms therefore
-- answer with the same lower-case name from two different spellings. A case
-- that had selected `aegis256` twice would have shown nothing about
-- canonicalisation at all.
-- No key is supplied, so the set-once rule R15.13 states is not in play and
-- re-selecting the cipher is allowed here.
PRAGMA cipher = 'aegis256';
PRAGMA cipher;
PRAGMA cipher = 'AEGIS256';
PRAGMA cipher;
SELECT 'session still usable';
