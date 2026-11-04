SELECT unistr('bad\');
SELECT unistr('\0');
SELECT unistr('\zz');
SELECT unistr('\+11FFFF');
SELECT unistr(1);
