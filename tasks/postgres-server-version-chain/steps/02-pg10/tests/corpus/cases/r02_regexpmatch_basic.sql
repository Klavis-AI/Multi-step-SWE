SELECT regexp_match('foobarbaz', '(b..)(b..)');
SELECT regexp_match('foobarbaz', 'nope');
SELECT regexp_match('foobarbaz', 'b.z');
SELECT regexp_match('ABC', 'abc', 'i');
SELECT (regexp_match('2020-05-06', '(\d+)-(\d+)-(\d+)'))[2];
