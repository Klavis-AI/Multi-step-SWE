SELECT encode(convert_to(normalize(U&'\0041\030A'), 'UTF8'), 'hex');
SELECT encode(convert_to(normalize(U&'\0041\030A', NFC),'UTF8'),'hex'), encode(convert_to(normalize(U&'\0041\030A', NFD),'UTF8'),'hex');
SELECT normalize(U&'\00C5', NFD) = U&'\0041\030A';
SELECT length(normalize(U&'\00C5', NFD)), length(U&'\00C5');
SELECT normalize('abc', NFKC), normalize('abc', NFKD);
