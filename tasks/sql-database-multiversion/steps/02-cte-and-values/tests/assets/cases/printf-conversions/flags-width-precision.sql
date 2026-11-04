SELECT printf('[%5d][%-5d][%05d]', 42, 42, 42);
SELECT printf('[%+d][% d][%+d]', 5, 5, -5);
SELECT printf('[%8.3s][%-8s][%8s]', 'abcdef', 'ab', 'ab');
SELECT printf('[%6.2f][%-8.2f]', 2.5, 2.5);
