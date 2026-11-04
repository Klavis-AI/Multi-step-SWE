SELECT casefold('ABC'), casefold('abc'), casefold('');
SELECT casefold('ABC') = lower('ABC');
SELECT casefold(NULL) IS NULL;
SELECT length(casefold('AbCdEf'));
