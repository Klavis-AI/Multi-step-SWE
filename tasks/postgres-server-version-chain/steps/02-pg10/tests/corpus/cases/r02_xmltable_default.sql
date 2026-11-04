SELECT x.n, x.v, x.d FROM xmltable('/r/row' PASSING
  XMLPARSE(DOCUMENT '<r><row><v>a</v></row><row></row></r>')
  COLUMNS n FOR ORDINALITY, v text PATH 'v', d text PATH 'missing' DEFAULT 'dflt') AS x
  ORDER BY x.n;
SELECT x.v FROM xmltable('/r/row' PASSING XMLPARSE(DOCUMENT '<r><row><v>7</v></row></r>')
  COLUMNS v int PATH 'v' NOT NULL) AS x;
SELECT x.v FROM xmltable('/r/row' PASSING XMLPARSE(DOCUMENT '<r><row/></r>')
  COLUMNS v int PATH 'v' NOT NULL) AS x;
