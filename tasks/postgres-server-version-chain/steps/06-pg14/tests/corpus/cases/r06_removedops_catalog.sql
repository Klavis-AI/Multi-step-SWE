SELECT count(*) FROM pg_operator WHERE oprname = '@' AND oprleft = 'box'::regtype;
SELECT count(*) FROM pg_operator WHERE oprname = '~' AND oprleft = 'box'::regtype;
SELECT count(*) FROM pg_operator WHERE oprname = '@>' AND oprleft = 'box'::regtype;
SELECT oprname, oprkind FROM pg_operator
  WHERE oprname IN ('@','~','@>','<@') AND oprleft='circle'::regtype ORDER BY oprname;
