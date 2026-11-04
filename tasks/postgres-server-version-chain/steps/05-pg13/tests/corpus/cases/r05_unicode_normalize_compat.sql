SELECT length(normalize(U&'\FB01', NFKC)), length(U&'\FB01');
SELECT normalize(U&'\FB01', NFKC) = 'fi';
SELECT normalize(U&'\FB01', NFC) = 'fi';
SELECT U&'\FB01' IS NFKC NORMALIZED, U&'\FB01' IS NFC NORMALIZED;
SELECT octet_length(normalize(U&'\00C5', NFD)), octet_length(U&'\00C5');
