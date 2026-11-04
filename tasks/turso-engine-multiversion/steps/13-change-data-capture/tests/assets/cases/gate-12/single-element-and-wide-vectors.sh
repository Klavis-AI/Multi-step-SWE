
# R12.3, R12.4 and R12.11: the stored length of a one-element and of an
# eight-element float32 vector (four bytes per element, so 4 and 32), the text
# vector_extract renders them back as, and the distance over the whole vector
# rather than over its first element.
# The layout has to hold both ends of the range it admits: a one-element
# vector, and a vector wide enough that its bytes dominate the row. A float32
# element is four bytes, so the stored length pins the element width as well
# as the dimension count -- and the distance over the whole vector has to be
# the sum of the squares, not of the first element.
#
# No stock sqlite3 cross-read, and none is possible: the vector functions are
# engine surface with no counterpart in stock sqlite3 (spec/PROFILE.md rule 9),
# which refuses the call with "no such function", so there is no second opinion
# to compare this against. What stands in for one is the contract and
# arithmetic: the lengths and the extracted text follow from the layout
# TEST_INTERFACE.md's "Vector values at this checkpoint" states, and the two
# rounded distances are roots of 204 and of 2, both exactly representable sums
# that section names.
set -u
set -o pipefail
"$CLI" -m "$CLI_MODE" -q main.db <<'SQL' | sed '/^[[:space:]]*$/d'
SELECT length(vector('[7]')), vector_extract(vector('[7]'));
SELECT vector_distance_l2(vector('[7]'), vector('[7]'));
SELECT length(vector('[1,2,3,4,5,6,7,8]')), vector_extract(vector('[1,2,3,4,5,6,7,8]'));
SELECT round(vector_distance_l2(vector('[1,2,3,4,5,6,7,8]'), vector('[0,0,0,0,0,0,0,0]')), 6);
SELECT round(vector_distance_l2(vector('[0.5,-0.5]'), vector('[-0.5,0.5]')), 6);
CREATE TABLE w(id INTEGER PRIMARY KEY, e BLOB);
INSERT INTO w VALUES(1, vector('[7]')), (2, vector('[1,2,3,4,5,6,7,8]'));
SELECT id, length(e), vector_extract(e) FROM w ORDER BY id;
SELECT round(vector_distance_l2(e, vector('[0,0,0,0,0,0,0,0]')), 6) FROM w WHERE id = 2;
SELECT count(*) FROM w;
SQL
